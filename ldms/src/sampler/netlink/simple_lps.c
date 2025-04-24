/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2021,2023,2025 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2021,2023,2025 Open Grid Computing, Inc. All rights reserved.
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
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <pwd.h>
#include <strings.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <linux/limits.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <pthread.h>
#include "simple_lps.h"
#include "ldmsd_stream.h"

struct slps {
	struct slps_target_list *cl;
	struct slps_target_list slps_target_list;
	pthread_mutex_t list_lock;
	char *stream;
	time_t io_timeout;
	int target_count;
	int blocking;
	int delivery;
	slps_msg_log_f log;
	char *send_log;
	FILE *send_log_f;
};

#define SLPS_RETRY 600 /* 10 minutes. */
#define SLPS_AUTH "munge"
#define SLPS_XPRT "sock"
#define SLPS_TIMEOUT 1000 /* in milliseconds */


#define LDBG 0
#define LERR 3
#define DEBUGC(C, K, FMT, ...) \
	if (C->log ) { struct timespec dts; clock_gettime(CLOCK_REALTIME, &dts); C->log(K, "%lu.%09lu: (%p) (%d) %s:%d " FMT, dts.tv_sec, dts.tv_nsec, C, getpid(), __func__, __LINE__, ##__VA_ARGS__); }
#define DEBUGL(K, FMT, ...) \
	if (l->log ) { struct timespec dts; clock_gettime(CLOCK_REALTIME, &dts); l->log(K, "%lu.%09lu: (%p) (%d) %s:%d " FMT, dts.tv_sec, dts.tv_nsec, l, getpid(), __func__, __LINE__, ##__VA_ARGS__); }
#define DEBUGP(K, FMT, ...) \
	if (log) { struct timespec dts; clock_gettime(CLOCK_REALTIME, &dts); log(K, "%lu.%09lu: (%d) %s:%d " FMT, dts.tv_sec, dts.tv_nsec, getpid(), __func__, __LINE__, ##__VA_ARGS__); }

#define HSIZE 64
#define RSIZE 32
#define NSIZE 16

struct slps_target {
	char xprt[NSIZE];
	char host[HSIZE];
	char port[NSIZE];
	char auth[NSIZE];
	int reconnect;
	time_t next_try;
	ldms_t ldms;
	pthread_cond_t wait_cond;
	pthread_mutex_t wait_lock;
	int state;
	int blocking;
	int last_publish_rc;
	slps_msg_log_f log;
	LIST_ENTRY(slps_target) entry;
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

static int target_state_set(int newstate, struct slps_target *target)
{
	DEBUGC(target, LDBG, "target->state = %s\n", state_name[newstate]);
	target->state = newstate;
	return newstate;
}

/* callback driven by a different thread in ldms_xprt */
static void event_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg)
{
	(void)x;
	struct slps_target *target = cb_arg;
#ifdef LNDEBUG
	const char *event;
#endif
	if (!target->ldms)
		return;
	/* only change state value outside of send call and wait loop.
	 * problem is if we go into in condwait, we can never get the lock
	 * here and we timeout. */
	pthread_mutex_lock(&target->wait_lock);
	switch (e->type) {
	case LDMS_XPRT_EVENT_CONNECTED:
		target_state_set(CONNECTED, target);
#ifdef LNDEBUG
		event = "connected";
#endif
		break;
	case LDMS_XPRT_EVENT_RECV:
#ifdef LNDEBUG
		event = "recv";
#endif
		target_state_set(ACKED, target);
		break;
	case LDMS_XPRT_EVENT_REJECTED:
		target_state_set(DISCONNECTED, target);
#ifdef LNDEBUG
		event = "rejected";
#endif
		break;
	case LDMS_XPRT_EVENT_DISCONNECTED:
		target_state_set(DISCONNECTED, target);
#ifdef LNDEBUG
		event = "disconnected";
#endif
		break;
	case LDMS_XPRT_EVENT_ERROR:
		target_state_set(DISCONNECTED, target);
#ifdef LNDEBUG
		event = "error";
#endif
		break;
	case LDMS_XPRT_EVENT_SEND_COMPLETE:
#ifdef LNDEBUG
		event = "send_complete";
#endif
		goto out;
	default:
#ifdef LNDEBUG
		event = "INVALID_EVENT";
#endif
		DEBUGC(target, LERR, "Received invalid event type\n");
	}
	pthread_cond_signal(&target->wait_cond);
out:
	pthread_mutex_unlock(&target->wait_lock);
#ifdef LNDEBUG
	DEBUGL(LDBG, "Event %s received for target xprt=%s host=%s port=%s auth=%s\n",
		event, target->xprt, target->host, target->port, target->auth);
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

static struct slps_target *slps_target_create(const char *xprt,
	const char *port, const char *auth, int reconnect, const char *host,
	int blocking, slps_msg_log_f log)
{
	struct slps_target *target = calloc(1, sizeof(*target));
	if (!target)
		return NULL;
	strcpy(target->xprt, xprt);
	strcpy(target->port, port);
	strcpy(target->auth, auth);
	strcpy(target->host, host);
	target->blocking = blocking;
	target->log = log;
	target->reconnect = reconnect;
	target->next_try = time(NULL) - 1;
	target_state_set(IDLE, target);
	pthread_mutex_init(&target->wait_lock, NULL);
	pthread_cond_init(&target->wait_cond, NULL);
	return target;
}

/* ldmsd can deliver events back to us for a small interval
 * after we've tried to close the connection. Without the retire
 * logic below, we are certain to hit freed-memory-read conditions.
 * We may still hit them in exiting, since finalize does not have
 * a sleep interval for messages to clear.
 */
static struct slps_target_list retired_slps_target_list;
static struct slps_target_list *retired_cl = &retired_slps_target_list;
static int slps_target_linger = 0;
static void retire_target(struct slps_target *c)
{
#define SLPS_TARGET_LINGER 10
// lock here fixme
	if (!retired_cl) {
		if (!slps_target_linger) {
			slps_target_linger = SLPS_TARGET_LINGER;
			const char *e = getenv("SLPS_TARGET_LINGER");
			if (e) {
				int stl = 0;
				//parse e fixme
				if (e > 0)
					slps_target_linger = stl;
			}
		}
		retired_cl = &retired_slps_target_list;
		LIST_INIT(retired_cl);
	}
	time_t now = time(NULL);
	struct slps_target *target = NULL;
	LIST_FOREACH(target, retired_cl, entry) {
		if (!c || target->next_try < now) {
			LIST_REMOVE(target, entry);
			target->next_try = 0;
			free(target);
		}
	}
	if (c) {
		c->next_try = now + slps_target_linger;
		LIST_INSERT_HEAD(retired_cl, c, entry);
	}
// unlock here fixme
}

static void slps_target_destroy(struct slps_target *c)
{
	if (!c)
		return;
	pthread_mutex_destroy(&c->wait_lock);
	pthread_cond_destroy(&c->wait_cond);
	memset(c, 0, sizeof(*c));
	c->next_try = time(NULL) - 1;
	free(c);
}

/*
 * create target from : string specification
 * and add to slps target list.
 * spec :::: is sock:411:munge:10:localhost
 * If a problem occurs, errno is set.
 */
static int add_target(struct slps *l, const char *spec)
{
	if (!l || !spec || strlen(spec) == 0) {
		DEBUGL(LERR, "slps add_target bad input\n");
		return EINVAL;
	}
	DEBUGL(LDBG, "slps add_target(l,\"%s\"\n", spec);
	const char *r;
	unsigned int i;
	struct slps_target_list *cl = l->cl;
	char xprt[NSIZE] = { '\0' };
	char host[HSIZE] = { '\0' };
	char port[NSIZE] = { '\0' };
	char auth[NSIZE] = { '\0' };
	char reconnect[RSIZE] = { '\0' };
	int ireconnect;

	/* Transport */
	r = spec;
	for (i = 0; *r != '\0' && *r != ':' && i < sizeof(xprt); i++)
		xprt[i] = *r++;
	if (i == 0) {
		strcpy(xprt, SLPS_XPRT);
		DEBUGL(LDBG, "slps add_target assume xprt %s\n", xprt);
	}
	if (*r == ':')
		r++;
	/* Port */
	for (i = 0; *r != '\0' && *r != ':' && i < sizeof(port); i++)
		port[i] = *r++;
	if (i == 0) {
		strcpy(port, "411");
		DEBUGL(LDBG, "slps add_target assume port %s\n", port);
	}
	if (*r == ':')
		r++;
	/* Auth */
	for (i = 0; *r != '\0' && *r != ':' && i < sizeof(auth); i++)
		auth[i] = *r++;
	if (i == 0) {
		strcpy(auth, SLPS_AUTH);
		DEBUGL(LDBG, "slps add_target assume port %s\n", auth);
	}
	if (*r == ':')
		r++;
	/* reconnect */
	char *end = NULL;
	for (i = 0; *r != '\0' && *r != ':' && i < sizeof(reconnect); i++)
		reconnect[i] = *r++;
	if (*r == ':')
		r++;
	if (i == 0) {
		ireconnect = SLPS_RETRY;
		DEBUGL(LDBG, "slps add_target assume reconnect %d\n", ireconnect);
	} else {
		ireconnect = strtol(reconnect, &end, 0);
		if (! (reconnect[0] != '\0' && end[0] == '\0')) {
			DEBUGL(LERR, "slps target parsing 'reconnect' failure.\n");
			return EINVAL;
		}
		if (ireconnect == -1)
			ireconnect = INT_MAX;
	}
	/* Host - comes last to allow ipv6 */
	for (i = 0; *r != '\0' && i < sizeof(host); i++)
		host[i] = *r++;
	if (i == 0)
		strcpy(host, "localhost");

	DEBUGL(LDBG, "_create() resolves to: xprt=%s host=%s port=%s auth=%s reconnect=%d\n", xprt, host, port, auth, ireconnect);
	struct slps_target *target = slps_target_create(xprt, port, auth,
					ireconnect, host, l->blocking, l->log);
	if (!target) {
		DEBUGL(LERR, "slps target creation failure.\n");
		return errno;
	}

	LIST_INSERT_HEAD(cl, target, entry);
	DEBUGL(LDBG, "target xprt=%s port=%s host=%s auth=%s reconnect=%d\n",
		target->xprt, target->port, target->host, target->auth,
		target->reconnect);
	l->target_count++;
	return 0;
}

#define DEFAULT_ARGS "::::"

static char *format_epoch(char *ts, size_t ts_len)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	snprintf(ts, ts_len, "%lu.%06lu", tv.tv_sec, tv.tv_usec);
	return ts;
}
#define EPOCH_STRING ({ char *_ts=alloca(32); format_epoch(_ts, 32); _ts; })

struct slps *slps_create_from_argv(int argc, const char *argv[], slps_msg_log_f log)
{
	const char *timeout = NULL;
	const char *delivery = NULL;
	char *stream = NULL;
	char *send_log = NULL;
	int rc, i;
	struct slps *l = calloc(1, sizeof(*l));
	if (!l) {
		errno = ENOMEM;
		return NULL;
	}
	l->cl = &l->slps_target_list;
	l->log = log;
	pthread_mutex_init(&l->list_lock, NULL);

	for (rc = 0; rc < argc; rc++) {
		if (0 == strncasecmp(argv[rc], "send_log", 8)) {
			send_log = get_arg_value(argv[rc]);
			continue;
		}
		if (0 == strncasecmp(argv[rc], "stream", 6)) {
			stream = get_arg_value(argv[rc]);
			continue;
		}
		if (0 == strncasecmp(argv[rc], "timeout", 7)) {
			timeout = get_arg_value(argv[rc]);
			continue;
		}
		if (0 == strncasecmp(argv[rc], "delivery", 8)) {
			delivery = get_arg_value(argv[rc]);
			continue;
		}
		if (0 == strncasecmp(argv[rc], "blocking", 8)) {
			l->blocking = SLPS_BLOCKING;
			continue;
		}
	}
	for (rc = 0; rc < argc; rc++) {
		DEBUGL(LDBG, "checking %s\n", argv[rc]);
		if (0 == strncasecmp(argv[rc], "target", 6)) {
			i = add_target(l, get_arg_value(argv[rc]));
			if (i) {
				DEBUGL(LERR, "fail to create target %s\n",
					argv[rc]);
			}
		}
	}
	if (LIST_EMPTY(l->cl)) {
		add_target(l, DEFAULT_ARGS);
	}
	if (send_log) {
		l->send_log = strdup(send_log);
	}
	if (!stream)
		stream = "slurm";
	if (!timeout) {
		l->io_timeout = SLPS_TIMEOUT;
	} else {
		char *end = NULL;
		l->io_timeout = strtoul(timeout, &end, 0);
		if (! (timeout[0] != '\0' && end[0] == '\0')) {
			errno = EINVAL;
			goto err;
		}
	}
	DEBUGL(LDBG, "timeout %s io_timeout %ld\n",
		timeout ? timeout : "defaulted", l->io_timeout);
	if (delivery) {
		char *end = NULL;
		l->delivery = strtol(delivery, &end, 0);
		if (! (delivery[0] != '\0' && end[0] == '\0')) {
			errno = EINVAL;
			goto err;
		}
	}
	DEBUGL(LDBG, "delivery %d from '%s'\n", l->delivery,
		delivery ? delivery : "default");
	DEBUGL(LDBG, "stream %s\n", stream);
	l->stream = strdup(stream);
	if (!l->stream ) {
		errno = ENOMEM;
		goto err;
	}
	if (send_log) {
		if (!l->send_log) {
			errno = ENOMEM;
			goto err;
		}
		l->send_log_f = fopen(l->send_log, "a");
		if (!l->send_log_f) {
			goto err;
		} else {
			fprintf(l->send_log_f, "%s: log started\n",
				EPOCH_STRING);
		}
	}
	return l;
err:
	slps_destroy(l);
	return NULL;
}

static const char *check_env(const char *pfx, const char *name, const char *def,
				slps_msg_log_f log)
{
	size_t psize = strlen(pfx) + 2 + strlen(name);
	char ename[psize];
	sprintf(ename, "%s%s", pfx, name);
	char *v = getenv(ename);
	if (v) {
		if (strlen(v) < 3) {
			DEBUGP(LERR, "Environment %s=%s too short.\n",
				ename, v);
			errno = EINVAL;
			return def;
		} else {
			DEBUGP(LDBG, "Environment %s=%s.\n", ename, v);
			return v;
		}
	}
	DEBUGP(LDBG, "Environment %s unset. using default %s.\n", ename, def);
	return def;
}

/* check errno before using returned value. */
static int check_env_int(const char *pfx, const char *name, int def,
			slps_msg_log_f log)
{
	size_t psize = strlen(pfx) + 2 + strlen(name);
	char ename[psize];
	sprintf(ename, "%s%s", pfx, name);
	char *v = getenv(ename);
	char * end = NULL;
	long i = def;
	if (v && v[0] != '\0') {
		i = strtol(v, &end, 10);
		if (*end != '\0') {
			DEBUGP(LERR, "Error parsing environment %s=%s as int\n",
				ename, v);
			errno = EINVAL;
			return def;
		}
		if (i < 0 || i > INT_MAX) {
			DEBUGP(LERR, "Error environment %s=%s out of range.\n",
				ename, v);
			errno = EINVAL;
			return def;
		}
		DEBUGP(LDBG, "Environment %s=%d.\n", ename, i);
	} else {
		DEBUGP(LDBG, "Environment %s unset. Using default %d.\n",
			ename, i);
	}
	return i;
}

struct slps *slps_create_from_env(const char *env_prefix, const char *stream,
	int blocking, slps_msg_log_f log,
	const char *def_xprt, const char *def_host, int def_port,
	const char *def_auth, int def_reconnect, int def_timeout,
	const char *def_send_log)
{
	if (!env_prefix || !stream) {
		DEBUGP(LERR, "slps_create_from_env: bad prefix or stream\n");
		errno = EINVAL;
		return NULL;
	}
	errno = 0;
	const char *estream = check_env(env_prefix, "_LDMS_STREAM", stream,log);
	const char *xprt = check_env(env_prefix, "_LDMS_XPRT", def_xprt, log);
	const char *host = check_env(env_prefix, "_LDMS_HOST", def_host, log);
	const char *auth = check_env(env_prefix, "_LDMS_AUTH", def_auth, log);
	const char *send_log = check_env(env_prefix, "_LDMS_SEND_LOG",
					def_send_log, log);
	int port = check_env_int(env_prefix, "_LDMS_PORT", def_port, log);
	int timeout = check_env_int(env_prefix, "_LDMS_TIMEOUT",
					def_timeout, log);
	int reconnect = check_env_int(env_prefix, "_LDMS_RECONNECT",
					def_reconnect, log);
	if (errno) {
		return NULL;
	}
	return slps_create(estream, blocking, log,
		xprt, host, port, auth, reconnect, timeout, send_log);
}

struct slps *slps_create(const char *stream, int blocking, slps_msg_log_f log,
	const char *xprt, const char *host, int port, const char *auth,
	int reconnect, int timeout, const char *send_log)
{
	if (blocking)
		blocking = SLPS_BLOCKING;
	DEBUGP(LDBG,"slps_create: blocking %d\n", blocking);
	if (!stream) {
		DEBUGP(LERR,"slps creation called without a stream name\n");
		return NULL;
	}
	if (!xprt)
		xprt = "sock";
	if (!host)
		host = "localhost";
	if (!port)
		port = 411;
	char sport[NSIZE];
	sprintf(sport, "%d", port);
	if (!auth)
		auth = "munge";
	if (!reconnect)
		reconnect = 60;
	if (!timeout)
		timeout = SLPS_TIMEOUT;

	struct slps *l = calloc(1, sizeof(*l));
	if (!l) {
		DEBUGP(LERR,"out of memory for %s\n", stream);
		errno = ENOMEM;
		return NULL;
	}
	l->log = log;
	if (send_log) {
		l->send_log = strdup(send_log);
	}
	l->io_timeout = timeout;
	l->stream = strdup(stream);
	l->delivery = 0;
	if (!l->stream ) {
		DEBUGL(LERR, "out of memory for stream %s\n", stream);
		errno = ENOMEM;
		goto err;
	}
	DEBUGL(LDBG, "_create() resolves to: stream=%s blocking=%d xprt=%s host=%s port=%s auth=%s reconnect=%d timeout=%d send_log=%s delivery=%d\n", l->stream, blocking, xprt, host, sport, auth, reconnect, l->io_timeout, l->send_log, l->delivery);
	if (send_log) {
		if (!l->send_log) {
			DEBUGL(LERR, "out of memory for send_log %s\n",
				send_log);
			errno = ENOMEM;
			goto err;
		}
		l->send_log_f = fopen(l->send_log, "a");
		if (!l->send_log_f) {
			goto err;
		} else {
			fprintf(l->send_log_f, "%s: log started\n",
				EPOCH_STRING);
		}
	}
	struct slps_target *target = slps_target_create(xprt, sport, auth,
					reconnect, host, l->blocking, l->log);
	if (!target) {
		DEBUGL(LERR, "slps target creation failure.\n");
		goto err;
	}

	l->cl = &l->slps_target_list;
	pthread_mutex_init(&l->list_lock, NULL);
	LIST_INSERT_HEAD(l->cl, target, entry);
	DEBUGL(LDBG, "target xprt=%s port=%s host=%s auth=%s reconnect=%d\n",
		target->xprt, target->port, target->host, target->auth,
		target->reconnect);
	l->target_count++;
	return l;
err:
	slps_destroy(l);
	return NULL;
}

/* it is assumed the caller has a wait_lock on target.
 * close/destroy connection and set next_try time from reconnect.
 */
static void target_reset(struct slps_target *target)
{
	if (!target)
		return;
	if (target->ldms) {
		DEBUGC(target, LDBG, "RESETTING target xprt=%s host=%s "
			"port=%s auth=%s reconnect=%d\n", target->xprt,
			target->host, target->port, target->auth,
			target->reconnect);
		ldms_xprt_put(target->ldms, "init");
		target->ldms = NULL;
	}
	target_state_set(IDLE, target);
	target->next_try = time(NULL) + target->reconnect;
}

int slps_target_count_get(struct slps *l)
{
	if (l)
		return l->target_count;
	return 0;
}

static int update_targets_blocking(struct slps *l)
{
	int rc;

	if (LIST_EMPTY(l->cl)) {
		DEBUGL(LERR, "empty l.cl\n");
		return ENOTCONN;
	}
	struct slps_target *target;
	time_t now = time(NULL);
	LIST_FOREACH(target, l->cl, entry) {
		pthread_mutex_lock(&target->wait_lock);
		if (target->state == DISCONNECTED)
			target_reset(target);
		if (target->state == IDLE && target->ldms == NULL &&
			now > target->next_try ) {
			target->ldms = ldms_xprt_new_with_auth(target->xprt,
					target->auth, NULL);
			if (!target->ldms) {
				DEBUGL(LERR, "ERROR %d creating the '%s' transport\n",
					     errno, target->xprt);
				target->next_try = now + target->reconnect;
				goto next_target;
			}
			/* Attempt to connect to each target every
			 * reconnect seconds if events happen.
			 */
			target_state_set(CONNECTING, target);
			rc = ldms_xprt_connect_by_name(target->ldms,
							target->host,
							target->port, NULL,
							NULL);
			if (rc) {
				DEBUGL(LERR, "Synchronous error %d connecting to %s:%s\n",
					rc, target->host, target->port);
				target_reset(target);
			} else {
				target_state_set(CONNECTED, target);
			}
		} else {
			DEBUGL(LDBG, "skipping update for %s:%s\n",
				target->host, target->port);
		}
	next_target:
		pthread_mutex_unlock(&target->wait_lock);
	}

	return 0;
}

static int update_targets_nonblocking(struct slps *l)
{
	int rc;

	if (LIST_EMPTY(l->cl))
		return ENOTCONN;
	struct slps_target *target;
	time_t now = time(NULL);
	LIST_FOREACH(target, l->cl, entry) {
		pthread_mutex_lock(&target->wait_lock);
		if (target->state == DISCONNECTED)
			target_reset(target);
		if (target->state == IDLE && target->ldms == NULL &&
			now > target->next_try ) {
			target->ldms = ldms_xprt_new_with_auth(target->xprt,
					target->auth, NULL);
			if (!target->ldms) {
				DEBUGL(LERR, "ERROR %d creating the '%s' transport\n",
					     errno, target->xprt);
				target->next_try = now + target->reconnect;
				goto next_target;
			}
			/* Attempt to connect to each target every reconnect seconds. */
			target_state_set(CONNECTING, target);
			rc = ldms_xprt_connect_by_name(target->ldms,
							target->host,
							target->port,
							event_cb, target);
			if (rc) {
				DEBUGL(LERR, "Synchronous error %d connecting to %s:%s\n",
					rc, target->host, target->port);
			}
		}
	next_target:
		pthread_mutex_unlock(&target->wait_lock);
	}

	/*
	 * Wait for the connections to complete and reset targets who
	 * failed to connect
	 */
	struct timespec wait_ts;
	wait_ts.tv_sec = time(NULL) + l->io_timeout;
	wait_ts.tv_nsec = 0;
	LIST_FOREACH(target, l->cl, entry) {
		pthread_mutex_lock(&target->wait_lock);
		if (target->state == CONNECTING) {
			rc = 0;
			while (target->state == CONNECTING && rc == 0)
				rc = pthread_cond_timedwait(&target->wait_cond,
					&target->wait_lock, &wait_ts);
			if (rc == ETIMEDOUT) {
				DEBUGL(LDBG, "CONNECTING timed out.\n");
			}
			if (target->state != CONNECTED) {
				DEBUGL(LDBG, "DELAYED state=%s connecting to %s:%s\n",
					state_name[target->state],
					target->host, target->port);
				target_reset(target);
			}
		}
		pthread_mutex_unlock(&target->wait_lock);
	}
	return 0;
}

static int update_targets(struct slps *l)
{
	if (!l)
		return ENOMEM;
	if (l->blocking)
		return update_targets_blocking(l);
	return update_targets_nonblocking(l);
}

int slps_destroy(struct slps *l)
{
	struct slps_target *target;
	struct slps_target_list *target_list = l->cl;
	int rc;
	pthread_mutex_lock(&l->list_lock);
	/*
	 * Disconnect target
	 */
	LIST_FOREACH(target, target_list, entry) {
		pthread_mutex_lock(&target->wait_lock);
		if (target->state < DISCONNECTED && target->ldms) {
			DEBUGL(LDBG, "CLOSING target xprt=%s host=%s "
				"port=%s auth=%s\n", target->xprt,
				target->host, target->port, target->auth);
			ldms_xprt_close(target->ldms);
			if (l->blocking) {
				if (target->state < DISCONNECTED &&
					target->ldms) {
					target_reset(target);
				}
			}
		}
		pthread_mutex_unlock(&target->wait_lock);
	}
	if (!l->blocking) {
		/*
		 * Waits for close complete
		 */
		struct timespec wait_ts;
		wait_ts.tv_sec = time(NULL) + l->io_timeout;
		wait_ts.tv_nsec = 0;
		LIST_FOREACH(target, target_list, entry) {
			pthread_mutex_lock(&target->wait_lock);
			if (target->state < DISCONNECTED && target->ldms) {
				DEBUGL(LDBG, "CLOSE WAIT for target %s:%s\n",
					target->host, target->port);
				rc = 0;
				while (target->state < DISCONNECTED &&
					 target->ldms && rc == 0)
					rc = pthread_cond_timedwait(
						&target->wait_cond,
						&target->wait_lock, &wait_ts);
				if (rc == ETIMEDOUT) {
					DEBUGL(LDBG, "CLOSE timed out.\n");
				}
				target_reset(target);
			}
			pthread_mutex_unlock(&target->wait_lock);
			pthread_mutex_destroy(&target->wait_lock);
		}
	}
	while (!LIST_EMPTY(l->cl)) {
		target = LIST_FIRST(l->cl);
		LIST_REMOVE(target, entry);
		slps_target_destroy(target);
	}
	pthread_mutex_unlock(&l->list_lock);
	free(l->stream);
	l->stream = NULL;
	pthread_mutex_destroy(&l->list_lock);
	free(l->send_log);
	l->send_log = NULL;
	if (l->send_log_f) {
		fprintf(l->send_log_f, "%s: done\n", EPOCH_STRING);
		fclose(l->send_log_f);
	}
	l->send_log_f = NULL;
	free(l);
	return 0;
}

struct slps_send_result slps_send_event(struct slps *l, jbuf_t jb)
{
	struct slps_target *target;
	struct slps_target_list *target_list = l->cl;
	struct slps_send_result result = LN_NULL_RESULT;

	if (!l || !jb) {
		result.rc = EINVAL;
		return result;
	}

	pthread_mutex_lock(&l->list_lock);
	/* reconnect targets that are missing and beyond next_try wait. */
	update_targets(l);

	/*
	 * Publish event to at least l->delivery connected targets
	 * or all if !l->delivery.
	 */
	LIST_FOREACH(target, target_list, entry) {
		pthread_mutex_lock(&target->wait_lock);
		if (target->state == CONNECTED || target->state == ACKED) {
			target_state_set(CONNECTED, target);
			DEBUGL(LDBG, "publishing to %s:%s\n", target->host,
				target->port);
			DEBUGL(LDBG, "%s %s:%d: %s\n", l->stream, __func__,
				__LINE__, jb->buf);
			if (l->send_log_f) {
				fprintf(l->send_log_f,
					"%s: publishing to %s:%s (%p)\n",
					EPOCH_STRING, target->host,
					target->port, target);
				fprintf(l->send_log_f, "%s: %s %s:%d: %s\n",
					EPOCH_STRING, l->stream, __func__,
					__LINE__, jb->buf);
			}
			target->last_publish_rc = ldmsd_stream_publish(
				target->ldms, l->stream, LDMSD_STREAM_JSON,
				jb->buf, jb->cursor + 1);
			if (target->last_publish_rc) {
				if (l->send_log_f)
					fprintf(l->send_log_f, "%s: Fail %d "
						"publishing json to %s:%s\n",
						EPOCH_STRING,
						target->last_publish_rc,
						target->host, target->port);
				DEBUGL(LDBG, "Problem %d publishing json to %s:%s\n",
					target->last_publish_rc,
					target->host, target->port);
				target_reset(target);
			} else {
				result.publish_count++;
				if (l->send_log_f)
					fprintf(l->send_log_f,
						"%s: %s %s: success\n",
						EPOCH_STRING, l->stream,
						__func__);
				DEBUGL(LDBG, "%s %s: success\n", l->stream,
					__func__);
			}
		}
		pthread_mutex_unlock(&target->wait_lock);
		if (l->delivery && result.publish_count >= l->delivery)
			break;
	}

	pthread_mutex_unlock(&l->list_lock);
	return result;
}


struct slps_send_result slps_send_string(struct slps *l, size_t buf_len,
	const char *buf)
{
	struct slps_target *target;
	struct slps_target_list *target_list = l->cl;
	struct slps_send_result result = LN_NULL_RESULT;

	if (!l || !buf) {
		result.rc = EINVAL;
		return result;
	}
	if (!buf_len)
		return result;

	pthread_mutex_lock(&l->list_lock);
	/* reconnect targets that are missing and beyond next_try wait. */
	update_targets(l);

	/*
	 * Publish event to at least l->delivery connected targets
	 * or all if !l->delivery
	 */
	LIST_FOREACH(target, target_list, entry) {
		pthread_mutex_lock(&target->wait_lock);
		if (target->state == CONNECTED || target->state == ACKED) {
			target_state_set(CONNECTED, target);
			DEBUGL(LDBG, "publishing to %s:%s\n", target->host,
				target->port);
			if (l->send_log_f) {
				fprintf(l->send_log_f, "%s: publishing to %s:%s (%p)\n",
					EPOCH_STRING, target->host,
					target->port, target);
				fprintf(l->send_log_f, "%s: %s %s:%d: %.40s\n",
					EPOCH_STRING, l->stream, __func__,
					__LINE__, buf);
			}
			target->last_publish_rc = ldmsd_stream_publish(
				target->ldms, l->stream, LDMSD_STREAM_STRING,
				buf, buf_len);
			target->last_publish_rc = 0;
			if (target->last_publish_rc) {
				if (l->send_log_f)
					fprintf(l->send_log_f, "%s: Fail %d "
						"publishing buf to %s:%s\n",
						EPOCH_STRING,
						target->last_publish_rc,
						target->host, target->port);
				DEBUGL(LDBG, "Problem %d publishing buf to "
					"%s:%s\n", target->last_publish_rc,
					target->host, target->port);
				target_reset(target);
			} else {
				result.publish_count++;
				if (l->send_log_f)
					fprintf(l->send_log_f,
						"%s: %s %s: success\n",
						EPOCH_STRING, l->stream,
						__func__);
				DEBUGL(LDBG, "%s %s: success\n", l->stream,
					__func__);
			}
		}
		pthread_mutex_unlock(&target->wait_lock);
		if (l->delivery && result.publish_count >= l->delivery)
			break;
	}

	pthread_mutex_unlock(&l->list_lock);
	return result;
}

void slps_init()
{
}

pthread_mutex_t fin_lock = PTHREAD_MUTEX_INITIALIZER;
static int finalized = 0;
void slps_finalize()
{
	pthread_mutex_lock(&fin_lock);
	if (!finalized)
		ldms_xprt_term(0);
	finalized = 1;
	retire_target(NULL);
	pthread_mutex_unlock(&fin_lock);
}
