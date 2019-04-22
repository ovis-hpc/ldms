/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2015-2018 Open Grid Computing, Inc. All rights reserved.
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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <coll/rbt.h>
#include <ovis_util/util.h>
#include <netdb.h>
#include <ev/ev.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldms_xprt.h"
#include "ldmsd_request.h"
#include "config.h"

int prdcr_resolve(const char *hostname, unsigned short port_no,
		  struct sockaddr_storage *ss, socklen_t *ss_len)
{
	struct hostent *h;

	h = gethostbyname(hostname);
	if (!h)
		return -1;

	if (h->h_addrtype != AF_INET)
		return -1;

	memset(ss, 0, sizeof *ss);
	struct sockaddr_in *sin = (struct sockaddr_in *)ss;
	sin->sin_addr.s_addr = *(unsigned int *)(h->h_addr_list[0]);
	sin->sin_family = h->h_addrtype;
	sin->sin_port = htons(port_no);
	*ss_len = sizeof(*sin);
	return 0;
}

void ldmsd_prdcr___del(ldmsd_cfgobj_t obj)
{
	ldmsd_prdcr_t prdcr = (ldmsd_prdcr_t)obj;
	if (prdcr->host_name)
		free(prdcr->host_name);
	if (prdcr->xprt_name)
		free(prdcr->xprt_name);
	if (prdcr->conn_auth)
		free(prdcr->conn_auth);
	if (prdcr->conn_auth_args)
		av_free(prdcr->conn_auth_args);
	ldmsd_cfgobj___del(obj);
}

static void __prdcr_set_del(ldmsd_prdcr_set_t set)
{
	ref_dump_no_lock(&set->ref, __func__);
	ldmsd_log(LDMSD_LINFO, "Deleting producer set %s\n", set->inst_name);
	if (set->schema_name) {
		free(set->schema_name);
	}
	if (set->set) {
		ldms_set_delete(set->set);
	}
	ldmsd_strgp_ref_t strgp_ref;
	strgp_ref = LIST_FIRST(&set->strgp_list);
	while (strgp_ref) {
		LIST_REMOVE(strgp_ref, entry);
		ldmsd_strgp_put(strgp_ref->strgp);
		free(strgp_ref);
		strgp_ref = LIST_FIRST(&set->strgp_list);
	}

	free(set->inst_name);
	free(set);
}

static ldmsd_prdcr_set_t prdcr_set_new(const char *inst_name)
{
	ldmsd_prdcr_set_t set = calloc(1, sizeof *set);
	if (!set)
		goto err_0;
	set->state = LDMSD_PRDCR_SET_STATE_START;
	set->inst_name = strdup(inst_name);
	if (!set->inst_name)
		goto err_1;
	pthread_mutex_init(&set->lock, NULL);
	rbn_init(&set->rbn, set->inst_name);

	set->state_ev = ev_new(prdcr_set_state_type);
	EV_DATA(set->state_ev, struct state_data)->prd_set = set;
	set->update_ev = ev_new(prdcr_set_update_type);
	EV_DATA(set->update_ev, struct update_data)->prd_set = set;

	ref_init(&set->ref, "create", (ref_free_fn_t)__prdcr_set_del, set);
	return set;
err_1:
	free(set);
err_0:
	return NULL;
}

static void prdcr_set_del(ldmsd_prdcr_set_t set)
{
	ref_dump(&set->ref, __func__);
	ldmsd_prdcr_set_ref_put(set, "create");
}

/**
 * Destroy all sets for the producer
 */
static void prdcr_reset_sets(ldmsd_prdcr_t prdcr)
{
	ldmsd_prdcr_set_t prd_set;
	struct rbn *rbn;
	while ((rbn = rbt_min(&prdcr->set_tree))) {
		prd_set = container_of(rbn, struct ldmsd_prdcr_set, rbn);
		EV_DATA(prd_set->state_ev, struct state_data)->start_n_stop = 0;
		ldmsd_prdcr_set_ref_get(prd_set, "state_ev");
		if (ev_post(producer, updater, prd_set->state_ev, NULL)) {
			ldmsd_prdcr_set_ref_put(prd_set, "state_ev");
		}
		if (prd_set->push_flags & LDMSD_PRDCR_SET_F_PUSH_REG) {
			/* Put back the reference taken when register for push */
			ldmsd_prdcr_set_ref_put(prd_set, "push");
		}
		rbt_del(&prdcr->set_tree, rbn);
		prdcr_set_del(prd_set);
	}
}

/**
 * Find the prdcr_set with the matching instance name
 *
 * Must be called with the prdcr->lock held.
 */
static ldmsd_prdcr_set_t _find_set(ldmsd_prdcr_t prdcr, const char *inst_name)
{
	struct rbn *rbn = rbt_find(&prdcr->set_tree, inst_name);
	if (rbn)
		return container_of(rbn, struct ldmsd_prdcr_set, rbn);
	return NULL;
}

/* Caller must hold prdcr lock and prdcr_set lock */
static void prdcr_set_updt_hint_update(ldmsd_prdcr_t prdcr, ldmsd_prdcr_set_t prd_set)
{
	struct rbn *rbn;
	ldmsd_updt_hint_set_list_t list;
	char *value;
	ldms_set_t set = prd_set->set;

	ldms_set_info_unset(set, LDMSD_SET_INFO_UPDATE_HINT_KEY);
	(void) ldmsd_set_update_hint_get(prd_set->set,
			&prd_set->updt_hint.intrvl_us, &prd_set->updt_hint.offset_us);

	return;
}

static void prdcr_lookup_cb(ldms_t xprt, enum ldms_lookup_status status,
			    int more, ldms_set_t set, void *arg)
{
	ldmsd_prdcr_set_t prd_set = arg;
	pthread_mutex_lock(&prd_set->lock);
	if (status != LDMS_LOOKUP_OK) {
		status = (status < 0 ? -status : status);
		if (status == ENOMEM) {
			ldmsd_log(LDMSD_LERROR,
				"Error %d in lookup callback for set '%s' "
				"Consider changing the -m parameter on the "
				"command line to a bigger value. "
				"The current value is %s\n",
				status, prd_set->inst_name,
				ldmsd_get_max_mem_sz_str());

		} else {
			ldmsd_log(LDMSD_LERROR,
				  "Error %d in lookup callback for set '%s'\n",
					  status, prd_set->inst_name);
		}
		prd_set->state = LDMSD_PRDCR_SET_STATE_START;
		goto out;
	}
	if (!prd_set->set) {
		/* This is the first lookup of the set. */
		prd_set->set = set;
		prd_set->schema_name = strdup(ldms_set_schema_name_get(set));
		ldms_ctxt_set(set, &prd_set->set_ctxt);
	}

	/*
	 * Unlocking producer set to lock producer and then producer set
	 * for locking order consistency
	 */
	pthread_mutex_unlock(&prd_set->lock);

	ldmsd_prdcr_lock(prd_set->prdcr);
	pthread_mutex_lock(&prd_set->lock);
	prdcr_set_updt_hint_update(prd_set->prdcr, prd_set);
	pthread_mutex_unlock(&prd_set->lock);
	ldmsd_prdcr_unlock(prd_set->prdcr);

	pthread_mutex_lock(&prd_set->lock);

	prd_set->state = LDMSD_PRDCR_SET_STATE_READY;
	EV_DATA(prd_set->state_ev, struct state_data)->start_n_stop = 1;
	ldmsd_prdcr_set_ref_get(prd_set, "state_ev");
	ldmsd_prdcr_set_ref_put(prd_set, "xprt_lookup");
	if (ev_post(producer, updater, prd_set->state_ev, NULL))
		ldmsd_prdcr_set_ref_put(prd_set, "state_ev");

	ldmsd_log(LDMSD_LINFO, "Set %s is ready\n", prd_set->inst_name);
	ldmsd_strgp_update(prd_set);
out:
	pthread_mutex_unlock(&prd_set->lock);
	return;
}

static void _add_cb(ldms_t xprt, ldmsd_prdcr_t prdcr, const char *inst_name)
{
	ldmsd_prdcr_set_t set;
	int rc;

	ldmsd_log(LDMSD_LINFO, "Adding the metric set '%s'\n", inst_name);

	/* Check to see if it's already there */
	set = _find_set(prdcr, inst_name);
	if (!set) {
		set = prdcr_set_new(inst_name);
		if (!set) {
			ldmsd_log(LDMSD_LERROR, "Memory allocation failure in %s "
				 "for set_name %s\n",
				 __FUNCTION__, inst_name);
			return;
		}
		set->prdcr = prdcr;
		rbt_ins(&prdcr->set_tree, &set->rbn);
	} else {
		ldmsd_log(LDMSD_LCRITICAL, "Receive a duplicated dir_add update of "
				"the set '%s'.\n", inst_name);
		return;
	}
	ldmsd_prdcr_set_ref_get(set, "xprt_lookup");
	/* Refresh the set with a lookup */
	rc = ldms_xprt_lookup(prdcr->xprt, inst_name,
			      LDMS_LOOKUP_BY_INSTANCE | LDMS_LOOKUP_SET_INFO,
			      prdcr_lookup_cb, set);
	if (rc) {
		ldmsd_log(LDMSD_LINFO, "Synchronous error %d from ldms_lookup\n", rc);
		ldmsd_prdcr_set_ref_put(set, "xprt_lookup");
	}

}

/*
 * Process the directory list and add or restore specified sets.
 */
static void prdcr_dir_cb_add(ldms_t xprt, ldms_dir_t dir, ldmsd_prdcr_t prdcr)
{
	int i;
	for (i = 0; i < dir->set_count; i++)
		_add_cb(xprt, prdcr, dir->set_names[i]);
}

static void prdcr_dir_cb_list(ldms_t xprt, ldms_dir_t dir, ldmsd_prdcr_t prdcr)
{
	return prdcr_dir_cb_add(xprt, dir, prdcr);
}

/*
 * Process the directory list and release the deleted sets
 */
static void prdcr_dir_cb_del(ldms_t xprt, ldms_dir_t dir, ldmsd_prdcr_t prdcr)
{
	ldmsd_prdcr_set_t set;
	int i;

	for (i = 0; i < dir->set_count; i++) {
		struct rbn *rbn = rbt_find(&prdcr->set_tree, dir->set_names[i]);
		if (!rbn)
			continue;
		set = container_of(rbn, struct ldmsd_prdcr_set, rbn);

		// UNSHARE ldms_xprt_set_release(xprt, set->set);
		rbt_del(&prdcr->set_tree, &set->rbn);
		prdcr_set_del(set);
	}
}

static void prdcr_dir_cb_upd(ldms_t xprt, ldms_dir_t dir, ldmsd_prdcr_t prdcr)
{
	ldmsd_prdcr_set_t set;
	int i, rc;

	for (i = 0; i < dir->set_count; i++) {
		set = ldmsd_prdcr_set_find(prdcr, dir->set_names[i]);
		if (!set) {
                        /* Received an update, but the set is gone. */
                        ldmsd_log(LDMSD_LERROR,
                                  "Ignoring 'dir update' for the set, '%s', which "
                                  "is not present in the prdcr_set tree.\n",
                                  dir->set_names[i]);
                        continue;
		}
		ldmsd_prdcr_set_ref_get(set, "xprt_lookup");
		pthread_mutex_lock(&set->lock);
		set->state = LDMSD_PRDCR_SET_STATE_START;
		pthread_mutex_unlock(&set->lock);
		rc = ldms_xprt_lookup(xprt, set->inst_name, LDMS_LOOKUP_SET_INFO,
						prdcr_lookup_cb, (void *)set);
		if (rc) {
			ldmsd_log(LDMSD_LINFO, "Synchronous error %d from "
					"		SET_INFO lookup\n", rc);
			ldmsd_prdcr_set_ref_put(set, "xprt_lookup");
		}
	}
}

/*
 * The ldms_dir has completed. Decode the directory type and call the
 * appropriate handler function.
 */
static void prdcr_dir_cb(ldms_t xprt, int status, ldms_dir_t dir, void *arg)
{
	ldmsd_prdcr_t prdcr = arg;
	if (status) {
		ldmsd_log(LDMSD_LINFO, "Error %d in dir on producer %s host %s.\n",
			 status, prdcr->obj.name, prdcr->host_name);
		return;
	}
	ldmsd_prdcr_lock(prdcr);
	switch (dir->type) {
	case LDMS_DIR_LIST:
		prdcr_dir_cb_list(xprt, dir, prdcr);
		break;
	case LDMS_DIR_ADD:
		prdcr_dir_cb_add(xprt, dir, prdcr);
		break;
	case LDMS_DIR_DEL:
		prdcr_dir_cb_del(xprt, dir, prdcr);
		break;
	case LDMS_DIR_UPD:
		prdcr_dir_cb_upd(xprt, dir, prdcr);
		break;
	}
	ldmsd_prdcr_unlock(prdcr);
	ldms_xprt_dir_free(xprt, dir);
}

static int __on_subs_resp(ldmsd_req_cmd_t rcmd)
{
	return 0;
}

/* Send subscribe request to peer */
static int __prdcr_subscribe(ldmsd_prdcr_t prdcr)
{
	ldmsd_req_cmd_t rcmd;
	int rc;
	ldmsd_prdcr_stream_t s;
	LIST_FOREACH(s, &prdcr->stream_list, entry) {
		rcmd = ldmsd_req_cmd_new(prdcr->xprt, LDMSD_STREAM_SUBSCRIBE_REQ,
					 NULL, __on_subs_resp, prdcr);
		rc = errno;
		if (!rcmd)
			goto err_0;
		rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_NAME, s->name);
		if (rc)
			goto err_1;
		rc = ldmsd_req_cmd_attr_term(rcmd);
		if (rc)
			goto err_1;
	}
	return 0;
 err_1:
	ldmsd_req_cmd_free(rcmd);
 err_0:
	return rc;
}

static void prdcr_connect_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg)
{
	struct timespec to;
	ldmsd_prdcr_t prdcr = cb_arg;
	ldmsd_prdcr_lock(prdcr);
	switch (e->type) {
	case LDMS_XPRT_EVENT_CONNECTED:
		ldmsd_log(LDMSD_LINFO, "Producer %s is connected (%s %s:%d)\n",
				prdcr->obj.name, prdcr->xprt_name,
				prdcr->host_name, (int)prdcr->port_no);
		prdcr->conn_state = LDMSD_PRDCR_STATE_CONNECTED;
		if (__prdcr_subscribe(prdcr)) {
			ldmsd_log(LDMSD_LERROR, "Could not subscribe to stream data on producer %s\n",
				  prdcr->obj.name);
		}
		if (ldms_xprt_dir(prdcr->xprt, prdcr_dir_cb, prdcr,
				  LDMS_DIR_F_NOTIFY))
			ldms_xprt_close(prdcr->xprt);
		// ldmsd_task_stop(&prdcr->task);
		break;
	case LDMS_XPRT_EVENT_REJECTED:
		ldmsd_log(LDMSD_LERROR, "Producer %s rejected the "
				"connection (%s %s:%d)\n", prdcr->obj.name,
				prdcr->xprt_name, prdcr->host_name,
				(int)prdcr->port_no);
		goto reset_prdcr;
	case LDMS_XPRT_EVENT_DISCONNECTED:
		ldmsd_log(LDMSD_LINFO, "Producer %s is disconnected (%s %s:%d)\n",
				prdcr->obj.name, prdcr->xprt_name,
				prdcr->host_name, (int)prdcr->port_no);
		goto reset_prdcr;
	case LDMS_XPRT_EVENT_ERROR:
		ldmsd_log(LDMSD_LINFO, "Producer %s: connection error to %s %s:%d\n",
				prdcr->obj.name, prdcr->xprt_name,
				prdcr->host_name, (int)prdcr->port_no);
		goto reset_prdcr;
	case LDMS_XPRT_EVENT_RECV:
		ldmsd_recv_msg(x, e->data, e->data_len);
		break;
	default:
		assert(0);
	}
	ldmsd_prdcr_unlock(prdcr);
	return;

reset_prdcr:
	prdcr_reset_sets(prdcr);
	if (prdcr->xprt)
		ldms_xprt_put(prdcr->xprt);
	prdcr->xprt = NULL;
	switch (prdcr->conn_state) {
	case LDMSD_PRDCR_STATE_STOPPING:
		prdcr->conn_state = LDMSD_PRDCR_STATE_STOPPED;
		break;
	case LDMSD_PRDCR_STATE_DISCONNECTED:
	case LDMSD_PRDCR_STATE_CONNECTING:
	case LDMSD_PRDCR_STATE_CONNECTED:
		prdcr->conn_state = LDMSD_PRDCR_STATE_DISCONNECTED;
		ev_sched_to(&to, prdcr->conn_intrvl_us / 1000000, 0);
		ev_post(producer, producer, prdcr->connect_ev, &to);
		break;
	case LDMSD_PRDCR_STATE_STOPPED:
		assert(0 == "STOPPED shouldn't have xprt event");
		break;
	}
	ldmsd_prdcr_unlock(prdcr);
}

static void prdcr_connect(ldmsd_prdcr_t prdcr)
{
	int ret;
	char *auth;
	struct attr_value_list *auth_opts;

	assert(prdcr->xprt == NULL);
	switch (prdcr->type) {
	case LDMSD_PRDCR_TYPE_ACTIVE:
		if (!prdcr->conn_auth) {
			/* Get the default auth and its options */
			auth = (char *)ldmsd_auth_name_get(NULL);
			auth_opts = ldmsd_auth_attr_get(NULL);
		} else {
			auth = prdcr->conn_auth;
			auth_opts = prdcr->conn_auth_args;
		}
		prdcr->conn_state = LDMSD_PRDCR_STATE_CONNECTING;
		prdcr->xprt = ldms_xprt_new_with_auth(prdcr->xprt_name, ldmsd_linfo,
					auth, auth_opts);
		if (prdcr->xprt) {
			ret  = ldms_xprt_connect(prdcr->xprt,
						 (struct sockaddr *)&prdcr->ss,
						 prdcr->ss_len,
						 prdcr_connect_cb, prdcr);
			if (ret) {
				ldms_xprt_put(prdcr->xprt);
				prdcr->xprt = NULL;
				prdcr->conn_state = LDMSD_PRDCR_STATE_DISCONNECTED;
			}
		} else {
			ldmsd_log(LDMSD_LERROR, "%s Error %d: creating endpoint on transport '%s'.\n",
				 __func__, errno, prdcr->xprt_name);
			prdcr->conn_state = LDMSD_PRDCR_STATE_DISCONNECTED;
		}
		break;
	case LDMSD_PRDCR_TYPE_PASSIVE:
		prdcr->xprt = ldms_xprt_by_remote_sin((struct sockaddr_in *)&prdcr->ss);
		/* Call connect callback to advance state and update timers*/
		if (prdcr->xprt)
			prdcr_connect_cb(prdcr->xprt, LDMS_XPRT_EVENT_CONNECTED, prdcr);
		break;
	case LDMSD_PRDCR_TYPE_LOCAL:
		assert(0);
	}
}
static int set_cmp(void *a, const void *b)
{
	return strcmp(a, b);
}

int ldmsd_prdcr_str2type(const char *type)
{
	enum ldmsd_prdcr_type prdcr_type;
	if (0 == strcasecmp(type, "active"))
		prdcr_type = LDMSD_PRDCR_TYPE_ACTIVE;
	else if (0 == strcasecmp(type, "passive"))
		prdcr_type = LDMSD_PRDCR_TYPE_PASSIVE;
	else if (0 == strcasecmp(type, "local"))
		prdcr_type = LDMSD_PRDCR_TYPE_LOCAL;
	else
		return -EINVAL;
	return prdcr_type;
}

const char *ldmsd_prdcr_type2str(enum ldmsd_prdcr_type type)
{
	if (LDMSD_PRDCR_TYPE_ACTIVE == type)
		return "active";
	else if (LDMSD_PRDCR_TYPE_PASSIVE == type)
		return "passive";
	else if (LDMSD_PRDCR_TYPE_LOCAL == type)
		return "local";
	else
		return NULL;
}

int prdcr_connect_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t ev)
{
	prdcr_connect(EV_DATA(ev, struct connect_data)->prdcr);
	return 0;
}

ldmsd_prdcr_t
ldmsd_prdcr_new_with_auth(const char *name, const char *xprt_name,
		const char *host_name, const unsigned short port_no,
		enum ldmsd_prdcr_type type, int conn_intrvl_us,
		const char *auth, char *auth_args,
		uid_t uid, gid_t gid, int perm)
{
	struct ldmsd_prdcr *prdcr;
	char *xprt, *host, *au;
	struct attr_value_list *au_opts = NULL;
	au = NULL;

	errno = EINVAL;
	if (!port_no)
		goto err_0;

	xprt = strdup(xprt_name);
	if (!xprt)
		goto err_0;

	host = strdup(host_name);
	if (!host)
		goto err_1;

	if (auth) {
		au = strdup(auth);
		if (!au)
			goto err_2;
		if (auth_args) {
			au_opts = ldmsd_auth_opts_str2avl(auth_args);
			if (!au_opts) {
				free(au);
				goto err_2;
			}
		}
	}

	ldmsd_log(LDMSD_LDEBUG,
		  "ldmsd_prdcr_new(name %s, xprt %s, host %s, "
		  "port %hu, type %d, intv %d\n",
		  name, xprt_name, host_name, port_no, type, conn_intrvl_us);

	prdcr = (struct ldmsd_prdcr *)
		ldmsd_cfgobj_new_with_auth(name, LDMSD_CFGOBJ_PRDCR,
				sizeof *prdcr, ldmsd_prdcr___del,
				uid, gid, perm);
	if (!prdcr)
		goto err_3;

	prdcr->type = type;
	prdcr->conn_intrvl_us = conn_intrvl_us;
	prdcr->port_no = port_no;
	prdcr->conn_state = LDMSD_PRDCR_STATE_STOPPED;
	rbt_init(&prdcr->set_tree, set_cmp);
	prdcr->host_name = host;
	prdcr->xprt_name = xprt;
	/*
	 * If auth is NULL, the default authentication method will be used
	 * when prdcr creates the LDMS transport.
	 */
	prdcr->conn_auth = au;
	prdcr->conn_auth_args = au_opts;

	if (prdcr_resolve(host_name, port_no, &prdcr->ss, &prdcr->ss_len)) {
		errno = EAFNOSUPPORT;
		ldmsd_log(LDMSD_LERROR, "ldmsd_prdcr_new: %s:%u not resolved.\n",
			host_name,(unsigned) port_no);
		goto err_4;
	}

	prdcr->connect_ev = ev_new(prdcr_connect_type);
	EV_DATA(prdcr->connect_ev, struct connect_data)->prdcr = prdcr;
	prdcr->start_ev = ev_new(prdcr_start_type);
	EV_DATA(prdcr->start_ev, struct start_data)->entity = prdcr;
	prdcr->stop_ev = ev_new(prdcr_stop_type);
	EV_DATA(prdcr->stop_ev, struct stop_data)->entity = prdcr;

	ldmsd_prdcr_unlock(prdcr);
	return prdcr;

 err_4:
	ldmsd_prdcr_unlock(prdcr);
	ldmsd_prdcr_put(prdcr);
 err_3:
	if (au) {
		free(au);
	}
	if (au_opts)
		av_free(au_opts);
 err_2:
	free(host);
 err_1:
	free(xprt);
 err_0:
	return NULL;
}

ldmsd_prdcr_t
ldmsd_prdcr_new(const char *name, const char *xprt_name,
		const char *host_name, const unsigned short port_no,
		enum ldmsd_prdcr_type type, int conn_intrvl_us,
		char *auth, char *auth_args)
{
	struct ldmsd_sec_ctxt sctxt;
	ldmsd_sec_ctxt_get(&sctxt);
	return ldmsd_prdcr_new_with_auth(name, xprt_name, host_name,
			port_no, type, conn_intrvl_us, auth, auth_args,
			sctxt.crd.uid, sctxt.crd.gid, 0777);
}

extern struct rbt *cfgobj_trees[];
extern pthread_mutex_t *cfgobj_locks[];
ldmsd_cfgobj_t __cfgobj_find(const char *name, ldmsd_cfgobj_type_t type);

int ldmsd_prdcr_del(const char *prdcr_name, ldmsd_sec_ctxt_t ctxt)
{
	int rc = 0;
	ldmsd_prdcr_t prdcr;
	pthread_mutex_lock(cfgobj_locks[LDMSD_CFGOBJ_PRDCR]);
	prdcr = (ldmsd_prdcr_t) __cfgobj_find(prdcr_name, LDMSD_CFGOBJ_PRDCR);
	if (!prdcr) {
		rc = ENOENT;
		goto out_0;
	}

	ldmsd_prdcr_lock(prdcr);
	rc = ldmsd_cfgobj_access_check(&prdcr->obj, 0222, ctxt);
	if (rc)
		goto out_1;
	if (prdcr->conn_state != LDMSD_PRDCR_STATE_STOPPED) {
		rc = EBUSY;
		goto out_1;
	}
	if (ldmsd_cfgobj_refcount(&prdcr->obj) > 2) {
		rc = EBUSY;
		goto out_1;
	}

	/* removing from the tree */
	rbt_del(cfgobj_trees[LDMSD_CFGOBJ_PRDCR], &prdcr->obj.rbn);
	ldmsd_prdcr_put(prdcr); /* putting down reference from the tree */

	rc = 0;
	/* let-through */
out_1:
	ldmsd_prdcr_unlock(prdcr);
out_0:
	pthread_mutex_unlock(cfgobj_locks[LDMSD_CFGOBJ_PRDCR]);
	if (prdcr)
		ldmsd_prdcr_put(prdcr); /* `find` reference */
	return rc;
}

/*
 * An updater has start. Send the updtr all of the prdcr sets.
 */
int updtr_start_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t ev)
{
	ldmsd_prdcr_t prdcr;
	ldmsd_prdcr_set_t prd_set;

	ldmsd_cfg_lock(LDMSD_CFGOBJ_PRDCR);
	for (prdcr = ldmsd_prdcr_first(); prdcr; prdcr = ldmsd_prdcr_next(prdcr)) {
		for (prd_set = ldmsd_prdcr_set_first(prdcr);
		     prd_set; prd_set = ldmsd_prdcr_set_next(prd_set)) {
			if (prd_set->state >= LDMSD_PRDCR_SET_STATE_READY) {
				EV_DATA(prd_set->state_ev, struct state_data)->start_n_stop = 1;
				ldmsd_prdcr_set_ref_get(prd_set, "state_ev");
				if (ev_post(producer, updater, prd_set->state_ev, NULL))
					ldmsd_prdcr_set_ref_put(prd_set, "state_ev");
			}
		}
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR);
	return 0;
}

int updtr_stop_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t ev)
{
	return 0;
}

int __ldmsd_prdcr_start(ldmsd_prdcr_t prdcr, ldmsd_sec_ctxt_t ctxt)
{
	int rc;
	ldmsd_prdcr_lock(prdcr);
	rc = ldmsd_cfgobj_access_check(&prdcr->obj, 0222, ctxt);
	if (rc)
		goto out;
	if (prdcr->conn_state != LDMSD_PRDCR_STATE_STOPPED) {
		rc = EBUSY;
		goto out;
	}

	prdcr->conn_state = LDMSD_PRDCR_STATE_DISCONNECTED;

	prdcr->obj.perm |= LDMSD_PERM_DSTART;

	ev_post(producer, updater, prdcr->start_ev, NULL);
	ev_post(producer, producer, prdcr->connect_ev, NULL);
out:
	ldmsd_prdcr_unlock(prdcr);
	return rc;
}

int ldmsd_prdcr_start(const char *name, const char *interval_str,
		      ldmsd_sec_ctxt_t ctxt)
{
	int rc = 0;
	ldmsd_prdcr_t prdcr = ldmsd_prdcr_find(name);
	if (!prdcr)
		return ENOENT;
	if (interval_str)
		prdcr->conn_intrvl_us = strtol(interval_str, NULL, 0);
	rc = __ldmsd_prdcr_start(prdcr, ctxt);
	ldmsd_prdcr_put(prdcr);
	return rc;
}

int __ldmsd_prdcr_stop(ldmsd_prdcr_t prdcr, ldmsd_sec_ctxt_t ctxt)
{
	int rc;
	ldmsd_prdcr_lock(prdcr);
	rc = ldmsd_cfgobj_access_check(&prdcr->obj, 0222, ctxt);
	if (rc)
		goto out;
	if (prdcr->conn_state == LDMSD_PRDCR_STATE_STOPPED) {
		rc = 0; /* already stopped,
			 * return 0 so that caller knows
			 * stop succeeds. */
		goto out;
	}
	if (prdcr->conn_state == LDMSD_PRDCR_STATE_STOPPING) {
		rc = EBUSY;
		goto out;
	}
	if (prdcr->type == LDMSD_PRDCR_TYPE_LOCAL)
		prdcr_reset_sets(prdcr);
	prdcr->obj.perm &= ~LDMSD_PERM_DSTART;
	prdcr->conn_state = LDMSD_PRDCR_STATE_STOPPING;
	if (prdcr->xprt)
		ldms_xprt_close(prdcr->xprt);
	ldmsd_prdcr_unlock(prdcr);
	ldmsd_prdcr_lock(prdcr);
	if (!prdcr->xprt)
		prdcr->conn_state = LDMSD_PRDCR_STATE_STOPPED;
	ev_post(producer, updater, prdcr->stop_ev, NULL);
out:
	ldmsd_prdcr_unlock(prdcr);
	return rc;
}

int ldmsd_prdcr_stop(const char *name, ldmsd_sec_ctxt_t ctxt)
{
	int rc = 0;
	ldmsd_prdcr_t prdcr = ldmsd_prdcr_find(name);
	if (!prdcr)
		return ENOENT;
	rc = __ldmsd_prdcr_stop(prdcr, ctxt);
	ldmsd_prdcr_put(prdcr);
	return rc;
}

int ldmsd_prdcr_subscribe(ldmsd_prdcr_t prdcr, const char *stream)
{
	ldmsd_prdcr_stream_t s = calloc(1, sizeof *s);
	if (!s)
		goto err;
	s->name = strdup(stream);
	if (!s->name)
		goto err;
	ldmsd_prdcr_lock(prdcr);
	LIST_INSERT_HEAD(&prdcr->stream_list, s, entry);
	ldmsd_prdcr_unlock(prdcr);
	return 0;
 err:
	if (s)
		free(s);
		return ENOMEM;
}

int ldmsd_prdcr_start_regex(const char *prdcr_regex, const char *interval_str,
			    char *rep_buf, size_t rep_len,
			    ldmsd_sec_ctxt_t ctxt)
{
	regex_t regex;
	ldmsd_prdcr_t prdcr;
	int rc;

	rc = ldmsd_compile_regex(&regex, prdcr_regex, rep_buf, rep_len);
	if (rc)
		return rc;

	ldmsd_cfg_lock(LDMSD_CFGOBJ_PRDCR);
	for (prdcr = ldmsd_prdcr_first(); prdcr; prdcr = ldmsd_prdcr_next(prdcr)) {
		rc = regexec(&regex, prdcr->obj.name, 0, NULL, 0);
		if (rc)
			continue;
		if (interval_str)
			prdcr->conn_intrvl_us = strtol(interval_str, NULL, 0);
		__ldmsd_prdcr_start(prdcr, ctxt);
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR);
	regfree(&regex);
	return 0;
}

int ldmsd_prdcr_stop_regex(const char *prdcr_regex, char *rep_buf,
			   size_t rep_len, ldmsd_sec_ctxt_t ctxt)
{
	regex_t regex;
	ldmsd_prdcr_t prdcr;
	int rc;

	rc = ldmsd_compile_regex(&regex, prdcr_regex, rep_buf, rep_len);
	if (rc)
		return rc;
	ldmsd_cfg_lock(LDMSD_CFGOBJ_PRDCR);
	for (prdcr = ldmsd_prdcr_first(); prdcr; prdcr = ldmsd_prdcr_next(prdcr)) {
		rc = regexec(&regex, prdcr->obj.name, 0, NULL, 0);
		if (rc)
			continue;
		__ldmsd_prdcr_stop(prdcr, ctxt);
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR);
	regfree(&regex);
	return 0;
}

/**
 * Get the first producer set
 *
 * This function must be called with the ldmsd_cfgobj_lock held.
 */
ldmsd_prdcr_set_t ldmsd_prdcr_set_first(ldmsd_prdcr_t prdcr)
{
	struct rbn *rbn = rbt_min(&prdcr->set_tree);
	if (rbn)
		return container_of(rbn, struct ldmsd_prdcr_set, rbn);
	return NULL;
}

/**
 * Get the next producer set
 *
 * This function must be called with the ldmsd_cfgobj_lock held.
 */
ldmsd_prdcr_set_t ldmsd_prdcr_set_next(ldmsd_prdcr_set_t prd_set)
{
	struct rbn *rbn = rbn_succ(&prd_set->rbn);
	if (rbn)
		return container_of(rbn, struct ldmsd_prdcr_set, rbn);
	return NULL;
}

/**
 * Get the producer set with the given name \c setname
 *
 * This function must be called with the producer lock held.
 */
ldmsd_prdcr_set_t ldmsd_prdcr_set_find(ldmsd_prdcr_t prdcr, const char *setname)
{
	struct rbn *rbn = rbt_find(&prdcr->set_tree, setname);
	if (rbn)
		return container_of(rbn, struct ldmsd_prdcr_set, rbn);
	return NULL;
}

/* Must be called with strgp lock held. */
void ldmsd_prdcr_update(ldmsd_strgp_t strgp)
{
	/*
	 * For each producer with a name that matches the storage
	 * policy's regex list, add a reference to this storage policy
	 * to each producer set matching the schema name
	 */
	ldmsd_cfg_lock(LDMSD_CFGOBJ_PRDCR);
	ldmsd_prdcr_t prdcr;
	for (prdcr = ldmsd_prdcr_first(); prdcr; prdcr = ldmsd_prdcr_next(prdcr)) {
		int rc;
		ldmsd_name_match_t match = ldmsd_strgp_prdcr_first(strgp);
		for (rc = 0; match; match = ldmsd_strgp_prdcr_next(match)) {
			rc = regexec(&match->regex, prdcr->obj.name, 0, NULL, 0);
			if (!rc)
				break;
		}
		if (rc)
			continue;

		ldmsd_prdcr_lock(prdcr);
		/*
		 * For each producer set matching our schema, add our policy
		 */
		ldmsd_prdcr_set_t prd_set;
		for (prd_set = ldmsd_prdcr_set_first(prdcr);
		     prd_set; prd_set = ldmsd_prdcr_set_next(prd_set)) {
			ldmsd_strgp_unlock(strgp);
			pthread_mutex_lock(&prd_set->lock);
			if (prd_set->state < LDMSD_PRDCR_SET_STATE_READY) {
				ldmsd_strgp_lock(strgp);
				pthread_mutex_unlock(&prd_set->lock);
				continue;
			}

			ldmsd_strgp_lock(strgp);
			ldmsd_strgp_update_prdcr_set(strgp, prd_set);
			pthread_mutex_unlock(&prd_set->lock);
		}
		ldmsd_prdcr_unlock(prdcr);
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR);
}

int ldmsd_prdcr_subscribe_regex(const char *prdcr_regex, char *stream_name,
				char *rep_buf, size_t rep_len,
				ldmsd_sec_ctxt_t ctxt)
{
	regex_t regex;
	ldmsd_prdcr_t prdcr;
	int rc;

	rc = ldmsd_compile_regex(&regex, prdcr_regex, rep_buf, rep_len);
	if (rc)
		return rc;
	ldmsd_cfg_lock(LDMSD_CFGOBJ_PRDCR);
	for (prdcr = ldmsd_prdcr_first(); prdcr; prdcr = ldmsd_prdcr_next(prdcr)) {
		rc = regexec(&regex, prdcr->obj.name, 0, NULL, 0);
		if (rc)
			continue;
		ldmsd_prdcr_subscribe(prdcr, stream_name);
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR);
	regfree(&regex);
	return 0;
}

