/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2017-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2017-2018 Open Grid Computing, Inc. All rights reserved.
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
#include <openssl/sha.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <coll/rbt.h>		/* container_of() */
#include <sys/queue.h>
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <pthread.h>
#include <stdarg.h>
#include <limits.h>
#include <errno.h>

#include <coll/htbl.h>
#include "kvd_log.h"
#include "kvd_svc.h"

/**
 * \page kvd_svc Common web services library
 *
 * \brief This is the main page for KVD Web Services
 *
 */

void kvd_req_ctxt_errprintf(struct kvd_req_ctxt *ctxt, int httprc, const char *fmt, ...)
{
	va_list ap;
	ctxt->httprc = httprc;
	va_start(ap, fmt);
	vsnprintf(ctxt->errstr, sizeof(ctxt->errstr), fmt, ap);
	va_end(ap);
}

static
void kvd_set_verbosity(const char *v)
{
	const char *vb[] = {
		"DEBUG",
		"INFO",
		"WARN",
		"ERROR",
		"QUIET"
	};
	int n = sizeof(vb)/sizeof(*vb);
	int i;
	for (i = 0; i < n; i++) {
		if (strcasecmp(v, vb[i]) == 0) {
			break;
		}
	}
	if (i >= n) {
		kerr("Unknown verbosity level: %s; Only one of the DEBUG, "
				"WARN, INFO and ERROR is allowed.", v);
		exit(-1);
	}
	kvd_log_set_level(i);
}

int cmp_svc_name(const void *a, const void *b, size_t len)
{
	return strncmp(a, b, len);
}

static kvd_svc_fn_t get_uri_handler(kvd_svc_t svc, const char *uri)
{
	hent_t hent = htbl_find(svc->dispatch_table, uri, strlen(uri));
	if (hent) {
		kvd_svc_handler_t h = container_of(hent, struct kvd_svc_handler_s, hent);
		return h->svc_fn;
	}
	return NULL;
}

void kvd_set_svc_handler(kvd_svc_t svc, const char *uri, kvd_svc_fn_t fn)
{
	if (!svc->dispatch_table) {
		svc->dispatch_table = htbl_alloc(cmp_svc_name, 1123);
		if (!svc->dispatch_table) {
			kerror("set_uri_handler");
			exit(ENOMEM);
		}
	}

	kvd_svc_handler_t h = malloc(sizeof(*h));
	if (!h) {
		kerror("set_uri_handler");
		exit(ENOMEM);
	}
	h->svc_fn = fn;
	hent_init(&h->hent, strdup(uri), strlen(uri));
	if (!h->hent.key) {
		kerror("set_uri_handler");
		exit(ENOMEM);
	}
	htbl_ins(svc->dispatch_table, &h->hent);
}

static void kvd_evhttp_cb(struct evhttp_request *req, void *arg)
{
	const struct evhttp_uri *uri = evhttp_request_get_evhttp_uri(req);
	const char *path = evhttp_uri_get_path(uri);
	struct evkeyvalq params;
	const char *query = evhttp_uri_get_query(uri);
	struct evkeyvalq *ohdr = evhttp_request_get_output_headers(req);
	struct kvd_req_ctxt *ctxt;
	kvd_svc_t svc = arg;
	int rc;

	evhttp_add_header(ohdr, "Access-Control-Allow-Origin", "*");

	kvd_svc_fn_t fn = get_uri_handler(svc, path);
	if (!fn) {
		evhttp_send_reply(req, HTTP_NOTFOUND, NULL, NULL);
		return;
	}

	ctxt = malloc(sizeof(*ctxt));
	if (!ctxt) {
		evhttp_send_reply(req, HTTP_INTERNAL, "Out of memory.", NULL);
		return;
	}

	if (query) {
		struct evkeyvalq *ohdr = evhttp_request_get_output_headers(req);
		evhttp_parse_query_str(query, &params);
		ctxt->params = &params;
	}

	ctxt->hdr = ohdr;
	ctxt->req = req;
	ctxt->uri = uri;
	ctxt->query = query;
	ctxt->httprc = HTTP_OK;
	sprintf(ctxt->errstr, "Unknown error.");

	ctxt->evbuffer = evbuffer_new();
	if (!ctxt->evbuffer) {
		evhttp_send_reply(req, HTTP_INTERNAL, "Out of memory.", NULL);
		goto cleanup;
	}

	/* Prepare the header */
	evhttp_add_header(ctxt->hdr, "Cache-Control", "no-cache, no-store, must-revalidate");
	evhttp_add_header(ctxt->hdr, "Pragma", "no-cache");
	evhttp_add_header(ctxt->hdr, "Expires", "0");

	/* Call the URI handler */
	fn(ctxt);

	/* Send the reply */
	if (ctxt->httprc == HTTP_OK) {
		evhttp_send_reply(req, ctxt->httprc, NULL, ctxt->evbuffer);
	} else {
		evhttp_send_reply(req, ctxt->httprc, ctxt->errstr, NULL);
	}

cleanup:
	if (ctxt->evbuffer)
		evbuffer_free(ctxt->evbuffer);
	free(ctxt);
}

void kvd_svc_init(kvd_svc_t svc, int foreground)
{
	int rc, i;

	kvd_set_verbosity(svc->verbosity);

	if (svc->log_path) {
		rc = kvd_log_open(svc->log_path);
		if (rc) {
			kerr("Failed to open the log file '%s'", svc->log_path);
			exit(ENOENT);
		}
	}

	kinfo("kvd Version: %s\n", "biffle");
	kinfo("git-SHA: %s\n", "DEADBEEF");

	if (!foreground) {
		kinfo("Daemonizing...");
		if (daemon(1, 1) == -1) {
			kerror("daemon");
			exit(-1);
		}
		kinfo("Daemonized");
	}

	rc = evthread_use_pthreads();
	svc->evbase = event_base_new();
	if (!svc->evbase) {
		kerror("event_base_new()");
		exit(-1);
	}

	svc->evhttp = evhttp_new(svc->evbase);
	if (!svc->evhttp) {
		kerr("evhttp_new() error: %d", errno);
		exit(-1);
	}

	evhttp_set_gencb(svc->evhttp, kvd_evhttp_cb, svc);
	svc->evhttp_socket = evhttp_bind_socket_with_handle(svc->evhttp, svc->address, atoi(svc->port));
	if (!svc->evhttp_socket) {
		kerr("evhttp_bind_socket_with_handle() error, errno: %d", errno);
		exit(errno);
	}

	event_base_dispatch(svc->evbase);
}

void kvd_cli_init(kvd_svc_t svc, int foreground)
{
	int rc, i;

	kvd_set_verbosity(svc->verbosity);

	if (svc->log_path) {
		rc = kvd_log_open(svc->log_path);
		if (rc) {
			kerr("kvd_cli_init[%d] '%s'", __LINE__, svc->log_path);
			exit(ENOENT);
		}
	}

	kinfo("kvd Version: %s\n", "biffle");
	kinfo("git-SHA: %s\n", "DEADBEEF");

	if (!foreground) {
		kinfo("Daemonizing...");
		if (daemon(1, 1) == -1) {
			kerror("daemon");
			exit(-1);
		}
		kinfo("Daemonized");
	}

	rc = evthread_use_pthreads();
	svc->evbase = event_base_new();
	if (!svc->evbase) {
		kerr("kvd_cli_init[%] error %d", __LINE__, errno);
		exit(-1);
	}

	svc->evhttp = evhttp_new(svc->evbase);
	if (!svc->evhttp) {
		kerr("kvd_cli_init[%d] error: %d", __LINE__, errno);
		exit(-1);
	}

	svc->evhttp_conn = evhttp_connection_base_new(svc->evbase, NULL, svc->address, atoi(svc->port));
	if (!svc->evhttp_conn) {
		kerr("kvd_cli_init[%d] error: %d", __LINE__, errno);
		exit(-1);
	}

	// event_base_dispatch(svc->evbase);
}

void kvd_req_done(struct evhttp_request *req, void *arg)
{
	kvd_svc_t svc = arg;
	size_t len;

	if (req == NULL) {
		kerr("FAILED (timeout)\n");
		return;
	}

	if (evhttp_request_get_response_code(req) != HTTP_OK) {

		kerr("FAILED (response code)\n");
		return;
	}

	if (evhttp_find_header(evhttp_request_get_input_headers(req), "Content-Type") == NULL) {
		kerr("FAILED (content type)\n");
		return;
	}

	len = evbuffer_get_length(evhttp_request_get_input_buffer(req));
	event_base_loopexit(svc->evbase, NULL);
}

int kvd_cli_post(kvd_svc_t svc, char *uri, char *data, size_t data_len)
{
	struct evhttp_request *req = evhttp_request_new(kvd_req_done,  svc);
	if (!req)
		return errno;
	evhttp_add_header(req->output_headers, "Host", svc->address);
	evhttp_add_header(req->output_headers, "Port", svc->port);
	if (data_len)
		evbuffer_add(evhttp_request_get_output_buffer(req), data, data_len);
	evhttp_make_request(svc->evhttp_conn, req, EVHTTP_REQ_POST, uri);
	event_base_dispatch(svc->evbase);
}

static void __attribute__ ((constructor)) kvd_svc_lib_init(void)
{
}

static void __attribute__ ((destructor)) kvd_svc_lib_term(void)
{
}
