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
 * \author Narate Taerat (narate at ogc dot us)
 * \file bhttpd_query.c
 */

#include <event2/event.h>

#include "bhttpd.h"
#include "bq_fmt_json.h"

#include "baler/btypes.h"
#include "baler/bptn.h"
#include "baler/bhash.h"
#include "baler/bmeta.h"
#include "baler/bqueue.h"
#include "baler/butils.h"
#include "query/bquery.h"

pthread_mutex_t query_session_mutex = PTHREAD_MUTEX_INITIALIZER;
struct bhash *query_session_hash;
uint32_t query_session_gn;
struct timeval query_session_timeout = {.tv_sec = 600, .tv_usec = 0};

static
void bhttpd_handle_query_ptn(struct bhttpd_req_ctxt *ctxt)
{
	struct bptn_store *ptn_store = bq_get_ptn_store(bq_store);
	int n = bptn_store_last_id(ptn_store);
	int rc = 0;
	int i;
	int first = 1;
	struct bq_formatter *fmt = NULL;
	struct bquery *q = NULL;
	struct bdstr *bdstr = NULL;

	fmt = bqfmt_json_new();
	if (!fmt) {
		bhttpd_req_ctxt_errprintf(ctxt, HTTP_INTERNAL, "Not enough memory");
		goto cleanup;
	}

	q = bquery_create(bq_store, NULL, NULL, NULL, NULL, 1, ' ', &rc);
	if (!q) {
		bhttpd_req_ctxt_errprintf(ctxt, HTTP_INTERNAL, "Cannot create query, rc: %d", rc);
		goto cleanup;
	}

	bq_set_formatter(q, fmt);

	bdstr = bdstr_new(1024);
	if (!bdstr) {
		bhttpd_req_ctxt_errprintf(ctxt, HTTP_INTERNAL, "Not enough memory");
		goto cleanup;
	}

	evbuffer_add_printf(ctxt->evbuffer, "{\"result\": [");
	for (i=BMAP_ID_BEGIN; i<=n; i++) {
		rc = bq_get_ptn(q, i, bdstr);
		if (rc) {
			bhttpd_req_ctxt_errprintf(ctxt, HTTP_INTERNAL, "pattern query internal"
					" error, rc: %d", rc);
			goto cleanup;
		}
		if (first) {
			first = 0;
			evbuffer_add_printf(ctxt->evbuffer, "%s", bdstr->str);
		} else {
			evbuffer_add_printf(ctxt->evbuffer, ", %s", bdstr->str);
		}
	}
	evbuffer_add_printf(ctxt->evbuffer, "]}");

cleanup:
	if (fmt)
		bqfmt_json_free(fmt);
	if (q)
		bquery_destroy(q);
	if (bdstr)
		bdstr_free(bdstr);
}

static
void bhttpd_msg_query_expire_cb(evutil_socket_t fd, short what, void *arg);

static
void bhttpd_msg_query_session_destroy(struct bhttpd_msg_query_session *qs)
{
	bdebug("destroying session: %lu", (uint64_t)qs);
	if (qs->event)
		event_free(qs->event);
	if (qs->q)
		bquery_destroy(qs->q);
	free(qs);
}

static
struct bhttpd_msg_query_session *bhttpd_msg_query_session_create(struct bhttpd_req_ctxt *ctxt)
{
	struct bhttpd_msg_query_session *qs;
	const char *host_ids, *ptn_ids, *ts0, *ts1;
	struct bpair_str *kv;
	int rc;
	qs = calloc(1, sizeof(*qs));
	if (!qs)
		return NULL;
	qs->event = event_new(evbase, -1, EV_READ,
			bhttpd_msg_query_expire_cb, qs);
	if (!qs->event) {
		bhttpd_req_ctxt_errprintf(ctxt, HTTP_INTERNAL,
				"Not enough memory.");
		goto err;
	}
	host_ids = bpair_str_value(&ctxt->kvlist, "host_ids");
	ptn_ids = bpair_str_value(&ctxt->kvlist, "ptn_ids");
	ts0 = bpair_str_value(&ctxt->kvlist, "ts0");
	ts1 = bpair_str_value(&ctxt->kvlist, "ts1");
	qs->q = bquery_create(bq_store, host_ids, ptn_ids, ts0, ts1, 1, ' ', &rc);
	if (!qs->q) {
		bhttpd_req_ctxt_errprintf(ctxt, HTTP_INTERNAL,
				"msg query creation failed, rc: %d.", rc);
		goto err;
	}
	qs->fmt = bqfmt_json_new();
	if (!qs->fmt) {
		bhttpd_req_ctxt_errprintf(ctxt, HTTP_INTERNAL,
				"Cannot create bqfmt_json, errno: %d.", errno);
		goto err;
	}

	bq_set_formatter(qs->q, qs->fmt);

	return qs;
err:

	bhttpd_msg_query_session_destroy(qs);
	return NULL;
}

static
void bhttpd_msg_query_expire_cb(evutil_socket_t fd, short what, void *arg)
{
	struct bhttpd_msg_query_session *qs = arg;
	uint64_t key = (uint64_t)arg;
	struct timeval tv, dtv;
	struct bhash_entry *ent;
	pthread_mutex_lock(&query_session_mutex);
	gettimeofday(&tv, NULL);
	timersub(&tv, &qs->last_use, &dtv);
	dtv.tv_sec *= 2;
	if (timercmp(&dtv, &query_session_timeout, <)) {
		/* This is the case where the client access the query session
		 * at the very last second. Just do nothing and wait for the
		 * next timeout event. */
		pthread_mutex_unlock(&query_session_mutex);
		return;
	}
	ent = bhash_entry_get(query_session_hash, (void*)&key, sizeof(key));
	if (!ent) {
		bwarn("Cannot find hash entry %d, in function %s", key, __func__);
		pthread_mutex_unlock(&query_session_mutex);
		return;
	}
	bhash_entry_remove_free(query_session_hash, ent);
	pthread_mutex_unlock(&query_session_mutex);
	bhttpd_msg_query_session_destroy(qs);
}

static
int bhttpd_msg_query_session_recover(struct bhttpd_msg_query_session *qs,
								int is_fwd)
{
	int rc = 0;
	uint64_t ref;
	int (*begin)(struct bquery *q);
	int (*step)(struct bquery *q);

	if (is_fwd) {
		begin = bq_last_entry;
		step = bq_prev_entry;
	} else {
		begin = bq_first_entry;
		step = bq_next_entry;
	}

	rc = begin(qs->q);
	if (rc)
		return rc;

	while (rc == 0) {
		ref = bq_entry_get_ref(qs->q);
		if (ref == qs->ref)
			break;
		rc = step(qs->q);
	}

	return rc;
}

static
void bhttpd_handle_query_msg(struct bhttpd_req_ctxt *ctxt)
{
	struct bhttpd_msg_query_session *qs = NULL;
	struct bdstr *bdstr;
	struct bhash_entry *ent = NULL;
	uint64_t session_id = 0;
	const char *str;
	int is_fwd = 1;
	int i, n = 50;
	int rc;

	bdstr = bdstr_new(256);
	if (!bdstr) {
		bhttpd_req_ctxt_errprintf(ctxt, HTTP_INTERNAL, "Out of memory");
		return;
	}
	str = bpair_str_value(&ctxt->kvlist, "n");
	if (str)
		n = atoi(str);
	str = bpair_str_value(&ctxt->kvlist, "dir");
	if (str && strcmp(str, "bwd") == 0)
		is_fwd = 0;

	str = bpair_str_value(&ctxt->kvlist, "session_id");
	if (str) {
		session_id = strtoull(str, NULL, 0);
		ent = bhash_entry_get(query_session_hash, (void*)&session_id,
				sizeof(session_id));
		if (!ent) {
			bhttpd_req_ctxt_errprintf(ctxt, HTTP_INTERNAL,
					"Session %lu not found.", session_id);
			goto out;
		}
		qs = (void*)ent->value;
	}
	if (!qs) {
		qs = bhttpd_msg_query_session_create(ctxt);
		if (!qs) {
			/* bhttpd_msg_query_session_create() has already
			 * set the error message. */
			goto out;
		}
		session_id = (uint64_t)qs;
		ent = bhash_entry_set(query_session_hash, (void*)&session_id,
				sizeof(session_id), (uint64_t)(void*)qs);
		if (!ent) {
			bhttpd_req_ctxt_errprintf(ctxt, HTTP_INTERNAL,
					"Hash insert failed, errno: %d",
					errno);
			goto out;
		}
	}
	/* update last_use */
	gettimeofday(&qs->last_use, NULL);
	rc = event_add(qs->event, &query_session_timeout);
	if (rc) {
		bhttpd_req_ctxt_errprintf(ctxt, HTTP_INTERNAL,
				"event_add() rc: %d, errno: %d", rc, errno);
		goto out;
	}

	evbuffer_add_printf(ctxt->evbuffer, "{");

	evbuffer_add_printf(ctxt->evbuffer, "\"session_id\": %lu", session_id);

	evbuffer_add_printf(ctxt->evbuffer, ", \"msgs\": [");
	for (i = 0; i < n; i++) {
		if (qs->first) {
			rc = bq_first_entry(qs->q);
		} else {
			if (is_fwd)
				rc = bq_next_entry(qs->q);
			else
				rc = bq_prev_entry(qs->q);
		}
		if (rc) {
			rc = bhttpd_msg_query_session_recover(qs, is_fwd);
			break;
		}
		qs->ref = bq_entry_get_ref(qs->q);
		bqfmt_json_set_msg_ref(qs->fmt, qs->ref);
		str = bq_entry_print(qs->q, bdstr);
		if (!str) {
			bhttpd_req_ctxt_errprintf(ctxt, HTTP_INTERNAL,
					"bq_entry_print() errno: %d", errno);
			goto out;
		}
		if (i)
			evbuffer_add_printf(ctxt->evbuffer, ",%s", str);
		else
			evbuffer_add_printf(ctxt->evbuffer, "%s", str);
		bdstr_reset(bdstr);
	}
	evbuffer_add_printf(ctxt->evbuffer, "]");
	evbuffer_add_printf(ctxt->evbuffer, "}");

out:
	bdstr_free(bdstr);
}

static
void bhttpd_handle_query_meta(struct bhttpd_req_ctxt *ctxt)
{
	int n = bptn_store_last_id(bq_get_ptn_store(bq_store));
	struct barray *array = barray_alloc(sizeof(uint32_t), n+1);
	int rc;
	int i, x, first;
	if (!array) {
		bhttpd_req_ctxt_errprintf(ctxt, HTTP_INTERNAL,
				"Not enough memory");
		return;
	}
	rc = bmptn_store_get_class_id_array(mptn_store, array);
	if (rc) {
		bhttpd_req_ctxt_errprintf(ctxt, HTTP_INTERNAL,
				"bmptn_store_get_class_id_array(), rc: %d", rc);
		return;
	}
	n = barray_get_len(array);
	first = 1;
	evbuffer_add_printf(ctxt->evbuffer, "{\"map\": [");
	for (i = BMAP_ID_BEGIN; i < n; i++) {
		barray_get(array, i, &x);
		if (first) {
			evbuffer_add_printf(ctxt->evbuffer, "[%d, %d]", i, x);
			first = 0;
		} else {
			evbuffer_add_printf(ctxt->evbuffer, ", [%d, %d]", i, x);
		}
	}
	evbuffer_add_printf(ctxt->evbuffer, "]}");
}

static
void bhttpd_handle_query_img(struct bhttpd_req_ctxt *ctxt)
{
	int rc;
	int first = 1;
	struct bpixel p;
	const char *ts0 = bpair_str_value(&ctxt->kvlist, "ts0");
	const char *ts1 = bpair_str_value(&ctxt->kvlist, "ts1");;
	const char *host_ids = bpair_str_value(&ctxt->kvlist, "host_ids");
	const char *ptn_ids = bpair_str_value(&ctxt->kvlist, "ptn_ids");
	const char *img_store = bpair_str_value(&ctxt->kvlist, "img_store");
	struct bimgquery *q = bimgquery_create(bq_store, host_ids,
			ptn_ids, ts0, ts1, img_store, &rc);

	if (!q) {
		bhttpd_req_ctxt_errprintf(ctxt, HTTP_INTERNAL,
				"bimgquery_create() error, errno: %d", errno);
		return;
	}
	evbuffer_add_printf(ctxt->evbuffer, "{\"pixels\": [");
	rc = bq_first_entry((void*)q);
	while (rc == 0) {
		rc = bq_img_entry_get_pixel(q, &p);
		if (rc)
			break;
		if (first)
			first = 0;
		else
			evbuffer_add_printf(ctxt->evbuffer, ",");
		evbuffer_add_printf(ctxt->evbuffer, "[%d, %d, %d, %d]",
					p.sec, p.comp_id, p.ptn_id, p.count);
		rc = bq_next_entry((void*)q);
	}
	evbuffer_add_printf(ctxt->evbuffer, "]}");
	bimgquery_destroy(q);
}

struct bhttpd_handle_fn_entry {
	const char *key;
	void (*fn)(struct bhttpd_req_ctxt*);
};

struct bhttpd_handle_fn_entry query_handle_entry[] = {
	{  "PTN",   bhttpd_handle_query_ptn   },
	{  "MSG",   bhttpd_handle_query_msg   },
	{  "META",  bhttpd_handle_query_meta  },
	{  "IMG",   bhttpd_handle_query_img   },
};

static
void bhttpd_handle_query(struct bhttpd_req_ctxt *ctxt)
{
	struct bpair_str *kv;
	int i, n, rc;
	kv = bpair_str_search(&ctxt->kvlist, "type", NULL);
	if (!kv) {
		bhttpd_req_ctxt_errprintf(ctxt, HTTP_INTERNAL,
						"Query type not specified");
		return;
	}

	struct evkeyvalq *ohdr = evhttp_request_get_output_headers(ctxt->req);
	evhttp_add_header(ohdr, "content-type", "application/json");
	evhttp_add_header(ohdr, "Access-Control-Allow-Origin", "*");

	n = sizeof(query_handle_entry)/sizeof(query_handle_entry[0]);
	for (i = 0; i < n; i++) {
		if (strcasecmp(query_handle_entry[i].key, kv->s1) == 0)
			break;
	}
	if (i < n) {
		pthread_mutex_lock(&query_session_mutex);
		rc = bq_store_refresh(bq_store);
		if (rc) {
			bhttpd_req_ctxt_errprintf(ctxt, HTTP_INTERNAL,
				"bq_store_refresh() error, rc: %d", rc);
		} else {
			query_handle_entry[i].fn(ctxt);
		}
		pthread_mutex_unlock(&query_session_mutex);
	} else {
		bhttpd_req_ctxt_errprintf(ctxt, HTTP_INTERNAL,
				"Unknown query type: %s", kv->s1);
	}
}

static __attribute__((constructor))
void __init()
{
	const char *s;
	int i, n;
	bdebug("Adding /query handler");
	set_uri_handle("/query", bhttpd_handle_query);
	query_session_hash = bhash_new(4099, 7, NULL);
	if (!query_session_hash) {
		berror("bhash_new()");
		exit(-1);
	}
	s = getenv("BHTTPD_QUERY_SESSION_TIMEOUT");
	if (s)
		query_session_timeout.tv_sec = atoi(s);
}
