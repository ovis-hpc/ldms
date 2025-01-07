/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2018 Open Grid Computing, Inc. All rights reserved.
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
 * \file shm_sampler.c
 * \brief shm data provider
 *
 * Reads shm containers of counters.
 */

#define _GNU_SOURCE
#include <sys/errno.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ldms.h"
#include "ldmsd.h"
#include "../sampler_base.h"

#include "shm_util/ldms_shm_index.h"
#include "shm_util/ldms_shm_event_set.h"

typedef struct ldms_shm_box {
	base_data_t base; /* ldms base defined for this box */
	ldms_shm_set_t shm_set;
	uint64_t schema_cksum; /* checksum of schema */
	int current; /* 1 if still in index when checked */
	int index_in_slot_list;
	int metric_offset;
} ldms_shm_box_t;

struct box_index {
	int index;
	LIST_ENTRY(box_index)
	entry;
};

LIST_HEAD( free_box_list, box_index)
free_box_list;
LIST_HEAD( inuse_box_list, box_index)
inuse_box_list;

/* box_cache collects the invariants.
 * These exist to deal with dynamic numbers of processes
 * on the node being sampled as a job progresses.
 *
 * The three size limits are necessary so that the
 * sampler (which is configured and run by root)
 * cannot be used by unprivileged user processes to
 * crash the ldmsd by violating fixed maximum set memory.
 * The maximum set RAM needed on the ldms side to support the
 * sampler will be * approximately
 * 	box_len * metric_max * array_max * sizeof(uint64_t)
 * bytes.
 *
 * Note: This object really needs to be a
 * member of a sampler instance self object so that multiple
 * shms can be configured simultaneously.
 */
typedef struct ldms_shm_box_cache {
	char *producer_name;
	char *instance_prefix; /* common prefix of instances generated */
	char *schema_prefix; /* common prefix of schema's generated. */
	char *index_name; /* index file system name */
	ldms_shm_index_t index; /* pointer to open shared memory index */
	uint64_t last_gen; /* generation number of index last time we looked */
	int metric_max; /* maximum total of (meta)metric elements in user set */
	int array_max; /* maximum array/string size allowed in user sets. */
	int box_len; /* size of box pointer array */
	int set_timeout; /* how long before we assume the set is out of use,
	 as can happen when a process dies without
	 unregistering its set. */
} ldms_shm_box_cache_t;
static ldms_shm_box_cache_t box_cache;

#define ARRAY_MAX_DEFAULT 1024
#define METRIC_MAX_DEFAULT 1024
#define BOX_LEN_DEFAULT 4
#define SHM_TIMEOUT_DEFAULT 10
#define SAMP "shm_sampler"

static ovis_log_t mylog;

static ldms_shm_box_t *boxes = NULL;
static base_data_t initial_base_config = NULL;

static char *INDEX_NAME_DEFAULT = "/dev/shm/ldms_shm_index";
static int ldms_shm_metric_set_counter = 0;

static int is_active(ldms_shm_box_t *box)
{
	return (NULL != box && 0 != box->current);
}

static int initialize_box_cache(const char* const index_name, int boxlen,
		int metric_max, int array_max, int shm_set_timeout)
{
	int rc = 0;
	if(NULL != index_name)
		box_cache.index_name = strdup(index_name);
	if(NULL != initial_base_config->instance_name)
		box_cache.instance_prefix = strdup(
				initial_base_config->instance_name);
	if(NULL != initial_base_config->schema_name)
		box_cache.schema_prefix = strdup(
				initial_base_config->schema_name);
	if(NULL != initial_base_config->producer_name)
		box_cache.producer_name = strdup(
				initial_base_config->producer_name);

	if(NULL == box_cache.instance_prefix || NULL == box_cache.schema_prefix
			|| NULL == box_cache.index_name
			|| NULL == box_cache.producer_name) {
		ovis_log(mylog, OVIS_LERROR, "config OOM\n");
		rc = ENOMEM;
		errno = rc;
		return rc;
	}
	box_cache.box_len = boxlen;
	box_cache.metric_max = metric_max;
	box_cache.last_gen = -1;
	box_cache.array_max = array_max;
	box_cache.set_timeout = shm_set_timeout;
	box_cache.index = ldms_shm_index_open(box_cache.index_name,
			box_cache.box_len, box_cache.metric_max,
			box_cache.array_max, box_cache.set_timeout);
	if(box_cache.index == NULL) {
		ovis_log(mylog, OVIS_LERROR, "failed to open shm index %s\n",
				box_cache.index_name);
		rc = ENOENT;
		errno = rc;
		return rc;
	} else {
		ovis_log(mylog, OVIS_LINFO,
		"index (%s) was successfully opened\n",
				box_cache.index_name);
	}
	return rc;
}

static void clear_box_cache()
{
	if(box_cache.producer_name)
		free(box_cache.producer_name);
	box_cache.producer_name = NULL;
	if(box_cache.instance_prefix)
		free(box_cache.instance_prefix);
	box_cache.instance_prefix = NULL;
	if(box_cache.schema_prefix)
		free(box_cache.schema_prefix);
	box_cache.schema_prefix = NULL;
	if(box_cache.index_name)
		free(box_cache.index_name);
	box_cache.index_name = NULL;
	/* FIXME clean index shared memory?*/
	if(box_cache.index_name)
		free(box_cache.index);
	box_cache.index = NULL;

	box_cache.last_gen = -1;
	box_cache.metric_max = -1;
	box_cache.array_max = -1;
	box_cache.box_len = -1;
	box_cache.set_timeout = -1;

}

static int initial_setup(const char* const index_name, int boxlen,
		int metric_max, int array_max, int shm_set_timeout)
{
	int rc = initialize_box_cache(index_name, boxlen, metric_max, array_max,
			shm_set_timeout);
	if(rc) {
		clear_box_cache();
		return rc;
	}
	/* allocate array box[box_len] stubs */
	boxes = calloc(box_cache.box_len, sizeof(ldms_shm_box_t));
	if(!boxes) {
		ovis_log(mylog, OVIS_LERROR, "out of mem %s\n", index_name);
		clear_box_cache();
		rc = ENOMEM;
		errno = rc;
		return rc;
	}
	int b;
	for(b = box_cache.box_len - 1; b >= 0; b--) {
		boxes[b].base = NULL;
		boxes[b].shm_set = NULL;
		boxes[b].schema_cksum = -1;
		boxes[b].index_in_slot_list = -1;
		boxes[b].metric_offset = -1;
		boxes[b].current = 0;
		struct box_index *current_box = calloc(1, sizeof(*current_box));
		current_box->index = b;
		LIST_INSERT_HEAD(&free_box_list, current_box, entry);
	}
	return rc;
}

static inline int index_changed(uint64_t gen)
{
	return (gen != box_cache.last_gen);
}

static inline int more_instances(int vaild_entry_cnt)
{
	return (vaild_entry_cnt
			< ldms_shm_index_get_instace_count(box_cache.index));
}

static char * build_instance_name(ldms_shm_index_entry_t entry)
{
	int num_delimiter_chars = 1;
	const char* set_label = ldms_shm_index_entry_set_label_get(entry);
	size_t instance_name_len = strlen(box_cache.instance_prefix)
			+ strlen(set_label) + num_delimiter_chars + 1;
	char* instance_name = calloc(instance_name_len, sizeof(char));
	if(!instance_name) {
		ovis_log(mylog, OVIS_LERROR,
		"failed to allocate memory for instance: %s\n",
				instance_name);
		errno = ENOMEM;
		return NULL;
	}
	snprintf(instance_name, instance_name_len, "%s_%s",
			box_cache.instance_prefix, set_label);
	return instance_name;
}

static int check_for_free_slot()
{
	ovis_log(mylog, OVIS_LDEBUG, "checking for free slot\n");
	int rc = 0;
	if(LIST_EMPTY(&free_box_list)) {
		ovis_log(mylog, OVIS_LERROR,
		"no space left in box array for the new entry\n");
		errno = ENOMEM;
		rc = errno;
	}
	return rc;
}

static int check_for_duplicate_instances(const char* instance_name)
{
	ovis_log(mylog, OVIS_LDEBUG, "checking for duplicate instances\n");
	int i, rc = 0;
	for(i = 0; i < box_cache.box_len; i++) {
		if(is_active(&boxes[i])
				&& 0
						== strcmp(instance_name,
								boxes[i].base->instance_name)) {
			ovis_log(mylog, OVIS_LERROR, "duplicate instance: %s\n",
					instance_name);
			errno = EEXIST;
			rc = errno;
			break;
		}
	}
	return rc;
}

static ldms_shm_box_t * allocate_slot_for_box()
{
	ovis_log(mylog, OVIS_LDEBUG, "allocating slot for the box\n");
	struct box_index *next_free = LIST_FIRST(&free_box_list);
	ldms_shm_box_t *box = &boxes[next_free->index];
	box->index_in_slot_list = next_free->index;
	LIST_REMOVE(next_free, entry);
	LIST_INSERT_HEAD(&inuse_box_list, next_free, entry);
	return box;
}

static void recover_box_slot(ldms_shm_box_t *box)
{
	struct box_index *bi;
	LIST_FOREACH(bi, &inuse_box_list, entry)
	{
		if(bi->index == box->index_in_slot_list) {
			LIST_REMOVE(bi, entry);
			LIST_INSERT_HEAD(&free_box_list, bi, entry);
			break;
		}
	}
}

static void clear_box(ldms_shm_box_t *box)
{
	if(NULL == box)
		return;
	if(NULL != box->base && box->base->set) {
		ldms_set_delete(box->base->set);
		ovis_log(mylog, OVIS_LDEBUG, "ldms set was deleted in clear_box\n");
	}
	if(box->base)
		base_del(box->base);
	box->schema_cksum = 0;
	box->current = 0;

	if(NULL != box->shm_set) {
		ldms_shm_clear_set(box->shm_set);
		free(box->shm_set);
	}
	box->shm_set = NULL;

	recover_box_slot(box);
}

static int create_base_for_box(ldms_shm_box_t *box, const char* schema_name,
		const char* instance_name)
{
	int rc = 0;
	box->base = calloc(1, sizeof(*box->base));
	if(!box->base) {
		errno = ENOMEM;
		rc = errno;
		return rc;
	}
	box->base->app_id_idx = initial_base_config->app_id_idx;
	box->base->component_id = initial_base_config->component_id;
	box->base->job_end_idx = initial_base_config->job_end_idx;
	box->base->job_id_idx = initial_base_config->job_id_idx;
	box->base->job_start_idx = initial_base_config->job_start_idx;
	box->base->auth.uid = initial_base_config->auth.uid;
	box->base->auth.gid = initial_base_config->auth.gid;
	box->base->auth.perm = initial_base_config->auth.perm;

	box->base->producer_name = strdup(initial_base_config->producer_name);
	box->base->schema_name = strdup(schema_name);
	box->base->instance_name = strdup(instance_name);

	return rc;
}

static int validate_shm_set(ldms_shm_set_t shm_set, uint64_t shm_set_checksum)
{
	ovis_log(mylog, OVIS_LDEBUG, "validating the shm_set\n");
	int rc = 0, e;
	// compute its checksum
	uint64_t checksum = ldms_shm_set_calc_checksum(shm_set);
	// check checksum against checksum stored in index.
	if(shm_set_checksum != checksum) {
		ovis_log(mylog, OVIS_LERROR,
				"computed checksum %" PRIu64 " for the set, is not equal to the stored checksum %" PRIu64 " in the index!\n",
				checksum, shm_set_checksum);
		errno = EINVAL;
		rc = errno;
		return rc;
	}

	if(shm_set->meta->total_events > box_cache.metric_max) {
		ovis_log(mylog, OVIS_LERROR,
				"number of events (%d) are more than the configured maximum (%d)\n",
				shm_set->meta->total_events,
				box_cache.metric_max);
		errno = EINVAL;
		rc = errno;
		return rc;
	}
	for(e = 0; e < shm_set->meta->num_events; e++) {
		ldms_shm_event_desc_t* event = ldms_shm_set_get_event(shm_set,
				e);
		if(strlen(event->name) > box_cache.array_max) {
			ovis_log(mylog, OVIS_LERROR,
					"length of event name (%s) is more than the configured maximum (%d)\n",
					event->name, box_cache.array_max);
			rc = EINVAL;
			return rc;
		}
	}
	return rc;
}

static inline int is_array(ldms_shm_event_desc_t* event)
{
	return event->num_elements > 1;
}

/**
 * This is a temporary fix for issues with ldms_set_delete.
 * We delete a set when we are done with sampling in function clear_box.
 * It does not always delete the set, because set->local_rbd_list is not always empty.
 * So, when we want to create a new set with the same name, we get EEXISTS error.
 * Here, we add a counter to the end of the set name to fix this issue temporarily.
 */
static void handle_ldms_set_delete_issue(ldms_shm_box_t* box)
{
	if(EEXIST == errno) {

		int ldms_shm_metric_set_counter_len =
				ldms_shm_metric_set_counter == 0 ?
						1 :
						log10(
								ldms_shm_metric_set_counter)
								+ 1;
		int num_delimiter_chars = 1;
		size_t instance_name_len = strlen(box->base->instance_name)
				+ num_delimiter_chars
				+ ldms_shm_metric_set_counter_len + 1;

		char* instance_name = calloc(instance_name_len, sizeof(char));
		if(!instance_name) {
			ovis_log(mylog, OVIS_LERROR,
					"failed to allocate memory of size=%zu (%zu + %d + %d + 1) for instance %s\n",
					instance_name_len,
					strlen(box->base->instance_name),
					num_delimiter_chars,
					ldms_shm_metric_set_counter_len,
					box->base->instance_name);
			errno = ENOMEM;
			return;
		}
		sprintf(instance_name, "%s_%d", box->base->instance_name,
				ldms_shm_metric_set_counter++);
		ovis_log(mylog, OVIS_LDEBUG,
				"Because of the issues with ldms_set_delete, name of the set was changed from %s to %s\n\r",
				box->base->instance_name, instance_name);
		free(box->base->instance_name);
		box->base->instance_name = instance_name;

		box->base->set = base_set_new(box->base);
	}
}

static int create_metric_set(ldms_shm_box_t *box, ldms_shm_set_t shm_set,
		uint64_t shm_set_checksum)
{
	ovis_log(mylog, OVIS_LDEBUG,
			"creating metric_set based on the shm_set\n");
	int rc, e;

	rc = validate_shm_set(shm_set, shm_set_checksum);
	if(rc)
		return rc;
	box->schema_cksum = shm_set_checksum;
	base_schema_new(box->base);
	if(!box->base->schema) {
		ovis_log(mylog, OVIS_LERROR,
				"%s: The schema '%s' could not be created, errno=%d.\n",
				__FILE__, box->base->schema_name, errno);
		rc = errno;
		return rc;
	}

	box->metric_offset = ldms_schema_metric_count_get(box->base->schema);

	/* create matching ldms_schema
	 add metrics based on shm_set */
	for(e = 0; e < shm_set->meta->num_events; e++) {
		ldms_shm_event_desc_t* event = ldms_shm_set_get_event(shm_set,
				e);
		if(is_array(event)) {
			rc = ldms_schema_metric_array_add(box->base->schema,
					event->name, LDMS_V_U64_ARRAY,
					event->num_elements);
			ovis_log(mylog, OVIS_LDEBUG,
					"event %d (%s) of array type with %d elements has been added to metric set\n\r",
					e, event->name, event->num_elements);
		} else {
			rc = ldms_schema_metric_add(box->base->schema,
					event->name, LDMS_V_U64);
		}
		if(rc < 0) {
			ovis_log(mylog, OVIS_LERROR,
			"failed to add event %s to metric set (%d).\n",
					event->name, rc);
			return rc;
		} else {
			rc = 0;
		}

	}
	box->base->set = base_set_new(box->base);
	if(!box->base->set) {
		handle_ldms_set_delete_issue(box);
		if(!box->base->set) {
			ovis_log(mylog, OVIS_LERROR,
			"(%d) failed to create metric set %s.\n",
			errno, box->base->instance_name);
			rc = errno;
			return rc;
		}
	}
	return rc;
}

static int add_mbox(ldms_shm_index_entry_t entry)
{
	ovis_log(mylog, OVIS_LDEBUG, "creating box from index entry\n");
	int rc;
	rc = check_for_free_slot();
	if(rc)
		return rc;
	char* instance_name = build_instance_name(entry);
	if(NULL == instance_name) {
		rc = errno;
		return rc;
	}

	rc = check_for_duplicate_instances(instance_name);
	if(rc) {
		free(instance_name);
		return rc;
	}

	ldms_shm_box_t *box = allocate_slot_for_box();

	box->shm_set = ldms_shm_data_open(entry);
	if(box->shm_set == NULL) {
		ovis_log(mylog, OVIS_LERROR, "no set found for entry: %s\n",
				ldms_shm_index_entry_set_label_get(entry));
		free(instance_name);
		recover_box_slot(box);
		errno = ENOENT;
		rc = errno;
		return rc;
	} else {
		ovis_log(mylog, OVIS_LDEBUG,
				"read data from the entry on index\n");
	}
	box->current = 1;

	rc = create_base_for_box(box, box_cache.schema_prefix, instance_name);
	if(rc) {
		free(instance_name);
		recover_box_slot(box);
		clear_box(box);
		return rc;
	}

	rc = create_metric_set(box, box->shm_set,
			ldms_shm_index_entry_get_schema_checksum(entry));
	if(rc) {
		ovis_log(mylog, OVIS_LERROR,
		"failed to create metric set for %s\n", instance_name);
		free(instance_name);
		recover_box_slot(box);
		clear_box(box);
		errno = rc;
	} else {
		ovis_log(mylog, OVIS_LDEBUG,
				"shm_box for instance %s has been configured successfully!\n\r",
				instance_name);
		free(instance_name);
	}
	return rc;
}

static ldms_shm_box_t* findbox(int instance_index)
{
	uint64_t schema_cksum = ldms_shm_index_get_schema_checksum(
			box_cache.index, instance_index);
	if(schema_cksum == 0)
		return NULL;
	int i;
	for(i = 0; i < box_cache.box_len; i++) {
		if(schema_cksum == boxes[i].schema_cksum) {
			return &boxes[i];
		}
	}
	return NULL;
}

static int init_box_from_entry(int instance_index)
{
	int rc = 0;
	ldms_shm_box_t* box = findbox(instance_index);
	if(!box) {

		ldms_shm_index_entry_t entry =
				ldms_shm_index_entry_register_instance_reader(
						box_cache.index,
						instance_index);
		if(NULL == entry) {
			ovis_log(mylog, OVIS_LERROR,
					"could not register as reader for instance %d\n\r",
					instance_index);
			rc = ENOENT;
		} else {
			ovis_log(mylog, OVIS_LDEBUG,
					"registered as the reader for instance_index =%d\n",
					instance_index);
			rc = add_mbox(entry);
			if(rc) {
				ovis_log(mylog, OVIS_LERROR,
						"failed to add box for instance i = %d\n\r",
						instance_index);
				int num_of_users =
						ldms_shm_index_entry_deregister_reader(
								entry);
				if(0 == num_of_users) {
					/* Nobody uses this set. It's time for cleanup */
					ldms_shm_index_entry_clean_shared_resources(
							entry);
					ovis_log(mylog, OVIS_LERROR,
							"no users were found for the set of instance i = %d\n\r",
							instance_index);
				}
			}
		}
	} else {
		box->current = 1;
	}
	return rc;
}

static void clear_mboxes(ldms_shm_box_t *boxes, int box_len)
{
	if(NULL != boxes) {
		int i;
		for(i = 0; i < box_len; i++) {
			clear_box(&(boxes[i]));
		}
	}
}

static void clear_inactive_boxes()
{
	int i;
	for(i = 0; i < box_cache.box_len; i++) {
		if(!is_active(&boxes[i])) {
			clear_box(&(boxes[i]));
		}
	}
}

static int get_updates_from_index()
{
	ovis_log(mylog, OVIS_LDEBUG, "getting updates from index\n");
	int rc = 0;
	if(ldms_shm_index_is_empty(box_cache.index)) {
		ovis_log(mylog, OVIS_LINFO,
		"index is empty. nothing to sample! \n\r");
		return rc;
	}
	int i, vaild_entry_cnt = 0;
	for(i = 0; i < box_cache.box_len && more_instances(vaild_entry_cnt);
			i++) {
		if(ldms_shm_index_is_instance_empty(box_cache.index, i))
			continue;
		int rc = init_box_from_entry(i);
		if(rc)
			break;
		vaild_entry_cnt++;
	}
	return rc;
}

static int check_for_index_update()
{
	int rc = 0;
	ldms_shm_index_lock();

	uint64_t gen = ldms_shm_gn_get(box_cache.index);
	if(!index_changed(gen)) {
		ldms_shm_index_unlock();
		return rc;
	}
	ovis_log(mylog, OVIS_LINFO,
	"index (%s) has been changed since last scan\n",
			box_cache.index_name);
	box_cache.last_gen = gen;

	rc = get_updates_from_index();

	ldms_shm_index_unlock();

	return rc;
}

/*
 * scan (or rescan) the index shared memory list that defines
 * the metric set instances and schemas. On a rescan, all parameters
 * are ignored (the first call must cache everything).
 *
 * \param box_len 0 on a rescan or (if > 1) the number of boxes to allocate
 * stubs for or (if < 0) clean up resources for shutdown.
 * Fewer than box_len boxes may be active at any given time.
 *
 */
static int scan_index(const char* const index_name, int boxlen, int metric_max,
		int array_max, int shm_set_timeout)
{
	int rc = 0;
	if(boxlen < 0) {
		clear_mboxes(boxes, boxlen);
		return rc;
	}
	if(boxlen >= 1) {
		rc = initial_setup(index_name, boxlen, metric_max, array_max,
				shm_set_timeout);
		if(rc)
			return rc;
	}
	rc = check_for_index_update();
	if(rc) {
		ovis_log(mylog, OVIS_LERROR,
		"error in check for update\n");
		clear_mboxes(boxes, box_cache.box_len);
		clear_box_cache();
		errno = rc;
	} else {
		clear_inactive_boxes();
	}
	return rc;
}

static int shm_sampler_config(struct ldmsd_plugin *self, struct attr_value_list* avl)
{

	int rc;
	initial_base_config = base_config(avl, self->inst_name, SAMP, mylog);
	if(!initial_base_config) {
		rc = errno;
		base_del(initial_base_config);
		return rc;
	}

	char* index_name;
	char* boxmax;
	char* shm_array_max;
	char* shm_metric_max;
	char* shm_set_timeout_str;

	boxmax = av_value(avl, "shm_boxmax");
	int box_len = BOX_LEN_DEFAULT;
	if(boxmax) {
		int blen = atoi(boxmax);
		if(blen < 1) {
			ovis_log(mylog, OVIS_LERROR, "shm_boxmax bad %s\n",
					boxmax);
			errno = EINVAL;
			base_del(initial_base_config);
			return EINVAL;
		}
		box_len = blen;
	}

	shm_set_timeout_str = av_value(avl, "shm_set_timeout");
	int shm_set_timeout = SHM_TIMEOUT_DEFAULT;
	if(shm_set_timeout_str) {
		int amax = atoi(shm_set_timeout_str);
		if(amax < 0) {
			ovis_log(mylog, OVIS_LERROR,
					"shm_set_timeout bad %s\n",
					shm_set_timeout_str);
			errno = EINVAL;
			base_del(initial_base_config);
			return EINVAL;
		}
		shm_set_timeout = amax;
	}

	shm_array_max = av_value(avl, "shm_array_max");
	int array_max = ARRAY_MAX_DEFAULT;
	if(shm_array_max) {
		int amax = atoi(shm_array_max);
		if(amax < 1) {
			ovis_log(mylog, OVIS_LERROR,
					"shm_array_max bad %s\n",
					shm_array_max);
			errno = EINVAL;
			base_del(initial_base_config);
			return EINVAL;
		}
		array_max = amax;
	}

	shm_metric_max = av_value(avl, "shm_metric_max");
	int metric_max = METRIC_MAX_DEFAULT;
	if(shm_metric_max) {
		int amax = atoi(shm_metric_max);
		if(amax < 1) {
			ovis_log(mylog, OVIS_LERROR,
					"shm_metric_max bad %s\n",
					shm_metric_max);
			errno = EINVAL;
			base_del(initial_base_config);
			return EINVAL;
		}
		metric_max = amax;
	}

	index_name = av_value(avl, "shm_index");
	if(!index_name)
		index_name = INDEX_NAME_DEFAULT;

	if(strlen(index_name) == 0) {
		ovis_log(mylog, OVIS_LERROR, "shm_index invalid.\n");
		errno = EINVAL;
		base_del(initial_base_config);
		return EINVAL;
	}
	rc = scan_index(index_name, box_len, metric_max, array_max,
			shm_set_timeout);
	if(rc) {
		base_del(initial_base_config);
		return rc;
	}
	errno = rc;
	return rc;

}

/**
 * check for invalid flags, with particular emphasis on warning the user about
 */
static int config_check(struct attr_value_list *kwl,
		struct attr_value_list *avl, void *arg)
{
	return 0;
}

/**
 * \brief Configuration
 *
 * config name=shm_sampler producer=<producer_name> instance=<instance_name> [component_id=<compid>] [with_jobid=<jid>] shm_index=<shmfile> shm_boxmax=<max>.
 *     producer       The producer id value.
 *     instance       The set name PREFIX.
 *     component_id   The component id. Defaults to zero
 *     jid            lookup jobid or report 0.
 *     shmfile		Name of a shared memory index
 *     shm_boxmax	Maximum instances to be locally sampled, even if shm_index is larger.
 *     shm_array_max	Maximum length of any array or string.
 *     shm_metric_max	Maximum number of metrics from any process.
 *     shm_set_timeout	Maximum idle time on a set before it is destroyed. 0==infinity
 */
static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl,
		struct attr_value_list *avl)
{
	int rc;

	rc = config_check(kwl, avl, NULL);
	if(rc != 0) {
		return rc;
	}

	rc = shm_sampler_config(self, avl);

	if(rc) {
		ovis_log(mylog, OVIS_LERROR, "failed to config shm_sampler\n");
		return rc;
	}
	ovis_log(mylog, OVIS_LDEBUG, "has been configured successfully!\n");
	return 0;
}

static void check_box_activation(ldms_shm_box_t *box)
{
	if(!ldms_shm_set_is_active(box->shm_set)) {
		int num_of_users = ldms_shm_set_deregister_reader(box->shm_set);
		if(0 == num_of_users) {
			/* Nobody uses this set. It's time for cleanup */
			ldms_shm_set_clean_entry_shared_resources(box->shm_set);
			clear_box(box);
		}
	}
}

static int sample_set(struct ldmsd_sampler *self, ldms_shm_box_t *box)
{

	int shm_metric_index, ldms_metric_index;
	base_sample_begin(box->base);
	for(shm_metric_index = 0;
			shm_metric_index < box->shm_set->meta->num_events;
			shm_metric_index++) {
		ldms_metric_index = box->metric_offset + shm_metric_index;
		if(ldms_metric_is_array(box->base->set, ldms_metric_index)) {
			ldms_shm_data_t* data = ldms_shm_event_array_read(
					box->shm_set, shm_metric_index);
			/* (ldms_mval_t)data works because currently, it only has a uint64_t* in it. If this changes, this line also should be changed */
			ldms_metric_array_set(box->base->set, ldms_metric_index,
					(ldms_mval_t)data, 0,
					ldms_metric_array_get_len(
							box->base->set,
							ldms_metric_index));
		} else {
			ldms_shm_data_t data = ldms_shm_event_read(box->shm_set,
					shm_metric_index);
			/* (ldms_mval_t)data works because currently, it only has a uint64_t* in it. If this changes, this line also should be changed */
			ldms_metric_set(box->base->set, ldms_metric_index,
					(ldms_mval_t)&data);
		}
	}
	base_sample_end(box->base);
	check_box_activation(box);
	return 0;
}

static int sample(struct ldmsd_sampler *self)
{
	int rc = 0;
	if(ldms_shm_index_is_empty(box_cache.index)) {
		ovis_log(mylog, OVIS_LINFO, "index is empty\n");
		return rc;
	}
	if(!boxes) {
		ovis_log(mylog, OVIS_LERROR, "plugin not initialized\n");
		rc = EINVAL;
		errno = rc;
		return rc;
	}
	/* update set boxes */
	rc = scan_index(NULL, 0, 0, 0, 0);
	if(rc) {
		ovis_log(mylog, OVIS_LERROR, "error in scan_index\n");
		errno = rc;
		return rc;
	}
	int i;
	for(i = 0; i < box_cache.box_len; i++) {
		if(is_active(&boxes[i])) {
			rc = sample_set(self, &boxes[i]);
			if(rc) {
				ovis_log(mylog, OVIS_LERROR,
						"failed to sample the set %s\n",
						boxes[i].base->instance_name);
			}
		}
	}
	return 0;
}

static void term(struct ldmsd_plugin *self)
{
	ldms_shm_index_lock();

	int index_should_be_cleaned = 1, i;
	for(i = 0; i < box_cache.box_len; i++) {
		ldms_shm_box_t *box = &boxes[i];
		if(!ldms_shm_set_is_active(box->shm_set)) {
			int num_of_users = ldms_shm_set_deregister_reader(
					box->shm_set);
			if(0 == num_of_users) {
				/* Nobody uses this set. It's time for cleanup */
				ldms_shm_set_clean_entry_shared_resources(
						box->shm_set);
				clear_box(box);
			}
		} else {
			index_should_be_cleaned = 0;
		}
	}
	ldms_shm_index_unlock();
	if(index_should_be_cleaned
			&& ldms_shm_index_is_empty(box_cache.index)) {
		ldms_shm_index_clean_shared_resources(box_cache.index);
	}
	ldms_shm_clear_index(box_cache.index);
	clear_box_cache();
	if (mylog)
		ovis_log_destroy(mylog);
	ovis_log(mylog, OVIS_LINFO, SAMP " was successfully terminated\n");
}

static const char *usage(struct ldmsd_plugin *self)
{
	return "config name=" SAMP " producer=<name> instance=<name> [shm_index=<name>][shm_boxmax=<int>][shm_array_max=<int>][shm_metric_max=<int>]"
	"[shm_set_timeout=<int>][component_id=<int>] [schema=<name>]\n"
	"                [job_set=<name> job_id=<name> app_id=<name> job_start=<name> job_end=<name>]\n"
	"    producer     A unique name for the host providing the data\n"
	"    instance     A unique name for the metric set\n"
	"    shm_index    A unique name for the shared memory index file\n"
	"    shm_boxmax   Maximum number of entries in the shared memory index file\n"
	"    shm_array_max   Maximum number of elements in array metrics\n"
	"    shm_metric_max  Maximum number of metrics\n"
	"    shm_set_timeout No read/write timeout in seconds\n"
	"    component_id A unique number for the component being monitored, Defaults to zero.\n"
	"    schema       The name of the metric set schema, Defaults to the sampler name\n"
	"    job_set      The instance name of the set containing the job data, default is 'job_info'\n"
	"    job_id       The name of the metric containing the Job Id, default is 'job_id'\n"
	"    app_id       The name of the metric containing the Application Id, default is 'app_id'\n"
	"    job_start    The name of the metric containing the Job start time, default is 'job_start'\n"
	"    job_end      The name of the metric containing the Job end time, default is 'job_end'\n";
}

static struct ldmsd_sampler shm_plugin = {
		.base = {
			.name = SAMP,
			.type = LDMSD_PLUGIN_SAMPLER,
			.term = term,
			.config = config,
			.usage = usage,
		},
		.sample = sample,
};

struct ldmsd_plugin *get_plugin()
{
	int rc;
	mylog = ovis_log_register("sampler."SAMP, "The log subsystem of the " SAMP " plugin");
	if (!mylog) {
		rc = errno;
		ovis_log(NULL, OVIS_LWARN, "Failed to create the subsystem "
				"of '" SAMP "' plugin. Error %d\n", rc);
	}
	return &shm_plugin.base;
}
