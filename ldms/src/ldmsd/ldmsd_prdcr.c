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
	if (prdcr->stream_list)
		json_entity_free(prdcr->stream_list);
	ldmsd_cfgobj___del(obj);
}

static void __prdcr_set_del(ldmsd_prdcr_set_t set)
{
	ref_dump_no_lock(&set->ref, __func__);
	ldmsd_log(LDMSD_LINFO, "Deleting producer set %s\n", set->inst_name);
	if (set->schema_name)
		free(set->schema_name);
	if (set->set) {
		ldms_set_unpublish(set->set);
		ldms_set_delete(set->set);
	}
	ldmsd_strgp_ref_t strgp_ref = LIST_FIRST(&set->strgp_list);
	while (strgp_ref) {
		LIST_REMOVE(strgp_ref, entry);
		ldmsd_strgp_put(strgp_ref->strgp);
		free(strgp_ref);
		strgp_ref = LIST_FIRST(&set->strgp_list);
	}

	free(set->inst_name);
	free(set);
}

static ldmsd_prdcr_set_t prdcr_set_new(const char *inst_name, const char *schema_name)
{
	ldmsd_prdcr_set_t set = calloc(1, sizeof *set);
	if (!set)
		goto err_0;
	set->state = LDMSD_PRDCR_SET_STATE_START;
	set->inst_name = strdup(inst_name);
	if (!set->inst_name)
		goto err_1;
	set->schema_name = strdup(schema_name);
	if (!set->schema_name)
		goto err_2;
	pthread_mutex_init(&set->lock, NULL);
	rbn_init(&set->rbn, set->inst_name);

	set->state_ev = ev_new(prdcr_set_state_type);
	EV_DATA(set->state_ev, struct state_data)->prd_set = set;
	set->update_ev = ev_new(prdcr_set_update_type);
	EV_DATA(set->update_ev, struct update_data)->prd_set = set;

	ref_init(&set->ref, "create", (ref_free_fn_t)__prdcr_set_del, set);
	return set;
 err_2:
	free(set->inst_name);
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

static void prdcr_reset_set(ldmsd_prdcr_t prdcr, ldmsd_prdcr_set_t prd_set)
{
	pthread_mutex_lock(&prd_set->lock);
	prd_set->state = LDMSD_PRDCR_SET_STATE_ERROR;
	pthread_mutex_unlock(&prd_set->lock);
	EV_DATA(prd_set->state_ev, struct state_data)->start_n_stop = 0;
	ldmsd_prdcr_set_ref_get(prd_set, "state_ev");
	if (ev_post(producer, updater, prd_set->state_ev, NULL)) {
		ldmsd_prdcr_set_ref_put(prd_set, "state_ev");
	}
	rbt_del(&prdcr->set_tree, &prd_set->rbn);
	prdcr_set_del(prd_set);
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
		prdcr_reset_set(prdcr, prd_set);
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

static void __update_set_info(ldmsd_prdcr_set_t set, ldms_dir_set_t dset)
{
	long intrvl_us;
	long offset_us;
	char *hint = ldms_dir_set_info_get(dset, LDMSD_SET_INFO_UPDATE_HINT_KEY);
	if (hint) {
		char *endptr;
		char *s = strdup(hint);
		char *tok;
		if (!s) {
			ldmsd_lerror("%s:%d Memory allocation failure.\n",
				     __func__, __LINE__);
			return;
		}
		tok = strtok_r(s, ":", &endptr);
		offset_us = LDMSD_UPDT_HINT_OFFSET_NONE;
		intrvl_us = strtol(tok, NULL, 0);
		tok = strtok_r(NULL, ":", &endptr);
		if (tok)
			offset_us = strtol(tok, NULL, 0);

		/* Sanity check the hints */
		if (offset_us >= intrvl_us) {
			ldmsd_lerror("set %s: Invalid hint '%s', ignoring hint\n",
					set->inst_name, hint);
		} else {
			if (offset_us != LDMSD_UPDT_HINT_OFFSET_NONE)
				set->updt_hint.offset_us = offset_us;
			set->updt_hint.intrvl_us = intrvl_us;
		}
		free(s);
	}
}

static void _add_cb(ldms_t xprt, ldmsd_prdcr_t prdcr, ldms_dir_set_t dset)
{
	ldmsd_prdcr_set_t set;

	ldmsd_log(LDMSD_LINFO, "Adding the metric set '%s'\n", dset->inst_name);

	/* Check to see if it's already there */
	set = _find_set(prdcr, dset->inst_name);
	if (!set) {
		set = prdcr_set_new(dset->inst_name, dset->schema_name);
		if (!set) {
			ldmsd_log(LDMSD_LERROR, "Memory allocation failure in %s "
				 "for set_name %s\n",
				 __FUNCTION__, dset->inst_name);
			return;
		}
		set->prdcr = prdcr;
		rbt_ins(&prdcr->set_tree, &set->rbn);
	} else {
		ldmsd_log(LDMSD_LCRITICAL, "Receive a duplicated dir_add update of "
				"the set '%s'.\n", dset->inst_name);
		return;
	}

	__update_set_info(set, dset);
	EV_DATA(set->state_ev, struct state_data)->start_n_stop = 1;
	ldmsd_prdcr_set_ref_get(set, "state_ev");
	if (ev_post(producer, updater, set->state_ev, NULL))
		ldmsd_prdcr_set_ref_put(set, "state_ev");
}

/*
 * Process the directory list and add or restore specified sets.
 */
static void prdcr_dir_cb_add(ldms_t xprt, ldms_dir_t dir, ldmsd_prdcr_t prdcr)
{
	int i;
	for (i = 0; i < dir->set_count; i++)
		_add_cb(xprt, prdcr, &dir->set_data[i]);
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
		struct rbn *rbn = rbt_find(&prdcr->set_tree, dir->set_data[i].inst_name);
		if (!rbn)
			continue;
		set = container_of(rbn, struct ldmsd_prdcr_set, rbn);
		prdcr_reset_set(prdcr, set);
	}
}

static void prdcr_dir_cb_upd(ldms_t xprt, ldms_dir_t dir, ldmsd_prdcr_t prdcr)
{
	ldmsd_prdcr_set_t set;
	int i;

	for (i = 0; i < dir->set_count; i++) {
		set = ldmsd_prdcr_set_find(prdcr, dir->set_data[i].inst_name);
		if (!set) {
			/* Received an update, but the set is gone. */
			ldmsd_log(LDMSD_LERROR,
				  "Ignoring 'dir update' for the set, '%s', which "
				  "is not present in the prdcr_set tree.\n",
				  dir->set_data[i].inst_name);
			continue;
		}
		pthread_mutex_lock(&set->lock);
		__update_set_info(set, &dir->set_data[i]);
		EV_DATA(set->state_ev, struct state_data)->start_n_stop = 1;
		ldmsd_prdcr_set_ref_get(set, "state_ev");
		if (ev_post(producer, updater, set->state_ev, NULL))
			ldmsd_prdcr_set_ref_put(set, "state_ev");
		pthread_mutex_unlock(&set->lock);
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

//static int __on_subs_resp(ldmsd_req_cmd_t rcmd)
//{
//	return 0;
//}

/* Send subscribe request to peer */
static int __prdcr_subscribe(ldmsd_prdcr_t prdcr)
{
	return ENOSYS;
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
	struct timespec to;
	ldmsd_auth_t auth_dom;
	assert(prdcr->xprt == NULL);
	switch (prdcr->type) {
	case LDMSD_PRDCR_TYPE_ACTIVE:
		/* The authentication domain must exist because
		 * its reference got taken when the producer was created.
		 */
		auth_dom = ldmsd_auth_find(prdcr->conn_auth);
		prdcr->conn_state = LDMSD_PRDCR_STATE_CONNECTING;
		prdcr->xprt = ldms_xprt_new_with_auth(prdcr->xprt_name,
						      ldmsd_linfo,
						      auth_dom->plugin,
						      auth_dom->attrs);
		ldmsd_cfgobj_put(&auth_dom->obj); /* Put the 'find' reference back */
		if (prdcr->xprt) {
			ret  = ldms_xprt_connect(prdcr->xprt,
						 (struct sockaddr *)&prdcr->ss,
						 prdcr->ss_len,
						 prdcr_connect_cb, prdcr);
			if (ret) {
				ldms_xprt_put(prdcr->xprt);
				prdcr->xprt = NULL;
				goto error;
			}
		} else {
			ldmsd_log(LDMSD_LERROR, "%s Error %d: creating endpoint on transport '%s'.\n",
				 __func__, errno, prdcr->xprt_name);
			goto error;
		}
		break;
	case LDMSD_PRDCR_TYPE_PASSIVE:
		prdcr->xprt = ldms_xprt_by_remote_sin((struct sockaddr_in *)&prdcr->ss);
		/* Call connect callback to advance state and update timers*/
		struct ldms_xprt_event e = {.type = LDMS_XPRT_EVENT_CONNECTED};
		if (prdcr->xprt)
			prdcr_connect_cb(prdcr->xprt, &e, prdcr);
		break;
	case LDMSD_PRDCR_TYPE_LOCAL:
		assert(0);
	}
	return;

 error:
	prdcr->conn_state = LDMSD_PRDCR_STATE_DISCONNECTED;
	ev_sched_to(&to, prdcr->conn_intrvl_us / 1000000, 0);
	ev_post(producer, producer, prdcr->connect_ev, &to);
	return;
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

const char *ldmsd_prdcr_state2str(enum ldmsd_prdcr_state state)
{
	switch (state) {
	case LDMSD_PRDCR_STATE_STOPPED:
		return "STOPPED";
	case LDMSD_PRDCR_STATE_DISCONNECTED:
		return "DISCONNECTED";
	case LDMSD_PRDCR_STATE_CONNECTING:
		return "CONNECTING";
	case LDMSD_PRDCR_STATE_CONNECTED:
		return "CONNECTED";
	case LDMSD_PRDCR_STATE_STOPPING:
		return "STOPPING";
	}
	return "BAD STATE";
}

int prdcr_connect_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t ev)
{
	prdcr_connect(EV_DATA(ev, struct connect_data)->prdcr);
	return 0;
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
			EV_DATA(prd_set->state_ev, struct state_data)->start_n_stop = 1;
			ldmsd_prdcr_set_ref_get(prd_set, "state_ev");
			if (ev_post(producer, updater, prd_set->state_ev, NULL))
				ldmsd_prdcr_set_ref_put(prd_set, "state_ev");
		}
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR);
	return 0;
}

int updtr_stop_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t ev)
{
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
void ldmsd_prdcr_strgp_update(ldmsd_strgp_t strgp)
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
		ldmsd_regex_ent_t match = ldmsd_strgp_prdcr_first(strgp);
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

static int ldmsd_prdcr_enable(ldmsd_cfgobj_t obj)
{
	ldmsd_prdcr_t prdcr = (ldmsd_prdcr_t)obj;
	if (prdcr->conn_state != LDMSD_PRDCR_STATE_STOPPED)
		return EBUSY;

	prdcr->conn_state = LDMSD_PRDCR_STATE_DISCONNECTED;

	prdcr->obj.perm |= LDMSD_PERM_DSTART; /* TODO: Remove this? */

	ev_post(producer, updater, prdcr->start_ev, NULL);
	ev_post(producer, producer, prdcr->connect_ev, NULL);
	return 0;
}

static int ldmsd_prdcr_disable(ldmsd_cfgobj_t obj)
{
	int rc = 0;
	ldmsd_prdcr_t prdcr = (ldmsd_prdcr_t)obj;
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

static int
__prdcr_auth_attr(json_entity_t value, json_entity_t auth, char **_auth, ldmsd_req_buf_t buf)
{
	int rc;
	ldmsd_auth_t auth_domain;
	if (!auth) {
		*_auth = NULL;
	} else {
		if (JSON_STRING_VALUE != json_entity_type(auth)) {
			value = json_dict_build(value, JSON_STRING_VALUE, "auth",
						"'auth' is not a JSON string.", -1);
			if (!value)
				return ENOMEM;
			return EINVAL;
		}

		*_auth = strdup(json_value_str(auth)->str);
		if (!*_auth)
			return ENOMEM;
		auth_domain = ldmsd_auth_find(*_auth);
		if (!auth_domain) {
			free(*_auth);
			rc = ldmsd_req_buf_append(buf, "Auth '%s' not found.", *_auth);
			if (rc)
				return ENOMEM;
			value = json_dict_build(value, JSON_STRING_VALUE, "auth", buf->buf, -1);
			if (!value)
				return ENOMEM;
			return EINVAL;
		} else {
			/*
			 * Do not put back the reference because the auth domain
			 * will be referred by the prdcr anyway.
			 */
		}
	}
	return 0;
}

static int
__prdcr_port_attr(json_entity_t value, json_entity_t port, short *_port,
							ldmsd_req_buf_t buf)
{
	int rc;
	char *port_s;
	int port_i;
	if (!port) {
		*_port = LDMSD_ATTR_NA;
	} else {
		if (JSON_STRING_VALUE == json_entity_type(port)) {
			port_s = json_value_str(port)->str;
			port_i = atoi(port_s);
		} else if (JSON_INT_VALUE == json_entity_type(port)) {
			port_i = (int)json_value_int(port);
		} else {
			value = json_dict_build(value, JSON_STRING_VALUE,
				"'port' is neither a string or an integer.", -1);
			if (!value)
				return ENOMEM;
			return EINVAL;
		}

		if (port_i < 1 || port_i > USHRT_MAX) {
			rc = ldmsd_req_buf_append(buf,
					"port '%s' is invalid", port_s);
			if (rc < 0)
				return ENOMEM;
			value = json_dict_build(value, JSON_STRING_VALUE, "port", buf->buf, -1);
			if (!value)
				return ENOMEM;
			return EINVAL;
		}
		*_port = port_i;
	}
	return 0;
}

static int __prdcr_type_attr(json_entity_t value, json_entity_t type,
		enum ldmsd_prdcr_type *_type, ldmsd_req_buf_t buf)
{
	char *type_s;
	int rc;
	if (!type) {
		*_type = LDMSD_ATTR_NA;
	} else {
		if (JSON_STRING_VALUE != json_entity_type(type)) {
			value = json_dict_build(value, JSON_STRING_VALUE, "type",
						"'type' is not a JSON string.", -1);
			if (!value)
				return ENOMEM;
			return EINVAL;
		}
		type_s = json_value_str(type)->str;
		*_type = ldmsd_prdcr_str2type(type_s);
		if ((int)*_type < 0) {
			rc = ldmsd_req_buf_append(buf, "type '%s' is invalid.", type_s);
			if (rc < 0)
				return ENOMEM;
			value = json_dict_build(value, JSON_STRING_VALUE, "type",
									buf->buf, -1);
			if (!value)
				return ENOMEM;
			return EINVAL;
		}
		if (*_type == LDMSD_PRDCR_TYPE_LOCAL) {
			value = json_dict_build(value, JSON_STRING_VALUE, "type",
					"type 'local' is not supported.", -1);
			if (!value)
				return ENOMEM;
			return EINVAL;
		}
	}
	return 0;
}

static json_entity_t __prdcr_attr_get(json_entity_t dft, json_entity_t spc,
				enum ldmsd_prdcr_type *_type, char **_xprt,
				char **_host, short *_port,
				long *_interval_us, char **_auth,
				json_entity_t *_stream_list,
				int *_perm)
{
	int rc;
	json_entity_t xprt, host, port, interval, auth, perm, streams, type;
	json_entity_t err = NULL;
	ldmsd_req_buf_t buf;
	xprt = host = port = interval = auth = streams = perm = type = NULL;

	buf = ldmsd_req_buf_alloc(1024);
	if (!buf)
		goto oom;

	err = json_entity_new(JSON_DICT_VALUE);
	if (!err)
		goto oom;

	if (spc) {
		xprt = json_value_find(spc, "xprt");
		host = json_value_find(spc, "host");
		port = json_value_find(spc, "port");
		interval = json_value_find(spc, "interval");
		auth = json_value_find(spc, "auth");
		streams = json_value_find(spc, "streams");
		perm = json_value_find(spc, "perm");
		type = json_value_find(spc, "type");
	}

	if (dft) {
		if (!xprt)
			xprt = json_value_find(dft, "xprt");
		if (!host)
			host = json_value_find(dft, "host");
		if (!port)
			port = json_value_find(dft, "port");
		if (!interval)
			interval = json_value_find(dft, "interval");
		if (!auth)
			auth = json_value_find(dft, "auth");
		if (!streams)
			streams = json_value_find(dft, "streams");
		if (!perm)
			perm = json_value_find(dft, "perm");
		if (!type)
			type = json_value_find(dft, "type");
	}

	/* streams */
	if (streams) {
		*_stream_list = json_entity_copy(streams);
		if (!_stream_list)
			goto oom;
	} else {
		*_stream_list = NULL;
	}

	/* type */
	rc = __prdcr_type_attr(err, type, _type, buf);
	if (rc) {
		if (ENOMEM == rc)
			goto oom;
	}

	/* xprt */
	if (xprt) {
		*_xprt = strdup(json_value_str(xprt)->str);
		if (!*_xprt)
			goto oom;
	} else {
		*_xprt = NULL;
	}

	/* re-connect interval */
	if (!interval) {
		*_interval_us = LDMSD_ATTR_NA;
	} else {
		if (JSON_STRING_VALUE == json_entity_type(interval)) {
			*_interval_us = ldmsd_time_str2us(json_value_str(interval)->str);
		} else if (JSON_INT_VALUE == json_entity_type(interval)) {
			*_interval_us = json_value_int(interval);
		} else {
			err = json_dict_build(err, JSON_STRING_VALUE, "interval",
					"'interval' is neither a string or an integer.", -1);
			if (!err)
				goto oom;
		}
	}

	/* authentication */
	rc = __prdcr_auth_attr(err, auth, _auth, buf);
	if (rc) {
		if (ENOMEM == rc)
			goto oom;
	}

	/* permission */
	if (perm) {
		if (JSON_STRING_VALUE != json_entity_type(perm)) {
			err = json_dict_build(err, JSON_STRING_VALUE,
						"'perm' is not a string.", -1);
			if (!err)
				goto oom;
		} else {
			*_perm = strtol(json_value_str(perm)->str, NULL, 0);
		}
	} else {
		*_perm = LDMSD_ATTR_NA;
	}

	/* host */
	if (host) {
		if (JSON_STRING_VALUE != json_entity_type(host)) {
			err = json_dict_build(err, JSON_STRING_VALUE,
						"'host' is not a string.", -1);
			if (!err)
				goto oom;
		} else {
			*_host = strdup(json_value_str(host)->str);
			if (!*_host)
				goto oom;
		}
	} else {
		*_host = NULL;
	}

	/* port */
	rc = __prdcr_port_attr(err, port, _port, buf);
	if (rc) {
		if (ENOMEM == rc)
			goto oom;
	}

	if (buf)
		ldmsd_req_buf_free(buf);

	if (!json_attr_count(err)) {
		/* no errors */
		json_entity_free(err);
		err = NULL;
	}
	return err;

oom:
	errno = ENOMEM;
	if (buf)
		ldmsd_req_buf_free(buf);
	if (err)
		json_entity_free(err);
	return NULL;
}

json_entity_t __prdcr_export_config(ldmsd_prdcr_t prdcr)
{
	json_entity_t query, stream_list = NULL, a;

	query =ldmsd_cfgobj_query_result_new(&prdcr->obj);
	if (!query)
		goto oom;

	query = json_dict_build(query,
			JSON_INT_VALUE, "port", prdcr->port_no,
			JSON_STRING_VALUE, "host", prdcr->host_name,
			JSON_STRING_VALUE, "xprt", prdcr->xprt_name,
			JSON_STRING_VALUE, "type", ldmsd_prdcr_type2str(prdcr->type),
			JSON_INT_VALUE, "interval", prdcr->conn_intrvl_us,
			JSON_STRING_VALUE, "auth", prdcr->conn_auth,
			-1);
	if (!query)
		goto oom;

	if (prdcr->stream_list) {
		stream_list = json_entity_copy(prdcr->stream_list);
		if (!stream_list)
			goto oom;
		a = json_entity_new(JSON_ATTR_VALUE, "streams", stream_list);
		if (!a)
			goto oom;
		json_attr_add(query, a);
	}

	return query;
oom:
	if (query)
		json_entity_free(query);
	if (!stream_list)
		json_entity_free(stream_list);
	return NULL;
}

json_entity_t ldmsd_prdcr_query(ldmsd_cfgobj_t obj)
{
	json_entity_t query;
	ldmsd_prdcr_t prdcr = (ldmsd_prdcr_t)obj;

	query = __prdcr_export_config(prdcr);
	if (!query)
		goto oom;
	query = json_dict_build(query, JSON_STRING_VALUE, "state",
			ldmsd_prdcr_state2str(prdcr->conn_state),
			-1);
	if (!query)
		goto oom;
	return ldmsd_result_new(0, NULL, query);
oom:
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
	return NULL;
}

json_entity_t ldmsd_prdcr_export(ldmsd_cfgobj_t obj)
{
	json_entity_t query;
	ldmsd_prdcr_t prdcr = (ldmsd_prdcr_t)obj;
	query = __prdcr_export_config(prdcr);
	if (!query)
		goto oom;
	return ldmsd_result_new(0, NULL, query);
oom:
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
	return NULL;
}

json_entity_t ldmsd_prdcr_update(ldmsd_cfgobj_t obj, short enabled,
				json_entity_t dft, json_entity_t spc)
{
	char *xprt, *host, *auth;
	short port;
	int perm;
	long interval;
	json_entity_t stream_list;
	enum ldmsd_prdcr_type type;
	json_entity_t err;
	ldmsd_prdcr_t prdcr = (ldmsd_prdcr_t)obj;

	if (obj->enabled && enabled)
		return ldmsd_result_new(EBUSY, NULL, NULL);

	err = __prdcr_attr_get(dft, spc, &type, &xprt, &host, &port,
				&interval, &auth, &stream_list, &perm);
	if (!err) {
		if (ENOMEM == errno)
			goto oom;
	} else {
		return ldmsd_result_new(EINVAL, NULL, err);
	}
	if (LDMSD_ATTR_NA != type) {
		err = json_dict_build(err, JSON_STRING_VALUE, "type",
					"'type' cannot be changed.", -1);
		if (!err)
			goto oom;
	}
	if (xprt) {
		err = json_dict_build(err, JSON_STRING_VALUE, "xprt",
					"'xprt' cannot be changed.", -1);
		if (!err)
			goto oom;
	}

	if (host) {
		err = json_dict_build(err, JSON_STRING_VALUE, "host",
					"'host' cannot be changed.", -1);
		if (!err)
			goto oom;
	}
	if (LDMSD_ATTR_NA != port) {
		err = json_dict_build(err, JSON_STRING_VALUE, "port",
				"'port' cannot be changed.", -1);
		if (!err)
			goto oom;
	}
	if (auth) {
		err = json_dict_build(err, JSON_STRING_VALUE, "auth",
				"'auth' cannot be changed.", -1);
		if (!err)
			goto oom;
	}
	if (LDMSD_ATTR_NA != perm) {
		err = json_dict_build(err, JSON_STRING_VALUE, "permission",
				"'permission' cannot be changed.", -1);
		if (!err)
			goto oom;
	}

	if (err)
		return ldmsd_result_new(EINVAL, 0, err);

	/* Only the re-connect interval can be changed */
	if (LDMSD_ATTR_NA != interval)
		prdcr->conn_intrvl_us = interval;

	if (stream_list) {
		if (prdcr->stream_list)
			json_entity_free(prdcr->stream_list);
		prdcr->stream_list = stream_list;
	}

	obj->enabled = ((0 <= enabled)?enabled:obj->enabled);
	return ldmsd_result_new(0, NULL, NULL);
oom:
	errno = ENOMEM;
	return NULL;
}

json_entity_t ldmsd_prdcr_create(const char *name, short enabled, json_entity_t dft,
					json_entity_t spc, uid_t uid, gid_t gid)
{
	char *xprt, *host, *auth;
	short port;
	int perm;
	long interval_us;
	json_entity_t stream_list;
	enum ldmsd_prdcr_type type;
	ldmsd_prdcr_t prdcr = NULL;
	json_entity_t err;
	int rc;

	xprt = host = auth = NULL;
	err = __prdcr_attr_get(dft, spc, &type, &xprt, &host, &port,
					&interval_us, &auth, &stream_list, &perm);
	if (!err) {
		if (ENOMEM == errno)
			goto oom;
	} else {
		/* There is at least one invalid attribute. */
		return ldmsd_result_new(EINVAL, NULL, err);
	}
	if (!xprt) {
		err = json_dict_build(err, JSON_STRING_VALUE, "xprt",
					"'xprt' is missing.", -1);
		if (!err)
			goto oom;
	}
	if (!host) {
		err = json_dict_build(err, JSON_STRING_VALUE, "host",
					"'host' is missing.", -1);
		if (!err)
			goto oom;
	}
	if (LDMSD_ATTR_NA == port) {
		err = json_dict_build(err, JSON_STRING_VALUE, "port",
					"'port' is missing.", -1);
		if (!err)
			goto oom;
	}
	if (LDMSD_ATTR_NA == interval_us) {
		err = json_dict_build(err, JSON_STRING_VALUE, "interval",
					"'interval' is missing.", -1);
		if (!err)
			goto oom;
	}
	if (!auth) {
		/*
		 * Set the authentication method to the default authentication method.
		 */
		auth = strdup(ldmsd_global_auth_name_get());
		if (!auth)
			goto oom;
	}
	if (LDMSD_ATTR_NA == type) {
		/* The type wasn't given, so set it to 'active'. */
		type = LDMSD_PRDCR_TYPE_ACTIVE;
	}
	if (LDMSD_ATTR_NA == perm) {
		/* The permission wasn't given, so set it to 0770 */
		perm = 0770;
	}
	if (err)
		return ldmsd_result_new(EINVAL, 0, err);

	/* All attributes are valid, so create the cfgobj */

	ldmsd_log(LDMSD_LDEBUG,
		  "ldmsd_prdcr_new(name %s, xprt %s, host %s, "
		  "port %hu, type %u, intv %lu\n",
		  name, xprt, host, port, type, interval_us);

	prdcr = (struct ldmsd_prdcr *)
		ldmsd_cfgobj_new(name, LDMSD_CFGOBJ_PRDCR,
				sizeof *prdcr, ldmsd_prdcr___del,
				ldmsd_prdcr_update,
				ldmsd_cfgobj_delete,
				ldmsd_prdcr_query,
				ldmsd_prdcr_export,
				ldmsd_prdcr_enable,
				ldmsd_prdcr_disable,
				uid, gid, perm, enabled);
	if (!prdcr)
		goto oom;

	prdcr->type = type;
	prdcr->conn_intrvl_us = interval_us;
	prdcr->port_no = port;
	prdcr->conn_state = LDMSD_PRDCR_STATE_STOPPED;
	rbt_init(&prdcr->set_tree, set_cmp);
	prdcr->host_name = host;
	prdcr->xprt_name = xprt;
	prdcr->conn_auth = auth;
	if (!stream_list)
		prdcr->stream_list = NULL;
	else
		prdcr->stream_list = stream_list;

	if (prdcr_resolve(prdcr->host_name, prdcr->port_no, &prdcr->ss, &prdcr->ss_len)) {
		ldmsd_req_buf_t buf = ldmsd_req_buf_alloc(512);
		if (!buf)
			goto oom;
		rc = ldmsd_req_buf_append(buf, "ldmsd_prdcr_new: %s:%u not resolved.\n",
							prdcr->host_name,
							(unsigned) prdcr->port_no);
		err = json_dict_build(err, JSON_STRING_VALUE, "host:port",
								buf->buf, -1);
		ldmsd_req_buf_free(buf);
		if (!err)
			goto oom;
		goto err;
	}

	prdcr->connect_ev = ev_new(prdcr_connect_type);
	EV_DATA(prdcr->connect_ev, struct connect_data)->prdcr = prdcr;
	prdcr->start_ev = ev_new(prdcr_start_type);
	EV_DATA(prdcr->start_ev, struct start_data)->entity = prdcr;
	prdcr->stop_ev = ev_new(prdcr_stop_type);
	EV_DATA(prdcr->stop_ev, struct stop_data)->entity = prdcr;

	ldmsd_prdcr_unlock(prdcr);

	return ldmsd_result_new(0, NULL, NULL);
oom:
	if (err)
		json_entity_free(err);
	err = NULL;
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
	rc = ENOMEM;
err:
	if (xprt)
		free(xprt);
	if (host)
		free(host);
	if (auth)
		free(auth);
	if (prdcr) {
		ldmsd_prdcr_unlock(prdcr);
		ldmsd_prdcr_put(prdcr);
	}
	return ldmsd_result_new(rc, NULL, err);
}
