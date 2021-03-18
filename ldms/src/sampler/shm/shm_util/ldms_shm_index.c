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
 * \file ldms_shm_index.c
 * \brief Routines to manage shared memory index
 */
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stddef.h>
#include <fcntl.h> /* For O_* constants */
#include <sys/time.h>
#include <string.h>
#include <errno.h>
#include "../../../../../lib/src/third/city.h"
#include "ldms_shm_obj.h"
#include "ldms_shm_index.h"
#include "ovis_util/util.h" /* for strerror macro only */

#define LDMS_SHM_GLOBAL_SEM_MUTEX_NAME_PREFIX "/ldms_shm_global_mutex"
static char* ldms_shm_global_sem_mutex_name;
static sem_t *mutex_sem = NULL;

static char* ldms_shm_index_mutex_name;
static ldms_shm_index_t shm_index = NULL;

static inline double get_second(struct timeval time)
{
	return (double)time.tv_sec + (double)time.tv_usec / 1000000;
}

static double getWallTime()
{
	struct timeval time;

	if(gettimeofday(&time, NULL))
		return 0;
	return get_second(time);
}

static int calc_index_mem_size(int max_entries, int metric_max, int array_max)
{
	return max_entries * sizeof(ldms_shm_index_entry_properties_t)
			+ sizeof(struct ldms_shm_index_properties);
}

static inline int index_is_initialized()
{
	return (LDMS_SHM_INDEX_STATE_INITIALIZED == shm_index->p->state);
}

static void initialize_index(const char *name, int max_entries,
		int shm_set_timeout)
{
	shm_index->p->state = LDMS_SHM_INDEX_STATE_INITIALIZED;
	shm_index->p->generation_number = 0;
	shm_index->p->instance_count = 0;
	shm_index->p->max_entries = max_entries;
	shm_index->p->shm_set_timeout = shm_set_timeout;
	sprintf(shm_index->p->name, "%s", name);
	int i;
	for(i = 0; i < shm_index->p->max_entries; i++) {
		ldms_shm_index_entry_properties_t *ip =
				&shm_index->instance_list[i];
		ip->state = LDMS_SHM_INDEX_ENTRY_STATE_EMPTY;
		ip->cnt_rdr = 0;
		ip->cnt_updtr = 0;
	}
}

static int allocate_shared_resources(const char *name, int max_entries,
		int metric_max, int array_max)
{
	int index_size = calc_index_mem_size(max_entries, metric_max,
			array_max);
	ldms_shm_obj_t shm_obj = ldms_shm_init(name, index_size);

	if(NULL == shm_obj) {
		printf(
				"Error in shm index initialization: failed to open a shm object with the name %s\n\r",
				name);
		free(shm_index);
		shm_index = NULL;
		return ENOMEM;
	}

	shm_index->p = shm_obj->addr;

	if((shm_index->mutex_sem = sem_open(ldms_shm_index_mutex_name, O_CREAT,
			0660, 1)) == SEM_FAILED) {
		printf(
				"Error in shm index initialization: failed to open a semaphore with the name %s\n\r",
				ldms_shm_index_mutex_name);
		free(shm_index);
		shm_index = NULL;
		free(shm_obj->name);
		free(shm_obj);
		return -1;
	}

	shm_index->instance_list = ((void*)shm_obj->addr
			+ sizeof(struct ldms_shm_index_properties));
	free(shm_obj->name);
	free(shm_obj);
	return 0;
}

static ldms_shm_index_t ldms_shm_index_init(const char *name, int max_entries,
		int metric_max, int array_max, int shm_set_timeout)
{
	/**
	 * TODO add validation for the requested memory size
	 */
	shm_index = calloc(1, sizeof(*shm_index));
	if(NULL == shm_index) {
		printf(
				"Error in shm index initialization: failed to allocate memory\n\r");
		return NULL;
	}

	int rc = allocate_shared_resources(name, max_entries, metric_max,
			array_max);
	if(rc) {
		free(shm_index);
		shm_index = NULL;
		return NULL;
	}

	ldms_shm_index_lock();

	if(index_is_initialized()) {
		ldms_shm_index_unlock();
		return shm_index;
	}

	initialize_index(name, max_entries, shm_set_timeout);

	ldms_shm_index_unlock();

	return shm_index;
}

static ldms_shm_index_entry_t ldms_shm_index_find_entry(const char *fslocation)
{
	int i;
	for(i = 0; i < shm_index->p->max_entries; i++) {
		ldms_shm_index_entry_properties_t *ip =
				&shm_index->instance_list[i];

		if(strcmp(ip->fslocation, fslocation) == 0) {
			ldms_shm_index_entry_t entry = calloc(1,
					sizeof(*entry));
			if(NULL == entry) {
				printf(
						"failed to allocate memmory for entry %s\n\r",
						fslocation);
				return NULL;
			}
			entry->p = ip;
			if((entry->mutex_sem = sem_open(fslocation, O_CREAT,
					0660, 1)) == SEM_FAILED) {
				printf(
						"Error in entry finding: failed to open a semaphore with the name %s\n\r",
						fslocation);
				return NULL;
			}
			return entry;
		}
	}
	return NULL;
}

void ldms_shm_index_lock()
{
	if(sem_wait(shm_index->mutex_sem) == -1)
		printf("Error: failed to acquire the lock on the index\n\r");
}

void ldms_shm_index_unlock()
{
	if(sem_post(shm_index->mutex_sem) == -1)
		printf("Error: failed to release the lock on the index\n\r");
}

void ldms_shm_index_entry_lock(ldms_shm_index_entry_t entry)
{
	if(sem_wait(entry->mutex_sem) == -1)
		printf("Error: failed to acquire the lock on the entry\n\r");
}

void ldms_shm_index_entry_unlock(ldms_shm_index_entry_t entry)
{
	if(sem_post(entry->mutex_sem) == -1)
		printf("Error: failed to release the lock on the entry\n\r");
}

static int build_shared_names(const char* index_name)
{
	int rc = 0;
	ldms_shm_global_sem_mutex_name = malloc(strlen(index_name) + strlen(
	LDMS_SHM_GLOBAL_SEM_MUTEX_NAME_PREFIX) + 1);
	if(NULL == ldms_shm_global_sem_mutex_name) {
		rc = ENOMEM;
		return rc;
	}
	sprintf(ldms_shm_global_sem_mutex_name, "%s%s",
	LDMS_SHM_GLOBAL_SEM_MUTEX_NAME_PREFIX, index_name + 1);

	ldms_shm_index_mutex_name = malloc(strlen(index_name) + strlen(
	LDMS_SHM_INDEX_MUTEX_NAME_PREFIX) + 1);
	if(NULL == ldms_shm_index_mutex_name) {
		rc = ENOMEM;
		return rc;
	}
	sprintf(ldms_shm_index_mutex_name, "%s%s",
	LDMS_SHM_INDEX_MUTEX_NAME_PREFIX, index_name + 1);
	return rc;
}

ldms_shm_index_t ldms_shm_index_open(const char *name, int max_entries,
		int metric_max, int array_max, int shm_set_timeout)
{
	int rc = build_shared_names(name);
	if(rc) {
		printf("Error in opening the index\n\r");
		return NULL;
	}

	if((mutex_sem = sem_open(ldms_shm_global_sem_mutex_name, O_CREAT, 0660,
			1)) == SEM_FAILED) {
		printf(
				"Error in opening the index: failed to open a semaphore with the name %s\n\r",
				ldms_shm_global_sem_mutex_name);
		return NULL;
	}
	if(sem_wait(mutex_sem) == -1) {
		printf(
				"Error in opening the index: failed to acquire the global lock during the opening of the index\n\r");
		return NULL;
	}
	if(shm_index != NULL) {
		printf("ldms_shm_index_open is called twice!\n\r");
		return shm_index;
	}

	ldms_shm_index_init(name, max_entries, metric_max, array_max,
			shm_set_timeout);

	if(sem_post(mutex_sem) == -1) {
		printf(
				"Error in opening the index: failed to release the global lock during the opening of the index\n\r");
		return NULL;
	}
	return shm_index;
}

static inline int ldms_shm_index_entry_is_empty(
		ldms_shm_index_entry_properties_t *entry_p)
{
	return (LDMS_SHM_INDEX_ENTRY_STATE_EMPTY == entry_p->state);
}

static ldms_shm_index_entry_t create_index_entry(const char *fslocation,
		const char *setlabel)
{
	ldms_shm_index_entry_t new_entry = calloc(1, sizeof(*new_entry));
	if(NULL == new_entry) {
		printf("ERROR: failed to create an entry for %s \n\r",
				fslocation);
		return NULL;
	}
	new_entry->p = NULL;
	int i;
	for(i = 0; i < shm_index->p->max_entries; i++) {
		ldms_shm_index_entry_properties_t *ip =
				&shm_index->instance_list[i];
		if(ldms_shm_index_entry_is_empty(ip)) {
			new_entry->p = ip;
			break;
		}
	}
	new_entry->mutex_sem = sem_open(fslocation, O_CREAT, 0660, 1);
	if(SEM_FAILED == new_entry->mutex_sem) {
		printf(
				"ERROR: failed to open a semaphore with the name %s  %d: %s\n\r",
				fslocation, errno, STRERROR(errno));
		if(NULL != new_entry)
			free(new_entry);
		return NULL;
	}

	ldms_shm_index_entry_lock(new_entry);

	new_entry->p->cnt_rdr = 0;
	new_entry->p->cnt_updtr = 0;
	new_entry->p->write_count = 0;
	new_entry->p->read_count = 0;
	new_entry->p->last_read_count_observed = 0;
	new_entry->p->last_write_count_observed = 0;
	sprintf(new_entry->p->setlabel, "%s", setlabel);
	sprintf(new_entry->p->fslocation, "%s", fslocation);

	new_entry->p->state = LDMS_SHM_INDEX_ENTRY_STATE_ACTIVE;

	ldms_shm_index_entry_unlock(new_entry);

	return new_entry;

}

void ldms_shm_clear_index_entry(ldms_shm_index_entry_t entry)
{
	entry->p = NULL;
	if(NULL != entry->mutex_sem) {
		entry->mutex_sem = NULL;
	}
}

int ldms_shm_index_entry_clean_shared_resources(ldms_shm_index_entry_t entry)
{
	int rc;
	rc = ldms_shm_clean(entry->p->fslocation);
	if(!rc) {
		rc = sem_unlink(entry->p->fslocation);
		if(rc) {
			printf(
					"ERROR: failed to unlink the semaphore %s  %d: %s\n\r",
					entry->p->fslocation, errno,
					STRERROR(errno));
		}
	}
	return rc;
}

int ldms_shm_index_clean_shared_resources(ldms_shm_index_t shm_index)
{
	int rc;
	rc = ldms_shm_clean(shm_index->p->name);
	if(!rc) {
		rc = sem_unlink(ldms_shm_index_mutex_name);
		if(rc) {
			printf(
					"ERROR: failed to unlink the semaphore %s  %d: %s\n\r",
					ldms_shm_index_mutex_name, errno,
					STRERROR(errno));
		} else {
			free(ldms_shm_index_mutex_name);
			ldms_shm_index_mutex_name = NULL;
			rc = sem_unlink(ldms_shm_global_sem_mutex_name);
			if(rc) {
				printf(
						"ERROR: failed to unlink the semaphore %s  %d: %s\n\r",
						ldms_shm_global_sem_mutex_name,
						errno, STRERROR(errno));
			} else {
				free(ldms_shm_index_mutex_name);
				ldms_shm_index_mutex_name = NULL;
				mutex_sem = NULL;
			}
		}
	}
	return rc;
}

void ldms_shm_clear_index(ldms_shm_index_t index)
{
	if(NULL == index)
		return;
	if(NULL != index->mutex_sem) {
		index->mutex_sem = NULL;
	}

	index->mutex_sem = NULL;
	index->p = NULL;
	index->instance_list = NULL;
}

static inline void ldms_shm_index_inc_gen_number()
{
	shm_index->p->generation_number++;
}

static inline int entry_exists(ldms_shm_index_entry_t entry)
{
	return (NULL != entry && ldms_shm_index_entry_is_active(entry));
}

static inline int index_is_full()
{
	return (shm_index->p->instance_count >= shm_index->p->max_entries);
}

ldms_shm_index_entry_t ldms_shm_index_add_entry(const char *setlabel,
		const char *fslocation)
{
	ldms_shm_index_lock();

	ldms_shm_index_entry_t entry = ldms_shm_index_find_entry(fslocation);

	if(entry_exists(entry)) {
		ldms_shm_index_unlock();
		printf("Info: entry %s already exists\n\r", fslocation);
		return entry;
	}

	if(index_is_full()) {
		ldms_shm_index_unlock();
		printf(
				"Error: ldms_shm_index_add_entry (%s): No more instances allowed! %d >= %d. Change the setting for maximum number of entries\n\r",
				setlabel, shm_index->p->instance_count,
				shm_index->p->max_entries);
		return NULL;
	}

	entry = create_index_entry(fslocation, setlabel);
	if(entry) {
		printf("Info: created the entry %s for the first time\n\r",
				fslocation);
		shm_index->p->instance_count++;
		ldms_shm_index_inc_gen_number();
	} else {
		printf("Error failed to create a new entry in the index\n\r");
	}

	ldms_shm_index_unlock();

	return entry;
}

static ldms_shm_index_entry_t ldms_shm_index_entry_register_reader(
		const char *fslocation)
{
	ldms_shm_index_entry_t entry = ldms_shm_index_find_entry(fslocation);

	if(NULL == entry) {
		printf(
				"ERROR: in registering as reader. Could not find index entry with label %s\n\r",
				fslocation);
		return NULL;
	}

	ldms_shm_index_entry_lock(entry);

	entry->p->cnt_rdr++;

	ldms_shm_index_entry_unlock(entry);

	printf(
			"INFO: reader #%d registered for reading the set with label (%s) at (%s) in the index\n\r",
			entry->p->cnt_rdr, entry->p->setlabel, fslocation);

	return entry;
}

ldms_shm_index_entry_t ldms_shm_index_entry_register_instance_reader(
		ldms_shm_index_t shm_index, int instance_index)
{
	return ldms_shm_index_entry_register_reader(
			shm_index->instance_list[instance_index].fslocation);
}

static inline int is_last_reader(ldms_shm_index_entry_t entry)
{
	return (entry->p->cnt_rdr == 1);
}

static inline int have_writers(ldms_shm_index_entry_t entry)
{
	return (entry->p->cnt_updtr > 0);
}

static int write_timeout_reached(ldms_shm_index_entry_t entry)
{
	if(entry->p->write_count == entry->p->last_write_count_observed) {
		return ((getWallTime()
				- get_second(entry->p->last_write_count_change))
				> shm_index->p->shm_set_timeout);
	} else {
		entry->p->last_write_count_observed = entry->p->write_count;
		gettimeofday(&entry->p->last_write_count_change, NULL);
		return 0;
	}
}

/**
 *  caller should hold the lock on the index
 * returns the number of remaining shm user
 */
int ldms_shm_index_entry_deregister_reader(ldms_shm_index_entry_t entry)
{
	int rem_users;

	ldms_shm_index_entry_lock(entry);

	if(is_last_reader(entry)) {
		entry->p->cnt_rdr = 0;
		if(have_writers(entry) && !write_timeout_reached(entry)) {
			rem_users = entry->p->cnt_updtr;
			ldms_shm_index_entry_unlock(entry);
		} else {
			entry->p->state = LDMS_SHM_INDEX_ENTRY_STATE_EMPTY;

			/* caller should lock the index */
			shm_index->p->instance_count--;
			ldms_shm_index_inc_gen_number();

			ldms_shm_index_entry_unlock(entry);

			rem_users = 0;
		}
	} else {
		entry->p->cnt_rdr--;
		rem_users = entry->p->cnt_updtr + entry->p->cnt_rdr;
		ldms_shm_index_entry_unlock(entry);
	}
	return rem_users;
}

int ldms_shm_index_entry_is_active(ldms_shm_index_entry_t entry)
{
	return (LDMS_SHM_INDEX_ENTRY_STATE_ACTIVE == entry->p->state);
}

static inline int is_last_updater(ldms_shm_index_entry_t entry)
{
	return (entry->p->cnt_updtr == 1);
}

static inline int have_readers(ldms_shm_index_entry_t entry)
{
	return (entry->p->cnt_rdr > 0);
}

static int read_timeout_reached(ldms_shm_index_entry_t entry)
{
	if(entry->p->read_count == entry->p->last_read_count_observed) {
		return ((getWallTime()
				- get_second(entry->p->last_read_count_change))
				> shm_index->p->shm_set_timeout);
	} else {
		entry->p->last_read_count_observed = entry->p->read_count;
		gettimeofday(&entry->p->last_read_count_change, NULL);
		return 0;
	}
}

/**
 * returns number of remaining shm user
 */
int ldms_shm_index_entry_deregister_writer(ldms_shm_index_entry_t entry)
{
	ldms_shm_index_entry_lock(entry);

	int rem_users;

	if(is_last_updater(entry)) {
		entry->p->state = LDMS_SHM_INDEX_ENTRY_STATE_INACTIVE;
		entry->p->cnt_updtr = 0;
		if(have_readers(entry) && !read_timeout_reached(entry)) {
			rem_users = entry->p->cnt_rdr;
			ldms_shm_index_entry_unlock(entry);

		} else {
			entry->p->state = LDMS_SHM_INDEX_ENTRY_STATE_EMPTY;
			ldms_shm_index_lock();
			shm_index->p->instance_count--;
			ldms_shm_index_inc_gen_number();
			ldms_shm_index_unlock();

			ldms_shm_index_entry_unlock(entry);
			rem_users = 0;
		}
	} else {
		entry->p->cnt_updtr--;
		rem_users = entry->p->cnt_updtr + entry->p->cnt_rdr;
		ldms_shm_index_entry_unlock(entry);
	}
	return rem_users;
}

int ldms_shm_index_is_instance_empty(ldms_shm_index_t shm_index,
		int instance_index)
{
	return (LDMS_SHM_INDEX_ENTRY_STATE_EMPTY
			== shm_index->instance_list[instance_index].state);
}

uint64_t ldms_shm_index_get_schema_checksum(ldms_shm_index_t shm_index,
		int instance_index)
{
	return shm_index->instance_list[instance_index].schema_checksum;
}

uint64_t ldms_shm_index_entry_get_schema_checksum(ldms_shm_index_entry_t entry)
{
	return entry->p->schema_checksum;
}

uint64_t ldms_shm_gn_get(ldms_shm_index_t index)
{
	return index->p->generation_number;
}

int ldms_shm_index_get_instace_count(ldms_shm_index_t index)
{
	return index->p->instance_count;
}

int ldms_shm_index_is_empty(ldms_shm_index_t index)
{
	return (0 == index->p->instance_count);
}

char* ldms_shm_index_entry_set_label_get(ldms_shm_index_entry_t entry)
{
	return entry->p->setlabel;
}
