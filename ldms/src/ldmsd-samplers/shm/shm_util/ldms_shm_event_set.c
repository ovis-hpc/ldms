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
 * \file ldms_shm_event_set.c
 * \brief Routines to manage events and metric set in the shared memory
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <errno.h>

#include "third/city.h"

#include "ldms_shm_obj.h"
#include "ldms_shm_event_set.h"

static inline void ldms_shm_init_meta_pointer(void *addr, ldms_shm_set_t set)
{
	set->meta = addr;
}

static void ldms_shm_init_meta(void *addr, ldms_shm_set_t set, int num_events)
{
	ldms_shm_init_meta_pointer(addr, set);
	set->meta->num_events = num_events;
}

static inline int ldms_shm_find_events_offset(ldms_shm_set_t set)
{
	return sizeof(struct ldms_shm_meta);
}

static inline void ldms_shm_init_events_pointer(ldms_shm_set_t set)
{
	set->events = (ldms_shm_event_desc_t *)(((void *)set->meta)
			+ ldms_shm_find_events_offset(set));
}

static inline int event_desc_sizeof(ldms_shm_event_desc_t * event)
{
	return sizeof(*event) + event->name_len;
}

static inline ldms_shm_event_desc_t* get_first_event(ldms_shm_set_t set)
{
	return &set->events[0];
}

static ldms_shm_event_desc_t* get_next_event(ldms_shm_event_desc_t * event)
{
	return (((void*)event) + event_desc_sizeof(event));
}

static int ldms_shm_find_data_offset(ldms_shm_set_t set)
{
	int i;
	int offset = ldms_shm_find_events_offset(set);
	ldms_shm_event_desc_t *event = get_first_event(set);
	for(i = 0; i < set->meta->num_events; i++) {
		offset += event_desc_sizeof(event);
		event = get_next_event(event);
	}
	return offset;
}

static inline void ldms_shm_init_data_pointer(ldms_shm_set_t set)
{
	set->data = (ldms_shm_data_t*)(((void *)set->meta)
			+ ldms_shm_find_data_offset(set));
}

static void ldms_shm_data_init(ldms_shm_set_t set)
{
	int e;
	ldms_shm_init_data_pointer(set);
	for(e = 0; e < set->meta->total_events; e++) {
		set->data[e].val = 0;
	}
}

ldms_shm_event_desc_t* ldms_shm_set_get_event(ldms_shm_set_t set,
		int event_index)
{
	ldms_shm_event_desc_t *event = get_first_event(set);
	int i;
	for(i = 0; i < set->meta->num_events; i++) {
		if(event_index == i)
			return event;
		event = get_next_event(event);
	}
	return NULL;
}

static int ldms_shm_set_init_event_index_map(ldms_shm_set_t set)
{
	int rc = 0;
	set->event_index_map = calloc(set->meta->num_events, sizeof(int));
	if(NULL == set->event_index_map) {
		printf(
				"ERROR: failed to allocate memory for event_index_map\n\r");
		rc = ENOMEM;
		return rc;
	}
	int i, index = 0;
	ldms_shm_event_desc_t *event = get_first_event(set);
	for(i = 0; i < set->meta->num_events; i++) {
		set->event_index_map[i] = index;
		index += event->num_elements;
		event = get_next_event(event);
	}
	return rc;
}

static void ldms_shm_add_events(ldms_shm_set_t set, int num_events,
		int *num_elements_per_event, char **event_names)
{
	int i;
	ldms_shm_event_desc_t *event = get_first_event(set);
	set->meta->total_events = 0;
	for(i = 0; i < num_events; i++) {

		event->num_elements = num_elements_per_event[i];
		set->meta->total_events += event->num_elements;
		event->name_len = strlen(event_names[i]) + 1;
		snprintf(event->name, event->name_len, "%s", event_names[i]);
		event = get_next_event(event);
	}
}

static int ldms_shm_calc_set_size(int num_events, int *num_elements_per_event,
		char **event_names)
{
	int meta_len = sizeof(struct ldms_shm_meta);
	int events_len = 0;
	int data_len = 0;

	int i;
	for(i = 0; i < num_events; i++) {
		events_len += sizeof(struct ldms_shm_event_desc)
				+ strlen(event_names[i]) + 1;
		data_len += num_elements_per_event[i] * sizeof(ldms_shm_data_t);
	}
	return meta_len + meta_len + data_len;
}

/* FIXME
 * Is this true?
 */
uint64_t ldms_shm_set_calc_checksum(ldms_shm_set_t set)
{
	int size = ldms_shm_find_data_offset(set);
	int counter = 0, i;
	char* buf = calloc(size, sizeof(char));
	char *buf_index = buf;
	int len = sprintf(buf_index, "%d", set->meta->num_events);
	buf_index = buf_index + len;
	counter += len;
	for(i = 0; i < set->meta->num_events; i++) {
		ldms_shm_event_desc_t* event = ldms_shm_set_get_event(set, i);
		len = sprintf(buf_index, "%s%d", event->name, i);
		buf_index = buf_index + len;
		counter += len;
	}
	uint64_t checksum = CityHash64(buf, counter);
	free(buf);
	return checksum;
}

/**
 * creates an object of type ldms_shm_set and initialized the shared memory, and set the pointers in the ldms_shm_set
 */
static ldms_shm_set_t create_ldms_shm_set(ldms_shm_obj_t shm_obj,
		int num_events, int *num_elements_per_event, char **event_names)
{
	int rc;
	ldms_shm_set_t set = calloc(1, sizeof(*set));
	if(NULL == set) {
		printf("ERROR: failed to allocate memory for the set\n\r");
		return NULL;
	}

	ldms_shm_init_meta(shm_obj->addr, set, num_events);

	ldms_shm_init_events_pointer(set);

	ldms_shm_add_events(set, num_events, num_elements_per_event,
			event_names);

	ldms_shm_data_init(set);

	rc = ldms_shm_set_init_event_index_map(set);
	if(rc) {
		ldms_shm_clear_set(set);
		free(set);
		return NULL;
	}
	return set;
}

void ldms_shm_clear_set(ldms_shm_set_t set)
{
	if(NULL == set)
		return;
	set->meta = NULL;
	set->events = NULL;
	set->data = NULL;
	if(NULL != set->entry) {
		ldms_shm_clear_index_entry(set->entry);
		free(set->entry);
	}
	set->entry = NULL;
	if(NULL != set->event_index_map) {
		free(set->event_index_map);
		set->event_index_map = NULL;
	}
}

ldms_shm_set_t ldms_shm_set_get(ldms_shm_obj_t shm_obj)
{
	int rc;

	ldms_shm_set_t set = calloc(1, sizeof(*set));

	if(NULL == set) {
		printf("ERROR: failed to allocate memory for the set\n\r");
		return NULL;
	}

	set->entry = NULL;

	ldms_shm_init_meta_pointer(shm_obj->addr, set);

	ldms_shm_init_events_pointer(set);

	ldms_shm_init_data_pointer(set);

	rc = ldms_shm_set_init_event_index_map(set);

	if(rc) {
		ldms_shm_clear_set(set);
		return NULL;
	}

	return set;
}

int ldms_shm_set_is_active(ldms_shm_set_t set)
{
	return (NULL != set && NULL != set->entry) ?
			ldms_shm_index_entry_is_active(set->entry) : 0;
}

int ldms_shm_set_deregister_reader(ldms_shm_set_t set)
{
	return ldms_shm_index_entry_deregister_reader(set->entry);
}

int ldms_shm_set_deregister_writer(ldms_shm_set_t set)
{
	return ldms_shm_index_entry_deregister_writer(set->entry);
}

void ldms_shm_set_clean_entry_shared_resources(ldms_shm_set_t set)
{
	ldms_shm_index_entry_clean_shared_resources(set->entry);
}

static inline int is_first_updater(ldms_shm_index_entry_t entry)
{
	return (0 == entry->p->cnt_updtr);
}

ldms_shm_set_t ldms_shm_index_register_set(const char *setlabel,
		const char *fslocation, int num_events,
		int *num_elements_per_event, char **event_names)
{

	ldms_shm_index_entry_t entry = ldms_shm_index_add_entry(setlabel,
			fslocation);
	if(entry == NULL) {
		printf(
				"ldms_shm_index_register_set failed to register the set with label \"%s\" in the location \"%s\" of the index\n\r",
				setlabel, fslocation);
		return NULL;
	}

	int set_size = ldms_shm_calc_set_size(num_events,
			num_elements_per_event, event_names);

	ldms_shm_index_lock();

	ldms_shm_obj_t shm_obj = ldms_shm_init(fslocation, set_size);

	ldms_shm_index_unlock();

	if(shm_obj == NULL) {
		printf(
				"Memory allocation error! failed to register the set with label \"%s\" in the location \"%s\" of the index\n\r",
				setlabel, fslocation);
		return NULL;
	}

	ldms_shm_set_t set;

	ldms_shm_index_entry_lock(entry);

	if(is_first_updater(entry)) {
		set = create_ldms_shm_set(shm_obj, num_events,
				num_elements_per_event, event_names);
		entry->p->schema_checksum = ldms_shm_set_calc_checksum(set);
	} else {
		set = ldms_shm_set_get(shm_obj);
	}
	entry->p->cnt_updtr++;
	ldms_shm_index_entry_unlock(entry);

	set->entry = entry;

	free(shm_obj);

	return set;
}

void print_ldms_shm_set(ldms_shm_set_t set)
{
	printf("shm_set=(%p){\n", set);
	printf("\tmeta=(%p){\n", set->meta);
	printf("\t\tnum_events=%d\n", set->meta->num_events);
	printf("\t\ttotal_events=%d\n", set->meta->total_events);

	ldms_shm_event_desc_t* event;

	printf("\t}events=(%p){\n", set->events);
	int i;
	int global_index = 0;
	for(i = 0; i < set->meta->num_events; i++) {

		event = ldms_shm_set_get_event(set, i);

		printf(
				"\t\tevent[%d]=%s @%p\n\t\tnum_elements=%d\n\t\t}data{\n",
				i, event->name, event, event->num_elements);

		int j;
		for(j = 0; j < event->num_elements; j++) {
			printf("\t\t\tdata[%d]=%" PRIu64 "@%p\n", j,
					set->data[global_index].val,
					&set->data[global_index]);
			global_index++;

		}
		printf("\t\t}\n");

	}
	printf("\t}\n}\n\r");
}

ldms_shm_set_t ldms_shm_data_open(ldms_shm_index_entry_t entry)
{
	ldms_shm_set_t set;
	ldms_shm_obj_t shm_obj = ldms_shm_init(entry->p->fslocation, 1);
	set = ldms_shm_set_get(shm_obj);
	set->entry = entry;
	return set;
}

/**
 * reads the value of counter for the event specified by 'event_index' from the rank specified by 'rank' using the information provided by 'set'
 */
ldms_shm_data_t ldms_shm_event_read(ldms_shm_set_t set, int event_index)
{
	/*FIXME lock? */
	set->entry->p->read_count++;
	return set->data[event_index];
}

ldms_shm_data_t* ldms_shm_event_array_read(ldms_shm_set_t set, int event_index)
{
	/*FIXME lock? */
	set->entry->p->read_count++;
	return &set->data[set->event_index_map[event_index]];
}

static inline void increment_write_counter(ldms_shm_set_t set)
{
	set->entry->p->write_count++;
}

/**
 * increments the value of counter for the event specified by 'event_index' from the rank specified by 'rank' using the information provided by 'set'
 */
void ldms_shm_atomic_counter_inc(ldms_shm_set_t set, int event_element_index)
{
	increment_write_counter(set);
	__sync_fetch_and_add(&set->data[event_element_index].val, 1);
}

static inline void increment_event_counter_non_atomic(ldms_shm_set_t set,
		int event_element_index)
{
	set->data[event_element_index].val++;
}

void ldms_shm_non_atomic_counter_inc(ldms_shm_set_t set,
		int event_element_index)
{
	increment_write_counter(set);
	increment_event_counter_non_atomic(set, event_element_index);
}

void ldms_shm_atomic_counter_add(ldms_shm_set_t set, int event_element_index,
		int val_to_inc)
{
	increment_write_counter(set);
	__sync_fetch_and_add(&set->data[event_element_index].val, val_to_inc);
}

void ldms_shm_non_atomic_counter_add(ldms_shm_set_t set,
		int event_element_index, int val_to_inc)
{
	increment_write_counter(set);
	set->data[event_element_index].val += val_to_inc;
}

void ldms_shm_counter_group_assign(ldms_shm_set_t set,
		int *event_element_indexes, ldms_shm_data_t* vals_to_assign,
		int count_events)
{
	increment_write_counter(set);
	int i;
	for(i = 0; i < count_events; i++)
		set->data[event_element_indexes[i]].val = vals_to_assign[i].val;
}

void ldms_shm_counter_group_add_atomic(ldms_shm_set_t set,
		int *event_element_indexes, ldms_shm_data_t* vals_to_add,
		int count_events)
{
	increment_write_counter(set);
	int i;
	for(i = 0; i < count_events; i++) {
		ldms_shm_atomic_counter_add(set, event_element_indexes[i],
				vals_to_add[i].val);
	}
}
