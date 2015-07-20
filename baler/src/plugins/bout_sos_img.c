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
	sos_t sos;
	sos_attr_t ptn_id_attr;
	int rc;
	if ((rc = bout_sos_start(this)))
		return rc;
	sos = _this->base.sos;
 retry:
	_this->img_index = sos_index_open(sos, BOUT_SOS_IDX_NAME);
	if (!_this->img_index) {
		rc = sos_index_new(sos, BOUT_SOS_IDX_NAME, "BXTREE", "UINT96", 5);
		if (!rc)
			goto retry;
		else
			return errno;
	}
	return 0;
}

static
void rotate_cb(struct bout_sos_plugin *p)
{
}

int bout_sos_img_process_output(struct boutplugin *this,
		struct boutq_data *odata)
{
	int rc = 0;
	struct bout_sos_plugin *_base = (void*)this;
	struct bout_sos_img_plugin *_this = (typeof(_this))this;
	sos_obj_t obj;
	sos_array_t img;
	SOS_KEY(ok);

	pthread_mutex_lock(&_base->sos_mutex);

	if (!_base->sos) {
		rc = EBADF;
		goto out;
	}

	bout_sos_rotate(_base, odata->tv.tv_sec, rotate_cb);

	if (!_this->img_index) {
		rc = EBADF;
		goto out;
	}

	struct bout_sos_img_key bk = {
		.comp_id = odata->comp_id,
		.ts = (odata->tv.tv_sec / _this->delta_ts) * _this->delta_ts,
		.ptn_id = odata->msg->ptn_id,
	};
	bout_sos_img_key_convert(&bk);
	sos_key_set(ok, &bk, sizeof(bk));

	obj = sos_index_find(_this->img_index, ok);
	if (obj) {
		img = sos_obj_ptr(obj);
		img->data.uint32_[BOUT_IMG_COUNT] ++;
		sos_obj_put(obj);
		goto out;
	}
	/* reaching here means not found, add new data */
	obj = sos_array_obj_new(_this->base.sos, SOS_TYPE_UINT32_ARRAY, 4);
	if (!obj) {
		bwarn("bout_sos_img: cannot alloce new sos obj,"
						" errno(%d): %m", errno);
		goto out;
	}
	img = sos_obj_ptr(obj);
	img->data.uint32_[BOUT_IMG_PTN_ID]	= bk.ptn_id;
	img->data.uint32_[BOUT_IMG_SEC]		= bk.ts;
	img->data.uint32_[BOUT_IMG_COMP_ID]	= bk.comp_id;
	img->data.uint32_[BOUT_IMG_COUNT]	= 1;

	rc = sos_index_insert(_this->img_index, ok, obj);
	if (rc) {
		bwarn("bout_sos_img: sos_obj_add() failed, rc: %d", rc);
		sos_obj_delete(obj);
	}

out_1:
	sos_obj_put(obj);
out:
	pthread_mutex_unlock(&_base->sos_mutex);
	return rc;
}

int bout_sos_img_stop(struct bplugin *this)
{
	struct bout_sos_img_plugin *_this = (typeof(_this))this;
	pthread_mutex_lock(&_this->base.sos_mutex);
	sos_index_close(_this->img_index, SOS_COMMIT_SYNC);
	_this->img_index = NULL;
	pthread_mutex_unlock(&_this->base.sos_mutex);
	return bout_sos_stop(this);
}

struct bplugin *create_plugin_instance()
{
	struct bout_sos_img_plugin *_p = calloc(1, sizeof(*_p));
	struct boutplugin *p = (typeof(p)) _p;
	bout_sos_init((void*)_p, "bout_sos_img");
	p->process_output = bout_sos_img_process_output;
	/* override some functions */
	p->base.config = bout_sos_img_config;
	p->base.start = bout_sos_img_start;
	p->base.stop = bout_sos_img_stop;
	return (void*)p;
}
