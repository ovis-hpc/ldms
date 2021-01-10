/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2019-2020 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2019-2020 Open Grid Computing, Inc. All rights reserved.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <getopt.h>
#include <semaphore.h>
#include <ovis_util/util.h>
#include <unistd.h>
#include "ldms.h"
#include "ldmsd_stream.h"

#define STRLEN 8096
static ldms_t ldms;
static sem_t conn_sem;
static int conn_status;
static sem_t recv_sem;

static struct option long_opts[] = {
	{"host",       required_argument, 0,  'h' },
	{"port",       required_argument, 0,  'p' },
	{"stream",     required_argument, 0,  's' },
	{"xprt",       required_argument, 0,  'x' },
	{"auth",       required_argument, 0,  'a' },
	{"auth_arg",   required_argument, 0,  'A' },
	{"filename", optional_argument, 0,  'f' },
	{"type",       required_argument, 0,  't' },
	{0,            0,                 0,  0 }
};

void usage(int argc, char **argv)
{
	printf("usage: %s -x <xprt> -h <host> -p <port> -s <stream-name>\n"
	       "	-a <auth> -A <auth-opt>\n"
	       "	-t <data-format>\n\n"
	       "	<data-format>	str | json (default is str)\n"
	       "        [-f <filename> (default is stdin)]\n",
	       argv[0]);
	exit(1);
}

static const char *short_opts = "h:p:s:x:a:A:f:t:";

#define AUTH_OPT_MAX 128

int server_rc;
static void event_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg)
{
	switch (e->type) {
	case LDMS_XPRT_EVENT_CONNECTED:
		sem_post(&conn_sem);
		conn_status = 0;
		break;
	case LDMS_XPRT_EVENT_REJECTED:
		ldms_xprt_put(x);
		conn_status = ECONNREFUSED;
		break;
	case LDMS_XPRT_EVENT_DISCONNECTED:
		ldms_xprt_put(x);
		conn_status = ENOTCONN;
		break;
	case LDMS_XPRT_EVENT_ERROR:
		conn_status = ECONNREFUSED;
		break;
	case LDMS_XPRT_EVENT_RECV:
		sem_post(&recv_sem);
		server_rc = ldmsd_stream_response(e);
		break;
	default:
		printf("Received invalid event type %d\n", e->type);
	}
}

struct timespec ts;
int to;
ldms_t setup_connection(const char *xprt, const char *host,
			const char *port, const char *auth)
{
	char hostname[PATH_MAX];
	const char *timeout = "5";
	int rc;

	if (!host) {
		if (0 == gethostname(hostname, sizeof(hostname)))
			host = hostname;
	}
	if (!timeout) {
		ts.tv_sec = time(NULL) + 5;
		ts.tv_nsec = 0;
	} else {
		to = atoi(timeout);
		if (to <= 0)
			to = 5;
		ts.tv_sec = time(NULL) + to;
		ts.tv_nsec = 0;
	}
	ldms = ldms_xprt_new_with_auth(xprt, NULL, auth, NULL);
	if (!ldms) {
		printf("Error %d creating the '%s' transport\n",
		       errno, xprt);
		return NULL;
	}

	sem_init(&recv_sem, 1, 0);
	sem_init(&conn_sem, 1, 0);

	rc = ldms_xprt_connect_by_name(ldms, host, port, event_cb, NULL);
	if (rc) {
		printf("Error %d connecting to %s:%s\n",
		       rc, host, port);
		return NULL;
	}
	sem_timedwait(&conn_sem, &ts);
	if (conn_status)
		return NULL;
	return ldms;
}

int main(int argc, char **argv)
{
	char *host = NULL;
	char *port = NULL;
	char *xprt = "sock";
	FILE *fd = stdin;
	char buf[STRLEN];
	char *fname = NULL;
	char *fmt = "str";
	ldmsd_stream_type_t type;
	char *stream = "hello_stream/hello";
	int opt, opt_idx;
	char *lval, *rval;
	char *auth = "none";
	struct attr_value_list *auth_opt = NULL;
	const int auth_opt_max = AUTH_OPT_MAX;
	int rc;

	auth_opt = av_new(auth_opt_max);
	if (!auth_opt) {
		perror("could not allocate auth options");
		exit(1);
	}

	while ((opt = getopt_long(argc, argv,
				  short_opts, long_opts,
				  &opt_idx)) > 0) {
		switch (opt) {
		case 'h':
			host = strdup(optarg);
			break;
		case 'p':
			port = strdup(optarg);
			break;
		case 'x':
			xprt = strdup(optarg);
			break;
		case 'a':
			auth = strdup(optarg);
			break;
		case 'A':
			lval = strtok(optarg, "=");
			rval = strtok(NULL, "");
			if (!lval || !rval) {
				printf("ERROR: Expecting -A name=value");
				exit(1);
			}
			if (auth_opt->count == auth_opt->size) {
				printf("ERROR: Too many auth options");
				exit(1);
			}
			auth_opt->list[auth_opt->count].name = lval;
			auth_opt->list[auth_opt->count].value = rval;
			auth_opt->count++;
			break;
		case 'f':
			fname = strdup(optarg);
			break;
		case 's':
			stream = strdup(optarg);
			break;
		case 't':
			fmt = strdup(optarg);
			break;
		default:
			usage(argc, argv);
		}
	}
	if (0 == strcmp(fmt, "str")) {
		type = LDMSD_STREAM_STRING;
	} else if (0 == strcmp(fmt, "json")) {
		type = LDMSD_STREAM_JSON;
	} else {
		printf("%s is an invalid data format\n", fmt);
		usage(argc, argv);
	}
	if (!host || !port)
		usage(argc, argv);

	if (fname){
		//read from a file, otherwise read from stdin
		fd = fopen(fname, "r");
		if (!fd){
			printf("Cannot open file '%s'\n", fname);
			usage(argc,argv);
		}
	}
	ldms_t ldms = setup_connection(xprt, host, port, auth);
	if (!ldms){
		printf("Error setting up connection -- exiting\n");
		exit(1);
	}


	while (fgets(buf, STRLEN, fd) != NULL){
		rc = ldmsd_stream_publish(ldms, stream, type, buf, strlen(buf) + 1);
		if (rc)
			printf("Error %d publishing data.\n", rc);
	}


	printf("Done\n");
	return server_rc;
}
