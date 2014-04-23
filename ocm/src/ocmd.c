/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013 Sandia Corporation. All rights reserved.
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

#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <getopt.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <semaphore.h>
#include <dlfcn.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netdb.h>
#include <limits.h>

#include <sys/queue.h>

#include "ocmd_plugin.h"
#include "ovis_util/util.h"

#include "ocm.h"

void ocmd_log(const char *fmt, ...);

int foreground = 0;
uint16_t port = OCM_DEFAULT_PORT;
const char *xprt = "sock";
const char *peer_list_path = NULL;
const char *log_path = NULL;
const char *plugin_name = NULL;
struct ocmd_plugin *plugin = NULL;

struct req_entry {
	char *key;
	struct ocm_event *ocm_ev;
	TAILQ_ENTRY(req_entry) entry;
};

pthread_mutex_t req_mutex = PTHREAD_MUTEX_INITIALIZER;
TAILQ_HEAD(req_queue, req_entry) req_queue = TAILQ_HEAD_INITIALIZER(req_queue);
sem_t req_sem;

pthread_t *resp_threads; /**< Request responding threads */
int resp_threads_count = 2;

ocm_t ocm = NULL;

void ocmd_log(const char *fmt, ...)
{
	char tstr[32];
	time_t t = time(NULL);
	struct tm tm;
	localtime_r(&t, &tm);
	strftime(tstr, 32, "%a %b %d %T %Y", &tm);
	printf("%s ", tstr);
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	fflush(stdout);
}

const char *short_opt = "x:p:l:t:FR:P:h?";
struct option long_opt[] = {
	{"xprt",        1,  0,  'x'},
	{"port",        1,  0,  'p'},
	{"log",         1,  0,  'l'},
	{"thread",      1,  0,  't'},
	{"foreground",  0,  0,  'F'},
	{"receiver",    1,  0,  'R'},
	{"plugin_name",      1,  0,  'P'},
	{"help",        0,  0,  'h'},
	{0,             0,  0,  0}
};

void print_usage()
{
	printf(
"Usage: ocmd [OCMD_OPTIONS] [ -- PLUGIN_OPTIONS ]\n"
"\n"
"OCMD_OPTIONS:\n"
"	-F,--foreground		Run in foreground mode.\n"
"	-x,--xprt <XPRT>	Specify zap transport (default: sock).\n"
"	-p,--port <PORT>	Listening port number (default: 12345).\n"
"	-l,--log <PATH>		Path to the log file. If not specified STDOUT\n"
"				will be used.\n"
"	-t,--thread <NUMBER>	The number of request processing threads.\n"
"				(default: 2).\n"
"	-R,--receiver <PATH>	Path to the receiver peer list file. If unset,\n"
"				no peer is added to the configuration receiver\n"
"				list.\n"
"	-P,--plugin_name <NAME>	OCMD Plugin name.\n"
"\n"
"The PLUGIN_OPTIONS (after -- ) are expected to be in attr=value format.\n"
"\n"
	);
}

struct attr_value_list *avl;

void handle_args(int argc, char **argv)
{
	char c;
loop:
	c = getopt_long(argc, argv, short_opt, long_opt, NULL);
	switch (c) {
	case 'F':
		foreground = 1;
		break;
	case 'p':
		port = atoi(optarg);
		break;
	case 'x':
		xprt = optarg;
		break;
	case 'l':
		log_path = optarg;
		break;
	case 't':
		resp_threads_count = atoi(optarg);
		break;
	case 'R':
		peer_list_path = optarg;
		break;
	case 'P':
		plugin_name = optarg;
		break;
	case -1:
		/* no more ocmd args */
		goto plugin_args;
	case 'h':
	case '?':
	default:
		print_usage();
		_exit(-1);
	}

	goto loop;

plugin_args:
	avl = av_new(128);
	if (!avl) {
		ocmd_log("av_new failed\n");
		_exit(-1);
	}

	int i;
	int idx = optind;
	for (i = 0, idx = optind; idx < argc; i++, idx++) {
		avl->list[i].name = strtok(argv[idx], "=");
		avl->list[i].value = strtok(NULL, "=");
	}
	avl->count = i;
}

struct req_entry *req_entry_new(struct ocm_event *e)
{
	struct req_entry *req = calloc(1, sizeof(*req));
	if (!req)
		goto err0;
	/* NOTE: Event data is available only in the callback path. Because
	 * req_entry will be processed outside of the callback path, we have to
	 * copy the key out.
	 *
	 * The event itself will be available until either ocm_event_resp_err()
	 * or ocm_event_resp_cfg() is called.
	 */
	req->key = strdup(ocm_cfg_req_key(e->req));
	if (!req->key)
		goto err1;
	req->ocm_ev = e;
	return req;
err1:
	free(req);
err0:
	return NULL;
}

void req_entry_free(struct req_entry *req)
{
	free(req->key);
	free(req);
}

void handle_cfg_request(struct ocm_event *e)
{
	struct req_entry *req = req_entry_new(e);
	if (!req) {
		ocmd_log("ERROR: ENOMEM at %s:%d:%s()", __FILE__, __LINE__,
							__func__);
		return;
	}
	/* post the request */
	pthread_mutex_lock(&req_mutex);
	TAILQ_INSERT_TAIL(&req_queue, req, entry);
	pthread_mutex_unlock(&req_mutex);
	sem_post(&req_sem);
}

int ocmd_ocm_cb(struct ocm_event *e)
{
	switch (e->type) {
	case OCM_EVENT_CFG_REQUESTED:
		handle_cfg_request(e);
		break;
	case OCM_EVENT_CFG_RECEIVED:
		/* Just in case of configuration update, but just
		 * do nothing for now */
		ocmd_log("CFG_RECEIVED, do nothing.\n");
		break;
	case OCM_EVENT_ERROR:
		break;
	default:
		ocmd_log("Receiving corrupted ocm_event type: %d\n", e->type);
	}
}

void process_request(struct req_entry *req, struct ocm_cfg_buff *buff)
{
	/* Process request here, but just response error for now */
#ifdef DEBUG
	ocmd_log("recv key: %s\n", req->key);
#endif
	ocm_cfg_buff_reset(buff, req->key);
	int rc = plugin->get_config(plugin, req->key, buff);
	if (rc) {
		ocm_event_resp_err(req->ocm_ev, rc, req->key,
					"get_config failed");
	} else {
		ocm_event_resp_cfg(req->ocm_ev, buff->buff);
	}
	req_entry_free(req);
}

void *resp_thread_proc(void *arg)
{
	struct ocm_cfg_buff *buff = ocm_cfg_buff_new(16*1024*1024, "NA");
	if (!buff) {
		ocmd_log("ocm_cfg_buff_new failed\n");
		return NULL;
	}
loop:
	sem_wait(&req_sem);
	pthread_mutex_lock(&req_mutex);
	struct req_entry *req = TAILQ_FIRST(&req_queue);
	TAILQ_REMOVE(&req_queue, req, entry);
	pthread_mutex_unlock(&req_mutex);

	process_request(req, buff);

	goto loop;
}

void ocmd_cleanup(int x)
{
	ocmd_log("ocm Daemon exiting ... status %d\n", x);
	exit(x);
}

void ocmd_init()
{
	int rc;

	struct sigaction cleanup_act;
	cleanup_act.sa_handler = ocmd_cleanup;
	cleanup_act.sa_flags = 0;

	sigaction(SIGHUP, &cleanup_act, NULL);
	sigaction(SIGINT, &cleanup_act, NULL);
	sigaction(SIGTERM, &cleanup_act, NULL);

	if (!foreground) {
		rc = daemon(1, 1);
		if (rc) {
			perror("daemon");
			_exit(-1);
		}
	}

	if (log_path) {
		int fd = open(log_path, O_WRONLY|O_APPEND|O_CREAT, 0644);
		if (fd < 0) {
			perror("ERROR: Cannot open log file, ");
			_exit(-1);
		}
		if (dup2(fd, 1) < 0) {
			perror("ERROR: Cannot redirect STDOUT to log file, ");
			_exit(-1);
		}
		if (dup2(fd, 2) < 0) {
			perror("ERROR: Cannot redirect STDERR to log file, ");
			_exit(-1);
		}
	}


	if (plugin_name) {
		struct ocmd_plugin* (*plugin_create)(
					void (*log_fn)(const char *fmt, ...),
					struct attr_value_list *avl);
		char soname[128];
		sprintf(soname, "lib%s.so", plugin_name);
		void *p = dlopen(soname, RTLD_NOW);
		if (!p) {
			ocmd_log("%s\n", dlerror());
			_exit(-1);
		}
		dlerror();
		*(void**) (&plugin_create) = dlsym(p, "ocmd_plugin_create");
		char *err = dlerror();
		if (err != NULL) {
			ocmd_log("%s\n", err);
			_exit(-1);
		}
		plugin = plugin_create(ocmd_log, avl);
		if (!plugin) {
			ocmd_log("plugin_create failed.\n");
			_exit(-1);
		}
	}

	sem_init(&req_sem, 0, 0);

	ocm = ocm_create(xprt, port, ocmd_ocm_cb, ocmd_log);
	if (!ocm) {
		ocmd_log("ERROR: Cannot create ocm handle\n");
		_exit(-1);
	}

	if (ocm_enable(ocm)) {
		ocmd_log("ERROR: Cannot enable ocm.\n");
		_exit(-1);
	}

	/* main thread is also a thread for resp_thread_proc */
	int _N = resp_threads_count - 1;
	resp_threads = calloc(_N, sizeof(pthread_t));
	int i;
	for (i = 0; i < _N; i++) {
		pthread_create(&resp_threads[i], NULL, resp_thread_proc,
				&resp_threads[i]);
	}
}

void ocmd_add_peers()
{
	if (!peer_list_path)
		return;
	FILE *f = fopen(peer_list_path, "r");
	if (!f) {
		ocmd_log("ERROR: Cannot open file %s: %m\n", peer_list_path);
		_exit(-1);
	}
	char *sline;
	char line[1024];
	char host[128];
	char port[16];
	char resolved[HOST_NAME_MAX];
	int n, rc;
	struct addrinfo *ai;
	char * comment;
	struct addrinfo hints;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET;    /* Allow IPv4 only */
	hints.ai_socktype = 0;
	hints.ai_flags = (AI_V4MAPPED | AI_ADDRCONFIG);
	hints.ai_protocol = 0;
	hints.ai_canonname = NULL;
	hints.ai_addr = NULL;
	hints.ai_next = NULL;
	while (1) {
		sline = fgets(line, 1024, f);
		if (!sline)
			break;
		comment = strchr(line, '#');
		if (comment) /* ignore anything after a comment */
			*comment ='\0';
		n = sscanf(line, " %[^:]:%[0-9]", host, port);
		if (n < 2)
			continue;
		rc = getaddrinfo(host, port, &hints, &ai);
		if (rc) {
			ocmd_log("ERROR: getaddrinfo(%d) -- %s. host: %s\n", rc, gai_strerror(rc), host);
			_exit(-1);
		}
		while(ai) {
			if (ai->ai_family == AF_INET) {
				if (NULL == inet_ntop(ai->ai_family,
					&((struct sockaddr_in *)ai->ai_addr)->sin_addr,
					resolved, HOST_NAME_MAX)) {
					ocmd_log("ERROR: resolving host %s:%s\n", host);
					perror("inet_ntop");
				}
				else if (ocm_add_receiver(ocm, ai->ai_addr, ai->ai_addrlen)==0) {
#ifdef DEBUG
					ocmd_log("Added reciever %s:%s\n", resolved,port);
#endif // DEBUG
					break; /* successs */
				}
			}
			else {
				ocmd_log("ignoring host %s\n", resolved);
			}
			ai = ai->ai_next;
		}
		freeaddrinfo(ai);
	}
	fclose(f);
}

int main(int argc, char** argv)
{
	int rc;
	handle_args(argc, argv);
	ocmd_init();

	ocmd_log("ocm Daemon started.\n");

	ocmd_add_peers();

	/* use main thread for request processing too */
	resp_thread_proc(0);
	return 0;
}
