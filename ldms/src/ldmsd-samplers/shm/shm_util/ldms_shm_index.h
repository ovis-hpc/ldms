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
 * \file ldms_shm_index.h
 * \brief Routines to manage shared memory index
 */
#ifndef LDMS_SHM_INDEX_H_
#define LDMS_SHM_INDEX_H_

#include <stdint.h>
#include <semaphore.h>

#define LDMS_SHM_INDEX_ENTRY_SET_LABEL_SIZE 256
#define LDMS_SHM_INDEX_NAME_SIZE 256
#define LDMS_SHM_INDEX_ENTRY_FSLOCATION_SIZE 256

#define LDMS_SHM_INDEX_MUTEX_NAME_PREFIX "/ldms_shm_index_mutex"
#define LDMS_SHM_SET_FSLOCATION_PREFIX "/ldms_shm_set_fslocation"

/**
 * structure to record the information related to the index_entry that should be retained in the shared memory, because it is shared between multiple set updaters and reader
 */
typedef struct ldms_shm_index_entry_properties {
	uint64_t write_count; /* Counter that is updated with each write event. This is used for determining the entry write activity */
	uint64_t schema_checksum; /* Unique checksum for each entry/set for validation */
	int cnt_rdr; /* Number of readers have been registered for the current entry/set */
	int cnt_updtr; /* Number of writers/updaters have been registered for the current entry/set */
	enum ldms_shm_index_entry_state {
		LDMS_SHM_INDEX_ENTRY_STATE_EMPTY, /* Initial state upon creation */
		LDMS_SHM_INDEX_ENTRY_STATE_ACTIVE, /* When a set writer registers its set */
		LDMS_SHM_INDEX_ENTRY_STATE_INACTIVE, /* When the laster set writer deregisters its set */
		LDMS_SHM_INDEX_ENTRY_STATE_INCONSISTENT
	} state; /* The latest state of the entry */
	struct timeval last_read_count_change; /* time that last read activity has been observed in this entry. This is only updated upon the set deregisteration time */
	int read_count; /* Counter that is updated with each read event. This is used for determining the entry read activity */
	int last_read_count_observed; /* the last value that has been observed for the read count in this entry. This is only updated upon the set deregisteration time */
	struct timeval last_write_count_change; /* time that last write activity has been observed in this entry. This is only updated upon the set deregisteration time */

	int last_write_count_observed; /* the last value that has been observed for the write count in this entry. This is only updated upon the set deregisteration time */
	char setlabel[LDMS_SHM_INDEX_ENTRY_SET_LABEL_SIZE]; /* Name of the metric set that is registered for this entry */
	char fslocation[LDMS_SHM_INDEX_ENTRY_FSLOCATION_SIZE]; /* Name of the shared memory location that the information related to metric set is retained */
} ldms_shm_index_entry_properties_t;

/**
 * structure to record the information related to the index_entry that is unique to each process and need not to be retained in the shared memory
 */
typedef struct ldms_shm_index_entry {
	ldms_shm_index_entry_properties_t *p; /* shared entry information that is recorded in the shared memory */
	sem_t *mutex_sem; /* semaphore that protect the current entry from shared access */
}*ldms_shm_index_entry_t;

/**
 * structure to record the information related to the index that should be retained in the shared memory, because it is shared between multiple set updaters and reader
 */
typedef struct ldms_shm_index_properties {
	char name[LDMS_SHM_INDEX_NAME_SIZE]; /* name of the shared memory index */
	enum ldms_shm_index_state {
		LDMS_SHM_INDEX_STATE_UNINITIALIZED = 0, /* The initial state upon creation */
		LDMS_SHM_INDEX_STATE_INITIALIZED, /* When all properties are set, this index is initialized state */
		LDMS_SHM_INDEX_STATE_INCONSISTENT
	} state; /* state of the shared memory index */
	int instance_count; /* number of active entries */
	int max_entries; /* maximum number of entries  */
	int shm_set_timeout; /* timeout for the set activity */
	uint64_t generation_number; /* generation number to track changes in the index, when reader detects a change, it should scan the index for updates */
}*ldms_shm_index_properties_t;

/**
 * structure to record the information related to the index that is unique to each process and need not to be retained in the shared memory
 */
typedef struct ldms_shm_index {
	sem_t *mutex_sem; /* semaphore that protect the current index from shared access */
	ldms_shm_index_properties_t p; /* shared index information that is recorded in the shared memory */

	ldms_shm_index_entry_properties_t *instance_list; /* list of entries (stored in the shared memory)*/
}*ldms_shm_index_t;

/**
 * \brief Opens and initializes the shared memory index
 *
 * \param name The shared memory index name
 * \param max_entries The maximum number of entries allowed in this index
 * \param metric_max The maximum number of metrics allowed in each metric set
 * \param array_max The maximum number of elements allowed in an array based metric set
 * \param shm_set_timeout The timeout in seconds for the index activity
 * \return shm_index if the shared memory index is opened and initialized successfully
 * \return NULL if fails to open and initialized the shared memory index
 */
ldms_shm_index_t ldms_shm_index_open(const char *name, int max_entries,
		int metric_max, int array_max, int shm_set_timeout);
/**
 * \brief lock the shared memory index
 */
void ldms_shm_index_lock();
/**
 * \brief unlock the shared memory index
 */
void ldms_shm_index_unlock();
/**
 * \brief lock the shared memory index entry
 * For changes that only affects the current entry subscribers, use this more fine grained lock
 *
 * \param entry to be protected
 */
void ldms_shm_index_entry_lock(ldms_shm_index_entry_t entry);
/**
 * \brief unlock the shared memory index entry
 *
 * \param entry to be unlocked
 */
void ldms_shm_index_entry_unlock(ldms_shm_index_entry_t entry);
/**
 * \brief clear the index
 * This does not clean the shared memory area
 *
 * \param index to be cleared
 */
void ldms_shm_clear_index(ldms_shm_index_t index);
/**
 * \brief clear the entry
 * This does not clean the shared memory area
 *
 * \param entry to be cleared
 */
void ldms_shm_clear_index_entry(ldms_shm_index_entry_t entry);

/**
 * \brief register as the reader for an entry in this index
 *
 * \param shm_index index that the entry belongs to
 * \param instance_index the index of the entry in the list
 * \return shm_index_entry if the registration has been done successfully
 * \return NULL if the registration failed
 */
ldms_shm_index_entry_t ldms_shm_index_entry_register_instance_reader(
		ldms_shm_index_t shm_index, int instance_index);
/**
 * \brief get the latest generation number of the current index
 *
 * \param index
 * \return the number represents the latest generation number of the index
 */
uint64_t ldms_shm_gn_get(ldms_shm_index_t index);
/**
 * \brief get the number of active entries in the index
 *
 * \param index
 * \return the number represents the latest number of active entries in the index
 */
int ldms_shm_index_get_instace_count(ldms_shm_index_t index);
/**
 * \brief determine if the index is empty
 *
 * \param index
 * \return 1 if there is no active entry in the index, 0 otherwise
 */
int ldms_shm_index_is_empty(ldms_shm_index_t index);
/**
 * \brief determine if an index entry is empty
 *
 * \param shm_index index that the entry belongs to
 * \param instance_index the index of the entry in the list
 * \return 1 if there is no set associated with the entry, 0 otherwise
 */
int ldms_shm_index_is_instance_empty(ldms_shm_index_t shm_index,
		int instance_index);
/**
 * \brief get the checksum value for the set associated with the entry
 *
 * \param shm_index index that the entry belongs to
 * \param instance_index the index of the entry in the list
 * \return the checksum value for the set associated with the entry
 */
uint64_t ldms_shm_index_get_schema_checksum(ldms_shm_index_t shm_index,
		int instance_index);
/**
 * \brief determine if an index entry is active
 *
 * \param entry
 * \return 1 if there is a set associated with the entry, 0 otherwise
 */
int ldms_shm_index_entry_is_active(ldms_shm_index_entry_t entry);
/**
 * \brief get the label of the set associated with this entry
 *
 * \param entry
 * \return the label of the set associated with this entry
 */
char* ldms_shm_index_entry_set_label_get(ldms_shm_index_entry_t entry);
/**
 * \brief get the checksum value for the set associated with the entry
 *
 * \param entry
 * \return the checksum value for the set associated with the entry
 */
uint64_t ldms_shm_index_entry_get_schema_checksum(ldms_shm_index_entry_t entry);
/**
 * \brief unlink and clean the shared resources associated with this index
 *
 * \param shm_index
 * \return 0 if all shared resources have been cleaned successfully, non-zero otherwise
 */
int ldms_shm_index_clean_shared_resources(ldms_shm_index_t shm_index);
/**
 * \brief deregister a reader from this entry
 * caller should hold the lock on the index, before calling this
 *
 * \param entry
 * \return the number of remaining users (reader/writer) of this entry and its associated set
 */
int ldms_shm_index_entry_deregister_reader(ldms_shm_index_entry_t entry);
/**
 * \brief unlink and clean the shared resources associated with this entry
 *
 * \param entry
 * \return 0 if all shared resources have been cleaned successfully, non-zero otherwise
 */
int ldms_shm_index_entry_clean_shared_resources(ldms_shm_index_entry_t entry);

/**
 * \brief add a new entry to the index
 *
 * \param setlabel Name of the metric set that is registered for this entry
 * \param fslocation Name of the shared memory location that the information related to metric set is retained
 * \return shm_index_entry if the new entry has been added successfully, NULL otherwise
 */
ldms_shm_index_entry_t ldms_shm_index_add_entry(const char *setlabel,
		const char *fslocation);
/**
 * \brief deregister a writer from this entry
 *
 * \param entry
 * \return the number of remaining users (reader/writer) of this entry and its associated set
 */
int ldms_shm_index_entry_deregister_writer(ldms_shm_index_entry_t entry);

#endif /* LDMS_SHM_INDEX_H_ */
