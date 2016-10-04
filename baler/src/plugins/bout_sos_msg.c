/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013-2016 Sandia Corporation. All rights reserved.
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
 * \page bout_sos_msg.config SOS message output plugin configuration
 *
 * \section synopsis SYNOPSIS
 *
 * <b>plugin name=bout_sos_msg</b>
 *
 *
 * \section description DESCRIPTION
 *
 * \b bout_sos_msg output plugin stores processed messages from Baler daemon
 * into SOS. The messages can be later obtained back by \b bquery command.
 */

#include "bout_sos_msg.h"
#include "bsos_msg.h"

#define BOUT_MSG_LEN(_msg_) ((_msg_)->argc + 4)

/**
 * \brief process_output for SOS.
 *
 * This function will create a new sos_object according to \a odata, and
 * put it into the opened sos storage.
 * \param this The pointer to the plugin instance.
 * \param odata The output data.
 * \return 0 on success.
 * \return Error code on error.
 */
int bout_sos_msg_process_output(struct boutplugin *this,
				struct boutq_data *odata)
{
	int rc;
	struct bout_sos_plugin *sp = (typeof(sp))this;
	struct bout_sos_msg_plugin *mp = (typeof(mp))this;
	sos_t sos;
	size_t len;
	size_t req_len;
	sos_array_t msg;
	sos_obj_t obj;
	int arg;
	bsos_msg_t bsos_msg = sp->bsos_handle;
	struct bsos_msg_key_ptc ptc_k;
	struct bsos_msg_key_tc tc_k;
	SOS_KEY(ts_key);
	SOS_KEY(ptc_key);

	pthread_mutex_lock(&sp->sos_mutex);
	if (!bsos_msg) {
		rc = EBADFD;
		goto err0;
	}

	sos = bsos_msg->sos;

	obj = sos_array_obj_new(bsos_msg->sos, SOS_TYPE_UINT32_ARRAY,
				BOUT_MSG_LEN(odata->msg));
	if (!obj) {
		rc = ENOMEM;
		goto err0;
	}
	/* setting object value */
	msg = sos_obj_ptr(obj);
	msg->data.uint32_[BSOS_MSG_SEC] = odata->tv.tv_sec;
	msg->data.uint32_[BSOS_MSG_USEC] = odata->tv.tv_usec;
	msg->data.uint32_[BSOS_MSG_COMP_ID] = odata->comp_id;
	msg->data.uint32_[BSOS_MSG_PTN_ID] = odata->msg->ptn_id;
	for (arg = 0; arg < odata->msg->argc; arg++)
		msg->data.uint32_[arg + BSOS_MSG_ARGV_0] = odata->msg->argv[arg];

	/* creat key */
	ptc_k.comp_id = msg->data.uint32_[BSOS_MSG_COMP_ID];
	ptc_k.sec = msg->data.uint32_[BSOS_MSG_SEC];
	ptc_k.ptn_id = msg->data.uint32_[BSOS_MSG_PTN_ID];

	tc_k.comp_id = ptc_k.comp_id;
	tc_k.sec = ptc_k.sec;

	/* need to convert only PTC (due to memcmp()) */
	bsos_msg_key_ptc_htobe(&ptc_k);


	/* add into Time-CompID index */
	sos_key_set(ts_key, &tc_k, 8);
	rc = sos_index_insert(bsos_msg->index_tc, ts_key, obj);
	if (rc)
		goto err1;

	/* add into PtnID-Time-CompID index */
	sos_key_set(ptc_key, &ptc_k, 12);
	rc = sos_index_insert(bsos_msg->index_ptc, ptc_key, obj);
	if (rc)
		goto err2;

	sos_obj_put(obj);
	pthread_mutex_unlock(&sp->sos_mutex);
	return 0;

err2:
	sos_index_remove(bsos_msg->index_tc, ts_key, obj);
err1:
	sos_obj_delete(obj);
	sos_obj_put(obj);
err0:
	pthread_mutex_unlock(&sp->sos_mutex);
	return rc;
}

struct bplugin *create_plugin_instance()
{
	struct bout_sos_msg_plugin *_p = calloc(1, sizeof(*_p));
	if (!_p)
		return NULL;
	struct boutplugin *p = (typeof(p)) _p;
	bout_sos_init((void*)_p, "bout_sos_msg");
	_p->base.bsos_handle_open = (void*)bsos_msg_open;
	_p->base.bsos_handle_close = (void*)bsos_msg_close;
	p->process_output = bout_sos_msg_process_output;
	return (void*)p;
}
