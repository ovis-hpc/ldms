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
int __evbuffer_add_json_bstr(struct evbuffer *evb, const struct bstr *bstr)
{
	int rc = 0;
	int i;
	evbuffer_add_printf(evb, "\"");
	if (rc < 0)
		return errno;
	for (i = 0; i < bstr->blen; i++) {
		switch (bstr->cstr[i]) {
		case '"':
		case '\\':
			rc = evbuffer_add_printf(evb, "%c", '\\');
			if (rc < 0)
				return errno;
		}
		rc = evbuffer_add_printf(evb, "%c", bstr->cstr[i]);
		if (rc < 0)
			return errno;
	}
	rc = evbuffer_add_printf(evb, "\"");
	if (rc < 0)
		return errno;
	return 0;
}

static
void __bhttpd_handle_query_ptn(struct bhttpd_req_ctxt *ctxt, int is_metric)
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
		if (bq_is_metric_pattern(bq_store, i) != is_metric)
			continue;
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
void bhttpd_handle_query_ptn(struct bhttpd_req_ctxt *ctxt)
{
	__bhttpd_handle_query_ptn(ctxt, 0);
}

static
void bhttpd_handle_query_metric_ptn(struct bhttpd_req_ctxt *ctxt)
{
	__bhttpd_handle_query_ptn(ctxt, 1);
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

	bdebug("creating session: %lu", (uint64_t)qs);

	qs->first = 1;
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
			qs->first = 0;
			if (is_fwd)
				rc = bq_first_entry(qs->q);
			else
				rc = bq_last_entry(qs->q);
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
		if (bq_is_metric_pattern(bq_store, i))
			continue;
		barray_get(array, i, &x);
		if (first) {
			evbuffer_add_printf(ctxt->evbuffer, "[%d, %d]", i, x);
			first = 0;
		} else {
			evbuffer_add_printf(ctxt->evbuffer, ", [%d, %d]", i, x);
		}
	}
	evbuffer_add_printf(ctxt->evbuffer, "], ");
	n = bmptn_store_get_last_cls_id(mptn_store);
	evbuffer_add_printf(ctxt->evbuffer, "\"cluster_names\": {");
	first = 1;
	for (i = 1; i <= n; i++) {
		const struct bstr *bstr = bmptn_get_cluster_name(mptn_store, i);
		if (!bstr)
			continue;
		if (first)
			first = 0;
		else
			evbuffer_add_printf(ctxt->evbuffer, ",");
		evbuffer_add_printf(ctxt->evbuffer, "\"%d\": ", i);
		__evbuffer_add_json_bstr(ctxt->evbuffer, bstr);
	}
	evbuffer_add_printf(ctxt->evbuffer, "}}");
}

static
void bhttpd_handle_query_metric_meta(struct bhttpd_req_ctxt *ctxt)
{
	int n = bptn_store_last_id(bq_get_ptn_store(bq_store));
	int rc;
	int i, x, first;
	const struct bstr *ptn;
	struct bhash *idhash = NULL;
	struct bhash_iter *itr = NULL;

	idhash = bhash_new(65521, 11, NULL);
	if (!idhash) {
		bhttpd_req_ctxt_errprintf(ctxt, HTTP_INTERNAL, "Not enough memory");
		goto cleanup;
	}
	first = 1;
	evbuffer_add_printf(ctxt->evbuffer, "{\"map\": [");
	for (i = BMAP_ID_BEGIN; i <= n; i++) {
		if (!bq_is_metric_pattern(bq_store, i))
			continue;
		ptn = bptn_store_get_ptn(bq_get_ptn_store(bq_store), i);
		if (!ptn) {
			bhttpd_req_ctxt_errprintf(ctxt, HTTP_INTERNAL,
					"Cannot get pattern id: %d", i);
			goto cleanup;
		}
		if (first) {
			evbuffer_add_printf(ctxt->evbuffer, "[%d, %d]", i,
								ptn->u32str[1]);
			first = 0;
		} else {
			evbuffer_add_printf(ctxt->evbuffer, ", [%d, %d]", i,
								ptn->u32str[1]);
		}
		bhash_entry_set(idhash, (void*)&ptn->u32str[1],
					sizeof(uint32_t), ptn->u32str[1]);
	}

	evbuffer_add_printf(ctxt->evbuffer, "], ");

	first = 1;
	evbuffer_add_printf(ctxt->evbuffer, "\"cluster_names\": {");

	itr = bhash_iter_new(idhash);
	rc = bhash_iter_begin(itr);
	while (rc == 0) {
		struct bhash_entry *ent = bhash_iter_entry(itr);
		if (first)
			first = 0;
		else
			evbuffer_add_printf(ctxt->evbuffer, ",");
		evbuffer_add_printf(ctxt->evbuffer, "\"%d\": ", (uint32_t)ent->value);
		const struct bstr *name = btkn_store_get_bstr(bq_get_tkn_store(bq_store), ent->value);
		if (!name) {
			evbuffer_add_printf(ctxt->evbuffer, "\"!!!ERROR!!!\"");
		} else {
			__evbuffer_add_json_bstr(ctxt->evbuffer, name);
		}
		rc = bhash_iter_next(itr);
	}
	evbuffer_add_printf(ctxt->evbuffer, "}}");

cleanup:
	if (itr)
		bhash_iter_free(itr);
	if (idhash)
		bhash_free(idhash);
}

static
float __get_url_param_float(struct bhttpd_req_ctxt *ctxt, const char *key,
							float default_value)
{
	const char *str = bpair_str_value(&ctxt->kvlist, key);
	if (!str)
		return default_value;
	return strtof(str, NULL);
}

static
int __get_url_param_int(struct bhttpd_req_ctxt *ctxt, const char *key,
							int default_value)
{
	const char *str = bpair_str_value(&ctxt->kvlist, key);
	if (!str)
		return default_value;
	return atoi(str);
}

static
void bhttpd_handle_query_img2(struct bhttpd_req_ctxt *ctxt)
{
	int rc;
	int first = 1;
	struct bpixel p;
	char ts0[16];
	char ts1[16];
	char host_ids[32];
	const char *ptn_ids = bpair_str_value(&ctxt->kvlist, "ptn_ids");
	const char *img_store = bpair_str_value(&ctxt->kvlist, "img_store");

	float spp = __get_url_param_float(ctxt, "spp", 0);
	float npp = __get_url_param_float(ctxt, "npp", 0);
	int width = __get_url_param_int(ctxt, "width", 0);
	int height = __get_url_param_int(ctxt, "height", 0);
	int ts_begin = __get_url_param_int(ctxt, "ts_begin", 0);
	int host_begin = __get_url_param_int(ctxt, "host_begin", 0);
	int ts_end = ts_begin + width*spp;
	int host_end = host_begin + height*npp;

	int *data = NULL;

	struct bimgquery *q;

	if (!img_store) {
		bhttpd_req_ctxt_errprintf(ctxt, HTTP_INTERNAL,
				"Please specify 'img_store'"
				" (see /list_img_store)");
		return;
	}

	if (!width || !height) {
		bhttpd_req_ctxt_errprintf(ctxt, HTTP_INTERNAL,
					"Please specify width and height");
		return;
	}

	if (spp < 0.000001 || npp < 0.000001) {
		bhttpd_req_ctxt_errprintf(ctxt, HTTP_INTERNAL,
					"Please specify spp and npp");
		return;
	}

	snprintf(ts0, sizeof(ts0), "%d", ts_begin);
	snprintf(ts1, sizeof(ts0), "%d", ts_end);
	snprintf(host_ids, sizeof(host_ids), "%d-%d", host_begin, host_end);

	data = calloc(sizeof(int),  width * height);

	if (!data) {
		bhttpd_req_ctxt_errprintf(ctxt, HTTP_INTERNAL, "Out of memory.");
		return;
	}

	q = bimgquery_create(bq_store, host_ids,
			ptn_ids, ts0, ts1, img_store, &rc);

	if (!q) {
		bhttpd_req_ctxt_errprintf(ctxt, HTTP_INTERNAL,
				"bimgquery_create() error, errno: %d", errno);
		return;
	}
	rc = bq_first_entry((void*)q);
	while (rc == 0) {
		rc = bq_img_entry_get_pixel(q, &p);
		if (rc)
			break;
		int x = (p.sec - ts_begin) / spp;
		int y = (p.comp_id - host_begin) / npp;
		if (x >= width || y >= height)
			goto next;
		int idx = y*width + x;
		data[idx] += p.count;
	next:
		rc = bq_next_entry((void*)q);
	}
	evbuffer_add(ctxt->evbuffer, data, sizeof(int)*width*height);
	bimgquery_destroy(q);
	bdebug("sending data, size: %d", sizeof(int)*width*height);
	free(data);
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
	struct bimgquery *q;

	if (!img_store) {
		bhttpd_req_ctxt_errprintf(ctxt, HTTP_INTERNAL,
				"Please specify 'img_store'"
				" (see /list_img_store)");
		return;
	}

	q = bimgquery_create(bq_store, host_ids,
			ptn_ids, ts0, ts1, img_store, &rc);

	if (!q) {
		bhttpd_req_ctxt_errprintf(ctxt, HTTP_INTERNAL,
				"bimgquery_create() error, errno: %d", errno);
		return;
	}
	// evbuffer_add_printf(ctxt->evbuffer, "{\"pixels\": [");
	rc = bq_first_entry((void*)q);
	while (rc == 0) {
		rc = bq_img_entry_get_pixel(q, &p);
		if (rc)
			break;
		evbuffer_add(ctxt->evbuffer, &p, sizeof(p));
		/*
		if (first)
			first = 0;
		else
			evbuffer_add_printf(ctxt->evbuffer, ",");
		evbuffer_add_printf(ctxt->evbuffer, "[%d, %d, %d, %d]",
					p.sec, p.comp_id, p.ptn_id, p.count);
		*/
		rc = bq_next_entry((void*)q);
	}
	// evbuffer_add_printf(ctxt->evbuffer, "]}");
	bimgquery_destroy(q);
}

static
void bhttpd_handle_query_destroy_session(struct bhttpd_req_ctxt *ctxt)
{
	const char *_session_id = bpair_str_value(&ctxt->kvlist, "session_id");
	uint64_t session_id;
	struct bhttpd_msg_query_session *qs;
	struct bhash_entry *ent;
	if (!session_id) {
		bhttpd_req_ctxt_errprintf(ctxt, HTTP_INTERNAL,
				"session_id is not set");
		return;
	}
	session_id = strtoul(_session_id, NULL, 0);

	pthread_mutex_lock(&query_session_mutex);
	ent = bhash_entry_get(query_session_hash, (void*)&session_id,
			sizeof(session_id));
	if (!ent) {
		bhttpd_req_ctxt_errprintf(ctxt, HTTP_INTERNAL,
				"session_id %ul not found", session_id);
		pthread_mutex_unlock(&query_session_mutex);
		return;
	}
	bhash_entry_remove_free(query_session_hash, ent);
	pthread_mutex_unlock(&query_session_mutex);
	qs = (void*)session_id;
	bhttpd_msg_query_session_destroy(qs);
}

static
void bhttpd_handle_query_host(struct bhttpd_req_ctxt *ctxt)
{
	const struct bstr *bstr;
	struct btkn_store *cmp_store = bq_get_cmp_store(bq_store);
	int id = 1;
	int first = 1;
	evbuffer_add_printf(ctxt->evbuffer, "{ \"host_ids\": {");
	while (1) {
		bstr = btkn_store_get_bstr(cmp_store, id + BMAP_ID_BEGIN - 1);
		if (!bstr)
			break;
		if (first)
			first = 0;
		else
			evbuffer_add_printf(ctxt->evbuffer, ", ");
		evbuffer_add_printf(ctxt->evbuffer, "\"%d\": \"%.*s\"",
						id, bstr->blen, bstr->cstr);
		id++;
	}
	evbuffer_add_printf(ctxt->evbuffer, "}}");
}

struct bhttpd_handle_fn_entry {
	const char *key;
	const char *content_type;
	void (*fn)(struct bhttpd_req_ctxt*);
};

#define  HTTP_CONT_JSON    "application/json"
#define  HTTP_CONT_STREAM  "application/octet-stream"

struct bhttpd_handle_fn_entry query_handle_entry[] = {
	{ "PTN",         HTTP_CONT_JSON,   bhttpd_handle_query_ptn         },
	{ "METRIC_PTN",  HTTP_CONT_JSON,   bhttpd_handle_query_metric_ptn  },
	{ "MSG",         HTTP_CONT_JSON,   bhttpd_handle_query_msg         },
	{ "META",        HTTP_CONT_JSON,   bhttpd_handle_query_meta        },
	{ "METRIC_META", HTTP_CONT_JSON,   bhttpd_handle_query_metric_meta },
	{ "IMG",         HTTP_CONT_STREAM, bhttpd_handle_query_img         },
	{ "IMG2",        HTTP_CONT_STREAM, bhttpd_handle_query_img2        },
	{ "HOST",        HTTP_CONT_JSON,   bhttpd_handle_query_host        },
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
			evhttp_add_header(ctxt->hdr, "content-type",
					query_handle_entry[i].content_type);
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
	set_uri_handle("/query/destroy_session",
			bhttpd_handle_query_destroy_session);
	query_session_hash = bhash_new(4099, 7, NULL);
	if (!query_session_hash) {
		berror("bhash_new()");
		exit(-1);
	}
	s = getenv("BHTTPD_QUERY_SESSION_TIMEOUT");
	if (s)
		query_session_timeout.tv_sec = atoi(s);
}
