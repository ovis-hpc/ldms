/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2018 Open Grid Computing, Inc. All rights reserved.
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
#include <slurm/spank.h>
#include <json/json_util.h>
#include "ldms.h"
#include "../ldmsd/ldmsd_stream.h"

static ldms_t ldms;
static char *stream;
static sem_t conn_sem;
static int conn_status;
static sem_t recv_sem;
static sem_t close_sem;
#define SLURM_NOTIFY_TIMEOUT 5
static time_t io_timeout = SLURM_NOTIFY_TIMEOUT;

static void msglog(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	vprintf(format, ap);
	va_end(ap);
}

/*
 * From the spank.h header file
 *
 *   slurmd
 *        `-> slurmd_init()
 *        |
 *        `-> job_prolog()
 *        |
 *        | `-> slurmstepd
 *        |      `-> init ()
 *        |       -> process spank options
 *        |       -> init_post_opt ()
 *        |      + drop privileges (initgroups(), seteuid(), chdir())
 *        |      `-> user_init ()
 *        |      + for each task
 *        |      |       + fork ()
 *        |      |       |
 *        |      |       + reclaim privileges
 *        |      |       `-> task_init_privileged ()
 *        |      |       |
 *        |      |       + become_user ()
 *        |      |       `-> task_init ()
 *        |      |       |
 *        |      |       + execve ()
 *        |      |
 *        |      + reclaim privileges
 *        |      + for each task
 *        |      |     `-> task_post_fork ()
 *        |      |
 *        |      + for each task
 *        |      |       + wait ()
 *        |      |          `-> task_exit ()
 *        |      `-> exit ()
 *        |
 *        `---> job_epilog()
 *        |
 *        `-> slurmd_exit()
 *
 *   In srun only the init(), init_post_opt() and local_user_init(), and exit()
 *    callbacks are used.
 *
 *   In sbatch/salloc only the init(), init_post_opt(), and exit() callbacks
 *    are used.
 *
 *   In slurmd proper, only the slurmd_init(), slurmd_exit(), and
 *    job_prolog/epilog callbacks are used.

 */
/*
 * This is a SLURM SPANK plugin sends 'events' to LDMSD plugins.
 *
 * Events are JSon formatted objects sent over the LDMS transport
 * to a set of plugins configured to receive them.
 *
 * Events have the following syntax:
 *
 *   {
 *      "event"     : <event-type>,
 *      "timestamp" : <timestamp>
 *      "context"   : <spank-context-name>
 *      "data"      : { <event-specific data> }
 *   }
 *
 * Init Event ("init") - Start of Job
 *
 *   "data" : {
 *        "id"     : <integer>		// S_JOB_ID
 *        "name"   : <string>		// getenv("SLURM_JOB_NAME")
 *        "uid"    : <integer>		// S_JOB_UID
 *        "gid"    : <integer>		// S_JOB_GID
 *        "ncpus"  : <integer>		// S_JOB_NCPUS
 *        "nnodes" : <integer>		// S_JOB_NNODES
 *        "local_tasks" : <integer>	// S_JOB_LOCAL_TASK_COUNT
 *        "total_tasks" : <integer>	// S_JOB_TOTAL_TASK_COUNT
 *   }
 *
 * Task Init ("task_init") - Start of each process (task) for the job on the node
 *
 *   "data" : {
 *        "id"          : <integer>	// S_JOB_ID
 *        "task_id"     : <integer>	// S_TASK_ID
 *        "global_id"   : <integer>	// S_TASK_GLOBAL_ID
 *        "task_pid"    : <integer>	// S_TASK_PID
 *   }
 *
 * Task Exit ("task_exit") - End of each process (task) for the job
 *
 *   "data" : {
 *        "id"          : <integer>	// S_JOB_ID
 *        "task_id"     : <integer>	// S_TASK_ID
 *        "global_id"   : <integer>	// S_TASK_GLOBAL_ID
 *        "task_pid"    : <integer>	// S_TASK_PID
 *        "task_exit_status" : <integer>// S_TASK_EXIT_STATUS
 *   }
 *
 * Exit Event("exit") - called after all tasks have exited
 *
 *   "data" : {
 *        "id"              : <integer>	// S_JOB_ID
 * 	  "job_exit_status" : <integer>	// S_TASK_EXIT_STATUS
 *   }
 */

#include <json/json_util.h>

SPANK_PLUGIN(slurm_notifier, 1)

static spank_err_t _get_item_u16(spank_t s, int id, uint16_t *pv)
{
	spank_err_t err = spank_get_item(s, id, pv);
	if (err) {
		*pv = 0;
		slurm_info("Spank returned %d accessing item %d", err, id);
	}
	return 0;
}

static spank_err_t _get_item_u32(spank_t s, int id, uint32_t *pv)
{
	spank_err_t err = spank_get_item(s, id, pv);
	if (err) {
		*pv = 0;
		slurm_info("Spank returned %d accessing item %d", err, id);
	}
	return 0;
}

static spank_err_t _get_item_u64(spank_t s, int id, uint64_t *pv)
{
	spank_err_t err = spank_get_item(s, id, pv);
	if (err)
		slurm_info("Spank returned %d accessing item %d\n", err, id);
	return err;
}

static jbuf_t _append_item_u16(spank_t s, jbuf_t jb, const char *name, spank_item_t id, char term)
{
	uint16_t v;
	spank_err_t err = _get_item_u16(s, id, &v);
	if (err) {
		jbuf_free(jb);
		return NULL;
	}
	return jbuf_append_attr(jb, name, "%hd%c", v, term);
}

static jbuf_t _append_item_u32(spank_t s, jbuf_t jb, const char *name, spank_item_t id, char term)
{
	uint32_t v;
	spank_err_t err = _get_item_u32(s, id, &v);
	if (err) {
		jbuf_free(jb);
		return NULL;
	}
	return jbuf_append_attr(jb, name, "%d%c", v, term);
}

static jbuf_t _append_item_u64(spank_t s, jbuf_t jb, const char *name, spank_item_t id, char term)
{
	uint64_t v;
	spank_err_t err = _get_item_u64(s, id, &v);
	if (err) {
		jbuf_free(jb);
		return NULL;
	}
	return jbuf_append_attr(jb, name, "%ld%c", v, term);
}

static void event_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg)
{
	slurm_info("%s[%d]: Event %d received",
		   __func__, __LINE__, e->type);
	switch (e->type) {
	case LDMS_XPRT_EVENT_CONNECTED:
		sem_post(&conn_sem);
		conn_status = 0;
		break;
	case LDMS_XPRT_EVENT_REJECTED:
		ldms_xprt_put(x);
		ldms = NULL;
		conn_status = ECONNREFUSED;
		break;
	case LDMS_XPRT_EVENT_DISCONNECTED:
		ldms_xprt_put(x);
		conn_status = ENOTCONN;
		sem_post(&close_sem);
		break;
	case LDMS_XPRT_EVENT_ERROR:
		conn_status = ECONNREFUSED;
		break;
	case LDMS_XPRT_EVENT_RECV:
		sem_post(&recv_sem);
		break;
	default:
		slurm_info("%s[%d]: Received invalid event type",
			   __func__, __LINE__);
	}
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

static ldms_t setup_connection(int argc, char *argv[])
{
	struct timespec wait_ts;
	char hostname[PATH_MAX];
	const char *xprt = NULL;
	const char *host = NULL;
	const char *port = NULL;
	const char *auth = NULL;
	const char *timeout = NULL;
	int rc;

	for (rc = 0; rc < argc; rc++) {
		if (0 == strncasecmp(argv[rc], "stream", 6)) {
			stream = get_arg_value(argv[rc]);
			continue;
		}
		if (0 == strncasecmp(argv[rc], "xprt", 4)) {
			xprt = get_arg_value(argv[rc]);
			continue;
		}
		if (0 == strncasecmp(argv[rc], "host", 4)) {
			host = get_arg_value(argv[rc]);
			continue;
		}
		if (0 == strncasecmp(argv[rc], "port", 4)) {
			port = get_arg_value(argv[rc]);
			continue;
		}
		if (0 == strncasecmp(argv[rc], "auth", 4)) {
			auth = get_arg_value(argv[rc]);
			continue;
		}
		if (0 == strncasecmp(argv[rc], "timeout", 7)) {
			timeout = get_arg_value(argv[rc]);
			continue;
		}
	}

	if (!host) {
		if (0 == gethostname(hostname, sizeof(hostname)))
			host = hostname;
	}
	if (!xprt)
		xprt = "sock";
	if (!stream)
		stream = "slurm";
	if (!auth)
		auth = "munge";
	if (!port)
		port = "10001";
	if (!timeout)
		io_timeout = SLURM_NOTIFY_TIMEOUT;
	else
		io_timeout = strtoul(timeout, NULL, 0);
	slurm_info("%s[%d]: timeout %s io_timeout %d", __func__, __LINE__, timeout, io_timeout);
	wait_ts.tv_sec = time(NULL) + io_timeout;
	wait_ts.tv_nsec = 0;

	slurm_info("%s:%d ", __func__, __LINE__);
	if (!stream || !port || !auth) {
		slurm_info("slurm_notifier: SLURM_NOTIFY_{STREAM,XPRT,HOST,PORT,AUTH} "
			   "must be set");
		return NULL;
	}
	slurm_info("%s:%d stream=%s xprt=%s host=%s port=%s auth=%s",
		   __func__, __LINE__, stream, xprt, host, port, auth);

	ldms = ldms_xprt_new_with_auth(xprt, msglog, auth, NULL);
	if (!ldms) {
		slurm_info("%s[%d]: Error %d creating the '%s' transport\n",
			   __func__, __LINE__,
			   errno, xprt);
		return NULL;
	}

	sem_init(&recv_sem, 1, 0);
	sem_init(&conn_sem, 1, 0);
	sem_init(&close_sem, 1, 0);

	rc = ldms_xprt_connect_by_name(ldms, host, port, event_cb, NULL);
	if (rc) {
		slurm_info("%s[%d]: Error %d connecting to %s:%s\n",
			   __func__, __LINE__,
			   rc, host, port);
		ldms_xprt_close(ldms);
		ldms = NULL;
		return NULL;
	}
	sem_timedwait(&conn_sem, &wait_ts);
	if (conn_status) {
		ldms_xprt_close(ldms);
		ldms = NULL;
		return NULL;
	}
	return ldms;
}

static int send_event(int argc, char *argv[], jbuf_t jb)
{
	struct timespec wait_ts;
	int rc = ENOTCONN;
	slurm_info("%s:%d ", __func__, __LINE__);
	if (!ldms)
		ldms = setup_connection(argc, argv);
	if (ldms)
		rc = ldmsd_stream_publish(ldms, stream, LDMSD_STREAM_JSON, jb->buf, jb->cursor);
	wait_ts.tv_sec = time(NULL) + io_timeout;
	wait_ts.tv_nsec = 0;
	sem_timedwait(&recv_sem, &wait_ts);
	return rc;
}

jbuf_t make_init_data(spank_t sh, const char *event, const char *context)
{
	char job_name[80];
	jbuf_t jb;
	spank_err_t err;
	jb = jbuf_new(); if (!jb) goto out_1;
	jb = jbuf_append_str(jb, "{"); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "schema", "\"slurm_job_data\","); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "event", "\"%s\",", event); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "timestamp", "%d,", time(NULL)); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "context", "\"%s\",", context); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "data", "{"); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "job_id", S_JOB_ID, ','); if (!jb) goto out_1;
	job_name[0] = '\0';
	err = spank_getenv(sh, "SLURM_JOB_NAME", job_name, sizeof(job_name));
	jb = jbuf_append_attr(jb, "job_name", "\"%s\",", job_name); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "nodeid", S_JOB_NODEID, ','); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "uid", S_JOB_UID, ','); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "gid", S_JOB_GID, ','); if (!jb) goto out_1;
	jb = _append_item_u16(sh, jb, "ncpus", S_JOB_NCPUS, ','); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "nnodes", S_JOB_NNODES, ','); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "local_tasks", S_JOB_LOCAL_TASK_COUNT, ','); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "total_tasks", S_JOB_TOTAL_TASK_COUNT, ' '); if (!jb) goto out_1;
	jb = jbuf_append_str(jb, "}}");
 out_1:
	return jb;
}

jbuf_t make_exit_data(spank_t sh, const char *event, const char *context)
{
	char job_name[80];
	jbuf_t jb;
	spank_err_t err;
	jb = jbuf_new(); if (!jb) goto out_1;
	jb = jbuf_append_str(jb, "{"); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "schema", "\"slurm_job_data\","); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "event", "\"%s\",", event); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "timestamp", "%d,", time(NULL)); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "context", "\"%s\",", context); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "data", "{"); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "job_id", S_JOB_ID, ','); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "nodeid", S_JOB_NODEID, ' '); if (!jb) goto out_1;
	jb = jbuf_append_str(jb, "}}");
 out_1:
	return jb;
}

jbuf_t make_task_init_data(spank_t sh, const char *event, const char *context)
{
	char job_name[80];
	jbuf_t jb;
	spank_err_t err;
	jb = jbuf_new(); if (!jb) goto out_1;
	jb = jbuf_append_str(jb, "{"); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "schema", "\"slurm_job_data\","); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "event", "\"%s\",", event); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "timestamp", "%d,", time(NULL)); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "context", "\"%s\",", context); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "data", "{"); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "job_id", S_JOB_ID, ','); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "task_id", S_TASK_ID, ','); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "task_global_id", S_TASK_GLOBAL_ID, ','); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "task_pid", S_TASK_PID, ','); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "nodeid", S_JOB_NODEID, ','); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "uid", S_JOB_UID, ','); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "gid", S_JOB_GID, ','); if (!jb) goto out_1;
	jb = _append_item_u16(sh, jb, "ncpus", S_JOB_NCPUS, ','); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "nnodes", S_JOB_NNODES, ','); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "local_tasks", S_JOB_LOCAL_TASK_COUNT, ','); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "total_tasks", S_JOB_TOTAL_TASK_COUNT, ' '); if (!jb) goto out_1;
	jb = jbuf_append_str(jb, "}}");
 out_1:
	return jb;
}

jbuf_t make_task_exit_data(spank_t sh, const char *event, const char *context)
{
	char job_name[80];
	jbuf_t jb;
	spank_err_t err;
	jb = jbuf_new(); if (!jb) goto out_1;
	jb = jbuf_append_str(jb, "{"); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "event", "\"%s\",", event); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "timestamp", "%d,", time(NULL)); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "context", "\"%s\",", context); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "data", "{"); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "job_id", S_JOB_ID, ','); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "task_id", S_TASK_ID, ','); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "task_global_id", S_TASK_GLOBAL_ID, ','); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "task_pid", S_TASK_PID, ','); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "nodeid", S_JOB_NODEID, ','); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "task_exit_status", S_TASK_EXIT_STATUS, ' '); if (!jb) goto out_1;
	jb = jbuf_append_str(jb, "}}");
 out_1:
	return jb;
}

static int nnodes(spank_t sh)
{
	uint32_t nnodes;
	spank_err_t err = spank_get_item(sh, S_JOB_NNODES, &nnodes);
	if (err)
		return 0;
	return nnodes;
}

int slurm_spank_init(spank_t sh, int argc, char *argv[])
{
	spank_context_t context = spank_context();
	const char *context_str;
	jbuf_t jb;
	int i;

	slurm_info("%s:%d nnodes=%d", __func__, __LINE__, nnodes(sh));
	for (i = 0; i < argc; i++)
		slurm_info("argc[%d] is %s", i, argv[i]);
	if (0 == nnodes(sh))
		/* Ignore events before node assignment */
		return ESPANK_SUCCESS;

	switch (context) {
	case S_CTX_REMOTE:
		context_str = "remote";
		break;
	case S_CTX_LOCAL:
	default:
		return ESPANK_SUCCESS;
	}

	jb = make_init_data(sh, "init", context_str);
	if (jb) {
		slurm_info("%s:%d %s", __func__, __LINE__, jb->buf);
		send_event(argc, argv, jb);
		jbuf_free(jb);
	}
	slurm_info("%s:%d", __func__, __LINE__);
	return ESPANK_SUCCESS;
}

int slurm_spank_task_init(spank_t sh, int argc, char *argv[])
{
#if 0
	/* Runs as uid */
	spank_context_t context = spank_context();
	const char *context_str;
	jbuf_t jb;

	slurm_info("%s:%d", __func__, __LINE__);

	if (0 == nnodes(sh))
		/* Ignore events before node assignment */
		return ESPANK_SUCCESS;

	switch (context) {
	case S_CTX_REMOTE:
		context_str = "remote";
		break;
	case S_CTX_LOCAL:
	default:
		return ESPANK_SUCCESS;
	}

	jb = make_task_init_data(sh, "task_init", context_str);
	if (jb) {
		slurm_info("%s:%d %s", __func__, __LINE__, jb->buf);
		send_event(jb);
		jbuf_free(jb);
	}
	slurm_info("%s:%d", __func__, __LINE__);
#endif
	return ESPANK_SUCCESS;
}

int
slurm_spank_task_init_privileged(spank_t sh, int argc, char *argv[])
{
	/* Runs as root */
	spank_context_t context = spank_context();
	const char *context_str;
	jbuf_t jb;

	slurm_info("%s:%d", __func__, __LINE__);

	if (0 == nnodes(sh))
		/* Ignore events before node assignment */
		return ESPANK_SUCCESS;

	switch (context) {
	case S_CTX_REMOTE:
		context_str = "remote";
		break;
	case S_CTX_LOCAL:
	default:
		return ESPANK_SUCCESS;
	}

	jb = make_task_init_data(sh, "task_init_priv", context_str);
	if (jb) {
		slurm_info("%s:%d %s", __func__, __LINE__, jb->buf);
		send_event(argc, argv, jb);
		jbuf_free(jb);
	}
	slurm_info("%s:%d", __func__, __LINE__);
	return ESPANK_SUCCESS;
}

/**
 * local
 *
 *     In local context, the plugin is loaded by srun. (i.e. the
 *     "local" part of a parallel job).
 *
 * remote
 *
 *     In remote context, the plugin is loaded by
 *     slurmstepd. (i.e. the "remote" part of a parallel job).
 *
 * allocator
 *
 *     In allocator context, the plugin is loaded in one of the job
 *     allocation utilities sbatch or salloc.
 *
 * slurmd
 *
 *     In slurmd context, the plugin is loaded in the slurmd daemon
 *     itself. Note: Plugins loaded in slurmd context persist for the
 *     entire time slurmd is running, so if configuration is changed or
 *     plugins are updated, slurmd must be restarted for the changes to
 *     take effect.
 *
 * job_script
 *
 *     In the job_script context, plugins are loaded in the
 *     context of the job prolog or epilog. Note: Plugins are loaded
 *     in job_script context on each run on the job prolog or epilog,
 *     in a separate address space from plugins in slurmd
 *     context. This means there is no state shared between this
 *     context and other contexts, or even between one call to
 *     slurm_spank_job_prolog or slurm_spank_job_epilog and subsequent
 *     calls.
 */
/*
 * Called by SLURM just after job exit.
 */
int
slurm_spank_task_exit(spank_t sh, int argc, char *argv[])
{
	/* Runs as root */
	spank_context_t context = spank_context();
	const char *context_str;
	jbuf_t jb;

	slurm_info("%s:%d", __func__, __LINE__);


	if (0 == nnodes(sh))
		/* Ignore events before node assignment */
		return ESPANK_SUCCESS;

	switch (context) {
	case S_CTX_REMOTE:
		context_str = "remote";
		break;
	case S_CTX_LOCAL:
	default:
		return ESPANK_SUCCESS;
	}

	jb = make_task_exit_data(sh, "task_exit", context_str);
	if (jb) {
		slurm_info("%s:%d %s", __func__, __LINE__, jb->buf);
		send_event(argc, argv, jb);
		jbuf_free(jb);
	}
	slurm_info("%s:%d", __func__, __LINE__);
	return ESPANK_SUCCESS;
}

int slurm_spank_exit(spank_t sh, int argc, char *argv[])
{
	/* Runs as root */
	spank_context_t context = spank_context();
	const char *context_str;
	jbuf_t jb;

	slurm_info("%s:%d", __func__, __LINE__);


	if (0 == nnodes(sh))
		/* Ignore events before node assignment */
		return ESPANK_SUCCESS;

	switch (context) {
	case S_CTX_REMOTE:
		context_str = "remote";
		break;
	case S_CTX_LOCAL:
	default:
		return ESPANK_SUCCESS;
	}

	jb = make_exit_data(sh, "exit", context_str);
	if (jb) {
		slurm_info("%s:%d %s", __func__, __LINE__, jb->buf);
		send_event(argc, argv, jb);
		jbuf_free(jb);
	}
	slurm_info("%s:%d", __func__, __LINE__);
	struct timespec wait_ts;
	wait_ts.tv_sec = time(NULL) + io_timeout;
	wait_ts.tv_nsec = 0;
	sem_timedwait(&close_sem, &wait_ts);
	return ESPANK_SUCCESS;
}

static void __attribute__ ((constructor)) slurm_notifier_init(void)
{
}

static void __attribute__ ((destructor)) slurm_notifier_term(void)
{
}
