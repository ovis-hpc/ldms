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
#include "bout_sos_msg.h"
#include "sos_msg_class_def.h"

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
	SOS_KEY(msg_key);

	pthread_mutex_lock(&sp->sos_mutex);
	if (!sp->sos) {
		rc = EBADFD;
		goto err0;
	}

	bout_sos_rotate(sp, odata->tv.tv_sec, NULL);
	sos = sp->sos;

	obj = sos_array_obj_new(sp->sos, SOS_TYPE_UINT32_ARRAY,
				BOUT_MSG_LEN(odata->msg));
	if (!obj) {
		rc = ENOMEM;
		goto err0;
	}
	msg = sos_obj_ptr(obj);
	msg->data.uint32_[BOUT_MSG_SEC] = odata->tv.tv_sec;
	msg->data.uint32_[BOUT_MSG_USEC] = odata->tv.tv_usec;
	msg->data.uint32_[BOUT_MSG_COMP_ID] = odata->comp_id;
	msg->data.uint32_[BOUT_MSG_PTN_ID] = odata->msg->ptn_id;
	for (arg = 0; arg < odata->msg->argc; arg++)
		msg->data.uint32_[arg + BOUT_MSG_ARGV_0] = odata->msg->argv[arg];

	sos_key_set(msg_key, &msg->data.uint32_[0], 8);
	rc = sos_index_insert(mp->msg_index, msg_key, obj);
	if (rc)
		goto err1;
	sos_obj_put(obj);
	pthread_mutex_unlock(&sp->sos_mutex);
	return 0;
err1:
	sos_obj_delete(obj);
	sos_obj_put(obj);
	pthread_mutex_unlock(&sp->sos_mutex);
err0:
	return rc;
}

int bout_sos_msg_start(struct bplugin *this)
{
	struct bout_sos_msg_plugin *_this = (void*)this;
	sos_t sos;
	int rc;
	if ((rc = bout_sos_start(this)))
		return rc;
	sos = _this->base.sos;
 retry:
	_this->msg_index = sos_index_open(sos, BOUT_SOS_IDX_NAME);
	if (!_this->msg_index) {
		rc = sos_index_new(sos, BOUT_SOS_IDX_NAME, "BXTREE", "UINT64", 5);
		if (!rc)
			goto retry;
		else
			return errno;
	}
	return 0;
}

struct bplugin *create_plugin_instance()
{
	struct bout_sos_msg_plugin *_p = calloc(1, sizeof(*_p));
	if (!_p)
		return NULL;
	struct boutplugin *p = (typeof(p)) _p;
	bout_sos_init((void*)_p, "bout_sos_msg");
	p->base.start = bout_sos_msg_start;
	p->process_output = bout_sos_msg_process_output;
	return (void*)p;
}
