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
 * \file bhttpd.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <pthread.h>
#include <stdarg.h>

#include <sys/queue.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/thread.h>
#include <event2/http.h>

#include "baler/bptn.h"
#include "baler/bhash.h"
#include "baler/bmeta.h"
#include "baler/bqueue.h"
#include "baler/butils.h"
#include "query/bquery.h"

#include "bhttpd.h"

#include "config.h"

/**
 * \page bhttpd Baler HTTP service daemon
 *
 * \section synopsis SYNOPSIS
 *
 * \b bhttpd [\b -F] [\b -l \a logpath] [\b -p \a port] [\b -s \a storepath]
 *      [\b -v \a verbosity]
 *
 * \section desc DESCRIPTION
 * \b bhttpd is an HTTP service for Baler data accessing (e.g. pattern query),
 * and some Baler data processing (e.g. issuing meat-clustering).  This manual
 * page is about running and configuring \b bhttpd. The detail about each URI
 * can be found in \ref bhttpd_uri.
 *
 * \section options OPTIONS
 *
 * \par -a,-address ADDR
 * Specific address to bind to (default: 0.0.0.0)
 *
 * \par -p,--port PORT_NUMBER
 * Port number to bind to (default: 18888)
 *
 * \par -l,--log LOG_PATH
 * Path to log file. If not given, print to STDOUT and STDERR.
 *
 * \par -F,--foreground
 * Foreground mode (instead of daemon mode).
 *
 * \par -s,--store STORE_PATH
 * Path to baler store. This option is required.
 *
 * \par -w,--worker-threads N
 * The number of worker threads (for data processing, default: 1).
 *
 * \par -v,--verbosity LEVEL
 * LEVEL can only be one of the DEBUG, INFO, WARN, ERROR, and QUIET. DEBUG will
 * print everything. INFO will print everything but DEBUG. WARN prints only WARN
 * and ERROR. ERROR prints nothing but ERROR. QUIET prints nothing. (the default
 * value is: WARN).
 *
 */

/**
 * \page bhttpd_uri URIs served by bhttpd
 *
 * \brief This is the main page for URIs served by \b bhttpd.
 *
 */

/***** OPTIONS *****/
const char *short_opt = "a:p:l:s:Fv:w:V?";

struct option long_opt[] = {
	{"address",         1,  0,  'a'},
	{"port",            1,  0,  'p'},
	{"log",             1,  0,  'l'},
	{"foreground",      0,  0,  'F'},
	{"store",           1,  0,  's'},
	{"worker-threads",  1,  0,  'w'},
	{"verbosity",       1,  0,  'v'},
	{"version",         0,  0,  'V'},
	{0,                 0,  0,  0}
};

static
void usage()
{
	printf("Usage: bhttpd [-a addr] [-p port] [-l logpath] -s storepath\n"
	       "              [-F] [-v LEVEL]\n"
	       "\n"
	       "For more information, please see man page bhttpd(3)\n");
}

/***** GLOBAL VARIABLES *****/

char path_buff[PATH_MAX];

int fg = 0;
const char *log_path = NULL;
const char *store_path = NULL;
const char *port = "18888";
const char *address = "0.0.0.0";
const char *verbosity = "WARN";

struct event_base *evbase;
struct evhttp *evhttp;
struct evhttp_bound_socket *evhttp_socket;

struct bhash *handle_hash;

int N_worker_threads = 1;
pthread_t *worker_threads;

struct bqueue *workq;

struct bq_store *bq_store;
struct bmptn_store *mptn_store;

/***** FUNCTIONS *****/

void bhttpd_req_ctxt_errprintf(struct bhttpd_req_ctxt *ctxt, int httprc, const char *fmt, ...)
{
	va_list ap;
	ctxt->httprc = httprc;
	va_start(ap, fmt);
	vsnprintf(ctxt->errstr, sizeof(ctxt->errstr), fmt, ap);
	va_end(ap);
}

static
void set_verbosity(const char *v)
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
		berr("Unknown verbosity level: %s; Only one of the DEBUG, "
				"WARN, INFO and ERROR is allowed.", v);
		exit(-1);
	}
	blog_set_level(i);
}

static
void handle_args(int argc, char **argv)
{
	char c;
loop:
	c = getopt_long(argc, argv, short_opt, long_opt, NULL);
	switch (c) {
	case -1:
		goto out;
		break;
	case 'a':
		address = optarg;
		break;
	case 'p':
		port = optarg;
		break;
	case 'l':
		log_path = optarg;
		break;
	case 'F':
		fg = 1;
		break;
	case 's':
		store_path = optarg;
		break;
	case 'w':
		N_worker_threads = atoi(optarg);
		break;
	case 'v':
		verbosity = optarg;
		break;
	case 'V':
		printf("bhttpd Version: %s\n", PACKAGE_VERSION);
		printf("git-SHA: %s\n", OVIS_GIT_LONG);
		exit(0);
		break;
	case 'h':
	case '?':
	default:
		usage();
		exit(-1);
	}
	goto loop;
out:
	/* EMPTY */;
}

void *get_uri_handle(const char *uri)
{
	struct bhash_entry *ent = bhash_entry_get(handle_hash, uri, strlen(uri)+1);
	if (ent)
		return (void*)ent->value;
	return NULL;
}

void set_uri_handle(const char *uri, bhttpd_req_handle_fn_t fn)
{
	static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
	pthread_mutex_lock(&mutex);
	if (!handle_hash) {
		handle_hash = bhash_new(4099, 7, NULL);
		if (!handle_hash) {
			berror("bhash_new()");
			exit(-1);
		}
	}

	int len = strlen(uri) + 1;
	struct bhash_entry *ent = bhash_entry_set(handle_hash, uri, len, (uint64_t)fn);
	if (!ent) {
		berr("Cannot add handle for uri: %s\n", uri);
		exit(-1);
	}
	pthread_mutex_unlock(&mutex);
}

int submit_work(bhttpd_work_routine_fn_t routine, void *arg)
{
	struct bhttpd_work *w = malloc(sizeof(*w));
	if (!w)
		return ENOMEM;
	w->routine = routine;
	w->arg = arg;
	bqueue_nq(workq, &w->qent);
	return 0;
}

static
struct bhttpd_work *acquire_work()
{
	return (void*)bqueue_dq(workq);
}

static
void bhttpd_handle_test(struct bhttpd_req_ctxt *ctxt)
{
	struct bpair_str *kv;
	struct evbuffer *evb = evbuffer_new();
	if (!evb) {
		bhttpd_req_ctxt_errprintf(ctxt, HTTP_INTERNAL, "ENOMEM");
		return;
	}
	evbuffer_add_printf(evb, "<html><head><title>Test</title></head><body>Test Page.<BR>");

	evbuffer_add_printf(evb, "query:<br>");

	LIST_FOREACH(kv, &ctxt->kvlist, link) {
		evbuffer_add_printf(evb, "%s: %s<BR>", kv->s0, kv->s1);
	}

	evbuffer_add_printf(evb, "</body></html>");
	evhttp_send_reply(ctxt->req, 200, NULL, evb);
	if (evb)
		evbuffer_free(evb);
}

static
void print_uri_handles()
{
	struct bhash_iter *iter = bhash_iter_new(handle_hash);
	const struct bhash_entry *ent;
	bhash_iter_begin(iter);
	while ((ent = bhash_iter_entry(iter))) {
		binfo("key: %s, fn: %p", ent->key, (void*)ent->value);
	}
	bhash_iter_free(iter);
}

static
void bhttpd_evhttp_cb(struct evhttp_request *req, void *arg)
{
	const struct evhttp_uri *uri = evhttp_request_get_evhttp_uri(req);
	const char *path = evhttp_uri_get_path(uri);
	const char *query = evhttp_uri_get_query(uri);
	struct evkeyvalq *ohdr = evhttp_request_get_output_headers(req);
	struct bhttpd_req_ctxt *ctxt;
	int rc;

	bdebug("GET path: %s", path);
	evhttp_add_header(ohdr, "Access-Control-Allow-Origin", "*");

	if (evhttp_request_get_command(req) != EVHTTP_REQ_GET) {
		evhttp_send_reply(req, HTTP_BADMETHOD, NULL, NULL);
		return;
	}

	bhttpd_req_handle_fn_t fn;

	fn = get_uri_handle(path);

	if (!fn) {
		evhttp_send_reply(req, HTTP_NOTFOUND, NULL, NULL);
		return;
	}

	ctxt = malloc(sizeof(*ctxt));
	if (!ctxt) {
		evhttp_send_reply(req, HTTP_INTERNAL, "Out of memory.", NULL);
		return;
	}

	ctxt->hdr = ohdr;
	ctxt->req = req;
	ctxt->uri = uri;
	LIST_INIT(&ctxt->kvlist);
	ctxt->httprc = HTTP_OK;
	sprintf(ctxt->errstr, "Unknown error.");

	if (query) {
		rc = bparse_http_query(query, &ctxt->kvlist);
		if (rc) {
			berr("bparse_http_query() error, rc: %d\n", rc);
			evhttp_send_reply(req, HTTP_INTERNAL,
						"Query parse error.", NULL);
			goto cleanup;
		}
	}

	ctxt->evbuffer = evbuffer_new();
	if (!ctxt->evbuffer) {
		evhttp_send_reply(req, HTTP_INTERNAL, "Out of memory.", NULL);
		goto cleanup;
	}

	/* Call handling function */
	evhttp_add_header(ctxt->hdr, "Cache-Control", "no-cache, no-store, must-revalidate");
	evhttp_add_header(ctxt->hdr, "Pragma", "no-cache");
	evhttp_add_header(ctxt->hdr, "Expires", "0");
	fn(ctxt);

	if (ctxt->httprc == HTTP_OK) {
		evhttp_send_reply(req, ctxt->httprc, NULL, ctxt->evbuffer);
	} else {
		evhttp_send_reply(req, ctxt->httprc, ctxt->errstr, NULL);
	}

cleanup:
	bpair_str_list_free(&ctxt->kvlist);
	if (ctxt->evbuffer)
		evbuffer_free(ctxt->evbuffer);
	free(ctxt);
}

void *worker_routine(void *arg)
{
	struct bhttpd_work *w;
loop:
	w = acquire_work();
	w->routine(w->arg);
	goto loop;
	return NULL;
}

static
void uri_handle_init()
{
	/* URI HANDLES */
	set_uri_handle("/test", bhttpd_handle_test);
}

static
void open_stores()
{
	int len;
	int max_len;
	char *p;
	if (!store_path) {
		berr("-s STOREPATH is needed");
		exit(-1);
	}
	len = snprintf(path_buff, PATH_MAX, "%s", store_path);
	p = path_buff + len;
	max_len = PATH_MAX - len;

	bq_store = bq_open_store(path_buff);
	if (!bq_store) {
		berr("bq_open_store() failed: %m, path: %s", path_buff);
		exit(-1);
	}

	snprintf(p, max_len, "/mptn_store");
	mptn_store = bmptn_store_open(path_buff, store_path, 1);
	if (!mptn_store) {
		berr("bmptn_store_open() failed: %m, path: %s", path_buff);
		exit(-1);
	}

	*p = 0;
}

static
void init()
{
	int rc, i;

	set_verbosity(verbosity);

	if (log_path) {
		rc = blog_open_file(log_path);
		if (rc) {
			berr("Failed to open the log file '%s'", log_path);
			exit(-1);
		}
	}

	binfo("bhttpd Version: %s\n", PACKAGE_VERSION);
	binfo("git-SHA: %s\n", OVIS_GIT_LONG);

	if (!fg) {
		binfo("Daemonizing...");
		if (daemon(1, 1) == -1) {
			berror("daemon");
			exit(-1);
		}
		binfo("Daemonized");
	}

	open_stores();

	workq = bqueue_new();
	if (!workq) {
		berror("bqueue_new()");
		exit(-1);
	}

	worker_threads = malloc(N_worker_threads * sizeof(pthread_t));
	if (!worker_threads) {
		berror("malloc()");
		exit(-1);
	}

	for (i = 0; i < N_worker_threads; i++) {
		rc = pthread_create(worker_threads + i, NULL,
					worker_routine, NULL);
		if (rc) {
			berr("pthread_create() error, rc: %d", rc);
			exit(-1);
		}
	}

	uri_handle_init();

	rc = evthread_use_pthreads();
	evbase = event_base_new();
	if (!evbase) {
		berror("event_base_new()");
		exit(-1);
	}

	evhttp = evhttp_new(evbase);
	if (!evhttp) {
		berr("evhttp_new() error: %d", errno);
		exit(-1);
	}

	evhttp_set_gencb(evhttp, bhttpd_evhttp_cb, NULL);
	evhttp_socket = evhttp_bind_socket_with_handle(evhttp,
							address, atoi(port));
	if (!evhttp_socket) {
		berr("evhttp_bind_socket_with_handle() error, errno: %d",
									errno);
		exit(-1);
	}
}

int main(int argc, char **argv)
{
	handle_args(argc, argv);
	init();
	event_base_dispatch(evbase);
	return 0;
}
