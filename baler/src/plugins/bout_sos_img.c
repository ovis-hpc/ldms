/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-16 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013-16 Sandia Corporation. All rights reserved.
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

/**
 * \page bout_sos_img.config SOS image output configuration
 *
 *
 * \section synopsis SYNOPSIS
 *
 * <b>plugin name=bout_sos_img</b> [delta_ts=NUM] [delta_node=NUM]
 *
 *
 * \section description DESCRIPTION
 *
 * \b bout_sos_img output plugin stores occurrences of each pattern from
 * Baler daemon output stream in \b delta_ts x \b delta_node time-node slots.
 * This data can later be used in event occurrence visualization and association
 * rule mining. This data is also accessible via \b bquery.
 *
 * It is advisable to have 3 \b bout_sos_img instances with \b delta_ts of 60,
 * 3600, 86400. It is also advisable that \b delta_node is 1 (default).
 *
 *
 * \section option OPTIONS
 *
 * \par delta_ts=NUM
 * Assign \b delta_ts value for the plugin. (deault: 3600)
 *
 * \par delta_node=NUM
 * Assign \b delta_node value for the plugin. (default: 1)
 *
 *
 * \section examples EXAMPLES
 * <pre>
 * plugin name=bout_sos_img delta_ts=60
 * plugin name=bout_sos_img delta_ts=3600
 * plugin name=bout_sos_img delta_ts=86400
 * </pre>
 */

#include "bout_sos_img.h"
#include "bsos_img.h"

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

int bout_sos_img_process_output(struct boutplugin *this,
		struct boutq_data *odata)
{
	int rc = 0;
	struct bout_sos_plugin *_base = (void*)this;
	struct bout_sos_img_plugin *_this = (typeof(_this))this;
	bsos_img_t bsos_img = _base->bsos_handle;
	sos_obj_t obj;
	sos_array_t img;
	SOS_KEY(ok);

	pthread_mutex_lock(&_base->sos_mutex);

	if (!bsos_img) {
		rc = EBADF;
		goto out;
	}

	struct bsos_img_key bk = {
		.comp_id = odata->comp_id,
		.ts = (odata->tv.tv_sec / _this->delta_ts) * _this->delta_ts,
		.ptn_id = odata->msg->ptn_id,
	};
	bsos_img_key_htobe(&bk);
	sos_key_set(ok, &bk, sizeof(bk));

	obj = sos_index_find(bsos_img->index, ok);
	if (obj) {
		img = sos_obj_ptr(obj);
		img->data.uint32_[BSOS_IMG_COUNT] ++;
		sos_obj_put(obj);
		goto out;
	}
	/* reaching here means not found, add new data */
	obj = sos_array_obj_new(bsos_img->sos, SOS_TYPE_UINT32_ARRAY, 4);
	if (!obj) {
		bwarn("bout_sos_img: cannot alloce new sos obj,"
						" errno(%d): %m", errno);
		goto out;
	}
	img = sos_obj_ptr(obj);
	img->data.uint32_[BSOS_IMG_PTN_ID]	= bk.ptn_id;
	img->data.uint32_[BSOS_IMG_SEC]		= bk.ts;
	img->data.uint32_[BSOS_IMG_COMP_ID]	= bk.comp_id;
	img->data.uint32_[BSOS_IMG_COUNT]	= 1;

	rc = sos_index_insert(bsos_img->index, ok, obj);
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

struct bplugin *create_plugin_instance()
{
	struct bout_sos_img_plugin *_p = calloc(1, sizeof(*_p));
	struct boutplugin *p = (typeof(p)) _p;
	bout_sos_init((void*)_p, "bout_sos_img");
	p->process_output = bout_sos_img_process_output;
	/* override some functions */
	p->base.config = bout_sos_img_config;
	_p->base.bsos_handle_open = (void*)bsos_img_open;
	_p->base.bsos_handle_close = (void*)bsos_img_close;
	return (void*)p;
}
