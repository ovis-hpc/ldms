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
#include <ovis_json/ovis_json.h>
#include <netdb.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldms_xprt.h"
#include "ldmsd_request.h"
#include "config.h"

static void prdcr_task_cb(ldmsd_task_t task, void *arg);

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

	set->ref_count = 1;
	return set;
 err_2:
	free(set->inst_name);
 err_1:
	free(set);
 err_0:
	return NULL;
}

void __prdcr_set_del(ldmsd_prdcr_set_t set)
{
	ldmsd_log(LDMSD_LINFO, "Deleting producer set %s from producer %s\n",
				set->inst_name, set->prdcr->obj.name);
	if (set->schema_name)
		free(set->schema_name);

	if (set->set) {
		ldms_set_ref_put(set->set, "prdcr_set");
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

	if (set->updt_hint_entry.le_prev)
		LIST_REMOVE(set, updt_hint_entry);

	free(set->inst_name);
	free(set);
}

void ldmsd_prdcr_set_ref_get(ldmsd_prdcr_set_t set)
{
	assert(set->ref_count);
	(void)__sync_fetch_and_add(&set->ref_count, 1);
}

void ldmsd_prdcr_set_ref_put(ldmsd_prdcr_set_t set)
{
	assert(set->ref_count);
	if (0 == __sync_sub_and_fetch(&set->ref_count, 1))
		__prdcr_set_del(set);
}

static void prdcr_set_del(ldmsd_prdcr_set_t set)
{
	set->state = LDMSD_PRDCR_SET_STATE_START;
	ldmsd_prdcr_set_ref_put(set);
}

#define UPDT_HINT_TREE_ADD 1
#define UPDT_HINT_TREE_REMOVE 2
void prdcr_hint_tree_update(ldmsd_prdcr_t prdcr, ldmsd_prdcr_set_t prd_set,
				struct ldmsd_updtr_schedule *hint, int op)
{
	struct rbn *rbn;
	struct ldmsd_updt_hint_set_list *list;
	struct ldmsd_updtr_schedule *hint_key;
	if (0 == hint->intrvl_us)
		return;
	rbn = rbt_find(&prdcr->hint_set_tree, hint);
	if (op == UPDT_HINT_TREE_REMOVE) {
		if (!rbn)
			return;
		list = container_of(rbn, struct ldmsd_updt_hint_set_list, rbn);
		assert(prd_set->ref_count);
		assert(prd_set->updt_hint_entry.le_prev);
		LIST_REMOVE(prd_set, updt_hint_entry);
		prd_set->updt_hint_entry.le_next = NULL;
		prd_set->updt_hint_entry.le_prev = NULL;
		ldmsd_prdcr_set_ref_put(prd_set);

		if (LIST_EMPTY(&list->list)) {
			rbt_del(&prdcr->hint_set_tree, &list->rbn);
			free(list->rbn.key);
			free(list);
		}
	} else if (op == UPDT_HINT_TREE_ADD) {
		if (!rbn) {
			list = malloc(sizeof(*list));
			hint_key = malloc(sizeof(*hint_key));
			*hint_key = *hint;
			rbn_init(&list->rbn, hint_key);
			rbt_ins(&prdcr->hint_set_tree, &list->rbn);
			LIST_INIT(&list->list);
		} else {
			list = container_of(rbn,
					struct ldmsd_updt_hint_set_list, rbn);
		}
		ldmsd_prdcr_set_ref_get(prd_set);
		LIST_INSERT_HEAD(&list->list, prd_set, updt_hint_entry);
	}
}

static void prdcr_reset_set(ldmsd_prdcr_t prdcr, ldmsd_prdcr_set_t prd_set)
{
	prdcr_hint_tree_update(prdcr, prd_set,
			       &prd_set->updt_hint, UPDT_HINT_TREE_REMOVE);
	rbt_del(&prdcr->set_tree, &prd_set->rbn);
	ldmsd_prdcr_set_ref_put(prd_set);	/* set_tree reference */
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

void ldmsd_prd_set_updtr_task_update(ldmsd_prdcr_set_t prd_set)
{
	ldmsd_updtr_t updtr;
	ldmsd_name_match_t match;
	char *str;
	int rc;
	ldmsd_cfg_lock(LDMSD_CFGOBJ_UPDTR);
	for (updtr = ldmsd_updtr_first(); updtr; updtr = ldmsd_updtr_next(updtr)) {
		ldmsd_updtr_lock(updtr);
		if (updtr->state != LDMSD_UPDTR_STATE_RUNNING) {
			ldmsd_updtr_unlock(updtr);
			continue;
		}

		/* Updaters for push don't schedule any updates. */
		if (0 != updtr->push_flags) {
			ldmsd_updtr_unlock(updtr);
			continue;
		}
		if (!ldmsd_updtr_prdcr_find(updtr, prd_set->prdcr->obj.name)) {
			ldmsd_updtr_unlock(updtr);
			continue;
		}
		if (!LIST_EMPTY(&updtr->match_list)) {
			LIST_FOREACH(match, &updtr->match_list, entry) {
				if (match->selector == LDMSD_NAME_MATCH_INST_NAME)
					str = prd_set->inst_name;
				else
					str = prd_set->schema_name;
				rc = regexec(&match->regex, str, 0, NULL, 0);
				if (!rc)
					goto update_tasks;
			}
			goto nxt_updtr;
		}
	update_tasks:
		pthread_mutex_lock(&prd_set->lock);
		ldmsd_updtr_tasks_update(updtr, prd_set);
		pthread_mutex_unlock(&prd_set->lock);
	nxt_updtr:
		ldmsd_updtr_unlock(updtr);
	}


	ldmsd_cfg_unlock(LDMSD_CFGOBJ_UPDTR);
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

extern void __ldmsd_prdset_lookup_cb(ldms_t xprt, enum ldms_lookup_status status,
				     int more, ldms_set_t set, void *arg);
static void _add_cb(ldms_t xprt, ldmsd_prdcr_t prdcr, ldms_dir_set_t dset)
{
	ldmsd_prdcr_set_t set;

	ldmsd_log(LDMSD_LINFO, "Adding the metric set '%s'\n", dset->inst_name);

	/* Check to see if it's already there */
	set = _find_set(prdcr, dset->inst_name);
	if (!set) {
		/* See if the ldms set is already there */
		ldms_set_t xs = ldms_xprt_set_by_name(xprt, dset->inst_name);
		if (xs) {
			ldmsd_log(LDMSD_LCRITICAL, "Received dir_add, prdset is missing, but set %s is present...ignoring",
				  dset->inst_name);
			ldms_set_put(xs);
			return;
		}
		set = prdcr_set_new(dset->inst_name, dset->schema_name);
		if (!set) {
			ldmsd_log(LDMSD_LERROR, "Memory allocation failure in %s "
				 "for set_name %s\n",
				 __FUNCTION__, dset->inst_name);
			return;
		}
		set->prdcr = prdcr;
		ldmsd_prdcr_set_ref_get(set); 	/* set_tree reference */
		rbt_ins(&prdcr->set_tree, &set->rbn);
	} else {
		ldmsd_log(LDMSD_LCRITICAL, "Received a dir_add update for "
			  "'%s', prdcr_set still present with refcount %d, and set "
			  "%p.\n", dset->inst_name, set->ref_count, set->set);
		return;
	}

	__update_set_info(set, dset);
	if (0 != set->updt_hint.intrvl_us) {
		ldmsd_log(LDMSD_LDEBUG, "producer '%s' add set '%s' to hint tree\n",
						prdcr->obj.name, set->inst_name);
		prdcr_hint_tree_update(prdcr, set,
				&set->updt_hint, UPDT_HINT_TREE_ADD);
 	}

	ldmsd_prdcr_unlock(prdcr);
	ldmsd_prd_set_updtr_task_update(set);
	ldmsd_prdcr_lock(prdcr);
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
 * Process the deleted set. This will only be received from downstream
 * peers that are older than 4.3.4
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
		assert(set->ref_count);
		prdcr_reset_set(prdcr, set);
	}
}

static void prdcr_dir_cb_upd(ldms_t xprt, ldms_dir_t dir, ldmsd_prdcr_t prdcr)
{
	ldmsd_prdcr_set_t set;
	int i;
	struct ldmsd_updtr_schedule prev_hint;

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
		prdcr_hint_tree_update(prdcr, set, &set->updt_hint, UPDT_HINT_TREE_REMOVE);
		prev_hint = set->updt_hint;
		__update_set_info(set, &dir->set_data[i]);
		prdcr_hint_tree_update(prdcr, set, &set->updt_hint, UPDT_HINT_TREE_ADD);
		pthread_mutex_unlock(&set->lock);
		if (0 != ldmsd_updtr_schedule_cmp(&prev_hint, &set->updt_hint)) {
			/*
			 * Update Updater tasks only when
			 * there are any changes to
			 * avoid unnecessary iterations.
			 */
			ldmsd_prdcr_unlock(prdcr);
			ldmsd_prd_set_updtr_task_update(set);
			ldmsd_prdcr_lock(prdcr);
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

static void __prdcr_remote_set_delete(ldmsd_prdcr_t prdcr, const char *name)
{
	const char *state_str = "bad_state";
	ldmsd_prdcr_set_t prdcr_set;
	prdcr_set = ldmsd_prdcr_set_find(prdcr, name);
	if (!prdcr_set)
		return;
	pthread_mutex_lock(&prdcr_set->lock);
	assert(prdcr_set->ref_count);
	switch (prdcr_set->state) {
	case LDMSD_PRDCR_SET_STATE_START:
		state_str = "START";
		break;
	case LDMSD_PRDCR_SET_STATE_LOOKUP:
		state_str = "LOOKUP";
		break;
	case LDMSD_PRDCR_SET_STATE_READY:
		state_str = "READY";
		break;
	case LDMSD_PRDCR_SET_STATE_UPDATING:
		state_str = "UPDATING";
		break;
	case LDMSD_PRDCR_SET_STATE_DELETED:
		state_str = "DELETING";
		break;
	}
	ldmsd_log(LDMSD_LINFO,
			"Deleting %s in the %s state\n",
			prdcr_set->inst_name, state_str);
	pthread_mutex_unlock(&prdcr_set->lock);
	prdcr_reset_set(prdcr, prdcr_set);
}

const char *_conn_state_str[] = {
	[LDMSD_PRDCR_STATE_STOPPED]       =  "LDMSD_PRDCR_STATE_STOPPED",
	[LDMSD_PRDCR_STATE_DISCONNECTED]  =  "LDMSD_PRDCR_STATE_DISCONNECTED",
	[LDMSD_PRDCR_STATE_CONNECTING]    =  "LDMSD_PRDCR_STATE_CONNECTING",
	[LDMSD_PRDCR_STATE_CONNECTED]     =  "LDMSD_PRDCR_STATE_CONNECTED",
	[LDMSD_PRDCR_STATE_STOPPING]      =  "LDMSD_PRDCR_STATE_STOPPING",
};

static const char *conn_state_str(int state)
{
	if (LDMSD_PRDCR_STATE_STOPPED<=state && state<=LDMSD_PRDCR_STATE_STOPPING)
		return _conn_state_str[state];
	return "UNKNOWN_STATE";
}

static void __ldmsd_xprt_ctxt_free(void *_ctxt)
{
	struct ldmsd_xprt_ctxt *ctxt = _ctxt;
	free(ctxt->name);
	free(ctxt);
}

static void prdcr_connect_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg)
{
	ldmsd_xprt_ctxt_t ctxt;
	ldmsd_prdcr_t prdcr = cb_arg;
	ldmsd_prdcr_lock(prdcr);
	ldmsd_log(LDMSD_LINFO, "%s:%d Producer %s (%s %s:%d:%s)"
				" conn_state: %d %s event type: %s\n",
				__func__, __LINE__,
				prdcr->obj.name, prdcr->xprt_name,
				prdcr->host_name, (int)prdcr->port_no,
				prdcr->conn_auth,
				prdcr->conn_state,
				conn_state_str(prdcr->conn_state),
				ldms_xprt_event_type_to_str(e->type));
	switch(e->type) {
	case LDMS_XPRT_EVENT_DISCONNECTED:
		x->disconnected = 1;
		break;
	default:
		assert(x->disconnected == 0);
		break;
	}
	switch (e->type) {
	case LDMS_XPRT_EVENT_CONNECTED:
		ldmsd_log(LDMSD_LINFO, "Producer %s is connected (%s %s:%d)\n",
				prdcr->obj.name, prdcr->xprt_name,
				prdcr->host_name, (int)prdcr->port_no);
		ctxt = malloc(sizeof(*ctxt));
		if (!ctxt) {
			ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
			return;
		}
		ctxt->name = strdup(prdcr->obj.name);
		if (!ctxt->name) {
			ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
			return;
		}
		ldms_xprt_ctxt_set(x, ctxt, __ldmsd_xprt_ctxt_free);
		prdcr->conn_state = LDMSD_PRDCR_STATE_CONNECTED;
		if (__prdcr_subscribe(prdcr)) {
			ldmsd_log(LDMSD_LERROR,
				  "Could not subscribe to stream data on producer %s\n",
				  prdcr->obj.name);
		}
		if (ldms_xprt_dir(prdcr->xprt, prdcr_dir_cb, prdcr,
				  LDMS_DIR_F_NOTIFY))
			ldms_xprt_close(prdcr->xprt);
		ldmsd_task_stop(&prdcr->task);
		break;
	case LDMS_XPRT_EVENT_RECV:
		ldmsd_recv_msg(x, e->data, e->data_len);
		break;
	case LDMS_XPRT_EVENT_SET_DELETE:
		__prdcr_remote_set_delete(prdcr, e->set_delete.name);
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
	case LDMS_XPRT_EVENT_SEND_COMPLETE:
		/* Ignore */
		break;
	default:
		assert(0);
	}
	ldmsd_prdcr_unlock(prdcr);
	return;

reset_prdcr:
	prdcr_reset_sets(prdcr);
	switch (prdcr->conn_state) {
	case LDMSD_PRDCR_STATE_STOPPING:
		prdcr->conn_state = LDMSD_PRDCR_STATE_STOPPED;
		break;
	case LDMSD_PRDCR_STATE_DISCONNECTED:
	case LDMSD_PRDCR_STATE_CONNECTING:
	case LDMSD_PRDCR_STATE_CONNECTED:
		prdcr->conn_state = LDMSD_PRDCR_STATE_DISCONNECTED;
		ldmsd_task_start(&prdcr->task, prdcr_task_cb, prdcr,
				 0, prdcr->conn_intrvl_us, 0);
		break;
	case LDMSD_PRDCR_STATE_STOPPED:
		assert(0 == "STOPPED shouldn't have xprt event");
		break;
	default:
		assert(0 == "BAD STATE");
	}
	if (prdcr->xprt) {
		ldmsd_xprt_term(prdcr->xprt);
		ldms_xprt_put(prdcr->xprt);
		prdcr->xprt = NULL;
	}
	ldmsd_log(LDMSD_LINFO, "%s:%d Producer (after reset) %s (%s %s:%d)"
				" conn_state: %d %s\n",
				__func__, __LINE__,
				prdcr->obj.name, prdcr->xprt_name,
				prdcr->host_name, (int)prdcr->port_no,
				prdcr->conn_state,
				conn_state_str(prdcr->conn_state));
	ldmsd_prdcr_unlock(prdcr);
}

extern const char *auth_name;
extern struct attr_value_list *auth_opt;

static void prdcr_connect(ldmsd_prdcr_t prdcr)
{
	int ret;

	assert(prdcr->xprt == NULL);
	switch (prdcr->type) {
	case LDMSD_PRDCR_TYPE_ACTIVE:
		prdcr->conn_state = LDMSD_PRDCR_STATE_CONNECTING;
		prdcr->xprt = ldms_xprt_new_with_auth(prdcr->xprt_name,
					ldmsd_linfo, prdcr->conn_auth,
					prdcr->conn_auth_args);
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
		if (prdcr->xprt) {
			struct ldms_xprt_event conn_ev = {.type = LDMS_XPRT_EVENT_CONNECTED};
			prdcr_connect_cb(prdcr->xprt, &conn_ev, prdcr);
		}
		break;
	case LDMSD_PRDCR_TYPE_LOCAL:
		assert(0);
	}
}
static void prdcr_task_cb(ldmsd_task_t task, void *arg)
{
	ldmsd_prdcr_t prdcr = arg;

	ldmsd_prdcr_lock(prdcr);
	switch (prdcr->conn_state) {
	case LDMSD_PRDCR_STATE_STOPPED:
	case LDMSD_PRDCR_STATE_STOPPING:
		ldmsd_task_stop(&prdcr->task);
		break;
	case LDMSD_PRDCR_STATE_DISCONNECTED:
		prdcr_connect(prdcr);
		break;
	case LDMSD_PRDCR_STATE_CONNECTING:
		break;
	case LDMSD_PRDCR_STATE_CONNECTED:
		ldmsd_task_stop(&prdcr->task);
		break;
	}
	ldmsd_prdcr_unlock(prdcr);
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

ldmsd_prdcr_t
ldmsd_prdcr_new_with_auth(const char *name, const char *xprt_name,
		const char *host_name, const unsigned short port_no,
		enum ldmsd_prdcr_type type, int conn_intrvl_us,
		const char *auth, uid_t uid, gid_t gid, int perm)
{
	extern struct rbt *cfgobj_trees[];
	struct ldmsd_prdcr *prdcr;
	ldmsd_auth_t auth_dom = NULL;

	ldmsd_log(LDMSD_LDEBUG, "ldmsd_prdcr_new(name %s, xprt %s, host %s, port %u, type %u, intv %d\n",
		name, xprt_name, host_name,(unsigned) port_no, (unsigned)type, conn_intrvl_us);
	prdcr = (struct ldmsd_prdcr *)
		ldmsd_cfgobj_new_with_auth(name, LDMSD_CFGOBJ_PRDCR,
				sizeof *prdcr, ldmsd_prdcr___del,
				uid, gid, perm);
	if (!prdcr)
		return NULL;

	prdcr->type = type;
	prdcr->conn_intrvl_us = conn_intrvl_us;
	prdcr->port_no = port_no;
	prdcr->conn_state = LDMSD_PRDCR_STATE_STOPPED;
	rbt_init(&prdcr->set_tree, set_cmp);
	rbt_init(&prdcr->hint_set_tree, ldmsd_updtr_schedule_cmp);
	prdcr->host_name = strdup(host_name);
	if (!prdcr->host_name)
		goto out;
	prdcr->xprt_name = strdup(xprt_name);
	if (!prdcr->port_no)
		goto out;

	if (prdcr_resolve(host_name, port_no, &prdcr->ss, &prdcr->ss_len)) {
		errno = EAFNOSUPPORT;
		ldmsd_log(LDMSD_LERROR, "ldmsd_prdcr_new: %s:%u not resolved.\n",
			host_name,(unsigned) port_no);
		goto out;
	}

	if (!auth)
		auth = DEFAULT_AUTH;
	auth_dom = ldmsd_auth_find(auth);
	if (!auth_dom) {
		errno = ENOENT;
		goto out;
	}
	prdcr->conn_auth = strdup(auth_dom->plugin);
	if (!prdcr->conn_auth)
		goto out;
	if (auth_dom->attrs) {
		prdcr->conn_auth_args = av_copy(auth_dom->attrs);
		if (!prdcr->conn_auth_args)
			goto out;
	}

	ldmsd_task_init(&prdcr->task);
	ldmsd_cfgobj_unlock(&prdcr->obj);
	return prdcr;
out:
	rbt_del(cfgobj_trees[LDMSD_CFGOBJ_PRDCR], &prdcr->obj.rbn);
	ldmsd_cfgobj_unlock(&prdcr->obj);
	ldmsd_cfgobj_put(&prdcr->obj);
	return NULL;
}

ldmsd_prdcr_t
ldmsd_prdcr_new(const char *name, const char *xprt_name,
		const char *host_name, const unsigned short port_no,
		enum ldmsd_prdcr_type type,
		int conn_intrvl_us)
{
	return ldmsd_prdcr_new_with_auth(name, xprt_name, host_name,
			port_no, type, conn_intrvl_us,
			DEFAULT_AUTH, getuid(), getgid(), 0777);
}

extern struct rbt *cfgobj_trees[];
extern pthread_mutex_t *cfgobj_locks[];
ldmsd_cfgobj_t __cfgobj_find(const char *name, ldmsd_cfgobj_type_t type);

extern void ldmsd_stream_publisher_remove(const char *p_name);
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

	ldmsd_stream_publisher_remove(prdcr_name);

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

ldmsd_prdcr_t ldmsd_prdcr_first()
{
	return (ldmsd_prdcr_t)ldmsd_cfgobj_first(LDMSD_CFGOBJ_PRDCR);
}

ldmsd_prdcr_t ldmsd_prdcr_next(struct ldmsd_prdcr *prdcr)
{
	return (ldmsd_prdcr_t)ldmsd_cfgobj_next(&prdcr->obj);
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
	ldmsd_task_start(&prdcr->task, prdcr_task_cb, prdcr,
			 LDMSD_TASK_F_IMMEDIATE,
			 prdcr->conn_intrvl_us, 0);
	ldmsd_log(LDMSD_LINFO, "Starting producer %s\n", prdcr->obj.name);
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
	ldmsd_log(LDMSD_LINFO, "Stopping producer %s\n", prdcr->obj.name);
	if (prdcr->type == LDMSD_PRDCR_TYPE_LOCAL)
		prdcr_reset_sets(prdcr);
	ldmsd_task_stop(&prdcr->task);
	prdcr->obj.perm &= ~LDMSD_PERM_DSTART;
	prdcr->conn_state = LDMSD_PRDCR_STATE_STOPPING;
	if (prdcr->xprt)
		ldms_xprt_close(prdcr->xprt);
	ldmsd_prdcr_unlock(prdcr);
	ldmsd_task_join(&prdcr->task);
	ldmsd_prdcr_lock(prdcr);
	if (!prdcr->xprt)
		prdcr->conn_state = LDMSD_PRDCR_STATE_STOPPED;
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
	int rc;
	ldmsd_req_cmd_t rcmd;
	ldmsd_prdcr_stream_t s = NULL;
	ldmsd_prdcr_lock(prdcr);
	LIST_FOREACH(s, &prdcr->stream_list, entry) {
		if (0 == strcmp(s->name, stream)) {
			rc = EEXIST;
			goto err_0;
		}
	}
	rc = ENOMEM;
	s = calloc(1, sizeof *s);
	if (!s)
		goto err_0;
	s->name = strdup(stream);
	if (!s->name)
		goto err_1;
	LIST_INSERT_HEAD(&prdcr->stream_list, s, entry);
	if (prdcr->conn_state == LDMSD_PRDCR_STATE_CONNECTED) {
		/* issue stream subscribe request right away if connected */
		rcmd = ldmsd_req_cmd_new(prdcr->xprt, LDMSD_STREAM_SUBSCRIBE_REQ,
					 NULL, __on_subs_resp, prdcr);
		rc = errno;
		if (!rcmd)
			goto rcmd_err;
		rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_NAME, s->name);
		if (rc)
			goto rcmd_err;
		rc = ldmsd_req_cmd_attr_term(rcmd);
		if (rc)
			goto rcmd_err;
	}
	ldmsd_prdcr_unlock(prdcr);
	return 0;
 err_1:
	if (s)
		free(s);
 err_0:
	ldmsd_prdcr_unlock(prdcr);
	return rc;

 rcmd_err:
	ldmsd_prdcr_unlock(prdcr);
	if (rcmd)
		ldmsd_req_cmd_free(rcmd);
	/* intentionally leave `s` in the list */
	return rc;
}

int ldmsd_prdcr_unsubscribe(ldmsd_prdcr_t prdcr, const char *stream)
{
	int rc;
	ldmsd_req_cmd_t rcmd;
	ldmsd_prdcr_stream_t s = NULL;
	ldmsd_prdcr_lock(prdcr);
	LIST_FOREACH(s, &prdcr->stream_list, entry) {
		if (0 == strcmp(s->name, stream))
			break; /* found */
	}
	if (!s) {
		rc = ENOENT;
		goto out;
	}
	LIST_REMOVE(s, entry);
	free((void*)s->name);
	free(s);
	if (prdcr->conn_state == LDMSD_PRDCR_STATE_CONNECTED) {
		/* issue stream unsubscribe request right away if connected */
		rcmd = ldmsd_req_cmd_new(prdcr->xprt, LDMSD_STREAM_UNSUBSCRIBE_REQ,
					 NULL, __on_subs_resp, prdcr);
		rc = errno;
		if (!rcmd)
			goto rcmd_err;
		rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_NAME, stream);
		if (rc)
			goto rcmd_err;
		rc = ldmsd_req_cmd_attr_term(rcmd);
		if (rc)
			goto rcmd_err;
	}
	rc = 0;
	/* let-through */
 out:
	ldmsd_prdcr_unlock(prdcr);
	return rc;

 rcmd_err:
	ldmsd_prdcr_unlock(prdcr);
	if (rcmd)
		ldmsd_req_cmd_free(rcmd);
	return rc;
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

/**
 * Get the first set with the given update hint \c intrvl and \c offset.
 *
 * Caller must hold the producer lock.
 */
ldmsd_prdcr_set_t ldmsd_prdcr_set_first_by_hint(ldmsd_prdcr_t prdcr,
					struct ldmsd_updtr_schedule *hint)
{
	struct rbn *rbn;
	ldmsd_updt_hint_set_list_t list;
	rbn = rbt_find(&prdcr->hint_set_tree, hint);
	if (!rbn)
		return NULL;
	list = container_of(rbn, struct ldmsd_updt_hint_set_list, rbn);
	if (LIST_EMPTY(&list->list))
		assert(0);
	return LIST_FIRST(&list->list);
}

/**
 * Get the next set with the given update hint \c intrvl and \c offset.
 *
 * Caller must hold the producer lock.
 */
ldmsd_prdcr_set_t ldmsd_prdcr_set_next_by_hint(ldmsd_prdcr_set_t prd_set)
{
	return LIST_NEXT(prd_set, updt_hint_entry);
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

int ldmsd_prdcr_unsubscribe_regex(const char *prdcr_regex, char *stream_name,
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
		ldmsd_prdcr_unsubscribe(prdcr, stream_name);
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR);
	regfree(&regex);
	return 0;
}
