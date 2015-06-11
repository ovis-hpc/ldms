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

static sos_schema_t create_msg_schema(sos_t sos)
{
	sos_schema_t schema;
	int rc;

	schema = sos_schema_new("Message");
	if (!schema)
		return NULL;
	rc = sos_schema_attr_add(schema, "timestamp", SOS_TYPE_TIMESTAMP);
	if (rc)
		goto err;
	rc = sos_schema_index_add(schema, "timestamp");
	if (rc)
		goto err;
	rc = sos_schema_attr_add(schema, "comp_id", SOS_TYPE_UINT32);
	if (rc)
		goto err;
	rc = sos_schema_attr_add(schema, "ptn_id", SOS_TYPE_UINT32);
	if (rc)
		goto err;
	rc = sos_schema_attr_add(schema, "argv", SOS_TYPE_UINT32_ARRAY);
	if (rc)
		goto err;
	return schema;
 err:
	sos_schema_put(schema);
	return NULL;
}

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
	pthread_mutex_lock(&sp->sos_mutex);
	sos_t sos;
	size_t len;
	size_t req_len;
	if (!sp->sos) {
		rc = EBADFD;
		goto err0;
	}

	bout_sos_rotate(sp, odata->tv.tv_sec, NULL);
	sos = sp->sos;

	sos_obj_t obj = sos_obj_new(mp->sos_schema);
	if (!obj) {
		rc = ENOMEM;
		goto err0;
	}
	struct sos_value_s val;
	sos_value_t value = sos_value_init(&val, obj, mp->time_attr);

	value->data->prim.timestamp_.fine.secs = odata->tv.tv_sec;
	value->data->prim.timestamp_.fine.usecs = odata->tv.tv_usec;

	value = sos_value_init(value, obj, mp->comp_id_attr);
	value->data->prim.uint32_ = odata->comp_id;

	value = sos_value_init(value, obj, mp->ptn_id_attr);
	value->data->prim.uint32_ = odata->msg->ptn_id;

	sos_value_put(value);
	sos_value_put(value);
	sos_value_put(value);

	value = sos_array_new(&val, mp->argv_attr, obj, odata->msg->argc);
	if (!value) {
		rc = ENOMEM;
		goto err1;
	}
	sos_value_memset(value, odata->msg->argv,
			 odata->msg->argc * sizeof(uint32_t));

	sos_value_put(value);
	rc = sos_obj_index(obj);
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
	if (!_this->sos_schema) {
		sos_schema_t schema;
		schema = sos_schema_by_name(sos, "Message");
		if (!schema) {
			schema = create_msg_schema(sos);
			if (!schema)
				return ENOENT;
			rc = sos_schema_add(sos, schema);
			if (rc)
				return EINVAL;
		}
		_this->sos_schema = schema;
		_this->time_attr = sos_schema_attr_by_id(schema, SOS_MSG_TIMESTAMP);
		_this->comp_id_attr = sos_schema_attr_by_id(schema, SOS_MSG_COMP_ID);
		_this->ptn_id_attr = sos_schema_attr_by_id(schema, SOS_MSG_PTN_ID);
		_this->argv_attr = sos_schema_attr_by_id(schema, SOS_MSG_ARGV);
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
