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
#include "ldmsd_request.h"
#include "ldms_xprt.h"
#include "config.h"

static void __updtr_prdcr_match_list_free(struct ldmsd_regex_list *list)
{
	ldmsd_regex_ent_t match;
	while (!LIST_EMPTY(list)) {
		match = LIST_FIRST(list);
		if (match->regex_str)
			free(match->regex_str);
		regfree(&match->regex);
		LIST_REMOVE(match, entry);
		free(match);
	}
	free(list);
}

static void __updtr_match_list_free(struct updtr_match_list *list)
{
	ldmsd_name_match_t match;
	while (!LIST_EMPTY(list) ) {
		match = LIST_FIRST(list);
		if (match->regex_str)
			free(match->regex_str);
		regfree(&match->regex);
		LIST_REMOVE(match, entry);
		free(match);
	}
	free(list);
}

static void __updtr_prdcr_tree_free(ldmsd_updtr_t updtr)
{
	struct rbn *rbn;
	ldmsd_prdcr_ref_t prdcr_ref;
	while (!rbt_empty(&updtr->prdcr_tree)) {
		rbn = rbt_min(&updtr->prdcr_tree);
		rbt_del(&updtr->prdcr_tree, rbn);
		prdcr_ref = container_of(rbn, struct ldmsd_prdcr_ref, rbn);
		ldmsd_cfgobj_put(&prdcr_ref->prdcr->obj);
		free(prdcr_ref);
	}
}

void ldmsd_updtr___del(ldmsd_cfgobj_t obj)
{
	ldmsd_updtr_t updtr = (ldmsd_updtr_t)obj;
	__updtr_match_list_free(updtr->match_list);
	__updtr_prdcr_match_list_free(updtr->prdcr_regex_list);
	__updtr_prdcr_tree_free(updtr);
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

static int __on_set_updated(ldmsd_prdcr_set_t prd_set, int status)
{
	/* NOTE: must be called with prd_set->lock held */
	uint64_t gn;
	int errcode = LDMS_UPD_ERROR(status);
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
		prd_set->state = LDMSD_PRDCR_SET_STATE_ERROR;
		goto out;
	}

	if (!ldms_set_is_consistent(prd_set->set)) {
		ldmsd_log(LDMSD_LINFO, "Set %s is inconsistent.\n", prd_set->inst_name);
		goto set_ready;
	}

	gn = ldms_set_data_gn_get(prd_set->set);
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
		/* No more data pending move prdcr_set state UPDATING --> READY */
		prd_set->state = LDMSD_PRDCR_SET_STATE_READY;
out:
	return errcode;
}

static void updtr_update_cb(ldms_t t, ldms_set_t set, int status, void *arg)
{
	ldmsd_prdcr_set_t prd_set = arg;
	int errcode;

	pthread_mutex_lock(&prd_set->lock);
#ifdef LDMSD_UPDATE_TIME
	gettimeofday(&prd_set->updt_end, NULL);
	prd_set->updt_duration = ldmsd_timeval_diff(&prd_set->updt_start,
							&prd_set->updt_end);
	__updt_time_put(prd_set->updt_time);
#endif /* LDMSD_UPDATE_TIME */
	errcode = __on_set_updated(prd_set, status);
	pthread_mutex_unlock(&prd_set->lock);
	if (0 == errcode) {
		ldmsd_log(LDMSD_LDEBUG, "Pushing set %p %s\n",
			  prd_set->set, prd_set->inst_name);
		int rc = ldms_xprt_push(prd_set->set);
		if (rc) {
			ldmsd_log(LDMSD_LERROR, "Failed to push set %s\n",
						prd_set->inst_name);
		}
	}
	if (0 == (status & (LDMS_UPD_F_PUSH|LDMS_UPD_F_MORE))) {
		/* Put reference taken before calling ldms_xprt_update. */
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
	gettimeofday(&prd_set->updt_start, NULL);
#ifdef LDMSD_UPDATE_TIME
	__updt_time_get(prd_set->updt_time);
	if (prd_set->updt_time->update_start.tv_sec == 0)
		prd_set->updt_time->update_start = prd_set->updt_start;
#endif
	if (!updtr->push_flags) {
		op_s = "Updating";

		EV_DATA(prd_set->update_ev, struct update_data)->updtr = updtr;
		EV_DATA(prd_set->update_ev, struct update_data)->prd_set = prd_set;
		EV_DATA(prd_set->update_ev, struct update_data)->reschedule = 1;
		struct timespec to;
		resched_prd_set(updtr, prd_set, &to);
		ldmsd_prdcr_set_ref_get(prd_set, "update_ev");
		if (ev_post(updtr->worker, updtr->worker, prd_set->update_ev, &to))
			ldmsd_prdcr_set_ref_put(prd_set, "update_ev");

	} else if (0 == (prd_set->push_flags & LDMSD_PRDCR_SET_F_PUSH_REG)) {
		op_s = "Registering push for";

		/* this doesn't work because it's taken after lookup
		 * which may fail or the updater is never started at
		 * all. since the flags don't tell yuou one way or the
		 * other this is just broken */
		/* prd_set->push_flags |= LDMSD_PRDCR_SET_F_PUSH_REG; */
		/* ldmsd_prdcr_set_ref_get(prd_set, "push"); */

		if (updtr->push_flags & LDMSD_UPDTR_F_PUSH_CHANGE)
			push_flags = LDMS_XPRT_PUSH_F_CHANGE;
		rc = ldms_xprt_register_push(prd_set->set, push_flags,
					     updtr_update_cb, prd_set);
		if (rc) {
			/* This message does not repeat */
			ldmsd_log(LDMSD_LERROR, "Register push error %d Set %s\n",
						rc, prd_set->inst_name);
		} else {
			/* Only set the flag if we succeed */
			prd_set->push_flags |= LDMSD_PRDCR_SET_F_PUSH_REG;
		}
	}
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
	/* ldmsd_prdcr_set_ref_put(prd_set, "push"); */
	return rc;
}

static void __handle_prdcrset_lookup(ldmsd_prdcr_set_t prd_set, int status,
				     ldms_set_t set)
{
	/* NOTE: muste be called with prd_set locked */
	if (status != LDMS_LOOKUP_OK) {
		status = ((int)status < 0 ? -status : status);
		switch ((int)status) {
		case ENOMEM:
			ldmsd_log(LDMSD_LERROR,
				"Out of memory error (%d) in lookup callback "
				"for set '%s'. "
				"Consider changing the -m parameter on the "
				"command line to a bigger value. "
				"The current value is %s\n",
				status, prd_set->inst_name,
				ldmsd_get_max_mem_sz_str());
			break;
		case EEXIST:
			ldmsd_log(LDMSD_LERROR,
				  "Lookup error, set '%s' existed.\n",
				  prd_set->inst_name
				 );
			break;
		default:
			ldmsd_log(LDMSD_LERROR,
				  "Error %d in lookup callback for set '%s'\n",
					  status, prd_set->inst_name);
		}
		prd_set->state = LDMSD_PRDCR_SET_STATE_START;
		return;
	}
	if (!prd_set->set) {
		/* This is the first lookup of the set. */
		prd_set->set = set;
		ldms_ctxt_set(set, &prd_set->set_ctxt);
	}

	prd_set->state = LDMSD_PRDCR_SET_STATE_READY;

	ldmsd_log(LDMSD_LINFO, "Set %s is ready\n", prd_set->inst_name);
	ldmsd_strgp_prdset_update(prd_set);
}

static void prdcrset_lookup_cb(ldms_t xprt, enum ldms_lookup_status status,
			    int more, ldms_set_t set, void *arg)
{
	ldmsd_prdcr_set_t prd_set = arg;
	pthread_mutex_lock(&prd_set->lock);
	__handle_prdcrset_lookup(prd_set, status, set);
	pthread_mutex_unlock(&prd_set->lock);
	ldmsd_prdcr_set_ref_put(prd_set, "xprt_lookup");
	return;
}

static void grp_lookup_cb(ldms_t xprt, enum ldms_lookup_status status,
			  int more, ldms_set_t set, void *arg)
{
	/* NOTE LDMS lookup on the group results in multiple look up replies
	 *      (callbacks): one callback for each member (with more flag, like
	 *      LOOKUP_RE), and the group itself come in last. */
	ldmsd_prdcr_set_t grp_set = arg;
	ldmsd_prdcr_t prdcr = grp_set->prdcr;
	ldmsd_prdcr_set_t prd_set;

	if (status) {
		prdcrset_lookup_cb(xprt, status, more, set, grp_set);
		return;
	}

	ldmsd_prdcr_lock(prdcr);
	prd_set = ldmsd_prdcr_set_find(prdcr, ldms_set_name_get(set));
	if (prd_set) {
		__handle_prdcrset_lookup(prd_set, status, set);
	} else {
		ldmsd_log(LDMSD_LWARNING,
			  "'%s' group member lookup succeeded but "
			  "no prdcr set.", ldms_set_name_get(set));
	}
	ldmsd_prdcr_unlock(prdcr);
}

static void cancel_prdcr_updates(ldmsd_updtr_t updtr,
				 ldmsd_prdcr_t prdcr, ldmsd_name_match_t match)
{
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

	if (!LIST_EMPTY(updtr->match_list)) {
		LIST_FOREACH(match, updtr->match_list, entry) {
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

static void __grp_entupd(ldms_grp_t grp, ldms_grp_ev_t ev)
{
	ldmsd_prdcr_set_t prd_set;
	struct ldmsd_set_ctxt_s *set_ctxt;
	int errcode;
	set_ctxt = ldms_ctxt_get(ev->entupd.set);
	prd_set = container_of(set_ctxt, struct ldmsd_prdcr_set, set_ctxt);
	assert(prd_set);
	if (!prd_set) {
		ldmsd_log(LDMSD_LWARNING, "%s:%d %s ldms ctxt is NULL",
				__func__, __LINE__,
				ldms_set_name_get(ev->entupd.set));
		return;
	}
	pthread_mutex_lock(&prd_set->lock);
	errcode = __on_set_updated(prd_set, ev->entupd.flags);
	pthread_mutex_unlock(&prd_set->lock);
	if (0 == errcode) {
		ldmsd_log(LDMSD_LDEBUG, "Pushing set %p %s\n",
			  prd_set->set, prd_set->inst_name);
		int rc = ldms_xprt_push(prd_set->set);
		if (rc) {
			ldmsd_log(LDMSD_LERROR, "Failed to push set %s\n",
						prd_set->inst_name);
		}
	}
}

static int __prdcr_set_lookup(ldmsd_prdcr_set_t prd_set);

static void __grp_noent(ldms_grp_t grp, ldms_grp_ev_t ev)
{
	ldmsd_prdcr_set_t gpset = ev->ctxt;
	ldmsd_prdcr_t prdcr = gpset->prdcr;
	ldmsd_prdcr_set_t prd_set;
	ldmsd_prdcr_lock(prdcr);
	prd_set = ldmsd_prdcr_set_find(prdcr, ev->noent.name);
	if (prd_set)
		__prdcr_set_lookup(prd_set);
	ldmsd_prdcr_unlock(prdcr);
}

static void __grp_cb(ldms_grp_t grp, ldms_grp_ev_t ev)
{
	ldmsd_prdcr_set_t gpset = ev->ctxt;
	switch (ev->type) {
	case LDMS_GRP_EV_NOENT:
		__grp_noent(grp, ev);
		break;
	case LDMS_GRP_EV_UPDATE_BEGIN:
		break;
	case LDMS_GRP_EV_UPDATED:
		__grp_entupd(grp, ev);
		break;
	case LDMS_GRP_EV_FINALIZE:
		pthread_mutex_lock(&gpset->lock);
		gpset->state = LDMSD_PRDCR_SET_STATE_READY;
		pthread_mutex_unlock(&gpset->lock);
		ldmsd_prdcr_set_ref_put(gpset, "xprt_update");
		break;
	}
}

static int __prdcr_set_lookup(ldmsd_prdcr_set_t prd_set)
{
	/* NOTE: must be called with prd_set->lock held */
	int rc;
	if (prd_set->state != LDMSD_PRDCR_SET_STATE_START) {
		ldmsd_log(LDMSD_LWARNING, "`%s` is being looked up "
				"while in state %s(%d)", prd_set->inst_name,
				ldmsd_prdcr_set_state_str(prd_set->state),
				prd_set->state);
		return EINVAL;
	}
	prd_set->state = LDMSD_PRDCR_SET_STATE_LOOKUP;
	ldms_lookup_cb_t cb;
	if (prd_set->dir_set_flags & LDMS_SET_F_GROUP)
		cb = grp_lookup_cb;
	else
		cb = prdcrset_lookup_cb;
	ldmsd_prdcr_set_ref_get(prd_set, "xprt_lookup");
	rc = ldms_xprt_lookup(prd_set->prdcr->xprt, prd_set->inst_name,
			      LDMS_LOOKUP_BY_INSTANCE,
			      cb, prd_set);
	if (rc) {
		ldmsd_log(LDMSD_LINFO,
				"Synchronous error %d "
				"from ldms_lookup\n", rc);
		ldmsd_prdcr_set_ref_put(prd_set, "xprt_lookup");
		prd_set->state = LDMSD_PRDCR_SET_STATE_START;
	}
	return rc;
}

static void __update_prdcr_set(ldmsd_updtr_t updtr, ldmsd_prdcr_set_t prd_set)
{
	struct timespec to;
	int rc;

	pthread_mutex_lock(&prd_set->lock);
	switch (prd_set->state) {
	case LDMSD_PRDCR_SET_STATE_START:
		rc = __prdcr_set_lookup(prd_set);
		if (rc)
			goto out;
		break;
	case LDMSD_PRDCR_SET_STATE_LOOKUP:
		/* outstanding lookup */
		ldmsd_log(LDMSD_LDEBUG,
			  "%s: there is an outstanding lookup %s\n",
			  __func__, prd_set->inst_name);
		break;
	case LDMSD_PRDCR_SET_STATE_READY:
		prd_set->state = LDMSD_PRDCR_SET_STATE_UPDATING;
		ldmsd_prdcr_set_ref_get(prd_set, "xprt_update");
		if (ldms_is_grp(prd_set->set))
			rc = ldms_grp_update((ldms_grp_t)prd_set->set, __grp_cb, prd_set);
		else
			rc = ldms_xprt_update(prd_set->set, updtr_update_cb, prd_set);
		if (rc) {
			ldmsd_prdcr_set_ref_put(prd_set, "xprt_update");
			ldmsd_log(LDMSD_LINFO,
				  "%s: ldms_xprt/grp_update returned %d for %s\n",
				  __func__, rc, prd_set->inst_name);
			prd_set->state = LDMSD_PRDCR_SET_STATE_READY;
			goto out;
		}
		break;
	case LDMSD_PRDCR_SET_STATE_UPDATING:
		ldmsd_log(LDMSD_LDEBUG,
			  "%s: there is an outstanding update %s\n",
			  __func__, prd_set->inst_name);
		break;
	case LDMSD_PRDCR_SET_STATE_ERROR:
		/* do not reschedule */
		goto out;
	}
	/* prd_set update reschedule */
	resched_prd_set(updtr, prd_set, &to);
	ldmsd_prdcr_set_ref_get(prd_set, "update_ev"); /* Dropped when event is processed */
	if (ev_post(updtr->worker, updtr->worker, prd_set->update_ev, &to))
		ldmsd_prdcr_set_ref_put(prd_set, "update_ev");
 out:
	pthread_mutex_unlock(&prd_set->lock);
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
		if (!LIST_EMPTY(updtr->match_list)) {
			LIST_FOREACH(match, updtr->match_list, entry) {
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

	ldmsd_updtr_lock(updtr);
	switch (updtr->state) {
	case LDMSD_UPDTR_STATE_STOPPED:
	case LDMSD_UPDTR_STATE_STOPPING:
		break;
	case LDMSD_UPDTR_STATE_RUNNING:
		__update_prdcr_set(updtr, prd_set);
		break;
	}
	ldmsd_updtr_unlock(updtr);
	ldmsd_prdcr_set_ref_put(prd_set, "update_ev");
	return 0;
}

int prdcr_ref_cmp(void *a, const void *b)
{
	return strcmp(a, b);
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

/* Caller must hold the updtr lock. */
int __ldmsd_updtr_start(ldmsd_updtr_t updtr, ldmsd_sec_ctxt_t ctxt)
{
	int rc = 0;

	rc = ldmsd_cfgobj_access_check(&updtr->obj, 0222, ctxt);
	if (rc)
		return rc;
	if (updtr->state != LDMSD_UPDTR_STATE_STOPPED)
		return EBUSY;
	updtr->state = LDMSD_UPDTR_STATE_RUNNING;
	updtr->obj.perm |= LDMSD_PERM_DSTART;

	/*
	 * Tell the producer an updater is starting. It will tell the updater
	 * the sets that can be updated
	 */
	ev_post(updater, producer, updtr->start_ev, NULL);
	return rc;
}

/* Caller must hold the updater lock. */
static void __updtr_tasks_stop(ldmsd_updtr_t updtr)
{
	ev_flush(updtr->worker);
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
	return LIST_FIRST(updtr->match_list);
}

ldmsd_name_match_t ldmsd_updtr_match_next(ldmsd_name_match_t cmp)
{
	return LIST_NEXT(cmp, entry);
}

enum ldmsd_name_match_sel ldmsd_updtr_match_str2enum(const char *str)
{
	if (0 == strcasecmp(str, "schema"))
		return LDMSD_NAME_MATCH_SCHEMA_NAME;
	else if (0 == strcasecmp(str, "inst"))
		return LDMSD_NAME_MATCH_INST_NAME;
	else
		return -1;
}

const char *ldmsd_updtr_match_enum2str(enum ldmsd_name_match_sel sel)
{
	if (sel == LDMSD_NAME_MATCH_INST_NAME)
		return "inst";
	else if (sel == LDMSD_NAME_MATCH_SCHEMA_NAME)
		return "schema";
	else
		return NULL;
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
	LIST_FOREACH(match, updtr->match_list, entry) {
		if (match->selector != sel)
			continue;
		if (0 == strcmp(match->regex_str, ex))
			return match;
	}
	return NULL;
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

static int ldmsd_updtr_enable(ldmsd_cfgobj_t obj)
{
	int rc = 0;
	ldmsd_updtr_t updtr = (ldmsd_updtr_t)obj;
	if (updtr->state != LDMSD_UPDTR_STATE_STOPPED)
		return EBUSY;
	updtr->state = LDMSD_UPDTR_STATE_RUNNING;
	updtr->obj.perm |= LDMSD_PERM_DSTART; /* TODO: remove this? */

	/*
	 * Tell the producer an updater is starting. It will tell the updater
	 * the sets that can be updated
	 */
	ev_post(updater, producer, updtr->start_ev, NULL);
	return rc;
}

static int ldmsd_updtr_disable(ldmsd_cfgobj_t obj)
{
	int rc = 0;
	ldmsd_updtr_t updtr = (ldmsd_updtr_t)obj;
	if (updtr->state == LDMSD_UPDTR_STATE_STOPPED) {
		/* already stopped, return 0 */
		goto out_1;
	}
	if (updtr->state != LDMSD_UPDTR_STATE_RUNNING) {
		rc = EBUSY;
		goto out_1;

	}
	updtr->state = LDMSD_UPDTR_STATE_STOPPING;
	updtr->obj.perm &= ~LDMSD_PERM_DSTART; /* TODO: remove this? */
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
	return rc;
}

static int __updtr_push_attr(json_entity_t err, json_entity_t push, int *_push_flags)
{
	int rc;

	if (!push) {
		*_push_flags = LDMSD_ATTR_NA;
		return 0;
	}

	if (JSON_BOOL_VALUE == json_entity_type(push)) {
		if (json_value_bool(push)) {
			*_push_flags = LDMSD_UPDTR_F_PUSH;
		}
	} else if (JSON_STRING_VALUE == json_entity_type(push)) {
		char *push_s = json_value_str(push)->str;
		if (0 == strcasecmp(push_s, "onchange")) {
			*_push_flags = LDMSD_UPDTR_F_PUSH | LDMSD_UPDTR_F_PUSH_CHANGE;
		} else if (0 == strcasecmp(push_s, "true") || 0 == strcasecmp(push_s, "yes")) {
			*_push_flags = LDMSD_UPDTR_F_PUSH;
		} else {
			ldmsd_req_buf_t buf = ldmsd_req_buf_alloc(512);
			if (!buf)
				goto oom;
			rc = ldmsd_req_buf_append(buf,
				       "Push '%s' is invalid.", push_s);
			if (rc < 0)
				goto oom;
			err = json_dict_build(err, JSON_STRING_VALUE,
							"push", buf->buf);
			ldmsd_req_buf_free(buf);
			if (!err)
				goto oom;
		}
	} else {
		err = json_dict_build(NULL, JSON_STRING_VALUE, "push",
				"'push' is neither a boolean or a string.");
		if (!err)
			goto oom;
	}
	return 0;
oom:
	return ENOMEM;
}

static int __updtr_prdcrs_attr(json_entity_t prdcrs, json_entity_t err,
				struct ldmsd_regex_list **_prdcr_list)
{
	int rc;
	json_entity_t p;
	ldmsd_req_buf_t buf = NULL;
	struct ldmsd_regex_list *filt_list = NULL;
	struct ldmsd_regex_ent *filt;

	if (!prdcrs)
		goto out;

	filt_list = malloc(sizeof(*filt_list));
	if (!filt_list)
		goto oom;

	buf = ldmsd_req_buf_alloc(1024);
	if (!buf)
		goto oom;

	for (p = json_item_first(prdcrs); p; p = json_item_next(p)) {
		if (JSON_STRING_VALUE != json_entity_type(p)) {
			err = json_dict_build(err, JSON_STRING_VALUE, "producers",
					"There is an element in 'producers' "
					"that is not a JSON string.", -1);
			if (!err)
				goto oom;
			rc = EINVAL;
			goto err;
		}
		filt = malloc(sizeof(*filt));
		if (!filt)
			goto oom;
		filt->regex_str = strdup(json_value_str(p)->str);
		if (!filt->regex_str) {
			free(filt);
			goto oom;
		}

		rc = ldmsd_compile_regex(&filt->regex, filt->regex_str,
							buf->buf, buf->len);
		if (rc) {
			err = json_dict_build(err, JSON_STRING_VALUE, "producers",
								buf->buf, -1);
			if (!err)
				goto oom;
			break;
		}
		LIST_INSERT_HEAD(filt_list, filt, entry);
	}
	ldmsd_req_buf_free(buf);
out:
	*_prdcr_list = filt_list;
	return 0;
oom:
	rc = ENOMEM;
err:
	if (buf)
		ldmsd_req_buf_free(buf);
	if (filt_list)
		__updtr_prdcr_match_list_free(filt_list);
	return rc;
}

static int __updtr_match_list_get(json_entity_t sets, json_entity_t schemas,
				json_entity_t err, struct updtr_match_list **_match_list)
{
	int rc;
	struct ldmsd_name_match *match;
	struct updtr_match_list *match_list = NULL;
	json_entity_t item, list;
	char *sel_str;
	ldmsd_req_buf_t buf;
	enum ldmsd_name_match_sel sel;

	buf = ldmsd_req_buf_alloc(512);
	if (!buf)
		return ENOMEM;

	if (!sets && !schemas)
		goto out;

	match_list = malloc(sizeof(*match_list));
	if (!match_list)
		goto oom;
	LIST_INIT(match_list);

	if (sets) {
		sel = LDMSD_NAME_MATCH_INST_NAME;
		sel_str = "set_instance_filters";
		list = sets;
	} else {
		sel = LDMSD_NAME_MATCH_SCHEMA_NAME;
		sel_str = "set_schema_filters";
		list = schemas;
	}
add_match:
	if (!list)
		goto out;

	for (item = json_item_first(list); item; item = json_item_next(item)) {
		if (JSON_STRING_VALUE != json_entity_type(item)) {
			rc = ldmsd_req_buf_append(buf, "This is an element in '%s' "
						"that is not a JSON string.", sel_str);
			if (rc < 0)
				goto oom;
			err = json_dict_build(err, JSON_STRING_VALUE, sel_str,
					buf->buf, -1);
			if (!err)
				goto oom;
			break;
		}
		match = malloc(sizeof(*match));
		if (!match)
			goto oom;
		match->regex_str = strdup(json_value_str(item)->str);
		if (!match->regex_str)
			goto oom;
		match->selector = sel;
		rc = ldmsd_compile_regex(&match->regex, match->regex_str,
							buf->buf, buf->len);
		if (rc) {
			err = json_dict_build(err, JSON_STRING_VALUE,
							sel_str, buf->buf, -1);
			if (!err)
				goto oom;
			rc = EINVAL;
			goto err;
		}
		LIST_INSERT_HEAD(match_list, match, entry);
	}

	/*
	 * Next iterate through the schema filter list
	 */
	if (sel == LDMSD_NAME_MATCH_INST_NAME) {
		sel = LDMSD_NAME_MATCH_SCHEMA_NAME;
		sel_str = "schemas";
		list = schemas;
		ldmsd_req_buf_reset(buf);
		goto add_match;
	}
out:

	*_match_list = match_list;

	ldmsd_req_buf_free(buf);
	return 0;
oom:
	rc = ENOMEM;
err:
	if (match_list)
		__updtr_match_list_free(match_list);
	ldmsd_req_buf_free(buf);
	return rc;
}

static json_entity_t __updtr_attr_get(json_entity_t dft, json_entity_t spc,
				long *_interval_us, long *_offset_us,
				int *_push_flags, int *_auto_task, int *_perm,
				struct ldmsd_regex_list **_prdcr_list,
				struct updtr_match_list **_match_list)
{
	int rc;
	ldmsd_req_buf_t buf = NULL;
	json_entity_t err = NULL;
	json_entity_t interval, offset, push, auto_task, perm, prdcrs, sets, schemas;
	interval = offset = push = auto_task = perm = prdcrs = sets = schemas = NULL;

	buf = ldmsd_req_buf_alloc(1024);
	if (!buf)
		goto oom;

	err = json_entity_new(JSON_DICT_VALUE);
	if (!err)
		goto oom;

	if (spc) {
		interval = json_value_find(spc, "interval");
		offset = json_value_find(spc, "offset");
		auto_task = json_value_find(spc, "auto_task");
		prdcrs = json_value_find(spc, "producer_filters");
		sets = json_value_find(spc, "set_instance_filters");
		schemas = json_value_find(spc, "set_schema_filters");
		perm = json_value_find(spc, "perm");
	}

	if (dft) {
		if (!interval)
			interval = json_value_find(dft, "interval");
		if (!offset)
			offset = json_value_find(dft, "offset");
		if (!auto_task)
			auto_task = json_value_find(dft, "auto_task");
		if (!prdcrs)
			prdcrs = json_value_find(dft, "producer_filters");
		if (!sets)
			sets = json_value_find(dft, "set_instance_filters");
		if (!schemas)
			schemas = json_value_find(dft, "set_schema_filters");
		if (!perm)
			perm = json_value_find(dft, "perm");
	}

	/* producers */
	rc = __updtr_prdcrs_attr(prdcrs, err, _prdcr_list);
	if (ENOMEM == rc)
		goto oom;

	/* match list */
	rc = __updtr_match_list_get(sets, schemas, err, _match_list);
	if (ENOMEM == rc)
		goto oom;

	/* update interval */
	*_interval_us = LDMSD_ATTR_NA;
	if (interval) {
		if (JSON_STRING_VALUE == json_entity_type(interval)) {
			*_interval_us = ldmsd_time_str2us(json_value_str(interval)->str);
		} else if (JSON_INT_VALUE == json_entity_type(interval)) {
			*_interval_us = json_value_int(interval);
		} else {
			*_interval_us = LDMSD_ATTR_INVALID;
			err = json_dict_build(err, JSON_STRING_VALUE, "interval",
					"'interval' is neither a string or an integer.", -1);
			if (!err)
				goto oom;
		}
	}

	/* update offset */
	*_offset_us = LDMSD_ATTR_NA;
	if (offset) {
		if (JSON_STRING_VALUE == json_entity_type(offset)) {
			char *offset_s = json_value_str(offset)->str;
			if (0 == strcasecmp("none", offset_s)) {
				*_offset_us = LDMSD_UPDT_HINT_OFFSET_NONE;
			} else {
				*_offset_us = ldmsd_time_str2us(offset_s);
			}
		} else if (JSON_INT_VALUE == json_entity_type(offset)) {
			*_offset_us = json_value_int(offset);
		} else {
			*_offset_us = LDMSD_ATTR_INVALID;
			err = json_dict_build(err, JSON_STRING_VALUE, "offset",
					"'offset' is neither a string or an integer.", -1);
			if (!err)
				goto oom;
		}
	}

	/* push flag */
	rc = __updtr_push_attr(err, push, _push_flags);
	if (rc)
		goto oom;

	/* auto task */
	*_auto_task = LDMSD_ATTR_NA;
	if (auto_task) {
		if (JSON_BOOL_VALUE != json_entity_type(auto_task)) {
			err = json_dict_build(err, JSON_STRING_VALUE, "auto_task",
						"'auto_task' is not a boolean.", -1);
			if (!err)
				goto oom;
			*_auto_task = LDMSD_ATTR_INVALID;
		} else {
			*_auto_task = json_value_bool(auto_task);
		}
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

	if (0 == json_attr_count(err)) {
		json_entity_free(err);
		err = NULL;
	}

	return err;
oom:
	errno = ENOMEM;
	if (err)
		json_entity_free(err);
	return NULL;
}

/*
 * Export only the configuration attributes
 */
static json_entity_t __updtr_export(ldmsd_updtr_t updtr)
{
	json_entity_t query, l, i, sets, schemas;
	ldmsd_name_match_t match;
	ldmsd_regex_ent_t prdcr_filter;

	query = ldmsd_cfgobj_query_result_new(&updtr->obj);
	if (!query)
		goto oom;

	query = json_dict_build(query,
			JSON_BOOL_VALUE, "auto_task", updtr->is_auto_task,
			JSON_INT_VALUE, "push", updtr->push_flags,
			JSON_INT_VALUE, "interval", updtr->sched.intrvl_us,
			JSON_INT_VALUE, "offset", updtr->sched.offset_us,
			-1);
	if (!query)
		goto oom;

	/* producers */
	query = json_dict_build(query, JSON_LIST_VALUE, "producer_filters", -2, -1);
	if (!query)
		goto oom;
	l = json_value_find(query, "producer_filters");
	LIST_FOREACH(prdcr_filter, updtr->prdcr_regex_list, entry) {
		i = json_entity_new(JSON_STRING_VALUE, prdcr_filter->regex_str);
		if (!i)
			goto oom;
		json_item_add(l, i);
	}

	/* sets and schemas */
	if (!updtr->match_list)
		goto out;

	query = json_dict_build(query, JSON_LIST_VALUE, "set_instance_filters", -2,
				JSON_LIST_VALUE, "set_schema_filters", -2, -1);
	if (!query)
		goto oom;
	sets = json_value_find(query, "set_instance_filters");
	schemas = json_value_find(query, "set_schema_filters");
	LIST_FOREACH(match, updtr->match_list, entry) {
		i = json_entity_new(JSON_STRING_VALUE, match->regex_str);
		if (!i)
			goto oom;
		if (match->selector == LDMSD_NAME_MATCH_INST_NAME)
			json_item_add(sets, i);
		else
			json_item_add(schemas, i);
	}
	if (0 == json_list_len(sets)) {
		json_attr_rem(query, "set_filters");
		json_entity_free(sets);
	}
	if (0 == json_list_len(schemas)) {
		json_attr_rem(query, "set_schema_filters");
		json_entity_free(schemas);
	}
out:
	return query;
oom:
	if (query)
		json_entity_free(query);
	return NULL;
}

json_entity_t ldmsd_updtr_query(ldmsd_cfgobj_t obj)
{
	json_entity_t query, l, i;
	ldmsd_prdcr_ref_t ref;
	ldmsd_updtr_t updtr = (ldmsd_updtr_t)obj;

	query = __updtr_export(updtr);
	if (!query)
		goto oom;

	query = json_dict_build(query,
			JSON_INT_VALUE, "offset_skew_value", updtr->sched.offset_skew,
			JSON_STRING_VALUE, "state", ldmsd_updtr_state_str(updtr->state),
			-1);
	if (!query)
		goto oom;

	/* producers */
	query = json_dict_build(query, JSON_LIST_VALUE, "producers", -2, -1);
	if (!query)
		goto oom;
	l = json_value_find(query, "producers");
	for (ref = ldmsd_updtr_prdcr_first(updtr); ref;
	     ref = ldmsd_updtr_prdcr_next(ref)) {
		i = json_dict_build(NULL,
				JSON_STRING_VALUE, "name", ref->prdcr->obj.name,
				JSON_STRING_VALUE, "hostname", ref->prdcr->host_name,
				JSON_STRING_VALUE, "xprt", ref->prdcr->xprt_name,
				JSON_INT_VALUE, "port_no", ref->prdcr->port_no,
				JSON_STRING_VALUE, "state",
					ldmsd_prdcr_state2str(ref->prdcr->conn_state),
				-1);
		if (!i)
			goto oom;
		json_item_add(l, i);
	}
	return ldmsd_result_new(0, NULL, query);
oom:
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
	return NULL;
}

json_entity_t ldmsd_updtr_export(ldmsd_cfgobj_t obj)
{
	json_entity_t query;
	ldmsd_updtr_t updtr = (ldmsd_updtr_t)obj;

	query = __updtr_export(updtr);
	return ldmsd_result_new(0, NULL, query);
}

json_entity_t
ldmsd_updtr_update(ldmsd_cfgobj_t obj, short enabled, json_entity_t dft, json_entity_t spc)
{
	json_entity_t err = 0;
	unsigned long interval_us, offset_us;
	int perm, auto_task, push_flags;
	struct updtr_match_list *match_list;
	struct ldmsd_regex_list *prdcr_list;
	ldmsd_req_buf_t buf = NULL;
	ldmsd_updtr_t updtr = (ldmsd_updtr_t)obj;

	if (obj->enabled && enabled)
		return ldmsd_result_new(EBUSY, NULL, NULL);

	buf = ldmsd_req_buf_alloc(1024);
	if (!buf)
		goto oom;

	err = __updtr_attr_get(dft, spc, (long *)&interval_us, (long *)&offset_us,
				&push_flags, &auto_task,
				&perm, &prdcr_list, &match_list);
	if (!err && (ENOMEM == errno))
		goto oom;

	if (LDMSD_ATTR_NA != interval_us)
		updtr->sched.intrvl_us = interval_us;
	if (LDMSD_ATTR_NA != offset_us)
		updtr->sched.offset_us = offset_us;
	if (LDMSD_ATTR_NA != perm)
		updtr->obj.perm = perm;
	if (LDMSD_ATTR_NA != auto_task)
		updtr->is_auto_task = auto_task;
	if (LDMSD_ATTR_NA != push_flags)
		updtr->push_flags = push_flags;
	if (prdcr_list) {
		__updtr_prdcr_match_list_free(updtr->prdcr_regex_list);
		updtr->prdcr_regex_list = prdcr_list;
	}
	if (match_list) {
		__updtr_match_list_free(updtr->match_list);
		updtr->match_list = match_list;
	}

	obj->enabled = ((0 <= enabled)?enabled:obj->enabled);
	return ldmsd_result_new(0, NULL, NULL);
oom:
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
	if (buf)
		ldmsd_req_buf_free(buf);
	if (err)
		json_entity_free(err);
	return NULL;
}

static ldmsd_updtr_t
__updtr_new(const char *name, long interval_us, long offset_us,
				int push_flags, int is_auto_task,
				struct ldmsd_regex_list *prdcr_list,
				struct updtr_match_list *match_list,
				uid_t uid, gid_t gid, int perm, int enabled)
{
	struct ldmsd_updtr *updtr = NULL;
	ev_worker_t worker;
	ev_t start_ev, stop_ev;
	char worker_name[PATH_MAX];
	start_ev = stop_ev = NULL;

	snprintf(worker_name, PATH_MAX, "updtr:%s", name);
	worker = ev_worker_new(worker_name, prdcr_set_update_actor);
	if (!worker) {
		ldmsd_log(LDMSD_LERROR,
			  "%s: error %d creating new worker %s\n",
			  __func__, errno, worker_name);
		goto err;
	}

	start_ev = ev_new(updtr_start_type);
	if (!start_ev) {
		ldmsd_log(LDMSD_LERROR,
			  "%s: error %d creating %s event\n",
			  __func__, errno, ev_type_name(updtr_start_type));
		goto err;
	}

	stop_ev = ev_new(updtr_stop_type);
	if (!stop_ev) {
		ldmsd_log(LDMSD_LERROR,
			  "%s: error %d creating %s event\n",
			  __func__, errno, ev_type_name(updtr_stop_type));
		goto err;
	}

	updtr = (struct ldmsd_updtr *)ldmsd_cfgobj_new(name, LDMSD_CFGOBJ_UPDTR,
					sizeof *updtr, ldmsd_updtr___del,
					ldmsd_updtr_update,
					ldmsd_cfgobj_delete,
					ldmsd_updtr_query,
					ldmsd_updtr_export,
					ldmsd_updtr_enable,
					ldmsd_updtr_disable,
					uid, gid, perm, enabled);
	if (!updtr)
		goto err;

	updtr->state = LDMSD_UPDTR_STATE_STOPPED;
	updtr->is_auto_task = is_auto_task;
	updtr->sched.intrvl_us = interval_us;
	updtr->sched.offset_us = offset_us;
	updtr->sched.offset_skew = updtr_sched_offset_skew_get();
	updtr->push_flags = push_flags;
	if (!prdcr_list) {
		updtr->prdcr_regex_list = malloc(sizeof(*prdcr_list));
		if (!updtr->prdcr_regex_list)
			goto oom;
		LIST_INIT(updtr->prdcr_regex_list);
	} else {
		updtr->prdcr_regex_list = prdcr_list;
	}

	if (!match_list) {
		updtr->match_list = malloc(sizeof(*updtr->match_list));
		if (!updtr->match_list)
			goto oom;
		LIST_INIT(updtr->match_list);
	} else {
		updtr->match_list = match_list;
	}

	updtr->worker = worker;
	updtr->start_ev = start_ev;
	updtr->stop_ev = stop_ev;
	EV_DATA(updtr->start_ev, struct start_data)->entity = updtr;
	EV_DATA(updtr->stop_ev, struct start_data)->entity = updtr;

	rbt_init(&updtr->prdcr_tree, prdcr_ref_cmp);

	ldmsd_cfgobj_unlock(&updtr->obj);
	return updtr;

oom:
	errno = ENOMEM;
err:
	if (worker)
		free(worker);
	if (start_ev)
		ev_put(start_ev);
	if (stop_ev)
		ev_put(stop_ev);
	if (updtr)
		ldmsd_updtr___del(&updtr->obj);
	return NULL;
}

json_entity_t ldmsd_updtr_create(const char *name, short enabled, json_entity_t dft,
					json_entity_t spc, uid_t uid, gid_t gid)
{
	int rc;
	json_entity_t result, err = NULL;
	unsigned long interval_us, offset_us;
	int perm, auto_task, push_flags;
	struct updtr_match_list *match_list;
	struct ldmsd_regex_list *prdcr_list;
	ldmsd_updtr_t updtr = NULL;
	ldmsd_req_buf_t buf;

	buf = ldmsd_req_buf_alloc(1024);
	if (!buf)
		goto oom;

	err = __updtr_attr_get(dft, spc, (long *)&interval_us, (long *)&offset_us,
					&push_flags, &auto_task, &perm,
					&prdcr_list, &match_list);
	if (!err && (ENOMEM == errno))
		goto oom;

	if (LDMSD_ATTR_NA == interval_us) {
		err = json_dict_build(err, JSON_STRING_VALUE, "interval",
						"'interval' is missing.", -1);
		if (!err)
			goto oom;
		if (LDMSD_ATTR_NA != offset_us) {
			err = json_dict_build(err, JSON_STRING_VALUE, "offset",
							"'offset' is ignore because "
							"'interval' isn't given.", -1);
			if (!err)
				goto oom;
		}
	} else {
		if ((offset_us < 0) || (offset_us >= interval_us)) {
			rc = ldmsd_req_buf_append(buf, "The value '%ld' is "
					"either less than 0 or equal or larger "
					"than the interval value '%ld'.",
					offset_us, interval_us);
			err = json_dict_build(err, JSON_STRING_VALUE, "offset",
					buf->buf, -1);
		}
	}

	if (LDMSD_ATTR_NA == push_flags)
		push_flags = 0;
	if (LDMSD_ATTR_NA == auto_task)
		auto_task = 0;
	if (LDMSD_ATTR_NA == perm)
		perm = 0770;
	if (push_flags && auto_task) {
		err = json_dict_build(err, JSON_STRING_VALUE, "auto_task",
						"'auto_task' is ignored "
						"because 'push' is in use.", -1);
		if (!err)
			goto oom;
	}

	if (err) {
		ldmsd_req_buf_free(buf);
		return ldmsd_result_new(EINVAL, NULL, err);
	}

	/*
	 * All attribute values are valid. Create the Updater.
	 */
	updtr = __updtr_new(name, interval_us, offset_us, push_flags, auto_task,
				prdcr_list, match_list, uid, gid, perm, enabled);
	if (!updtr) {
		rc = errno;
		ldmsd_req_buf_t buf = ldmsd_req_buf_alloc(512);
		if (!buf)
			goto oom;
		if (0 > ldmsd_req_buf_append(buf, "Failed to create an updater '%s'.", name))
			goto oom;
		result = ldmsd_result_new(rc, buf->buf, NULL);
		ldmsd_req_buf_free(buf);
		return result;
	}
	ldmsd_req_buf_free(buf);
	ldmsd_updtr_unlock(updtr);
	return ldmsd_result_new(0, NULL, NULL);
oom:
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
	if (err)
		json_entity_free(err);
	if (updtr) {
		ldmsd_updtr_unlock(updtr);
		ldmsd_updtr_put(updtr);
	}
	if (buf)
		ldmsd_req_buf_free(buf);
	return NULL;
}
