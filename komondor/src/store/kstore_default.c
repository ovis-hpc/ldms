/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2014 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2014 Sandia Corporation. All rights reserved.
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
 * \file kstore_default.c
 * \author Narate Taerat
 * \brief Default komondor store.
 *
 * This store plugin uses Scalable Object Storeage (SOS) as its storage engine.
 * The information stored by this plugin is the following:
 *   -# sec*
 *   -# usec
 *   -# model_id*
 *   -# comp_id*
 *   -# metric_type_id*
 *   -# level
 *   -# event_id*
 *   -# status
 *
 * The fields that are marked by * are indexed field. The comp_id and
 * metric_type_id fields are inferred from metric_id in the message, with an
 * assumption that metric_id = [comp_id|metric_type_id].
 */

#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include "sos/sos.h"
#include "../komondor.h"

enum KSTORE_SOS_KEY_IDX {
	KS_SOS_SEC = 0,
	KS_SOS_USEC,
	KS_SOS_MODEL_ID,
	KS_SOS_COMP_ID,
	KS_SOS_METRIC_TYPE_ID,
	KS_SOS_LEVEL,
	KS_SOS_EVENT_ID,
	KS_SOS_STATUS,
};

SOS_OBJ_BEGIN(k_tahoma_event_class, "KomondorGenericEvent")
	SOS_OBJ_ATTR_WITH_KEY("tv_sec", SOS_TYPE_UINT32),
	SOS_OBJ_ATTR("tv_usec", SOS_TYPE_UINT32),
	SOS_OBJ_ATTR_WITH_KEY("model_id", SOS_TYPE_UINT32),
	SOS_OBJ_ATTR_WITH_KEY("comp_id", SOS_TYPE_UINT32),
	SOS_OBJ_ATTR_WITH_KEY("metric_type_id", SOS_TYPE_UINT32),
	SOS_OBJ_ATTR("level", SOS_TYPE_UINT32),
	SOS_OBJ_ATTR_WITH_KEY("event_id", SOS_TYPE_UINT64),
	SOS_OBJ_ATTR("status", SOS_TYPE_UINT32)
SOS_OBJ_END(8);

struct kmd_store_tahoma {
	struct kmd_store s;
	char *path;
	pthread_mutex_t mutex;
	uint64_t next_event_id;
	sos_t sos;
};

/**
 * \brief Store configuration.
 * Expecting only one attribute-value pair, path="..." .
 *
 * \returns 0 on success.
 * \returns Error number on error.
 */
int config(struct kmd_store *s, struct attr_value_list *av_list)
{
	struct kmd_store_tahoma *this = (void*)s;
	char *path = av_value(av_list, "path");
	int rc = 0;
	if (!path) {
		rc = EINVAL;
		goto err0;
	}
	this->path = strdup(path);
	if (!this->path) {
		rc = ENOMEM;
		goto err0;
	}
	this->sos = sos_open(this->path, O_RDWR|O_CREAT, 0660,
			  &k_tahoma_event_class);
	if (!this->sos) {
		k_log("ERROR: Cannot open sos: %s\n", this->path);
		rc = ENOMEM;
		goto err1;
	}

	sos_iter_t iter = sos_iter_new(this->sos, KS_SOS_EVENT_ID);
	struct sos_key_s k;
	sos_key_set_uint64(this->sos, KS_SOS_EVENT_ID, -1LU, &k);
	int found = sos_iter_seek_inf(iter, &k);
	if (found) {
		sos_obj_t obj = sos_iter_next(iter);
		this->next_event_id = 1 + sos_obj_attr_get_uint64(this->sos,
							KS_SOS_EVENT_ID, obj);
	} else {
		this->next_event_id = 1;
	}

	goto out;

err1:
	free(this->path);
err0:
out:
	return rc;
}

struct event_object {
	uint64_t event_id;
};

void* get_event_object(struct kmd_store *s, struct kmd_msg *e)
{
	struct kmd_store_tahoma *this = (void*)s;
	pthread_mutex_lock(&this->mutex);
	struct event_object *x = malloc(sizeof(*x));
	if (!x)
		goto err;

	sos_obj_t obj = sos_obj_new(this->sos);
	if (!obj)
		goto err1;
	uint32_t v;
	sos_obj_attr_set(this->sos, KS_SOS_SEC, obj, &e->sec);
	sos_obj_attr_set(this->sos, KS_SOS_USEC, obj, &e->usec);
	v = e->model_id;
	sos_obj_attr_set(this->sos, KS_SOS_MODEL_ID, obj, &v);
	v = e->metric_id >> 32;
	sos_obj_attr_set(this->sos, KS_SOS_COMP_ID, obj, &v);
	v = (uint32_t) e->metric_id;
	sos_obj_attr_set(this->sos, KS_SOS_METRIC_TYPE_ID, obj, &v);
	v = e->level;
	sos_obj_attr_set(this->sos, KS_SOS_LEVEL, obj, &v);
	sos_obj_attr_set(this->sos, KS_SOS_EVENT_ID, obj, &this->next_event_id);
	v = KMD_EVENT_NEW;
	sos_obj_attr_set(this->sos, KS_SOS_STATUS, obj, &v);
	sos_obj_add(this->sos, obj);

	x->event_id = this->next_event_id;
	this->next_event_id++;
	pthread_mutex_unlock(&this->mutex);
	return x;
	/* NOTE: obj can not be used because it can be relocated when sos
	 * expands. */
err1:
	free(x);
err:
	pthread_mutex_unlock(&this->mutex);
	return NULL;
}

void put_event_object(struct kmd_store *s, void *event_object)
{
	k_log("DEBUG: freeing event_object\n");
	free(event_object);
}

int event_update(struct kmd_store *s, void *event_object,
		k_event_status_e status)
{
	struct event_object *ref = event_object;
	struct kmd_store_tahoma *this = (void*)s;
	int rc;
	pthread_mutex_lock(&this->mutex);
	sos_iter_t iter = sos_iter_new(this->sos, KS_SOS_EVENT_ID);
	if (!iter) {
		rc = ENOMEM;
		goto out;
	}
	struct sos_key_s k;
	sos_key_set_uint64(this->sos, KS_SOS_EVENT_ID, ref->event_id, &k);
	int found = sos_iter_seek(iter, &k);
	if (!found) {
		rc = ENOENT;
		goto out;
	}
	sos_obj_t obj = sos_iter_next(iter);
	uint32_t v = status;
	sos_obj_attr_set(this->sos, KS_SOS_STATUS, obj, &v);
out:
	pthread_mutex_unlock(&this->mutex);
	return rc;
}

void destroy(struct kmd_store *s)
{
	struct kmd_store_tahoma *this = (void*)s;
	sos_close(this->sos);
	free(this->path);
	free(s);
}

struct kmd_store *create_store()
{
	struct kmd_store_tahoma *g = calloc(1, sizeof(*g));
	if (!g)
		return NULL;
	struct kmd_store *s = (void*)g;
	s->config = config;
	s->get_event_object = get_event_object;
	s->put_event_object = put_event_object;
	s->event_update = event_update;
	s->destroy = destroy;
	return s;
}
