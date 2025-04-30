/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2019 Open Grid Computing, Inc. All rights reserved.
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
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <linux/limits.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <grp.h>
#include <pwd.h>
#include <sys/syscall.h>
#include <assert.h>
#include <sos/sos.h>
#include "ldms.h"
#include "ldmsd.h"

/**
 * \brief PAPI-store
 *
 * This store handle the PAPI event metric set. It parses each set as
 * it arrives and produces output of the form:
 *
 * timestamp, component_id, job_id, app_id, rank, papi-event-value
 *
 * Each PAPI event in a metric set instance results in an object in
 * the Sos store. Although the schema for the objectg store are
 * actually all the same, there are different schema for each event
 * name in order to distinguish one event from the
 * other. Specifically, all PAPI_TOT_CYC events will be stored in a
 * schema named PAPI_TOT_CYC. Similarly, all PAPI_TOT_INS will be
 * stored in a schema named PAPI_TOT_INS.
 *
 */

#define STORE "papi"

static ovis_log_t mylog;
#define LOG_(level, ...) do { \
	ovis_log(mylog, level, ## __VA_ARGS__); \
} while(0);

static int schema_cmp(void *a, const void *b)
{
	char *x = a;
	char *y = (char *)b;

	return strcmp(x, y);
}

typedef struct sos_schema_ref_s {
	sos_schema_t schema;
	struct rbn rbn;
} *sos_schema_ref_t;

typedef struct sos_handle_s {
	int ref_count;
	char path[PATH_MAX];
	sos_t sos;
	pthread_mutex_t schema_tree_lock;
	struct rbt schema_tree;	/* Tree of PAPI schema in this store */
	LIST_ENTRY(sos_handle_s) entry;
} *sos_handle_t;

static LIST_HEAD(sos_handle_list, sos_handle_s) sos_handle_list;

/*
 * NOTE:
 *   <sos::path> = <root_path>/<container>
 */
struct sos_instance {
	struct ldmsd_store *store;
	char *container;
	char *path; /**< <root_path>/<container> */
	sos_handle_t sos_handle; /**< sos handle */
	pthread_mutex_t lock; /**< lock at metric store level */

	LIST_ENTRY(sos_instance) entry;
};
static pthread_mutex_t cfg_lock;
LIST_HEAD(sos_inst_list, sos_instance) inst_list;

static char root_path[PATH_MAX]; /**< store root path */

sos_handle_t create_handle(const char *path, sos_t sos)
{
	sos_handle_t h = calloc(1, sizeof(*h));
	if (!h)
		return NULL;
	int len = strlen(path);
	if (len >= sizeof(h->path)) {
		errno = ENAMETOOLONG;
		free(h);
		return NULL;
	}
	memcpy(h->path, path, len+1);
	h->ref_count = 1;
	h->sos = sos;
	rbt_init(&h->schema_tree, schema_cmp);
	pthread_mutex_init(&h->schema_tree_lock, NULL);
	pthread_mutex_lock(&cfg_lock);
	LIST_INSERT_HEAD(&sos_handle_list, h, entry);
	pthread_mutex_unlock(&cfg_lock);
	return h;
}

/* Analyze data across time for a given job/rank */
static const char *job_rank_time_attrs[] = { "job_id", "rank", "timestamp" };
/* Analyze data across rank for a given job/time */
static const char *job_time_rank_attrs[] = { "job_id", "timestamp", "rank" };
/* Find all jobs in a given time period */
static const char *time_job_attrs[] = { "timestamp", "job_id" };
/* Component-wide analysis by time */
static const char *comp_time_attrs[] = { "component_id", "timestamp" };

/*
 * All PAPI-event schema use the template below. The schema.name and
 * attrs[5].name field are overriden for each event-name
 */
struct sos_schema_template papi_event_schema = {
	.name = NULL,
	.attrs = {
		{
			.name = "timestamp",
			.type = SOS_TYPE_TIMESTAMP,
			.indexed = 0,
		},
		{
			.name = "component_id",
			.type = SOS_TYPE_UINT64,
			.indexed = 0,
		},
		{
			.name = "job_id",
			.type = SOS_TYPE_UINT64,
			.indexed = 0,
		},
		{
			.name = "app_id",
			.type = SOS_TYPE_UINT64,
			.indexed = 0,
		},
		{
			.name = "rank",
			.type = SOS_TYPE_UINT64,
		},
		{
			.name = NULL, /* Placeholder for the PAPI event name */
			.type = SOS_TYPE_UINT64,
		},
		{
			.name = "comp_time",
			.type = SOS_TYPE_JOIN,
			.size = 2,
			.join_list = comp_time_attrs,
			.indexed = 1,
		},
		{	/* time_job */
			.name = "time_job",
			.type = SOS_TYPE_JOIN,
			.size = 2,
			.join_list = time_job_attrs,
			.indexed = 1,
		},
		{	/* job_rank_time */
			.name = "job_rank_time",
			.type = SOS_TYPE_JOIN,
			.size = 2,
			.join_list = job_rank_time_attrs,
			.indexed = 1,
		},
		{	/* job_time_rank */
			.name = "job_time_rank",
			.type = SOS_TYPE_JOIN,
			.size = 3,
			.join_list = job_time_rank_attrs,
			.indexed = 1,
		},
		{ NULL }
	}
};

enum sos_attr_ids {
	TIMESTAMP_ATTR,
	COMPONENT_ATTR,
	JOB_ATTR,
	APP_ATTR,
	RANK_ATTR,
	EVENT_ATTR
};

sos_schema_t get_schema(sos_handle_t sh, const char *name)
{
	int rc;
	struct rbn *rbn;
	sos_schema_ref_t ref;
	sos_schema_t schema = NULL;

	pthread_mutex_lock(&sh->schema_tree_lock);
	rbn = rbt_find(&sh->schema_tree, name);
	if (!rbn) {
		sos_schema_ref_t ref = malloc(sizeof *ref);
		if (!ref) {
			LOG_(OVIS_LERROR, "%s[%d]: Memory allocation error.\n",
			       __func__, __LINE__);
			goto out;
		}
		/* Look it up */
		schema = sos_schema_by_name(sh->sos, name);
		if (!schema) {
			/* Create a new schema for 'name' */
			papi_event_schema.name = name;
			papi_event_schema.attrs[EVENT_ATTR].name = name;
			schema = sos_schema_from_template(&papi_event_schema);
			if (!schema) {
				LOG_(OVIS_LERROR,
				       "%s[%d]: Error %d allocating '%s' schema.\n",
				       __func__, __LINE__,
				       errno, name);
				free(ref);
				goto out;
			}
			rc = sos_schema_add(sh->sos, schema);
			if (rc) {
				LOG_(OVIS_LERROR,
				       "%s[%d]: Error %d adding '%s' schema to container.\n",
				       __func__, __LINE__,
				       rc, name);
				free(ref);
				goto out;
			}
		}
		ref->schema = schema;
		rbn_init(&ref->rbn, (char *)sos_schema_name(schema));
		rbt_ins(&sh->schema_tree, &ref->rbn);
	} else {
		ref = container_of(rbn, struct sos_schema_ref_s, rbn);
		schema = ref->schema;
	}
 out:
	pthread_mutex_unlock(&sh->schema_tree_lock);
	return schema;
}

sos_handle_t create_container(const char *path)
{
	int rc = 0;
	sos_t sos;
	time_t t;
	char part_name[16];	/* Unix timestamp as string */
	sos_part_t part;

	rc = sos_container_new(path, 0660);
	if (rc) {
		LOG_(OVIS_LERROR, "Error %d creating the container at '%s'\n",
		       rc, path);
		goto err_0;
	}
	sos = sos_container_open(path, SOS_PERM_RW);
	if (!sos) {
		LOG_(OVIS_LERROR, "Error %d opening the container at '%s'\n",
		       errno, path);
		goto err_0;
	}
	/*
	 * Create the first partition. All other partitions and
	 * rollover are handled with the SOS partition commands
	 */
	t = time(NULL);
	sprintf(part_name, "%d", (unsigned int)t);
	rc = sos_part_create(sos, part_name, path);
	if (rc) {
		LOG_(OVIS_LERROR, "Error %d creating the partition '%s' in '%s'\n",
		       rc, part_name, path);
		goto err_1;
	}
	part = sos_part_find(sos, part_name);
	if (!part) {
		LOG_(OVIS_LERROR, "Newly created partition was not found\n");
		goto err_1;
	}
	rc = sos_part_state_set(part, SOS_PART_STATE_PRIMARY);
	if (rc) {
		LOG_(OVIS_LERROR, "New partition could not be made primary\n");
		goto err_2;
	}
	sos_part_put(part);
	return create_handle(path, sos);
 err_2:
	sos_part_put(part);
 err_1:
	sos_container_close(sos, SOS_COMMIT_ASYNC);
 err_0:
	if (rc)
		errno = rc;
	return NULL;
}

static void close_container(sos_handle_t h)
{
	assert(h->ref_count == 0);
	sos_container_close(h->sos, SOS_COMMIT_ASYNC);
	free(h);
}

static void put_container_no_lock(sos_handle_t h)
{
	h->ref_count--;
	if (h->ref_count == 0) {
		/* remove from list, destroy the handle */
		LIST_REMOVE(h, entry);
		close_container(h);
	}
}

static void put_container(sos_handle_t h)
{
	pthread_mutex_lock(&cfg_lock);
	put_container_no_lock(h);
	pthread_mutex_unlock(&cfg_lock);
}

static sos_handle_t find_container(const char *path)
{
	sos_handle_t h;
	LIST_FOREACH(h, &sos_handle_list, entry){
		if (0 != strncmp(path, h->path, sizeof(h->path)))
			continue;

		/* found */
		/* take reference */
		pthread_mutex_lock(&cfg_lock);
		h->ref_count++;
		pthread_mutex_unlock(&cfg_lock);
		return h;
	}
	return NULL;
}

/**
 * \brief Configuration
 */
static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	struct sos_instance *si;
	int rc, len;
	char *value;
	value = av_value(avl, "path");
	if (!value) {
		LOG_(OVIS_LERROR,
		       "%s[%d]: The 'path' configuraiton option is required.\n",
		       __func__, __LINE__);
		return EINVAL;
	}
	len = strlen(value);
	if (len >= PATH_MAX) {
		LOG_(OVIS_LERROR,
		       "%s[%d]: The 'path' is too long.\n",
		       __func__, __LINE__);
		return ENAMETOOLONG;
	}
	pthread_mutex_lock(&cfg_lock);
	memcpy(root_path, value, len+1);

	/*
	 * Run through all open containers and close them. They will
	 * get re-opened when store() is next called
	 */
	rc = ENOMEM;
	LIST_FOREACH(si, &inst_list, entry) {
		pthread_mutex_lock(&si->lock);
		if (si->sos_handle) {
			put_container_no_lock(si->sos_handle);
			si->sos_handle = NULL;
		}
		size_t pathlen =
			strlen(root_path) + strlen(si->container) + 4;
		if (si->path)
			free(si->path);
		si->path = malloc(pathlen);
		if (!si->path) {
			LOG_(OVIS_LERROR, "%s[%d]: Memory allocation error.\n",
			       __func__, __LINE__);
			goto err_0;
		}
		sprintf(si->path, "%s/%s", root_path, si->container);
		pthread_mutex_unlock(&si->lock);
	}
	pthread_mutex_unlock(&cfg_lock);
	return 0;

 err_0:
	pthread_mutex_unlock(&cfg_lock);
	return rc;
}

static const char *usage(ldmsd_plug_handle_t handle)
{
	return  "    config name=store_papi path=<path>\n"
		"       path The path to primary storage\n";
}

static ldmsd_store_handle_t
open_store(ldmsd_plug_handle_t s, const char *container, const char *schema,
	   struct ldmsd_strgp_metric_list *metric_list)
{
	struct sos_instance *si = calloc(1, sizeof(*si));
	if (!si)
		goto out;
	si->container = strdup(container);
	if (!si->container)
		goto err1;
	size_t pathlen =
		strlen(root_path) + strlen(si->container) + 4;
	si->path = malloc(pathlen);
	if (!si->path)
		goto err2;
	sprintf(si->path, "%s/%s", root_path, container);
	pthread_mutex_init(&si->lock, NULL);
	pthread_mutex_lock(&cfg_lock);
	LIST_INSERT_HEAD(&inst_list, si, entry);
	pthread_mutex_unlock(&cfg_lock);
	return si;
 err2:
	free(si->container);
 err1:
	free(si);
 out:
	return NULL;
}

static int
_open_store(struct sos_instance *si, ldms_set_t set)
{
	int rc;

	/* Check if the container is already open */
	si->sos_handle = find_container(si->path);
	if (si->sos_handle)
		return 0;

	/* See if it exists, but has not been opened yet. */
	sos_t sos = sos_container_open(si->path, SOS_PERM_RW);
	if (sos) {
		/* Create a new handle and add it for this SOS */
		si->sos_handle = create_handle(si->path, sos);
		if (!si->sos_handle) {
			sos_container_close(sos, SOS_COMMIT_ASYNC);
			rc = ENOMEM;
			goto err_0;
		}
		return 0;
	}

	/* Create the container */
	si->sos_handle = create_container(si->path);
	if (!si->sos_handle)
		return errno;
	return 0;
 err_0:
	put_container(si->sos_handle);
	return rc;
}

static uint64_t get_by_name(ldms_set_t set, const char *name)
{
	int mid = ldms_metric_by_name(set, name);
	if (mid < 0) {
		LOG_(OVIS_LERROR, "%s[%d]: set is missing the '%s' attribute\n",
		       __func__, __LINE__, name);
		return -1;
	}
	return ldms_metric_get_u64(set, mid);
}

static int
store(ldmsd_plug_handle_t handle,
      ldmsd_store_handle_t _sh,
      ldms_set_t set,
      int *metric_arry /* ignored */,
      size_t metric_count /* ignored */)
{
	int rc = 0;
	struct sos_instance *si = _sh;
	sos_obj_t obj;
	sos_schema_t schema;
	struct sos_value_s v_;
	int local_ranks, event_mid, last_mid, i;

	if (!si)
		return EINVAL;

	struct ldms_timestamp timestamp = ldms_transaction_timestamp_get(set);
	uint64_t component_id = get_by_name(set, "component_id");
	uint64_t job_id = get_by_name(set, "job_id");
	uint64_t app_id = get_by_name(set, "app_id");

	int rank_mid = ldms_metric_by_name(set, "task_ranks");
	if (rank_mid < 0) {
		LOG_(OVIS_LERROR, "%s[%d]: set is missing the 'rank' attribute\n",
		       __func__, __LINE__);
		return -1;
	}
	pthread_mutex_lock(&si->lock);
	if (!si->sos_handle) {
		rc = _open_store(si, set);
		if (rc) {
			pthread_mutex_unlock(&si->lock);
			LOG_(OVIS_LERROR, "Failed to create store "
			       "for %s.\n", si->container);
			errno = rc;
			goto err;
		}
	}
	local_ranks = ldms_metric_array_get_len(set, rank_mid);
	event_mid = rank_mid + 1;
	last_mid = ldms_set_card_get(set);
	for (event_mid = rank_mid + 1; event_mid < last_mid; event_mid ++) {
		for (i = 0; i < local_ranks; i++) {
			schema = get_schema(si->sos_handle, ldms_metric_name_get(set, event_mid));
			obj = sos_obj_new(schema);
			if (!obj) {
				LOG_(OVIS_LERROR, "%s[%d]: Error %d allocating Sos object for '%s'\n",
				       __func__, __LINE__, errno, sos_schema_name(schema));
				goto err;
			}

			/* Timestamp */
			sos_value_t v = sos_value_by_id(&v_, obj, TIMESTAMP_ATTR);
			v->data->prim.timestamp_.tv.tv_usec = timestamp.usec;
			v->data->prim.timestamp_.tv.tv_sec = timestamp.sec;
			sos_value_put(v);

			/* Component ID */
			v = sos_value_by_id(&v_, obj, COMPONENT_ATTR);
			v->data->prim.uint64_ = component_id;
			sos_value_put(v);

			/* Job ID */
			v = sos_value_by_id(&v_, obj, JOB_ATTR);
			v->data->prim.uint64_ = job_id;
			sos_value_put(v);

			/* App ID */
			v = sos_value_by_id(&v_, obj, APP_ATTR);
			v->data->prim.uint64_ = app_id;
			sos_value_put(v);

			/* Rank */
			v = sos_value_by_id(&v_, obj, RANK_ATTR);
			v->data->prim.uint32_ = ldms_metric_array_get_u32(set, rank_mid, i);
			sos_value_put(v);

			/* Event count */
			v = sos_value_by_id(&v_, obj, EVENT_ATTR);
			v->data->prim.int64_ = ldms_metric_array_get_s64(set, event_mid, i);
			sos_value_put(v);

			sos_obj_index(obj);
			sos_obj_put(obj);
		}
	}
	pthread_mutex_unlock(&si->lock);
	return rc;
err:
	pthread_mutex_unlock(&si->lock);
	return errno;
}

static int flush_store(ldmsd_plug_handle_t handle, ldmsd_store_handle_t _sh)
{
	struct sos_instance *si = _sh;
	if (!_sh)
		return EINVAL;
	pthread_mutex_lock(&si->lock);
	if (si->sos_handle)
		sos_container_commit(si->sos_handle->sos, SOS_COMMIT_ASYNC);
	pthread_mutex_unlock(&si->lock);
	return 0;
}

static void close_store(ldmsd_plug_handle_t handle, ldmsd_store_handle_t _sh)
{
	struct sos_instance *si = _sh;

	if (!si)
		return;

	pthread_mutex_lock(&cfg_lock);
	LIST_REMOVE(si, entry);
	pthread_mutex_unlock(&cfg_lock);

	if (si->sos_handle)
		put_container(si->sos_handle);
	if (si->path)
		free(si->path);
	free(si->container);
	free(si);
}

static int constructor(ldmsd_plug_handle_t handle)
{
	mylog = ldmsd_plug_log_get(handle);

        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
}

struct ldmsd_store ldmsd_plugin_interface = {
	.base = {
		.name = STORE,
		.config = config,
		.usage = usage,
		.type = LDMSD_PLUGIN_STORE,
		.constructor = constructor,
		.destructor = destructor,
	},
	.open = open_store,
	.store = store,
	.flush = flush_store,
	.close = close_store,
};

static void __attribute__ ((constructor)) store_papi_init();
static void store_papi_init()
{
	pthread_mutex_init(&cfg_lock, NULL);
	LIST_INIT(&sos_handle_list);
}

static void __attribute__ ((destructor)) store_papi_fini(void);
static void store_papi_fini()
{
	pthread_mutex_destroy(&cfg_lock);
	/* TODO: clean up container and metric trees */
}
