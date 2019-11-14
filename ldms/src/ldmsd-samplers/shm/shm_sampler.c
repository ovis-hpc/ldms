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

#include "ldmsd.h"
#include "ldmsd_sampler.h"

#include "shm_util/ldms_shm_index.h"
#include "shm_util/ldms_shm_event_set.h"

#define ARRAY_MAX_DEFAULT 1024
#define METRIC_MAX_DEFAULT 1024
#define BOX_LEN_DEFAULT 4
#define SHM_TIMEOUT_DEFAULT 10

#define INST(x) ((ldmsd_plugin_inst_t)(x))
#define INST_LOG(inst, lvl, fmt, ...) \
		ldmsd_log((lvl), "%s: " fmt, INST(inst)->inst_name, \
								##__VA_ARGS__)

typedef struct ldms_shm_box {
	ldms_shm_set_t shm_set;
	uint64_t schema_cksum; /* checksum of schema */
	int current; /* 1 if still in index when checked */
	int index_in_slot_list;
	int metric_offset;
	char *instance_name;
	ldms_set_t ldms_set;
} ldms_shm_box_t;

struct box_index {
	int index;
	LIST_ENTRY(box_index)
	entry;
};

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

static const char *INDEX_NAME_DEFAULT = "/dev/shm/ldms_shm_index";

typedef struct shm_sampler_inst_s *shm_sampler_inst_t;
struct shm_sampler_inst_s {
	struct ldmsd_plugin_inst_s base;

	int (*samp_sample)(ldmsd_plugin_inst_t inst);

	LIST_HEAD(, box_index) free_box_list;
	LIST_HEAD(, box_index) inuse_box_list;
	ldms_shm_box_cache_t box_cache;
	ldms_shm_box_t *boxes;
	int ldms_shm_metric_set_counter;
};

/* ============== Utility functions ============== */

static int is_active(ldms_shm_box_t *box)
{
	return (NULL != box && 0 != box->current);
}

static int initialize_box_cache(shm_sampler_inst_t inst,
				const char* const index_name, int boxlen,
				int metric_max, int array_max,
				int shm_set_timeout)
{
	ldmsd_sampler_type_t samp = (void*)inst->base.base;
	int rc = 0;
	if (NULL != index_name)
		inst->box_cache.index_name = strdup(index_name);
	if (NULL != samp->set_inst_name)
		inst->box_cache.instance_prefix = strdup(samp->set_inst_name);
	if (NULL != samp->schema_name)
		inst->box_cache.schema_prefix = strdup(samp->schema_name);
	if (NULL != samp->producer_name)
		inst->box_cache.producer_name = strdup(samp->producer_name);

	if (NULL == inst->box_cache.instance_prefix
			|| NULL == inst->box_cache.schema_prefix
			|| NULL == inst->box_cache.index_name
			|| NULL == inst->box_cache.producer_name) {
		INST_LOG(inst, LDMSD_LERROR, "config OOM\n");
		rc = ENOMEM;
		errno = rc;
		return rc;
	}
	inst->box_cache.box_len = boxlen;
	inst->box_cache.metric_max = metric_max;
	inst->box_cache.last_gen = -1;
	inst->box_cache.array_max = array_max;
	inst->box_cache.set_timeout = shm_set_timeout;
	inst->box_cache.index = ldms_shm_index_open(inst->box_cache.index_name,
			inst->box_cache.box_len, inst->box_cache.metric_max,
			inst->box_cache.array_max, inst->box_cache.set_timeout);
	if (inst->box_cache.index == NULL) {
		INST_LOG(inst, LDMSD_LERROR,
			 "failed to open shm index %s\n",
			 inst->box_cache.index_name);
		rc = ENOENT;
		errno = rc;
		return rc;
	} else {
		INST_LOG(inst, LDMSD_LINFO,
			 "index (%s) was successfully opened\n",
			 inst->box_cache.index_name);
	}
	return rc;
}

static void clear_box_cache(shm_sampler_inst_t inst)
{
	if (inst->box_cache.producer_name)
		free(inst->box_cache.producer_name);
	inst->box_cache.producer_name = NULL;
	if (inst->box_cache.instance_prefix)
		free(inst->box_cache.instance_prefix);
	inst->box_cache.instance_prefix = NULL;
	if (inst->box_cache.schema_prefix)
		free(inst->box_cache.schema_prefix);
	inst->box_cache.schema_prefix = NULL;
	if (inst->box_cache.index_name)
		free(inst->box_cache.index_name);
	inst->box_cache.index_name = NULL;
	/* FIXME clean index shared memory?*/
	if (inst->box_cache.index_name)
		free(inst->box_cache.index);
	inst->box_cache.index = NULL;

	inst->box_cache.last_gen = -1;
	inst->box_cache.metric_max = -1;
	inst->box_cache.array_max = -1;
	inst->box_cache.box_len = -1;
	inst->box_cache.set_timeout = -1;

}

static int initial_setup(shm_sampler_inst_t inst,
			 const char* const index_name, int boxlen,
			 int metric_max, int array_max, int shm_set_timeout)
{
	int rc = initialize_box_cache(inst, index_name, boxlen, metric_max,
				      array_max, shm_set_timeout);
	if (rc) {
		clear_box_cache(inst);
		return rc;
	}
	/* allocate array box[box_len] stubs */
	inst->boxes = calloc(inst->box_cache.box_len, sizeof(ldms_shm_box_t));
	if (!inst->boxes) {
		INST_LOG(inst, LDMSD_LERROR, "out of mem %s\n", index_name);
		clear_box_cache(inst);
		rc = ENOMEM;
		errno = rc;
		return rc;
	}
	int b;
	for (b = inst->box_cache.box_len - 1; b >= 0; b--) {
		inst->boxes[b].shm_set = NULL;
		inst->boxes[b].schema_cksum = -1;
		inst->boxes[b].index_in_slot_list = -1;
		inst->boxes[b].metric_offset = -1;
		inst->boxes[b].current = 0;
		struct box_index *current_box = calloc(1, sizeof(*current_box));
		current_box->index = b;
		LIST_INSERT_HEAD(&inst->free_box_list, current_box, entry);
	}
	return rc;
}

static inline int index_changed(shm_sampler_inst_t inst, uint64_t gen)
{
	return (gen != inst->box_cache.last_gen);
}

static inline int more_instances(shm_sampler_inst_t inst, int vaild_entry_cnt)
{
	return (vaild_entry_cnt
		< ldms_shm_index_get_instace_count(inst->box_cache.index));
}

static char * build_instance_name(shm_sampler_inst_t inst,
				  ldms_shm_index_entry_t entry)
{
	int num_delimiter_chars = 1;
	const char* set_label = ldms_shm_index_entry_set_label_get(entry);
	size_t instance_name_len = strlen(inst->box_cache.instance_prefix)
					  + strlen(set_label)
					  + num_delimiter_chars + 1;
	char* instance_name = calloc(instance_name_len, sizeof(char));
	if (!instance_name) {
		INST_LOG(inst, LDMSD_LERROR,
			 "failed to allocate memory for instance: %s\n",
			 instance_name);
		errno = ENOMEM;
		return NULL;
	}
	snprintf(instance_name, instance_name_len, "%s_%s",
			inst->box_cache.instance_prefix, set_label);
	return instance_name;
}

static int check_for_free_slot(shm_sampler_inst_t inst)
{
	INST_LOG(inst, LDMSD_LDEBUG, "checking for free slot\n");
	int rc = 0;
	if(LIST_EMPTY(&inst->free_box_list)) {
		INST_LOG(inst, LDMSD_LERROR,
			 "no space left in box array for the new entry\n");
		errno = ENOMEM;
		rc = errno;
	}
	return rc;
}

static int check_for_duplicate_instances(shm_sampler_inst_t inst,
					 const char* instance_name)
{
	INST_LOG(inst, LDMSD_LDEBUG, "checking for duplicate instances\n");
	int i, rc = 0;
	for (i = 0; i < inst->box_cache.box_len; i++) {
		if (is_active(&inst->boxes[i])
				&& 0 == strcmp(instance_name,
					       inst->boxes[i].instance_name)) {
			INST_LOG(inst, LDMSD_LERROR,
				 "duplicate instance: %s\n", instance_name);
			errno = EEXIST;
			rc = errno;
			break;
		}
	}
	return rc;
}

static ldms_shm_box_t * allocate_slot_for_box(shm_sampler_inst_t inst)
{
	INST_LOG(inst, LDMSD_LDEBUG, "allocating slot for the box\n");
	struct box_index *next_free = LIST_FIRST(&inst->free_box_list);
	ldms_shm_box_t *box = &inst->boxes[next_free->index];
	box->index_in_slot_list = next_free->index;
	LIST_REMOVE(next_free, entry);
	LIST_INSERT_HEAD(&inst->inuse_box_list, next_free, entry);
	return box;
}

static void recover_box_slot(shm_sampler_inst_t inst, ldms_shm_box_t *box)
{
	struct box_index *bi;
	LIST_FOREACH(bi, &inst->inuse_box_list, entry)
	{
		if (bi->index == box->index_in_slot_list) {
			LIST_REMOVE(bi, entry);
			LIST_INSERT_HEAD(&inst->free_box_list, bi, entry);
			break;
		}
	}
}

static void clear_box(shm_sampler_inst_t inst, ldms_shm_box_t *box)
{
	if (NULL == box)
		return;
	if (box->instance_name)
		free(box->instance_name);
	if (NULL != box->ldms_set) {
		ldms_set_delete(box->ldms_set);
		INST_LOG(inst, LDMSD_LDEBUG,
			 "ldms set was deleted in clear_box\n");
		box->ldms_set = NULL;
	}
	box->schema_cksum = 0;
	box->current = 0;
	if (NULL != box->shm_set) {
		ldms_shm_clear_set(box->shm_set);
		free(box->shm_set);
	}
	box->shm_set = NULL;
	recover_box_slot(inst, box);
}

static int validate_shm_set(shm_sampler_inst_t inst,
			    ldms_shm_set_t shm_set, uint64_t shm_set_checksum)
{
	INST_LOG(inst, LDMSD_LDEBUG, "validating the shm_set\n");
	int rc = 0, e;
	// compute its checksum
	uint64_t checksum = ldms_shm_set_calc_checksum(shm_set);
	// check checksum against checksum stored in index.
	if (shm_set_checksum != checksum) {
		INST_LOG(inst, LDMSD_LERROR,
			 "computed checksum %" PRIu64 " for the set, is not "
			 "equal to the stored checksum %" PRIu64 " in the "
			 "index!\n", checksum, shm_set_checksum);
		errno = EINVAL;
		rc = errno;
		return rc;
	}

	if (shm_set->meta->total_events > inst->box_cache.metric_max) {
		INST_LOG(inst, LDMSD_LERROR,
			 "number of events (%d) are more than the configured "
			 "maximum (%d)\n",
			 shm_set->meta->total_events,
			 inst->box_cache.metric_max);
		errno = EINVAL;
		rc = errno;
		return rc;
	}
	for (e = 0; e < shm_set->meta->num_events; e++) {
		ldms_shm_event_desc_t* event =
					ldms_shm_set_get_event(shm_set, e);
		if(strlen(event->name) > inst->box_cache.array_max) {
			INST_LOG(inst, LDMSD_LERROR,
				 "length of event name (%s) is more than the "
				 "configured maximum (%d)\n",
				 event->name, inst->box_cache.array_max);
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
static void handle_ldms_set_delete_issue(shm_sampler_inst_t inst,
					 ldms_schema_t schema,
					 ldms_shm_box_t* box)
{
	if (EEXIST == errno) {

		ldmsd_sampler_type_t samp = (void*)inst->base.base;
		int ldms_shm_metric_set_counter_len =
			inst->ldms_shm_metric_set_counter == 0 ?
				1 :
				log10(inst->ldms_shm_metric_set_counter) + 1;
		int num_delimiter_chars = 1;
		int instance_name_len = strlen(box->instance_name)
					+ num_delimiter_chars
					+ ldms_shm_metric_set_counter_len + 1;

		char* instance_name = calloc(instance_name_len, sizeof(char));
		if(!instance_name) {
			INST_LOG(inst, LDMSD_LERROR,
				 "shm_sampler: failed to allocate memory of "
				 "size=%d (%d + %d + %d + 1) for instance %s\n",
				 instance_name_len,
				 (int)strlen(box->instance_name),
				 num_delimiter_chars,
				 ldms_shm_metric_set_counter_len,
				 box->instance_name);
			errno = ENOMEM;
			return;
		}
		sprintf(instance_name, "%s_%d", box->instance_name,
					inst->ldms_shm_metric_set_counter++);
		INST_LOG(inst, LDMSD_LDEBUG,
			 "Because of the issues with ldms_set_delete, name "
			 "of the set was changed from %s to %s\n\r",
			 box->instance_name, instance_name);
		free(box->instance_name);
		box->instance_name = instance_name;

		box->ldms_set = samp->create_set(&inst->base,
						 instance_name,
						 schema,
						 box);
	}
}

static int create_metric_set(shm_sampler_inst_t inst, ldms_shm_box_t *box,
			     ldms_shm_set_t shm_set, uint64_t shm_set_checksum)
{
	ldmsd_sampler_type_t samp = (void*)inst->base.base;
	INST_LOG(inst, LDMSD_LDEBUG,
		 "creating metric_set based on the shm_set\n");
	int rc, e;
	ldms_schema_t schema = NULL;

	rc = validate_shm_set(inst, shm_set, shm_set_checksum);
	if (rc)
		return rc;
	box->schema_cksum = shm_set_checksum;
	schema = samp->create_schema(&inst->base);
	if (!schema) {
		INST_LOG(inst, LDMSD_LERROR,
			 "%s: The schema '%s' could not be created, "
			 "errno=%d.\n", __FILE__, samp->schema_name, errno);
		rc = errno;
		return rc;
	}

	/* create matching ldms_schema
	 add metrics based on shm_set */
	for (e = 0; e < shm_set->meta->num_events; e++) {
		ldms_shm_event_desc_t* event = ldms_shm_set_get_event(shm_set,
				e);
		if (is_array(event)) {
			rc = ldms_schema_metric_array_add(schema,
					event->name, LDMS_V_U64_ARRAY, "",
					event->num_elements);
			INST_LOG(inst, LDMSD_LDEBUG,
				"event %d (%s) of array type with %d elements "
				"has been added to metric set\n\r",
				e, event->name, event->num_elements);
		} else {
			rc = ldms_schema_metric_add(schema,
					event->name, LDMS_V_U64, "");
		}
		if (rc < 0) {
			INST_LOG(inst, LDMSD_LERROR,
				 "failed to add event %s to metric set (%d).\n",
				 event->name, rc);
			rc = -rc; /* rc = -errno */
			goto out;
		} else {
			rc = 0;
		}

	}
	box->ldms_set = samp->create_set(&inst->base, box->instance_name,
					 schema, box);
	if (!box->ldms_set) {
		handle_ldms_set_delete_issue(inst, schema, box);
		if (!box->ldms_set) {
			INST_LOG(inst, LDMSD_LERROR,
				 "(%d) failed to create metric set %s.\n",
				 errno, box->instance_name);
			rc = errno;
			goto out;
		}
	}
out:
	if (schema)
		ldms_schema_delete(schema);
	return rc;
}

static int add_mbox(shm_sampler_inst_t inst, ldms_shm_index_entry_t entry)
{
	INST_LOG(inst, LDMSD_LDEBUG, "creating box from index entry\n");
	int rc;
	rc = check_for_free_slot(inst);
	if(rc)
		return rc;
	char* instance_name = build_instance_name(inst, entry);
	if (NULL == instance_name) {
		rc = errno;
		return rc;
	}

	rc = check_for_duplicate_instances(inst, instance_name);
	if (rc) {
		free(instance_name);
		return rc;
	}

	ldms_shm_box_t *box = allocate_slot_for_box(inst);

	box->shm_set = ldms_shm_data_open(entry);
	if(box->shm_set == NULL) {
		INST_LOG(inst, LDMSD_LERROR, "no set found for entry: %s\n",
			 ldms_shm_index_entry_set_label_get(entry));
		free(instance_name);
		recover_box_slot(inst, box);
		errno = ENOENT;
		rc = errno;
		return rc;
	} else {
		INST_LOG(inst, LDMSD_LDEBUG,
			 "read data from the entry on index\n");
	}
	box->current = 1;
	box->instance_name = instance_name;

	rc = create_metric_set(inst, box, box->shm_set,
			       ldms_shm_index_entry_get_schema_checksum(entry));
	if (rc) {
		INST_LOG(inst, LDMSD_LERROR,
			 "failed to create metric set for %s\n", instance_name);
		recover_box_slot(inst, box);
		clear_box(inst, box);
		errno = rc;
	} else {
		INST_LOG(inst, LDMSD_LDEBUG,
			 "shm_box for instance %s has been configured "
			 "successfully!\n\r", instance_name);
	}
	return rc;
}

static ldms_shm_box_t* findbox(shm_sampler_inst_t inst, int instance_index)
{
	uint64_t schema_cksum = ldms_shm_index_get_schema_checksum(
					inst->box_cache.index, instance_index);
	if (schema_cksum == 0)
		return NULL;
	int i;
	for (i = 0; i < inst->box_cache.box_len; i++) {
		if (schema_cksum == inst->boxes[i].schema_cksum) {
			return &inst->boxes[i];
		}
	}
	return NULL;
}

static int init_box_from_entry(shm_sampler_inst_t inst, int instance_index)
{
	int rc = 0;
	ldms_shm_box_t* box = findbox(inst, instance_index);
	if(!box) {

		ldms_shm_index_entry_t entry =
				ldms_shm_index_entry_register_instance_reader(
						inst->box_cache.index,
						instance_index);
		if (NULL == entry) {
			INST_LOG(inst, LDMSD_LERROR,
				 "could not register as reader for instance "
				 "%d\n\r", instance_index);
			rc = ENOENT;
		} else {
			INST_LOG(inst, LDMSD_LDEBUG,
				 "registered as the reader for instance_index "
				 "=%d\n", instance_index);
			rc = add_mbox(inst, entry);
			if (rc) {
				INST_LOG(inst, LDMSD_LERROR,
					 "failed to add box for instance i = "
					 "%d\n\r", instance_index);
				int num_of_users =
					ldms_shm_index_entry_deregister_reader(
								entry);
				if (0 == num_of_users) {
					/* Nobody uses this set. It's time for cleanup */
					ldms_shm_index_entry_clean_shared_resources(
							entry);
					INST_LOG(inst, LDMSD_LERROR,
						 "no users were found for the "
						 "set of instance i = %d\n\r",
						 instance_index);
				}
			}
		}
	} else {
		box->current = 1;
	}
	return rc;
}

static void clear_mboxes(shm_sampler_inst_t inst,
			 ldms_shm_box_t *boxes, int box_len)
{
	if (NULL != boxes) {
		int i;
		for (i = 0; i < box_len; i++) {
			clear_box(inst, &(boxes[i]));
		}
	}
}

static void clear_inactive_boxes(shm_sampler_inst_t inst)
{
	int i;
	for (i = 0; i < inst->box_cache.box_len; i++) {
		if (!is_active(&inst->boxes[i])) {
			clear_box(inst, &(inst->boxes[i]));
		}
	}
}

static int get_updates_from_index(shm_sampler_inst_t inst)
{
	INST_LOG(inst, LDMSD_LDEBUG, "getting updates from index\n");
	int rc = 0;
	if (ldms_shm_index_is_empty(inst->box_cache.index)) {
		INST_LOG(inst, LDMSD_LINFO,
			 "index is empty. nothing to sample! \n\r");
		return rc;
	}
	int i, vaild_entry_cnt = 0;
	for (i = 0; i < inst->box_cache.box_len
			&& more_instances(inst, vaild_entry_cnt); i++) {
		if (ldms_shm_index_is_instance_empty(inst->box_cache.index, i))
			continue;
		int rc = init_box_from_entry(inst, i);
		if (rc)
			break;
		vaild_entry_cnt++;
	}
	return rc;
}

static int check_for_index_update(shm_sampler_inst_t inst)
{
	int rc = 0;
	ldms_shm_index_lock();

	uint64_t gen = ldms_shm_gn_get(inst->box_cache.index);
	if (!index_changed(inst, gen)) {
		ldms_shm_index_unlock();
		return rc;
	}
	INST_LOG(inst, LDMSD_LINFO,
		 "index (%s) has been changed since last scan\n",
		 inst->box_cache.index_name);
	inst->box_cache.last_gen = gen;

	rc = get_updates_from_index(inst);

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
static int scan_index(shm_sampler_inst_t inst, const char* const index_name,
		      int boxlen, int metric_max, int array_max,
		      int shm_set_timeout)
{
	int rc = 0;
	if (boxlen < 0) {
		clear_mboxes(inst, inst->boxes, boxlen);
		return rc;
	}
	if (boxlen >= 1) {
		rc = initial_setup(inst, index_name, boxlen, metric_max,
				   array_max, shm_set_timeout);
		if(rc)
			return rc;
	}
	rc = check_for_index_update(inst);
	if (rc) {
		INST_LOG(inst, LDMSD_LERROR, "error in check for update\n");
		clear_mboxes(inst, inst->boxes, inst->box_cache.box_len);
		clear_box_cache(inst);
		errno = rc;
	} else {
		clear_inactive_boxes(inst);
	}
	return rc;
}

static void check_box_activation(shm_sampler_inst_t inst, ldms_shm_box_t *box)
{
	if (!ldms_shm_set_is_active(box->shm_set)) {
		int num_of_users = ldms_shm_set_deregister_reader(box->shm_set);
		if (0 == num_of_users) {
			/* Nobody uses this set. It's time for cleanup */
			ldms_shm_set_clean_entry_shared_resources(box->shm_set);
			clear_box(inst, box);
		}
	}
}

/* ============== Sampler Plugin APIs ================= */

static
int shm_sampler_update_schema(ldmsd_plugin_inst_t pi, ldms_schema_t schema)
{
	/* do nothing, see `create_metric_set()` */
	return 0;
}

static
int shm_sampler_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set, void *ctxt)
{
	shm_sampler_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)inst->base.base;
	ldms_shm_box_t *box = ctxt;
	/* Populate set metrics */
	int shm_metric_index, ldms_metric_index;
	for (shm_metric_index = 0;
			shm_metric_index < box->shm_set->meta->num_events;
			shm_metric_index++) {
		ldms_metric_index = samp->first_idx + shm_metric_index;
		if (ldms_metric_is_array(box->ldms_set, ldms_metric_index)) {
			ldms_shm_data_t* data = ldms_shm_event_array_read(
					box->shm_set, shm_metric_index);
			/* (ldms_mval_t)data works because currently, it only has a uint64_t* in it. If this changes, this line also should be changed */
			ldms_metric_array_set(box->ldms_set, ldms_metric_index,
					(ldms_mval_t)data, 0,
					ldms_metric_array_get_len(
							box->ldms_set,
							ldms_metric_index));
		} else {
			ldms_shm_data_t data = ldms_shm_event_read(box->shm_set,
					shm_metric_index);
			/* (ldms_mval_t)data works because currently, it only has a uint64_t* in it. If this changes, this line also should be changed */
			ldms_metric_set(box->ldms_set, ldms_metric_index,
					(ldms_mval_t)&data);
		}
	}
	check_box_activation(inst, box);
	return 0;
}

int shm_sampler_sample(ldmsd_plugin_inst_t pi)
{
	shm_sampler_inst_t inst = (void*)pi;
	int rc = 0;
	if (ldms_shm_index_is_empty(inst->box_cache.index)) {
		INST_LOG(inst, LDMSD_LINFO, "index is empty\n");
		return EINVAL;
	}
	if (!inst->boxes) {
		INST_LOG(inst, LDMSD_LERROR, "plugin not initialized\n");
		rc = EINVAL;
		errno = rc;
		return rc;
	}
	/* update set boxes */
	rc = scan_index(inst, NULL, 0, 0, 0, 0);
	if (rc) {
		INST_LOG(inst, LDMSD_LERROR, "error in scan_index\n");
		errno = rc;
		return rc;
	}
	return inst->samp_sample(pi); /* this will call shm_sampler_update_set()
				       * for each set */
}

/* ============== Common Plugin APIs ================= */

static
const char *shm_sampler_desc(ldmsd_plugin_inst_t pi)
{
	return "shm_sampler - shm_sampler sampler plugin";
}

static
char *_help =
"config name=<INST> [COMMON_OPTIONS] [shm_index=<name>] [shm_boxmax=<int>] \n\
                    [shm_array_max=<int>] [shm_metric_max=<int>]\n\
                    [shm_set_timeout=<int>]\n\
		    \n\
    shm_index       A unique name for the shared memory index file\n\
    shm_boxmax      Maximum number of entries in the shared memory index file\n\
    shm_array_max   Maximum number of elements in array metrics\n\
    shm_metric_max  Maximum number of metrics\n\
    shm_set_timeout No read/write timeout in seconds\n\
";

static
const char *shm_sampler_help(ldmsd_plugin_inst_t pi)
{
	return _help;
}

static const char *__attr_find(shm_sampler_inst_t inst, json_entity_t json,
				char *ebuf, size_t ebufsz,
				char *attr_name)
{
	json_entity_t v;

	errno = 0;
	v = json_value_find(json, attr_name);
	if (!v) {
		errno = ENOENT;
		return NULL;
	}
	if (v->type != JSON_STRING_VALUE) {
		errno = EINVAL;
		snprintf(ebuf, ebufsz, "%s: The given '%s' value is "
				"not a string.\n", inst->base.inst_name, attr_name);
		return NULL;
	}
	return json_value_str(v)->str;
}

static
int shm_sampler_config(ldmsd_plugin_inst_t pi, json_entity_t json,
				      char *ebuf, int ebufsz)
{
	shm_sampler_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)inst->base.base;
	int rc;
	const char* index_name;
	const char* boxmax;
	const char* shm_array_max;
	const char* shm_metric_max;
	const char* shm_set_timeout_str;


	rc = samp->base.config(pi, json, ebuf, ebufsz);
	if (rc)
		return rc;

	boxmax = __attr_find(inst, json, ebuf, ebufsz, "shm_boxmax");
	int box_len = BOX_LEN_DEFAULT;
	if (!boxmax && (errno == EINVAL))
		return EINVAL;
	if (boxmax) {
		int blen = atoi(boxmax);
		if (blen < 1) {
			INST_LOG(inst, LDMSD_LERROR,
				 "shm_boxmax bad %s\n", boxmax);
			errno = EINVAL;
			return EINVAL;
		}
		box_len = blen;
	}

	shm_set_timeout_str = __attr_find(inst, json, ebuf, ebufsz, "shm_set_timeout");
	if (!shm_set_timeout_str && (errno == EINVAL))
		return EINVAL;
	int shm_set_timeout = SHM_TIMEOUT_DEFAULT;
	if (shm_set_timeout_str) {
		int amax = atoi(shm_set_timeout_str);
		if (amax < 0) {
			INST_LOG(inst, LDMSD_LERROR,
				 "shm_set_timeout bad %s\n",
				 shm_set_timeout_str);
			errno = EINVAL;
			return EINVAL;
		}
		shm_set_timeout = amax;
	}

	shm_array_max = __attr_find(inst, json, ebuf, ebufsz, "shm_array_max");
	if (!shm_array_max && (errno == EINVAL))
		return EINVAL;
	int array_max = ARRAY_MAX_DEFAULT;
	if (shm_array_max) {
		int amax = atoi(shm_array_max);
		if (amax < 1) {
			INST_LOG(inst, LDMSD_LERROR,
				 "shm_array_max bad %s\n",
				 shm_array_max);
			errno = EINVAL;
			return EINVAL;
		}
		array_max = amax;
	}

	shm_metric_max = __attr_find(inst, json, ebuf, ebufsz, "shm_metric_max");
	if (!shm_metric_max && (errno == EINVAL))
		return EINVAL;
	int metric_max = METRIC_MAX_DEFAULT;
	if (shm_metric_max) {
		int amax = atoi(shm_metric_max);
		if (amax < 1) {
			INST_LOG(inst, LDMSD_LERROR,
				 "shm_metric_max bad %s\n",
				 shm_metric_max);
			errno = EINVAL;
			return EINVAL;
		}
		metric_max = amax;
	}

	index_name = __attr_find(inst, json, ebuf, ebufsz, "shm_index");
	if (!index_name) {
		if (errno == ENOENT)
			index_name = INDEX_NAME_DEFAULT;
		else
			return EINVAL;
	}

	if (strlen(index_name) == 0) {
		INST_LOG(inst, LDMSD_LERROR, "shm_index invalid.\n");
		errno = EINVAL;
		return EINVAL;
	}
	rc = scan_index(inst, index_name, box_len, metric_max, array_max,
			shm_set_timeout);
	if (rc) {
		return rc;
	}
	errno = rc;
	return rc;
}

static
void shm_sampler_del(ldmsd_plugin_inst_t pi)
{
	shm_sampler_inst_t inst = (void*)pi;

	ldms_shm_index_lock();

	int index_should_be_cleaned = 1, i;
	for (i = 0; i < inst->box_cache.box_len; i++) {
		ldms_shm_box_t *box = &inst->boxes[i];
		if (!ldms_shm_set_is_active(box->shm_set)) {
			int num_of_users = ldms_shm_set_deregister_reader(
					box->shm_set);
			if (0 == num_of_users) {
				/* Nobody uses this set. It's time for cleanup */
				ldms_shm_set_clean_entry_shared_resources(
						box->shm_set);
				clear_box(inst, box);
			}
		} else {
			index_should_be_cleaned = 0;
		}
	}
	ldms_shm_index_unlock();
	if (index_should_be_cleaned
			&& ldms_shm_index_is_empty(inst->box_cache.index)) {
		ldms_shm_index_clean_shared_resources(inst->box_cache.index);
	}
	ldms_shm_clear_index(inst->box_cache.index);
	clear_box_cache(inst);

	INST_LOG(inst, LDMSD_LINFO, "successfully terminated\n");
}

static
json_entity_t shm_sampler_query(ldmsd_plugin_inst_t pi, const char *q)
{
	json_entity_t result = ldmsd_sampler_query(pi, q);
	if (!result)
		return result;

	/*
	 * Only override the 'env' query.
	 */
	if (0 != strcmp(q, "env"))
		return result;

	json_entity_t attr, envs, str;
	envs = json_entity_new(JSON_LIST_VALUE);
	if (!envs)
		goto enomem;
	str = json_entity_new(JSON_STRING_VALUE, "envs");
	if (!str) {
		json_entity_free(envs);
		goto enomem;
	}
	attr = json_entity_new(JSON_ATTR_VALUE, str, envs);
	if (!attr) {
		json_entity_free(str);
		json_entity_free(envs);
		goto enomem;
	}

	const char *env_names[] = {
			/* Environment variables used in mpi_profiler */
			"LDMS_SHM_MPI_SHM_UPDATE_INTERVAL",
			"LDMS_SHM_MPI_STAT_SCOPE",
			"LDMS_SHM_MPI_PROFILER_LOG_LEVEL",
			/* Environment variables used in mpi_profiler_configuration */
			"LDMS_SHM_MPI_FUNC_INCLUDE",
			"LDMS_SHM_MPI_FUNC_EXCLUDE",
			NULL
	};
	int i;
	for (i = 0; env_names[i]; i++) {
		str = json_entity_new(JSON_STRING_VALUE, env_names[i]);
		if (!str) {
			json_entity_free(attr);
			goto enomem;
		}
		json_item_add(envs, str);
	}
	json_attr_add(result, attr);
	return result;

enomem:
	ldmsd_plugin_qjson_err_set(result, ENOMEM, "Out of memory");
	return result;
}

static
int shm_sampler_init(ldmsd_plugin_inst_t pi)
{
	shm_sampler_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)inst->base.base;

	/* override update_schema() and update_set() */
	samp->update_schema = shm_sampler_update_schema;
	samp->update_set = shm_sampler_update_set;

	/* override query() */
	samp->base.query = shm_sampler_query;

	/* save smap_sample & override it */
	inst->samp_sample = samp->sample;
	samp->sample = shm_sampler_sample;

	/* NOTE More initialization code here if needed */
	return 0;
}

static
struct shm_sampler_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "sampler",
		.plugin_name = "shm_sampler",

		/* Common Plugin APIs */
		.desc   = shm_sampler_desc,
		.help   = shm_sampler_help,
		.init   = shm_sampler_init,
		.del    = shm_sampler_del,
		.config = shm_sampler_config,
	},
	/* plugin-specific data initialization (for new()) here */
};

ldmsd_plugin_inst_t new()
{
	shm_sampler_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
