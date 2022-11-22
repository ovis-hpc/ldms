/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015-2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2015-2019 Open Grid Computing, Inc. All rights reserved.
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

/* a - b */
static inline double ts_diff_usec(struct timespec *a, struct timespec *b)
{
	double aa = a->tv_sec*1e9 + a->tv_nsec;
	double bb = b->tv_sec*1e9 + b->tv_nsec;
	return (aa - bb)/1e3; /* make it usec */
}

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

ldmsd_prdcr_ref_t updtr_prdcr_ref_first(ldmsd_updtr_t updtr)
{
	struct rbn *rbn = rbt_min(&updtr->prdcr_tree);
	if (!rbn)
		return NULL;
	return container_of(rbn, struct ldmsd_prdcr_ref, rbn);
}

ldmsd_prdcr_ref_t updtr_prdcr_ref_next(ldmsd_prdcr_ref_t ref)
{
	struct rbn *rbn = rbn_succ(&ref->rbn);
	if (!rbn)
		return NULL;
	return container_of(rbn, struct ldmsd_prdcr_ref, rbn);
}

/* Caller must hold the updater lock */
static ldmsd_updtr_task_t updtr_task_find(ldmsd_updtr_t updtr,
				struct ldmsd_updtr_schedule *sched)
{
	struct rbn *rbn;
	rbn = rbt_find(&updtr->task_tree, (void *)sched);
	if (!rbn)
		return NULL;
	return container_of(rbn, struct ldmsd_updtr_task, rbn);
}

static ldmsd_updtr_task_t updtr_task_first(ldmsd_updtr_t updtr)
{
	struct rbn *rbn;
	rbn = rbt_min(&updtr->task_tree);
	if (!rbn)
		return NULL;
	return container_of(rbn, struct ldmsd_updtr_task, rbn);
}

static ldmsd_updtr_task_t updtr_task_next(ldmsd_updtr_task_t task)
{
	struct rbn *rbn;
	ldmsd_updtr_task_t next;
	rbn = rbn_succ(&task->rbn);
	if (!rbn)
		return NULL;
	next = container_of(rbn, struct ldmsd_updtr_task, rbn);
	if ((next->hint.intrvl_us != task->hint.intrvl_us) ||
			(next->hint.offset_us != task->hint.offset_us))
		return NULL;
	return next;
}

static void updtr_task_init(ldmsd_updtr_task_t task, ldmsd_updtr_t updtr,
				int is_default, long interval, long offset)
{
	int offset_increment = updtr_sched_offset_skew_get();
	if (offset != LDMSD_UPDT_HINT_OFFSET_NONE) {
		task->task_flags = LDMSD_TASK_F_SYNCHRONOUS;
		task->sched.offset_us = offset + offset_increment;
	} else {
		task->task_flags = 0;
		task->sched.offset_us = LDMSD_UPDT_HINT_OFFSET_NONE;
	}
	task->hint.intrvl_us = task->sched.intrvl_us = interval;
	task->hint.offset_us = offset;
	task->updtr = updtr;
	task->is_default = is_default;
	task->set_count = 0;
	rbn_init(&task->rbn, &task->hint);
	ldmsd_task_init(&task->task);
}

/* Caller must hold the updater lock. */
static ldmsd_updtr_task_t updtr_task_new(ldmsd_updtr_t updtr,
					long hint_interval,
					long hint_offset)
{
	ldmsd_updtr_task_t task = calloc(1, sizeof(*task));
	if (!task)
		return NULL;
	updtr_task_init(task, updtr, 0, hint_interval, hint_offset);
	rbt_ins(&updtr->task_tree, &task->rbn);
	return task;
}

static void updtr_task_cb(ldmsd_task_t task, void *arg);
static inline void updtr_update_task_start(ldmsd_updtr_task_t task)
{
	ldmsd_task_start(&task->task, updtr_task_cb, task,
		task->task_flags, task->sched.intrvl_us, task->sched.offset_us);
}

static inline void updtr_task_stop(ldmsd_updtr_task_t task)
{
	ldmsd_task_stop(&task->task);
}

/* Caller must hold the updater lock. */
static void updtr_task_del(ldmsd_updtr_task_t task)
{
	ldmsd_updtr_t updtr = task->updtr;
	ldmsd_task_join(&task->task);
	rbt_del(&updtr->task_tree, &task->rbn);
	free(task);
}

static void updtr_task_set_add(ldmsd_updtr_task_t task)
{
	__sync_add_and_fetch(&task->set_count, 1);
}

static void updtr_task_set_reset(ldmsd_updtr_task_t task)
{
	task->set_count = 0;
}

static inline void
__stats(struct ldmsd_stat *stat, struct timespec *start, struct timespec *end)
{
	double dur = ts_diff_usec(end, start);

	stat->count++;
	if (1 == stat->count) {
		stat->avg = stat->min = stat->max = dur;
	} else {
		stat->avg = (stat->avg * ((stat->count - 1.0)/stat->count)) + (dur/stat->count);
		if (stat->min > dur)
			stat->min = dur;
		else if (stat->max < dur)
			stat->max = dur;
	}
}

static void updtr_update_cb(ldms_t t, ldms_set_t set, int status, void *arg)
{
	uint64_t gn, push_it = 0;
	ldmsd_prdcr_set_t prd_set = arg;
	int errcode;
	struct timespec start;
	struct timespec end;

	pthread_mutex_lock(&prd_set->lock);
	clock_gettime(CLOCK_REALTIME, &prd_set->updt_stat.end);
	__stats(&prd_set->updt_stat, &prd_set->updt_stat.start, &prd_set->updt_stat.end);

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

	gn = ldms_set_data_gn_get(set);
	if (prd_set->last_gn == gn) {
		ldmsd_log(LDMSD_LINFO, "Set %s oversampled %"PRIu64" == %"PRIu64".\n",
			  prd_set->inst_name, prd_set->last_gn, gn);
		__atomic_fetch_add(&prd_set->oversampled_cnt, 1, __ATOMIC_SEQ_CST);
		goto set_ready;
	}
	prd_set->last_gn = gn;
	push_it = 1;

	ldmsd_strgp_ref_t str_ref;
	LIST_FOREACH(str_ref, &prd_set->strgp_list, entry) {
		ldmsd_strgp_t strgp = str_ref->strgp;

		ldmsd_strgp_lock(strgp);
		clock_gettime(CLOCK_REALTIME, &start);
		strgp->update_fn(strgp, prd_set);
		clock_gettime(CLOCK_REALTIME, &end);
		__stats(&strgp->stat, &start, &end);
		ldmsd_strgp_unlock(strgp);
	}
set_ready:
	if ((status & LDMS_UPD_F_MORE) == 0)
		/* No more data pending move prdcr_set state UPDATING --> READY */
		prd_set->state = LDMSD_PRDCR_SET_STATE_READY;
out:
	pthread_mutex_unlock(&prd_set->lock);
	if (0 == errcode && push_it) {
		ldmsd_log(LDMSD_LDEBUG, "Pushing set %p %s\n",
			  prd_set->set, prd_set->inst_name);
		int rc = ldms_xprt_push(prd_set->set);
		if (rc) {
			ldmsd_log(LDMSD_LERROR, "Failed to push set %s\n",
						prd_set->inst_name);
		}
	}
	if (0 == (status & (LDMS_UPD_F_PUSH|LDMS_UPD_F_MORE)))
		/* Put reference taken before calling ldms_xprt_update. */
		ldmsd_prdcr_set_ref_put(prd_set);
	return;
}

struct str_list_ent_s {
	LIST_ENTRY(str_list_ent_s) entry;
	char str[]; /* '\0' terminated string */
};

struct ldmsd_group_traverse_ctxt {
	ldmsd_prdcr_t prdcr;
	ldmsd_updtr_t updtr;
	ldmsd_updtr_task_t task;
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

void __ldmsd_prdset_lookup_cb(ldms_t xprt, enum ldms_lookup_status status,
			      int more, ldms_set_t set, void *arg);
static int schedule_set_updates(ldmsd_prdcr_set_t prd_set, ldmsd_updtr_task_t task)
{
	int rc = 0;
	int flags;
	char *op_s = "skipped doing anything";
	ldmsd_prdcr_set_t pset;
	ldmsd_updtr_t updtr = task->updtr;
	struct ldmsd_group_traverse_ctxt ctxt;
	/* The reference will be put back in update_cb */
	ldmsd_log(LDMSD_LDEBUG, "Schedule an update for set %s\n",
					prd_set->inst_name);
	int push_flags = 0;
	struct str_list_ent_s *ent;
	LIST_INIT(&ctxt.str_list);
	clock_gettime(CLOCK_REALTIME, &prd_set->updt_stat.start);
	if (!updtr->push_flags) {
		op_s = "Updating";
		prd_set->state = LDMSD_PRDCR_SET_STATE_UPDATING;
		ldmsd_prdcr_set_ref_get(prd_set);
		flags = ldmsd_group_check(prd_set->set);
		if (flags & LDMSD_GROUP_IS_GROUP) {
			/* This is a group */
			ctxt.prdcr = prd_set->prdcr;
			ctxt.updtr = updtr;
			ctxt.task = task;
			/* __grp_iter_cb() will populate ctxt.str_list */
			rc = ldmsd_group_iter(prd_set->set,
					      __grp_iter_cb, &ctxt);
			if (rc)
				goto out;
			LIST_FOREACH(ent, &ctxt.str_list, entry) {
				pset = ldmsd_prdcr_set_find(prd_set->prdcr, ent->str);
				if (!pset)
					continue; /* It is OK. Try again next iteration */
				if (pset->state == LDMSD_PRDCR_SET_STATE_START) {
					/*
					 * The lookup callback of the setgroup
					 * is received before the DIR_ADD
					 * of this set member (pset).
					 *
					 * Thus, do the lookup here.
					 */
					rc = ldms_xprt_lookup(pset->prdcr->xprt,
							      pset->inst_name,
							      LDMS_LOOKUP_BY_INSTANCE,
							      __ldmsd_prdset_lookup_cb, pset);
					if (rc)
						goto out;
				}
				if (pset->state != LDMSD_PRDCR_SET_STATE_READY)
					continue; /* It is OK. The set might not be ready */
				rc = schedule_set_updates(pset, task);
				if (rc)
					goto out;
			}
			prd_set->state = LDMSD_PRDCR_SET_STATE_READY;
			/*
			 * No metrics in the setgroup, so
			 * do not update the setgroup.
			 */
		} else {
			rc = ldms_xprt_update(prd_set->set, updtr_update_cb, prd_set);
		}
	} else if (0 == (prd_set->push_flags & LDMSD_PRDCR_SET_F_PUSH_REG)) {
		op_s = "Registering push for";
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
out:
	while ((ent = LIST_FIRST(&ctxt.str_list))) {
		LIST_REMOVE(ent, entry);
		free(ent);
	}
	if (rc) {
		ldmsd_log(LDMSD_LINFO, "Synchronous error %d: %s Set %s\n",
						rc, op_s, prd_set->inst_name);
		if (!updtr->push_flags)
			ldmsd_prdcr_set_ref_put(prd_set);
	}
	return rc;
}

static int cancel_set_updates(ldmsd_prdcr_set_t prd_set, ldmsd_updtr_t updtr)
{
	int rc;
	ldmsd_log(LDMSD_LDEBUG, "Cancel push for set %s\n", prd_set->inst_name);
	assert(prd_set->set);
	if (!(prd_set->push_flags & LDMSD_PRDCR_SET_F_PUSH_REG))
		return 0;
	rc = ldms_xprt_cancel_push(prd_set->set);
	if (rc) {
		ldmsd_log(LDMSD_LINFO, "Synchronous error %d: canceling push for Set %s\n",
			  rc, prd_set->inst_name);
	}
	/* Put the push reference */
	prd_set->push_flags &= ~LDMSD_PRDCR_SET_F_PUSH_REG;
	return rc;
}

static int __setgrp_members_lookup(ldmsd_prdcr_set_t setgrp)
{
	struct ldmsd_group_traverse_ctxt ctxt;
	struct str_list_ent_s *ent;
	ldmsd_prdcr_set_t pset;
	int rc;

	LIST_INIT(&ctxt.str_list);
	ctxt.prdcr = setgrp->prdcr;
	ctxt.updtr = NULL;
	ctxt.task = NULL;

	/*
	 * __grp_iter_cb() will populate ctxt.str_list
	 */
	rc = ldmsd_group_iter(setgrp->set, __grp_iter_cb, &ctxt);
	if (rc) {
		ldmsd_log(LDMSD_LERROR, "Error %d: Failed to get the set member "
				"list of ssetgroup %s\n", rc, setgrp->inst_name);
		return rc;
	}
	LIST_FOREACH(ent, &ctxt.str_list, entry) {
		pset = ldmsd_prdcr_set_find(setgrp->prdcr, ent->str);
		if (!pset) {
			/*
			 * LDMSD has not received the DIR_ADD of
			 * this pset yet.
			 *
			 * Do lookup when schedule an update for
			 * the set group.
			 */
			continue;
		}
		switch (pset->state) {
			case LDMSD_PRDCR_SET_STATE_READY:
			case LDMSD_PRDCR_SET_STATE_LOOKUP:
				/*
				 * This could occur if the set group stays
				 * in the START state due to the synchronous
				 * lookup error of a set member.
				 */
				continue;
			case LDMSD_PRDCR_SET_STATE_UPDATING:
				ldmsd_log(LDMSD_LINFO, "%s: %s in an "
						"unexpected state (%s)\n",
						__func__, pset->inst_name,
						ldmsd_prdcr_set_state_str(pset->state));
				continue;
			case LDMSD_PRDCR_SET_STATE_START:
				ldmsd_prdcr_set_ref_get(pset);
				pset->state = LDMSD_PRDCR_SET_STATE_LOOKUP;
				rc = ldms_xprt_lookup(setgrp->prdcr->xprt,
						pset->inst_name,
						LDMS_LOOKUP_BY_INSTANCE,
						      __ldmsd_prdset_lookup_cb, pset);
				if (rc) {
					pset->state = LDMSD_PRDCR_SET_STATE_START;
					ldmsd_log(LDMSD_LINFO,
						"Synchronous error %d "
						"from ldms_lookup\n", rc);
					ldmsd_prdcr_set_ref_put(pset);
					goto out;
				}
				break;
			case LDMSD_PRDCR_SET_STATE_DELETED:
			default:
				continue;
		}
	}
out:
	while ((ent = LIST_FIRST(&ctxt.str_list))) {
		LIST_REMOVE(ent, entry);
		free(ent);
	}
	return rc;
}

void __ldmsd_prdset_lookup_cb(ldms_t xprt, enum ldms_lookup_status status,
					int more, ldms_set_t set, void *arg)
{
	ldmsd_prdcr_set_t prd_set = arg;
	int ready = 0;
	int flags;
	pthread_mutex_lock(&prd_set->lock);
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
				  "prdcr %s: The set '%s' (%p) already exists. "
				  "It is likely that there are multiple "
				  "producers providing a set with the same instance name.\n",
				  prd_set->prdcr->obj.name, prd_set->inst_name, set);
		} else {
			ldmsd_log(LDMSD_LERROR,
				  "prdcr %s: Error %d in lookup callback of set '%s' (%p)\n",
				  prd_set->prdcr->obj.name,
				  status, prd_set->inst_name, set);
		}
		prd_set->state = LDMSD_PRDCR_SET_STATE_START;
		goto out;
	}
	if (!prd_set->set) {
		/* This is the first lookup of the set. */
		ldms_set_ref_get(set, "prdcr_set");
		prd_set->set = set;
	} else {
		assert(0 == "multiple lookup on the same prdcr_set");
	}
	flags = ldmsd_group_check(prd_set->set);
	clock_gettime(CLOCK_REALTIME, &prd_set->lookup_complete_ts);
	if (flags & LDMSD_GROUP_IS_GROUP) {
		/*
		 * Lookup the member sets
		 */
		if (__setgrp_members_lookup(prd_set))
			goto out;
	}
	prd_set->state = LDMSD_PRDCR_SET_STATE_READY;
	ldmsd_log(LDMSD_LINFO, "Set %s is ready\n", prd_set->inst_name);
	ldmsd_strgp_update(prd_set);
	ready = 1;
out:
	pthread_mutex_unlock(&prd_set->lock);
	if (ready)
		ldmsd_prd_set_updtr_task_update(prd_set);
	ldmsd_prdcr_set_ref_put(prd_set); /* The ref is taken before calling lookup */
	return;
}

static void schedule_prdcr_updates(ldmsd_updtr_task_t task,
				   ldmsd_prdcr_t prdcr, ldmsd_name_match_t match)
{
	ldmsd_updtr_t updtr = task->updtr;
	struct timespec ts;
	ldmsd_prdcr_lock(prdcr);
	if (prdcr->conn_state != LDMSD_PRDCR_STATE_CONNECTED || prdcr->xprt->disconnected)
		goto out;

	ldmsd_prdcr_set_t prd_set;
	if (updtr->is_auto_task)
		prd_set = ldmsd_prdcr_set_first_by_hint(prdcr, &task->hint);
	else
		prd_set = ldmsd_prdcr_set_first(prdcr);

	while (prd_set) {
		int rc;
		const char *str;

		if (match) {
			if (match->selector == LDMSD_NAME_MATCH_INST_NAME)
				str = prd_set->inst_name;
			else
				str = prd_set->schema_name;
			rc = regexec(&match->regex, str, 0, NULL, 0);
			if (rc)
				goto next_prd_set;
		}

		ldmsd_log(LDMSD_LDEBUG, "updtr_task sched '%ld': set '%s'\n",
				task->sched.intrvl_us, prd_set->inst_name);
		updtr_task_set_add(task);

		switch (prd_set->state) {
		case LDMSD_PRDCR_SET_STATE_READY:
			clock_gettime(CLOCK_REALTIME, &ts);
			if (ts_diff_usec(&ts, &prd_set->lookup_complete_ts) < 1000000) {
				goto next_prd_set;
			}
			break;
		case LDMSD_PRDCR_SET_STATE_START:
			ldmsd_prdcr_set_ref_get(prd_set); /* It will be put back in lookup_cb */
			/* Lookup the set */
			prd_set->state = LDMSD_PRDCR_SET_STATE_LOOKUP;
			assert(prd_set->set == NULL);
			rc = ldms_xprt_lookup(prdcr->xprt, prd_set->inst_name,
					      LDMS_LOOKUP_BY_INSTANCE,
					      __ldmsd_prdset_lookup_cb, prd_set);
			if (rc) {
				/* If the error is EEXIST, the set is already in the set tree. */
				if (rc == EEXIST) {
					ldmsd_log(LDMSD_LERROR, "Prdcr '%s': "
						"lookup failed synchronously. "
						"The set '%s' already exists. "
						"It is likely that there are more "
						"than one producers pointing to "
						"the set.\n",
						prd_set->prdcr->obj.name,
						prd_set->inst_name);
				} else {
					ldmsd_log(LDMSD_LINFO, "Synchronous error "
							"%d from ldms_lookup\n", rc);
				}
				prd_set->state = LDMSD_PRDCR_SET_STATE_START;
				ldmsd_prdcr_set_ref_put(prd_set);
			}
			goto next_prd_set;
		case LDMSD_PRDCR_SET_STATE_LOOKUP:
			ldmsd_log(LDMSD_LINFO, "%s: Set %s: "
				"there is an outstanding lookup.\n",
				__func__, prd_set->inst_name);
			goto next_prd_set;
		case LDMSD_PRDCR_SET_STATE_UPDATING:
			ldmsd_log(LDMSD_LINFO, "%s: Set %s: "
				"there is an outstanding update.\n",
				__func__, prd_set->inst_name);
			__atomic_fetch_add(&prd_set->skipped_upd_cnt, 1, __ATOMIC_SEQ_CST);
		case LDMSD_PRDCR_SET_STATE_DELETED:
		default:
			goto next_prd_set;
		}

		schedule_set_updates(prd_set, task);

next_prd_set:
		if (updtr->is_auto_task)
			prd_set = ldmsd_prdcr_set_next_by_hint(prd_set);
		else
			prd_set = ldmsd_prdcr_set_next(prd_set);
	}
out:
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
			cancel_set_updates(prd_set, updtr);
			continue;
		}
		rc = 1;
		if (match->selector == LDMSD_NAME_MATCH_INST_NAME)
			str = prd_set->inst_name;
		else
			str = prd_set->schema_name;
		rc = regexec(&match->regex, str, 0, NULL, 0);
		if (!rc) {
			cancel_set_updates(prd_set, updtr);
		}
	}
out:
	ldmsd_prdcr_unlock(prdcr);
}

static void schedule_updates(ldmsd_updtr_task_t task)
{
	ldmsd_updtr_t updtr = task->updtr;
	ldmsd_name_match_t match;

	updtr_task_set_reset(task);
	if (!LIST_EMPTY(&updtr->match_list)) {
		LIST_FOREACH(match, &updtr->match_list, entry) {
			ldmsd_prdcr_ref_t ref;
			for (ref = updtr_prdcr_ref_first(updtr); ref;
					ref = updtr_prdcr_ref_next(ref))
				schedule_prdcr_updates(task, ref->prdcr, match);
		}
	} else {
		ldmsd_prdcr_ref_t ref;
		for (ref = updtr_prdcr_ref_first(updtr); ref;
				ref = updtr_prdcr_ref_next(ref))
			schedule_prdcr_updates(task, ref->prdcr, NULL);
	}
	if ((!task->is_default) && (0 == task->set_count))
		updtr_task_stop(task);
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

static void updtr_task_cb(ldmsd_task_t task, void *arg)
{
	ldmsd_updtr_task_t utask = arg;
	ldmsd_updtr_t updtr = utask->updtr;
	ldmsd_updtr_lock(updtr);
	switch (updtr->state) {
	case LDMSD_UPDTR_STATE_STOPPING:
	case LDMSD_UPDTR_STATE_STOPPED:
		break;
	case LDMSD_UPDTR_STATE_RUNNING:
		schedule_updates(utask);
		break;
	}
	ldmsd_updtr_unlock(updtr);
}

/* Delete all unused tasks */
static void __updtr_task_tree_cleanup(ldmsd_updtr_t updtr)
{
	ldmsd_updtr_task_t task;
	struct ldmsd_updtr_task_list unused_task_list;

	LIST_INIT(&unused_task_list);
	for (task = updtr_task_first(updtr); task; task = updtr_task_next(task)) {
		if (task->is_default)
			continue;
		if (task->task.flags & LDMSD_TASK_F_STOP)
			LIST_INSERT_HEAD(&unused_task_list, task, entry);
	}
	LIST_FOREACH(task, &unused_task_list, entry) {
		ldmsd_task_join(&task->task);
		LIST_REMOVE(task, entry);
		updtr_task_del(task);
	}
}

static void updtr_tree_task_cb(ldmsd_task_t task, void *arg)
{
	ldmsd_updtr_task_t utask = arg;
	ldmsd_updtr_t updtr = utask->updtr;
	ldmsd_updtr_lock(updtr);
	switch (updtr->state) {
	case LDMSD_UPDTR_STATE_STOPPING:
	case LDMSD_UPDTR_STATE_STOPPED:
		break;
	case LDMSD_UPDTR_STATE_RUNNING:
		__updtr_task_tree_cleanup(updtr);
		break;
	}
	ldmsd_updtr_unlock(updtr);
}

void __prdcr_set_update_sched(ldmsd_prdcr_set_t prd_set,
				  ldmsd_updtr_task_t updt_task)
{
	prd_set->updt_interval = updt_task->sched.intrvl_us;
	prd_set->updt_offset = updt_task->sched.offset_us;
	prd_set->updt_sync = (updt_task->task_flags & LDMSD_TASK_F_SYNCHRONOUS)?1:0;
}

/**
 * Update the task tree of \c updater
 *
 * Caller must hold the updater lock and the prd_set lock.
 *
 * The updater MUST in RUNNING state.
 */
int ldmsd_updtr_tasks_update(ldmsd_updtr_t updtr, ldmsd_prdcr_set_t prd_set)
{
	ldmsd_updtr_task_t task;
	int rc = 0;

	if (!updtr->is_auto_task) {
		/*
		 * Ignore the update hint and use the default schedule.
		 */
		task = &updtr->default_task;
		goto out;
	} else if (prd_set->updt_hint.intrvl_us == 0) {
		task = &updtr->default_task;
		goto out;
	} else if ((updtr->default_task.hint.intrvl_us == prd_set->updt_hint.intrvl_us) &&
		(updtr->default_task.hint.offset_us == prd_set->updt_hint.offset_us)) {
		/* The default task will update the producer set. */
		task = &updtr->default_task;
		goto out;
	}

	task = updtr_task_find(updtr, &prd_set->updt_hint);
	if (task)
		goto start_task;

	task = updtr_task_new(updtr, prd_set->updt_hint.intrvl_us,
					prd_set->updt_hint.offset_us);
	if (!task)
		return ENOMEM;
start_task:
	updtr_update_task_start(task);
out:
	__prdcr_set_update_sched(prd_set, task);
	if (prd_set->set)
		rc = ldmsd_set_update_hint_set(prd_set->set, task->sched.intrvl_us,
					       task->sched.offset_us);
	return rc;
}

/* Caller must hold the updtr lock. */
static int updtr_tasks_create(ldmsd_updtr_t updtr)
{
	ldmsd_prdcr_ref_t prd_ref;
	ldmsd_prdcr_t prdcr;
	ldmsd_name_match_t match;
	ldmsd_prdcr_set_t prd_set;
	char *str;
	int rc;

	ldmsd_log(LDMSD_LDEBUG, "updtr '%s' getting auto-schedule\n", updtr->obj.name);

	for (prd_ref = updtr_prdcr_ref_first(updtr); prd_ref;
			prd_ref = updtr_prdcr_ref_next(prd_ref)) {
		prdcr = prd_ref->prdcr;
		ldmsd_prdcr_lock(prdcr);
		for (prd_set = ldmsd_prdcr_set_first(prdcr); prd_set;
		     prd_set = ldmsd_prdcr_set_next(prd_set)) {
			pthread_mutex_lock(&prd_set->lock);
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
				goto nxt_prd_set;
			}
		update_tasks:
			rc = ldmsd_updtr_tasks_update(updtr, prd_set);
			if (rc)
				goto err;
		nxt_prd_set:
			pthread_mutex_unlock(&prd_set->lock);
		}
		ldmsd_prdcr_unlock(prdcr);
	}
	return 0;
err:
	pthread_mutex_unlock(&prd_set->lock);
	ldmsd_prdcr_unlock(prdcr);
	return rc;
}

int prdcr_ref_cmp(void *a, const void *b)
{
	return strcmp(a, b);
}

#define UPDTR_TREE_MGMT_TASK_INTRVL 3600000000

ldmsd_updtr_t
ldmsd_updtr_new_with_auth(const char *name, char *interval_str, char *offset_str,
					int push_flags, int is_auto_task,
					uid_t uid, gid_t gid, int perm)
{
	struct ldmsd_updtr *updtr;
	char *endptr;
	long interval_us = UPDTR_TREE_MGMT_TASK_INTRVL, offset_us = LDMSD_UPDT_HINT_OFFSET_NONE;
	updtr = (struct ldmsd_updtr *)
		ldmsd_cfgobj_new_with_auth(name, LDMSD_CFGOBJ_UPDTR,
				 sizeof *updtr, ldmsd_updtr___del,
				 uid, gid, perm);
	if (!updtr)
		return NULL;

	updtr->state = LDMSD_UPDTR_STATE_STOPPED;
	updtr->default_task.is_default = 1;
	updtr->is_auto_task = is_auto_task;
	if (interval_str) {
		interval_us = strtol(interval_str, &endptr, 0);
		if (('\0' == interval_str[0]) || ('\0' != endptr[0]))
			goto einval;
		if (0 >= interval_us)
			goto einval;
		if (offset_str) {
			offset_us = strtol(offset_str, &endptr, 0);
			if (('\0' == offset_str[0]) || ('\0' != endptr[0]))
				goto einval;
			if (interval_us < labs(offset_us) * 2)
				goto einval;
			/* Make it a hint offset */
			offset_us -= updtr_sched_offset_skew_get();
		} else {
			offset_us = LDMSD_UPDT_HINT_OFFSET_NONE;
		}
	} else {
		if (push_flags)
			updtr->default_task.task_flags = LDMSD_TASK_F_IMMEDIATE;
	}
	/* Initialize the default task */
	updtr_task_init(&updtr->default_task, updtr, 1, interval_us, offset_us);
	updtr_task_init(&updtr->tree_mgmt_task, updtr, 1, UPDTR_TREE_MGMT_TASK_INTRVL,
							LDMSD_UPDT_HINT_OFFSET_NONE);
	rbt_init(&updtr->prdcr_tree, prdcr_ref_cmp);
	LIST_INIT(&updtr->match_list);
	rbt_init(&updtr->task_tree, ldmsd_updtr_schedule_cmp);
	updtr->push_flags = push_flags;
	ldmsd_cfgobj_unlock(&updtr->obj);
	return updtr;
einval:
	ldmsd_cfgobj_unlock(&updtr->obj);
	ldmsd_updtr_put(updtr);
	errno = EINVAL;
	return NULL;
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

	updtr_update_task_start(&updtr->default_task);

	if (updtr->is_auto_task) {
		ldmsd_task_start(&updtr->tree_mgmt_task.task, updtr_tree_task_cb,
				&updtr->tree_mgmt_task,
				updtr->tree_mgmt_task.task_flags,
				updtr->tree_mgmt_task.sched.intrvl_us,
				updtr->tree_mgmt_task.sched.offset_us);
	}

	if ((0 == updtr->push_flags) || (updtr->is_auto_task)) {
		/* Task tree isn't needed for updaters that are for 'push' */
		/*
		 * Create the task tree that contains
		 * the tasks that handle the producer sets
		 * that have different hint different from the default task.
		 */
		updtr_tasks_create(updtr);
	}

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
	interval_us = updtr->default_task.sched.intrvl_us;
	offset_us = updtr->default_task.hint.offset_us;
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
		offset_us = strtol(offset_str, NULL, 0)
					- updtr_sched_offset_skew_get();

	if (interval_us < labs(offset_us) * 2) {
		ldmsd_log(LDMSD_LERROR, "%s: The absolute value of the offset"
			" value must not be larger than the half of "
			"the update interval. (i=%ld, o=%ld)\n", "ldmsd_updtr_start",
			interval_us, offset_us);
		rc = EINVAL;
		goto err;
	}
	/* Initialize the default task */
	updtr_task_init(&updtr->default_task, updtr, 1, interval_us, offset_us);
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
	ldmsd_updtr_task_t task;

	/* Stop the default task */
	ldmsd_task_stop(&updtr->default_task.task);
	ldmsd_task_join(&updtr->default_task.task);

	/* Stop the task tree management task */
	ldmsd_task_stop(&updtr->tree_mgmt_task.task);
	ldmsd_task_join(&updtr->tree_mgmt_task.task);

	while (!rbt_empty(&updtr->task_tree)) {
		task = updtr_task_first(updtr);
		ldmsd_task_stop(&task->task);
		updtr_task_del(task);
	}
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

	if (ldmsd_compile_regex(&match->regex, regex_str, rep_buf, rep_len)) {
		rc = -EINVAL;
		goto out_3;
	}

	LIST_INSERT_HEAD(&updtr->match_list, match, entry);
	goto out_1;

out_3:
	free(match->regex_str);
out_2:
	free(match);
out_1:
	ldmsd_updtr_unlock(updtr);
	ldmsd_updtr_put(updtr);
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
	return rc;
}

ldmsd_prdcr_ref_t prdcr_ref_new(ldmsd_prdcr_t prdcr)
{
	ldmsd_prdcr_ref_t ref = calloc(1, sizeof *ref);
	if (ref) {
		ref->prdcr = ldmsd_prdcr_get(prdcr);
		rbn_init(&ref->rbn, prdcr->obj.name);
	}
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
		return EINVAL;

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
	if (rc) {
		rc = EINVAL;
		goto out_0;
	}

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

/**
 * Compare two updater schedule.
 */
int ldmsd_updtr_schedule_cmp(void *a, const void *b)
{
	struct ldmsd_updtr_schedule *ka;
	const struct ldmsd_updtr_schedule *kb;
	int diff;
	ka = a;
	kb = b;
	diff = ka->intrvl_us - kb->intrvl_us;
	if (diff)
		return diff;
	if (ka->offset_us == kb->offset_us)
		return 0;
	if (ka->offset_us == LDMSD_UPDT_HINT_OFFSET_NONE)
		return -1;
	else if (kb->offset_us == LDMSD_UPDT_HINT_OFFSET_NONE)
		return 1;

	return ka->offset_us - kb->offset_us;
}

ldmsd_updtr_task_t updtr_task_get(struct rbn *rbn)
{
	return container_of(rbn, struct ldmsd_updtr_task, rbn);
}
