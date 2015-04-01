/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013 Sandia Corporation. All rights reserved.
 * Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
 * license for use of this work by or on behalf of the U.S. Government.
 * Export of this program may require a license from the United States
 * Government.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the BSD-type
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *      Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *      Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 *      Neither the name of Sandia nor the names of any contributors may
 *      be used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 *      Neither the name of Open Grid Computing nor the names of any
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *      Modified source versions must be plainly marked as such, and
 *      must not be misrepresented as being the original software.
 *
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "bout_sos_img.h"
#include "sos_img_class_def.h"

int bout_sos_img_config(struct bplugin *this, struct bpair_str_head *cfg_head)
{
	struct bout_sos_img_plugin *_this = (void*)this;
	int rc = 0;
	char *tmp = NULL;
	char buff[64];
	struct bpair_str KV = {.s0 = "store_name", .s1 = buff};
	struct bpair_str *kv;
	_this->delta_node = 1;
	_this->delta_ts = 3600;
	if ((kv = bpair_str_search(cfg_head, "delta_ts", NULL))) {
		_this->delta_ts = strtoul(kv->s1, NULL, 0);
	}
	if ((kv = bpair_str_search(cfg_head, "delta_node", NULL))) {
		_this->delta_node = strtoul(kv->s1, NULL, 0);
	}
	snprintf(buff, sizeof(buff), "%d-%d", _this->delta_ts, _this->delta_node);
	kv = bpair_str_search(cfg_head, "store_name", NULL);
	if (kv) {
		/* store_name should be <delta_ts>-<delta_node> */
		bwarn("bout_sos_img should not have store_name option set, "
				"forcing following name convention: %s", buff);
		tmp = kv->s1;
		kv->s1 = buff;
	} else {
		kv = &KV;
		LIST_INSERT_HEAD(cfg_head, kv, link);
	}
	rc = bout_sos_config(this, cfg_head);

cleanup:
	if (tmp)
		kv->s1 = tmp;
	else
		LIST_REMOVE(kv, link);
	return rc;
}

int bout_sos_img_start(struct bplugin *this)
{
	struct bout_sos_img_plugin *_this = (void*)this;
	int rc;
	if ((rc = bout_sos_start(this)))
		return rc;
	_this->sos_iter = sos_iter_new(_this->base.sos, 0);
	if (!_this->sos_iter) {
		rc = errno;
		bout_sos_stop(this);
		return rc;
	}
	return 0;
}

static
void rotate_cb(struct bout_sos_plugin *p)
{
	struct bout_sos_img_plugin *_this = (void*)p;
	if (_this->sos_iter)
		sos_iter_free(_this->sos_iter);
	_this->sos_iter = sos_iter_new(p->sos, 0);
}

int bout_sos_img_process_output(struct boutplugin *this,
		struct boutq_data *odata)
{
	int rc = 0;
	struct bout_sos_plugin *_base = (void*)this;
	struct bout_sos_img_plugin *_this = (typeof(_this))this;
	uint32_t *tmp;
	obj_key_t ok = NULL;
	obj_ref_t ref;
	pthread_mutex_lock(&_base->sos_mutex);
	sos_iter_t iter;
	sos_t sos;
	if (!_base->sos) {
		rc = EBADF;
		goto err0;
	}

	bout_sos_rotate(_base, odata->tv.tv_sec, rotate_cb);

	if (!_this->sos_iter) {
		rc = EBADF;
		goto err0;
	}

	sos = _base->sos;
	iter = _this->sos_iter;

	sos_attr_t attr = sos_obj_attr_by_id(sos, 0);
	if (!attr) {
		rc = EBADF;
		goto err0;
	}

	struct bout_sos_img_key bk = {
		.sos_blob = {.len = sizeof(bk) - sizeof(bk.sos_blob)},
		.comp_id = odata->comp_id,
		.ts = (odata->tv.tv_sec / _this->delta_ts) * _this->delta_ts,
		.ptn_id = odata->msg->ptn_id,
	};
	bout_sos_img_key_convert(&bk);
	ok = obj_key_new(sizeof(bk));
	if (!ok) {
		rc = ENOMEM;
		goto err0;
	}
	obj_key_set(ok, &bk, sizeof(bk));
	uint32_t count = 1;
	if (0 == sos_iter_seek(iter, ok)) {
		/* found, increment the couter */
		tmp = sos_obj_attr_get(sos, SOS_IMG_COUNT, sos_iter_obj(iter));
		(*tmp)++;
		goto out;
	}
	/* reaching here means not found, add new data */
	sos_obj_t obj;
	obj = sos_obj_new(sos);
	if (!obj) {
		bwarn("bout_sos_img: cannot alloce new sos obj,"
						" errno(%d): %m", errno);
		goto err0;
	}
	ref = sos_obj_to_ref(sos, obj);
	sos_obj_attr_set(sos, SOS_IMG_KEY, sos_ref_to_obj(sos, ref), &bk);
	sos_obj_attr_set(sos, SOS_IMG_COUNT, sos_ref_to_obj(sos, ref), &count);
	rc = sos_obj_add(sos, sos_ref_to_obj(sos, ref));
	if (rc) {
		bwarn("bout_sos_img: sos_obj_add() failed, rc: %d", rc);
		goto err1;
	}
	goto out;

err1:
	sos_obj_delete(sos, obj);
err0:
out:
	if (ok)
		obj_key_delete(ok);
	pthread_mutex_unlock(&_base->sos_mutex);
	return rc;
}

int bout_sos_img_stop(struct bplugin *this)
{
	struct bout_sos_img_plugin *_this = (typeof(_this))this;
	pthread_mutex_lock(&_this->base.sos_mutex);
	sos_iter_free(_this->sos_iter);
	_this->sos_iter = 0;
	pthread_mutex_unlock(&_this->base.sos_mutex);
	return bout_sos_stop(this);
}

struct bplugin *create_plugin_instance()
{
	struct bout_sos_img_plugin *_p = calloc(1, sizeof(*_p));
	_p->base.sos_class = &sos_img_class;
	struct boutplugin *p = (typeof(p)) _p;
	bout_sos_init((void*)_p, "bout_sos_img");
	p->process_output = bout_sos_img_process_output;
	/* override some functions */
	p->base.config = bout_sos_img_config;
	p->base.start = bout_sos_img_start;
	p->base.stop = bout_sos_img_stop;
	return (void*)p;
}
