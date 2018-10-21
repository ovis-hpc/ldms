/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2010-2018 Open Grid Computing, Inc. All rights reserved.
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

#ifndef _KVD_SVC_H_
#define _KVD_SVC_H_

#include <limits.h>

#include <coll/htbl.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/thread.h>
#include <event2/http.h>
#include <event2/http_struct.h>
#include <event2/keyvalq_struct.h>

#define KVD_KVC_ERR_LEN 1024
struct kvd_req_ctxt {
	struct evhttp_request *req;
	const struct evhttp_uri *uri;
	const char *query;
	struct evkeyvalq *params;
	int httprc;
	struct evbuffer *evbuffer;
	struct evkeyvalq *hdr;
	char errstr[KVD_KVC_ERR_LEN];
};

typedef void (*kvd_svc_fn_t)(struct kvd_req_ctxt *ctxt);

typedef struct kvd_svc_handler_s {
	kvd_svc_fn_t svc_fn;
	struct hent hent;
} *kvd_svc_handler_t;

typedef struct kvd_svc_s {
	char path_buff[PATH_MAX];
	char *log_path;
	char *port;
	char *address;
	char *verbosity;

	struct event_base *evbase;
	struct evhttp *evhttp;
	struct evhttp_bound_socket *evhttp_socket; /* passive */
	struct evhttp_connection *evhttp_conn;	   /* active */

	htbl_t dispatch_table;
} *kvd_svc_t;

void kvd_set_svc_handler(kvd_svc_t svc, const char *uri, kvd_svc_fn_t fn);
void kvd_svc_init(kvd_svc_t svc, int foreground);
void kvd_cli_init(kvd_svc_t svc, int foreground);
int kvd_cli_get(kvd_svc_t svc, char *kind, char *uri);
int kvd_cli_post(kvd_svc_t svc, char *uri, char *data, size_t data_len);
#endif
