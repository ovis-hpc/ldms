/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-2014 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013-2014 Sandia Corporation. All rights reserved.
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

/*
 * store_sos.c
 *
 *  Created on: Jul 17, 2013
 *      Author: nichamon
 */
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <limits.h>
#include <sos/sos.h>
#include <coll/idx.h>
#include <ovis_util/util.h>
#include "me.h"
#include "me_interface.h"

SOS_OBJ_BEGIN(me_event_class, "me_event")
	SOS_OBJ_ATTR_WITH_KEY("tv_sec", SOS_TYPE_UINT32),
	SOS_OBJ_ATTR("tv_usec", SOS_TYPE_UINT32),
	SOS_OBJ_ATTR_WITH_KEY("model_id", SOS_TYPE_UINT32),
	SOS_OBJ_ATTR_WITH_KEY("metric_id", SOS_TYPE_UINT64),
	SOS_OBJ_ATTR("sev_level", SOS_TYPE_INT32)
SOS_OBJ_END(5);

static pthread_mutex_t cfg_lock;
static me_log_fn msglog;
static char *file_path;

struct me_store_sos {
	struct me_store base;
	pthread_mutex_t sos_lock;
	sos_t sos;
};

static const char *usage(void)
{
	return	"	config name=store_sos path=<path>\n"
		"	   <path>	The path that contains the sos objects.\n";
}

static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	value = av_value(avl, "path");
	if (!value)
		return EINVAL;

	pthread_mutex_lock(&cfg_lock);
	file_path = strdup(value);
	pthread_mutex_unlock(&cfg_lock);
	if (!file_path)
		return ENOMEM;
	return 0;
}

static void store(struct me_store *strg, struct me_output *op)
{
	struct me_store_sos *strg_sos = (struct me_store_sos *)strg;
	sos_obj_t obj;
	int rc = 0;
	pthread_mutex_lock(&strg_sos->sos_lock);
	obj = sos_obj_new(strg_sos->sos);
	if (!obj) {
		msglog("store_sos: Failed to create an object. Error %d.\n",
				errno);
		pthread_mutex_unlock(&strg_sos->sos_lock);
		return;
	}

	sos_obj_attr_set(strg_sos->sos, 0, obj, (void*)&op->ts.tv_sec);
	sos_obj_attr_set(strg_sos->sos, 1, obj, (void*)&op->ts.tv_usec);
	sos_obj_attr_set(strg_sos->sos, 2, obj, (void*)&op->model_id);
	sos_obj_attr_set(strg_sos->sos, 3, obj, (void*)&op->metric_ids[0]);
	sos_obj_attr_set(strg_sos->sos, 4, obj, (void*)&op->level);

	rc = sos_obj_add(strg_sos->sos, obj);
	pthread_mutex_unlock(&strg_sos->sos_lock);
	if (rc) {
		msglog("store_sos: Failed to store new object. "
						"Error %d\n", rc);
	}
	return;
}

static int flush_sos(struct me_store *strg)
{
	struct me_store_sos *strg_sos = (struct me_store_sos *)strg;
	pthread_mutex_lock(&strg_sos->sos_lock);
	sos_commit(strg_sos->sos, ODS_COMMIT_SYNC);
	pthread_mutex_unlock(&strg_sos->sos_lock);
	return 0;
}

static void destroy_sos(struct me_store *strg)
{
	struct me_store_sos *strg_sos = (struct me_store_sos *)strg;
	pthread_mutex_lock(&strg_sos->sos_lock);
	sos_close(strg_sos->sos, ODS_COMMIT_SYNC);
	pthread_mutex_unlock(&strg_sos->sos_lock);
	free(strg_sos);
}

static void *get_instance(struct attr_value_list *avlist,
				struct me_interface_plugin *pi)
{
	struct me_store_sos *me_sos = calloc(1, sizeof(*me_sos));
	if (!me_sos) {
		msglog("store_sos: Failed to allocate memory for "
				"me_store_sos.\n");
		return NULL;
	}
	me_sos->base.intf_base = pi;
	char file[PATH_MAX];
	char *container = av_value(avlist, "container");
	pthread_mutex_lock(&cfg_lock);
	if (container)
		sprintf(file, "%s/%s", file_path, container);
	else
		sprintf(file, "%s", file_path);
	pthread_mutex_unlock(&cfg_lock);
	pthread_mutex_init(&me_sos->sos_lock, NULL);
	me_sos->sos = sos_open(file, O_RDWR|O_CREAT, 0660,
				&me_event_class);
	if (!me_sos->sos) {
		msglog("store_sos: Could not open SOS at %s.\n", file_path);
		return NULL;
	}

	me_sos->base.store = store;
	me_sos->base.flush_store = flush_sos;
	me_sos->base.destroy_store = destroy_sos;
	return (void *)me_sos;
}

static void term()
{
	if (file_path)
		free(file_path);
}

static struct me_interface_plugin store_sos = {
	.base = {
			.name = "store_sos",
			.type = ME_INTERFACE_PLUGIN,
			.usage = usage,
			.config = config,
			.term = term
	},
	.type = ME_STORE,
	.get_instance = get_instance,
};

struct me_plugin *get_plugin(me_log_fn log_fn)
{
	store_sos.base.intf_pi = &store_sos;
	msglog = log_fn;
	return &store_sos.base;
}

static void __attribute__ ((constructor)) store_sos_init();
static void store_sos_init()
{
	pthread_mutex_init(&cfg_lock, NULL);
}

static void __attribute__ ((destructor)) store_sos_fini(void);
static void store_sos_fini()
{
	pthread_mutex_destroy(&cfg_lock);
}
