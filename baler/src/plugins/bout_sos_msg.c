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
	struct bout_sos_plugin *_this = (typeof(_this))this;
	pthread_mutex_lock(&_this->sos_mutex);
	sos_t sos = _this->sos;
	if (!sos)
		return EBADFD;
	sos_obj_t obj = sos_obj_new(sos);
	sos_obj_attr_set(sos, 0, obj, &odata->tv.tv_sec);
	sos_obj_attr_set(sos, 1, obj, &odata->tv.tv_usec);
	sos_obj_attr_set(sos, 2, obj, &odata->comp_id);
	sos_obj_attr_set(sos, 3, obj, &(struct sos_blob_arg_s) {
				sizeof(struct bmsg) +
				odata->msg->argc * sizeof(odata->msg->argv[0]),
				odata->msg
			});
	if (sos_obj_add(sos, obj))
		goto err1;
	pthread_mutex_unlock(&_this->sos_mutex);
	return 0;
err1:
	sos_obj_delete(sos, obj);
	pthread_mutex_unlock(&_this->sos_mutex);
err0:
	return EINVAL;
}

struct bplugin *create_plugin_instance()
{
	struct bout_sos_msg_plugin *_p = calloc(1, sizeof(*_p));
	_p->base.sos_class = &sos_msg_class;
	struct boutplugin *p = (typeof(p)) _p;
	bout_sos_init((void*)_p, "bout_sos_msg");
	p->process_output = bout_sos_msg_process_output;
	return (void*)p;
}
