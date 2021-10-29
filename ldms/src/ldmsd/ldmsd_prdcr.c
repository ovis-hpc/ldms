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
#include "ldms.h"
#include "ldmsd.h"
#include "ldms_xprt.h"
#include "ldmsd_request.h"
#include "ldmsd_failover.h"
#include "ldmsd_event.h"
#include "ldmsd_cfgobj.h"
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

void __prdcr_set_del(void *args)
{
	ldmsd_prdcr_set_t set = args;
	ldmsd_log(LDMSD_LINFO, "Deleting producer set %s\n", set->inst_name);
	if (set->schema_name)
		free(set->schema_name);

	if (set->set) {
		ldms_set_ref_put(set->set, "prdcr_set");
		ldms_set_unpublish(set->set);
		ldms_set_delete(set->set);
	}

	if (set->updt_hint_entry.le_prev)
		LIST_REMOVE(set, updt_hint_entry);

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

	ref_init(&set->ref, "new", __prdcr_set_del, set);
	set->worker = assign_prdset_worker();
	return set;
 err_2:
	free(set->inst_name);
 err_1:
	free(set);
 err_0:
	return NULL;
}

static void __post_prdset_del_ev(ldmsd_prdcr_set_t set)
{
	ev_t ev = ev_new(prdset_del_type);
	if (!ev) {
		LDMSD_LOG_ENOMEM();
		return;
	}

	EV_DATA(ev, struct prdset_data)->prdset = set;
	ev_post(set->prdcr->worker, set->worker, ev, NULL);
}

static void __destroy_prdset_info(void *v)
{
	struct prdset_info *info = v;
	free(info->schema_name);
	free(info->prdcr_name);
	free(info->inst_name);
	if (info->set_snapshot)
		ldms_set_put(info->set_snapshot);
	ldmsd_prdcr_set_ref_put((ldmsd_prdcr_set_t)info->ctxt, "prdcr_info");
	free(info);
}

struct prdset_info *__prdset_info_get(ldmsd_prdcr_set_t prdset,
					   ldms_set_t snapshot,
					      const char *name)
{
	struct prdset_info *info = calloc(1, sizeof(*info));
	if (!info)
		return NULL;
	ref_init(&info->ref, name, __destroy_prdset_info, info);
	info->inst_name = strdup(prdset->inst_name);
	if (!info->inst_name)
		goto err;
	info->prdcr_name = strdup(prdset->producer_name);
	if (!info->prdcr_name)
		goto err;
	info->schema_name = strdup(prdset->schema_name);
	if (!info->schema_name)
		goto err;
	info->prdset_worker = prdset->worker;
	info->set_snapshot = snapshot;
	ldmsd_prdcr_set_ref_get(prdset, "prdcr_info");
	info->ctxt = prdset;
	return info;
err:
	__destroy_prdset_info(info);
	return NULL;
}

int
__post_prdset_state_ev(struct prdset_info *prdset_info,
			    enum ldmsd_prdcr_set_state state,
			    void *obj, ev_worker_t src,
			    ev_worker_t dst, struct timespec *to)
{
	ev_t prdset_ev;

	prdset_ev = ev_new(prdset_state_type);
	if (!prdset_ev)
		return ENOMEM;
	EV_DATA(prdset_ev, struct prdset_state_data)->prdset_info = prdset_info;
	EV_DATA(prdset_ev, struct prdset_state_data)->state = state;
	EV_DATA(prdset_ev, struct prdset_state_data)->obj = obj;
	return ev_post(src, dst, prdset_ev, to);
}

int prdset_update_complete_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e)
{
	if (EV_OK != status)
		return 0;

	int rc = 0;
	uint64_t gn;
	uint8_t free_snapshot = 1;
	struct prdset_info *prdset_info;
	ldms_set_t snapshot = EV_DATA(e, struct update_complete_data)->set;
	ldmsd_prdcr_set_t prdset = EV_DATA(e, struct update_complete_data)->prdset;
	int upd_status = EV_DATA(e, struct update_complete_data)->status;

	if (LDMS_UPD_ERROR(upd_status)) {
		char *op_s;
		if (0 == (upd_status & LDMS_UPD_F_PUSH))
			op_s = "update";
		else
			op_s = "push";
		ldmsd_log(LDMSD_LINFO, "Set %s: %s completing with "
					"bad status %d\n",
					prdset->inst_name, op_s,
					LDMS_UPD_ERROR(upd_status));
		goto out;
	}

	if (prdset->state == LDMSD_PRDCR_SET_STATE_DELETED)
		goto out;

	if (!ldms_set_is_consistent(snapshot)) {
		ldmsd_log(LDMSD_LINFO, "Set %s is inconsistent.\n", prdset->inst_name);
		goto set_ready;
	}

	gn = ldms_set_data_gn_get(snapshot);
	if (prdset->last_gn == gn) {
		ldmsd_log(LDMSD_LINFO, "Set %s oversampled %"PRIu64" == %"PRIu64".\n",
			  prdset->inst_name, prdset->last_gn, gn);
		goto set_ready;
	}
	prdset->last_gn = gn;

	prdset_info = __prdset_info_get(prdset, snapshot, "prdset2strgp_tree");
	if (!prdset_info) {
		LDMSD_LOG_ENOMEM();
		rc = ENOMEM;
		goto out;
	}

	free_snapshot = 0;
	__post_prdset_state_ev(prdset_info, LDMSD_PRDCR_SET_STATE_READY, NULL,
				prdset->worker, strgp_tree_w, NULL);
set_ready:
	if ((upd_status & LDMS_UPD_F_MORE) == 0)
		prdset->state = LDMSD_PRDCR_SET_STATE_READY;
out:
	if (0 == LDMS_UPD_ERROR(upd_status)) {
		ldmsd_log(LDMSD_LDEBUG, "Pushing set %p %s\n",
			  prdset->set, prdset->inst_name);
		int rc = ldms_xprt_push(prdset->set);
		if (rc) {
			ldmsd_log(LDMSD_LERROR, "Failed to push set %s\n",
						prdset->inst_name);
		}
	}
	if (0 == (upd_status & (LDMS_UPD_F_PUSH|LDMS_UPD_F_MORE)))
		ldmsd_prdcr_set_ref_put(prdset, "sched_update");

	/* Put reference taken when the src worker posted the event. */
	ldmsd_prdcr_set_ref_put(prdset, "update_complete");
	ev_put(e);
	if (free_snapshot)
		ldms_set_put(snapshot);
	return rc;
}

static int
__prdcr_post_updtr_state_ev(enum ldmsd_updtr_state state,
		 struct updtr_info *info, void *obj,
		   ev_worker_t src, ev_worker_t dst)
{
	ev_t ev = ev_new(updtr_state_type);
	if (!ev) {
		return ENOMEM;
	}
	EV_DATA(ev, struct updtr_state_data)->updtr_info = info;
	EV_DATA(ev, struct updtr_state_data)->state = state;
	EV_DATA(ev, struct updtr_state_data)->obj = obj;
	EV_DATA(ev, struct updtr_state_data)->ctxt = NULL;
	return ev_post(src, dst, ev, 0);
}

static int
__grp_iter_cb(ldms_set_t grp, const char *name, void *arg)
{
	int len;
	int rc = 0;
	char errstr[512];
	struct ldmsd_match_queue *list = arg;
	struct ldmsd_name_match *ent;


	len = strlen(name);
	ent = malloc(sizeof(*ent));
	if (!ent) {
		LDMSD_LOG_ENOMEM();
		rc = ENOMEM;
		goto err;
	}
	ent->regex_str = malloc(len + 3);
	if (!ent->regex_str) {
		LDMSD_LOG_ENOMEM();
		rc = ENOMEM;
		goto err;
	}
	snprintf(ent->regex_str, len + 2, "^%s$", name);

	rc = ldmsd_compile_regex(&ent->regex, ent->regex_str, errstr, 512);
	if (rc) {
		ldmsd_log(LDMSD_LERROR, "%s: %s", __func__, errstr);
		goto err;
	}
	ent->selector = LDMSD_NAME_MATCH_INST_NAME;
	TAILQ_INSERT_TAIL(list, ent, entry);
	return 0;
err:
	if (ent) {
		free(ent->regex_str);
		free(ent);
	}
	return rc;
}

extern void __updtr_info_destroy(void *x);
int __setgrp_lookup(ldmsd_prdcr_set_t prdset)
{
	int rc;
	struct updtr_info *grp_updtr_info;

	grp_updtr_info = calloc(1, sizeof(*grp_updtr_info));
	if (!grp_updtr_info)
		goto enomem;
	grp_updtr_info->name = strdup(prdset->updtr_name);
	if (!grp_updtr_info->name) {
		free(grp_updtr_info);
		goto enomem;
	}
	grp_updtr_info->is_auto = prdset->is_auto_update;
	grp_updtr_info->push_flags = prdset->push_flags;
	grp_updtr_info->sched = prdset->updt_sched;

	/* __grp_iter_cb will populate match_list. */
	rc = ldmsd_group_iter(prdset->set, __grp_iter_cb,
				&grp_updtr_info->match_list);
	if (rc)
		goto err;
	if (!TAILQ_EMPTY(&grp_updtr_info->match_list)) {
		/*
		 * Send fake updtr_state events to the prdcr_tree worker
		 * to lookup or update the group members.
		 */
		__prdcr_post_updtr_state_ev(LDMSD_UPDTR_STATE_RUNNING,
					    grp_updtr_info, NULL,
					    prdset->worker, prdcr_tree_w);
	} else {
		ref_put(&grp_updtr_info->ref, "create");
	}
	return 0;
enomem:
	LDMSD_LOG_ENOMEM();
	rc = ENOMEM;
err:
	__updtr_info_destroy(grp_updtr_info);
	return rc;
}

static int
__prdset_update_hint_set(ldmsd_prdcr_set_t prdset)
{
	char value[128];
	unsigned long offset_us;
	unsigned long interval_us;
	if (prdset->is_auto_update) {
		offset_us = prdset->updt_hint.offset_us + prdset->updt_hint.offset_skew;
		interval_us = prdset->updt_hint.intrvl_us;
	} else {
		offset_us = prdset->updt_sched.offset_us;
		interval_us = prdset->updt_sched.intrvl_us;
	}
	if (offset_us == LDMSD_UPDT_HINT_OFFSET_NONE)
		snprintf(value, 127, "%ld:", interval_us);
	else
		snprintf(value, 127, "%ld:%ld", interval_us, offset_us);
	return ldms_set_info_set(prdset->set, LDMSD_SET_INFO_UPDATE_HINT_KEY, value);
}

static void resched_prdset_update(ldmsd_prdcr_set_t prdset, struct timespec *to)
{
	clock_gettime(CLOCK_REALTIME, to);
	if (prdset->is_auto_update && prdset->updt_hint.intrvl_us) {
		to->tv_sec += prdset->updt_hint.intrvl_us / 1000000;
		if (prdset->updt_hint.offset_us != LDMSD_UPDT_HINT_OFFSET_NONE) {
			to->tv_nsec =
				(prdset->updt_hint.offset_us
				 + prdset->updt_hint.offset_skew) * 1000;
		} else {
			to->tv_nsec = prdset->updt_hint.offset_skew * 1000;
		}
	} else if (prdset->push_flags) {
		/* schedule immediately */
		to->tv_sec += 1; /* Wait 1 sec to register for push */
		to->tv_nsec = 0;
	} else {
		to->tv_sec += prdset->updt_sched.intrvl_us / 1000000;
		if (prdset->updt_sched.offset_us != LDMSD_UPDT_HINT_OFFSET_NONE) {
			to->tv_nsec = prdset->updt_sched.offset_us * 1000;
		}
	}
}

int schedule_update(ldmsd_prdcr_set_t prdset)
{
	struct timespec to;
	resched_prdset_update(prdset, &to);
	return __post_prdset_state_ev(NULL, prdset->state, prdset,
				prdset->worker, prdset->worker, &to);
}

int prdset_lookup_complete_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e)
{
	if (EV_OK != status)
		return 0;

	int flags;
	int rc = 0;
	ldms_set_t set = EV_DATA(e, struct lookup_data)->set;
	ldmsd_prdcr_set_t prdset = EV_DATA(e, struct lookup_data)->prdset;

	if (LDMS_LOOKUP_OK != EV_DATA(e, struct lookup_data)->status) {
		prdset->state = LDMSD_PRDCR_SET_STATE_START;
		goto out;
	}

	if (!prdset->set) {
		/* This is the first lookup of the set. */
		ldms_set_ref_get(set, "prdcr_set");
		prdset->set = set;
	} else {
		assert(0 == "multiple lookup on the same prdcr_set");
	}
	__prdset_update_hint_set(prdset);
	flags = ldmsd_group_check(prdset->set);
	if (flags & LDMSD_GROUP_IS_GROUP) {
		/*
		 * Lookup the member sets
		 */
		if (__setgrp_lookup(prdset))
			goto out;
	}
	prdset->state = LDMSD_PRDCR_SET_STATE_READY;
	ldmsd_log(LDMSD_LINFO, "Set %s is ready.\n", prdset->inst_name);
	rc = schedule_update(prdset);
out:
	ldmsd_prdcr_set_ref_put(prdset, "lookup");
	ev_put(e);
	return rc;
}

static void __update_set_info(ldmsd_prdcr_set_t prdset, struct ldmsd_updtr_schedule *hint)
{
	if (hint->offset_us != LDMSD_UPDT_HINT_OFFSET_NONE)
		prdset->updt_hint.offset_us = hint->offset_us;
	prdset->updt_hint.intrvl_us = hint->intrvl_us;
}

int
prdset_update_hint_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e)
{
	if (EV_OK != status)
		return 0;

	ldmsd_prdcr_set_t prdset = EV_DATA(e, struct update_hint_data)->prdset;
	struct ldmsd_updtr_schedule *hint = &EV_DATA(e, struct update_hint_data)->hint;
	if (!hint->intrvl_us) {
		/* No update hint */
		return 0;
	}
	__update_set_info(prdset, hint);
	ev_put(e);
	ldmsd_prdcr_set_ref_put(prdset, "update_hint_ev");
	return 0;
}

struct prdcr_lookup_args {
	struct updtr_info *updtr_info;
	ldmsd_prdcr_set_t prdset;
};

void __ldmsd_prdset_lookup_cb(ldms_t xprt, enum ldms_lookup_status status,
					int more, ldms_set_t set, void *arg)
{
	ev_t ev;
	ldmsd_prdcr_set_t prd_set = ((struct prdcr_lookup_args *)arg)->prdset;
	if (status != LDMS_LOOKUP_OK) {
		assert(NULL == set);
		status = (status < 0 ? -status : status);
		if (status == ENOMEM) {
			ldmsd_log(LDMSD_LERROR,
				"prdcr %s: Set memory allocation failure in lookup of "
				"set '%s'. Consider changing the -m parameter on the "
				"command line to a larger value. The current value is %s\n",
				prd_set->prdcr->obj.name,
				prd_set->inst_name,
				ldmsd_get_max_mem_sz_str());
		} else if (status == EEXIST) {
			ldmsd_log(LDMSD_LERROR,
					"prdcr %s: The set '%s' already exists. "
					"It is likely that there are multiple "
					"producers providing a set with the same instance name.\n",
					prd_set->prdcr->obj.name, prd_set->inst_name);
		} else {
			ldmsd_log(LDMSD_LERROR,
				  	"prdcr %s: Error %d in lookup callback of set '%s'\n",
					prd_set->prdcr->obj.name,
					status, prd_set->inst_name);
		}
	}

	ev = ev_new(lookup_complete_type);
	EV_DATA(ev, struct lookup_data)->prdset = prd_set;
	EV_DATA(ev, struct lookup_data)->more = more;
	EV_DATA(ev, struct lookup_data)->set = set;
	EV_DATA(ev, struct lookup_data)->status = status;
	ev_post(NULL, prd_set->worker, ev, 0);
	free(arg);
}

struct str_list;

struct updt_hint_list {
	struct ldmsd_updtr_schedule hint;
	struct rbn rbn;
	struct str_list_s list;
};

struct ldmsd_group_traverse_ctxt {
	ldmsd_prdcr_t prdcr;
	ldmsd_updtr_t updtr;
	ldmsd_updtr_task_t task;
	struct str_list_s str_list;
};

#define SET_COPY_NAME_PREFIX ' '
static void prdset_update_cb(ldms_t t, ldms_set_t set, int status, void *arg)
{
	ldms_set_t lset;
	ldmsd_prdcr_set_t prd_set = arg;
	ev_t ev = ev_new(update_complete_type);
	char thr_name[512];
	pthread_t thr = pthread_self();
	pthread_getname_np(thr, thr_name, 512);

	EV_DATA(ev, struct update_complete_data)->status = status;
	ldmsd_log(LDMSD_LDEBUG, "%s[%ld]: Update complete for Set %s with status %#x\n",
				thr_name, thr, prd_set->inst_name, LDMS_UPD_ERROR(status));

	lset = ldms_set_light_copy(set);
	if (!lset) {
		LDMSD_LOG_ENOMEM();
		return;
	}

	ldmsd_prdcr_set_ref_get(prd_set, "update_complete");
	EV_DATA(ev, struct update_complete_data)->set = lset;
	EV_DATA(ev, struct update_complete_data)->prdset = prd_set;
	ev_post(NULL, prd_set->worker, ev, 0);
}

static int __prdset_ready(ldmsd_prdcr_set_t prdset)
{
	int rc = 0;
	int flags;
	int push_flags = 0;

	if (!prdset->push_flags) {
		prdset->state = LDMSD_PRDCR_SET_STATE_UPDATING;
		flags = ldmsd_group_check(prdset->set);
		if (flags & LDMSD_GROUP_IS_GROUP) {
			/* This is a group */
			rc = __setgrp_lookup(prdset);
			if (rc) {
				ldmsd_log(LDMSD_LINFO, "Failed to update a setgroup member\n");
			}
			ldmsd_prdcr_set_ref_get(prdset, "sched_update");
			rc = ldms_xprt_update(prdset->set, prdset_update_cb, prdset);
			if (rc) {
				ldmsd_log(LDMSD_LINFO, "ldms_xprt_update(%s), "
						"synchronous error %d\n",
						prdset->inst_name, rc);
				ldmsd_prdcr_set_ref_put(prdset, "sched_update");
			}
			goto out;
		} else {
			ldmsd_prdcr_set_ref_get(prdset, "sched_update");
			rc = ldms_xprt_update(prdset->set, prdset_update_cb, prdset);
			if (rc) {
				ldmsd_log(LDMSD_LINFO, "ldms_xprt_update(%s), "
						"synchronous error %d\n",
						prdset->inst_name, rc);
				ldmsd_prdcr_set_ref_put(prdset, "sched_update");
			}
		}
	} else if (0 == (prdset->push_flags & LDMSD_PRDCR_SET_F_PUSH_REG)) {
		if (prdset->push_flags & LDMSD_UPDTR_F_PUSH_CHANGE)
			push_flags = LDMS_XPRT_PUSH_F_CHANGE;
		rc = ldms_xprt_register_push(prdset->set, push_flags,
					     prdset_update_cb, prdset);
		if (rc) {
			/* This message does not repeat */
			ldmsd_log(LDMSD_LERROR, "Register push error %d Set %s\n",
						rc, prdset->inst_name);
		} else {
			/* Only set the flag if we succeed */
			prdset->push_flags |= LDMSD_PRDCR_SET_F_PUSH_REG;
		}
	}
out:
	return rc;
}

int prdset_state_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e)
{
	int rc;
	struct prdset_state_data *prdset_data = EV_DATA(e, struct prdset_state_data);
	struct ldmsd_prdcr_set *prdset = (struct ldmsd_prdcr_set *)prdset_data->obj;

	struct prdcr_lookup_args *lookup_args;
	switch (prdset->state) {
	case LDMSD_PRDCR_SET_STATE_START:
		prdset->state = LDMSD_PRDCR_SET_STATE_LOOKUP;
		assert(prdset->set == NULL);
		lookup_args = malloc(sizeof(*lookup_args));
		if (!lookup_args) {
			LDMSD_LOG_ENOMEM();
			rc = ENOMEM;
			goto out;
		}
		ldmsd_prdcr_set_ref_get(prdset, "lookup");
		lookup_args->prdset = prdset;
		rc = ldms_xprt_lookup(prdset->prdcr->xprt, prdset->inst_name,
				      LDMS_LOOKUP_BY_INSTANCE,
				      __ldmsd_prdset_lookup_cb, lookup_args);
		if (rc) {
			/* If the error is EEXIST, the set is already in the set tree. */
			if (rc == EEXIST) {
				ldmsd_log(LDMSD_LERROR, "Prdcr '%s': "
					"lookup failed synchronously. "
					"The set '%s' already exists. "
					"It is likely that there are more "
					"than one producers pointing to "
					"the set.\n",
					prdset->prdcr->obj.name,
					prdset->inst_name);
			} else {
				ldmsd_log(LDMSD_LINFO, "Synchronous error "
						"%d from ldms_lookup\n", rc);
			}
			prdset->state = LDMSD_PRDCR_SET_STATE_START;
		}
		break;
	case LDMSD_PRDCR_SET_STATE_READY:
		rc = __prdset_ready(prdset);
		if (prdset->push_flags && LDMSD_PRDCR_SET_F_PUSH_REG)
			goto out;
		goto sched;
	case LDMSD_PRDCR_SET_STATE_UPDATING:
		ldmsd_log(LDMSD_LINFO, "prdset '%s': still updating\n",
							prdset->inst_name);
		goto sched;
	case LDMSD_PRDCR_SET_STATE_LOOKUP:
		ldmsd_log(LDMSD_LINFO, "prdset '%s' is being looked up\n",
							prdset->inst_name);
		goto sched;
	case LDMSD_PRDCR_SET_STATE_DELETED:
		/* do nothing */
		break;
	default:
		ldmsd_log(LDMSD_LDEBUG, "%s not supported prdset's state %d\n",
							__func__, prdset->state);
		assert(0 == "Not supported/implemented");
		break;
	}
	return 0;
sched:
	rc = schedule_update(prdset);
out:
	return rc;
}

static int
__prdset_assign_updtr(ldmsd_prdcr_set_t prdset, struct updtr_info *updtr_info)
{
	int rc = 0;
	if (!prdset->updtr_name) {
		prdset->updtr_name = strdup(updtr_info->name);
		if (!prdset->updtr_name) {
			LDMSD_LOG_ENOMEM();
			rc = ENOMEM;
			goto out;
		}
	} else {
		if (0 != strcmp(prdset->updtr_name, updtr_info->name)) {
			/*
			 * Updaters not matched.
			 * Do nothing.
			 */
			goto out;
		}
	}

	prdset->push_flags = updtr_info->push_flags;
	prdset->is_auto_update = updtr_info->is_auto;
	prdset->updt_sched = updtr_info->sched;
	prdset->updt_hint.offset_skew = updtr_info->sched.offset_skew;
out:
	return rc;
}

static int
__prdset_unassign_updtr(ldmsd_prdcr_set_t prdset, struct updtr_info *updtr_info)
{
	int rc = 0;
	if (!prdset->updtr_name || (0 != strcmp(prdset->updtr_name, updtr_info->name))) {
		/*
		 * Do nothing.
		 */
		goto out;
	}

	if (!(prdset->push_flags && LDMSD_PRDCR_SET_F_PUSH_REG)) {
		/*
		 * nothing to do if it is not in the push mode.
		 */
		return 0;
	}

	rc = ldms_xprt_cancel_push(prdset->set);
	if (rc) {
		ldmsd_log(LDMSD_LINFO, "Synchronous error %d: canceling push for Set %s\n",
							rc, prdset->inst_name);
	}
	prdset->push_flags &= ~LDMSD_PRDCR_SET_F_PUSH_REG;

	free(prdset->updtr_name);
	prdset->updtr_name = NULL;
	prdset->updt_sched.intrvl_us = 0;
out:
	return rc;
}

/*
 * There are two scenario for a producer set to receive an updater state event.
 *
 * 1. The updater started after the producer has added the producer set.
 *    updater_start:
 *      updater    -- updtr_state event  --> prdcr_tree
 *      prdcr_tree -- updtr_state event --> prdcr
 *      prdcr      -- updtr_state event --> prdset
 * 2. In contrast, the producer added the producer set after the updater has started.
 *    prdset added:
 *      prdset     -- prdset_state event --> updtr_tree
 *      updtr_tree -- prdset_state event --> updtr
 *      updtr      -- updtr_state  event --> prdset
 *
 */
int prdset_updtr_state_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e)
{
	int rc;
	struct updtr_info *updtr_info;
	enum ldmsd_updtr_state updtr_state;
	struct prdset_info *prdset_info;
	ldmsd_prdcr_set_t prdset;
	char *updtr_info_ref_name;

	updtr_info = EV_DATA(e, struct updtr_state_data)->updtr_info;
	updtr_state = EV_DATA(e, struct updtr_state_data)->state;
	prdset_info = EV_DATA(e, struct updtr_state_data)->ctxt;

	if (prdset_info) {
		/*
		 * The producer set was added after the updater has started
		 */
		prdset = (ldmsd_prdcr_set_t)prdset_info->ctxt;
		updtr_info_ref_name = "create";
	} else {
		/*
		 * The updater started after the producer set has been added.
		 */
		prdset = EV_DATA(e, struct updtr_state_data)->obj;
		updtr_info_ref_name = "prdcr2prdset";
	}

	ldmsd_log(LDMSD_LDEBUG, "prdset '%s' received updtr_state event "
			"with state %s\n", prdset->inst_name,
			ldmsd_updtr_state_str(updtr_state));

	switch (updtr_state) {
	case LDMSD_UPDTR_STATE_RUNNING:
		rc = __prdset_assign_updtr(prdset, updtr_info);
		if (rc)
			goto out;
		rc = __post_prdset_state_ev(NULL, prdset->state, prdset,
					    prdset->worker, prdset->worker, NULL);
		break;
	case LDMSD_UPDTR_STATE_STOPPING:
		rc = __prdset_unassign_updtr(prdset, updtr_info);
		break;
	default:
		assert(0 == "not supported/implemented");
		break;
	}
out:
	if (prdset_info)
		ref_put(&prdset_info->ref, "updtr2prdset");
	else
		ldmsd_prdcr_set_ref_put(prdset, "prdcr2prdset");
	ref_put(&updtr_info->ref, updtr_info_ref_name);
	ev_put(e);
	return rc;
}

int prdset_del_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e)
{
	if (EV_OK != status)
		return 0;

	ldmsd_prdcr_set_t prdset = EV_DATA(e, struct prdset_data)->prdset;
	ldmsd_log(LDMSD_LDEBUG, "Moving the state of prdset '%s' from %s to %s\n",
			prdset->inst_name, ldmsd_prdcr_set_state_str(prdset->state),
			ldmsd_prdcr_set_state_str(LDMSD_PRDCR_SET_STATE_DELETED));
	prdset->state = LDMSD_PRDCR_SET_STATE_DELETED;
	ldmsd_prdcr_set_ref_put(prdset, "new");
	ev_put(e);
	return 0;
}

static void prdcr_reset_set(ldmsd_prdcr_t prdcr, ldmsd_prdcr_set_t prd_set)
{
	ldmsd_log(LDMSD_LINFO, "Removing prdset '%s' from producer '%s'\n",
					prd_set->inst_name, prdcr->obj.name);
	rbt_del(&prdcr->set_tree, &prd_set->rbn);
	ldmsd_prdcr_set_ref_put(prd_set, "prdcr_set_tree");
	__post_prdset_del_ev(prd_set);
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

static int
__parse_update_hint(ldmsd_prdcr_set_t set, ldms_dir_set_t dset, struct ldmsd_updtr_schedule *sched)
{
	char *hint = ldms_dir_set_info_get(dset, LDMSD_SET_INFO_UPDATE_HINT_KEY);
	if (hint) {
		char *endptr;
		char *s = strdup(hint);
		char *tok;
		if (!s) {
			ldmsd_lcritical("%s:%d Memory allocation failure.\n",
				     __func__, __LINE__);
			return ENOMEM;
		}
		tok = strtok_r(s, ":", &endptr);
		sched->offset_us = LDMSD_UPDT_HINT_OFFSET_NONE;
		sched->intrvl_us = strtol(tok, NULL, 0);
		tok = strtok_r(NULL, ":", &endptr);
		if (tok)
			sched->offset_us = strtol(tok, NULL, 0);

		/* Sanity check the hints */
		if (sched->offset_us >= sched->intrvl_us) {
			ldmsd_lerror("set %s: Invalid hint '%s', ignoring hint\n",
					set->inst_name, hint);
			return EINVAL;
		}
		free(s);
	} else {
		sched->intrvl_us = 0;
	}
	return 0;
}

static void _add_cb(ldms_t xprt, ldmsd_prdcr_t prdcr, ldms_dir_set_t dset)
{
	ldmsd_prdcr_set_t set;
	struct prdset_info *prdset_info;
	struct ldmsd_updtr_schedule hint;

	ldmsd_log(LDMSD_LINFO, "Adding the metric set '%s'\n", dset->inst_name);

	/* Check to see if it's already there */
	set = _find_set(prdcr, dset->inst_name);
	if (!set) {
		/* See if the ldms set is already there */
		ldms_set_t xs = ldms_xprt_set_by_name(xprt, dset->inst_name);
		if (xs) {
			ldmsd_log(LDMSD_LCRITICAL, "Received dir_add, prdset is missing, but set %s is present...ignoring",
					dset->inst_name);
			return;
		}
		set = prdcr_set_new(dset->inst_name, dset->schema_name);
		if (!set) {
			ldmsd_log(LDMSD_LERROR, "Memory allocation failure in %s "
				 "for set_name %s\n",
				 __FUNCTION__, dset->inst_name);
			return;
		}
		set->producer_name = strdup(prdcr->obj.name);
		if (!set->producer_name) {
			LDMSD_LOG_ENOMEM();
			return;
		}
		set->prdcr = prdcr;
		ldmsd_prdcr_set_ref_get(set, "prdcr_set_tree");
		rbt_ins(&prdcr->set_tree, &set->rbn);
	} else {
		ldmsd_log(LDMSD_LCRITICAL, "Received a dir_add update for "
			  "'%s', prdcr_set still present with refcount %d, and set "
			  "%p.\n", dset->inst_name, set->ref.ref_count, set->set);
		return;
	}

	if (__parse_update_hint(set, dset, &hint))
		return;

	__update_set_info(set, &hint);
	if (0 != set->updt_hint.intrvl_us) {
		ldmsd_log(LDMSD_LDEBUG, "producer '%s' add set '%s' to hint tree\n",
						prdcr->obj.name, set->inst_name);
 	}

	prdset_info = __prdset_info_get(set, NULL, "prdset2updtr_tree");
	if (!prdset_info) {
		LDMSD_LOG_ENOMEM();
		return;
	}

	/* Tell all updaters that the set exists. */
	(void) __post_prdset_state_ev(prdset_info, set->state, set,
				  prdcr->worker, updtr_tree_w, NULL);
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
		assert(set->ref.ref_count);
		prdcr_reset_set(prdcr, set);
	}
}

static void prdcr_dir_cb_upd(ldms_t xprt, ldms_dir_t dir, ldmsd_prdcr_t prdcr)
{
	int i;
	ldmsd_prdcr_set_t set;
	struct ldmsd_updtr_schedule *hint;
	ev_t update_hint_ev;

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

		update_hint_ev = ev_new(update_hint_type);
		if (!update_hint_ev) {
			LDMSD_LOG_ENOMEM();
			return;
		}

		ldmsd_prdcr_set_ref_get(set, "update_hint_ev");
		EV_DATA(update_hint_ev, struct update_hint_data)->prdset = set;
		hint = &EV_DATA(update_hint_ev, struct update_hint_data)->hint;
		if (__parse_update_hint(set, &dir->set_data[i], hint)) {
			continue;
		}
		ev_post(prdcr->worker, set->worker, update_hint_ev, 0);
	}
}

int
prdcr_dir_complete_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e)
{
	ldmsd_prdcr_t prdcr = EV_DATA(e, struct dir_data)->prdcr;
	ldms_dir_t dir = EV_DATA(e, struct dir_data)->dir;

	switch (dir->type) {
	case LDMS_DIR_LIST:
		prdcr_dir_cb_list(prdcr->xprt, dir, prdcr);
		break;
	case LDMS_DIR_ADD:
		prdcr_dir_cb_add(prdcr->xprt, dir, prdcr);
		break;
	case LDMS_DIR_DEL:
		prdcr_dir_cb_del(prdcr->xprt, dir, prdcr);
		break;
	case LDMS_DIR_UPD:
		prdcr_dir_cb_upd(prdcr->xprt, dir, prdcr);
		break;
	}
	ev_put(e);
	ldmsd_prdcr_put(prdcr); /* Put back the ref taken when the ev's posted */
	ldms_xprt_dir_free(prdcr->xprt, dir);
	return 0;
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
	assert(xprt == prdcr->xprt);
	ev_t dir_ev = ev_new(dir_complete_type);
	if (!dir_ev)
		goto enomem;
	EV_DATA(dir_ev, struct dir_data)->dir = dir;
	EV_DATA(dir_ev, struct dir_data)->prdcr = ldmsd_prdcr_get(prdcr);
	ev_post(NULL, prdcr->worker, dir_ev, 0);
	return ;

enomem:
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory");
	ldms_xprt_dir_free(xprt, dir);
	return;
}

static int __on_subs_resp(ldmsd_req_cmd_t rcmd)
{
	ldmsd_req_cmd_free(rcmd);
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
	ldmsd_prdcr_set_t prdcr_set;
	prdcr_set = ldmsd_prdcr_set_find(prdcr, name);
	if (!prdcr_set)
		return;
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


static int prdcr_connect_ev(ldmsd_prdcr_t prdcr, unsigned long interval_us)
{
	struct timespec to;
	ev_t conn_ev = ev_new(prdcr_connect_type);
	if (!conn_ev)
		return ENOMEM;

	EV_DATA(conn_ev, struct cfgobj_data)->obj = &ldmsd_prdcr_get(prdcr)->obj;
	EV_DATA(conn_ev, struct cfgobj_data)->ctxt = NULL;

	clock_gettime(CLOCK_REALTIME, &to);
	to.tv_sec += interval_us / 1000000;
	to.tv_nsec += (interval_us % 1000000) * 1000;

	ev_post(prdcr->worker, prdcr->worker, conn_ev, &to);
	return 0;
}

int
prdcr_xprt_event_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t ev)
{
	ldmsd_prdcr_t prdcr = (ldmsd_prdcr_t)EV_DATA(ev, struct cfgobj_data)->obj;
	ldms_xprt_event_t xprt_ev = (ldms_xprt_event_t)EV_DATA(ev, struct cfgobj_data)->ctxt;

	ldmsd_log(LDMSD_LDEBUG, "%s:%d Producer %s (%s %s:%d) conn_state: %d %s xprt_event %d\n",
				__func__, __LINE__,
				prdcr->obj.name, prdcr->xprt_name,
				prdcr->host_name, (int)prdcr->port_no,
				prdcr->conn_state,
				conn_state_str(prdcr->conn_state),
				xprt_ev->type);
	switch(xprt_ev->type) {
	case LDMS_XPRT_EVENT_DISCONNECTED:
		prdcr->xprt->disconnected = 1;
		break;
	default:
		assert(prdcr->xprt->disconnected == 0);
		break;
	}
	switch (xprt_ev->type) {
	case LDMS_XPRT_EVENT_CONNECTED:
		ldmsd_log(LDMSD_LINFO, "Producer %s is connected (%s %s:%d)\n",
				prdcr->obj.name, prdcr->xprt_name,
				prdcr->host_name, (int)prdcr->port_no);
		prdcr->conn_state = LDMSD_PRDCR_STATE_CONNECTED;
		if (__prdcr_subscribe(prdcr)) { /* TODO: look into this */
			ldmsd_log(LDMSD_LERROR,
				  "Could not subscribe to stream data on producer %s\n",
				  prdcr->obj.name);
		}
		if (ldms_xprt_dir(prdcr->xprt, prdcr_dir_cb, prdcr,
				  LDMS_DIR_F_NOTIFY))
			ldms_xprt_close(prdcr->xprt);
		break;
	case LDMS_XPRT_EVENT_RECV:
		ldmsd_recv_msg(prdcr->xprt, xprt_ev->data, xprt_ev->data_len);
		break;
	case LDMS_XPRT_EVENT_SET_DELETE:
		__prdcr_remote_set_delete(prdcr, xprt_ev->set_delete.name);
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
	ev_put(ev);
	ldmsd_xprt_event_free(xprt_ev);
	ldmsd_prdcr_put(prdcr);
	return 0;

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
		if (prdcr_connect_ev(prdcr, prdcr->conn_intrvl_us))
			ldmsd_log(LDMSD_LCRITICAL, "Out of memory.\n");
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
	ldmsd_log(LDMSD_LDEBUG, "%s:%d Producer (after reset) %s (%s %s:%d)"
				" conn_state: %d %s\n",
				__func__, __LINE__,
				prdcr->obj.name, prdcr->xprt_name,
				prdcr->host_name, (int)prdcr->port_no,
				prdcr->conn_state,
				conn_state_str(prdcr->conn_state));
	ev_put(ev);
	free(xprt_ev);
	ldmsd_prdcr_put(prdcr);
	return 0;
}

static void prdcr_connect_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg)
{
	ldms_xprt_event_t xprt_ev;
	ldmsd_prdcr_t prdcr = (ldmsd_prdcr_t)cb_arg;
	assert(x == prdcr->xprt);

	xprt_ev = ldmsd_xprt_event_get(e);
	if (!xprt_ev)
		goto enomem;

	ev_t ev = ev_new(prdcr_xprt_type);
	if (!ev)
		goto enomem;
	EV_DATA(ev, struct cfgobj_data)->obj = &ldmsd_prdcr_get(prdcr)->obj;
	EV_DATA(ev, struct cfgobj_data)->ctxt = xprt_ev;

	ev_post(NULL, prdcr->worker, ev, 0);
	return;

enomem:
	LDMSD_LOG_ENOMEM();
	return;
}

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

int
prdcr_connect_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e)
{
	if (EV_OK != status)
		return 0;

	ldmsd_prdcr_t prdcr = (ldmsd_prdcr_t)EV_DATA(e, struct cfgobj_data)->obj;

	switch (prdcr->conn_state) {
	case LDMSD_PRDCR_STATE_DISCONNECTED:
		prdcr_connect(prdcr);
		break;
	case LDMSD_PRDCR_STATE_STOPPED:
	case LDMSD_PRDCR_STATE_STOPPING:
	case LDMSD_PRDCR_STATE_CONNECTING:
	case LDMSD_PRDCR_STATE_CONNECTED:
		/* do nothing */
		break;
	}
	ev_put(e);
	ldmsd_prdcr_put(prdcr); /* put back the reference taken when post \c e */
	return 0;
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

const char *ldmsd_prdcr_state_str(enum ldmsd_prdcr_state state)
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

	prdcr->worker = assign_prdcr_worker();

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

ldmsd_prdcr_t ldmsd_prdcr_first()
{
	return (ldmsd_prdcr_t)ldmsd_cfgobj_first(LDMSD_CFGOBJ_PRDCR);
}

ldmsd_prdcr_t ldmsd_prdcr_next(struct ldmsd_prdcr *prdcr)
{
	return (ldmsd_prdcr_t)ldmsd_cfgobj_next(&prdcr->obj);
}

ldmsd_prdcr_t ldmsd_prdcr_first_re(regex_t regex)
{
	return (ldmsd_prdcr_t)ldmsd_cfgobj_first_re(LDMSD_CFGOBJ_PRDCR, regex);
}

ldmsd_prdcr_t ldmsd_prdcr_next_re(struct ldmsd_prdcr *prdcr, regex_t regex)
{
	return (ldmsd_prdcr_t)ldmsd_cfgobj_next_re(&prdcr->obj, regex);
}

/**
 * Get the first producer set
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
 */
ldmsd_prdcr_set_t ldmsd_prdcr_set_find(ldmsd_prdcr_t prdcr, const char *setname)
{
	struct rbn *rbn = rbt_find(&prdcr->set_tree, setname);
	if (rbn)
		return container_of(rbn, struct ldmsd_prdcr_set, rbn);
	return NULL;
}

extern void ldmsd_req_ctxt_sec_get(ldmsd_req_ctxt_t rctxt, ldmsd_sec_ctxt_t sctxt);

struct prdcr_start_ctxt {
	struct ldmsd_cfgobj_cfg_ctxt base;
	ldmsd_interval interval;
	uint8_t is_deferred;
};

struct prdcr_peercfg_ctxt {
	struct ldmsd_cfgobj_cfg_ctxt base;
	void *ctxt;
};

struct prdcr_subscribe_ctxt {
	struct ldmsd_cfgobj_cfg_ctxt base;
	char *stream_name;
};

void __prdcr_subscribe_ctxt_destroy(void *args)
{
	struct prdcr_subscribe_ctxt *ctxt = args;
	free(ctxt->stream_name);
	free(ctxt);
}

void __prdcr_start_ctxt_destroy(void *args)
{
	struct prdcr_start_ctxt *ctxt = args;
	free(ctxt);
}

struct prdcr_set_route_ctxt {
	struct ldmsd_cfgobj_cfg_ctxt base;
	struct set_route *route;
};

static struct ldmsd_cfgobj_cfg_rsp *
prdcr_stop_handler(ldmsd_prdcr_t prdcr, void *cfg_ctxt)
{
	struct ldmsd_cfgobj_cfg_ctxt *ctxt = (struct ldmsd_cfgobj_cfg_ctxt *)cfg_ctxt;
	struct ldmsd_cfgobj_cfg_rsp *rsp;
	int rc;

	rsp = calloc(1, sizeof(*rsp));
	if (!rsp)
		goto enomem;

	rc = ldmsd_cfgobj_access_check(&prdcr->obj, 0222, &ctxt->sctxt);
	if (rc) {
		rsp->errcode = EPERM;
		rc = asprintf(&rsp->errmsg, "prdcr '%s' permission denied.",
							prdcr->obj.name);
		if (rc < 0)
			goto enomem;
		else
			goto out;
	}
	if (prdcr->conn_state == LDMSD_PRDCR_STATE_STOPPED)
		goto out;

	if (prdcr->conn_state == LDMSD_PRDCR_STATE_STOPPING) {
		rsp->errcode = EBUSY;
		rc = asprintf(&rsp->errmsg, "prdcr '%s' already stopped.",
							prdcr->obj.name);
		if (rc < 0)
			goto enomem;
		else
			goto out;
	}

	if (prdcr->type == LDMSD_PRDCR_TYPE_LOCAL)
		prdcr_reset_sets(prdcr);

	prdcr->obj.perm &= ~LDMSD_PERM_DSTART;
	prdcr->conn_state = LDMSD_PRDCR_STATE_STOPPING;
	if (prdcr->xprt)
		ldms_xprt_close(prdcr->xprt);
	if (!prdcr->xprt)
		prdcr->conn_state = LDMSD_PRDCR_STATE_STOPPED;
out:
	return rsp;
enomem:
	LDMSD_LOG_ENOMEM();
	free(rsp);
	return NULL;
}

static struct ldmsd_cfgobj_cfg_rsp *
prdcr_start_handler(ldmsd_prdcr_t prdcr, void *cfg_ctxt)
{
	struct prdcr_start_ctxt *ctxt = (struct prdcr_start_ctxt *)cfg_ctxt;
	struct ldmsd_cfgobj_cfg_rsp *rsp;
	int rc;

	ldmsd_log(LDMSD_LDEBUG, "%s: prdcr %s -- deferred: %s\n",
			__func__, prdcr->obj.name, (ctxt->is_deferred?"yes":"no"));

	rsp = calloc(1, sizeof(*rsp));
	if (!rsp)
		goto enomem;

	rc = ldmsd_cfgobj_access_check(&prdcr->obj, 0222, &ctxt->base.sctxt);
	if (rc) {
		rsp->errcode = EPERM;
		rc = asprintf(&rsp->errmsg, "prdcr '%s' permission denied.",
							prdcr->obj.name);
		if (rc < 0)
			goto enomem;
		else
			goto out;
	}

	if (prdcr->conn_state != LDMSD_PRDCR_STATE_STOPPED) {
		rsp->errcode = EBUSY;
		rc = asprintf(&rsp->errmsg, "prdcr '%s' is already running.",
							prdcr->obj.name);
		if (rc < 0)
			goto enomem;
		else
			goto out;
	}

	if (ctxt->interval)
		prdcr->conn_intrvl_us = ctxt->interval;
	prdcr->obj.perm |= LDMSD_PERM_DSTART;

	if (!ctxt->is_deferred) {
		prdcr->conn_state = LDMSD_PRDCR_STATE_DISCONNECTED;
		if (prdcr_connect_ev(prdcr, 0))
			goto enomem;
	}

out:
	return rsp;
enomem:
	LDMSD_LOG_ENOMEM();
	free(rsp);
	return NULL;
}

static struct ldmsd_msg_buf *
__prdcr_status_json_obj(ldmsd_prdcr_t prdcr)
{
	ldmsd_prdcr_set_t prv_set;
	int set_count = 0;
	int cnt;
	struct ldmsd_msg_buf *buf = ldmsd_msg_buf_new(1024);
	if (!buf)
		goto enomem;

	cnt = ldmsd_msg_buf_append(buf,
			"{ \"name\":\"%s\","
			"\"type\":\"%s\","
			"\"host\":\"%s\","
			"\"port\":%hu,"
			"\"transport\":\"%s\","
			"\"reconnect_us\":\"%ld\","
			"\"state\":\"%s\","
			"\"sets\": [",
			prdcr->obj.name, ldmsd_prdcr_type2str(prdcr->type),
			prdcr->host_name, prdcr->port_no, prdcr->xprt_name,
			prdcr->conn_intrvl_us,
			ldmsd_prdcr_state_str(prdcr->conn_state));
	if (cnt < 0)
		goto enomem;

	set_count = 0;
	for (prv_set = ldmsd_prdcr_set_first(prdcr); prv_set;
	     prv_set = ldmsd_prdcr_set_next(prv_set)) {
		if (set_count) {
			cnt = ldmsd_msg_buf_append(buf, ",\n");
			if (cnt < 0)
				goto enomem;
		}

		cnt = ldmsd_msg_buf_append(buf,
			"{ \"inst_name\":\"%s\","
			"\"schema_name\":\"%s\","
			"\"state\":\"%s\"}",
			prv_set->inst_name,
			(prv_set->schema_name ? prv_set->schema_name : ""),
			ldmsd_prdcr_set_state_str(prv_set->state));
		if (cnt < 0)
			goto enomem;

		set_count++;
	}
	cnt = ldmsd_msg_buf_append(buf, "]}");
	if (cnt < 0)
		goto enomem;

	return buf;

enomem:
	LDMSD_LOG_ENOMEM();
	ldmsd_msg_buf_free(buf);
	return NULL;
}

static struct ldmsd_cfgobj_cfg_rsp *
prdcr_status_handler(ldmsd_prdcr_t prdcr, void *cfg_ctxt)
{
	struct ldmsd_cfgobj_cfg_rsp *rsp;

	rsp = calloc(1, sizeof(*rsp));
	if (!rsp) {
		LDMSD_LOG_ENOMEM();
		return NULL;
	}
	rsp->ctxt = __prdcr_status_json_obj(prdcr);
	if (!rsp->ctxt) {
		rsp->errcode = ENOMEM;
		free(rsp);
		return NULL;
	}
	return rsp;
}

static struct ldmsd_cfgobj_cfg_rsp *
prdcr_del_handler(ldmsd_prdcr_t prdcr, void *cfg_ctxt)
{
	int rc;
	struct ldmsd_cfgobj_cfg_ctxt *ctxt = (struct ldmsd_cfgobj_cfg_ctxt *)cfg_ctxt;
	struct ldmsd_cfgobj_cfg_rsp *rsp = calloc(1, sizeof(*rsp));
	if (!rsp)
		goto enomem;
	rsp->errcode = ldmsd_cfgobj_access_check(&prdcr->obj, 0222, &ctxt->sctxt);
	if (rsp->errcode) {
		rc = asprintf(&rsp->errmsg, "prdcr '%s' permission denied.",
							  prdcr->obj.name);
		if (rc < 0)
			goto enomem;
		else
			goto out;
	}

	if (prdcr->conn_state != LDMSD_PRDCR_STATE_STOPPED) {
		rsp->errcode = EBUSY;
		rc = asprintf(&rsp->errmsg, "prdcr '%s' is in use.", prdcr->obj.name);
		if (rc < 0)
			goto enomem;
		else
			goto out;
	}

	if (ldmsd_cfgobj_refcount(&prdcr->obj) > 2) {
		rsp->errcode = EBUSY;
		rc = asprintf(&rsp->errmsg, "prdcr '%s' is in use.", prdcr->obj.name);
		if (rc < 0)
			goto enomem;
		else
			goto out;
	}
	rsp->ctxt = ldmsd_prdcr_get(prdcr);

out:
	return rsp;

enomem:
	LDMSD_LOG_ENOMEM();
	return NULL;
}

int __prdcr_stream(ldmsd_prdcr_t prdcr, const char *stream_name,
					enum ldmsd_request req_id,
					struct ldmsd_cfgobj_cfg_rsp *rsp)
{
	int rc;
	ldmsd_prdcr_stream_t s = NULL;
	ldmsd_req_cmd_t rcmd = NULL;
	enum ldmsd_request fwd_req_id;
	if (req_id == LDMSD_PRDCR_SUBSCRIBE_REQ) {
		/* subscribe */
		fwd_req_id = LDMSD_STREAM_SUBSCRIBE_REQ;
		LIST_FOREACH(s, &prdcr->stream_list, entry) {
			if (0 == strcmp(s->name, stream_name)) {
				rsp->errcode = EEXIST;
				rc = asprintf(&rsp->errmsg, "prdcr '%s' already "
						"subscribed stream '%s'.",
						prdcr->obj.name, stream_name);
				if (rc < 0)
					goto enomem;
				goto out;
			}
		}
		rc = ENOMEM;
		s = calloc(1, sizeof *s);
		if (!s)
			goto enomem;
		s->name = strdup(stream_name);
		if (!s->name)
			goto enomem;
		LIST_INSERT_HEAD(&prdcr->stream_list, s, entry);
	} else {
		/* unsubscribe */
		fwd_req_id = LDMSD_STREAM_UNSUBSCRIBE_REQ;
		LIST_FOREACH(s, &prdcr->stream_list, entry) {
			if (0 == strcmp(s->name, stream_name))
				break; /* found */
		}
		if (!s) {
			rsp->errcode = ENOENT;
			rc = asprintf(&rsp->errmsg,
					"Stream %s not found in prdcr '%s'\n",
					stream_name, prdcr->obj.name);
			if (rc < 0)
				goto enomem;
			goto out;
		} else {
			LIST_REMOVE(s, entry);
			free((void*)s->name);
			free(s);
		}
	}

	if (prdcr->conn_state == LDMSD_PRDCR_STATE_CONNECTED) {
		/* issue stream subscribe request right away if connected */
		rcmd = ldmsd_req_cmd_new(prdcr->xprt, fwd_req_id, NULL,
						__on_subs_resp, prdcr);
		if (!rcmd)
			goto enomem;
		rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_NAME, stream_name);
		if (rc) {
			rc = asprintf(&rsp->errmsg, "Failed to forward "
					"the %s cmd to prdcr '%s'",
					prdcr->obj.name, ldmsd_req_id2str(req_id));
			if (rc < 0)
				goto enomem;
			goto out;
		}
		rc = ldmsd_req_cmd_attr_term(rcmd);
		if (rc) {
			rc = asprintf(&rsp->errmsg, "Failed to forward "
					"the %s cmd to prdcr '%s'",
					prdcr->obj.name, ldmsd_req_id2str(req_id));
			if (rc < 0)
				goto enomem;
			goto out;
		}
	}
	return 0;
enomem:
	rc = ENOMEM;
	free(s);
	if (rcmd)
		ldmsd_req_cmd_free(rcmd);
out:
	return rc;
}

static struct ldmsd_cfgobj_cfg_rsp *
prdcr_stream_handler(ldmsd_prdcr_t prdcr, void *cfg_ctxt)
{
	int rc;
	struct prdcr_subscribe_ctxt *ctxt = (struct prdcr_subscribe_ctxt *)cfg_ctxt;
	struct ldmsd_cfgobj_cfg_rsp *rsp = calloc(1, sizeof(*rsp));
	if (!rsp)
		goto enomem;
	rsp->errcode = ldmsd_cfgobj_access_check(&prdcr->obj, 0222, &ctxt->base.sctxt);
	if (rsp->errcode) {
		rc = asprintf(&rsp->errmsg, "prdcr '%s' permission denied.",
							  prdcr->obj.name);
		if (rc < 0)
			goto enomem;
		goto out;
	}
	rsp->errcode = __prdcr_stream(prdcr, ctxt->stream_name,
				      ctxt->base.reqc->req_id, rsp);
	if (ENOMEM == rsp->errcode)
		goto enomem;
out:
	return rsp;
enomem:
	LDMSD_LOG_ENOMEM();
	free(rsp);
	return NULL;
}

void __prdcr_set_status(struct ldmsd_msg_buf *buf, ldmsd_prdcr_set_t prd_set)
{
	struct ldms_timestamp ts = { 0, 0 }, dur = { 0, 0 };
	const char *producer_name = "";

	if (prd_set->set) {
		ts = ldms_transaction_timestamp_get(prd_set->set);
		dur = ldms_transaction_duration_get(prd_set->set);
		producer_name = ldms_set_producer_name_get(prd_set->set);
	}

	ldmsd_msg_buf_append(buf,
		"{ "
		"\"inst_name\":\"%s\","
		"\"schema_name\":\"%s\","
		"\"state\":\"%s\","
		"\"origin\":\"%s\","
		"\"producer\":\"%s\","
		"\"timestamp.sec\":\"%d\","
		"\"timestamp.usec\":\"%d\","
		"\"duration.sec\":\"%u\","
		"\"duration.usec\":\"%u\""
		"}",
		prd_set->inst_name, prd_set->schema_name,
		ldmsd_prdcr_set_state_str(prd_set->state),
		producer_name,
		prd_set->prdcr->obj.name,
		ts.sec, ts.usec,
		dur.sec, dur.usec);
}

static struct ldmsd_cfgobj_cfg_rsp *
prdcr_prdcr_set_status_handler(ldmsd_prdcr_t prdcr, void *cfg_ctxt)
{
	struct ldmsd_cfgobj_cfg_rsp *rsp;
	char *setname, *schema;
	struct ldmsd_cfgobj_cfg_ctxt *ctxt = cfg_ctxt;
	ldmsd_req_ctxt_t reqc = ctxt->reqc;
	ldmsd_prdcr_set_t prdset;
	int count = 0;
	struct ldmsd_msg_buf *buf;

	rsp = calloc(1, sizeof(*rsp));
	if (!rsp) {
		LDMSD_LOG_ENOMEM();
		return NULL;
	}

	buf = ldmsd_msg_buf_new(1024);
	if (!buf) {
		LDMSD_LOG_ENOMEM();
		free(rsp);
		return NULL;
	}
	rsp->ctxt = buf;

	setname = schema = NULL;
	setname = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_INSTANCE);
	schema = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_SCHEMA);

	if (setname) {
		prdset = ldmsd_prdcr_set_find(prdcr, setname);
		if (!prdset) {
			rsp->errcode = ENOENT;
			(void) asprintf(&rsp->errmsg, "prdset %s not found.", setname);
			goto out;
		}
		ldmsd_prdcr_set_ref_get(prdset, "prdcr_set_status");
		if (schema && (0 != strcmp(prdset->schema_name, schema))) {
			rsp->errcode = EINVAL;
			(void) asprintf(&rsp->errmsg,
					"prdset %s is of schema %s not the given schema %s",
					setname, prdset->schema_name, schema);
			goto out;
		}
		__prdcr_set_status(buf, prdset);
	} else {
		count = 0;
		for (prdset = ldmsd_prdcr_set_first(prdcr); prdset;
				prdset = ldmsd_prdcr_set_next(prdset)) {
			if (schema && (0 != strcmp(prdset->schema_name, schema)))
				continue;
			if (count)
				ldmsd_msg_buf_append(buf, ",");
			__prdcr_set_status(buf, prdset);
			count++;
		}
	}

out:
	free(setname);
	free(schema);
	return rsp;
}

static int __prdcr_hint_set_list_json_obj(struct ldmsd_msg_buf *buf,
					struct updt_hint_list *list,
					struct ldmsd_updtr_schedule *updt_hint)
{
	struct str_list_ent_s *str;
	int set_count = 0;

	if (LIST_EMPTY(&list->list))
		return 0;

	ldmsd_msg_buf_append(buf, "{\"interval_us\":\"%ld\",",
				  updt_hint->intrvl_us);
	if (updt_hint->offset_us == LDMSD_UPDT_HINT_OFFSET_NONE) {
		ldmsd_msg_buf_append(buf, "\"offset_us\":\"None\",");
	} else {
		ldmsd_msg_buf_append(buf, "\"offset_us\":\"%ld\",",
					  updt_hint->offset_us);
	}
	ldmsd_msg_buf_append(buf, "\"sets\":[");

	LIST_FOREACH(str, &list->list, entry) {
		if (set_count)
			ldmsd_msg_buf_append(buf, ",");
		ldmsd_msg_buf_append(buf, "\"%s\"", str->str);
		set_count++;
	}
	ldmsd_msg_buf_append(buf, "]}");
	return 0;
}

static struct ldmsd_cfgobj_cfg_rsp *
prdcr_prdset_update_hint_handler(ldmsd_prdcr_t prdcr, void *cfg_ctxt)
{
	struct ldmsd_cfgobj_cfg_rsp * rsp;
	ldmsd_prdcr_set_t prdset;
	struct ldmsd_msg_buf *buf;
	struct rbt hint_tree;
	struct updt_hint_list *list;
	struct rbn *rbn;
	struct str_list_ent_s *str_ent;
	int count = 0;

	rbt_init(&hint_tree, ldmsd_updtr_schedule_cmp);

	rsp = calloc(1, sizeof(*rsp));
	if (!rsp)
		goto enomem;

	buf = ldmsd_msg_buf_new(1024);
	if (!buf)
		goto enomem;

	/* Collect set names by update hints */
	for (prdset = ldmsd_prdcr_set_first(prdcr); prdset;
		prdset = ldmsd_prdcr_set_next(prdset)) {
		rbn = rbt_find(&hint_tree, &prdset->updt_hint);
		if (!rbn) {
			list = malloc(sizeof(*list));
			list->hint = prdset->updt_hint;
			rbn_init(&list->rbn, &list->hint);
			rbt_ins(&hint_tree, &list->rbn);
			LIST_INIT(&list->list);
		} else {
			list = container_of(rbn, struct updt_hint_list, rbn);
		}
		str_ent = malloc(sizeof(*str_ent) + strlen(prdset->inst_name) + 1);
		 if (!str_ent)
			 goto enomem;
		strcpy(str_ent->str, prdset->inst_name);
		LIST_INSERT_HEAD(&list->list, str_ent, entry);
	}

	ldmsd_msg_buf_append(buf,
			"{\"name\":\"%s\","
			"\"hints\":[", prdcr->obj.name);
	rbn = rbt_min(&hint_tree);
	while (rbn) {
		if (0 < count)
			ldmsd_msg_buf_append(buf, ",");
		list = container_of(rbn, struct updt_hint_list, rbn);
		__prdcr_hint_set_list_json_obj(buf, list, list->rbn.key);
		count++;
		rbn = rbn_succ(rbn);
	}
	ldmsd_msg_buf_append(buf, "]}");
	rsp->ctxt = buf;

out:
	/* Clean up the tree */
	while ((rbn = rbt_min(&hint_tree))) {
		rbt_del(&hint_tree, rbn);
		list = container_of(rbn, struct updt_hint_list, rbn);
		while ((str_ent = LIST_FIRST(&list->list))) {
			LIST_REMOVE(str_ent, entry);
			free(str_ent);
		}
		free(list);
	}
	return rsp;

enomem:
	LDMSD_LOG_ENOMEM();
	free(rsp);
	rsp = NULL;
	goto out;
}

extern int set_route_resp_handler(ldmsd_req_cmd_t rcmd);

static struct ldmsd_cfgobj_cfg_rsp *
prdcr_set_route_handler(ldmsd_prdcr_t prdcr, void *cfg_ctxt)
{
	struct ldmsd_cfgobj_cfg_rsp *rsp;
	struct prdcr_set_route_ctxt *ctxt = cfg_ctxt;
	ldmsd_prdcr_set_t prdset;
	struct set_route *route = ctxt->route;

	rsp = calloc(1, sizeof(*rsp));
	if (!rsp)
		goto enomem;

	prdset = ldmsd_prdcr_set_find(prdcr, ctxt->route->inst_name);
	if (!prdset)
		goto out;

	route->origin_type = LDMSD_SET_ORIGIN_PRDCR;
	route->origin_name = strdup(prdcr->obj.name);
	route->prdcr_set_info.host_name = strdup(prdcr->host_name);
	route->prdcr_set_info.update_interval_us = prdset->updt_sched.intrvl_us;
	route->prdcr_set_info.update_offset_us = prdset->updt_sched.offset_us;
	route->prdcr_set_info.sync = (prdset->updt_sched.offset_us == LDMSD_UPDT_HINT_OFFSET_NONE)?0:1;
	route->prdcr_set_info.update_start = prdset->updt_start;
	if (prdset->state == LDMSD_PRDCR_SET_STATE_UPDATING) {
		route->prdcr_set_info.update_end.tv_sec = 0;
		route->prdcr_set_info.update_end.tv_usec = 0;
	} else {
		route->prdcr_set_info.update_end = prdset->updt_end;
	}

	rsp->errcode = ldmsd_set_route_request(prdcr, ctxt->base.reqc,
						ctxt->route->inst_name,
						set_route_resp_handler,
						ctxt->route);
	if (rsp->errcode) {
		asprintf(&rsp->errmsg, "%s: error forwarding set_route_request to "
					"prdcr '%s'", ldmsd_myhostname_get(),
					prdcr->obj.name);
	}
out:
	return rsp;
enomem:
	LDMSD_LOG_ENOMEM();
	free(rsp);
	return NULL;
}

extern struct ldmsd_cfgobj_cfg_rsp *
prdcr_failover_peercfg_handler(ldmsd_prdcr_t prdcr, void *cfg_ctxt);
extern struct ldmsd_cfgobj_cfg_rsp *
prdcr_failover_cfgprdcr_handler(ldmsd_prdcr_t prdcr, void *cfg_ctxt);

int prdcr_cfg_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e)
{
	if (EV_OK != status)
		return 0;

	int rc;
	struct ldmsd_cfgobj_cfg_rsp *rsp = NULL;
	ev_t rsp_ev;

	ldmsd_prdcr_t prdcr = (ldmsd_prdcr_t)EV_DATA(e, struct cfgobj_data)->obj;
	struct ldmsd_cfgobj_cfg_ctxt *cfg_ctxt = EV_DATA(e, struct cfgobj_data)->ctxt;
	ldmsd_req_ctxt_t reqc = cfg_ctxt->reqc;
	errno = 0;
	switch (reqc->req_id) {
	case LDMSD_PRDCR_DEL_REQ:
		rsp = prdcr_del_handler(prdcr, cfg_ctxt);
		break;
	case LDMSD_PRDCR_START_REQ:
	case LDMSD_PRDCR_START_REGEX_REQ:
	case LDMSD_PRDCR_DEFER_START_REQ:
	case LDMSD_PRDCR_DEFER_START_REGEX_REQ:
		rsp = prdcr_start_handler(prdcr, cfg_ctxt);
		break;
	case LDMSD_PRDCR_STOP_REQ:
	case LDMSD_PRDCR_STOP_REGEX_REQ:
		rsp = prdcr_stop_handler(prdcr, cfg_ctxt);
		break;
	case LDMSD_PRDCR_STATUS_REQ:
		rsp = prdcr_status_handler(prdcr, cfg_ctxt);
		break;
	case LDMSD_PRDCR_SUBSCRIBE_REQ:
	case LDMSD_PRDCR_UNSUBSCRIBE_REQ:
		rsp = prdcr_stream_handler(prdcr, cfg_ctxt);
		break;
	case LDMSD_PRDCR_SET_REQ:
		rsp = prdcr_prdcr_set_status_handler(prdcr, cfg_ctxt);
		break;
	case LDMSD_PRDCR_HINT_TREE_REQ:
		rsp = prdcr_prdset_update_hint_handler(prdcr, cfg_ctxt);
		break;
	case LDMSD_SET_ROUTE_REQ:
		rsp = prdcr_set_route_handler(prdcr, cfg_ctxt);
		goto out;
	case LDMSD_FAILOVER_PEERCFG_REQ:
		rsp = prdcr_failover_peercfg_handler(prdcr, cfg_ctxt);
		break;
	case LDMSD_FAILOVER_CFGPRDCR_REQ:
		rsp = prdcr_failover_cfgprdcr_handler(prdcr, cfg_ctxt);
		break;
	default:
		ldmsd_log(LDMSD_LERROR, "%s received an unsupported request ID %d.\n",
							__func__, reqc->req_id);
		rc = ENOTSUP;
		goto err;
	}

	rsp_ev = ev_new(cfgobj_rsp_type);
	if (!rsp_ev)
		goto enomem;

	EV_DATA(rsp_ev, struct cfgobj_rsp_data)->ctxt = cfg_ctxt;
	EV_DATA(rsp_ev, struct cfgobj_rsp_data)->rsp = rsp;
	ev_post(prdcr->worker, prdcr_tree_w, rsp_ev, 0);
out:
	ldmsd_prdcr_put(prdcr); /* Take in prdcr_tree_cfg_actor() */
	ev_put(e);
	return 0;
enomem:
	LDMSD_LOG_ENOMEM();
	rc = ENOMEM;
err:
	ldmsd_prdcr_put(prdcr);
	ev_put(e);
	free(rsp);
	return rc;
}

typedef int (*prdset_actor_fn)(ldmsd_prdcr_t prdcr, ldmsd_prdcr_set_t prdset, void *args);
int __prdset_filter(ldmsd_prdcr_t prdcr, struct ldmsd_match_queue *filter,
					prdset_actor_fn actor, void *args)
{
	int rc;
	ldmsd_prdcr_set_t prdset;
	struct ldmsd_name_match *m;
	char *s;

	if (TAILQ_EMPTY(filter)) {
		for (prdset = ldmsd_prdcr_set_first(prdcr); prdset;
				prdset = ldmsd_prdcr_set_next(prdset)) {
			(void) actor(prdcr, prdset, args);
		}
	} else {
		for (prdset = ldmsd_prdcr_set_first(prdcr); prdset;
				prdset = ldmsd_prdcr_set_next(prdset)) {
			TAILQ_FOREACH(m, filter, entry) {
				if (LDMSD_NAME_MATCH_INST_NAME == m->selector)
					s = prdset->inst_name;
				else
					s = prdset->schema_name;
				rc = regexec(&m->regex, s, 0, NULL, 0);
				if (0 == rc) {
					/* matched */
					(void) actor(prdcr, prdset, args);
					break;
				}
			}
		}
	}
	return 0;
}

static int
__prdcr_post_updtr_state(ldmsd_prdcr_t prdcr, ldmsd_prdcr_set_t prdset, void *args)
{
	int rc;
	struct updtr_state_data *data = args;
	struct updtr_info *info = data->updtr_info;

	ldmsd_prdcr_set_ref_get(prdset, "prdcr2prdset");
	ref_get(&info->ref, "prdcr2prdset");
	rc = __prdcr_post_updtr_state_ev(data->state, info, prdset,
				prdcr->worker, prdset->worker);
	if (rc) {
		ldmsd_log(LDMSD_LINFO, "%s: prdcr '%s' failed to post "
				"an event to prdset '%s', error %d\n",
				__func__, prdcr->obj.name, prdset->inst_name, rc);
		ref_put(&info->ref, "prdcr2prdset");
		ldmsd_prdcr_set_ref_put(prdset, "prdcr2prdset");
	}
	return rc;
}

int prdcr_updtr_state_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e)
{
	if (EV_OK != status)
		return 0;

	struct updtr_state_data *data;
	enum ldmsd_updtr_state updtr_state;
	ldmsd_prdcr_t prdcr;
	int rc;

	data = EV_DATA(e, struct updtr_state_data);
	updtr_state = EV_DATA(e, struct updtr_state_data)->state;
	prdcr = EV_DATA(e, struct updtr_state_data)->obj;

	switch (updtr_state) {
	case LDMSD_UPDTR_STATE_RUNNING:
	case LDMSD_UPDTR_STATE_STOPPING:
		rc = __prdset_filter(prdcr, &data->updtr_info->match_list,
					__prdcr_post_updtr_state, data);
		break;
	default:
		break;
	}
	ref_put(&data->updtr_info->ref, "prdcr_tree2prdcr");
	ldmsd_prdcr_put(prdcr);
	ev_put(e);
	return rc;
}

/* Producer tree worker event handlers */

/*
 * Aggregate the status of each producer.
 *
 * Without an error of a producer, the response to the client looks like this
 *    [ {<producer status} , {}, ... ].
 * In case an producer responding with an error, the response will be
 * <prdcr A's errmsg>, <prdcr B's errmsg>. \c reqc->errcode is the same
 * as the first error occurrence.
 */
static int
tree_status_rsp_handler(ldmsd_req_ctxt_t reqc, struct ldmsd_cfgobj_cfg_rsp *rsp,
						struct ldmsd_cfgobj_cfg_ctxt *ctxt)
{
	struct ldmsd_msg_buf *buf = (struct ldmsd_msg_buf *)rsp->ctxt;

	if (reqc->errcode) {
		if (!rsp->errcode) {
			/* Ignore the status */
			goto out;
		}
		(void) linebuf_printf(reqc, ", %s", rsp->errmsg);
		goto out;
	}

	if (rsp->errcode) {
		reqc->errcode = rsp->errcode;
		(void) snprintf(reqc->line_buf, reqc->line_len, "%s", rsp->errmsg);
		goto out;
	}

	if (buf->off) {
		if (1 < ctxt->num_recv)
			(void) linebuf_printf(reqc, ",");
		(void) linebuf_printf(reqc, "%s", buf->buf);
	}

	if (ldmsd_cfgtree_done(ctxt))
		(void) linebuf_printf(reqc, "]");

out:
	ldmsd_msg_buf_free(buf);
	free(rsp->errmsg);
	free(rsp);
	return 0;
}

static int
tree_cfg_rsp_handler(ldmsd_req_ctxt_t reqc, struct ldmsd_cfgobj_cfg_rsp *rsp,
					struct ldmsd_cfgobj_cfg_ctxt *ctxt)
{
	if (rsp->errcode) {
		if (!reqc->errcode)
			reqc->errcode = rsp->errcode;
		if (ctxt->num_recv > 1)
			(void)linebuf_printf(reqc, ", ");
		(void)linebuf_printf(reqc, "%s", rsp->errmsg);
	}

	free(rsp->errmsg);
	free(rsp->ctxt);
	free(rsp);
	return 0;
}

extern int ldmsd_cfgobj_tree_del_rsp_handler(ldmsd_req_ctxt_t reqc,
				struct ldmsd_cfgobj_cfg_rsp *rsp,
				struct ldmsd_cfgobj_cfg_ctxt *ctxt,
				enum ldmsd_cfgobj_type type);
extern int tree_failover_peercfg_rsp_handler(ldmsd_req_ctxt_t reqc,
					     struct ldmsd_cfgobj_cfg_rsp *rsp,
					     struct ldmsd_cfgobj_cfg_ctxt *ctxt,
					     enum ldmsd_cfgobj_type type);
extern int tree_failover_cfgobj_rsp_handler(ldmsd_req_ctxt_t reqc,
					     struct ldmsd_cfgobj_cfg_rsp *rsp,
					     struct ldmsd_cfgobj_cfg_ctxt *ctxt);
int prdcr_tree_cfg_rsp_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e)
{
	if (EV_OK != status)
		return 0;

	void *rsp = EV_DATA(e, struct cfgobj_rsp_data)->rsp;
	struct ldmsd_cfgobj_cfg_ctxt *ctxt = EV_DATA(e, struct cfgobj_rsp_data)->ctxt;
	struct ldmsd_req_ctxt *reqc = ctxt->reqc;
	int rc = 0;

	ctxt->num_recv++;
	if (!rsp)
		goto out;

	switch (reqc->req_id) {
	case LDMSD_PRDCR_ADD_REQ:
		assert(0 == "Impossible case");
		break;
	case LDMSD_PRDCR_DEL_REQ:
		rc = ldmsd_cfgobj_tree_del_rsp_handler(reqc, rsp, ctxt,
						   LDMSD_CFGOBJ_PRDCR);
		break;
	case LDMSD_PRDCR_DEFER_START_REQ:
	case LDMSD_PRDCR_START_REQ:
	case LDMSD_PRDCR_DEFER_START_REGEX_REQ:
	case LDMSD_PRDCR_START_REGEX_REQ:
	case LDMSD_PRDCR_STOP_REQ:
	case LDMSD_PRDCR_STOP_REGEX_REQ:
	case LDMSD_PRDCR_SUBSCRIBE_REQ:
	case LDMSD_PRDCR_UNSUBSCRIBE_REQ:
		rc = tree_cfg_rsp_handler(reqc, rsp, ctxt);
		break;
	case LDMSD_PRDCR_STATUS_REQ:
	case LDMSD_PRDCR_SET_REQ:
	case LDMSD_PRDCR_HINT_TREE_REQ:
		rc = tree_status_rsp_handler(reqc, rsp, ctxt);
		if (ldmsd_cfgtree_done(ctxt)) {
			/* All updaters have sent back the responses */
			ldmsd_send_json_response(reqc, reqc->line_buf);
			ref_put(&ctxt->ref, "create");
		}
		goto out;
	case LDMSD_SET_ROUTE_REQ:
		rc = tree_cfg_rsp_handler(reqc, rsp, ctxt);
		if (ldmsd_cfgtree_done(ctxt)) {
			ref_put(&ctxt->ref, "create");
		}
		goto out;
	case LDMSD_FAILOVER_PEERCFG_REQ:
		rc = tree_failover_peercfg_rsp_handler(reqc, rsp, ctxt, LDMSD_CFGOBJ_PRDCR);
		break;
	case LDMSD_FAILOVER_CFGPRDCR_REQ:
		rc = tree_failover_cfgobj_rsp_handler(reqc, rsp, ctxt);
		goto out;
	default:
		assert(0 == "impossible case");
		rc = EINTR;
		goto out;
	}

	if (rc) {
		reqc->errcode = EINTR;
		(void) snprintf(reqc->line_buf, reqc->line_len,
				"LDMSD: failed to construct the response.");
		rc = 0;
	}

	if (ldmsd_cfgtree_done(ctxt)) {
		/* All producers have sent back the responses */
		ldmsd_send_req_response(reqc, reqc->line_buf);
		ref_put(&ctxt->ref, "create");
	}

out:
	ref_put(&ctxt->ref, "prdcr_tree");
	ev_put(e);
	return rc;
}

static int tree_prdcr_stop_regex_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	char *regex_str = NULL;
	regex_t regex;
	ldmsd_prdcr_t prdcr, nxt_prdcr;
	struct ldmsd_cfgobj_cfg_ctxt *ctxt = NULL;

	ctxt = calloc(1, sizeof(*ctxt));
	if (!ctxt)
		goto enomem;
	ref_init(&ctxt->ref, "create", free, ctxt);

	reqc->errcode = 0;

	regex_str = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_REGEX);
	if (!regex_str) {
		reqc->errcode = EINVAL;
		rc = linebuf_printf(reqc,
				"The attribute 'regex' is required by prdcr_start_regex.");
		goto send_reply;
	}

	reqc->errcode = ldmsd_compile_regex(&regex, regex_str, reqc->line_buf,
								reqc->line_len);
	if (reqc->errcode)
		goto send_reply;

	ldmsd_req_ctxt_sec_get(reqc, &ctxt->sctxt);

	prdcr = ldmsd_prdcr_first_re(regex);
	while (prdcr) {
		nxt_prdcr = ldmsd_prdcr_next_re(prdcr, regex);
		rc = regexec(&regex, prdcr->obj.name, 0, NULL, 0);
		if (rc)
			goto next;
		if (!nxt_prdcr)
			ctxt->is_all = 1;
		ref_get(&ctxt->ref, "prdcr_tree");
		rc = ldmsd_cfgtree_post2cfgobj(&prdcr->obj, prdcr_tree_w,
						prdcr->worker, reqc, ctxt);
		if (rc) {
			ref_put(&ctxt->ref, "prdcr_tree");
			if (ENOMEM == rc)
				goto enomem;
			reqc->errcode = EINTR;
			rc = linebuf_printf(reqc,
					"Failed to handle the prdcr_start command.");
			goto send_reply;
		}
	next:
		prdcr = nxt_prdcr;
	}
	goto out;
enomem:
	reqc->errcode = ENOMEM;
	rc = linebuf_printf(reqc, "LDMSD: out of memory.");
	LDMSD_LOG_ENOMEM();
	free(ctxt);
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
out:
	free(regex_str);
	regfree(&regex);
	return 0;
}


static int __tree_forward2prdcr(ldmsd_req_ctxt_t reqc)
{
	int rc;
	char *name = NULL;
	ldmsd_prdcr_t prdcr;
	struct ldmsd_cfgobj_cfg_ctxt *ctxt = NULL;

	ctxt = calloc(1, sizeof(*ctxt));
	if (!ctxt)
		goto enomem;
	ref_init(&ctxt->ref, "create", free, ctxt);

	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!name) {
		reqc->errcode = EINVAL;
		rc = linebuf_printf(reqc,
				"The attribute 'name' is required.");
		goto send_reply;
	}

	ldmsd_req_ctxt_sec_get(reqc, &ctxt->sctxt);

	prdcr = ldmsd_prdcr_find(name);
	if (!prdcr) {
		reqc->errcode = ENOENT;
		rc = linebuf_printf(reqc, "prdcr '%s' does not exist.", name);
		goto send_reply;
	}

	ctxt->is_all = 1;
	ref_get(&ctxt->ref, "prdcr_tree");
	rc = ldmsd_cfgtree_post2cfgobj(&prdcr->obj, prdcr_tree_w,
					prdcr->worker, reqc, ctxt);
	ldmsd_prdcr_put(prdcr); /* Put back ldmsd_prdcr_find()'s reference */
	if (rc) {
		ref_put(&ctxt->ref, "prdcr_tree");
		if (ENOMEM == rc)
			goto enomem;
		reqc->errcode = EINTR;
		rc = linebuf_printf(reqc,
				"Failed to handle the prdcr_start command.");
		goto send_reply;
	}

	goto out;

enomem:
	reqc->errcode = ENOMEM;
	(void) linebuf_printf(reqc, "LDMSD: out of memory.");
	free(ctxt);
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
out:
	free(name);
	return 0;
}

static inline int tree_prdcr_del_handler(ldmsd_req_ctxt_t reqc)
{
	return __tree_forward2prdcr(reqc);
}

static inline int tree_prdcr_stop_handler(ldmsd_req_ctxt_t reqc)
{
	return __tree_forward2prdcr(reqc);
}

static int tree_prdcr_start_regex_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	char *regex_str, *interval_str;
	regex_str = interval_str = NULL;
	regex_t regex;
	ldmsd_prdcr_t prdcr, nxt_prdcr;
	struct prdcr_start_ctxt *ctxt = NULL;

	ctxt = calloc(1, sizeof(*ctxt));
	if (!ctxt)
		goto enomem;
	ref_init(&ctxt->base.ref, "create", __prdcr_start_ctxt_destroy, ctxt);

	if (LDMSD_PRDCR_DEFER_START_REGEX_REQ == reqc->req_id)
		ctxt->is_deferred = 1;

	reqc->errcode = 0;

	regex_str = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_REGEX);
	if (!regex_str) {
		reqc->errcode = EINVAL;
		rc = linebuf_printf(reqc,
				"The attribute 'regex' is required by prdcr_start_regex.");
		goto send_reply;
	}

	reqc->errcode = ldmsd_compile_regex(&regex, regex_str, reqc->line_buf,
								reqc->line_len);
	if (reqc->errcode)
		goto send_reply;

	interval_str = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_INTERVAL);
	if (interval_str) {
		reqc->errcode = ldmsd_time_dur_str2us(interval_str, &ctxt->interval);
		if (reqc->errcode) {
			rc = linebuf_printf(reqc,
					"The interval '%s' is invalid.", interval_str);
			goto send_reply;
		}
	}

	ldmsd_req_ctxt_sec_get(reqc, &ctxt->base.sctxt);

	prdcr = ldmsd_prdcr_first_re(regex);
	while (prdcr) {
		nxt_prdcr = ldmsd_prdcr_next_re(prdcr, regex);
		rc = regexec(&regex, prdcr->obj.name, 0, NULL, 0);
		if (rc)
			goto next;
		if (!nxt_prdcr)
			ctxt->base.is_all = 1;
		ref_get(&ctxt->base.ref, "prdcr_tree");
		rc = ldmsd_cfgtree_post2cfgobj(&prdcr->obj, prdcr_tree_w,
						prdcr->worker, reqc, &ctxt->base);
		if (rc) {
			ref_put(&ctxt->base.ref, "prdcr_tree");
			if (ENOMEM == rc)
				goto enomem;
			reqc->errcode = EINTR;
			rc = linebuf_printf(reqc,
					"Failed to handle the %s command.",
					ldmsd_req_id2str(reqc->req_id));
			goto send_reply;
		}
	next:
		prdcr = nxt_prdcr;
	}
	goto out;
enomem:
	reqc->errcode = ENOMEM;
	rc = linebuf_printf(reqc, "LDMSD: out of memory.");
	LDMSD_LOG_ENOMEM();
	free(ctxt);
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
out:
	free(regex_str);
	free(interval_str);
	regfree(&regex);
	return 0;
}

static int tree_prdcr_start_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	char *name, *interval_str;
	name = interval_str = NULL;
	ldmsd_prdcr_t prdcr;
	struct prdcr_start_ctxt *ctxt = NULL;

	ctxt = calloc(1, sizeof(*ctxt));
	if (!ctxt)
		goto enomem;
	ref_init(&ctxt->base.ref, "create", __prdcr_start_ctxt_destroy, ctxt);

	if (LDMSD_PRDCR_DEFER_START_REQ == reqc->req_id)
		ctxt->is_deferred = 1;

	reqc->errcode = 0;

	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!name) {
		reqc->errcode = EINVAL;
		rc = linebuf_printf(reqc,
				"The attribute 'name' is required by prdcr_start.");
		goto send_reply;
	}

	interval_str = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_INTERVAL);
	if (interval_str) {
		reqc->errcode = ldmsd_time_dur_str2us(interval_str, &ctxt->interval);
		if (reqc->errcode) {
			rc = linebuf_printf(reqc,
					"The interval '%s' is invalid.", interval_str);
			goto send_reply;
		}
	}

	ldmsd_req_ctxt_sec_get(reqc, &ctxt->base.sctxt);

	prdcr = ldmsd_prdcr_find(name);
	if (!prdcr) {
		reqc->errcode = ENOENT;
		(void) linebuf_printf(reqc, "The producer specified does not exist.");
		goto send_reply;
	}

	ctxt->base.is_all = 1;
	ref_get(&ctxt->base.ref, "prdcr_tree");
	rc = ldmsd_cfgtree_post2cfgobj(&prdcr->obj, prdcr_tree_w,
					prdcr->worker, reqc, &ctxt->base);
	ldmsd_prdcr_put(prdcr); /* Put back ldmsd_prdcr_find()'s reference */
	if (rc) {
		ref_put(&ctxt->base.ref, "prdcr_tree");
		if (ENOMEM == rc)
			goto enomem;
		reqc->errcode = EINTR;
		rc = linebuf_printf(reqc,
				"Failed to handle the prdcr_start command.");
		goto send_reply;
	}

	goto out;

enomem:
	reqc->errcode = ENOMEM;
	LDMSD_LOG_ENOMEM();
	(void) linebuf_printf(reqc, "LDMSD: out of memory.");
	free(ctxt);
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
out:
	free(name);
	free(interval_str);
	return 0;
}

static int tree_prdcr_status_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	char *name;
	ldmsd_prdcr_t prdcr, nxt_prdcr;
	struct ldmsd_cfgobj_cfg_ctxt *ctxt = calloc(1, sizeof(*ctxt));
	if (!ctxt)
		goto enomem;
	ref_init(&ctxt->ref, "create", free, ctxt);

	ldmsd_req_ctxt_sec_get(reqc, &ctxt->sctxt);

	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);

	rc = linebuf_printf(reqc, "[");

	if (name) {
		prdcr = ldmsd_prdcr_find(name);
		if (!prdcr) {
			reqc->errcode = ENOENT;
			(void) linebuf_printf(reqc, "prdcr '%s' not found.", name);
		} else {
			ref_get(&ctxt->ref, "prdcr_tree");
			rc = ldmsd_cfgtree_post2cfgobj(&prdcr->obj, prdcr_tree_w,
							prdcr->worker, reqc, ctxt);
			ldmsd_prdcr_put(prdcr);
			if (rc) {
				ref_put(&ctxt->ref, "prdcr_tree");
				if (ENOMEM == rc)
					goto enomem;
				reqc->errcode = EINTR;
				rc = linebuf_printf(reqc,
						"Failed to handle the prdcr_start command.");
				goto send_reply;
			}
			ctxt->is_all = 1;
		}
	} else {
		prdcr = ldmsd_prdcr_first();
		while (prdcr) {
			nxt_prdcr = ldmsd_prdcr_next(prdcr);
			if (!nxt_prdcr)
				ctxt->is_all = 1;
			ref_get(&ctxt->ref, "prdcr_tree");
			rc = ldmsd_cfgtree_post2cfgobj(&prdcr->obj, prdcr_tree_w,
							prdcr->worker, reqc, ctxt);
			if (rc) {
				ref_put(&ctxt->ref, "prdcr_tree");
				if (ENOMEM == rc)
					goto enomem;
				reqc->errcode = EINTR;
				rc = linebuf_printf(reqc,
						"Failed to handle the prdcr_start command.");
				goto send_reply;
			}
			prdcr = nxt_prdcr;
		}
		if (0 == ctxt->num_sent) {
			linebuf_printf(reqc, "]");
			goto send_reply;
		}
	}

	goto out;
enomem:
	reqc->errcode = ENOMEM;
	LDMSD_LOG_ENOMEM();
	(void) linebuf_printf(reqc, "LDMSD: out of memory.");
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
out:
	free(name);
	return rc;

}

static int tree_prdset_status_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	char *name;
	ldmsd_prdcr_t prdcr, nxt_prdcr;
	struct ldmsd_cfgobj_cfg_ctxt *ctxt = calloc(1, sizeof(*ctxt));
	if (!ctxt)
		goto enomem;
	ref_init(&ctxt->ref, "create", free, ctxt);

	ldmsd_req_ctxt_sec_get(reqc, &ctxt->sctxt);

	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);

	rc = linebuf_printf(reqc, "[");

	if (name) {
		prdcr = ldmsd_prdcr_find(name);
		if (!prdcr) {
			reqc->errcode = ENOENT;
			(void) linebuf_printf(reqc, "prdcr '%s' not found.", name);
		} else {
			ref_get(&ctxt->ref, "prdcr_tree");
			rc = ldmsd_cfgtree_post2cfgobj(&prdcr->obj, prdcr_tree_w,
							prdcr->worker, reqc, ctxt);
			ldmsd_prdcr_put(prdcr);
			if (rc) {
				ref_put(&ctxt->ref, "prdcr_tree");
				if (ENOMEM == rc)
					goto enomem;
				reqc->errcode = EINTR;
				rc = linebuf_printf(reqc,
						"Failed to handle the prdcr_start command.");
				goto send_reply;
			}
			ctxt->is_all = 1;
		}
	} else {
		prdcr = ldmsd_prdcr_first();
		while (prdcr) {
			nxt_prdcr = ldmsd_prdcr_next(prdcr);
			if (!nxt_prdcr)
				ctxt->is_all = 1;
			if (0 == rbt_card(&prdcr->set_tree))
				goto next;
			ref_get(&ctxt->ref, "prdcr_tree");
			rc = ldmsd_cfgtree_post2cfgobj(&prdcr->obj, prdcr_tree_w,
							prdcr->worker, reqc, ctxt);
			if (rc) {
				ref_put(&ctxt->ref, "prdcr_tree");
				if (ENOMEM == rc)
					goto enomem;
				reqc->errcode = EINTR;
				rc = linebuf_printf(reqc,
						"Failed to handle the prdcr_start command.");
				goto send_reply;
			}
		next:
			prdcr = nxt_prdcr;
		}
		if (0 == ctxt->num_sent) {
			linebuf_printf(reqc, "]");
			goto send_reply;
		}
	}

	goto out;
enomem:
	reqc->errcode = ENOMEM;
	LDMSD_LOG_ENOMEM();
	(void) linebuf_printf(reqc, "LDMSD: out of memory.");
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
out:
	free(name);
	return rc;

}

static int tree_prdset_update_hint_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	char *name;
	ldmsd_prdcr_t prdcr, nxt_prdcr;
	struct ldmsd_cfgobj_cfg_ctxt *ctxt = calloc(1, sizeof(*ctxt));
	if (!ctxt)
		goto enomem;
	ref_init(&ctxt->ref, "create", free, ctxt);

	ldmsd_req_ctxt_sec_get(reqc, &ctxt->sctxt);

	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);

	rc = linebuf_printf(reqc, "[");

	if (name) {
		prdcr = ldmsd_prdcr_find(name);
		if (!prdcr) {
			reqc->errcode = ENOENT;
			(void) linebuf_printf(reqc, "prdcr '%s' not found.", name);
		} else {
			ref_get(&ctxt->ref, "prdcr_tree");
			rc = ldmsd_cfgtree_post2cfgobj(&prdcr->obj, prdcr_tree_w,
							prdcr->worker, reqc, ctxt);
			ldmsd_prdcr_put(prdcr);
			if (rc) {
				ref_put(&ctxt->ref, "prdcr_tree");
				if (ENOMEM == rc)
					goto enomem;
				reqc->errcode = EINTR;
				rc = linebuf_printf(reqc,
						"Failed to handle the prdcr_start command.");
				goto send_reply;
			}
			ctxt->is_all = 1;
		}
	} else {
		prdcr = ldmsd_prdcr_first();
		while (prdcr) {
			nxt_prdcr = ldmsd_prdcr_next(prdcr);
			if (!nxt_prdcr)
				ctxt->is_all = 1;
			ref_get(&ctxt->ref, "prdcr_tree");
			rc = ldmsd_cfgtree_post2cfgobj(&prdcr->obj, prdcr_tree_w,
							prdcr->worker, reqc, ctxt);
			if (rc) {
				ref_put(&ctxt->ref, "prdcr_tree");
				if (ENOMEM == rc)
					goto enomem;
				reqc->errcode = EINTR;
				rc = linebuf_printf(reqc,
						"Failed to handle the prdcr_start command.");
				goto send_reply;
			}
			prdcr = nxt_prdcr;
		}
		if (0 == ctxt->num_sent) {
			linebuf_printf(reqc, "]");
			goto send_reply;
		}
	}

	goto out;
enomem:
	reqc->errcode = ENOMEM;
	LDMSD_LOG_ENOMEM();
	(void) linebuf_printf(reqc, "LDMSD: out of memory.");
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
out:
	free(name);
	return rc;
	return 0;
}

static int tree_set_route_handler(ldmsd_req_ctxt_t reqc, void *cfg_ctxt)
{
	struct prdcr_set_route_ctxt *ctxt = NULL;
	ldmsd_prdcr_t prdcr;
	int rc;

	ctxt = calloc(1, sizeof(*ctxt));
	if (!ctxt)
		goto enomem;
	ref_init(&ctxt->base.ref, "create", free, ctxt);
	ctxt->route = (struct set_route *)cfg_ctxt;
	reqc->errcode = 0;

	for (prdcr = ldmsd_prdcr_first(); prdcr; prdcr = ldmsd_prdcr_next(prdcr)) {
		ref_get(&ctxt->base.ref, "prdcr_tree");
		rc = ldmsd_cfgtree_post2cfgobj(&prdcr->obj, prdcr_tree_w,
						prdcr->worker, reqc, &ctxt->base);
		if (rc) {
			ref_put(&ctxt->base.ref, "prdcr_tree");
			if (ENOMEM == rc)
				goto enomem;
			reqc->errcode = EINTR;
			rc = linebuf_printf(reqc,
				"%s: Failed to handle the set_route", __func__);
			goto send_reply;
		}
	}
	return 0;
enomem:
	reqc->errcode = ENOMEM;
	rc = ENOMEM;
	LDMSD_LOG_ENOMEM();
	(void) linebuf_printf(reqc, "LDMSD: out of memory");
send_reply:
	free(ctxt);
	ldmsd_send_req_response(reqc, reqc->line_buf);
	return rc;

}

static int tree_prdcr_add_handler(ldmsd_req_ctxt_t reqc)
{
	ldmsd_prdcr_t prdcr;
	char *name, *host, *xprt, *attr_name, *type_s, *port_s, *interval_s;
	char *auth;
	enum ldmsd_prdcr_type type = -1;
	unsigned short port_no = 0;
	int interval_us = -1;
	size_t rc;
	uid_t uid;
	gid_t gid;
	int perm;
	char *perm_s = NULL;

	reqc->errcode = 0;
	name = host = xprt = type_s = port_s = interval_s = auth = NULL;

	attr_name = "name";
	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!name)
		goto einval;

	attr_name = "type";
	type_s = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_TYPE);
	if (!type_s) {
		goto einval;
	} else {
		type = ldmsd_prdcr_str2type(type_s);
		if ((int)type < 0) {
			rc = linebuf_printf(reqc,
					"The attribute type '%s' is invalid.",
					type_s);
			if (rc < 0)
				goto enomem;
			reqc->errcode = EINVAL;
			goto send_reply;
		}
		if (type == LDMSD_PRDCR_TYPE_LOCAL) {
			rc = linebuf_printf(reqc,
					"Producer with type 'local' is "
					"not supported.");
			if (rc < 0)
				goto enomem;
			reqc->errcode = EINVAL;
			goto send_reply;
		}
	}

	attr_name = "xprt";
	xprt = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_XPRT);
	if (!xprt)
		goto einval;

	attr_name = "host";
	host = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_HOST);
	if (!host)
		goto einval;

	attr_name = "port";
	port_s = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_PORT);
	if (!port_s) {
		goto einval;
	} else {
		long ptmp = 0;
		ptmp = strtol(port_s, NULL, 0);
		if (ptmp < 1 || ptmp > USHRT_MAX) {
			goto einval;
		}
		port_no = (unsigned)ptmp;
	}

	attr_name = "interval";
	interval_s = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_INTERVAL);
	if (!interval_s) {
		goto einval;
	} else {
		 interval_us = strtol(interval_s, NULL, 0);
	}

	auth = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_AUTH);

	struct ldmsd_sec_ctxt sctxt;
	ldmsd_req_ctxt_sec_get(reqc, &sctxt);
	uid = sctxt.crd.uid;
	gid = sctxt.crd.gid;

	perm = 0770;
	perm_s = ldmsd_req_attr_str_value_get_by_name(reqc, "perm");
	if (perm_s)
		perm = strtol(perm_s, NULL, 0);

	ldmsd_log(LDMSD_LDEBUG, "%s: prdcr %s\n", __func__, name);
	prdcr = ldmsd_prdcr_new_with_auth(name, xprt, host, port_no, type,
					  interval_us, auth, uid, gid, perm);
	if (!prdcr) {
		if (errno == EEXIST)
			goto eexist;
		else if (errno == EAFNOSUPPORT)
			goto eafnosupport;
		else if (errno == ENOENT)
			goto ebadauth;
		else
			goto enomem;
	}

	goto send_reply;
ebadauth:
	reqc->errcode = ENOENT;
	rc = linebuf_printf(reqc,
			"Authentication name not found, check the auth_add configuration.");
	goto send_reply;
enomem:
	reqc->errcode = ENOMEM;
	(void) linebuf_printf(reqc, "Memory allocation failed.");
	goto send_reply;
eexist:
	reqc->errcode = EEXIST;
	rc = linebuf_printf(reqc, "The prdcr %s already exists.", name);
	goto send_reply;
eafnosupport:
	reqc->errcode = EAFNOSUPPORT;
	rc = linebuf_printf(reqc, "Error resolving hostname '%s'\n", host);
	goto send_reply;
einval:
	reqc->errcode = EINVAL;
	rc = linebuf_printf(reqc, "The attribute '%s' is required.", attr_name);
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(name);
	free(type_s);
	free(port_s);
	free(interval_s);
	free(host);
	free(xprt);
	free(perm_s);
	free(auth);
	return 0;
}

static int tree_prdcr_stream_handler(ldmsd_req_ctxt_t reqc)
{
	int rc, count;
	char *prdcr_regex = NULL;
	regex_t regex;
	ldmsd_prdcr_t prdcr, nxt_prdcr;
	struct prdcr_subscribe_ctxt *ctxt = calloc(1, sizeof(*ctxt));
	if (!ctxt)
		goto enomem;
	ref_init(&ctxt->base.ref, "create", __prdcr_subscribe_ctxt_destroy, ctxt);

	ldmsd_req_ctxt_sec_get(reqc, &ctxt->base.sctxt);
	reqc->errcode = 0;

	prdcr_regex = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_REGEX);
	if (!prdcr_regex) {
		reqc->errcode = EINVAL;
		rc = linebuf_printf(reqc,
				"The attribute 'regex' is required by prdcr_stop_regex.");
		goto send_reply;
	}

	ctxt->stream_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_STREAM);
	if (!ctxt->stream_name) {
		reqc->errcode = EINVAL;
		rc = linebuf_printf(reqc,
				"The attribute 'stream' is required by prdcr_subscribe_regex.");
		goto send_reply;
	}

	reqc->errcode = ldmsd_compile_regex(&regex, prdcr_regex,
					    reqc->line_buf, reqc->line_len);
	if (reqc->errcode)
		goto send_reply;

	count = 0;
	prdcr = ldmsd_prdcr_first_re(regex);
	while (prdcr) {
		nxt_prdcr = ldmsd_prdcr_next_re(prdcr, regex);
		rc = regexec(&regex, prdcr->obj.name, 0, NULL, 0);
		if (rc)
			goto next;
		if (!nxt_prdcr)
			ctxt->base.is_all = 1;
		ref_get(&ctxt->base.ref, "prdcr_tree");
		rc = ldmsd_cfgtree_post2cfgobj(&prdcr->obj, prdcr_tree_w,
					       prdcr->worker, reqc, &ctxt->base);
		if (rc) {
			ref_put(&ctxt->base.ref, "prdcr_tree");
			if (ENOMEM == rc)
				goto enomem;
			reqc->errcode = EINTR;
			rc = linebuf_printf(reqc,
					"Failed to handle the %s command.",
					ldmsd_req_id2str(reqc->req_id));
			goto send_reply;
		}
		count++;
	next:
		prdcr = nxt_prdcr;
	}
	if (0 == count) {
		/* No prdcr matched the regex */
		ref_put(&ctxt->base.ref, "create");
		goto send_reply;
	}
	goto out;
enomem:
	reqc->errcode = ENOMEM;
	LDMSD_LOG_ENOMEM();
	(void) linebuf_printf(reqc, "LDMSD: Out of memory.");
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
out:
	if (prdcr_regex)
		regfree(&regex);
	free(prdcr_regex);
	return 0;
}

/*
 * Sends a JSON formatted summary of Producer statistics as follows:
 *
 * {
 *   "prdcr_count" : <int>,
 *   "stopped" : <int>,
 *   "disconnected" : <int>,
 *   "connecting" : <int>,
 * 	 "connected" : <int>,
 *   "stopping"	: <int>,
 *   "compute_time" : <int>
 * }
 */
static int tree_prdcr_stats_handler(ldmsd_req_ctxt_t reqc)
{
	ldmsd_prdcr_t prdcr;
	struct timespec start, end;
	int prdcr_count = 0, stopped_count = 0, disconnected_count = 0,
		connecting_count = 0, connected_count = 0, stopping_count = 0,
		set_count = 0;

	(void)clock_gettime(CLOCK_REALTIME, &start);
	for (prdcr = ldmsd_prdcr_first(); prdcr;
			prdcr = ldmsd_prdcr_next(prdcr)) {
		prdcr_count += 1;
		switch (prdcr->conn_state) {
		case LDMSD_PRDCR_STATE_STOPPED:
			stopped_count++;
			break;
		case LDMSD_PRDCR_STATE_DISCONNECTED:
			disconnected_count++;
			break;
		case LDMSD_PRDCR_STATE_CONNECTING:
			connecting_count++;
			break;
		case LDMSD_PRDCR_STATE_CONNECTED:
			connected_count++;
			break;
		case LDMSD_PRDCR_STATE_STOPPING:
			stopping_count++;
			break;
		}
		set_count += rbt_card(&prdcr->set_tree);
	}

	linebuf_printf(reqc, "{");
	linebuf_printf(reqc, " \"prdcr_count\": %d,\n", prdcr_count);
	linebuf_printf(reqc, " \"stopped_count\": %d,\n", stopped_count);
	linebuf_printf(reqc, " \"disconnected_count\": %d,\n", disconnected_count);
	linebuf_printf(reqc, " \"connecting_count\": %d,\n", connecting_count);
	linebuf_printf(reqc, " \"connected_count\": %d,\n", connected_count);
	linebuf_printf(reqc, " \"stopping_count\": %d,\n", stopping_count);
	linebuf_printf(reqc, " \"set_count\": %d,\n", set_count);
	(void)clock_gettime(CLOCK_REALTIME, &end);
	uint64_t compute_time = ldms_timespec_diff_us(&start, &end);
	linebuf_printf(reqc, " \"compute_time\": %ld\n", compute_time);
	linebuf_printf(reqc, "}"); /* end */

	ldmsd_send_req_response(reqc, reqc->line_buf);
	return 0;
}

extern int tree_failover_peercfg_handler(ldmsd_req_ctxt_t reqc, enum ldmsd_cfgobj_type type);
extern int tree_failover_cfgprdcr_handler(ldmsd_req_ctxt_t reqc, void *fctxt);
int prdcr_tree_cfg_actor(ev_worker_t src, ev_worker_t dst,
					ev_status_t status, ev_t e)
{
	if (EV_OK != status)
		return 0;

	struct ldmsd_req_ctxt *reqc = EV_DATA(e, struct cfg_data)->reqc;
	void *ctxt = EV_DATA(e, struct cfg_data)->ctxt;
	int rc = 0;

	switch (reqc->req_id) {
	case LDMSD_PRDCR_ADD_REQ:
		rc = tree_prdcr_add_handler(reqc);
		break;
	case LDMSD_PRDCR_DEL_REQ:
		rc = tree_prdcr_del_handler(reqc);
		break;
	case LDMSD_PRDCR_DEFER_START_REQ:
	case LDMSD_PRDCR_START_REQ:
		rc = tree_prdcr_start_handler(reqc);
		break;
	case LDMSD_PRDCR_DEFER_START_REGEX_REQ:
	case LDMSD_PRDCR_START_REGEX_REQ:
		rc = tree_prdcr_start_regex_handler(reqc);
		break;
	case LDMSD_PRDCR_STOP_REQ:
		rc = tree_prdcr_stop_handler(reqc);
		break;
	case LDMSD_PRDCR_STOP_REGEX_REQ:
		rc = tree_prdcr_stop_regex_handler(reqc);
		break;
	case LDMSD_PRDCR_STATUS_REQ:
		rc = tree_prdcr_status_handler(reqc);
		break;
	case LDMSD_PRDCR_SUBSCRIBE_REQ:
	case LDMSD_PRDCR_UNSUBSCRIBE_REQ:
		rc = tree_prdcr_stream_handler(reqc);
		break;
	case LDMSD_PRDCR_STATS_REQ:
		rc = tree_prdcr_stats_handler(reqc);
		break;
	case LDMSD_PRDCR_SET_REQ:
		rc = tree_prdset_status_handler(reqc);
		break;
	case LDMSD_PRDCR_HINT_TREE_REQ:
		rc = tree_prdset_update_hint_handler(reqc);
		break;
	case LDMSD_SET_ROUTE_REQ:
		rc = tree_set_route_handler(reqc, ctxt);
		break;
	case LDMSD_FAILOVER_PEERCFG_REQ:
		rc = tree_failover_peercfg_handler(reqc, LDMSD_CFGOBJ_PRDCR);
		break;
	case LDMSD_FAILOVER_CFGPRDCR_REQ:
		rc = tree_failover_cfgprdcr_handler(reqc, ctxt);
		break;
	default:
		ldmsd_log(LDMSD_LERROR, "%s not support '%s'\n",
				__func__, ldmsd_req_id2str(reqc->req_id));
		rc = ENOTSUP;
		goto out;
	}
out:
	ev_put(e);
	return rc;
}

int __tree_post_updtr_state(ldmsd_prdcr_t prdcr, void *args)
{
	int rc;
	struct updtr_state_data *data = args;
	struct updtr_info *info = data->updtr_info;
	enum ldmsd_updtr_state state = data->state;

	ref_get(&info->ref, "prdcr_tree2prdcr");
	ldmsd_prdcr_get(prdcr);
	rc = __prdcr_post_updtr_state_ev(state, info, prdcr,
					  prdcr_tree_w, prdcr->worker);
	if (rc) {
		ldmsd_log(LDMSD_LINFO, "%s: prdcr_tree failed to post "
				"an event to prdcr '%s' error %d\n",
				__func__, prdcr->obj.name, rc);
		ref_put(&info->ref, "prdcr_tree2prdcr");
		ldmsd_prdcr_put(prdcr);
	}
	return rc;
}

typedef int (*actor_fn)(ldmsd_prdcr_t prdcr, void *args);
int __prdcr_filter(struct ldmsd_match_queue *filter, actor_fn actor, void *args)
{
	ldmsd_prdcr_t prdcr;
	struct ldmsd_name_match *fent;
	int rc, _rc = 0;

	for (prdcr = ldmsd_prdcr_first(); prdcr; prdcr = ldmsd_prdcr_next(prdcr)) {
		TAILQ_FOREACH(fent, filter, entry) {
			if (fent->is_regex) {
				rc = regexec(&fent->regex, prdcr->obj.name, 0, NULL, 0);
			} else {
				rc = strcmp(prdcr->obj.name, fent->regex_str);
			}
			if (0 == rc) {
				/* matched */
				rc = actor(prdcr, args);
				if (rc) {
					ldmsd_log(LDMSD_LERROR, "%s: prdcr %s: "
						"actor() failed with error %d\n",
						__func__, prdcr->obj.name, rc);
					if (0 == _rc)
						_rc = -1;
				}
			}
		}
	}
	return _rc;
}

int prdcr_tree_updtr_state_actor(ev_worker_t src, ev_worker_t dst,
					ev_status_t status, ev_t e)
{
	if (EV_OK != status)
		return 0;

	int rc;
	struct updtr_state_data *data = EV_DATA(e, struct updtr_state_data);
	struct updtr_info *updtr_info = data->updtr_info;
	enum ldmsd_updtr_state state = data->state;

	switch (state) {
	case LDMSD_UPDTR_STATE_RUNNING:
	case LDMSD_UPDTR_STATE_STOPPING:
		rc = __prdcr_filter(&updtr_info->prdcr_list,
					__tree_post_updtr_state, data);
		break;
	default:
		assert(0 == "Unsupported updtr's state");
		break;
	}
	ref_put(&updtr_info->ref, "create");
	ev_put(e);
	return rc;
}

static int __add_prdcr_name(ldmsd_prdcr_t prdcr, void *args)
{
	struct ldmsd_match_queue *list = args;
	struct ldmsd_name_match *ent;
	struct ldmsd_msg_buf *buf;
	size_t cnt;

	buf = ldmsd_msg_buf_new(128);
	if (!buf)
		return ENOMEM;

	ent = calloc(1, sizeof(*ent));
	if (!ent)
		goto enomem;
	ent->is_regex = 0;
	cnt = ldmsd_msg_buf_append(buf,
				"{\"name\":\"%s\","
				"\"host\":\"%s\","
				"\"port\":%hu,"
				"\"transport\":\"%s\","
				"\"state\":\"%s\"}",
				prdcr->obj.name,
				prdcr->host_name,
				prdcr->port_no,
				prdcr->xprt_name,
				ldmsd_prdcr_state_str(prdcr->conn_state));
	if (cnt < 0)
		goto enomem;
	ent->regex_str = ldmsd_msg_buf_detach(buf);
	ldmsd_msg_buf_free(buf);
	TAILQ_INSERT_TAIL(list, ent, entry);
	return 0;
enomem:
	ldmsd_msg_buf_free(buf);
	free(ent);
	return ENOMEM;
}

int prdcr_tree_prdcr_filter_req_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e)
{
	if (EV_OK != status)
		return 0;

	int rc;
	struct ldmsd_match_queue filter = EV_DATA(e, struct prdcr_filter_req_data)->filter;
	struct ldmsd_match_queue *prdcr_list = &EV_DATA(e, struct prdcr_filter_req_data)->prdcr_list;

	TAILQ_INIT(prdcr_list);
	rc = __prdcr_filter(&filter, __add_prdcr_name, prdcr_list);
	if (rc)
		ldmsd_match_queue_free(prdcr_list);

	EV_DATA(e, struct prdcr_filter_req_data)->rsp->errcode = rc;
	ev_post(prdcr_tree_w, EV_DATA(e, struct prdcr_filter_req_data)->resp_worker, e, NULL);
	return 0;
}
