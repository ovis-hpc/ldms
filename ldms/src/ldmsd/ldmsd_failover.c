/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2018 National Technology & Engineering Solutions
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

/**
 * \file ldmsd_failover.c
 *
 * \brief LDMSD failover routines.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif /* _GNU_SOURCE */
#include <assert.h>
#include <pthread.h>
#include <netdb.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>

#include "coll/rbt.h"
#include "ovis_event/ovis_event.h"
#include "ovis_ev/ev.h"

#include "ldmsd.h"
#include "ldmsd_request.h"
#include "ldmsd_event.h"
#include "ldmsd_cfgobj.h"

#include "config.h"

#define DEFAULT_PING_INTERVAL 1000000 /* unit: uSec */
#define DEFAULT_AUTOSWITCH 1
#define DEFAULT_TIMEOUT_FACTOR 2

#define ARRAY_LEN(x) (sizeof(x)/sizeof(*(x)))

/* So that we can change this in gdb */
static int failover_debug = 0;

#define __ASSERT(x) assert(x)

void __ldmsd_log(enum ldmsd_loglevel level, const char *fmt, va_list ap);

__attribute__((format(printf, 1, 2)))
static inline
void __dlog(const char *fmt, ...)
{
	if (!failover_debug)
		return;
	va_list ap;
	va_start(ap, fmt);
	__ldmsd_log(LDMSD_LALL, fmt, ap);
	va_end(ap);
}

/*
 * active-side tasks:
 *   - pairing request
 *   - heartbeat
 *   - send redundant cfgobj
 *
 * passive-side tasks:
 *   - accept / reject pairing
 *   - receive + process redundant cfgobj
 *   - failover: activate redundant cfgobjs
 *   - failback: deactivate redundant cfgobjs
 */

typedef enum {
	FAILOVER_STATE_STOP,
	FAILOVER_STATE_START,
	FAILOVER_STATE_STOPPING,
	FAILOVER_STATE_LAST,
} failover_state_t;

typedef enum {
	FAILOVER_CONN_STATE_DISCONNECTED,
	FAILOVER_CONN_STATE_CONNECTING,
	FAILOVER_CONN_STATE_PAIRING,     /* connected, pairing in progress */
	FAILOVER_CONN_STATE_PAIRING_RETRY, /* connected, retry pairing */
	FAILOVER_CONN_STATE_RESETTING,   /* paired, resetting failover state */
	FAILOVER_CONN_STATE_CONFIGURING, /* paired, requesting peer config */
	FAILOVER_CONN_STATE_CONFIGURED,  /* peer config received */
	FAILOVER_CONN_STATE_UNPAIRING, /* unpairing (for stopping) */
	FAILOVER_CONN_STATE_ERROR,       /* error */
	FAILOVER_CONN_STATE_LAST,
} failover_conn_state_t;

typedef enum {
	__FAILOVER_CONFIGURED         = 0x0001,
	__FAILOVER_PEERCFG_ACTIVATED  = 0x0002,
	__FAILOVER_PEERCFG_RECEIVED   = 0x0004,
	__FAILOVER_OUTSTANDING_PING   = 0x0008,
	__FAILOVER_OURCFG_ACTIVATED   = 0x0010,
	__FAILOVER_OUTSTANDING_UNPAIR = 0x0020,

} __failover_flags_t;

#define __F_ON(f, x) do { \
		(f)->flags |= x; \
	} while (0)

#define __F_OFF(f, x) do { \
		(f)->flags &= ~x; \
	} while (0)

#define __F_GET(f, x) ((f)->flags & x)

struct failover_req_ctxt {
	struct str_rbn *srbn;
	struct rbn rbn;
};

static int req_cmp(void *a, const void *b)
{
	return (uint32_t)(uint64_t)a - (uint32_t)(uint64_t)b;
}

typedef
struct ldmsd_failover {
	uint64_t flags;
	char host[256];
	char port[8];
	char xprt[16];
	char peer_name[512];
	int auto_switch;
	uint64_t ping_interval;
	uint64_t task_interval; /* interval for the task */
	double timeout_factor;
	pthread_mutex_t mutex;
	ldms_t ax; /* active xprt */

	failover_state_t state;
	failover_conn_state_t conn_state;

	struct timeval ping_ts;
	struct timeval echo_ts;
	struct timeval timeout_ts;

	struct ldmsd_task task;

	/* store redundant pdrcr and updtr names instead of relying on cfgobj
	 * tree so that we don't have to mess with cfgobj global locks */
	struct rbt prdcr_rbt;
	struct rbt updtr_rbt;
	struct rbt strgp_rbt;

	uint64_t moving_sum;
	int ping_idx;
	int ping_n;
	uint64_t ping_rtt[8]; /* ping round-trip time */
	uint64_t ping_max;    /* ping round-trip time max */
	uint64_t ping_avg;    /* ping round-trip time average */
	double ping_sse;      /* ping round-trip time sum of squared error */
	double ping_sd;       /* ping round-trip time standard deviation */

	int ping_skipped; /* the number of ping skipped due to outstanding */

	ev_worker_t worker;
	ev_t routine_ev;
	struct rbt cfg_tree;
} *ldmsd_failover_t;

struct str_rbn {
	struct rbn rbn;
	int started;
	int will_start;
	char str[OVIS_FLEX];
};

#define STR_RBN(x) ((struct str_rbn *)(x))

int str_rbn_cmp(void *tree_key, const void *key)
{
	return strcmp(tree_key, key);
}

struct str_rbn *str_rbn_new(const char *str)
{
	struct str_rbn *srbn;
	int slen = strlen(str) + 1;
	srbn = calloc(1, sizeof(*srbn) + slen);
	if (!srbn)
		return NULL;
	memcpy(srbn->str, str, slen);
	srbn->rbn.key = srbn->str;
	return srbn;
}

void str_rbn_free(struct str_rbn *srbn)
{
	free(srbn);
}

int ldmsd_str_rbn_add(ldmsd_failover_t f, const char *name, enum ldmsd_cfgobj_type type)
{
	struct rbt *rbt;
	struct str_rbn *srbn = str_rbn_new(name);
	if (!srbn)
		return ENOMEM;
	switch (type) {
	case LDMSD_CFGOBJ_PRDCR:
		rbt = &f->prdcr_rbt;
		break;
	case LDMSD_CFGOBJ_UPDTR:
		rbt = &f->updtr_rbt;
		break;
	case LDMSD_CFGOBJ_STRGP:
		rbt = &f->strgp_rbt;
		break;
	default:
		break;
	}
	rbt_ins(rbt, &srbn->rbn);
	return 0;
}

int __regex_to_fo_regex(const char *regex_str, char *buf, size_t buf_sz)
{
	size_t cnt;
	if (regex_str[0] == '^') {
		cnt = snprintf(buf, buf_sz,
				"^" LDMSD_FAILOVER_NAME_PREFIX "%s",
				&regex_str[1]);
	} else {
		cnt = snprintf(buf, buf_sz,
				"^" LDMSD_FAILOVER_NAME_PREFIX ".*(%s)",
				regex_str);
	}
	if (cnt >= buf_sz)
		return ENOMEM;
	return 0;
}

struct fo_peercfg_ctxt {
	ldms_t x;
	uint8_t is_prdcr_done;
	uint8_t is_prdcr_error;

	uint8_t is_updtr_done;
	uint8_t is_updtr_error;

	uint8_t is_strgp_done;
	uint8_t is_strgp_error;
};


static struct ldmsd_failover __failover;

static
const char *__failover_state_str(ldmsd_failover_t f)
{
	static const char *str[] = {
		[FAILOVER_STATE_STOP]     = "STOP"     ,
		[FAILOVER_STATE_START]    = "START"    ,
		[FAILOVER_STATE_STOPPING] = "STOPPING" ,
	};
	if (f->state < FAILOVER_STATE_LAST)
		return str[f->state];
	return "UNKNOWN";
}

static
const char *__failover_conn_state_str(ldmsd_failover_t f)
{
	static const char *str[] = {
		[FAILOVER_CONN_STATE_DISCONNECTED]  = "DISCONNECTED"  ,
		[FAILOVER_CONN_STATE_CONNECTING]    = "CONNECTING"    ,
		[FAILOVER_CONN_STATE_PAIRING]       = "PAIRING"       ,
		[FAILOVER_CONN_STATE_PAIRING_RETRY] = "PAIRING_RETRY" ,
		[FAILOVER_CONN_STATE_RESETTING]     = "RESETTING"     ,
		[FAILOVER_CONN_STATE_CONFIGURING]   = "CONFIGURING"   ,
		[FAILOVER_CONN_STATE_CONFIGURED]    = "CONFIGURED"    ,
		[FAILOVER_CONN_STATE_UNPAIRING]     = "UNPAIRING"     ,
		[FAILOVER_CONN_STATE_ERROR]         = "ERROR"         ,
	};
	if (f->conn_state < FAILOVER_CONN_STATE_LAST) {
		return str[f->conn_state];
	}
	return "UNKNOWN";
}

static
void __failover_print(ldmsd_failover_t f)
{
	int i;
	static uint64_t fl[] = {
		__FAILOVER_CONFIGURED,
		__FAILOVER_PEERCFG_ACTIVATED,
		__FAILOVER_PEERCFG_RECEIVED,
		__FAILOVER_OUTSTANDING_PING,
		__FAILOVER_OURCFG_ACTIVATED,
	};
	static const char *fls[] = {
		"CONFIGURED",
		"PEERCFG_ACTIVATED",
		"PEERCFG_RECEIVED",
		"OUTSTANDING_PING",
		"OURCFG_ACTIVATED",
	};
	printf("-- failover info %p:\n", f);
	printf("    failover service state: %s\n", __failover_state_str(f));
	printf("    connection state: %s\n", __failover_conn_state_str(f));
	printf("    flags:\n");
	for (i = 0; i < (sizeof(fl)/sizeof(*fl)); i++) {
		printf("	%s: %d\n", fls[i], !!__F_GET(f, fl[i]));
	}
	printf("    host: %s\n", f->host);
	printf("    port: %s\n", f->port);
	printf("    auto_switch: %d\n", f->auto_switch);
	printf("    task_interval: %ld\n", f->task_interval);
}

/* for debugging */
void print_failover_info()
{
	__failover_print(&__failover);
}

static
int __peercfg_activated(ldmsd_failover_t f);
static
int __peercfg_prdcr_activated(ldmsd_failover_t f);
static
int __peercfg_updtr_activated(ldmsd_failover_t f);
static
int __peercfg_reset(ldmsd_failover_t f);
static
int __peercfg_start(ldmsd_failover_t f);
static
int __peercfg_stop(ldmsd_failover_t f);
static
int __failover_reset_and_request_peercfg(ldmsd_failover_t f);

static inline
void __failover_task_resched(ldmsd_failover_t f)
{
	/* f->lock is held */
	ldmsd_task_resched(&f->task, 0, f->task_interval, 0);
}

void __failover_set_ping_interval(ldmsd_failover_t f, uint64_t i)
{
	if (!i)
		i = DEFAULT_PING_INTERVAL;
	f->ping_interval = i;
}

void __failover_init(ldmsd_failover_t f)
{
	bzero(f, sizeof(*f));
	pthread_mutex_init(&f->mutex, NULL);
	f->flags = 0;

	f->task_interval = DEFAULT_PING_INTERVAL;
	f->auto_switch = DEFAULT_AUTOSWITCH;

	rbt_init(&f->prdcr_rbt, str_rbn_cmp);
	rbt_init(&f->updtr_rbt, str_rbn_cmp);
	rbt_init(&f->strgp_rbt, str_rbn_cmp);

	ldmsd_task_init(&f->task);

	f->timeout_factor = DEFAULT_TIMEOUT_FACTOR;

	__failover_set_ping_interval(f, DEFAULT_PING_INTERVAL);

	rbt_init(&f->cfg_tree, req_cmp);
}

static inline
struct ldmsd_sec_ctxt __get_sec_ctxt(struct ldmsd_req_ctxt *req)
{
	struct ldmsd_sec_ctxt sctxt;
	if (req) {
		ldms_xprt_cred_get(req->xprt->ldms.ldms, NULL, &sctxt.crd);
	} else {
		ldmsd_sec_ctxt_get(&sctxt);
	}
	return sctxt;
}

int __name_is_failover(const char *name)
{
	return 0 == strncmp(LDMSD_FAILOVER_NAME_PREFIX, name,
			    sizeof(LDMSD_FAILOVER_NAME_PREFIX)-1);
}

int ldmsd_cfgobj_is_failover(ldmsd_cfgobj_t obj)
{
	return __name_is_failover(obj->name);
}

int cfgobj_is_failover(ldmsd_cfgobj_t obj)
{
	return __name_is_failover(obj->name);
}

static inline
void __failover_lock(ldmsd_failover_t f)
{
	pthread_mutex_lock(&f->mutex);
}

static inline
void __failover_unlock(ldmsd_failover_t f)
{
	pthread_mutex_unlock(&f->mutex);
}

int __failover_send_prdcr(ldmsd_failover_t f, ldms_t x, ldmsd_prdcr_t p)
{
	/* f->lock is held */
	int rc = 0;
	ldmsd_req_cmd_t rcmd;
	char buff[128];
	const char *cstr;
	ldmsd_prdcr_stream_t s;

	rcmd = ldmsd_req_cmd_new(x, LDMSD_FAILOVER_CFGPRDCR_REQ,
				   NULL, NULL, NULL);
	if (!rcmd) {
		rc = errno;
		goto err;
	}

	/* NAME */
	snprintf(buff, sizeof(buff), LDMSD_FAILOVER_NAME_PREFIX "%s", p->obj.name);
	rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_NAME, buff);
	if (rc)
		goto cleanup;

	/* HOST */
	rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_HOST, p->host_name);
	if (rc)
		goto cleanup;

	/* PORT */
	snprintf(buff, sizeof(buff), "%d", (int)p->port_no);
	rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_PORT, buff);
	if (rc)
		goto cleanup;

	/* XPRT */
	rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_XPRT, p->xprt_name);
	if (rc)
		goto cleanup;

	/* INTERVAL */
	snprintf(buff, sizeof(buff), "%ld", p->conn_intrvl_us);
	rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_INTERVAL, buff);
	if (rc)
		goto cleanup;

	/* TYPE */
	cstr = (p->type == LDMSD_PRDCR_TYPE_ACTIVE)?("active"):("passive");
	rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_TYPE, cstr);
	if (rc)
		goto cleanup;

	/* UID */
	snprintf(buff, sizeof(buff), "%u", p->obj.uid);
	rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_UID, buff);
	if (rc)
		goto cleanup;

	/* GID */
	snprintf(buff, sizeof(buff), "%u", p->obj.gid);
	rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_GID, buff);
	if (rc)
		goto cleanup;

	/* PERM */
	snprintf(buff, sizeof(buff), "%#o", p->obj.perm);
	rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_PERM, buff);
	if (rc)
		goto cleanup;

	/* Terminate the message */
	rc = ldmsd_req_cmd_attr_term(rcmd);
	if (rc)
		goto cleanup;
	ldmsd_req_cmd_free(rcmd);

	/* stream */
	LIST_FOREACH(s, &p->stream_list, entry) {
		rcmd = ldmsd_req_cmd_new(x, LDMSD_FAILOVER_CFGPRDCR_REQ,
					   NULL, NULL, NULL);
		if (!rcmd) {
			rc = errno;
			goto err;
		}
		snprintf(buff, sizeof(buff), LDMSD_FAILOVER_NAME_PREFIX "%s", p->obj.name);
		rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_NAME, buff);
		if (rc)
			goto cleanup;
		rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_STREAM, s->name);
		if (rc)
			goto cleanup;
		rc = ldmsd_req_cmd_attr_term(rcmd);
		if (rc)
			goto cleanup;
		ldmsd_req_cmd_free(rcmd);
	}

	return 0;
cleanup:
	ldmsd_req_cmd_free(rcmd);
err:
	return rc;
}

static int __on_reset_resp(ldmsd_req_cmd_t rcmd)
{
	int rc = 0;
	ldmsd_failover_t f = rcmd->ctxt;
	ldmsd_req_hdr_t hdr = (void*)rcmd->reqc->recv_buf;
	__failover_lock(f);
	__F_OFF(f, __FAILOVER_OUTSTANDING_UNPAIR);
	switch (hdr->rsp_err) {
	case EAGAIN:
		/* try again later */
		break;
	case 0:
	default:
		f->conn_state = FAILOVER_CONN_STATE_ERROR;
		ldms_xprt_close(f->ax);
		break;
	}
	__failover_unlock(f);
	return rc;
}

static
int __failover_send_reset(ldmsd_failover_t f, ldms_t xprt)
{
	/* f->lock is held */
	int rc;
	ldmsd_req_cmd_t rcmd = NULL;

	if (__F_GET(f, __FAILOVER_OUTSTANDING_UNPAIR)) {
		__dlog("(DEBUG) ERROR: Outstanding unpair.\n");
		rc = EINPROGRESS;
		goto out;
	}

	rcmd = ldmsd_req_cmd_new(xprt, LDMSD_FAILOVER_RESET_REQ,
				 NULL, __on_reset_resp, f);
	if (!rcmd) {
		rc = errno;
		goto out;
	}
	/* reset has no attribute */
	rc = ldmsd_req_cmd_attr_term(rcmd);
	__F_ON(f, __FAILOVER_OUTSTANDING_UNPAIR);
out:
	if (rc && rcmd)
		ldmsd_req_cmd_free(rcmd);
	return rc;
}

int __failover_send_updtr(ldmsd_failover_t f, ldms_t x, ldmsd_updtr_t u)
{
	/* NOTE: Send a bunch of small messages due to msg size limitation. */
	int rc = 0;
	char buff[128];
	const char *cstr;
	ldmsd_name_match_t nm;
	ldmsd_req_cmd_t rcmd;
	struct ldmsd_name_match *m;

	if (0 == strncmp(LDMSD_FAILOVER_NAME_PREFIX, u->obj.name,
				sizeof(LDMSD_FAILOVER_NAME_PREFIX)-1)) {
		rc = EINVAL;
		goto out;
	}

	rcmd = ldmsd_req_cmd_new(x, LDMSD_FAILOVER_CFGUPDTR_REQ,
				   NULL, NULL, NULL);

	if (!rcmd) {
		rc = errno;
		goto out;
	}

	/* NAME */
	snprintf(buff, sizeof(buff), LDMSD_FAILOVER_NAME_PREFIX "%s", u->obj.name);
	rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_NAME, buff);
	if (rc)
		goto cleanup;

	/* INTERVAL */
	snprintf(buff, sizeof(buff), "%ld", u->sched.intrvl_us);
	rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_INTERVAL, buff);
	if (rc)
		goto cleanup;

	/* OFFSET */
	if (u->sched.offset_us != LDMSD_UPDT_HINT_OFFSET_NONE) {
		snprintf(buff, sizeof(buff), "%ld", u->sched.offset_us);
		rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_OFFSET,
						   buff);
		if (rc)
			goto cleanup;
	}

	/* AUTO INTERVAL */
	if (!u->is_auto_task)
	snprintf(buff, sizeof(buff), "false");
	rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_AUTO_INTERVAL, buff);
	if (rc)
		goto cleanup;

	/* PUSH */
	if (u->push_flags & LDMSD_UPDTR_F_PUSH) {
		cstr = "onpush";
		if (u->push_flags & LDMSD_UPDTR_F_PUSH_CHANGE)
			cstr = "onchange";
		rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_PUSH, cstr);
		if (rc)
			goto cleanup;
	}

	/* UID */
	snprintf(buff, sizeof(buff), "%u", u->obj.uid);
	rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_UID, buff);
	if (rc)
		goto cleanup;

	/* GID */
	snprintf(buff, sizeof(buff), "%u", u->obj.gid);
	rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_GID, buff);
	if (rc)
		goto cleanup;

	/* PERM */
	snprintf(buff, sizeof(buff), "%#o", u->obj.perm);
	rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_PERM, buff);
	if (rc)
		goto cleanup;

	/* TERM */
	rc = ldmsd_req_cmd_attr_term(rcmd);
	if (rc)
		goto cleanup;
	ldmsd_req_cmd_free(rcmd);
	rcmd = NULL;

	/* list of PRODUCERs in this updater */
	for (m = ldmsd_name_match_first(&u->prdcr_list); m; m = ldmsd_name_match_next(m)) {
		rcmd = ldmsd_req_cmd_new(x, LDMSD_FAILOVER_CFGUPDTR_REQ,
					   NULL, NULL, NULL);
		if (!rcmd) {
			rc = errno;
			goto out;
		}

		/* NAME */
		snprintf(buff, sizeof(buff),
			 LDMSD_FAILOVER_NAME_PREFIX "%s", u->obj.name);
		rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_NAME, buff);
		if (rc)
			goto cleanup;
		/* PRODUCER */
		__regex_to_fo_regex(m->regex_str, buff, sizeof(buff));
		rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_PRODUCER,
						   buff);
		if (rc)
			goto cleanup;
		/* TERM */
		rc = ldmsd_req_cmd_attr_term(rcmd);
		if (rc)
			goto cleanup;
		ldmsd_req_cmd_free(rcmd);
		rcmd = NULL;
	}

	/* list of MATCH & REGEX for MATCH_ADD */
	TAILQ_FOREACH(nm, &u->match_list, entry) {
		rcmd = ldmsd_req_cmd_new(x, LDMSD_FAILOVER_CFGUPDTR_REQ,
					   NULL, NULL, NULL);
		if (!rcmd) {
			rc = errno;
			goto out;
		}
		/* NAME */
		snprintf(buff, sizeof(buff),
			 LDMSD_FAILOVER_NAME_PREFIX "%s", u->obj.name);
		rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_NAME, buff);
		if (rc)
			goto cleanup;
		/* MATCH */
		switch (nm->selector) {
		case LDMSD_NAME_MATCH_INST_NAME:
			cstr = "inst";
			break;
		case LDMSD_NAME_MATCH_SCHEMA_NAME:
			cstr = "schema";
			break;
		default:
			__ASSERT(0 == "Unknown match selector");
		}
		rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_MATCH, cstr);
		if (rc)
			goto cleanup;
		/* REGEX */
		rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_REGEX,
						   nm->regex_str);
		if (rc)
			goto cleanup;
		rc = ldmsd_req_cmd_attr_term(rcmd);
		if (rc)
			goto cleanup;
		ldmsd_req_cmd_free(rcmd);
		rcmd = NULL;
	}

	/* let-through */
cleanup:
	if (rcmd)
		ldmsd_req_cmd_free(rcmd);
out:
	return rc;
}

static
int __failover_send_strgp(ldmsd_failover_t f, ldms_t x, ldmsd_strgp_t s)
{

	/* NOTE: Send a bunch of small messages due to msg size limitation. */
	int rc = 0;
	char buff[128];
	ldmsd_name_match_t nm;
	ldmsd_strgp_metric_t sm;
	ldmsd_req_cmd_t rcmd;

	if (0 == strncmp(LDMSD_FAILOVER_NAME_PREFIX, s->obj.name,
				sizeof(LDMSD_FAILOVER_NAME_PREFIX)-1)) {
		rc = EINVAL;
		goto out;
	}

	rcmd = ldmsd_req_cmd_new(x, LDMSD_FAILOVER_CFGSTRGP_REQ,
				 NULL, NULL, NULL);

	if (!rcmd) {
		rc = errno;
		goto out;
	}

	/* NAME */
	snprintf(buff, sizeof(buff), LDMSD_FAILOVER_NAME_PREFIX "%s",
				     s->obj.name);
	rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_NAME, buff);
	if (rc)
		goto cleanup;

	/* PLUGIN */
	rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_PLUGIN,
					   s->plugin_name);
	if (rc)
		goto cleanup;

	/* CONTAINER */
	rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_CONTAINER,
					   s->container);
	if (rc)
		goto cleanup;

	/* SCHEMA */
	rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_SCHEMA, s->schema);
	if (rc)
		goto cleanup;

	/* TERM */
	rc = ldmsd_req_cmd_attr_term(rcmd);
	if (rc)
		goto cleanup;
	ldmsd_req_cmd_free(rcmd);
	rcmd = NULL;

	/* list of PRDCR_MATCHES */
	TAILQ_FOREACH(nm, &s->prdcr_list, entry) {
		rcmd = ldmsd_req_cmd_new(x, LDMSD_FAILOVER_CFGSTRGP_REQ,
					 NULL, NULL, NULL);
		/* NAME */
		snprintf(buff, sizeof(buff), LDMSD_FAILOVER_NAME_PREFIX "%s",
					     s->obj.name);
		rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_NAME, buff);
		if (rc)
			goto cleanup;

		/* REGEX */
		__regex_to_fo_regex(nm->regex_str, buff, sizeof(buff));
		rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_REGEX, buff);
		if (rc)
			goto cleanup;

		/* TERM */
		rc = ldmsd_req_cmd_attr_term(rcmd);
		if (rc)
			goto cleanup;
		ldmsd_req_cmd_free(rcmd);
		rcmd = NULL;
	}

	/* list of METRICs in this strgp */
	TAILQ_FOREACH(sm, &s->metric_list, entry) {
		rcmd = ldmsd_req_cmd_new(x, LDMSD_FAILOVER_CFGSTRGP_REQ,
					 NULL, NULL, NULL);
		/* NAME */
		snprintf(buff, sizeof(buff), LDMSD_FAILOVER_NAME_PREFIX "%s",
					     s->obj.name);
		rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_NAME, buff);
		if (rc)
			goto cleanup;

		/* METRIC */
		rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_METRIC,
						   sm->name);
		if (rc)
			goto cleanup;

		/* TERM */
		rc = ldmsd_req_cmd_attr_term(rcmd);
		if (rc)
			goto cleanup;
		ldmsd_req_cmd_free(rcmd);
		rcmd = NULL;
	}

	/* let through */

cleanup:
	if (rcmd)
		ldmsd_req_cmd_free(rcmd);
out:
	return rc;
}

#if 0
static
int __failover_send_cfgobjs(ldmsd_failover_t f, ldms_t x)
{
	/* f->lock is held */
	ldmsd_prdcr_t p;
	ldmsd_updtr_t u;
	ldmsd_strgp_t s;
	int rc = 0;

	/* Send PRDCR update */
	ldmsd_cfg_lock(LDMSD_CFGOBJ_PRDCR);
	for (p = ldmsd_prdcr_first(); p; p = ldmsd_prdcr_next(p)) {
		if (ldmsd_cfgobj_is_failover(&p->obj))
			continue;
		rc = __failover_send_prdcr(f, x, p);
		if (rc) {
			ldmsd_prdcr_put(p);
			ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR);
			goto out;
		}
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR);

	/* Send UPDTR update */
	ldmsd_cfg_lock(LDMSD_CFGOBJ_UPDTR);
	for (u = ldmsd_updtr_first(); u; u = ldmsd_updtr_next(u)) {
		if (ldmsd_cfgobj_is_failover(&u->obj))
			continue;
		rc = __failover_send_updtr(f, x, u);
		if (rc) {
			ldmsd_updtr_put(u);
			ldmsd_cfg_unlock(LDMSD_CFGOBJ_UPDTR);
			goto out;
		}
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_UPDTR);

	ldmsd_cfg_lock(LDMSD_CFGOBJ_STRGP);
	for (s = ldmsd_strgp_first(); s; s = ldmsd_strgp_next(s)) {
		if (ldmsd_cfgobj_is_failover(&s->obj))
			continue;
		rc = __failover_send_strgp(f, x, s);
		if (rc) {
			ldmsd_strgp_put(s);
			ldmsd_cfg_unlock(LDMSD_CFGOBJ_STRGP);
			goto out;
		}
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_STRGP);

out:
	return rc;
}
#endif /* Not complete */

static int failover_peercfg_rsp_handler(ldmsd_failover_t f, ldmsd_req_hdr_t hdr)
{
	if (hdr->rsp_err) {
		ldmsd_lerror("Failover: peer config request remote error: %d\n",
			     hdr->rsp_err);
		f->conn_state = FAILOVER_CONN_STATE_ERROR;
		ldms_xprt_close(f->ax);
	} else {
		/* all peercfg have been received at this point */
		__F_ON(f, __FAILOVER_PEERCFG_RECEIVED);
		f->conn_state = FAILOVER_CONN_STATE_CONFIGURED;
		ldmsd_linfo("Failover: peer config recv success\n");
	}
	return 0;
}

static
int __on_resp(ldmsd_req_cmd_t rcmd)
{
	ev_t e;
	e = ev_new(ib_rsp_type);
	EV_DATA(e, struct rsp_data)->rcmd = rcmd;
	EV_DATA(e, struct rsp_data)->ctxt = NULL;

	return ev_post(cfg_w, failover_w, e, 0);
}

static
int __failover_request_peercfg(ldmsd_failover_t f)
{
	/* f->lock is held */
	ldmsd_req_cmd_t rcmd;
	int rc;

	ldmsd_linfo("Failover: requesting peer config\n");

	rcmd = ldmsd_req_cmd_new(f->ax, LDMSD_FAILOVER_PEERCFG_REQ,
				 NULL, __on_resp, f);
	if (!rcmd) {
		rc = errno;
		goto out;
	}
	rc = ldmsd_req_cmd_attr_term(rcmd);
out:
	if (rc) {
		ldmsd_lerror("Failover: peer config request local error: %d\n",
			    rc);
	}
	return rc;
}

static
int __failover_reset_and_request_peercfg(ldmsd_failover_t f)
{
	/* f->lock is held */
	int rc = 0;
	__ASSERT(f->conn_state == FAILOVER_CONN_STATE_RESETTING);
	if (f->conn_state != FAILOVER_CONN_STATE_RESETTING)
		return EINVAL;
	rc = __peercfg_reset(f);
	if (rc)
		return rc; /* the f->task will retry this later */
	f->conn_state = FAILOVER_CONN_STATE_CONFIGURING;
	return __failover_request_peercfg(f);
}

static int failover_pair_rsp_handler(ldmsd_failover_t f, ldmsd_req_hdr_t hdr)
{
	int rc = 0;

	__ASSERT(f->conn_state == FAILOVER_CONN_STATE_PAIRING);

	if (hdr->rsp_err) {
		if (hdr->rsp_err == EAGAIN) {
			f->conn_state = FAILOVER_CONN_STATE_PAIRING_RETRY;
			goto out; /* the task will try pairing again */
		}
		ldmsd_lerror("Failover pairing error: %d\n", hdr->rsp_err);
		rc = hdr->rsp_err;
		goto err;
	}
	ldmsd_linfo("Failover pairing success, peer: %s\n", f->peer_name);

	f->conn_state = FAILOVER_CONN_STATE_RESETTING;
	rc = __failover_reset_and_request_peercfg(f);

err:
	if (rc) {
		f->conn_state = FAILOVER_CONN_STATE_ERROR;
		ldms_xprt_close(f->ax);
		/* the disconnect path will handle the state change */
	}
out:
	return rc;
}

static
void __failover_pair(ldmsd_failover_t f)
{
	/* f->lock is held */
	ldmsd_req_cmd_t rcmd;
	const char *myname;
	int rc;

	if (f->auto_switch == 0 && __peercfg_activated(f)) {
		/* Don't start new pairing when the peercfg is manually turned
		 * on and auto_switch is off. Otherwise, peercfg will be
		 * automatically stopped and deleted in the pairing process.
		 */
		f->conn_state = FAILOVER_CONN_STATE_PAIRING_RETRY;
		return;
	}

	ldmsd_linfo("Failover pairing with peer: %s\n", f->peer_name);

	/* just become connected ... request pairing */
	myname = ldmsd_myname_get();
	rcmd = ldmsd_req_cmd_new(f->ax, LDMSD_FAILOVER_PAIR_REQ,
				 NULL, __on_resp, f);
	rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_PEER_NAME, myname);
	if (rc)
		goto err;
	rc = ldmsd_req_cmd_attr_term(rcmd);
	if (rc)
		goto err;
	f->conn_state = FAILOVER_CONN_STATE_PAIRING;
	/* rcmd will be freed when the reply is received */
	return;
err:
	ldmsd_linfo("Failover pairing error (local), rc: %d\n", rc);
	if (rcmd)
		ldmsd_req_cmd_free(rcmd);
	f->conn_state = FAILOVER_CONN_STATE_ERROR;
	/* no need to change the state .. the xprt event will drive the state
	 * change later */
	ldms_xprt_close(f->ax);
}

int __failover_post_routine(ldmsd_failover_t f)
{
	struct timespec to;
	int rc;
	int is_post = 1;

	clock_gettime(CLOCK_REALTIME, &to);
	to.tv_sec += f->task_interval/1000000;
	to.tv_nsec += (f->task_interval%1000000)*1000;

	if (ev_posted(f->routine_ev)) {
		rc = ev_cancel(f->routine_ev);
		if (EBUSY == rc) {
			/*
			 * Failed to cancel the event and
			 * it is soon to be delivered.
			 */
			is_post = 0;
		}
		rc = 0;
	}

	if (is_post)
		rc = ev_post(f->worker, f->worker, f->routine_ev, &to);
	return rc;
}

int failover_xprt_event_actor(ev_worker_t src, ev_worker_t dst,
					ev_status_t status, ev_t e)
{
	int need_start;
	ldmsd_failover_t f;
	ldms_xprt_event_t xprt_ev;

	f = EV_DATA(e, struct failover_data)->f;
	xprt_ev = (ldms_xprt_event_t)EV_DATA(e, struct failover_data)->ctxt;

	switch (xprt_ev->type) {
	case LDMS_XPRT_EVENT_CONNECTED:
		__dlog("Failover: xprt connected\n");
		ldms_xprt_priority_set(f->ax, 1);
		__failover_lock(f);
		/* so that retry operations can follow up not too slow */
		f->task_interval = 1000000;
		__failover_post_routine(f); /* TODO: Check with narate -- do we really need to reschedule here? */
		__ASSERT(f->conn_state == FAILOVER_CONN_STATE_CONNECTING);
		__failover_pair(f);
		__failover_unlock(f);
		break;
	case LDMS_XPRT_EVENT_DISCONNECTED:
		__dlog("Failover: xprt disconnected\n");
	case LDMS_XPRT_EVENT_ERROR:
	case LDMS_XPRT_EVENT_REJECTED:
		__failover_lock(f);
		f->task_interval = f->ping_interval;
		__failover_task_resched(f);
		f->conn_state = FAILOVER_CONN_STATE_DISCONNECTED;
		ldms_xprt_put(f->ax);
		f->ax = NULL;
		need_start = !__F_GET(f, __FAILOVER_OURCFG_ACTIVATED);
		__F_ON(f, __FAILOVER_OURCFG_ACTIVATED);
		__F_OFF(f, __FAILOVER_OUTSTANDING_PING);
		__F_OFF(f, __FAILOVER_OUTSTANDING_UNPAIR);
		__failover_unlock(f);
		if (need_start) {
			ldmsd_ourcfg_start_proc();
		}
		break;
	case LDMS_XPRT_EVENT_RECV:
		ldmsd_recv_msg(f->ax, xprt_ev->data, xprt_ev->data_len);
		break;
	case LDMS_XPRT_EVENT_SET_DELETE:
	case LDMS_XPRT_EVENT_SEND_COMPLETE:
		/* no-op */
		break;
	default:
		__ASSERT(0 == "Unknown Event");
	}
	ev_put(e);
	ldmsd_xprt_event_free(xprt_ev);
	return 0;
}

static
void __failover_xprt_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg)
{
	ev_t ev;
	ldms_xprt_event_t xprt_ev;
	ldmsd_failover_t f = cb_arg;

	xprt_ev = ldmsd_xprt_event_get(e);
	if (!xprt_ev)
		goto enomem;

	ev = ev_new(failover_xprt_type);
	if (!ev)
		goto enomem;

	EV_DATA(ev, struct failover_data)->f = f;
	EV_DATA(ev, struct failover_data)->ctxt = (void*)xprt_ev;
	ev_post(NULL, f->worker, ev, 0);
	return;

enomem:
	free(xprt_ev);
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
	return;
}

extern const char *auth_name;
extern struct attr_value_list *auth_opt;

static
int __failover_active_connect(ldmsd_failover_t f)
{
	/* f->lock is held */
	int rc = 0;
	__dlog("Failover: connecting, flags: %#lx\n", f->flags);
	__ASSERT(f->ax == NULL);
	__ASSERT(f->conn_state == FAILOVER_CONN_STATE_DISCONNECTED);
	f->ax = ldms_xprt_new_with_auth(f->xprt, ldmsd_linfo,
					auth_name, auth_opt);
	if (!f->ax) {
		rc = errno;
		goto out;
	}
	rc = ldms_xprt_connect_by_name(f->ax, f->host, f->port,
				       __failover_xprt_cb, f);
	if (rc)
		goto err1;
	f->conn_state = FAILOVER_CONN_STATE_CONNECTING;
	goto out;
err1:
	ldms_xprt_put(f->ax);
	f->ax = NULL;
	f->conn_state = FAILOVER_CONN_STATE_DISCONNECTED;
out:
	__dlog("Failover: __failover_active_connect() rc: %d, flags: %#lx\n",
	       rc, f->flags);
	return rc;
}


static
void __ping_stat_dlog(ldmsd_failover_t f)
{
	/* f->lock is held */
	__dlog("Failover stat: number of pings: %d\n", f->ping_n);
	__dlog("Failover stat: ping MAX: %ld\n", f->ping_max);
	__dlog("Failover stat: ping AVG: %ld\n", f->ping_avg);
	__dlog("Failover stat: ping SSE: %lf\n", f->ping_sse);
	__dlog("Failover stat: ping SD: %lf\n", f->ping_sd);
}

struct failover_ping_data {
	failover_state_t state;
	failover_conn_state_t conn_state;
	uint64_t flags;
	uint64_t zero; /* padding zero */
};

void __ping_data_hton(struct failover_ping_data *data)
{
	data->state = htonl(data->state);
	data->conn_state = htonl(data->conn_state);
	data->flags = htobe64(data->flags);
}

void __ping_data_ntoh(struct failover_ping_data *data)
{
	data->state = ntohl(data->state);
	data->conn_state = ntohl(data->conn_state);
	data->flags = be64toh(data->flags);
}

static
int __on_ping_resp(ldmsd_req_cmd_t rcmd)
{
	struct timeval tv;
	uint64_t dur;
	uint64_t dur2;
	ldmsd_failover_t f = rcmd->ctxt;
	int will_start = 0;
	struct failover_ping_data *ping_data;
	ldmsd_req_attr_t attr;
	int i;
	__failover_lock(f);

	__dlog("Failover: PING resp recv\n");

	/* Update ping statistics */
	gettimeofday(&f->echo_ts, NULL);
	timersub(&f->echo_ts, &f->ping_ts, &tv);
	dur = tv.tv_sec * 1000000 + tv.tv_usec;
	if (f->ping_n < ARRAY_LEN(f->ping_rtt)) {
		f->ping_n++;
	}
	f->ping_rtt[f->ping_idx] = dur;
	f->ping_idx = (f->ping_idx + 1) % ARRAY_LEN(f->ping_rtt);
	f->ping_max = 0;
	f->moving_sum = 0;
	for (i = 0; i < f->ping_n; i++) {
		f->moving_sum += f->ping_rtt[i];
		if (f->ping_max < f->ping_rtt[i])
			f->ping_max = f->ping_rtt[i];
	}
	f->ping_avg = f->moving_sum / f->ping_n;
	f->ping_sse = 0;
	for (i = 0; i < f->ping_n; i++) {
		f->ping_sse += pow(f->ping_rtt[i] - (double)f->ping_avg, 2);
	}
	if (f->ping_n < 2) {
		f->ping_sd = sqrt(f->ping_sse);
	} else {
		f->ping_sd = sqrt(f->ping_sse / (f->ping_n - 1));
	}

	__ping_stat_dlog(f);

	/* stop peercfg on our side */
	if (f->auto_switch) {
		__peercfg_stop(f);
	}

	/* get peer state */
	attr = ldmsd_req_attr_get_by_id(rcmd->reqc->recv_buf, LDMSD_ATTR_UDATA);
	if (!attr)
		goto out;
	ping_data = (void*)attr->attr_value;
	__ping_data_ntoh(ping_data);
	if (ping_data->flags & __FAILOVER_PEERCFG_ACTIVATED) {
		/* cannot start ours yet */
		goto out;
	}

	/* Check our config status */
	if (!__F_GET(f, __FAILOVER_OURCFG_ACTIVATED)) {
		will_start = 1;
		__F_ON(f, __FAILOVER_OURCFG_ACTIVATED);
	}

	/* calculate & update timeout_ts */
	/* pick larger base, between ping_interval and ping_max */
	dur = (f->ping_max < f->ping_interval)?(f->ping_interval):(f->ping_max);
	/* adjust with 4 sd */
	dur2 = dur + 4*f->ping_sd;
	/* in contrast, adjust with timeout factor */
	dur *= f->timeout_factor;
	/* pick the larger of the two */
	dur = (dur > dur2)?(dur):(dur2);

	__dlog("Failover: timeout (duration): %lu\n", dur);

	tv.tv_sec = dur / 1000000;
	tv.tv_usec = dur % 1000000;
	timeradd(&f->echo_ts, &tv, &f->timeout_ts);
	__dlog("Failover: timeout (timestamp): %lu.%06lu\n",
		f->timeout_ts.tv_sec, f->timeout_ts.tv_usec);

out:
	__failover_unlock(f);
	if (will_start) {
		ldmsd_ourcfg_start_proc();
	}
	__F_OFF(f, __FAILOVER_OUTSTANDING_PING);
	return 0;
}

/* see also failover_ping_handler() */
static
void __failover_ping(ldmsd_failover_t f)
{
	int rc;
	ldmsd_req_cmd_t rcmd;
	const char *name;
	if (__F_GET(f, __FAILOVER_OUTSTANDING_PING)) {
		f->ping_skipped++;
		__dlog("Failover: OUTSTANDING_PING ... will not ping\n");
		return;
	}
	rcmd = ldmsd_req_cmd_new(f->ax, LDMSD_FAILOVER_PING_REQ,
				 NULL, __on_ping_resp, f);
	if (!rcmd)
		goto err;
	name = ldmsd_myname_get();
	rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_NAME, name);
	if (rc)
		goto err;
	rc = ldmsd_req_cmd_attr_term(rcmd);
	if (rc)
		goto err;
	f->task_interval = f->ping_interval;
	__F_ON(f, __FAILOVER_OUTSTANDING_PING);
	f->ping_skipped = 0;
	gettimeofday(&f->ping_ts, NULL);
	/* rcmd will be automatically freed in the ldmsd resp handler */
	return;
err:
	f->conn_state = FAILOVER_CONN_STATE_ERROR;
	return;
}

static int __start_rsp(ldmsd_req_hdr_t reply, struct str_rbn *srbn)
{
	char *s;
	struct ldmsd_req_attr_s *attr;

	switch (reply->req_id) {
	case LDMSD_PRDCR_START_REQ:
		s = "prdcr";
		break;
	case LDMSD_UPDTR_START_REQ:
		s = "updtr";
		break;
	default:
		ldmsd_log(LDMSD_LINFO, "Failover received an unrecognized "
				"config response with ID %d.\n", reply->req_id);
		assert(0 == ENOTSUP);
		return 0;
	}

	attr = ldmsd_first_attr(reply);

	if (reply->rsp_err && (attr->attr_id == LDMSD_ATTR_STRING)) {
		/* Print the error message to the log */
		ldmsd_log(LDMSD_LERROR, "failover: failed to start %s '%s'\n",
								s,  srbn->str);
	} else if (0 == reply->rsp_err) {
		srbn->started = 1;
	}
	return reply->rsp_err;
}

static int __stop_rsp(ldmsd_req_hdr_t reply, struct str_rbn *srbn)
{
	char *s;
	struct ldmsd_req_attr_s *attr;

	switch (reply->req_id) {
	case LDMSD_PRDCR_STOP_REQ:
		s = "prdcr";
		break;
	case LDMSD_UPDTR_STOP_REQ:
		s = "updtr";
		break;
	default:
		ldmsd_log(LDMSD_LINFO, "Failover received an unrecognized "
				"config response with ID %d.\n", reply->req_id);
		assert(0 == ENOTSUP);
		return 0;
	}

	attr = ldmsd_first_attr(reply);

	if (reply->rsp_err && (attr->attr_id == LDMSD_ATTR_STRING)) {
		/* Print the error message to the log */
		ldmsd_log(LDMSD_LERROR, "failover: failed to stop %s '%s'\n",
								s, srbn->str);
	} else if (0 == reply->rsp_err) {
		srbn->started = 0;
	}
	return reply->rsp_err;
}

//static int __del_rsp(ldmsd_req_hdr_t reply, struct str_rbn *srbn)
//{
//	return 0;
//}

struct failover_xprt_ctxt {
	ldmsd_failover_t f;
	enum failover_xprt_ctxt_type {
		FAILOVER_PEERCFG_START = 1,
		FAILOVER_PEERCFG_STOP = 2,
		FAILOVER_PEERCFG_DEL = 3,
	} type;
	struct rbt req_tree;

	int is_all_sent;
	int req_count;
	int rsp_count;
};

static int __is_cfg_done(struct failover_xprt_ctxt *ctxt)
{
	if (ctxt->is_all_sent && rbt_empty(&ctxt->req_tree))
		return 1;
	return 0;
}

static int failover_response_fn(void *_xprt, char *data, size_t data_len)
{
	ev_t e;
	ldmsd_req_hdr_t hdr;
	struct ldmsd_cfg_xprt_s *xprt = _xprt;
	struct failover_xprt_ctxt *xprt_ctxt = xprt->ctxt;

	hdr = malloc(data_len);
	if (!hdr)
		return ENOMEM;

	e = ev_new(ob_rsp_type);
	if (!e) {
		free(hdr);
		LDMSD_LOG_ENOMEM();
		return ENOMEM;
	}
	memcpy(hdr, data, data_len);
	ldmsd_ntoh_req_msg(hdr);
	EV_DATA(e, struct ob_rsp_data)->hdr = hdr;
	EV_DATA(e, struct ob_rsp_data)->ctxt = xprt_ctxt;
	return ev_post(NULL, failover_w, e, 0);
}

extern int post_recv_rec_ev(ldmsd_cfg_xprt_t xprt, struct ldmsd_req_hdr_s *reqc);
static int
__cfg_post(ldmsd_cfg_xprt_t xprt, enum ldmsd_request req_id, struct str_rbn *srbn)
{
	int rc;
	struct failover_req_ctxt *ctxt;
	ldmsd_req_hdr_t req;
	struct ldmsd_req_attr_s attr;
	struct failover_xprt_ctxt *fctxt = xprt->ctxt;

	struct ldmsd_msg_buf *buf = ldmsd_msg_buf_new(512);
	if (!buf)
		goto enomem;

	ctxt = malloc(sizeof(*ctxt));
	if (!ctxt)
		goto enomem;

	req = (ldmsd_req_hdr_t)buf->buf;
	buf->off = sizeof(*req);

	attr.attr_id = LDMSD_ATTR_NAME;
	attr.discrim = 1;
	attr.attr_len = strlen(srbn->str) + 1;

	memcpy(&buf->buf[buf->off], &attr, sizeof(attr));
	buf->off += sizeof(attr);
	ldmsd_msg_buf_append(buf, "%s", srbn->str);
	buf->buf[buf->off] = '\0';
	buf->off++;

	attr.discrim = 0;
	memcpy(&buf->buf[buf->off], &attr.discrim, sizeof(attr.discrim));
	buf->off += sizeof(attr.discrim);

	req->flags = LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F;
	req->rec_len = buf->off;
	req->marker = LDMSD_RECORD_MARKER;
	req->msg_no = ldmsd_msg_no_get();
	req->type = LDMSD_REQ_TYPE_CONFIG_CMD;
	req->req_id = req_id;

	rbn_init(&ctxt->rbn, (void *)(uint64_t)req->msg_no);
	ldmsd_hton_req_msg(req);

	ctxt->srbn = srbn;

	rbt_ins(&fctxt->req_tree, &ctxt->rbn);
	rc = post_recv_rec_ev(xprt, req);
	if (rc) {
		rbt_del(&fctxt->req_tree, &ctxt->rbn);
		ldmsd_log(LDMSD_LERROR, "failover: Failed to post an internal request.\n");
		goto err;
	}

	(void) ldmsd_msg_buf_detach(buf);
	ldmsd_msg_buf_free(buf);

	return 0;
enomem:
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
	rc = ENOMEM;
err:
	ldmsd_msg_buf_free(buf);
	free(ctxt);
	return rc;
}

static
int __peercfg_delete(ldmsd_failover_t f)
{
	/* f->lock is held */
	int rc = 0, _rc, i;
	struct rbn *rbn;
	struct str_rbn *ent;
	enum ldmsd_request req_ids[] = { LDMSD_STRGP_DEL_REQ,
					 LDMSD_UPDTR_DEL_REQ,
					 LDMSD_PRDCR_DEL_REQ };
	struct rbt *t[] = {&f->strgp_rbt, &f->updtr_rbt, &f->prdcr_rbt};
	struct ldmsd_cfg_xprt_s xprt = {0};
	struct failover_xprt_ctxt *fctxt;

	fctxt = calloc(1, sizeof(*fctxt));
	if (!fctxt) {
		ldmsd_log(LDMSD_LCRITICAL, "out of memory\n");
		return ENOMEM;
	}

	fctxt->f = f;
	fctxt->type = FAILOVER_PEERCFG_DEL;
	rbt_init(&fctxt->req_tree, req_cmp);

	xprt.type = LDMSD_CFG_TYPE_INTR;
	xprt.max_msg = LDMSD_CFG_FILE_XPRT_MAX_REC;
	xprt.send_fn = failover_response_fn;
	xprt.trust = 1;
	xprt.ctxt = fctxt;

	ldmsd_linfo("Failover: deleting peer config\n");

	/* cfgobjs have all already stopped */
	for (i = 0; i < 3; i++) {
		while ((rbn = rbt_min(t[i]))) {
			ent = STR_RBN(rbn);
			_rc = __cfg_post(&xprt, req_ids[i], ent);
			if (_rc)
				rc = EAGAIN;
			else
				__sync_fetch_and_add(&fctxt->req_count, 1);
			rbt_del(t[i], rbn);
			free(ent);
		}
	}

	fctxt->is_all_sent = 1;
	return rc;
}

static
int __peercfg_stop(ldmsd_failover_t f)
{
	/* f->lock is held */
	int rc = 0, _rc, i;
	struct rbn *rbn;
	struct str_rbn *ent;
	struct rbt *t[] = {&f->updtr_rbt, &f->prdcr_rbt};
	enum ldmsd_request req[] = {LDMSD_PRDCR_STOP_REQ, LDMSD_UPDTR_STOP_REQ};
	enum ldmsd_request req_id;
	struct ldmsd_cfg_xprt_s xprt = {0};
	struct failover_xprt_ctxt *fctxt;

	fctxt = calloc(1, sizeof(*fctxt));
	if (!fctxt) {
		ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
		return ENOMEM;
	}

	fctxt->f = f;
	fctxt->type = FAILOVER_PEERCFG_STOP;
	rbt_init(&fctxt->req_tree, req_cmp);
	xprt.type = LDMSD_CFG_TYPE_INTR;
	xprt.max_msg = LDMSD_CFG_FILE_XPRT_MAX_REC;
	xprt.send_fn = failover_response_fn;
	xprt.trust = 1;
	xprt.ctxt = fctxt;

	/* NOTE: Leaving out peer storage policy in the favor of letting our
	 *       storage policy picks up the data. */

	ldmsd_linfo("Failover: stopping peercfg\n");
	for (i = 0; i < ARRAY_LEN(t); i++) {
		req_id = req[i];
		RBT_FOREACH(rbn, t[i]) {
			ent = STR_RBN(rbn);
			_rc = __cfg_post(&xprt, req_id, ent);
			if (_rc)
				rc = EAGAIN;
			else
				__sync_fetch_and_add(&fctxt->req_count, 1);
		}
	}
	fctxt->is_all_sent = 1;
	if (rc) {
		ldmsd_linfo("Failover: peer config stopping failed: %d\n", rc);
	} else {
		/*
		 * TODO: Move to the failover's response function.
		 */
		ldmsd_linfo("Failover: peer config stopped\n");
		f->timeout_ts.tv_sec = INT64_MAX;
		__F_OFF(f, __FAILOVER_PEERCFG_ACTIVATED);
	}
	return rc;
}

static
int __peercfg_reset(ldmsd_failover_t f)
{
	/* f->lock is held */
	ldmsd_linfo("Failover: resetting peer config\n");
	int rc = 0;
	rc = __peercfg_stop(f);
	if (rc)
		return rc;
	__peercfg_delete(f);
	__F_OFF(f, __FAILOVER_PEERCFG_RECEIVED);
	return 0;
}

static
int __try_stop(ldmsd_failover_t f)
{
	/* f->lock is held */
	int rc;

	rc = __peercfg_reset(f);
	if (rc)
		return rc;

	switch (f->conn_state) {
	case FAILOVER_CONN_STATE_ERROR:
		return EAGAIN;
	case FAILOVER_CONN_STATE_UNPAIRING:
		if (!__F_GET(f, __FAILOVER_OUTSTANDING_UNPAIR))
			__failover_send_reset(f, f->ax);
		return EAGAIN;
	case FAILOVER_CONN_STATE_CONNECTING:
		f->conn_state = FAILOVER_CONN_STATE_ERROR;
		ldms_xprt_close(f->ax);
		break;
	case FAILOVER_CONN_STATE_PAIRING:
	case FAILOVER_CONN_STATE_PAIRING_RETRY:
	case FAILOVER_CONN_STATE_RESETTING:
	case FAILOVER_CONN_STATE_CONFIGURING:
	case FAILOVER_CONN_STATE_CONFIGURED:
		f->conn_state = FAILOVER_CONN_STATE_UNPAIRING;
		__failover_send_reset(f, f->ax);
		return EAGAIN;
	case FAILOVER_CONN_STATE_DISCONNECTED:
		/* good */
		break;
	default:
		__ASSERT(0 == "BAD FAILOVER CONN STATE");
		return EINVAL;
	}

	ldmsd_task_stop(&f->task);
	f->state = FAILOVER_STATE_STOP;
	ldmsd_inband_cfg_mask_set(LDMSD_PERM_FAILOVER_ALLOWED | 0777);
	return 0;
}

/*
 * Check if conditions are right, then start peercfg.
 */
static
void __try_start_peercfg(ldmsd_failover_t f)
{
	/* f->lock is held */

	struct timeval tv;

	if (!f->auto_switch)
		return; /* do not start peercfg automatically */

	/*
	 * TODO: Check with __FAILOVER_PEERCFG_COMPLETE instead
	 */
	if (!__F_GET(f, __FAILOVER_PEERCFG_RECEIVED))
		return; /* no peercfg ... can't do anything */

	/* check timeout */
	gettimeofday(&tv, NULL);
	if (timercmp(&tv, &f->timeout_ts, <))
		return; /* not timeout yet */

	/* timeout, start peercfg */
	__peercfg_start(f);
}

static int __failover_active_routine(ldmsd_failover_t f)
{
	/* f->lock is held */
	switch (f->conn_state) {
	case FAILOVER_CONN_STATE_DISCONNECTED:
		(void)__failover_active_connect(f);
		break;
	case FAILOVER_CONN_STATE_PAIRING_RETRY:
		__failover_pair(f);
		break;
	case FAILOVER_CONN_STATE_RESETTING:
		(void)__failover_reset_and_request_peercfg(f);
		break;
	case FAILOVER_CONN_STATE_CONNECTING:
	case FAILOVER_CONN_STATE_PAIRING:
	case FAILOVER_CONN_STATE_CONFIGURING:
	case FAILOVER_CONN_STATE_ERROR:
		/* do nothing */
		/* The main driver of these states are in xprt cb path */
		/* ERROR will eventually become DISCONNECTED */
		break;
	case FAILOVER_CONN_STATE_CONFIGURED:
		__failover_ping(f);
		break;
	default:
		__ASSERT(0 == "BAD STATE");
		break;
	}
	__try_start_peercfg(f);

	return 0;
}

int failover_routine_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e)
{
	ldmsd_failover_t f = EV_DATA(e, struct failover_data)->f;
	switch (f->state) {
	case FAILOVER_STATE_STOP:
		__ASSERT(0 == "BAD STATE");
		break;
	case FAILOVER_STATE_START:
		__failover_active_routine(f);
		break;
	case FAILOVER_STATE_STOPPING:
		__try_stop(f);
		break;
	default:
		__ASSERT(0 == "BAD FAILOVER STATE");
	}
	if (f->state != FAILOVER_STATE_STOP)
		return __failover_post_routine(f);
	return 0;
}

static
int __peercfg_prdcr_activated(ldmsd_failover_t f)
{
	struct rbn *rbn;
	struct str_rbn *srbn;
	int sum = 0;
	RBT_FOREACH(rbn, &f->prdcr_rbt) {
		srbn = STR_RBN(rbn);
		if (srbn->started)
			sum += 1;
	}
	return sum;
}

static
int __peercfg_updtr_activated(ldmsd_failover_t f)
{
	struct rbn *rbn;
	struct str_rbn *srbn;
	int sum = 0;
	RBT_FOREACH(rbn, &f->updtr_rbt) {
		srbn = STR_RBN(rbn);
		if (srbn->started)
			sum += 1;
	}
	return sum;
}

static
int __peercfg_activated(ldmsd_failover_t f)
{
	struct rbn *rbn;
	struct str_rbn *srbn;
	RBT_FOREACH(rbn, &f->prdcr_rbt) {
		srbn = STR_RBN(rbn);
		if (srbn->started)
			return 1;
	}
	RBT_FOREACH(rbn, &f->updtr_rbt) {
		srbn = STR_RBN(rbn);
		if (srbn->started)
			return 1;
	}
	return 0;
}

static
int __peercfg_start(ldmsd_failover_t f)
{
	/* f->lock is held */
	int rc = 0;
	struct rbn *rbn;
	struct str_rbn *srbn;
	struct ldmsd_cfg_xprt_s xprt = {0};
	struct failover_xprt_ctxt *fctxt;

	fctxt = calloc(1, sizeof(*fctxt));
	if (!fctxt) {
		LDMSD_LOG_ENOMEM();
		return ENOMEM;
	}

	fctxt->f = f;
	fctxt->type = FAILOVER_PEERCFG_START;
	rbt_init(&fctxt->req_tree, req_cmp);

	xprt.type = LDMSD_CFG_TYPE_INTR;
	xprt.max_msg = LDMSD_CFG_FILE_XPRT_MAX_REC;
	xprt.send_fn = failover_response_fn;
	xprt.trust = 1;
	xprt.ctxt = fctxt;

	ldmsd_linfo("Failover: starting peercfg, flags: %#lo\n", f->flags);

	RBT_FOREACH(rbn, &f->prdcr_rbt) {
		srbn = STR_RBN(rbn);
		if (srbn->started)
			continue;
		(void) __cfg_post(&xprt, LDMSD_PRDCR_START_REQ, srbn);
	}

	RBT_FOREACH(rbn, &f->updtr_rbt) {
		srbn = STR_RBN(rbn);
		if (srbn->started)
			continue;
		(void) __cfg_post(&xprt, LDMSD_UPDTR_START_REQ, srbn);
	}

	__dlog("Failover: __peercfg_start(), flags: %#lx, rc: %d\n",
	       f->flags, rc);
	return 0;
}

/* ===============================
 * LDMSD Failover Request Handlers
 * ===============================
 * (This is a set of passive-side routines)
 */

static
ldmsd_failover_t __ldmsd_req_failover_get(ldmsd_req_ctxt_t req)
{
	/* right now, we have only one global failover object */
	return &__failover;
}

static inline
char *__req_attr_gets(ldmsd_req_ctxt_t req, enum ldmsd_request_attr aid)
{
	return ldmsd_req_attr_str_value_get_by_id(req, aid);
}

int failover_config_handler(ldmsd_req_ctxt_t req)
{
	char *host;
	char *port;
	char *xprt;
	char *auto_switch;
	char *interval;
	char *peer_name;
	char *timeout_factor;
	const char *errmsg = NULL;
	ldmsd_failover_t f;
	int len;
	int rc = 0;
	double d;

	f = __ldmsd_req_failover_get(req);
	if (!f) {
		rc = ENOENT;
		errmsg = "failover config not found";
		goto resp;
	}

	host = __req_attr_gets(req, LDMSD_ATTR_HOST);
	port = __req_attr_gets(req, LDMSD_ATTR_PORT);
	xprt = __req_attr_gets(req, LDMSD_ATTR_XPRT);
	auto_switch = __req_attr_gets(req, LDMSD_ATTR_AUTO_SWITCH);
	interval = __req_attr_gets(req, LDMSD_ATTR_INTERVAL);
	peer_name = __req_attr_gets(req, LDMSD_ATTR_PEER_NAME);
	timeout_factor = __req_attr_gets(req, LDMSD_ATTR_TIMEOUT_FACTOR);

	__failover_lock(f);
	if (f->state != FAILOVER_STATE_STOP) {
		rc = EBUSY;
		errmsg = "cannot reconfigure due to busy failover service";
		goto out;
	}
	if (host) {
		len = snprintf(f->host, sizeof(f->host), "%s", host);
		if (len >= sizeof(f->host)) {
			rc = ENAMETOOLONG;
			goto out;
		}
	}

	if (port) {
		len = snprintf(f->port, sizeof(f->port), "%s", port);
		if (len >= sizeof(f->port)) {
			rc = ENAMETOOLONG;
			goto out;
		}
	}
	if (xprt) {
		len = snprintf(f->xprt, sizeof(f->xprt), "%s", xprt);
		if (len >= sizeof(f->xprt)) {
			rc = ENAMETOOLONG;
			goto out;
		}
	}
	if (!*f->host || !*f->port || !*f->xprt) {
		rc = EINVAL;
		errmsg = "missing host, port or xprt attribute";
		goto out;
	}
	if (peer_name) {
		len = snprintf(f->peer_name, sizeof(f->peer_name),
			       "%s", peer_name);
		if (len >= sizeof(f->peer_name)) {
			rc = ENAMETOOLONG;
			errmsg = "";
		}
	}
	if (auto_switch) {
		f->auto_switch = atoi(auto_switch);
	}
	if (timeout_factor) {
		d = atof(timeout_factor);
		if (!d) {
			d = DEFAULT_TIMEOUT_FACTOR;
		}
		f->timeout_factor = d;
	}
	if (interval) {
		__failover_set_ping_interval(f, strtoul(interval, NULL, 0));
	}

	f->worker = assign_failover_worker();
	ev_dispatch(f->worker, failover_routine_type, failover_routine_actor);

	__F_ON(f, __FAILOVER_CONFIGURED);

out:
	if (rc) {
		__F_OFF(f, __FAILOVER_CONFIGURED);
	}
	__failover_unlock(f);
	if (host)
		free(host);
	if (port)
		free(port);
	if (xprt)
		free(xprt);
	if (auto_switch)
		free(auto_switch);
	if (interval)
		free(interval);
	if (peer_name)
		free(peer_name);
	if (timeout_factor)
		free(timeout_factor);
resp:
	req->errcode = rc;
	ldmsd_send_req_response(req, errmsg);
	return rc;
}

int failover_mod_handler(ldmsd_req_ctxt_t req)
{
	char *auto_switch;
	char *errmsg = NULL;
	ldmsd_failover_t f;
	int rc = 0;
	f = __ldmsd_req_failover_get(req);
	if (!f) {
		rc = ENOENT;
		errmsg = "failover config not found";
		goto resp;
	}
	auto_switch = __req_attr_gets(req, LDMSD_ATTR_AUTO_SWITCH);
	__failover_lock(f);
	if (auto_switch) {
		f->auto_switch = atoi(auto_switch);
		free(auto_switch);
	}
	__failover_unlock(f);
resp:
	req->errcode = rc;
	ldmsd_send_req_response(req, errmsg);
	return rc;
}

int failover_status_handler(ldmsd_req_ctxt_t req)
{
	ldmsd_failover_t f;
	void *buff = NULL;
	char *s;
	size_t sz = 4096;
	int i, rc, len;
	uint32_t term;
	const char *errmsg = NULL;
	ldmsd_req_attr_t attr;
	static uint64_t fl[] = {
		__FAILOVER_CONFIGURED,
		__FAILOVER_PEERCFG_RECEIVED,
		__FAILOVER_OUTSTANDING_PING,
		__FAILOVER_OURCFG_ACTIVATED,
	};
	static const char *fls[] = {
		"CONFIGURED",
		"PEERCFG_RECEIVED",
		"OUTSTANDING_PING",
		"OURCFG_ACTIVATED",
	};
	f = __ldmsd_req_failover_get(req);
	if (!f) {
		rc = ENOENT;
		errmsg = "failover config not found";
		goto err;
	}

	buff = malloc(sz);
	if (!buff) {
		rc = ENOMEM;
		errmsg = "out of memory";
		goto err;
	}
	attr = buff;
	attr->discrim = 1;
	attr->attr_id = LDMSD_ATTR_JSON;
	/* len will be assigned after the str is populated */
	s = buff + sizeof(*attr);
	sz -= sizeof(*attr);

	/* convenient macro just for this function */
	#define __APPEND(...) do {\
		len = snprintf(s, sz, __VA_ARGS__); \
		if (len >= sz) { \
			rc = ENOMEM; \
			errmsg = "out of memory"; \
			goto err; \
		} \
		s += len; \
		sz -= len; \
	} while(0)

	__APPEND("{");
	__APPEND("\"host\": \"%s\"", f->host);
	__APPEND(", \"port\": \"%s\"", f->port);
	__APPEND(", \"xprt\": \"%s\"", f->xprt);
	__APPEND(", \"peer_name\": \"%s\"", f->peer_name);
	__APPEND(", \"task_interval\": \"%ld\"", f->task_interval);
	__APPEND(", \"ping_interval\": \"%ld\"", f->ping_interval);
	__APPEND(", \"ping_skipped\": \"%d\"", f->ping_skipped);
	__APPEND(", \"ping_max\": \"%ld\"", f->ping_max);
	__APPEND(", \"ping_avg\": \"%ld\"", f->ping_avg);
	__APPEND(", \"ping_sd\": \"%lf\"", f->ping_sd);
	__APPEND(", \"timeout_factor\": \"%lf\"", f->timeout_factor);
	__APPEND(", \"auto_switch\": \"%d\"", f->auto_switch);
	__APPEND(", \"ping_ts\": \"%ld.%ld\"", f->ping_ts.tv_sec,
					       f->ping_ts.tv_usec);
	__APPEND(", \"echo_ts\": \"%ld.%ld\"", f->echo_ts.tv_sec,
					       f->echo_ts.tv_usec);
	__APPEND(", \"timeout_ts\": \"%ld.%ld\"", f->timeout_ts.tv_sec,
						  f->timeout_ts.tv_usec);
	__APPEND(", \"failover_state\": \"%s\"", __failover_state_str(f));
	__APPEND(", \"conn_state\": \"%s\"", __failover_conn_state_str(f));
	__APPEND(", \"peercfg_activated\": \"%d\"", __peercfg_activated(f));
	__APPEND(", \"peercfg_updtr_activated\": \"%d\"", __peercfg_updtr_activated(f));
	__APPEND(", \"peercfg_prdcr_activated\": \"%d\"", __peercfg_prdcr_activated(f));
	__APPEND(", \"flags\": {");
	for (i = 0; i < (sizeof(fl)/sizeof(*fl)); i++) {
		if (i)
			__APPEND(", ");
		__APPEND("\"%s\": \"%d\"", fls[i], !!__F_GET(f, fl[i]));
	}
	__APPEND("}"); /* flags */
	__APPEND("}"); /* main obj */
	sz = (void*)s - buff + 1;
	attr->attr_len = sz - sizeof(*attr);
	ldmsd_hton_req_attr(attr);
	rc = ldmsd_append_reply(req, buff, sz, LDMSD_REQ_SOM_F);
	if (rc) {
		errmsg = "append reply error";
		goto err;
	}
	term = 0;
	rc = ldmsd_append_reply(req, (void*)&term, sizeof(term),
				LDMSD_REQ_EOM_F);
	if (rc) {
		errmsg = "append reply error";
		goto err;
	}
	free(buff);
	return 0;
err:
	if (buff)
		free(buff);
	req->errcode = rc;
	ldmsd_send_req_response(req, errmsg);
	return rc;
}

int failover_start_handler(ldmsd_req_ctxt_t req)
{
	int rc;
	char errbuf[128];
	const char *errmsg = NULL;

	rc = ldmsd_failover_start();
	switch (rc) {
	case 0:
		/* OK, do nothing */
		break;
	case EBUSY:
		errmsg = "failover service busy";
		break;
	default:
		/* other errors */
		snprintf(errbuf, sizeof(errbuf),
			 "failover_start failed, rc: %d", rc);
		errmsg = errbuf;
		break;
	}

	req->errcode = rc;
	ldmsd_send_req_response(req, errmsg);
	return rc;
}

int failover_stop_handler(ldmsd_req_ctxt_t req)
{
	/* `failover_stop` will stop the service on both daemons in the pair. */
	int rc = 0;
	char errbuf[128];
	const char *errmsg = errbuf;
	ldmsd_failover_t f;
	f = __ldmsd_req_failover_get(req);
	__failover_lock(f);
	if (f->state != FAILOVER_STATE_START) {
		rc = EBUSY;
		errmsg = "failover service busy";
		goto out;
	}
	f->state = FAILOVER_STATE_STOPPING;
	/* so that we keep __try_stop() fairly quickly */
	f->task_interval = 1000000;
	ldmsd_task_resched(&f->task, LDMSD_TASK_F_IMMEDIATE, f->task_interval, 0);
out:
	__failover_unlock(f);
	req->errcode = rc;
	ldmsd_send_req_response(req, errmsg);
	return rc;
}

int failover_peercfg_start_handler(ldmsd_req_ctxt_t req)
{
	int rc = 0;
	const char *errmsg = NULL;
	char buff[32];
	ldmsd_failover_t f = __ldmsd_req_failover_get(req);

	__failover_lock(f);

	/*
	 * TODO: check if state in CONFIGURED
	 */

	rc = __peercfg_start(f);
	if (rc) {
		snprintf(buff, sizeof(buff), "error: %d\n", rc);
		errmsg = buff;
	}

	__failover_unlock(f);
	req->errcode = rc;
	ldmsd_send_req_response(req, errmsg);
	return rc;
}

int failover_peercfg_stop_handler(ldmsd_req_ctxt_t req)
{
	int rc = 0;
	const char *errmsg = NULL;
	char buff[32];
	ldmsd_failover_t f = __ldmsd_req_failover_get(req);

	__failover_lock(f);

	rc = __peercfg_stop(f);
	if (rc) {
		snprintf(buff, sizeof(buff), "error: %d\n", rc);
		errmsg = buff;
	}

	__failover_unlock(f);
	req->errcode = rc;
	ldmsd_send_req_response(req, errmsg);
	return rc;
}

static
int __verify_pair_req(ldmsd_failover_t f, ldmsd_req_ctxt_t req)
{
	char *peer_name;
	int rc;
	if (!f->peer_name[0])
		return 0;
	/* peer_name must match with the config */
	peer_name = __req_attr_gets(req, LDMSD_ATTR_PEER_NAME);
	if (!peer_name)
		return EPERM;
	rc = (strcmp(f->peer_name, peer_name))?EPERM:0;
	free(peer_name);
	return rc;
}

int failover_pair_handler(ldmsd_req_ctxt_t req)
{
	/* Handling failover pairing request */
	int rc = 0;
	ldmsd_failover_t f;

	f = __ldmsd_req_failover_get(req);
	if (!f) {
		rc = ENOENT;
		goto err0;
	}

	__failover_lock(f);
	if (!__F_GET(f, __FAILOVER_CONFIGURED)) {
		rc = EAGAIN; /* we are not ready */
		goto err1;
	}

	rc = __verify_pair_req(f, req);

err1:
	__failover_unlock(f);
err0:
	req->errcode = rc;
	ldmsd_send_req_response(req, NULL);
	return rc;
}

int failover_reset_handler(ldmsd_req_ctxt_t req)
{
	ldmsd_failover_t f;
	int rc = 0;

	f = __ldmsd_req_failover_get(req);
	if (!f)
		return ENOENT;
	__failover_lock(f);
	switch (f->state) {
	case FAILOVER_STATE_START:
		f->state = FAILOVER_STATE_STOPPING;
		f->task_interval = 1000000;
		ldmsd_task_resched(&f->task, LDMSD_TASK_F_IMMEDIATE,
				   f->task_interval, 0);
		/* let through */
	case FAILOVER_STATE_STOPPING:
		rc = __peercfg_reset(f);
		break;
	case FAILOVER_STATE_STOP:
		/* already stop, do nothing */
	default:
		__ASSERT("BAD FAILOVER STATE\n");
	}
	__failover_unlock(f);
	/* this req needs no resp */
	req->errcode = rc;
	ldmsd_send_req_response(req, NULL);
	return 0;
}

int failover_fwd2cfgobj_tree(ldmsd_failover_t f, ldmsd_req_ctxt_t req,
					ev_worker_t dst, void *ctxt)
{
	ev_t fwd_ev;

	fwd_ev = ev_new(ib_cfg_type);
	if (!fwd_ev) {
		LDMSD_LOG_ENOMEM();
		return ENOMEM;
	}
	EV_DATA(fwd_ev, struct cfg_data)->reqc = req;
	EV_DATA(fwd_ev, struct cfg_data)->ctxt = ctxt;
	return ev_post(f->worker, prdcr_tree_w, fwd_ev, 0);
}

int failover_cfgprdcr_handler(ldmsd_req_ctxt_t req)
{
	ldmsd_failover_t f = __ldmsd_req_failover_get(req);
	return failover_fwd2cfgobj_tree(f, req, prdcr_tree_w, f);
}

int failover_cfgupdtr_handler(ldmsd_req_ctxt_t req)
{
	ldmsd_failover_t f = __ldmsd_req_failover_get(req);
	return failover_fwd2cfgobj_tree(f, req, updtr_tree_w, f);
}

int failover_cfgstrgp_handler(ldmsd_req_ctxt_t req)
{
	ldmsd_failover_t f = __ldmsd_req_failover_get(req);
	return failover_fwd2cfgobj_tree(f, req, strgp_tree_w, f);
}

int failover_ping_handler(ldmsd_req_ctxt_t req)
{
	int rc = 0;
	ldmsd_failover_t f;
	struct ldmsd_req_attr_s attr;
	struct failover_ping_data data;
	char *name;
	const char *errstr = NULL;

	ldms_xprt_priority_set(req->xprt->ldms.ldms, 1);
	f = __ldmsd_req_failover_get(req);
	if (!f)
		return ENOENT;

	__failover_lock(f);
	name = ldmsd_req_attr_str_value_get_by_id(req, LDMSD_ATTR_NAME);
	if (!name) {
		rc = EINVAL;
		errstr = "missing `name` attribute";
		goto err;
	}
	if (strlen(f->peer_name) && 0 != strcmp(f->peer_name, name)) {
		rc = EINVAL;
		errstr = "peer name unmatched";
		goto err;
	}
	__dlog("Failover: PING received, our flags: %#lx\n", f->flags);
	data.state = f->state;
	data.conn_state = f->conn_state;
	data.flags = f->flags;
	data.zero = 0;
	__ping_data_hton(&data);
	attr.attr_id = LDMSD_ATTR_UDATA;
	attr.discrim = 1;
	attr.attr_len = sizeof(data);
	ldmsd_hton_req_attr(&attr);
	__failover_unlock(f);
	req->errcode = 0;
	ldmsd_append_reply(req, (void*)&attr, sizeof(attr),
			   LDMSD_REQ_SOM_F);
	ldmsd_append_reply(req, (void*)&data, sizeof(data), 0);
	attr.discrim = 0;
	ldmsd_append_reply(req, (char *)&attr.discrim, sizeof(uint32_t),
			   LDMSD_REQ_EOM_F);
	free(name);
	return rc;
err:
	free(name);
	__failover_unlock(f);
	req->errcode = rc;
	ldmsd_send_req_response(req, errstr);
	return rc;
}

int failover_peercfg_handler(ldmsd_req_ctxt_t reqc)
{
	int rc = 0;
	ldmsd_failover_t f;
	ev_worker_t dsts[] = {prdcr_tree_w, updtr_tree_w, strgp_tree_w};
	ev_t e;
	int i;

	f = __ldmsd_req_failover_get(reqc);
	if (!f) {
		rc = ENOENT;
		goto out;
	}

	struct fo_peercfg_ctxt *ctxt = calloc(1, sizeof(*ctxt));
	if (!ctxt)
		goto enomem;
	ctxt->x = ldms_xprt_get(reqc->xprt->ldms.ldms);
	reqc->ctxt = ctxt;

	for (i = 0; i < ARRAY_LEN(dsts); i++) {
		e = ev_new(ib_cfg_type);
		if (!e)
			goto enomem;
		ldmsd_req_ctxt_ref_get(reqc, "peercfg2cfgobjtree");
		EV_DATA(e, struct cfg_data)->reqc = reqc;
		EV_DATA(e, struct cfg_data)->ctxt = ldms_xprt_get(f->ax);
		rc = ev_post(failover_w, dsts[i], e, 0);
		if (rc) {
			ldmsd_req_ctxt_ref_put(reqc, "peercfg2cfgobjtree");
			rc = EINTR;
			goto out;
		}

	}
out:
	reqc->errcode = rc;
	ldmsd_send_req_response(reqc, NULL);
	return rc;
enomem:
	free(ctxt);
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
	return ENOMEM;
}

int ldmsd_failover_start()
{
	int rc;
	ldmsd_failover_t f;

	f = &__failover;
	if (f->state != FAILOVER_STATE_STOP) {
		rc = EBUSY;
		goto out;
	}
	f->task_interval = f->ping_interval;

	f->routine_ev = ev_new(failover_routine_type);
	if (!f->routine_ev) {
		ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
		rc = ENOMEM;
		goto out;
	}
	EV_DATA(f->routine_ev, struct failover_data)->f = f;

	f->state = FAILOVER_STATE_START;
	/* allows only failover-safe and failover-internal commands */
	ldmsd_inband_cfg_mask_set(LDMSD_PERM_FAILOVER_ALLOWED |
				  LDMSD_PERM_FAILOVER_INTERNAL);

	rc = ev_post(f->worker, f->worker, f->routine_ev, 0);
out:
	return rc;
}

int failover_cfg_actor(ev_worker_t dst, ev_worker_t src, ev_status_t status, ev_t e)
{
	int rc;
	ldmsd_req_ctxt_t reqc = EV_DATA(e, struct cfg_data)->reqc;

	switch (reqc->req_id) {
	case LDMSD_FAILOVER_MOD_REQ:
		break;
	case LDMSD_FAILOVER_START_REQ:
		rc = failover_start_handler(reqc);
		break;
	case LDMSD_FAILOVER_PAIR_REQ:
		rc = failover_pair_handler(reqc);
		break;
	case LDMSD_FAILOVER_PEERCFG_REQ:
		rc = failover_peercfg_handler(reqc);
		break;
	case LDMSD_FAILOVER_PING_REQ:
		rc = failover_ping_handler(reqc);
		break;
	case LDMSD_FAILOVER_CFGPRDCR_REQ:
		rc = failover_cfgprdcr_handler(reqc);
		break;
	case LDMSD_FAILOVER_CFGUPDTR_REQ:
		rc = failover_cfgupdtr_handler(reqc);
		break;
	case LDMSD_FAILOVER_CFGSTRGP_REQ:
		rc = failover_cfgstrgp_handler(reqc);
		break;
	case LDMSD_FAILOVER_PEERCFG_START_REQ:
	case LDMSD_FAILOVER_PEERCFG_STOP_REQ:
	default:
		assert(0);
		break;
	}
	ev_put(e);
	return rc;
}

int __failover_cfgobj_peercfg_rsp_handler(ldmsd_req_ctxt_t reqc)
{
	struct fo_peercfg_ctxt *ctxt = reqc->ctxt;

	if (!ctxt->is_prdcr_done)
		return 0;

	if (!ctxt->is_updtr_done)
		return 0;

	if (!ctxt->is_strgp_done)
		return 0;

	if (ctxt->is_prdcr_error || ctxt->is_updtr_error || ctxt->is_strgp_error) {
		reqc->errcode = EINTR;
	}
	ldmsd_send_req_response(reqc, NULL);
	ldmsd_req_ctxt_ref_put(reqc, "peercfg2cfgobjtree");
	ldms_xprt_put(ctxt->x);
	free(ctxt);
	return 0;
}

int failover_cfgobj_rsp_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e)
{
	if (EV_OK != status)
		return 0;

	int rc = 0;
	ldmsd_req_ctxt_t reqc = EV_DATA(e, struct cfgobj_rsp_data)->ctxt;

	switch (reqc->req_id) {
	case LDMSD_FAILOVER_PEERCFG_REQ:
		rc = __failover_cfgobj_peercfg_rsp_handler(reqc);
		break;
	default:
		break;
	}

	return rc;
}

int failover_ob_rsp_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e)
{
	if (EV_OK != status)
		return 0;

	int rc;
	ldmsd_req_hdr_t hdr;
	struct failover_xprt_ctxt *fctxt;
	ldmsd_failover_t f;
	struct failover_req_ctxt *ctxt;
	struct rbn *rbn;

	hdr = EV_DATA(e, struct ob_rsp_data)->hdr;
	fctxt = (struct failover_xprt_ctxt *)EV_DATA(e, struct ob_rsp_data)->ctxt;
	f = fctxt->f;

	rbn = rbt_find(&fctxt->req_tree, (void *)(uint64_t)hdr->msg_no);
	assert(rbn);
	rbt_del(&fctxt->req_tree, rbn);
	ctxt = container_of(rbn, struct failover_req_ctxt, rbn);

	switch (hdr->req_id) {
	case LDMSD_PRDCR_START_REQ:
	case LDMSD_UPDTR_START_REQ:
		rc = __start_rsp(hdr, ctxt->srbn);
		break;
	case LDMSD_PRDCR_STOP_REQ:
	case LDMSD_UPDTR_STOP_REQ:
	case LDMSD_STRGP_STOP_REQ:
		rc = __stop_rsp(hdr, ctxt->srbn);
		break;
	case LDMSD_STRGP_DEL_REQ:
	case LDMSD_PRDCR_DEL_REQ:
	case LDMSD_UPDTR_DEL_REQ:
		break;
	default:
		ldmsd_log(LDMSD_LINFO, "Failover received an unrecognized "
				"config response with ID %d.\n", hdr->req_id);
		return 0;
	}
	if (__is_cfg_done(fctxt)) {
		switch (fctxt->type) {
		case FAILOVER_PEERCFG_STOP:
			if (!rc) {
				ldmsd_linfo("Failover: peer config stopped\n");
				f->timeout_ts.tv_sec = INT64_MAX;
			}
			break;
		case FAILOVER_PEERCFG_START:
			/* do nothing */
			break;
		case FAILOVER_PEERCFG_DEL:
			if (!rc)
				ldmsd_linfo("Failover: peer config deleted\n");
			break;
		default:
			break;
		}
		free(fctxt);
	}

	free(ctxt);
	ev_put(e);
	return 0;
}

int failover_ib_rsp_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e)
{
	if (EV_OK != status)
		return 0;

	ldmsd_req_cmd_t rcmd = EV_DATA(e, struct rsp_data)->rcmd;
	ldmsd_req_hdr_t hdr = (ldmsd_req_hdr_t)rcmd->reqc->recv_buf;
	uint32_t req_id = rcmd->reqc->req_id;
	ldmsd_failover_t f = __ldmsd_req_failover_get(rcmd->reqc);

	switch (req_id) {
	case LDMSD_FAILOVER_PAIR_REQ:
		(void) failover_pair_rsp_handler(f, hdr);
		break;
	case LDMSD_FAILOVER_PEERCFG_REQ:
		(void) failover_peercfg_rsp_handler(f, hdr);
		break;
	default:
		break;
	}

	ev_put(e);
	return 0;
}


int tree_failover_peercfg_handler(ldmsd_req_ctxt_t reqc, enum ldmsd_cfgobj_type type)
{
	int rc;
	ldmsd_cfgobj_t obj;
	const char *tree_name;
	ev_worker_t src;
	ev_worker_t dst;
	struct ldmsd_cfgobj_cfg_ctxt *ctxt;

	switch (type) {
	case LDMSD_CFGOBJ_PRDCR:
		tree_name = "prdcr_tree";
		src = prdcr_tree_w;
		break;
	case LDMSD_CFGOBJ_UPDTR:
		tree_name = "updtr_tree";
		src = updtr_tree_w;
		break;
	case LDMSD_CFGOBJ_STRGP:
		tree_name = "strgp_tree";
		src = strgp_tree_w;
		break;
	default:
		break;
	}

	ctxt = calloc(1, sizeof(*ctxt));
	if (!ctxt) {
		LDMSD_LOG_ENOMEM();
		return ENOMEM;
	}
	ref_init(&ctxt->ref, "create", free, ctxt);
	for (obj = ldmsd_cfgobj_first(type); obj; obj = ldmsd_cfgobj_next(obj)) {
		if (ldmsd_cfgobj_is_failover(obj))
			continue;
		ref_get(&ctxt->ref, tree_name);
		dst = ldmsd_cfgobj_worker_get(obj);
		rc = ldmsd_cfgtree_post2cfgobj(obj, src, dst, reqc, ctxt);
		if (rc) {
			ref_put(&ctxt->ref, tree_name);
			if (ENOMEM == rc)
				goto enomem;
			reqc->errcode = EINTR;
			rc = linebuf_printf(reqc,
					"The %s worker failed to "
					"handle the failover_peercfg command.",
					tree_name);
			goto send_reply;
		}
	}
	return 0;
enomem:
	reqc->errcode = ENOMEM;
	LDMSD_LOG_ENOMEM();
	rc = ENOMEM;
	(void) linebuf_printf(reqc, "LDMSD: out of memory.");
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	return rc;
}

extern void ldmsd_req_ctxt_sec_get(ldmsd_req_ctxt_t rctxt, ldmsd_sec_ctxt_t sctxt);
static int __failover_prdcr_add(ldmsd_req_ctxt_t reqc)
{
	/* create new cfg if not existed, or update if exited */
	char *name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	char *host = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_HOST);
	char *port = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_PORT);
	char *xprt = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_XPRT);
	char *interval = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_INTERVAL);
	char *type = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_TYPE);
	char *uid = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_UID);
	char *gid = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_GID);
	char *perm = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_PERM);
	char *auth = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_AUTH);
	char *stream = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_STREAM);

	uid_t _uid;
	gid_t _gid;
	mode_t _perm;

	enum ldmsd_prdcr_type ptype;
	ldmsd_prdcr_t p;
	ldmsd_prdcr_stream_t s = NULL;
	struct ldmsd_sec_ctxt sctxt;
	int rc = 0;

	if (!name || !__name_is_failover(name)) {
		__ASSERT(0 == "BAD MESSAGE");
		rc = EINVAL;
		goto out;
	}

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);
	_uid = (uid)?(strtoul(uid, NULL, 0)):(sctxt.crd.uid);
	_gid = (gid)?(strtoul(gid, NULL, 0)):(sctxt.crd.gid);
	_perm = (perm)?(strtoul(perm, NULL, 0)):(0700);
	sctxt.crd.uid = _uid;
	sctxt.crd.gid = _gid;

	if (!port || !xprt || !host || !interval || !type) {
		__ASSERT(0 == "BAD MESSAGE");
		rc = EINVAL;
		goto out;
	}
	if (strncasecmp("passive", type, 8) == 0) {
		ptype = LDMSD_PRDCR_TYPE_PASSIVE;
	} else {
		ptype = LDMSD_PRDCR_TYPE_ACTIVE;
	}
	p = ldmsd_prdcr_new_with_auth(name, xprt, host, atoi(port), ptype,
			atoi(interval), auth, _uid, _gid, _perm);
	if (!p) {
		rc = errno;
		goto out;
	}

	if (stream) {
		s = calloc(1, sizeof(*s));
		if (!s)
			goto enomem;
		s->name = strdup(stream);
		if (!s->name)
			goto enomem;
		LIST_INSERT_HEAD(&p->stream_list, s, entry);
	}
	goto out;
enomem:
	LDMSD_LOG_ENOMEM();
	rc = ENOMEM;
	free(s);
out:
	free(name);
	free(host);
	free(port);
	free(xprt);
	free(interval);
	free(type);
	free(uid);
	free(gid);
	free(perm);
	/* this req needs no resp */
	return rc;
}

int tree_failover_cfgprdcr_handler(ldmsd_req_ctxt_t reqc, void *fctxt)
{
	int rc = 0;
	ldmsd_prdcr_t prdcr;
	struct ldmsd_cfgobj_cfg_ctxt *ctxt;
	char *name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	ldmsd_failover_t f = fctxt;
	struct str_rbn *srbn;

	if (!name || !__name_is_failover(name)) {
		assert(0 == "failover: BAD MESSAGE");
		rc = EINVAL;
		goto out;
	}

	ctxt = malloc(sizeof(*ctxt));
	if (!ctxt) {
		LDMSD_LOG_ENOMEM();
		rc = ENOMEM;
		goto out;
	}
	ref_init(&ctxt->ref, "create", free, ctxt);

	prdcr = ldmsd_prdcr_find(name);
	if (prdcr) {
		ref_get(&ctxt->ref, "prdcr_tree");
		ctxt->is_all = 1;
		rc = ldmsd_cfgtree_post2cfgobj(&prdcr->obj, prdcr_tree_w, prdcr->worker, reqc, ctxt);
		if (rc) {
			ldmsd_log(LDMSD_LERROR, "Failed to process a failover cfgprdcr request\n");
			ref_put(&ctxt->ref, "prdcr_tree");
			goto err;
		}
	} else {
		/* Create new prdcr */
		rc = __failover_prdcr_add(reqc);
		if (rc)
			goto err;
		srbn = str_rbn_new(name);
		if (!srbn) {
			LDMSD_LOG_ENOMEM();
			rc = ENOMEM;
			goto err;
		}
		__failover_lock(f);
		rbt_ins(&f->prdcr_rbt, &srbn->rbn);
		__failover_unlock(f);
	}
out:
	free(name);
	return rc;
err:
	ref_put(&ctxt->ref, "create");
	goto out;
}

static int __failover_updtr_add(ldmsd_req_ctxt_t reqc)
{
	char *name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	char *interval = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_INTERVAL);
	char *offset = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_OFFSET);
	char *regex = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_REGEX);
	char *match = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_MATCH);
	char *push = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_PUSH);
	char *producer = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_PRODUCER);
	char *auto_interval = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_AUTO_INTERVAL);
	char *uid = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_UID);
	char *gid = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_GID);
	char *perm = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_PERM);

	uid_t _uid;
	gid_t _gid;
	mode_t _perm;

	int rc = 0;
	struct ldmsd_name_match *mp, *mm;
	mp = mm = NULL;
	ldmsd_updtr_t u;
	struct ldmsd_sec_ctxt sctxt;
	int push_flags = 0;
	uint8_t is_auto_interval = (auto_interval)?0:1;

	if (!name || !__name_is_failover(name)) {
		__ASSERT(0 == "BAD MESSAGE");
		rc = EINVAL;
		goto out;
	}

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);
	_uid = (uid)?(strtoul(uid, NULL, 0)):(sctxt.crd.uid);
	_gid = (gid)?(strtoul(gid, NULL, 0)):(sctxt.crd.gid);
	_perm = (perm)?(strtoul(perm, NULL, 0)):(0700);
	sctxt.crd.uid = _uid;
	sctxt.crd.gid = _gid;

	if (push) {
		if (0 == strcasecmp("onchange", push)) {
			push_flags = LDMSD_UPDTR_F_PUSH |
					LDMSD_UPDTR_F_PUSH_CHANGE;
		} else {
			push_flags = LDMSD_UPDTR_F_PUSH;
		}
	}

	/* create */
	u = ldmsd_updtr_new_with_auth(name, interval, offset,
					push_flags, is_auto_interval,
					_uid, _gid, _perm);
	if (!u) {
		rc = errno;
		goto out;
	}
	if (producer) {
		/* add producer */
		mp = calloc(1, sizeof(*mp));
		if (!mp)
			goto enomem;
		mp->is_regex = 1;
		mp->regex_str = strdup(match);
		if (!mp->regex_str)
			goto enomem;
		rc = ldmsd_compile_regex(&mp->regex, mp->regex_str,
					reqc->line_buf, reqc->line_len);
		if (rc) {
			ldmsd_log(LDMSD_LERROR, "Failed to process a cfgupdtr request\n");
			goto err;
		}
		TAILQ_INSERT_TAIL(&u->prdcr_list, mp, entry);
	}
	if (match && regex) {
		/* add matching condition */
		mm = calloc(1, sizeof(*mm));
		if (!mm)
			goto enomem;
		mm->is_regex = 1;
		mm->regex_str = strdup(match);
		if (!mm->regex_str)
			goto enomem;
		rc = ldmsd_compile_regex(&mm->regex, mm->regex_str,
					reqc->line_buf, reqc->line_len);
		if (rc) {
			ldmsd_log(LDMSD_LERROR, "Failed to process a cfgupdtr request\n");
			goto err;
		}
		TAILQ_INSERT_TAIL(&u->match_list, mm, entry);
	}
	goto out;
enomem:
	LDMSD_LOG_ENOMEM();
err:
	ldmsd_name_match_free(mp);
	ldmsd_name_match_free(mm);
out:
	free(name);
	free(interval);
	free(offset);
	free(regex);
	free(match);
	free(push);
	free(producer);
	free(uid);
	free(gid);
	free(perm);
	/* this req need no resp */
	return rc;
}

int tree_failover_cfgupdtr_handler(ldmsd_req_ctxt_t reqc, void *fctxt)
{
	int rc = 0;
	ldmsd_updtr_t updtr;
	struct str_rbn *srbn;
	struct ldmsd_cfgobj_cfg_ctxt *ctxt;
	char *name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	ldmsd_failover_t f = fctxt;

	if (!name || !__name_is_failover(name)) {
		assert(0 == "failover: BAD MESSAGE");
		rc = EINVAL;
		goto out;
	}

	ctxt = malloc(sizeof(*ctxt));
	if (!ctxt) {
		LDMSD_LOG_ENOMEM();
		rc = ENOMEM;
		goto out;
	}
	ref_init(&ctxt->ref, "create", free, ctxt);

	updtr = ldmsd_updtr_find(name);
	if (updtr) {
		ref_get(&ctxt->ref, "updtr_tree");
		ctxt->is_all = 1;
		rc = ldmsd_cfgtree_post2cfgobj(&updtr->obj, updtr_tree_w, updtr->worker, reqc, ctxt);
		if (rc) {
			ldmsd_log(LDMSD_LERROR, "Failed to process a failover cfgupdtr request\n");
			ref_put(&ctxt->ref, "updtr_tree");
			goto err;
		}
	} else {
		/* Create new updtr */
		rc = __failover_updtr_add(reqc);
		if (rc)
			goto err;
		srbn = str_rbn_new(name);
		if (!srbn) {
			LDMSD_LOG_ENOMEM();
			rc = ENOMEM;
			goto err;
		}
		__failover_lock(f);
		rbt_ins(&f->prdcr_rbt, &srbn->rbn);
		__failover_unlock(f);
	}
	goto out;
err:
	ref_put(&ctxt->ref, "create");
out:
	free(name);
	return rc;
}

extern ldmsd_strgp_metric_t strgp_metric_new(const char *metric_name);
static int __failover_strgp_add(ldmsd_req_ctxt_t reqc)
{
	/* create new cfg if not existed, or update if exited */
	char *name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	char *regex = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_REGEX);
	char *metric = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_METRIC);
	char *plugin = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_PLUGIN);
	char *schema = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_SCHEMA);
	char *container = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_CONTAINER);
	char *uid = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_UID);
	char *gid = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_GID);
	char *perm = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_PERM);

	uid_t _uid;
	gid_t _gid;
	mode_t _perm;

	ldmsd_strgp_t s;
	struct ldmsd_sec_ctxt sctxt;
	struct ldmsd_name_match *mp = NULL;
	struct ldmsd_strgp_metric *m = NULL;
	int rc = 0;

	if (!name || !__name_is_failover(name)) {
		__ASSERT(0 == "BAD MESSAGE");
		rc = EINVAL;
		goto out;
	}

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);
	_uid = (uid)?(strtoul(uid, NULL, 0)):(sctxt.crd.uid);
	_gid = (gid)?(strtoul(gid, NULL, 0)):(sctxt.crd.gid);
	_perm = (perm)?(strtoul(perm, NULL, 0)):(0700);
	sctxt.crd.uid = _uid;
	sctxt.crd.gid = _gid;

	/* create */
	/* check if the required parameters are all there */
	if (!schema || !plugin || !container) {
		rc = EINVAL;
		goto out;
	}
	s = ldmsd_strgp_new_with_auth(name, _uid, _gid, _perm);
	if (!s) {
		rc = errno;
		goto out;
	}
	ldmsd_strgp_get(s); /* so that we can `put` without del */
	s->plugin_name = plugin;
	plugin = NULL; /* give plugin to s */
	s->schema = schema;
	schema = NULL;
	s->container = container;
	container = NULL;

	if (regex) {
		/* strgp_prdcr_add */
		mp = calloc(1, sizeof(*mp));
		if (!mp)
			goto enomem;
		mp->regex_str = strdup(regex);
		if (!mp->regex_str)
			goto enomem;
		mp->is_regex = 1;
		rc = ldmsd_compile_regex(&mp->regex, mp->regex_str,
					reqc->line_buf, reqc->line_len);
		if (rc) {
			ldmsd_log(LDMSD_LERROR, "Failed to process a cfgstrgp request\n");
			goto err;
		}
		TAILQ_INSERT_TAIL(&s->prdcr_list, mp, entry);
	}

	if (metric) {
		/* strgp_metric_add */
		m = strgp_metric_new(metric);
		if (!m)
			goto enomem;
		TAILQ_INSERT_TAIL(&s->metric_list, m, entry);
	}
	goto out;
enomem:
	LDMSD_LOG_ENOMEM();
	rc = ENOMEM;
err:
	ldmsd_name_match_free(mp);
out:
	free(name);
	free(regex);
	free(metric);
	free(plugin);
	free(container);
	free(schema);
	free(uid);
	free(gid);
	free(perm);
	/* this req need no resp */
	return rc;
}

int tree_failover_cfgstrgp_handler(ldmsd_req_ctxt_t reqc, void *fctxt)
{
	int rc = 0;
	ldmsd_strgp_t strgp;
	struct ldmsd_cfgobj_cfg_ctxt *ctxt;
	struct str_rbn *srbn;
	char *name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	ldmsd_failover_t f = fctxt;

	if (!name || !__name_is_failover(name)) {
		assert(0 == "failover: BAD MESSAGE");
		rc = EINVAL;
		goto out;
	}

	ctxt = malloc(sizeof(*ctxt));
	if (!ctxt) {
		LDMSD_LOG_ENOMEM();
		rc = ENOMEM;
		goto out;
	}
	ref_init(&ctxt->ref, "create", free, ctxt);

	strgp = ldmsd_strgp_find(name);
	if (strgp) {
		ref_get(&ctxt->ref, "strgp_tree");
		ctxt->is_all = 1;
		rc = ldmsd_cfgtree_post2cfgobj(&strgp->obj, strgp_tree_w, strgp->worker, reqc, ctxt);
		if (rc) {
			ldmsd_log(LDMSD_LERROR, "Failed to process a failover cfgstrgp request\n");
			ref_put(&ctxt->ref, "strgp_tree");
			goto err;
		}
	} else {
		/* Create new prdcr */
		rc = __failover_strgp_add(reqc);
		if (rc)
			goto err;
		srbn = str_rbn_new(name);
		if (!srbn) {
			LDMSD_LOG_ENOMEM();
			rc = ENOMEM;
			goto err;
		}
		__failover_lock(f);
		rbt_ins(&f->prdcr_rbt, &srbn->rbn);
		__failover_unlock(f);

	}
out:
	free(name);
	return rc;
err:
	ref_put(&ctxt->ref, "create");
	goto out;
}





struct ldmsd_cfgobj_cfg_rsp *
prdcr_failover_peercfg_handler(ldmsd_prdcr_t prdcr, void *cfg_ctxt)
{
	int rc;
	struct ldmsd_cfgobj_cfg_rsp *rsp;
	struct ldmsd_cfgobj_cfg_ctxt *ctxt = cfg_ctxt;
	ldmsd_req_ctxt_t reqc = ctxt->reqc;
	struct fo_peercfg_ctxt *fo_ctxt = (struct fo_peercfg_ctxt *)reqc->ctxt;

	rsp = calloc(1, sizeof(*rsp));
	if (!rsp)
		return NULL;
	rc = __failover_send_prdcr(NULL, fo_ctxt->x, prdcr);
	rsp->errcode = rc;
	return rsp;
}

extern int __prdcr_stream(ldmsd_prdcr_t prdcr, char *stream_name,
					enum ldmsd_request req_id,
					struct ldmsd_cfgobj_cfg_rsp *rsp);
struct ldmsd_cfgobj_cfg_rsp *
prdcr_failover_cfgprdcr_handler(ldmsd_prdcr_t prdcr, void *cfg_ctxt)
{
	struct ldmsd_cfgobj_cfg_rsp *rsp;
	struct ldmsd_cfgobj_cfg_ctxt *ctxt = cfg_ctxt;
	ldmsd_req_ctxt_t reqc = ctxt->reqc;

	/* Update prdcr */

	rsp = malloc(sizeof(*rsp));
	if (!rsp)
		goto enomem;

	char *interval = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_INTERVAL);
	char *stream = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_STREAM);

	if (interval)
		prdcr->conn_intrvl_us = atoi(interval);
	if (stream) {
		rsp->errcode = __prdcr_stream(prdcr, stream,
						LDMSD_PRDCR_SUBSCRIBE_REQ, rsp);
		if (ENOMEM == rsp->errcode)
			goto enomem;
	}

	return rsp;
enomem:
	LDMSD_LOG_ENOMEM();
	free(rsp);
	return NULL;
}

struct ldmsd_cfgobj_cfg_rsp *
updtr_failover_peercfg_handler(ldmsd_updtr_t updtr, struct ldmsd_cfgobj_cfg_ctxt *cfg_ctxt)
{
	int rc;
	struct ldmsd_cfgobj_cfg_rsp *rsp;
	struct ldmsd_cfgobj_cfg_ctxt *ctxt = cfg_ctxt;
	ldmsd_req_ctxt_t reqc = ctxt->reqc;
	struct fo_peercfg_ctxt *fo_ctxt = (struct fo_peercfg_ctxt *)reqc->ctxt;

	rsp = calloc(1, sizeof(*rsp));
	if (!rsp)
		return NULL;
	rc = __failover_send_updtr(NULL, fo_ctxt->x, updtr);
	rsp->errcode = rc;
	return rsp;
}

struct ldmsd_cfgobj_cfg_rsp *
updtr_failover_cfgupdtr_handler(ldmsd_updtr_t updtr, struct ldmsd_cfgobj_cfg_ctxt *cfg_ctxt)
{
	int rc;
	struct ldmsd_cfgobj_cfg_rsp *rsp;
	struct ldmsd_cfgobj_cfg_ctxt *ctxt = cfg_ctxt;
	ldmsd_req_ctxt_t reqc = ctxt->reqc;
	struct ldmsd_name_match *mp, *mm;
	mp = mm = NULL;

	rsp = malloc(sizeof(*rsp));
	if (!rsp)
		goto enomem;

	char *interval = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_INTERVAL);
	char *offset = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_OFFSET);
	char *push = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_PUSH);
	char *producer = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_PRODUCER);
	char *match = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_MATCH);
	char *regex = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_REGEX);

	if (interval)
		updtr->sched.intrvl_us = atoi(interval);
	if (offset)
		updtr->sched.offset_us = atoi(offset);
	if (push) {
		if (0 == strcasecmp("onchange", push)) {
			updtr->push_flags = LDMSD_UPDTR_F_PUSH |
						LDMSD_UPDTR_F_PUSH_CHANGE;
		} else {
			updtr->push_flags = LDMSD_UPDTR_F_PUSH;
		}
	}
	if (producer) {
		/* add producer */
		mp = calloc(1, sizeof(*mp));
		if (!mp)
			goto enomem;
		mp->is_regex = 1;
		mp->regex_str = strdup(match);
		if (!mp->regex_str)
			goto enomem;
		rc = ldmsd_compile_regex(&mp->regex, mp->regex_str,
					reqc->line_buf, reqc->line_len);
		if (rc) {
			rsp->errcode = rc;
			asprintf(&rsp->errmsg, "Failed to process a cfgupdtr request.");
			goto out;
		}
		TAILQ_INSERT_TAIL(&updtr->prdcr_list, mp, entry);
	}
	if (match && regex) {
		/* add matching condition */
		mm = calloc(1, sizeof(*mm));
		if (!mm)
			goto enomem;
		mm->is_regex = 1;
		mm->regex_str = strdup(match);
		if (!mm->regex_str)
			goto enomem;
		rc = ldmsd_compile_regex(&mm->regex, mm->regex_str,
					reqc->line_buf, reqc->line_len);
		if (rc) {
			rsp->errcode = rc;
			asprintf(&rsp->errmsg, "Failed to process a cfgupdtr request.");
			goto out;
		}
		TAILQ_INSERT_TAIL(&updtr->match_list, mm, entry);
	}

out:
	free(interval);
	free(offset);
	free(push);
	free(producer);
	free(match);
	free(regex);
	ldmsd_name_match_free(mp);
	ldmsd_name_match_free(mm);
	return rsp;
enomem:
	LDMSD_LOG_ENOMEM();
	free(rsp);
	return NULL;
}

struct ldmsd_cfgobj_cfg_rsp *
strgp_failover_peercfg_handler(ldmsd_strgp_t strgp, struct ldmsd_cfgobj_cfg_ctxt *cfg_ctxt)
{
	int rc;
	struct ldmsd_cfgobj_cfg_rsp *rsp;
	struct ldmsd_cfgobj_cfg_ctxt *ctxt = cfg_ctxt;
	ldmsd_req_ctxt_t reqc = ctxt->reqc;
	struct fo_peercfg_ctxt *fo_ctxt = (struct fo_peercfg_ctxt *)reqc->ctxt;

	rsp = calloc(1, sizeof(*rsp));
	if (!rsp)
		return NULL;
	rc = __failover_send_strgp(NULL, fo_ctxt->x, strgp);
	rsp->errcode = rc;
	return rsp;
}

struct ldmsd_cfgobj_cfg_rsp *
strgp_failover_cfgstrgp_handler(ldmsd_strgp_t strgp, struct ldmsd_cfgobj_cfg_ctxt *cfg_ctxt)
{
	int rc;
	struct ldmsd_cfgobj_cfg_rsp *rsp;
	struct ldmsd_cfgobj_cfg_ctxt *ctxt = cfg_ctxt;
	ldmsd_req_ctxt_t reqc = ctxt->reqc;
	struct ldmsd_name_match *mp = NULL;
	struct ldmsd_strgp_metric *m = NULL;

	/* Update prdcr */

	rsp = malloc(sizeof(*rsp));
	if (!rsp)
		goto enomem;

	char *regex = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_REGEX);
	char *metric = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_METRIC);

	if (regex) {
		/* strgp_prdcr_add */
		mp = calloc(1, sizeof(*mp));
		if (!mp)
			goto enomem;
		mp->regex_str = strdup(regex);
		if (!mp->regex_str)
			goto enomem;
		mp->is_regex = 1;
		rc = ldmsd_compile_regex(&mp->regex, mp->regex_str,
					reqc->line_buf, reqc->line_len);
		if (rc) {
			rsp->errcode = rc;
			asprintf(&rsp->errmsg, "Failed to process a cfgstrgp request.");
			goto out;
		}
		TAILQ_INSERT_TAIL(&strgp->prdcr_list, mp, entry);
	}

	if (metric) {
		/* strgp_metric_add */
		m = strgp_metric_new(metric);
		if (!m)
			goto enomem;
		TAILQ_INSERT_TAIL(&strgp->metric_list, m, entry);
	}
out:
	free(regex);
	free(metric);
	ldmsd_name_match_free(mp);
	return rsp;
enomem:
	LDMSD_LOG_ENOMEM();
	free(regex);
	free(metric);
	free(rsp);
	return NULL;
}

int tree_failover_peercfg_rsp_handler(ldmsd_req_ctxt_t reqc,
					     struct ldmsd_cfgobj_cfg_rsp *rsp,
					     struct ldmsd_cfgobj_cfg_ctxt *ctxt,
					     enum ldmsd_cfgobj_type type)
{
	ev_t ev;
	ev_worker_t src;
	struct fo_peercfg_ctxt *fo_ctxt = reqc->ctxt;
	uint8_t *is_cfgobj_error;
	uint8_t *is_cfgobj_done;

	switch (type) {
	case LDMSD_CFGOBJ_PRDCR:
		is_cfgobj_error = &fo_ctxt->is_prdcr_error;
		is_cfgobj_done = &fo_ctxt->is_prdcr_done;
		src = prdcr_tree_w;
		break;
	case LDMSD_CFGOBJ_UPDTR:
		is_cfgobj_error = &fo_ctxt->is_updtr_error;
		is_cfgobj_done = &fo_ctxt->is_updtr_done;
		src = updtr_tree_w;
		break;
	case LDMSD_CFGOBJ_STRGP:
		is_cfgobj_error = &fo_ctxt->is_strgp_error;
		is_cfgobj_done = &fo_ctxt->is_strgp_done;
		src = strgp_tree_w;
		break;
	default:
		break;
	}
	if (rsp->errcode)
		*is_cfgobj_error = EINTR;
	free(rsp->errmsg);
	free(rsp);

	if (!ldmsd_cfgtree_done(ctxt))
		return 0;

	*is_cfgobj_done = 1;

	ev = ev_new(cfgobj_rsp_type);
	if (!ev)
		return ENOMEM;
	EV_DATA(ev, struct cfgobj_rsp_data)->rsp = NULL;
	EV_DATA(ev, struct cfgobj_rsp_data)->ctxt = reqc;
	return ev_post(src, failover_w, ev, 0);
}

int tree_failover_cfgobj_rsp_handler(ldmsd_req_ctxt_t reqc,
					     struct ldmsd_cfgobj_cfg_rsp *rsp,
					     struct ldmsd_cfgobj_cfg_ctxt *ctxt)
{
	if (rsp->errcode) {
		ldmsd_log(LDMSD_LERROR, "Failover: %s\n", rsp->errmsg);
		free(rsp->errmsg);
	}
	ref_put(&ctxt->ref, "create");
	free(rsp);
	return 0;
}

__attribute__((constructor))
void __failover_init_once()
{
	__failover_init(&__failover);
}
