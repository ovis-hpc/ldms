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
/*
 * store_example.c
 *
 *  Created on: Apr 10, 2013
 *      Author: nichamon
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ovis_util/util.h>

#include "me.h"
#include "me_interface.h"

static pthread_mutex_t cfg_lock;
static char *path = "output";
static me_log_fn msglog;

struct me_store_example {
	struct me_store base;
	char *filename;
	FILE *f;
};

static const char *usage(void)
{
	return "	config name=store_example path=<path>\n"
		"	   - Set the file path\n"
		"	   <path>	The file path\n";
}

static int flush_store_ex(struct me_store *strg)
{
	struct me_store_example *strg_ex = (struct me_store_example *)strg;
	fflush(strg_ex->f);
	return 0;
}

static void print_output(struct me_store *strg, me_output_t output)
{
	struct me_store_example *strg_ex = (struct me_store_example *)strg;
	static count = 0;
	fprintf(strg_ex->f, "%j" PRIu32 "\t%s\t%" PRIu32 "\t%" PRIu8 "\n",
			output->ts.tv_sec, output->model_name,
			output->model_id, output->level);
	count++;
	if (count % 10 == 0)
		flush_store_ex(strg);
}

static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	value = av_value(avl, "path");
	if (!value)
		return EINVAL;

	pthread_mutex_lock(&cfg_lock);
	path = strdup(value);
	pthread_mutex_unlock(&cfg_lock);
	return 0;
}

static void term()
{
	pthread_mutex_lock(&cfg_lock);
	free(path);
	pthread_mutex_unlock(&cfg_lock);
}

static void destroy_store_ex(struct me_store *store)
{
	struct me_store_example *store_ex = (struct me_store_example *)store;
	fflush(store_ex->f);
	fclose(store_ex->f);
	free(store_ex);
}

static void *get_instance(struct attr_value_list *avlist,
		struct me_interface_plugin *pi)
{
	struct me_store_example *store = malloc(sizeof(*store));
	store->base.intf_base = pi;
	store->f = fopen(path, "w");
	if (!store->f)
		return NULL;
	fprintf(store->f, "timestamp\tmodel_name\tmodel_id\tcomp_id\tlevel\n");
	store->base.store = print_output;
	store->base.flush_store = flush_store_ex;
	store->base.destroy_store = destroy_store_ex;
	return (void *) store;
}

static struct me_interface_plugin store_ex = {
	.base = {
			.name = "store_example",
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

	store_ex.base.intf_pi = &store_ex;
	msglog = log_fn;
	return &store_ex.base;
}

static void __attribute__ ((constructor)) store_example_init();
static void store_example_init()
{
	pthread_mutex_init(&cfg_lock, NULL);
}

static void __attribute__ ((destructor)) store_example_fini(void);
static void store_example_fini()
{
	pthread_mutex_destroy(&cfg_lock);
}
