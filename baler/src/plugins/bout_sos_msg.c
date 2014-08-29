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

/*
 * This is a hack to prevent stale obj.
 */
#include "../../../sos/src/sos_priv.h"

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
	struct bout_sos_plugin *sp = (typeof(sp))this;
	struct bout_sos_msg_plugin *mp = (typeof(mp))this;
	pthread_mutex_lock(&sp->sos_mutex);
	sos_t sos;
	size_t len;
	size_t req_len;
	if (!sp->sos)
		return EBADFD;

	bout_sos_rotate(sp, odata->tv.tv_sec, NULL);
	sos = sp->sos;

	sos_obj_t obj = sos_obj_new(sos);
	if (!obj)
		goto err0;
	obj_ref_t objref = ods_obj_ptr_to_ref(sos->ods, obj);
	sos_obj_attr_set(sos, SOS_MSG_SEC, obj, &odata->tv.tv_sec);
	sos_obj_attr_set(sos, SOS_MSG_USEC, obj, &odata->tv.tv_usec);
	sos_obj_attr_set(sos, SOS_MSG_COMP_ID, obj, &odata->comp_id);
	len = BMSG_SZ(odata->msg);
	req_len = len + sizeof(struct sos_blob_obj_s);
	if (mp->blob_sz < req_len) {
		size_t new_size = (req_len | 0xFFFF) + 1;
		void *new_blob = malloc(new_size);
		if (!new_blob)
			goto err1;
		free(mp->blob);
		mp->blob = new_blob;
		mp->blob_sz = new_size;
	}
	mp->blob->len = len;
	memcpy(mp->blob->data, odata->msg, len);
	sos_obj_attr_set(sos, SOS_MSG_MSG, obj, mp->blob);
	obj = ods_obj_ref_to_ptr(sos->ods, objref);
	if (sos_obj_add(sos, obj))
		goto err1;
	pthread_mutex_unlock(&sp->sos_mutex);
	return 0;
err1:
	sos_obj_delete(sos, obj);
	pthread_mutex_unlock(&sp->sos_mutex);
err0:
	return ENOMEM;
}

struct bplugin *create_plugin_instance()
{
	struct bout_sos_msg_plugin *_p = calloc(1, sizeof(*_p));
	if (!_p)
		return NULL;
	_p->base.sos_class = &sos_msg_class;
	_p->blob_sz = 65536;
	_p->blob = malloc(_p->blob_sz);
	if (!_p->blob) {
		free(_p);
		return NULL;
	}
	struct boutplugin *p = (typeof(p)) _p;
	bout_sos_init((void*)_p, "bout_sos_msg");
	p->process_output = bout_sos_msg_process_output;
	return (void*)p;
}
