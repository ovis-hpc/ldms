/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2018,2023 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2018,2023 Open Grid Computing, Inc. All rights reserved.
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

#include <assert.h>
#include <pthread.h>
#include <netdb.h>
#include <stdint.h>
#include <math.h>

#include "coll/rbt.h"
#include "ovis_event/ovis_event.h"

#include "ldmsd.h"
#include "ldmsd_request.h"

#include "ldms_rail.h"

#include "config.h"

#define DEFAULT_PING_INTERVAL 1000000 /* unit: uSec */
#define DEFAULT_AUTOSWITCH 1
#define DEFAULT_TIMEOUT_FACTOR 2

/* Defined in ldmsd.c */
extern ovis_log_t fo_log;

#define ARRAY_LEN(x) (sizeof(x)/sizeof(*(x)))

/* Use -L 8 or set ldmsd_req_debug |= 8 to enable failover messages */
/* static int failover_debug; */

#define __ASSERT(x) assert(x)

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

static inline
int __name_is_failover(const char *name)
{
	return 0 == strncmp(LDMSD_FAILOVER_NAME_PREFIX, name,
			    sizeof(LDMSD_FAILOVER_NAME_PREFIX)-1);
}

static inline
int __cfgobj_is_failover(ldmsd_cfgobj_t obj)
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

static int __on_reset_resp(ldmsd_req_cmd_t rcmd)
{
	int rc = 0;
	ldmsd_failover_t f = rcmd->ctxt;
	ldmsd_req_hdr_t hdr = (void*)rcmd->reqc->req_buf;
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
		__dlog(DLOG_FOVER,"(DEBUG) ERROR: Outstanding unpair.\n");
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

int __on_peercfg_resp(ldmsd_req_cmd_t rcmd)
{
	ldmsd_failover_t f = rcmd->ctxt;
	ldmsd_req_hdr_t hdr = (void*)rcmd->reqc->req_buf;
	__failover_lock(f);
	if (hdr->rsp_err) {
		ovis_log(fo_log, OVIS_LERROR, "peer config request remote error: %d\n",
			     hdr->rsp_err);
		f->conn_state = FAILOVER_CONN_STATE_ERROR;
		ldms_xprt_close(f->ax);
	} else {
		/* all peercfg have been received at this point */
		__F_ON(f, __FAILOVER_PEERCFG_RECEIVED);
		f->conn_state = FAILOVER_CONN_STATE_CONFIGURED;
		ovis_log(fo_log, OVIS_LINFO, "peer config recv success\n");
	}
	__failover_unlock(f);
	return 0;
}

static
int __failover_request_peercfg(ldmsd_failover_t f)
{
	/* f->lock is held */
	ldmsd_req_cmd_t rcmd;
	int rc;

	ovis_log(fo_log, OVIS_LINFO, "requesting peer config\n");

	rcmd = ldmsd_req_cmd_new(f->ax, LDMSD_FAILOVER_PEERCFG_REQ,
				 NULL, __on_peercfg_resp, f);
	if (!rcmd) {
		rc = errno;
		goto out;
	}
	rc = ldmsd_req_cmd_attr_term(rcmd);
out:
	if (rc) {
		ovis_log(fo_log, OVIS_LERROR, "peer config request local error: %d\n",
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

static
int __on_pair_resp(ldmsd_req_cmd_t rcmd)
{
	ldmsd_failover_t f = rcmd->ctxt;
	ldmsd_req_hdr_t hdr = (void*)rcmd->reqc->req_buf;
	int rc = 0;

	__failover_lock(f);
	__ASSERT(f->conn_state == FAILOVER_CONN_STATE_PAIRING);

	if (hdr->rsp_err) {
		if (hdr->rsp_err == EAGAIN) {
			f->conn_state = FAILOVER_CONN_STATE_PAIRING_RETRY;
			goto out; /* the task will try pairing again */
		}
		ovis_log(fo_log, OVIS_LERROR, "Failover pairing error: %d\n", hdr->rsp_err);
		rc = hdr->rsp_err;
		goto err;
	}
	ovis_log(fo_log, OVIS_LINFO, "Failover pairing success, peer: %s\n", f->peer_name);

	f->conn_state = FAILOVER_CONN_STATE_RESETTING;
	rc = __failover_reset_and_request_peercfg(f);

err:
	if (rc) {
		f->conn_state = FAILOVER_CONN_STATE_ERROR;
		ldms_xprt_close(f->ax);
		/* the disconnect path will handle the state change */
	}
out:
	__failover_unlock(f);
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

	ovis_log(fo_log, OVIS_LINFO, "Failover pairing with peer: %s\n", f->peer_name);

	/* just become connected ... request pairing */
	myname = ldmsd_myname_get();
	rcmd = ldmsd_req_cmd_new(f->ax, LDMSD_FAILOVER_PAIR_REQ,
				 NULL, __on_pair_resp, f);
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
	ovis_log(fo_log, OVIS_LINFO, "Failover pairing error (local), rc: %d\n", rc);
	if (rcmd)
		ldmsd_req_cmd_free(rcmd);
	f->conn_state = FAILOVER_CONN_STATE_ERROR;
	/* no need to change the state .. the xprt event will drive the state
	 * change later */
	ldms_xprt_close(f->ax);
}

static
void __failover_xprt_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg)
{
	int need_start;
	ldmsd_failover_t f = cb_arg;
	switch (e->type) {
	case LDMS_XPRT_EVENT_CONNECTED:
		__dlog(DLOG_FOVER,"Failover: xprt connected\n");
		ldms_xprt_priority_set(f->ax, 1);
		__failover_lock(f);
		/* so that retry operations can follow up not too slow */
		f->task_interval = 1000000;
		__failover_task_resched(f);
		__ASSERT(f->conn_state == FAILOVER_CONN_STATE_CONNECTING);
		__failover_pair(f);
		__failover_unlock(f);
		break;
	case LDMS_XPRT_EVENT_DISCONNECTED:
		__dlog(DLOG_FOVER,"Failover: xprt disconnected\n");
	case LDMS_XPRT_EVENT_ERROR:
	case LDMS_XPRT_EVENT_REJECTED:
		__failover_lock(f);
		f->task_interval = f->ping_interval;
		__failover_task_resched(f);
		f->conn_state = FAILOVER_CONN_STATE_DISCONNECTED;
		ldms_xprt_put(f->ax, "rail_ref");
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
		ldmsd_recv_msg(x, e->data, e->data_len);
		break;
	case LDMS_XPRT_EVENT_SET_DELETE:
	case LDMS_XPRT_EVENT_SEND_COMPLETE:
		/* no-op */
		break;
	default:
		__ASSERT(0 == "Unknown Event");
	}
}

extern const char *auth_name;
extern struct attr_value_list *auth_opt;

static
int __failover_active_connect(ldmsd_failover_t f)
{
	/* f->lock is held */
	int rc = 0;
	__dlog(DLOG_FOVER,"Failover: connecting, flags: %#lx\n", f->flags);
	__ASSERT(f->ax == NULL);
	__ASSERT(f->conn_state == FAILOVER_CONN_STATE_DISCONNECTED);
	f->ax = ldms_xprt_new_with_auth(f->xprt, auth_name, auth_opt);
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
	ldms_xprt_put(f->ax, "rail_ref");
	f->ax = NULL;
	f->conn_state = FAILOVER_CONN_STATE_DISCONNECTED;
out:
	__dlog(DLOG_FOVER,"Failover: __failover_active_connect() rc: %d, flags: %#lx\n",
	       rc, f->flags);
	return rc;
}


static
void __ping_stat_dlog(ldmsd_failover_t f)
{
	/* f->lock is held */
	__dlog(DLOG_FOVER,"Failover stat: number of pings: %d\n", f->ping_n);
	__dlog(DLOG_FOVER,"Failover stat: ping MAX: %ld\n", f->ping_max);
	__dlog(DLOG_FOVER,"Failover stat: ping AVG: %ld\n", f->ping_avg);
	__dlog(DLOG_FOVER,"Failover stat: ping SSE: %lf\n", f->ping_sse);
	__dlog(DLOG_FOVER,"Failover stat: ping SD: %lf\n", f->ping_sd);
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

	__dlog(DLOG_FOVER,"Failover: PING resp recv\n");

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
	attr = ldmsd_req_attr_get_by_id(rcmd->reqc->req_buf, LDMSD_ATTR_UDATA);
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

	__dlog(DLOG_FOVER,"Failover: timeout (duration): %lu\n", dur);

	tv.tv_sec = dur / 1000000;
	tv.tv_usec = dur % 1000000;
	timeradd(&f->echo_ts, &tv, &f->timeout_ts);
	__dlog(DLOG_FOVER,"Failover: timeout (timestamp): %lu.%06lu\n",
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
		__dlog(DLOG_FOVER,"Failover: OUTSTANDING_PING ... will not ping\n");
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

static
int __peercfg_delete(ldmsd_failover_t f)
{
	/* f->lock is held */
	int rc, i;
	struct rbn *rbn;
	struct str_rbn *ent;
	struct ldmsd_sec_ctxt sctxt = __get_sec_ctxt(NULL);
	struct rbt *t[] = {&f->strgp_rbt, &f->updtr_rbt, &f->prdcr_rbt};
	void *del[] = {ldmsd_strgp_del, ldmsd_updtr_del, ldmsd_prdcr_del};
	int (*fn)(void*, void*);

	ovis_log(fo_log, OVIS_LINFO, "deleting peer config\n");

	/* cfgobjs have all already stopped */
	for (i = 0; i < 3; i++) {
		fn = del[i];
		while ((rbn = rbt_min(t[i]))) {
			ent = STR_RBN(rbn);
			rc = fn(ent->str, &sctxt);
			if (rc) {
				ovis_log(fo_log, OVIS_LINFO, "peer config deletion "
					    "failed, rc: %d\n", rc);
				return rc;
			}
			rbt_del(t[i], rbn);
			str_rbn_free((void*)rbn);
		}
	}

	ovis_log(fo_log, OVIS_LINFO, "peer config deleted\n");

	return 0;
}

static
int __peercfg_stop(ldmsd_failover_t f)
{
	/* f->lock is held */
	int rc = 0, _rc, i;
	struct rbn *rbn;
	struct str_rbn *ent;
	struct ldmsd_sec_ctxt sctxt = __get_sec_ctxt(NULL);
	struct rbt *t[] = {&f->updtr_rbt, &f->prdcr_rbt};
	void *stop[] = {ldmsd_updtr_stop, ldmsd_prdcr_stop};
	int (*fn)(void*, void*);

	/* NOTE: Leaving out peer storage policy in the favor of letting our
	 *       storage policy picks up the data. */

	ovis_log(fo_log, OVIS_LINFO, "stopping peercfg\n");
	for (i = 0; i < ARRAY_LEN(t); i++) {
		fn = stop[i];
		RBT_FOREACH(rbn, t[i]) {
			ent = STR_RBN(rbn);
			if (!ent->started)
				continue;
			_rc = fn(ent->str, &sctxt);
			if (0 == _rc)
				ent->started = 0;
			else
				rc = EAGAIN;
		}
	}

	if (rc) {
		ovis_log(fo_log, OVIS_LINFO, "peer config stopping failed: %d\n", rc);
	} else {
		ovis_log(fo_log, OVIS_LINFO, "peer config stopped\n");
		f->timeout_ts.tv_sec = INT64_MAX;
		__F_OFF(f, __FAILOVER_PEERCFG_ACTIVATED);
	}

	return rc;
}

static
int __peercfg_reset(ldmsd_failover_t f)
{
	/* f->lock is held */
	ovis_log(fo_log, OVIS_LINFO, "resetting peer config\n");
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

static
void __failover_active_routine(ldmsd_failover_t f);
static
void __failover_task(ldmsd_task_t task, void *arg)
{
	ldmsd_failover_t f = arg;
	__failover_lock(f);
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
	if (f->state != FAILOVER_STATE_STOP) {
		__failover_task_resched(f);
	}
	__failover_unlock(f);
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

	if (!__F_GET(f, __FAILOVER_PEERCFG_RECEIVED))
		return; /* no peercfg ... can't do anything */

	/* check timeout */
	gettimeofday(&tv, NULL);
	if (timercmp(&tv, &f->timeout_ts, <))
		return; /* not timeout yet */

	/* timeout, start peercfg */
	__peercfg_start(f);
}

static
void __failover_active_routine(ldmsd_failover_t f)
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
	struct ldmsd_sec_ctxt sctxt = __get_sec_ctxt(NULL);
	int is_activated = 1;

	ovis_log(fo_log, OVIS_LINFO, "starting peercfg, flags: %#lo\n", f->flags);

	RBT_FOREACH(rbn, &f->prdcr_rbt) {
		srbn = STR_RBN(rbn);
		if (srbn->started)
			continue;
		rc = ldmsd_prdcr_start(srbn->str, NULL, &sctxt);
		if (rc) {
			ovis_log(fo_log, OVIS_LERROR,
				  "failover: prdcr_start(%s) failed, "
				  "rc: %d\n", srbn->str, rc);
			continue;
		}
		srbn->started = 1;
		is_activated = 1;
	}

	RBT_FOREACH(rbn, &f->updtr_rbt) {
		srbn = STR_RBN(rbn);
		if (srbn->started)
			continue;
		rc = ldmsd_updtr_start(srbn->str, NULL, NULL, NULL, &sctxt);
		if (rc) {
			ovis_log(fo_log, OVIS_LERROR,
				  "failover: updtr_start(%s) failed, "
				  "rc: %d\n", srbn->str, rc);
			continue;
		}
		srbn->started = 1;
		is_activated = 1;
	}

	if (is_activated)
		__F_ON(f, __FAILOVER_PEERCFG_ACTIVATED);
	__dlog(DLOG_FOVER,"Failover: __peercfg_start(), flags: %#lx, rc: %d\n",
	       f->flags, rc);
	return 0;
}

/* ===============================
 * LDMSD Failover Request Handlers
 * ===============================
 * (This is a set of passive-side routines)
 */
static inline
char *__req_attr_gets(ldmsd_req_ctxt_t req, enum ldmsd_request_attr aid)
{
	return ldmsd_req_attr_str_value_get_by_id(req, aid);
}

int __failover_noop_handler(ldmsd_req_ctxt_t req)
{
	const char *errmsg = "Pairwise failover is deplicated.";
	req->errcode = ENOTSUP;
	ldmsd_send_req_response(req, errmsg);
	return 0;
}

int failover_config_handler(ldmsd_req_ctxt_t req)
{
	return __failover_noop_handler(req);
}

int failover_mod_handler(ldmsd_req_ctxt_t req)
{
	return __failover_noop_handler(req);
}

int failover_status_handler(ldmsd_req_ctxt_t req)
{
	return __failover_noop_handler(req);
}

int failover_start_handler(ldmsd_req_ctxt_t req)
{
	return __failover_noop_handler(req);
}

int failover_stop_handler(ldmsd_req_ctxt_t req)
{
	return __failover_noop_handler(req);
}

int failover_peercfg_start_handler(ldmsd_req_ctxt_t req)
{
	return __failover_noop_handler(req);
}

int failover_peercfg_stop_handler(ldmsd_req_ctxt_t req)
{
	return __failover_noop_handler(req);
}

int failover_pair_handler(ldmsd_req_ctxt_t req)
{
	return __failover_noop_handler(req);
}

int failover_reset_handler(ldmsd_req_ctxt_t req)
{
	return __failover_noop_handler(req);
}

int failover_cfgprdcr_handler(ldmsd_req_ctxt_t req)
{
	return __failover_noop_handler(req);
}

int failover_cfgupdtr_handler(ldmsd_req_ctxt_t req)
{
	return __failover_noop_handler(req);
}

int failover_cfgstrgp_handler(ldmsd_req_ctxt_t req)
{
	return __failover_noop_handler(req);
}

int failover_ping_handler(ldmsd_req_ctxt_t req)
{
	return __failover_noop_handler(req);
}

int failover_peercfg_handler(ldmsd_req_ctxt_t req)
{
	return __failover_noop_handler(req);
}

int ldmsd_failover_start()
{
	int rc;
	ldmsd_failover_t f;
	f = &__failover;
	__failover_lock(f);
	if (f->state != FAILOVER_STATE_STOP) {
		rc = EBUSY;
		goto out;
	}
	f->task_interval = f->ping_interval;
	rc = ldmsd_task_start(&f->task, __failover_task, f,
			      LDMSD_TASK_F_IMMEDIATE, f->task_interval, 0);
	if (rc)
		goto out;

	f->state = FAILOVER_STATE_START;
	/* allows only failover-safe and failover-internal commands */
	ldmsd_inband_cfg_mask_set(LDMSD_PERM_FAILOVER_ALLOWED |
				  LDMSD_PERM_FAILOVER_INTERNAL);
out:
	__failover_unlock(f);
	return rc;
}

__attribute__((constructor))
void __failover_init_once()
{
	__failover_init(&__failover);
}
