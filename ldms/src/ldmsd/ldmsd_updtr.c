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

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <coll/rbt.h>
#include <ovis_util/util.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldms_xprt.h"
#include "config.h"

void ldmsd_updtr___del(ldmsd_cfgobj_t obj)
{
	ldmsd_updtr_t updtr = (ldmsd_updtr_t)obj;
	ldmsd_name_match_t match;
	while (!LIST_EMPTY(&updtr->match_list) ) {
		match = LIST_FIRST(&updtr->match_list);
		if (match->regex_str)
			free(match->regex_str);
		regfree(&match->regex);
		LIST_REMOVE(match, entry);
		free(match);
	}
	struct rbn *rbn;
	ldmsd_prdcr_ref_t prdcr_ref;
	while (!rbt_empty(&updtr->prdcr_tree)) {
		rbn = rbt_min(&updtr->prdcr_tree);
		rbt_del(&updtr->prdcr_tree, rbn);
		prdcr_ref = container_of(rbn, struct ldmsd_prdcr_ref, rbn);
		ldmsd_cfgobj_put(&prdcr_ref->prdcr->obj);
		free(prdcr_ref);
	}
	ldmsd_cfgobj___del(obj);
}


static int updtr_sched_offset_skew_get()
{
	int skew = LDMSD_UPDTR_OFFSET_INCR_DEFAULT;
	char *str = getenv(LDMSD_UPDTR_OFFSET_INCR_VAR);
	if (str)
		skew = strtol(str, NULL, 0);
	return skew;
}

static ldmsd_prdcr_ref_t updtr_prdcr_ref_first(ldmsd_updtr_t updtr)
{
	struct rbn *rbn = rbt_min(&updtr->prdcr_tree);
	if (!rbn)
		return NULL;
	return container_of(rbn, struct ldmsd_prdcr_ref, rbn);
}

static ldmsd_prdcr_ref_t updtr_prdcr_ref_next(ldmsd_prdcr_ref_t ref)
{
	struct rbn *rbn = rbn_succ(&ref->rbn);
	if (!rbn)
		return NULL;
	return container_of(rbn, struct ldmsd_prdcr_ref, rbn);
}

#ifdef LDMSD_UPDATE_TIME
void __updt_time_get(struct ldmsd_updt_time *updt_time)
{
	__sync_fetch_and_add(&updt_time->ref, 1);
}

void __updt_time_put(struct ldmsd_updt_time *updt_time)
{
	if (0 == __sync_sub_and_fetch(&updt_time->ref, 1)) {
		if (updt_time->update_start.tv_sec != 0) {
			struct timeval end;
			gettimeofday(&end, NULL);
			updt_time->updtr->duration =
				ldmsd_timeval_diff(&updt_time->update_start,
						&end);
		} else {
			updt_time->updtr->duration = -1;
		}
		free(updt_time);
	}
}
#endif /* LDMSD_UDPATE_TIME */

static void updtr_task_cb(ldmsd_task_t task, void *arg);

static void
__grp_info_lookup_cb(ldms_t xprt, enum ldms_lookup_status status,
			    int more, ldms_set_t set, void *arg)
{
	ldmsd_prdcr_set_t prd_set = arg;
	pthread_mutex_lock(&prd_set->lock);
	prd_set->state = LDMSD_PRDCR_SET_STATE_READY;
	ldmsd_prdcr_set_ref_put(prd_set, "grp_lookup");
	pthread_mutex_unlock(&prd_set->lock);
}

static void updtr_update_cb(ldms_t t, ldms_set_t set, int status, void *arg)
{
	int flags;
	int rc;
	uint64_t gn;
	const char *name;
	ldmsd_prdcr_set_t prd_set = arg;
	int ready = 0;
	int errcode;

	pthread_mutex_lock(&prd_set->lock);
#ifdef LDMSD_UPDATE_TIME
	gettimeofday(&prd_set->updt_end, NULL);
	prd_set->updt_duration = ldmsd_timeval_diff(&prd_set->updt_start,
							&prd_set->updt_end);
	__updt_time_put(prd_set->updt_time);
#endif /* LDMSD_UPDATE_TIME */
	errcode = LDMS_UPD_ERROR(status);
	ldmsd_log(LDMSD_LDEBUG, "Update complete for Set %s with status %#x\n",
					prd_set->inst_name, status);
	if (errcode) {
		char *op_s;
		if (0 == (status & LDMS_UPD_F_PUSH))
			op_s = "update";
		else
			op_s = "push";
		ldmsd_log(LDMSD_LINFO, "Set %s: %s completing with "
					"bad status %d\n",
					prd_set->inst_name, op_s,errcode);
		goto out;
	}

	if (!ldms_set_is_consistent(set)) {
		ldmsd_log(LDMSD_LINFO, "Set %s is inconsistent.\n", prd_set->inst_name);
		goto set_ready;
	}

	flags = ldmsd_group_check(prd_set->set);
	if (flags & LDMSD_GROUP_MODIFIED) {
		/* Group modified, need info update --> re-lookup the info */
		ldmsd_prdcr_set_ref_get(prd_set, "grp_lookup");
		name = ldms_set_instance_name_get(prd_set->set);
		rc = ldms_xprt_lookup(t, name, LDMS_LOOKUP_SET_INFO,
				      __grp_info_lookup_cb, prd_set);
		if (rc) {
			ldmsd_prdcr_set_ref_put(prd_set, "grp_lookup");
			goto set_ready;
		}
		/* __grp_info_lookup_cb() will change the state */
		pthread_mutex_unlock(&prd_set->lock);
		goto out;
	}

	gn = ldms_set_data_gn_get(set);
	if (prd_set->last_gn == gn) {
		ldmsd_log(LDMSD_LINFO, "Set %s oversampled %"PRIu64" == %"PRIu64".\n",
			  prd_set->inst_name, prd_set->last_gn, gn);
		goto set_ready;
	}
	prd_set->last_gn = gn;

	ldmsd_strgp_ref_t str_ref;
	LIST_FOREACH(str_ref, &prd_set->strgp_list, entry) {
		ldmsd_strgp_t strgp = str_ref->strgp;
		ldmsd_prdcr_set_ref_get(prd_set, "store_ev");
		ev_post(updater, strgp->worker, str_ref->store_ev, NULL);
	}
set_ready:
	if ((status & LDMS_UPD_F_MORE) == 0)
		ready = 1;
out:
	pthread_mutex_unlock(&prd_set->lock);
	if (0 == errcode) {
		ldmsd_log(LDMSD_LINFO, "Pushing set %p %s\n", prd_set->set,
						prd_set->inst_name);
		int rc = ldms_xprt_push(prd_set->set);
		if (rc) {
			ldmsd_log(LDMSD_LERROR, "Failed to push set %s\n",
						prd_set->inst_name);
		}
	}
	if (ready) {
		pthread_mutex_lock(&prd_set->lock);
		prd_set->state = LDMSD_PRDCR_SET_STATE_READY;
		pthread_mutex_unlock(&prd_set->lock);
	}
	if (0 == (status & (LDMS_UPD_F_PUSH|LDMS_UPD_F_MORE))) {
		/*
		 * This is an pull update. Put the reference
		 * taken before calling ldms_xprt_update.
		 */
		ldmsd_prdcr_set_ref_put(prd_set, "xprt_update");
	}
	return;
}

struct str_list_ent_s {
	LIST_ENTRY(str_list_ent_s) entry;
	char str[]; /* '\0' terminated string */
};

struct ldmsd_group_traverse_ctxt {
	ldmsd_prdcr_t prdcr;
	ldmsd_updtr_t updtr;
	LIST_HEAD(, str_list_ent_s) str_list;
};

static int
__grp_iter_cb(ldms_set_t grp, const char *name, void *arg)
{
	int len, sz;
	struct str_list_ent_s *ent;
	struct ldmsd_group_traverse_ctxt *ctxt = arg;

	len = strlen(name);
	sz = sizeof(*ent) + len + 1;
	ent = malloc(sz);
	if (!ent)
		return ENOMEM;
	snprintf(ent->str, len+1, "%s", name);
	LIST_INSERT_HEAD(&ctxt->str_list, ent, entry);
	return 0;
}

void resched_prd_set(ldmsd_updtr_t updtr, ldmsd_prdcr_set_t prd_set, struct timespec *to)
{
	clock_gettime(CLOCK_REALTIME, to);
	if (updtr->is_auto_task && prd_set->updt_hint.intrvl_us) {
		to->tv_sec += prd_set->updt_hint.intrvl_us / 1000000;
		if (prd_set->updt_hint.offset_us != LDMSD_UPDT_HINT_OFFSET_NONE) {
			to->tv_nsec =
				(prd_set->updt_hint.offset_us
				 + prd_set->updt_hint.offset_skew) * 1000;
		} else {
			to->tv_nsec = prd_set->updt_hint.offset_skew * 1000;
		}
	} else {
		to->tv_sec += updtr->sched.intrvl_us / 1000000;
		if (updtr->sched.offset_us != LDMSD_UPDT_HINT_OFFSET_NONE) {
			to->tv_nsec =
				(updtr->sched.offset_us
				 + updtr->sched.offset_skew) * 1000;
		} else {
			to->tv_nsec = updtr->sched.offset_skew * 1000;
		}
	}
}

static int schedule_set_updates(ldmsd_updtr_t updtr,
				ldmsd_prdcr_set_t prd_set,
				ldmsd_name_match_t match)
{
	int rc = 0;
	int flags;
	char *op_s;
	/* The reference will be put back in update_cb */
	ldmsd_log(LDMSD_LDEBUG, "Schedule an update for set %s\n",
					prd_set->inst_name);

	/* If a match condition is not specified, everything matches */
	if (match) {
		if (match->selector == LDMSD_NAME_MATCH_INST_NAME)
			rc = regexec(&match->regex, prd_set->inst_name, 0, NULL, 0);
		else
			rc = regexec(&match->regex, prd_set->schema_name, 0, NULL, 0);
		if (rc)
			return 0;
	}

	int push_flags = 0;
	struct str_list_ent_s *ent;
	gettimeofday(&prd_set->updt_start, NULL);
#ifdef LDMSD_UPDATE_TIME
	__updt_time_get(prd_set->updt_time);
	if (prd_set->updt_time->update_start.tv_sec == 0)
		prd_set->updt_time->update_start = prd_set->updt_start;
#endif
	if (!updtr->push_flags) {
		op_s = "Updating";
		prd_set->state = LDMSD_PRDCR_SET_STATE_UPDATING;
		flags = ldmsd_group_check(prd_set->set);
		if (flags & LDMSD_GROUP_IS_GROUP) {
			struct ldmsd_group_traverse_ctxt ctxt;
			LIST_INIT(&ctxt.str_list);

			/* This is a group */
			ctxt.prdcr = prd_set->prdcr;
			ctxt.updtr = updtr;
			/* __grp_iter_cb() will populate ctxt.str_list */
			rc = ldmsd_group_iter(prd_set->set,
					      __grp_iter_cb, &ctxt);
			if (rc)
				goto out;

			LIST_FOREACH(ent, &ctxt.str_list, entry) {
				ldmsd_prdcr_set_t pset =
					ldmsd_prdcr_set_find(prd_set->prdcr, ent->str);
				if (!pset)
					continue; /* It is OK. Try again next iteration */
				if (pset->state != LDMSD_PRDCR_SET_STATE_READY)
					continue; /* It is OK. The set might not be ready */
				rc = schedule_set_updates(updtr, pset, NULL);
				if (rc)
					break;
			}
			while ((ent = LIST_FIRST(&ctxt.str_list))) {
				LIST_REMOVE(ent, entry);
				free(ent);
			}
		}

		EV_DATA(prd_set->update_ev, struct update_data)->updtr = updtr;
		EV_DATA(prd_set->update_ev, struct update_data)->prd_set = prd_set;
		EV_DATA(prd_set->update_ev, struct update_data)->reschedule = 1;
		struct timespec to;
		resched_prd_set(updtr, prd_set, &to);
		ldmsd_prdcr_set_ref_get(prd_set, "update_ev");
		ev_post(updtr->worker, updtr->worker, prd_set->update_ev, &to);

	} else if (0 == (prd_set->push_flags & LDMSD_PRDCR_SET_F_PUSH_REG)) {
		op_s = "Registering push for";
		prd_set->push_flags |= LDMSD_PRDCR_SET_F_PUSH_REG;
		ldmsd_prdcr_set_ref_get(prd_set, "push");
		if (updtr->push_flags & LDMSD_UPDTR_F_PUSH_CHANGE)
			push_flags = LDMS_XPRT_PUSH_F_CHANGE;
		rc = ldms_xprt_register_push(prd_set->set, push_flags,
					     updtr_update_cb, prd_set);
		if (rc) {
			/* This message does not repeat */
			ldmsd_log(LDMSD_LERROR, "Register push error %d Set %s\n",
						rc, prd_set->inst_name);
		}
	}
out:
	if (rc) {
#ifdef LDMSD_UPDATE_TIME
		__updt_time_put(prd_set->updt_time);
#endif
		ldmsd_log(LDMSD_LINFO, "Synchronous error %d: %s Set %s\n",
						rc, op_s, prd_set->inst_name);
		if (!updtr->push_flags) {
			ldmsd_prdcr_set_ref_put(prd_set, "update");
		}
	}
	return rc;
}

static int cancel_set_updates(ldmsd_updtr_t updtr, ldmsd_prdcr_set_t prd_set)
{
	int rc;
	struct timeval end;
	ldmsd_log(LDMSD_LDEBUG, "Cancel push for set %s\n", prd_set->inst_name);
	assert(prd_set->set);
	if (prd_set->update_ev)
		EV_DATA(prd_set->update_ev, struct update_data)->reschedule = 0;
	if (!(prd_set->push_flags & LDMSD_PRDCR_SET_F_PUSH_REG))
		return 0;
	rc = ldms_xprt_cancel_push(prd_set->set);
	if (rc) {
		ldmsd_log(LDMSD_LINFO, "Synchronous error %d: canceling push for Set %s\n",
			  rc, prd_set->inst_name);
	}
	/* Put the push reference */
	prd_set->push_flags &= ~LDMSD_PRDCR_SET_F_PUSH_REG;
	ldmsd_prdcr_set_ref_put(prd_set, "push");
	return rc;
}

static void cancel_prdcr_updates(ldmsd_updtr_t updtr,
				 ldmsd_prdcr_t prdcr, ldmsd_name_match_t match)
{
	int rc;
	ldmsd_prdcr_lock(prdcr);
	if (prdcr->conn_state != LDMSD_PRDCR_STATE_CONNECTED)
		goto out;
	ldmsd_prdcr_set_t prd_set;
	for (prd_set = ldmsd_prdcr_set_first(prdcr); prd_set;
	     prd_set = ldmsd_prdcr_set_next(prd_set)) {
		int rc;
		const char *str;
		if (!prd_set->push_flags)
			continue;

		/* If a match condition is not specified, everything matches */
		if (!match) {
			cancel_set_updates(updtr, prd_set);
			continue;
		}
		rc = 1;
		if (match->selector == LDMSD_NAME_MATCH_INST_NAME)
			str = prd_set->inst_name;
		else
			str = prd_set->schema_name;
		rc = regexec(&match->regex, str, 0, NULL, 0);
		if (!rc) {
			cancel_set_updates(updtr, prd_set);
		}
	}
out:
	ldmsd_prdcr_unlock(prdcr);
}

static void cancel_push(ldmsd_updtr_t updtr)
{
	ldmsd_name_match_t match;

	if (!LIST_EMPTY(&updtr->match_list)) {
		LIST_FOREACH(match, &updtr->match_list, entry) {
			ldmsd_prdcr_ref_t ref;
			for (ref = updtr_prdcr_ref_first(updtr); ref;
					ref = updtr_prdcr_ref_next(ref))
				cancel_prdcr_updates(updtr, ref->prdcr, match);
		}
	} else {
		ldmsd_prdcr_ref_t ref;
		for (ref = updtr_prdcr_ref_first(updtr); ref;
				ref = updtr_prdcr_ref_next(ref))
			cancel_prdcr_updates(updtr, ref->prdcr, NULL);
	}
}

int prdcr_start_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t ev)
{
	return 0;
}

int prdcr_stop_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t ev)
{
	return 0;
}

static void __update_prdcr_set(ldmsd_updtr_t updtr, ldmsd_prdcr_set_t prd_set)
{
	struct timespec to;
	int rc;

	if (!prd_set->set) {
		ldmsd_log(LDMSD_LINFO, "%s: metric set for producer set %s is NULL\n",
			  __func__, prd_set->inst_name);
		return;
	}

	ldmsd_prdcr_set_ref_get(prd_set, "xprt_update");
	rc = ldms_xprt_update(prd_set->set, updtr_update_cb, prd_set);
	if (rc) {
		ldmsd_prdcr_set_ref_put(prd_set, "xprt_update");
		ldmsd_log(LDMSD_LINFO, "%s: ldms_xprt_update returned %d for %s\n",
			  __func__, rc, prd_set->inst_name);
		return;
	}

	resched_prd_set(updtr, prd_set, &to);
	ldmsd_prdcr_set_ref_get(prd_set, "update_ev"); /* Dropped when event is processed */
	ev_post(updtr->worker, updtr->worker, prd_set->update_ev, &to);
	ldmsd_updtr_unlock(updtr);
}

int prdcr_set_state_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t ev)
{
	ldmsd_name_match_t match;
	ldmsd_updtr_t updtr;
	ldmsd_prdcr_set_t prd_set = EV_DATA(ev, struct state_data)->prd_set;
	int state = EV_DATA(ev, struct state_data)->start_n_stop;
	EV_DATA(prd_set->update_ev, struct update_data)->reschedule = state;

	if (!state)
		goto out;

	ldmsd_cfg_lock(LDMSD_CFGOBJ_UPDTR);
	for (updtr = ldmsd_updtr_first(); updtr; updtr = ldmsd_updtr_next(updtr)) {
		if (!LIST_EMPTY(&updtr->match_list)) {
			LIST_FOREACH(match, &updtr->match_list, entry) {
				schedule_set_updates(updtr, prd_set, match);
			}
		} else {
			schedule_set_updates(updtr, prd_set, NULL);
		}
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_UPDTR);
 out:
	ldmsd_prdcr_set_ref_put(prd_set, "state_ev");
	return 0;
}

int prdcr_set_update_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t ev)
{
	ldmsd_updtr_t updtr = EV_DATA(ev, struct update_data)->updtr;
	ldmsd_prdcr_set_t prd_set = EV_DATA(ev, struct update_data)->prd_set;
	struct timespec to;
	int rc;

	ldmsd_updtr_lock(updtr);
	switch (updtr->state) {
	case LDMSD_UPDTR_STATE_STOPPED:
		break;
	case LDMSD_UPDTR_STATE_RUNNING:
		__update_prdcr_set(updtr, prd_set);
		break;
	}
	ldmsd_updtr_unlock(updtr);
	ldmsd_prdcr_set_ref_put(prd_set, "update_ev");
}

int prdcr_ref_cmp(void *a, const void *b)
{
	return strcmp(a, b);
}

ldmsd_updtr_t
ldmsd_updtr_new_with_auth(const char *name, char *interval_str, char *offset_str,
					int push_flags, int is_auto_task,
					uid_t uid, gid_t gid, int perm)
{
	struct ldmsd_updtr *updtr;
	long interval_us = 2000000, offset_us = LDMSD_UPDT_HINT_OFFSET_NONE;
	ev_worker_t worker;
	ev_t start_ev, stop_ev;
	char worker_name[PATH_MAX];

	snprintf(worker_name, PATH_MAX, "updtr:%s", name);
	worker = ev_worker_new(worker_name, prdcr_set_update_actor);
	if (!worker) {
		ldmsd_log(LDMSD_LERROR,
			  "%s: error %d creating new worker %s\n",
			  __func__, errno, worker_name);
		return NULL;
	}

	start_ev = ev_new(updtr_start_type);
	if (!start_ev) {
		ldmsd_log(LDMSD_LERROR,
			  "%s: error %d creating %s event\n",
			  __func__, errno, ev_type_name(updtr_start_type));
		return NULL;
	}

	stop_ev = ev_new(updtr_stop_type);
	if (!stop_ev) {
		ldmsd_log(LDMSD_LERROR,
			  "%s: error %d creating %s event\n",
			  __func__, errno, ev_type_name(updtr_stop_type));
		return NULL;
	}

	updtr = (struct ldmsd_updtr *)
		ldmsd_cfgobj_new_with_auth(name, LDMSD_CFGOBJ_UPDTR,
				 sizeof *updtr, ldmsd_updtr___del,
				 uid, gid, perm);
	if (!updtr)
		return NULL;

	updtr->state = LDMSD_UPDTR_STATE_STOPPED;
	updtr->is_auto_task = is_auto_task;
	if (interval_str) {
		interval_us = strtol(interval_str, NULL, 0);
		if (offset_str) {
			/* Make it a hint offset */
			offset_us = strtol(offset_str, NULL, 0);
		} else {
			offset_us = LDMSD_UPDT_HINT_OFFSET_NONE;
		}
	}
	updtr->sched.intrvl_us = interval_us;
	updtr->sched.offset_us = offset_us;
	updtr->sched.offset_skew = updtr_sched_offset_skew_get();

	updtr->worker = worker;
	updtr->start_ev = start_ev;
	updtr->stop_ev = stop_ev;
	EV_DATA(updtr->start_ev, struct start_data)->entity = updtr;
	EV_DATA(updtr->stop_ev, struct start_data)->entity = updtr;

	rbt_init(&updtr->prdcr_tree, prdcr_ref_cmp);
	LIST_INIT(&updtr->match_list);
	updtr->push_flags = push_flags;


	ldmsd_cfgobj_unlock(&updtr->obj);
	return updtr;
}

ldmsd_updtr_t
ldmsd_updtr_new(const char *name, char *interval_str,
		char *offset_str, int push_flags,
		int is_auto_interval)
{
	struct ldmsd_sec_ctxt sctxt;
	ldmsd_sec_ctxt_get(&sctxt);
	return ldmsd_updtr_new_with_auth(name,
				interval_str, offset_str,
				push_flags, is_auto_interval,
				sctxt.crd.uid, sctxt.crd.gid, 0777);
}

extern struct rbt *cfgobj_trees[];
extern pthread_mutex_t *cfgobj_locks[];
ldmsd_cfgobj_t __cfgobj_find(const char *name, ldmsd_cfgobj_type_t type);

int ldmsd_updtr_del(const char *updtr_name, ldmsd_sec_ctxt_t ctxt)
{
	int rc = 0;
	ldmsd_updtr_t updtr;

	pthread_mutex_lock(cfgobj_locks[LDMSD_CFGOBJ_UPDTR]);
	updtr = (ldmsd_updtr_t)__cfgobj_find(updtr_name, LDMSD_CFGOBJ_UPDTR);
	if (!updtr) {
		rc = ENOENT;
		goto out_0;
	}
	ldmsd_updtr_lock(updtr);
	rc = ldmsd_cfgobj_access_check(&updtr->obj, 0222, ctxt);
	if (rc)
		goto out_1;
	if (updtr->state != LDMSD_UPDTR_STATE_STOPPED) {
		rc = EBUSY;
		goto out_1;
	}

	rbt_del(cfgobj_trees[LDMSD_CFGOBJ_UPDTR], &updtr->obj.rbn);
	ldmsd_updtr_put(updtr); /* tree reference */
	rc = 0;
	/* let-through */
out_1:
	ldmsd_updtr_unlock(updtr);
out_0:
	pthread_mutex_unlock(cfgobj_locks[LDMSD_CFGOBJ_UPDTR]);
	if (updtr)
		ldmsd_updtr_put(updtr); /* `find` reference */
	return rc;
}

int __ldmsd_updtr_start(ldmsd_updtr_t updtr, ldmsd_sec_ctxt_t ctxt)
{
	int rc = 0;
	ldmsd_name_match_t match;

	ldmsd_updtr_lock(updtr);
	rc = ldmsd_cfgobj_access_check(&updtr->obj, 0222, ctxt);
	if (rc)
		goto out;
	if (updtr->state != LDMSD_UPDTR_STATE_STOPPED) {
		rc = EBUSY;
		goto out;
	}
	updtr->state = LDMSD_UPDTR_STATE_RUNNING;
	updtr->obj.perm |= LDMSD_PERM_DSTART;

	/*
	 * Tell the producer an updater is starting. It will tell the updater
	 * the sets that can be updated
	 */
	ev_post(updater, producer, updtr->start_ev, NULL);
 out:
	ldmsd_updtr_unlock(updtr);
	return rc;
}

int ldmsd_updtr_start(const char *updtr_name, const char *interval_str,
		      const char *offset_str, const char *auto_interval,
		      ldmsd_sec_ctxt_t ctxt)
{
	int rc = 0;
	long interval_us, offset_us;
	ldmsd_updtr_t updtr = ldmsd_updtr_find(updtr_name);
	if (!updtr)
		return ENOENT;

	ldmsd_updtr_lock(updtr);

	rc = ldmsd_cfgobj_access_check(&updtr->obj, 0222, ctxt);
	if (rc)
		goto err;
	if (updtr->state != LDMSD_UPDTR_STATE_STOPPED) {
		rc = EBUSY;
		goto err;
	}

	if (auto_interval) {
		if(0 == strcasecmp(auto_interval, "false"))
			updtr->is_auto_task = 0;
		else
			updtr->is_auto_task = 1;
	}
	interval_us = updtr->sched.intrvl_us;
	offset_us = updtr->sched.offset_us;
	if (interval_str) {
		/* A new interval is given. */
		interval_us = strtol(interval_str, NULL, 0);
		if (!offset_str) {
			/* An offset isn't given. We assume that
			 * users want the updater to schedule asynchronously.
			 */
			offset_us = LDMSD_UPDT_HINT_OFFSET_NONE;
		}
	}
	if (offset_str)
		offset_us = strtol(offset_str, NULL, 0);

	updtr->sched.intrvl_us = interval_us;
	updtr->sched.offset_us = offset_us;
	updtr->sched.offset_skew = updtr_sched_offset_skew_get();

	ldmsd_updtr_unlock(updtr);
	rc = __ldmsd_updtr_start(updtr, ctxt);
	ldmsd_updtr_put(updtr);
	return rc;

err:
	ldmsd_updtr_unlock(updtr);
	ldmsd_updtr_put(updtr);
	return rc;
}

/* Caller must hold the updater lock. */
static void __updtr_tasks_stop(ldmsd_updtr_t updtr)
{
	ev_flush(updtr->worker);
}

int __ldmsd_updtr_stop(ldmsd_updtr_t updtr, ldmsd_sec_ctxt_t ctxt)
{
	int rc = 0;
	ldmsd_updtr_lock(updtr);
	rc = ldmsd_cfgobj_access_check(&updtr->obj, 0222, ctxt);
	if (rc)
		goto out_1;
	if (updtr->state == LDMSD_UPDTR_STATE_STOPPED) {
		/* already stopped, return 0 */
		goto out_1;
	}
	if (updtr->state != LDMSD_UPDTR_STATE_RUNNING) {
		rc = EBUSY;
		goto out_1;

	}
	updtr->state = LDMSD_UPDTR_STATE_STOPPING;
	updtr->obj.perm &= ~LDMSD_PERM_DSTART;
	if (updtr->push_flags)
		cancel_push(updtr);
	ldmsd_updtr_unlock(updtr);

	/* joining tasks, need to unlock as task cb also took updtr lock */
	__updtr_tasks_stop(updtr);

	ldmsd_updtr_lock(updtr);
	/* tasks stopped */
	updtr->state = LDMSD_UPDTR_STATE_STOPPED;
	/* let-through */

	ev_post(updater, producer, updtr->stop_ev, NULL);
out_1:
	ldmsd_updtr_unlock(updtr);
	return rc;
}

int ldmsd_updtr_stop(const char *updtr_name, ldmsd_sec_ctxt_t ctxt)
{
	int rc = 0;
	ldmsd_updtr_t updtr = ldmsd_updtr_find(updtr_name);
	if (!updtr)
		return ENOENT;
	rc = __ldmsd_updtr_stop(updtr, ctxt);
	ldmsd_updtr_put(updtr);
	return rc;
}

ldmsd_updtr_t ldmsd_updtr_first()
{
	return (ldmsd_updtr_t)ldmsd_cfgobj_first(LDMSD_CFGOBJ_UPDTR);
}

ldmsd_updtr_t ldmsd_updtr_next(struct ldmsd_updtr *updtr)
{
	return (ldmsd_updtr_t)ldmsd_cfgobj_next(&updtr->obj);
}

ldmsd_name_match_t ldmsd_updtr_match_first(ldmsd_updtr_t updtr)
{
	return LIST_FIRST(&updtr->match_list);
}

ldmsd_name_match_t ldmsd_updtr_match_next(ldmsd_name_match_t cmp)
{
	return LIST_NEXT(cmp, entry);
}

ldmsd_prdcr_ref_t ldmsd_updtr_prdcr_first(ldmsd_updtr_t updtr)
{
	struct rbn *rbn = rbt_min(&updtr->prdcr_tree);
	if (!rbn)
		return NULL;
	return container_of(rbn, struct ldmsd_prdcr_ref, rbn);
}

ldmsd_prdcr_ref_t ldmsd_updtr_prdcr_next(ldmsd_prdcr_ref_t ref)
{
	struct rbn *rbn;
	rbn = rbn_succ(&ref->rbn);
	if (!rbn)
		return NULL;
	return container_of(rbn, struct ldmsd_prdcr_ref, rbn);
}

/*
 * Caller must hold the updater lock.
 */
ldmsd_prdcr_ref_t ldmsd_updtr_prdcr_find(ldmsd_updtr_t updtr,
					const char *prdcr_name)
{
	ldmsd_prdcr_ref_t ref;
	struct rbn *rbn = rbt_find(&updtr->prdcr_tree, prdcr_name);
	if (!rbn)
		return NULL;
	return container_of(rbn, struct ldmsd_prdcr_ref, rbn);
}

ldmsd_name_match_t updtr_find_match_ex(ldmsd_updtr_t updtr,
				       enum ldmsd_name_match_sel sel,
				       const char *ex)
{
	ldmsd_name_match_t match;
	LIST_FOREACH(match, &updtr->match_list, entry) {
		if (match->selector != sel)
			continue;
		if (0 == strcmp(match->regex_str, ex))
			return match;
	}
	return NULL;
}

int ldmsd_updtr_match_add(const char *updtr_name, const char *regex_str,
		const char *selector_str, char *rep_buf, size_t rep_len,
		ldmsd_sec_ctxt_t ctxt)
{
	int rc = 0;
	ldmsd_updtr_t updtr = ldmsd_updtr_find(updtr_name);
	if (!updtr)
		return ENOENT;

	ldmsd_updtr_lock(updtr);
	rc = ldmsd_cfgobj_access_check(&updtr->obj, 0222, ctxt);
	if (rc)
		goto out_1;
	if (updtr->state != LDMSD_UPDTR_STATE_STOPPED) {
		rc = EBUSY;
		goto out_1;
	}
	ldmsd_name_match_t match = calloc(1, sizeof *match);
	if (!match) {
		rc = ENOMEM;
		goto out_1;
	}
	match->regex_str = strdup(regex_str);
	if (!match->regex_str) {
		rc = ENOMEM;
		goto out_2;
	}

	if (!selector_str)
		match->selector = LDMSD_NAME_MATCH_INST_NAME;
	else if (0 == strcasecmp(selector_str, "schema"))
		match->selector = LDMSD_NAME_MATCH_SCHEMA_NAME;
	else if (0 == strcasecmp(selector_str, "inst"))
		match->selector = LDMSD_NAME_MATCH_INST_NAME;
	else {
		rc = EINVAL;
		goto out_3;
	}

	if (ldmsd_compile_regex(&match->regex, regex_str, rep_buf, rep_len))
		goto out_3;

	LIST_INSERT_HEAD(&updtr->match_list, match, entry);
	goto out_1;

out_3:
	free(match->regex_str);
out_2:
	free(match);
out_1:
	ldmsd_updtr_unlock(updtr);
	ldmsd_updtr_put(updtr);
out_0:
	return rc;
}

int ldmsd_updtr_match_del(const char *updtr_name, const char *regex_str,
			  const char *selector_str, ldmsd_sec_ctxt_t ctxt)
{
	int rc = 0;
	enum ldmsd_name_match_sel sel;
	if (!selector_str)
		sel = LDMSD_NAME_MATCH_INST_NAME;
	else if (0 == strcasecmp(selector_str, "inst"))
		sel = LDMSD_NAME_MATCH_INST_NAME;
	else if (0 == strcasecmp(selector_str, "schema"))
		sel = LDMSD_NAME_MATCH_SCHEMA_NAME;
	else {
		return EINVAL;
	}

	ldmsd_updtr_t updtr = ldmsd_updtr_find(updtr_name);
	if (!updtr)
		return ENOENT;

	ldmsd_updtr_lock(updtr);
	rc = ldmsd_cfgobj_access_check(&updtr->obj, 0222, ctxt);
	if (rc)
		goto out_1;
	if (updtr->state != LDMSD_UPDTR_STATE_STOPPED) {
		rc = EBUSY;
		goto out_1;
	}
	ldmsd_name_match_t match = updtr_find_match_ex(updtr, sel, regex_str);
	if (!match) {
		rc = -ENOENT;
		goto out_1;
	}
	LIST_REMOVE(match, entry);
	regfree(&match->regex);
	free(match->regex_str);
	free(match);
out_1:
	ldmsd_updtr_unlock(updtr);
	ldmsd_updtr_put(updtr);
out_0:
	return rc;
}

ldmsd_prdcr_ref_t prdcr_ref_new(ldmsd_prdcr_t prdcr)
{
	ldmsd_prdcr_ref_t ref = calloc(1, sizeof *ref);
	if (ref)
		ref->prdcr = ldmsd_prdcr_get(prdcr);
	rbn_init(&ref->rbn, prdcr->obj.name);
	return ref;
}

ldmsd_prdcr_ref_t prdcr_ref_find(ldmsd_updtr_t updtr, const char *name)
{
	struct rbn *rbn;
	rbn = rbt_find(&updtr->prdcr_tree, name);
	if (!rbn)
		return NULL;
	return container_of(rbn, struct ldmsd_prdcr_ref, rbn);
}



ldmsd_prdcr_ref_t prdcr_ref_find_regex(ldmsd_updtr_t updtr, regex_t *regex)
{
	struct rbn *rbn;
	ldmsd_prdcr_ref_t ref;
	rbn = rbt_min(&updtr->prdcr_tree);
	if (!rbn)
		return NULL;
	while (rbn) {
		ref = container_of(rbn, struct ldmsd_prdcr_ref, rbn);
		if (0 == regexec(regex, ref->prdcr->obj.name, 0, NULL, 0))
			return container_of(rbn, struct ldmsd_prdcr_ref, rbn);
		rbn = rbn_succ(rbn);
	}
	return NULL;
}

int __ldmsd_updtr_prdcr_add(ldmsd_updtr_t updtr, ldmsd_prdcr_t prdcr)
{
	int rc = 0;
	ldmsd_prdcr_ref_t ref;

	ldmsd_updtr_lock(updtr);
	if (updtr->state != LDMSD_UPDTR_STATE_STOPPED) {
		rc = EBUSY;
		goto out;
	}
	ref = prdcr_ref_find(updtr, prdcr->obj.name);
	if (ref) {
		rc = EEXIST;
		goto out;
	}
	ref = prdcr_ref_new(prdcr);
	if (!ref) {
		rc = errno;
		goto out;
	}
	rbt_ins(&updtr->prdcr_tree, &ref->rbn);
out:
	ldmsd_updtr_unlock(updtr);
	return rc;
}

int ldmsd_updtr_prdcr_add(const char *updtr_name, const char *prdcr_regex,
			  char *rep_buf, size_t rep_len, ldmsd_sec_ctxt_t ctxt)
{
	regex_t regex;
	ldmsd_updtr_t updtr;
	ldmsd_prdcr_t prdcr;
	int rc;

	rc = ldmsd_compile_regex(&regex, prdcr_regex, rep_buf, rep_len);
	if (rc)
		return rc;

	updtr = ldmsd_updtr_find(updtr_name);
	if (!updtr) {
		sprintf(rep_buf, "%dThe updater specified does not "
						"exist\n", ENOENT);
		regfree(&regex);
		return ENOENT;
	}

	ldmsd_updtr_lock(updtr);
	rc = ldmsd_cfgobj_access_check(&updtr->obj, 0222, ctxt);
	if (rc)
		goto out_1;
	if (updtr->state != LDMSD_UPDTR_STATE_STOPPED) {
		sprintf(rep_buf, "%dConfiguration changes cannot be made "
				"while the updater is running\n", EBUSY);
		rc = EBUSY;
		goto out_1;
	}
	ldmsd_cfg_lock(LDMSD_CFGOBJ_PRDCR);
	for (prdcr = ldmsd_prdcr_first(); prdcr; prdcr = ldmsd_prdcr_next(prdcr)) {
		if (regexec(&regex, prdcr->obj.name, 0, NULL, 0))
			continue;
		/* See if this match is already in the list */
		ldmsd_prdcr_ref_t ref = prdcr_ref_find(updtr, prdcr->obj.name);
		if (ref)
			continue;
		ref = prdcr_ref_new(prdcr);
		if (!ref) {
			rc = ENOMEM;
			sprintf(rep_buf, "%dMemory allocation failure.\n", ENOMEM);
			ldmsd_prdcr_put(prdcr);
			ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR);
			goto out_1;
		}
		rbt_ins(&updtr->prdcr_tree, &ref->rbn);
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR);
	sprintf(rep_buf, "0\n");
out_1:
	regfree(&regex);
	ldmsd_updtr_unlock(updtr);
	ldmsd_updtr_put(updtr);
	return rc;
}

int ldmsd_updtr_prdcr_del(const char *updtr_name, const char *prdcr_regex,
			  char *rep_buf, size_t rep_len, ldmsd_sec_ctxt_t ctxt)
{
	int rc = 0;
	regex_t regex;
	ldmsd_prdcr_ref_t ref;

	rc = ldmsd_compile_regex(&regex, prdcr_regex, rep_buf, rep_len);
	if (rc)
		goto out_0;

	ldmsd_updtr_t updtr = ldmsd_updtr_find(updtr_name);
	if (!updtr) {
		rc = ENOENT;
		regfree(&regex);
		goto out_0;
	}
	ldmsd_updtr_lock(updtr);
	rc = ldmsd_cfgobj_access_check(&updtr->obj, 0222, ctxt);
	if (rc)
		goto out_1;
	if (updtr->state != LDMSD_UPDTR_STATE_STOPPED) {
		rc = EBUSY;
		goto out_1;
	}
	for (ref = prdcr_ref_find_regex(updtr, &regex);
	     ref; ref = prdcr_ref_find_regex(updtr, &regex)) {
		rbt_del(&updtr->prdcr_tree, &ref->rbn);
		ldmsd_prdcr_put(ref->prdcr);
		free(ref);
	}
out_1:
	regfree(&regex);
	ldmsd_updtr_unlock(updtr);
	ldmsd_updtr_put(updtr);
out_0:
	return rc;
}
