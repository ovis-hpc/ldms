/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2021 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2021 Open Grid Computing, Inc. All rights reserved.
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
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <pwd.h>
#include <strings.h>
#include <string.h>
#include <pwd.h>
#include <time.h>
#include <linux/limits.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <semaphore.h>
#include <pthread.h>
#include "ldms_sps.h"
#include <ovis_json/ovis_json.h>
#include <assert.h>
#include <ovis_json/ovis_json.h>
#include "ldmsd_stream.h"

#define LNOTIFY_RETRY 600 /* 10 minutes. */
#define LNOTIFY_AUTH "munge"
#define LNOTIFY_XPRT "sock"

#define LDBG 1
#define LERR 4
#define DEBUGC(O, K, FMT, ...) \
	if (O->log && (K > LDBG || O->verbose)) O->log(K, "(%d) %s:%d " FMT, getpid(), __func__, __LINE__, ##__VA_ARGS__)
#define DEBUGL(K, FMT, ...) \
	if (l->log && (K > LDBG || l->verbose)) l->log(K, "(%d) %s:%d " FMT, getpid(), __func__, __LINE__, ##__VA_ARGS__)

struct sps_target {
	char xprt[16];
	char host[64];
	char port[16];
	char auth[16];
	int retry;
	time_t next_try;
	ldms_t ldms;
	pthread_cond_t wait_cond;
	pthread_mutex_t wait_lock;
	int state;
	int debug_ack;
	int verbose;
	int last_publish_rc;
	ldms_sps_msg_log_f log;
	LIST_ENTRY(sps_target) entry;
};

/* #define LNDEBUG */

/*
 * This is a library to send 'events' to LDMSD plugins.
 *
 * Events are JSon formatted objects sent over the LDMS transport
 * to a set of plugins configured to receive them.
 *
 * Events can have any syntax.
 */


#define IDLE		0 /* not opened or failed */
#define CONNECTING	1
#define CONNECTED	2
#define ACKED		3
#define DISCONNECTED	4
#define FAILED		5
const char *state_name[] = {
	"IDLE",
	"CONNECTING",
	"CONNECTED",
	"ACKED",
	"DISCONNECTED",
	"FAILED"
};

static int client_state_set(int newstate, struct sps_target *client)
{
	DEBUGC(client, LDBG, "client->state = %s\n", state_name[newstate]);
	client->state = newstate;
	return newstate;
}

static void event_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg)
{
	(void)x;
	struct sps_target *client = cb_arg;
#ifdef LNDEBUG
	const char *event;
#endif
	if (!client->ldms)
		return;
	pthread_mutex_lock(&client->wait_lock);
	switch (e->type) {
	case LDMS_XPRT_EVENT_CONNECTED:
		client_state_set(CONNECTED, client);
#ifdef LNDEBUG
		event = "connected";
#endif
		break;
	case LDMS_XPRT_EVENT_RECV:
#ifdef LNDEBUG
		event = "recv";
#endif
		if (client->debug_ack) {
			client_state_set(ACKED, client);
		}
		break;
	case LDMS_XPRT_EVENT_REJECTED:
		client_state_set(DISCONNECTED, client);
#ifdef LNDEBUG
		event = "rejected";
#endif
		break;
	case LDMS_XPRT_EVENT_DISCONNECTED:
		client_state_set(DISCONNECTED, client);
#ifdef LNDEBUG
		event = "disconnected";
#endif
		break;
	case LDMS_XPRT_EVENT_ERROR:
		client_state_set(DISCONNECTED, client);
#ifdef LNDEBUG
		event = "error";
#endif
		break;
	default:
#ifdef LNDEBUG
		event = "INVALID_EVENT";
#endif
		DEBUGC(client, LERR, "Received invalid event type\n");
	}
	pthread_cond_signal(&client->wait_cond);
	pthread_mutex_unlock(&client->wait_lock);
#ifdef LNDEBUG
	DEBUGL(LDBG, "Event %s received for client xprt=%s host=%s port=%s auth=%s\n",
		event, client->xprt, client->host, client->port, client->auth);
#endif
}

static char *get_arg_value(const char *arg)
{
	char *s = strstr(arg, "=");
	if (s) {
		s++;
		return s;
	}
	return NULL;
}


/*
 * :::: is sock:localhost:411:munge:10
 */
static
void add_client(struct ldms_sps *l, const char *spec)
{
	if (!l || !spec || strlen(spec) == 0)
		return;
	const char *r;
	unsigned int i;
	struct sps_target *client;
	struct sps_target_list *cl = l->cl;

	client = calloc(1, sizeof(*client));
	if (!client)
		goto err;
	/* Transport */
	r = spec;
	for (i = 0; *r != '\0' && *r != ':' && i < sizeof(client->xprt); i++)
		client->xprt[i] = *r++;
	if (i == 0)
		strcpy(client->xprt, LNOTIFY_XPRT);
	if (*r == ':')
		r++;
	/* Host */
	for (i = 0; *r != '\0' && *r != ':' && i < sizeof(client->host); i++)
		client->host[i] = *r++;
	if (i == 0)
		strcpy(client->host, "localhost");
	if (*r == ':')
		r++;
	/* Port */
	for (i = 0; *r != '\0' && *r != ':' && i < sizeof(client->port); i++)
		client->port[i] = *r++;
	if (i == 0)
		strcpy(client->port, "411");
	if (*r == ':')
		r++;
	/* Auth */
	for (i = 0; *r != '\0' && *r != ':' && i < sizeof(client->auth); i++)
		client->auth[i] = *r++;
	if (i == 0)
		strcpy(client->auth, LNOTIFY_AUTH);
	if (*r == ':')
		r++;
	/* retry */
	char retry[32] = { '\0' };
	char *end = NULL;
	for (i = 0; *r != '\0' && *r != ':' && i < sizeof(retry); i++)
		retry[i] = *r++;
	if (i == 0) {
		client->retry = LNOTIFY_RETRY;
	} else {
		client->retry = strtoul(retry, &end, 0);
		if (! (retry[0] != '\0' && end[0] == '\0')) {
			errno = EINVAL;
			goto err;
		}
	}

	client->debug_ack = l->debug_ack;
	client->log = l->log;
	client->verbose = l->verbose;
	client_state_set(IDLE, client);
	client->next_try = time(NULL) - 1;
	pthread_mutex_init(&client->wait_lock, NULL);
	pthread_cond_init(&client->wait_cond, NULL);
	LIST_INSERT_HEAD(cl, client, entry);
	DEBUGL(LDBG, "client xprt=%s host=%s port=%s auth=%s retry=%d\n",
		   client->xprt, client->host, client->port, client->auth, client->retry);
	l->target_count++;
	return;
 err:
	if (client)
		free(client);
	DEBUGL(LERR, "Memory allocation failure.\n");
}

#define DEFAULT_ARGS "::::"

struct ldms_sps *ldms_sps_create(int argc, const char *argv[], ldms_sps_msg_log_f log)
{
	const char *timeout = NULL;
	char *stream = NULL;
	int rc;
	struct ldms_sps *l = calloc(1, sizeof(*l));
	if (!l) {
		errno = ENOMEM;
		return NULL;
	}
	l->cl = &l->sps_target_list;
	l->log = log;
	pthread_mutex_init(&l->list_lock, NULL);

	for (rc = 0; rc < argc; rc++) {
		if (0 == strncasecmp(argv[rc], "client", 6)) {
			add_client(l, get_arg_value(argv[rc]));
		}
		if (0 == strncasecmp(argv[rc], "stream", 6)) {
			stream = get_arg_value(argv[rc]);
			continue;
		}
		if (0 == strncasecmp(argv[rc], "timeout", 7)) {
			timeout = get_arg_value(argv[rc]);
			continue;
		}
		if (0 == strncasecmp(argv[rc], "debug_ack", 9)) {
			l->debug_ack = 1;
			continue;
		}
		if (0 == strncasecmp(argv[rc], "verbose", 7)) {
			l->verbose = 1;
			continue;
		}
	}
	if (LIST_EMPTY(l->cl)) {
		add_client(l, DEFAULT_ARGS);
	}
	if (!stream)
		stream = "slurm";
	if (!timeout) {
		l->io_timeout = LNOTIFY_TIMEOUT;
	} else {
		char *end = NULL;
		l->io_timeout = strtoul(timeout, &end, 0);
		if (! (timeout[0] != '\0' && end[0] == '\0')) {
			errno = EINVAL;
			goto err;
		}
	}
	DEBUGL(LDBG, "timeout %s io_timeout %ld\n", timeout, l->io_timeout);
	DEBUGL(LDBG, "stream %s\n", stream);
	l->stream = strdup(stream);
	if (!l->stream) {
		errno = ENOMEM;
		goto err;
	}
	return l;
err:
	ldms_sps_destroy(l);
	return NULL;
}

struct ldms_sps *ldms_sps_create_1(const char *stream, const char *xprt, const char *host, int port, const char *auth, int retry, int timeout, ldms_sps_msg_log_f log, int flags)
{
	const char *argv[10];
	size_t i = 0;
	if (stream) {
		argv[i] = stream;
		i++;
	}
	if (flags & LN_FLAG_DEBUG_ACK) {
		argv[i] = "debug_ack=";
		i++;
	}
	if (flags & LN_FLAG_VERBOSE) {
		argv[i] = "verbose=";
		i++;
	}
	char tbuf[32];
	snprintf(tbuf, sizeof(tbuf), "timeout=%d", timeout);
	if (timeout) {
		argv[i] = tbuf;
		i++;
	}
	char client[8+16+64+6+32+20];
	snprintf(client, sizeof(client), "client=%s:%s:%d:%s:%d",
		xprt ? xprt : LNOTIFY_XPRT,
		host ? host : "localhost",
		port,
		auth ? auth : LNOTIFY_AUTH,
		retry >= 0 ? retry : LNOTIFY_RETRY);
	argv[i] = client;
	i++;
	return ldms_sps_create(i, argv, log);
}


/* it is assumed the caller has a wait_lock on client.
 * close/destroy connection and set next_try time from retry.
 */
static void client_reset(struct sps_target *client)
{
	if (!client)
		return;
	if (client->ldms) {
		DEBUGC(client, LDBG, "RESETTING client xprt=%s host=%s "
			"port=%s auth=%s retry=%d\n", client->xprt, client->host,
			client->port, client->auth, client->retry);
		ldms_xprt_put(client->ldms);
		client->ldms = NULL;
	}
	client_state_set(IDLE, client);
	client->next_try = time(NULL) + client->retry;
}

int ldms_sps_target_count_get(struct ldms_sps *l)
{
	if (l)
		return l->target_count;
	return 0;
}


static int update_clients(struct ldms_sps *l)
{
	int rc;

	if (LIST_EMPTY(l->cl))
		return ENOTCONN;
	struct sps_target *client;
	time_t now = time(NULL);
	LIST_FOREACH(client, l->cl, entry) {
		pthread_mutex_lock(&client->wait_lock);
		if (client->state == DISCONNECTED)
			client_reset(client);
		if (client->state == IDLE && client->ldms == NULL &&
			now > client->next_try ) {
			client->ldms = ldms_xprt_new_with_auth(client->xprt,
					(ldms_log_fn_t)printf, client->auth, NULL);
			if (!client->ldms) {
				DEBUGL(LERR, "ERROR %d creating the '%s' transport\n",
					     errno, client->xprt);
				client->next_try = now + client->retry;
				goto next_client;
			}
			/* Attempt to connect to each client every retry seconds. */
			client_state_set(CONNECTING, client);
			rc = ldms_xprt_connect_by_name(client->ldms, client->host,
						       client->port, event_cb, client);
			if (rc) {
				DEBUGL(LERR, "Synchronous error %d connecting to %s:%s\n",
					rc, client->host, client->port);
			}
		}
	next_client:
		pthread_mutex_unlock(&client->wait_lock);
	}

	/*
	 * Wait for the connections to complete and reset clients who
	 * failed to connect
	 */
	struct timespec wait_ts;
	wait_ts.tv_sec = time(NULL) + l->io_timeout;
	wait_ts.tv_nsec = 0;
	LIST_FOREACH(client, l->cl, entry) {
		pthread_mutex_lock(&client->wait_lock);
		if (client->state == CONNECTING) {
			rc = 0;
			while (client->state == CONNECTING && rc == 0)
				rc = pthread_cond_timedwait(&client->wait_cond,
					&client->wait_lock, &wait_ts);
			if (rc == ETIMEDOUT) {
				DEBUGL(LDBG, "CONNECTING timed out.\n");
			}
			if (client->state != CONNECTED) {
				DEBUGL(LDBG, "DELAY state=%s connecting to %s:%s\n",
					state_name[client->state],
					client->host, client->port);
				client_reset(client);
			}
		}
		pthread_mutex_unlock(&client->wait_lock);
	}
	return 0;

}

int ldms_sps_destroy(struct ldms_sps *l)
{
	struct sps_target *client;
	struct sps_target_list *client_list = l->cl;
	int rc;
	pthread_mutex_lock(&l->list_lock);
	/*
	 * Disconnect client
	 */
	LIST_FOREACH(client, client_list, entry) {
		pthread_mutex_lock(&client->wait_lock);
		if (client->state < DISCONNECTED && client->ldms) {
			DEBUGL(LDBG, "CLOSING client xprt=%s host=%s "
				"port=%s auth=%s\n", client->xprt, client->host,
				client->port, client->auth);
			ldms_xprt_close(client->ldms);
		}
		pthread_mutex_unlock(&client->wait_lock);
	}
	/*
	 * Wait for close complete
	 */
	struct timespec wait_ts;
	wait_ts.tv_sec = time(NULL) + l->io_timeout;
	wait_ts.tv_nsec = 0;
	LIST_FOREACH(client, client_list, entry) {
		pthread_mutex_lock(&client->wait_lock);
		if (client->state < DISCONNECTED && client->ldms) {
			DEBUGL(LDBG, "CLOSE WAIT for client %s:%s\n",
				client->host, client->port);
			rc = 0;
			while (client->state < DISCONNECTED && client->ldms && rc == 0)
				rc = pthread_cond_timedwait(&client->wait_cond,
						&client->wait_lock, &wait_ts);
			if (rc == ETIMEDOUT) {
				DEBUGL(LDBG, "CLOSE timed out.\n");
			}
			client_reset(client);
		}
		pthread_mutex_unlock(&client->wait_lock);
		pthread_mutex_destroy(&client->wait_lock);
	}
	while (!LIST_EMPTY(l->cl)) {
		client = LIST_FIRST(l->cl);
		LIST_REMOVE(client, entry);
		free(client);
	}
	pthread_mutex_unlock(&l->list_lock);
	free(l->stream);
	pthread_mutex_destroy(&l->list_lock);
	free(l);
	return 0;
}

struct ldms_sps_send_result ldms_sps_send_event(struct ldms_sps *l, jbuf_t jb)
{
	struct sps_target *client;
	struct sps_target_list *client_list = l->cl;
	struct timespec wait_ts;
	struct ldms_sps_send_result result = LN_NULL_RESULT;

	if (!l || !jb) {
		result.rc = EINVAL;
		return result;
	}

	pthread_mutex_lock(&l->list_lock);
	/* retry clients that are missing and beyond next_try wait. */
	update_clients(l);

	/*
	 * Publish event to connected clents
	 */
	wait_ts.tv_sec = time(NULL) + l->io_timeout;
	wait_ts.tv_nsec = 0;
	LIST_FOREACH(client, client_list, entry) {
		pthread_mutex_lock(&client->wait_lock);
		if (client->state == CONNECTED || client->state == ACKED) {
			client_state_set(CONNECTED, client);
			DEBUGL(LDBG, "publishing to %s:%s\n", client->host, client->port);
			DEBUGL(LDBG, "slurm %s:%d: %s\n", __func__, __LINE__, jb->buf);
			client->last_publish_rc = ldmsd_stream_publish(
				client->ldms, l->stream, LDMSD_STREAM_JSON,
				jb->buf, jb->cursor + 1);
/*
			client->last_publish_rc = 0;
*/
			if (client->last_publish_rc) {
				DEBUGL(LDBG, "Problem %d publishing json to %s:%s\n",
					client->last_publish_rc, client->host, client->port);
				client_reset(client);
			} else {
				result.publish_count++;
				DEBUGL(LDBG, "slurm %s: success\n", __func__);
			}
		}
		pthread_mutex_unlock(&client->wait_lock);
	}

	if (l->debug_ack) {
		/*
		 * Wait for the event to be acknowledged by the client before
		 * disconnecting. do i need this since we keep connection open?
		 * We have to consume an ack here since it changes state from connected.
		 */
		wait_ts.tv_sec = time(NULL) + l->io_timeout;
		wait_ts.tv_nsec = 0;
		LIST_FOREACH(client, client_list, entry) {
			if (client->last_publish_rc != 0)
				continue;
			pthread_mutex_lock(&client->wait_lock);
			int rc = 0;
			while (client->state == CONNECTED && rc == 0)
				rc = pthread_cond_timedwait(&client->wait_cond,
					&client->wait_lock, &wait_ts);
			if (client->state == ACKED) {
				DEBUGL(LDBG, "ACKED %s:%s\n", client->host, client->port);
				result.ack_count++;
				client_state_set(CONNECTED, client);
			} else {
				DEBUGL(LDBG, "ACK TIMEOUT state=%s %s:%s\n",
					state_name[client->state], client->host, client->port);
			}
			pthread_mutex_unlock(&client->wait_lock);
		}
	}

	pthread_mutex_unlock(&l->list_lock);
	return result;
}


struct ldms_sps_send_result ldms_sps_send_string(struct ldms_sps *l, size_t buf_len, const char *buf)
{
	struct sps_target *client;
	struct sps_target_list *client_list = l->cl;
	struct timespec wait_ts;
	struct ldms_sps_send_result result = { 0, 0, 0};

	if (!l || !buf) {
		result.rc = EINVAL;
		return result;
	}
	if (!buf_len)
		return result;

	pthread_mutex_lock(&l->list_lock);
	/* retry clients that are missing and beyond next_try wait. */
	update_clients(l);

	/*
	 * Publish event to connected clents
	 */
	wait_ts.tv_sec = time(NULL) + l->io_timeout;
	wait_ts.tv_nsec = 0;
	LIST_FOREACH(client, client_list, entry) {
		pthread_mutex_lock(&client->wait_lock);
		if (client->state == CONNECTED || client->state == ACKED) {
			client_state_set(CONNECTED, client);
			DEBUGL(LDBG, "publishing to %s:%s\n", client->host, client->port);
			client->last_publish_rc = ldmsd_stream_publish(
				client->ldms, l->stream, LDMSD_STREAM_STRING,
				buf, buf_len);
			client->last_publish_rc = 0;
			if (client->last_publish_rc) {
				DEBUGL(LDBG, "Problem %d publishing buf to %s:%s\n",
					client->last_publish_rc, client->host, client->port);
				client_reset(client);
			} else {
				result.publish_count++;
			}
		}
		pthread_mutex_unlock(&client->wait_lock);
	}

	if (l->debug_ack) {
		/*
		 * Wait for the event to be acknowledged by the client before
		 * disconnecting. do i need this since we keep connection open?
		 * We have to consume an ack here since it changes state from connected.
		 */
		wait_ts.tv_sec = time(NULL) + l->io_timeout;
		wait_ts.tv_nsec = 0;
		LIST_FOREACH(client, client_list, entry) {
			if (client->last_publish_rc != 0)
				continue;
			pthread_mutex_lock(&client->wait_lock);
			int rc = 0;
			while (client->state == CONNECTED && rc == 0)
				rc = pthread_cond_timedwait(&client->wait_cond,
					&client->wait_lock, &wait_ts);
			if (client->state == ACKED) {
				DEBUGL(LDBG, "ACKED %s:%s\n", client->host, client->port);
				result.ack_count++;
				client_state_set(CONNECTED, client);
			} else {
				DEBUGL(LDBG, "ACK TIMEOUT state=%s %s:%s\n",
					state_name[client->state], client->host, client->port);
			}
			pthread_mutex_unlock(&client->wait_lock);
		}
	}

	pthread_mutex_unlock(&l->list_lock);
	return result;
}

__attribute__((constructor))
void __init__()
{
}

__attribute__((destructor))
void __del__()
{
	ldms_xprt_term(0);
}
