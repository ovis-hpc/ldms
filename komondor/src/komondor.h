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

/**
 * \file komondor.h
 * \author Narate Taerat
 * \brief Definitions shared among komondor modules.
 */

#include <sys/queue.h>
#include <stdarg.h>
#include "sos/sos.h"
#include "ovis_util/util.h"

/**
 * Return code from the action script.
 */
#define KMD_RESOLVED_CODE 254

/**
 * Default store plugin.
 */
#define KSTORE_DEFAULT "kstore_default"

#pragma pack(4)
struct kmd_msg {
	uint16_t model_id;
	uint64_t metric_id;
	uint8_t level;
	uint32_t sec;
	uint32_t usec;
};

typedef struct event_action_s {
	struct sos_blob_obj_s blob;
	struct {
		uint32_t model_id;
		uint64_t metric_id;
		char action[0];
	} data;
} *event_action_t;
#pragma pack()

typedef enum k_event_status {
	KMD_EVENT_NEW,
	KMD_EVENT_RESOLVING,
	KMD_EVENT_RESOLVED,
	KMD_EVENT_RESOLVE_FAIL,
} k_event_status_e;

static inline size_t evact_blob_len(event_action_t e)
{
	size_t slen = strlen(e->data.action) + 1;
	return sizeof(e->data) + slen;
}


/**
 * Komondor store interface.
 *
 * A store for Komondor stores events. Upon event arrival, komondor will invoke
 * \c get_event_object(), in which the store should return an event object
 * handle. Then, komondor will invoke actions asssociated with the event, and
 * will call \c event_update() if necessary. After komondor has nothing more to
 * do with the event, \c put_event_object() will be called.
 */
struct kmd_store {

	/**
	 * Store configuration.
	 *
	 * \param s The store handle.
	 * \param av_list Attribute-value list.
	 *
	 * \returns 0 on success.
	 * \returns Error code on error.
	 */
	int (*config)(struct kmd_store *s, struct attr_value_list *av_list);

	/**
	 * Get store event object based on komondor event entry \c e.
	 *
	 * \param s The store handle.
	 * \param e The komondor event (message) entry.
	 *
	 * \returns store event object handle on success.
	 * \returns NULL on error.
	 */
	void* (*get_event_object)(struct kmd_store *s, struct kmd_msg *e);

	/**
	 * Put store event object.
	 * The store should free the event_object handle when this function is
	 * called. The event object (e.g. a row in SQL database) itself should
	 * not be removed.
	 *
	 * \param s The store handle.
	 * \param event_object The store event object handle.
	 */
	void (*put_event_object)(struct kmd_store *s, void *event_object);

	/**
	 * Update an event in the store.
	 *
	 * \param s The store handle.
	 * \param event_object Event object handle.
	 * \param status The event status.
	 *
	 * \returns 0 on success.
	 * \returns Error code on error.
	 */
	int (*event_update)(struct kmd_store *s, void *event_object,
				k_event_status_e status);

	/**
	 * Store destructor.
	 *
	 * \param s The store handle to be destroyed.
	 */
	void (*destroy)(struct kmd_store *s);

	/**
	 * List entry for store for internal use.
	 */
	LIST_ENTRY(kmd_store) entry;
};

typedef void (*kmd_log_f)(const char *fmt, ...);
typedef struct kmd_store *(*kmd_create_store_f)(kmd_log_f f);

void k_log(const char *fmt, ...);
