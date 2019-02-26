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
 * \file ldms_shm_event_set.h
 * \brief Routines to manage events and metric set in the shared memory
 */
#ifndef LDMS_SHM_EVENT_SET_H_
#define LDMS_SHM_EVENT_SET_H_

#include <stdint.h>
#include "ldms_shm_obj.h"
#include "ldms_shm_index.h"

/**
 * structure to record the information related to an event
 */
typedef struct ldms_shm_event_desc {
	int num_elements; /* Number of elements represented by an event. if array -> size of the array, else -> 1*/
	int name_len; /* Length of the name string */
	char name[0]; /* name of the event *//* TODO make this a constant sized array and remove name_len  */
} ldms_shm_event_desc_t;

/**
 * structure to record the metadata
 */
typedef struct ldms_shm_meta {
	int total_events; /* num_elements * num_events*/
	int num_events; /* number of events for the set */
}*ldms_shm_meta_t;

/**
 * structure to record the data for each event
 */
typedef struct ldms_shm_data {
	uint64_t val; /* The counter value for the event *//* TODO make this double datatype */
} ldms_shm_data_t;

/**
 * structure to keep the pointers to the specific required offsets in the shared memory
 */
typedef struct ldms_shm_set {
	ldms_shm_index_entry_t entry; /* entry associated with this set */
	int *event_index_map; /* array that is used to cache the mapping between event index and counter value associated with that event */
	ldms_shm_meta_t meta; /* pointer to the location of the metadata for this set in the shared memory  */
	ldms_shm_event_desc_t *events; /* pointer to the staring point of the events for this set in the shared memory  */
	ldms_shm_data_t *data; /* pointer to the staring point of the data for this set in the shared memory  */
}*ldms_shm_set_t;

/**
 * \brief Get the set that is stored in the shared memory specified by the shared memory object
 *
 * \param shm_obj The shared memory object that stores the set
 * \return shm_set if the shm_set is found and initialized successfully
 * \return NULL if fails to find and initialize the shm_set
 */
ldms_shm_set_t ldms_shm_set_get(ldms_shm_obj_t shm_obj);
/**
 * \brief Get the event specified by the event index from the set
 *
 * \param set
 * \param event_index
 * \return shm_event_desc if the event is found at the index
 * \return NULL if fails to find the event
 */
ldms_shm_event_desc_t* ldms_shm_set_get_event(ldms_shm_set_t set,
		int event_index);
/**
 * \brief Calculate the checksum value for the set
 *
 * \param set
 * \return the calculated checksum value for the set
 */
uint64_t ldms_shm_set_calc_checksum(ldms_shm_set_t set);
/**
 * \brief clear the set and free the memory allocated locally
 * This does not clean the shared memory area
 *
 * \param set to be cleared
 */
void ldms_shm_clear_set(ldms_shm_set_t set);
/**
 * \brief unlink and clean the shared resources associated with the entry that stores set
 *
 * \param entry
 */
void ldms_shm_set_clean_entry_shared_resources(ldms_shm_set_t set);
/**
 * \brief print the set content for debug purposes
 *
 * \param set
 */
void print_ldms_shm_set(ldms_shm_set_t set);

/**
 * \brief Open the shared memory associated with this entry and initialize the set
 *
 * \param entry
 * \return shm_set that is stored in the entry
 */
ldms_shm_set_t ldms_shm_data_open(ldms_shm_index_entry_t entry);
/**
 * \brief determine if the set and its entry is active
 *
 * \param entry
 * \return 1 if the set and entry is active, 0 otherwise
 */
int ldms_shm_set_is_active(ldms_shm_set_t set);
/**
 * \brief deregister a reader from this set
 * caller should hold the lock on the index, before calling this
 *
 * \param entry
 * \return the number of remaining users (reader/writer) of the set
 */
int ldms_shm_set_deregister_reader(ldms_shm_set_t set);
/**
 * \brief reads the value of counter for the event specified by 'event_index' using the information provided by 'set'
 *
 * \param set
 * \param event_index
 * \return the shm_data represents the latest value for the event
 */
ldms_shm_data_t ldms_shm_event_read(ldms_shm_set_t set, int event_index);
/**
 * \brief reads the value of counter for the array based event specified by 'event_index' using the information provided by 'set'
 *
 * \param set
 * \param event_index
 * \return the shm_data array represents the latest values for the event
 */
ldms_shm_data_t* ldms_shm_event_array_read(ldms_shm_set_t set, int event_index);

/**
 * \brief register as the writer for a set
 *
 * \param setlabel Name of the metric set
 * \param fslocation Name of the shared memory location that the information related to metric set is retained
 * \param num_events Number of events in this set
 * \param num_elements_per_event Number of elements for each event
 * \param event_names Name of each event
 * \return shm_set if the registration has been done successfully
 * \return NULL if the registration failed
 */
ldms_shm_set_t ldms_shm_index_register_set(const char *setlabel,
		const char *fslocation, int num_events,
		int *num_elements_per_event, char **event_names);
/**
 * \brief deregister a writer from this set
 *
 * \param set
 * \return the number of remaining users (reader/writer) of this set
 */
int ldms_shm_set_deregister_writer(ldms_shm_set_t set);
/**
 * \brief atomically increment the counter of the event specified by the event_element_index in set
 *
 * \param set
 * \param event_element_index the index of element of the event that is going to be incremented
 */
void ldms_shm_atomic_counter_inc(ldms_shm_set_t set, int event_element_index);
/**
 * \brief non-atomically increment the counter of the event specified by the event_element_index in set
 *
 * \param set
 * \param event_element_index the index of element of the event that is going to be incremented
 */
void ldms_shm_non_atomic_counter_inc(ldms_shm_set_t set,
		int event_element_index);
/**
 * \brief atomically add a value to the counter of the event specified by the event_element_index in set
 *
 * \param set
 * \param event_element_index the index of element of the event that is going to be incremented
 * \param val_to_inc The value to be added to the counter
 */
void ldms_shm_atomic_counter_add(ldms_shm_set_t set, int event_element_index,
		int val_to_inc);
/**
 * \brief non-atomically add a value to the counter of the event specified by the event_element_index in set
 *
 * \param set
 * \param event_element_index the index of element of the event that is going to be updated
 * \param val_to_inc The value to be added to the counter
 */
void ldms_shm_non_atomic_counter_add(ldms_shm_set_t set,
		int event_element_index, int val_to_inc);
/**
 * \brief non-atomically assign values to the counter of the events specified by the event_element_indexes in set
 *
 * \param set
 * \param event_element_indexes the index array of elements of the events that are going to be updated
 * \param vals_to_assign The values to be assigned to the counters
 * \param count_events number Number of events to be updated
 */
void ldms_shm_counter_group_assign(ldms_shm_set_t set,
		int *event_element_indexes, ldms_shm_data_t* vals_to_assign,
		int count_events);
/**
 * \brief atomically add values to the counter of the events specified by the event_element_indexes in set
 *
 * \param set
 * \param event_element_indexes the index array of elements of the events that are going to be updated
 * \param vals_to_add The values to be added to the counters
 * \param count_events number Number of events to be updated
 */
void ldms_shm_counter_group_add_atomic(ldms_shm_set_t set,
		int *event_element_indexes, ldms_shm_data_t* vals_to_add,
		int count_events);
#endif /* LDMS_SHM_EVENT_SET_H_ */
