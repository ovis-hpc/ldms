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

#include <assert.h>
#include <pthread.h>
#include <netdb.h>

#include "coll/rbt.h"
#include "ovis_event/ovis_event.h"

#include "ldmsd.h"
#include "ldmsd_request.h"

#include "config.h"

#define DEFAULT_HEARTBEAT 1000000 /* unit: uSec */
#define DEFAULT_AUTOSWITCH 1
#define DEFAULT_TIMEOUT_FACTOR 2

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
	__FAILOVER_CONFIGURED  =  0x1,
	__FAILOVER_ACTIVATED   =  0x2,
	__FAILOVER_NOHB_ARMED  =  0x4,
	/* for ax state */
	__FAILOVER_AX_DISCONNECTED  =  0x00,
	__FAILOVER_AX_CONNECTING    =  0x08,
	__FAILOVER_AX_CONNECTED     =  0x10,
	__FAILOVER_AX_MASK = __FAILOVER_AX_CONNECTING|__FAILOVER_AX_CONNECTED,
} __failover_flags_t;

#define __AX_GET(f) ((f)->flags & __FAILOVER_AX_MASK)
#define __AX_SET(f, val) do {\
		(f)->flags = (((f)->flags & ~__FAILOVER_AX_MASK) | \
			((val) & __FAILOVER_AX_MASK)); \
	} while(0)

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
	uint64_t interval;
	double timeout_factor;
	pthread_mutex_t mutex;
	ldms_t ax; /* active xprt */
	struct ovis_event_s hb_ev; /* For active side (sending heartbeat) */
	struct ovis_event_s no_hb_ev; /* For passive side (heartbeat timeout) */

	/* store redundant pdrcr and updtr names instead of relying on cfgobj
	 * tree so that we don't have to mess with cfgobj global locks */
	struct rbt prdcr_rbt;
	struct rbt updtr_rbt;
} *ldmsd_failover_t;

struct str_rbn {
	struct rbn rbn;
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
static ovis_scheduler_t sched;
static pthread_t sched_thread;

static
void __failover_print(ldmsd_failover_t f)
{
	int i;
	static uint64_t fl[] = {
		__FAILOVER_CONFIGURED,
		__FAILOVER_ACTIVATED,
		__FAILOVER_NOHB_ARMED,
		__FAILOVER_AX_CONNECTING,
		__FAILOVER_AX_CONNECTED,
	};
	static const char *fls[] = {
		"CONFIGURED",
		"ACTIVATED",
		"NOHB_ARMED",
		"AX_CONNECTING",
		"AX_CONNECTED",
	};
	printf("-- failover info %p:\n", f);
	printf("    flags:\n");
	for (i = 0; i < (sizeof(fl)/sizeof(*fl)); i++) {
		printf("        %s: %d\n", fls[i], !!__F_GET(f, fl[i]));
	}
	printf("    host: %s\n", f->host);
	printf("    port: %s\n", f->port);
	printf("    auto_switch: %d\n", f->auto_switch);
	printf("    interval: %ld\n", f->interval);
}

/* for debugging */
void print_failover_info()
{
	__failover_print(&__failover);
}

static
void __failover_hb_ev_cb(ovis_event_t oev);
static
void __failover_no_hb_ev_cb(ovis_event_t oev);

static
int __failover_do_failover(ldmsd_failover_t f);
static
int __failover_do_failback(ldmsd_failover_t f);

void __failover_set_hb_timeout(ldmsd_failover_t f, uint64_t usec)
{
	f->no_hb_ev.param.timeout.tv_sec = usec / 1000000;
	f->no_hb_ev.param.timeout.tv_usec = usec % 1000000;
}

void __failover_set_interval(ldmsd_failover_t f, uint64_t i)
{
	if (!i)
		i = DEFAULT_HEARTBEAT;
	f->interval = i;
	f->hb_ev.param.periodic.period_us = i;
	f->hb_ev.param.periodic.phase_us = 0;
	__failover_set_hb_timeout(f, f->timeout_factor * i);
}

void __failover_init(ldmsd_failover_t f)
{
	bzero(f, sizeof(*f));
	pthread_mutex_init(&f->mutex, NULL);
	f->flags = 0;

	f->interval = DEFAULT_HEARTBEAT;
	f->auto_switch = DEFAULT_AUTOSWITCH;

	rbt_init(&f->prdcr_rbt, str_rbn_cmp);
	rbt_init(&f->updtr_rbt, str_rbn_cmp);

	OVIS_EVENT_INIT(&f->no_hb_ev);
	f->no_hb_ev.param.type = OVIS_EVENT_TIMEOUT;
	f->no_hb_ev.param.cb_fn = __failover_no_hb_ev_cb;
	f->no_hb_ev.param.ctxt = f;

	OVIS_EVENT_INIT(&f->hb_ev);
	f->hb_ev.param.type = OVIS_EVENT_PERIODIC;
	f->hb_ev.param.cb_fn = __failover_hb_ev_cb;
	f->hb_ev.param.ctxt = f;

	f->timeout_factor = DEFAULT_TIMEOUT_FACTOR;

	__failover_set_interval(f, DEFAULT_HEARTBEAT);
}

static inline
struct ldmsd_sec_ctxt __get_sec_ctxt(struct ldmsd_req_ctxt *req)
{
	struct ldmsd_sec_ctxt sctxt;
	if (req) {
		ldms_xprt_cred_get(req->xprt->xprt, NULL, &sctxt.crd);
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

static
int __failover_no_hb_reset(ldmsd_failover_t f)
{
	/* f->lock is held */
	int rc;
	if (__F_GET(f, __FAILOVER_NOHB_ARMED)) {
		/* removed before re-arm */
		rc = ovis_scheduler_event_del(sched, &f->no_hb_ev);
		__ASSERT(rc == 0);
	}
	rc = ovis_scheduler_event_add(sched, &f->no_hb_ev);
	__ASSERT(rc == 0);
	if (!rc)
		__F_ON(f, __FAILOVER_NOHB_ARMED);
	return rc;
}

static
int __failover_send_prdcr(ldmsd_failover_t f, ldms_t x, ldmsd_prdcr_t p)
{
	/* f->lock is held */
	int rc = 0;
	ldmsd_req_cmd_t rcmd;
	char buff[128];
	const char *cstr;

	if (__cfgobj_is_failover(&p->obj)) {
		rc = EINVAL;
		goto out;
	}

	rcmd = ldmsd_req_cmd_new(x, LDMSD_FAILOVER_CFGPRDCR_REQ,
				   NULL, NULL, NULL);
	if (!rcmd) {
		rc = errno;
		goto out;
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

	/* Terminate the message */
	rc = ldmsd_req_cmd_attr_term(rcmd);
	if (rc)
		goto cleanup;

	/* let-through for cleanup */
cleanup:
	ldmsd_req_cmd_free(rcmd);
out:
	return rc;
}

static
int __failover_send_updtr(ldmsd_failover_t f, ldms_t x, ldmsd_updtr_t u)
{
	/* NOTE: Send a bunch of small messages due to msg size limitation. */
	int rc = 0;
	char buff[128];
	const char *cstr;
	ldmsd_prdcr_ref_t pref;
	ldmsd_name_match_t nm;
	ldmsd_prdcr_t p;
	ldmsd_req_cmd_t rcmd;
	struct rbn *rbn;

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
	snprintf(buff, sizeof(buff), "%ld", u->default_task.hint.intrvl_us);
	rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_INTERVAL, buff);
	if (rc)
		goto cleanup;

	/* OFFSET */
	if (u->default_task.task_flags & LDMSD_TASK_F_SYNCHRONOUS) {
		snprintf(buff, sizeof(buff), "%ld", u->default_task.hint.offset_us);
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

	/* TERM */
	rc = ldmsd_req_cmd_attr_term(rcmd);
	if (rc)
		goto cleanup;
	ldmsd_req_cmd_free(rcmd);
	rcmd = NULL;

	/* list of PRODUCERs in this updater */
	for (rbn = rbt_min(&u->prdcr_tree); rbn; rbn = rbn_succ(rbn)) {
		pref = container_of(rbn, struct ldmsd_prdcr_ref, rbn);
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
		p = pref->prdcr;
		/* PRODUCER */
		snprintf(buff, sizeof(buff),
			 LDMSD_FAILOVER_NAME_PREFIX "%s", p->obj.name);
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
	LIST_FOREACH(nm, &u->match_list, entry) {
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
int __failover_send_reset(ldmsd_failover_t f, ldms_t xprt)
{
	/* f->lock is held */
	int rc;
	ldmsd_req_cmd_t rcmd;

	rcmd = ldmsd_req_cmd_new(xprt, LDMSD_FAILOVER_RESET_REQ,
				 NULL, NULL, NULL);
	if (!rcmd) {
		rc = errno;
		goto out;
	}
	/* reset has no attribute */
	rc = ldmsd_req_cmd_attr_term(rcmd);
	__ASSERT(rc == 0);
out:
	if (rcmd)
		ldmsd_req_cmd_free(rcmd);
	return rc;
}

static
int __failover_send_cfgobjs(ldmsd_failover_t f, ldms_t x)
{
	/* f->lock is held */
	ldmsd_prdcr_t p;
	ldmsd_updtr_t u;
	int rc = 0;

	/* Send PRDCR update */
	ldmsd_cfg_lock(LDMSD_CFGOBJ_PRDCR);
	for (p = ldmsd_prdcr_first(); p; p = ldmsd_prdcr_next(p)) {
		if (__cfgobj_is_failover(&p->obj))
			continue;
		rc = __failover_send_prdcr(f, x, p);
		__ASSERT(rc == 0); /* for testing/debugging */
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
		if (__cfgobj_is_failover(&u->obj))
			continue;
		rc = __failover_send_updtr(f, x, u);
		__ASSERT(rc == 0);
		if (rc) {
			ldmsd_updtr_put(u);
			ldmsd_cfg_unlock(LDMSD_CFGOBJ_UPDTR);
			goto out;
		}
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_UPDTR);

out:
	return rc;
}

static
int __on_pair_resp(ldmsd_req_cmd_t rcmd)
{
	ldmsd_failover_t f = rcmd->ctxt;
	ldmsd_req_hdr_t hdr = (void*)rcmd->reqc->req_buf;

	__failover_lock(f);
	__ASSERT(__AX_GET(f) == __FAILOVER_AX_CONNECTING);
	if (hdr->rsp_err) {
		ldmsd_linfo("Failover pairing error: %d\n", hdr->rsp_err);
		ldms_xprt_close(f->ax);
		/* the disconnect path will handle the state change */
	} else {
		__AX_SET(f, __FAILOVER_AX_CONNECTED);
	}
	__failover_unlock(f);
	return 0;
}

static
void __failover_on_active_connected(ldmsd_failover_t f)
{
	/* f->lock is held */
	ldmsd_req_cmd_t rcmd;
	const char *myname;
	int rc;

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
	/* rcmd will be freed when the reply is received */
	return;
err:
	if (rcmd)
		ldmsd_req_cmd_free(rcmd);
	/* no need to change the state .. the xprt event will drive the state
	 * change later */
	ldms_xprt_close(f->ax);
}

static
void __failover_active_xprt_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg)
{
	ldmsd_failover_t f = cb_arg;
	switch (e->type) {
	case LDMS_XPRT_EVENT_CONNECTED:
		__failover_lock(f);
		__ASSERT(__AX_GET(f) == __FAILOVER_AX_CONNECTING);
		__failover_on_active_connected(f);
		__failover_unlock(f);
		break;
	case LDMS_XPRT_EVENT_DISCONNECTED:
	case LDMS_XPRT_EVENT_ERROR:
	case LDMS_XPRT_EVENT_REJECTED:
		__failover_lock(f);
		__AX_SET(f, __FAILOVER_AX_DISCONNECTED);
		ldms_xprt_put(f->ax);
		f->ax = NULL;
		__failover_unlock(f);
		break;
	case LDMS_XPRT_EVENT_RECV:
                ldmsd_recv_msg(x, e->data, e->data_len);
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
	__dlog("Failover connecting, flags: %#lx\n", f->flags);
	__ASSERT(f->ax == NULL);
	__ASSERT(__AX_GET(f) == __FAILOVER_AX_DISCONNECTED);
	f->ax = ldms_xprt_new_with_auth(f->xprt, ldmsd_linfo,
					auth_name, auth_opt);
	if (!f->ax) {
		rc = errno;
		goto out;
	}
	rc = ldms_xprt_connect_by_name(f->ax, f->host, f->port,
				       __failover_active_xprt_cb, f);
	if (rc)
		goto err1;
	__AX_SET(f, __FAILOVER_AX_CONNECTING);
	goto out;
err1:
	ldms_xprt_put(f->ax);
	f->ax = NULL;
	__AX_SET(f, __FAILOVER_AX_DISCONNECTED);
out:
	__dlog("__failover_active_connect() rc: %d, flags: %#lx\n", rc, f->flags);
	return rc;
}

static
void __failover_send_heartbeat(ldmsd_failover_t f)
{
	int buff[sizeof(struct ldmsd_req_hdr_s)/sizeof(int) + 2];
	struct ldmsd_req_hdr_s *hb = (void*)buff;
	int rc, len;

	hb->marker = LDMSD_RECORD_MARKER;
	hb->type = LDMSD_REQ_TYPE_CONFIG_CMD;
	hb->flags = LDMSD_REQ_SOM_F|LDMSD_REQ_EOM_F;
	hb->req_id = LDMSD_FAILOVER_HEARTBEAT_REQ;
	/* terminate the message */
	*(int*)&hb[1] = 0;
	len = hb->rec_len = sizeof(*hb) + sizeof(int);
	ldmsd_hton_req_msg(hb);

	__dlog("Sending HB, flags: %#lx\n", f->flags);

	rc = ldms_xprt_send(f->ax, (void*)hb, len);
	if (rc) {
		ldmsd_linfo("heartbeat send failed, rc: %d\n", rc);
	}
}

static
void __failover_hb_ev_cb(ovis_event_t oev)
{
	ldmsd_failover_t f = oev->param.ctxt;
	__failover_lock(f);
	switch (__AX_GET(f)) {
	case __FAILOVER_AX_DISCONNECTED:
		/* connect to peer */
		(void)__failover_active_connect(f);
		break;
	case __FAILOVER_AX_CONNECTING:
		/* do nothing */
		break;
	case __FAILOVER_AX_CONNECTED:
		/* send heartbeat */
		__failover_send_heartbeat(f);
		break;
	default:
		__ASSERT(0 == "UNKNOWN STATE");
	}
	__failover_unlock(f);
}

static
void __failover_no_hb_ev_cb(ovis_event_t oev)
{
	/* Heartbeat timeout */
	int rc;
	ldmsd_failover_t f = oev->param.ctxt;

	__failover_lock(f);

	__dlog("NOHB ev triggered, flags: %#lx\n", f->flags);

	/* Disable heartbeat timeout */
	/* oev is &f->no_hb_ev */
	__ASSERT(oev == &f->no_hb_ev);
	rc = ovis_scheduler_event_del(sched, oev);
	__ASSERT(rc == 0);
	__F_OFF(f, __FAILOVER_NOHB_ARMED);

	if (f->auto_switch) {
		(void)__failover_do_failover(f);
	}

	__failover_unlock(f);
}

static
void *__failover_sched_proc(void *arg)
{
	int rc;
	rc = ovis_scheduler_loop(sched, 0);
	ldmsd_log(LDMSD_LDEBUG, "Failover thread exiting, rc: %d\n", rc);
	return NULL;
}

static
int __failover_sched_init()
{
	static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
	static int once = 0;
	int rc = 0;
	pthread_mutex_lock(&mutex);
	if (once)
		goto out;

	rc = pthread_create(&sched_thread, NULL, __failover_sched_proc, NULL);
	if (rc)
		goto out;

	once = 1;
out:
	pthread_mutex_unlock(&mutex);
	return rc;
}

static
int __failover_start(ldmsd_failover_t f)
{
	/* f->lock is held */

	int rc = 0;

	rc = __failover_sched_init();
	if (rc)
		goto out;

	__ASSERT(__AX_GET(f) == __FAILOVER_AX_DISCONNECTED);
	rc = __failover_active_connect(f);
	if (rc) {
		goto out;
	}

	/* Start hb_ev for heartbeat */
	rc = ovis_scheduler_event_add(sched, &f->hb_ev);
	__ASSERT(rc == 0);
	if (rc) {
		ldmsd_log(LDMSD_LINFO, "ldmsd_failover: active event_add failed"
				       ", rc: %d\n", rc);
		goto out;
	}

out:
	return rc;
}

static
int __failover_do_failover(ldmsd_failover_t f)
{
	/* f->lock is held */
	int rc = 0;
	struct rbn *rbn;
	struct ldmsd_sec_ctxt sctxt = __get_sec_ctxt(NULL);

	__dlog("Executing failover, flags: %#lx\n", f->flags);
	if (f->flags & __FAILOVER_ACTIVATED) {
		rc = EINVAL;
		goto out;
	}

	RBT_FOREACH(rbn, &f->prdcr_rbt) {
		rc = ldmsd_prdcr_start(STR_RBN(rbn)->str, NULL, &sctxt);
		__ASSERT(rc == 0);
	}

	RBT_FOREACH(rbn, &f->updtr_rbt) {
		rc = ldmsd_updtr_start(STR_RBN(rbn)->str, NULL, NULL, NULL, &sctxt);
		__ASSERT(rc == 0);
	}

	__F_ON(f, __FAILOVER_ACTIVATED);
out:
	__dlog("__failover_do_failover(), flags: %#lx, rc: %d\n", f->flags, rc);
	return rc;
}

static
int __failover_do_failback(ldmsd_failover_t f)
{
	/* f->lock is held. */
	int rc = 0;
	struct rbn *rbn;
	struct ldmsd_sec_ctxt sctxt = __get_sec_ctxt(NULL);

	__dlog("Executing failback, flags: %#lx\n", f->flags);

	if (0 == (f->flags & __FAILOVER_ACTIVATED)) {
		rc = EINVAL;
		goto out;
	}

	RBT_FOREACH(rbn, &f->prdcr_rbt) {
		rc = ldmsd_prdcr_stop(STR_RBN(rbn)->str, &sctxt);
		__ASSERT(rc == 0);
	}

	RBT_FOREACH(rbn, &f->updtr_rbt) {
		rc = ldmsd_updtr_stop(STR_RBN(rbn)->str, &sctxt);
		__ASSERT(rc == 0);
	}

	__F_OFF(f, __FAILOVER_ACTIVATED);
out:
	__dlog("__failover_do_failback(), flags: %#lx, rc: %d\n", f->flags, rc);
	return rc;
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
	char buff[64];
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
	if (f->flags & __FAILOVER_CONFIGURED) {
		rc = EEXIST;
		errmsg = "failover cannot be reconfigured";
		goto out;
	}
	if (!host || !port || !xprt) {
		rc = EINVAL;
		errmsg = "missing host, port or xprt attribute";
		goto out;
	}
	len = snprintf(f->host, sizeof(f->host), "%s", host);
	if (len >= sizeof(f->host)) {
		rc = ENAMETOOLONG;
		goto out;
	}
	len = snprintf(f->port, sizeof(f->port), "%s", port);
	if (len >= sizeof(f->port)) {
		rc = ENAMETOOLONG;
		goto out;
	}
	len = snprintf(f->xprt, sizeof(f->xprt), "%s", xprt);
	if (len >= sizeof(f->xprt)) {
		rc = ENAMETOOLONG;
		goto out;
	}
	if (peer_name) {
		len = snprintf(f->peer_name, sizeof(f->peer_name),
			       "%s", peer_name);
		if (len >= sizeof(f->peer_name)) {
			rc = ENAMETOOLONG;
			errmsg = "";
		}
	} else {
		f->peer_name[0] = 0;
		/* accept all */
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
		__failover_set_interval(f, strtoul(interval, NULL, 0));
	}

	rc = __failover_start(f);
	if (rc) {
		snprintf(buff, sizeof(buff), "failover start failed: %d\n", rc);
		errmsg = buff;
		goto out;
	}

	__F_ON(f, __FAILOVER_CONFIGURED);

out:
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
		__FAILOVER_ACTIVATED,
		__FAILOVER_NOHB_ARMED,
		__FAILOVER_AX_CONNECTING,
		__FAILOVER_AX_CONNECTED,
	};
	static const char *fls[] = {
		"CONFIGURED",
		"ACTIVATED",
		"NOHB_ARMED",
		"AX_CONNECTING",
		"AX_CONNECTED",
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
	__APPEND(", \"interval\": \"%ld\"", f->interval);
	__APPEND(", \"timeout_factor\": \"%lf\"", f->timeout_factor);
	__APPEND(", \"auto_switch\": \"%d\"", f->auto_switch);
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

int failover_do_failover_handler(ldmsd_req_ctxt_t req)
{
	int rc = 0;
	const char *errmsg = NULL;
	char buff[32];
	ldmsd_failover_t f = __ldmsd_req_failover_get(req);

	__failover_lock(f);

	rc = __failover_do_failover(f);
	if (rc) {
		snprintf(buff, sizeof(buff), "error: %d\n", rc);
		errmsg = buff;
	}

	__failover_unlock(f);
	req->errcode = rc;
	ldmsd_send_req_response(req, errmsg);
	return rc;
}

int failover_do_failback_handler(ldmsd_req_ctxt_t req)
{
	int rc = 0;
	const char *errmsg = NULL;
	char buff[32];
	ldmsd_failover_t f = __ldmsd_req_failover_get(req);

	__failover_lock(f);

	rc = __failover_do_failback(f);
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
	if (!(f->flags & __FAILOVER_CONFIGURED)) {
		rc = EAGAIN; /* we are not ready */
		goto err1;
	}

	rc = __verify_pair_req(f, req);
	if (rc)
		goto err1;
	/* pairing accepted */
	if ((f->flags & __FAILOVER_ACTIVATED) && f->auto_switch) {
		__failover_do_failback(f);
	}
	req->errcode = rc;
	ldmsd_send_req_response(req, NULL);

	/* then, request a reset and share our objects */
	rc = __failover_send_reset(f, req->xprt->ldms.ldms);
	__ASSERT(rc == 0);
	rc = __failover_send_cfgobjs(f, req->xprt->ldms.ldms);
	__ASSERT(rc == 0);
	__failover_unlock(f);
	return 0;

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
	struct rbn *rbn;
	struct ldmsd_sec_ctxt sctxt = __get_sec_ctxt(req);
	int rc;

	f = __ldmsd_req_failover_get(req);
	if (!f)
		return ENOENT;
	__failover_lock(f);
	if (f->flags & __FAILOVER_ACTIVATED)
		(void)__failover_do_failback(f); /* intentionally ignore rc */
	RBT_FOREACH(rbn, &f->updtr_rbt) {
		rc = ldmsd_updtr_del(STR_RBN(rbn)->str, &sctxt);
		__ASSERT(rc == 0);
	}
	RBT_FOREACH(rbn, &f->prdcr_rbt) {
		rc = ldmsd_prdcr_del(STR_RBN(rbn)->str, &sctxt);
		__ASSERT(rc == 0);
	}
	__failover_unlock(f);
	/* this req needs no resp */
	return 0;
}

int failover_cfgprdcr_handler(ldmsd_req_ctxt_t req)
{
	/* create new cfg if not existed, or update if exited */

	ldmsd_failover_t f = __ldmsd_req_failover_get(req);

	char *name = __req_attr_gets(req, LDMSD_ATTR_NAME);
	char *host = __req_attr_gets(req, LDMSD_ATTR_HOST);
	char *port = __req_attr_gets(req, LDMSD_ATTR_PORT);
	char *xprt = __req_attr_gets(req, LDMSD_ATTR_XPRT);
	char *interval = __req_attr_gets(req, LDMSD_ATTR_INTERVAL);
	char *type = __req_attr_gets(req, LDMSD_ATTR_TYPE);

	enum ldmsd_prdcr_type ptype;
	ldmsd_prdcr_t p;
	struct str_rbn *srbn;
	struct ldmsd_sec_ctxt sctxt = __get_sec_ctxt(req);
	int rc = 0;

	__failover_lock(f);

	if (!name || !__name_is_failover(name)) {
		__ASSERT(0 == "BAD MESSAGE");
		rc = EINVAL;
		goto out;
	}

	p = ldmsd_prdcr_find(name);
	if (p) {
		/* update (only interval) */
		p->conn_intrvl_us = atoi(interval);
		ldmsd_prdcr_put(p);
		goto out;
	}
	/* create */
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
	srbn = str_rbn_new(name);
	if (!srbn) {
		rc = errno;
		goto out;
	}
	p = ldmsd_prdcr_new_with_auth(name, xprt, host, atoi(port), ptype,
			atoi(interval), sctxt.crd.uid, sctxt.crd.gid, 0700);
	if (!p) {
		rc = errno;
		str_rbn_free(srbn);
		goto out;
	}
	rbt_ins(&f->prdcr_rbt, &srbn->rbn);
out:
	__failover_unlock(f);
	if (name)
		free(name);
	if (host)
		free(host);
	if (port)
		free(port);
	if (xprt)
		free(xprt);
	if (interval)
		free(interval);
	if (type)
		free(type);
	/* this req needs no resp */
	return rc;
}

int __ldmsd_updtr_prdcr_add(ldmsd_updtr_t updtr, ldmsd_prdcr_t prdcr);

int failover_cfgupdtr_handler(ldmsd_req_ctxt_t req)
{
	/* create new cfg if not existed, or update if exited */
	int push_flags = 0;
	ldmsd_failover_t f = __ldmsd_req_failover_get(req);

	char *name = __req_attr_gets(req, LDMSD_ATTR_NAME);
	char *interval = __req_attr_gets(req, LDMSD_ATTR_INTERVAL);
	char *offset = __req_attr_gets(req, LDMSD_ATTR_OFFSET);
	char *regex = __req_attr_gets(req, LDMSD_ATTR_REGEX);
	char *match = __req_attr_gets(req, LDMSD_ATTR_MATCH);
	char *push = __req_attr_gets(req, LDMSD_ATTR_PUSH);
	char *producer = __req_attr_gets(req, LDMSD_ATTR_PRODUCER);
	char *auto_interval = __req_attr_gets(req, LDMSD_ATTR_AUTO_INTERVAL);

	ldmsd_updtr_t u;
	ldmsd_prdcr_t p;
	struct str_rbn *srbn;
	struct ldmsd_sec_ctxt sctxt = __get_sec_ctxt(req);
	int rc = 0;
	uint8_t is_auto_interval = (auto_interval)?0:1;

	__failover_lock(f);

	if (!name || !__name_is_failover(name)) {
		__ASSERT(0 == "BAD MESSAGE");
		rc = EINVAL;
		goto out;
	}

	if (push) {
		if (0 == strcasecmp("onchange", push)) {
			push_flags = LDMSD_UPDTR_F_PUSH |
					LDMSD_UPDTR_F_PUSH_CHANGE;
		} else {
			push_flags = LDMSD_UPDTR_F_PUSH;
		}
	}

	u = ldmsd_updtr_find(name);
	if (!u) {
		/* create */
		srbn = str_rbn_new(name);
		if (!srbn)
			goto out;
		u = ldmsd_updtr_new_with_auth(name, interval, offset,
						push_flags, is_auto_interval,
						sctxt.crd.uid, sctxt.crd.gid, 0700);
		if (!u) {
			rc = errno;
			str_rbn_free(srbn);
			goto out;
		}
		rbt_ins(&f->updtr_rbt, &srbn->rbn);
		ldmsd_updtr_get(u); /* so that we can `put` without del */
	} else {
		/* update by parameters */
		if (interval) {
			u->default_task.hint.intrvl_us = atoi(interval);
		}
		if (offset) {
			u->default_task.hint.offset_us = atoi(offset);
			u->default_task.task_flags = LDMSD_TASK_F_SYNCHRONOUS;
		}
		u->push_flags = push_flags;
		if (push_flags)
			u->default_task.task_flags = LDMSD_TASK_F_IMMEDIATE;
	}
	if (producer) {
		/* add producer */
		p = ldmsd_prdcr_find(producer);
		if (!p) {
			rc = ENOENT;
			goto updtr_put;
		}
		rc = __ldmsd_updtr_prdcr_add(u, p);
		ldmsd_prdcr_put(p);
	}
	if (match && regex) {
		/* add matching condition */
		rc = ldmsd_updtr_match_add(name, regex, match, req->line_buf,
					   req->line_len, &sctxt);
	}
updtr_put:
	ldmsd_updtr_put(u);
out:
	__failover_unlock(f);
	if (name)
		free(name);
	if (interval)
		free(interval);
	if (offset)
		free(offset);
	if (regex)
		free(regex);
	if (match)
		free(match);
	if (push)
		free(push);
	if (producer)
		free(producer);
	/* this req need no resp */
	return rc;
}

int failover_heartbeat_handler(ldmsd_req_ctxt_t req)
{
	int rc;
	ldmsd_failover_t f;

	f = __ldmsd_req_failover_get(req);
	if (!f)
		return ENOENT;

	/* heart beat received ... reset the timeout event */
	__failover_lock(f);
	__dlog("HB received, flags: %#lx\n", f->flags);
	rc = __failover_no_hb_reset(f);
	__ASSERT(rc == 0);
	if (rc)
		goto out;
	if (f->auto_switch && (f->flags & __FAILOVER_ACTIVATED)) {
		/* need failback */
		rc = __failover_do_failback(f);
		__ASSERT(rc == 0);
	}
out:
	__failover_unlock(f);
	return rc;
}

__attribute__((constructor))
void __failover_init_once()
{
	sched = ovis_scheduler_new();
	__ASSERT(sched);
	__failover_init(&__failover);
}
