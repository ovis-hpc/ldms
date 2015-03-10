/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2015 Sandia Corporation. All rights reserved.
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
 * \file bhttpd.h
 * \author Narate Taerat (narate at ogc dot us)
 */
#ifndef __BHTTPD_H
#define __BHTTPD_H

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/http.h>

#include "baler/btypes.h"
#include "baler/butils.h"
#include "baler/bqueue.h"

/***** GLOBAL VARIABLES *****/
extern struct bq_store *bq_store;
extern struct bmptn_store *mptn_store;
extern struct event_base *evbase;
extern const char *store_path;

/***** TYPES *****/
struct bhttpd_req_ctxt {
	struct evhttp_request *req;
	const struct evhttp_uri *uri;
	struct bpair_str_head kvlist;
	int httprc;
	struct evbuffer *evbuffer;
	struct evkeyvalq *hdr;
	char errstr[1024];
};

typedef void (*bhttpd_req_handle_fn_t)(struct bhttpd_req_ctxt *ctxt);

typedef void (*bhttpd_work_routine_fn_t)(void *arg);

struct bhttpd_work {
	struct bqueue_entry qent;
	bhttpd_work_routine_fn_t routine;
	void *arg;
};

struct bhttpd_msg_query_session {
	struct bquery *q;
	struct event *event;
	struct timeval last_use;
	struct bq_formatter *fmt;
	int first;
	uint64_t ref;
};

void set_uri_handle(const char *uri, bhttpd_req_handle_fn_t fn);

void *get_uri_handle(const char *uri);

int submit_work(bhttpd_work_routine_fn_t routine, void *arg);

void bhttpd_req_ctxt_errprintf(struct bhttpd_req_ctxt *ctxt, int httprc, const char *fmt, ...);

static inline
const char *bpair_str_value(struct bpair_str_head *head, const char *key)
{
	struct bpair_str *kv = bpair_str_search(head, key, NULL);
	if (kv)
		return kv->s1;
	return NULL;
}

#endif
