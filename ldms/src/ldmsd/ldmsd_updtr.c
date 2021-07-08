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

#define _GNU_SOURCE
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <coll/rbt.h>
#include <ovis_util/util.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldms_xprt.h"
#include "ldmsd_event.h"
#include "ldmsd_request.h"
#include "ldmsd_cfgobj.h"
#include "config.h"

extern void ldmsd_req_ctxt_sec_get(ldmsd_req_ctxt_t rctxt, ldmsd_sec_ctxt_t sctxt);

ldmsd_updtr_t ldmsd_updtr_first()
{
	return (ldmsd_updtr_t)ldmsd_cfgobj_first(LDMSD_CFGOBJ_UPDTR);
}

ldmsd_updtr_t ldmsd_updtr_next(struct ldmsd_updtr *updtr)
{
	return (ldmsd_updtr_t)ldmsd_cfgobj_next(&updtr->obj);
}

ldmsd_name_match_t updtr_find_match_ex(ldmsd_updtr_t updtr,
				       enum ldmsd_name_match_sel sel,
				       const char *ex)
{
	ldmsd_name_match_t match;
	for (match = ldmsd_name_match_first(&updtr->match_list); match;
			match = ldmsd_name_match_next(match)) {
		if (match->selector != sel)
			continue;
		if (0 == strcmp(match->regex_str, ex))
			return match;
	}
	return NULL;
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

struct updtr_start_ctxt {
	struct ldmsd_cfgobj_cfg_ctxt base;
	int is_deferred;
	long interval_us;
	long offset_us;
	int is_auto;
};

struct updtr_filter_ctxt {
	struct ldmsd_cfgobj_cfg_ctxt base;
	struct ldmsd_name_match *match;
};

void __updtr_filter_ctxt_destory(void *args)
{
	struct updtr_filter_ctxt *ctxt = args;
	/* Do not free ctxt->match. updtr is accessing it */
	free(ctxt);
}

void __updtr_info_destroy(void *x)
{
	struct updtr_info *info = (struct updtr_info *)x;
	ldmsd_match_queue_free(&info->prdcr_list);
	ldmsd_match_queue_free(&info->match_list);
	free(x);
}

static struct updtr_info *__updtr_info_get(ldmsd_updtr_t updtr)
{
	int rc;
	struct updtr_info *info = malloc(sizeof(*info));
	if (!info)
		return NULL;
	TAILQ_INIT(&info->prdcr_list);
	TAILQ_INIT(&info->match_list);
	ref_init(&info->ref, "create", __updtr_info_destroy, info);
	info->is_auto = updtr->is_auto_task;
	info->sched = updtr->sched;
	info->push_flags = updtr->push_flags;
	rc = ldmsd_match_queue_copy(&updtr->prdcr_list, &info->prdcr_list);
	if (rc)
		goto enomem;
	rc = ldmsd_match_queue_copy(&updtr->match_list, &info->match_list);
	if (rc)
		goto enomem;
	return info;
enomem:
	ldmsd_match_queue_free(&info->prdcr_list);
	ldmsd_match_queue_free(&info->match_list);
	free(info);
	return NULL;
}

static int __post_updtr_state_ev(ldmsd_updtr_t updtr, ev_worker_t dst, void *ctxt)
{
	struct updtr_info *info;
	ev_t e = ev_new(updtr_state_type);
	if (!e) {
		ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
		return ENOMEM;
	}
	EV_DATA(e, struct updtr_state_data)->state = updtr->state;
	info = __updtr_info_get(updtr);
	if (!info)
		goto enomem;
	EV_DATA(e, struct updtr_state_data)->updtr_info = info;
	EV_DATA(e, struct updtr_state_data)->ctxt = ctxt;
	EV_DATA(e, struct updtr_state_data)->obj = NULL;
	return ev_post(updtr->worker, dst, e, 0);
enomem:
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
	ev_put(e);
	return ENOMEM;
}

void ldmsd_updtr___del(ldmsd_cfgobj_t obj)
{
	ldmsd_updtr_t updtr = (ldmsd_updtr_t)obj;

	ldmsd_match_queue_free(&updtr->match_list);
	ldmsd_match_queue_free(&updtr->prdcr_list);
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

void __prdcr_set_update_sched(ldmsd_prdcr_set_t prd_set,
				  ldmsd_updtr_task_t updt_task)
{
	prd_set->updt_interval = updt_task->sched.intrvl_us;
	prd_set->updt_offset = updt_task->sched.offset_us;
	prd_set->updt_sync = (updt_task->task_flags & LDMSD_TASK_F_SYNCHRONOUS)?1:0;
}

#define UPDTR_TREE_MGMT_TASK_INTRVL 3600000000

ldmsd_updtr_t
ldmsd_updtr_new_with_auth(const char *name, char *interval_str, char *offset_str,
					int push_flags, int is_auto_task,
					uid_t uid, gid_t gid, int perm)
{
	extern struct rbt *cfgobj_trees[];
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
	}
	TAILQ_INIT(&updtr->match_list);
	TAILQ_INIT(&updtr->prdcr_list);
	updtr->push_flags = push_flags;
	updtr->sched.intrvl_us = interval_us;
	updtr->sched.offset_us = offset_us;

	updtr->worker = assign_updtr_worker();
	ldmsd_cfgobj_unlock(&updtr->obj);
	return updtr;
einval:
	rbt_del(cfgobj_trees[LDMSD_CFGOBJ_UPDTR], &updtr->obj.rbn);
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

int post_cfg_rsp2tree(ev_worker_t src, struct ldmsd_cfgobj_cfg_ctxt *cfg_ctxt,
			struct ldmsd_cfgobj_cfg_rsp *rsp)
{
	ev_t rsp_ev = ev_new(cfgobj_rsp_type);
	if (!rsp_ev)
		return ENOMEM;
	EV_DATA(rsp_ev, struct cfgobj_rsp_data)->ctxt = cfg_ctxt;
	EV_DATA(rsp_ev, struct cfgobj_rsp_data)->rsp = rsp;
	return ev_post(src, updtr_tree_w, rsp_ev, NULL);
}

int updtr_status_prdcr_filter_req_actor(ev_worker_t src, ev_worker_t dst,
					ev_status_t status, ev_t e)
{
	if (EV_OK != status)
		return 0;

	int rc;
	size_t cnt = 0;
	struct ldmsd_cfgobj_cfg_rsp *rsp = EV_DATA(e, struct prdcr_filter_req_data)->rsp;
	struct ldmsd_match_queue *prdcr_list = &EV_DATA(e, struct prdcr_filter_req_data)->prdcr_list;
	struct ldmsd_name_match *ent;
	struct ldmsd_msg_buf *buf = rsp->ctxt;

	if (rsp->errcode && (rsp->errcode != EBUSY)) {
		/*
		 * Fail to get the producer list
		 */
		goto out;
	}

	for (ent = ldmsd_name_match_first(prdcr_list); ent;
			ent = ldmsd_name_match_next(ent)) {
		if (cnt) {
			cnt = ldmsd_msg_buf_append(buf, ",", ent->regex_str);
			if (cnt < 0) {
				LDMSD_LOG_ENOMEM();
				goto enomem;
			}
		}
		cnt = ldmsd_msg_buf_append(buf, "%s", ent->regex_str);
		if (cnt < 0) {
			LDMSD_LOG_ENOMEM();
			goto enomem;
		}
	}
	cnt = ldmsd_msg_buf_append(buf, "]}");
	if (cnt < 0) {
		LDMSD_LOG_ENOMEM();
		goto enomem;
	}

	rsp->errcode = 0;
out:
	rc = post_cfg_rsp2tree(EV_DATA(e, struct prdcr_filter_req_data)->resp_worker,
			EV_DATA(e, struct prdcr_filter_req_data)->cfg_ctxt, rsp);
	if (rc)
		goto err;

	ldmsd_match_queue_free(&EV_DATA(e, struct prdcr_filter_req_data)->prdcr_list);
	ldmsd_match_queue_free(&EV_DATA(e, struct prdcr_filter_req_data)->filter);
	ev_put(e);
	return 0;
enomem:
	rc = ENOMEM;
err:
	ldmsd_msg_buf_free((struct ldmsd_msg_buf *)rsp->ctxt);
	free(rsp);
	ldmsd_match_queue_free(prdcr_list);
	ldmsd_match_queue_free(&EV_DATA(e, struct prdcr_filter_req_data)->filter);
	ev_put(e);
	return rc;
}

static const char *update_mode(int push_flags)
{
	if (!push_flags)
		return "Pull";
	if (push_flags & LDMSD_UPDTR_F_PUSH_CHANGE)
		return "Push on Change";
	return "Push on Request";
}

static struct ldmsd_msg_buf
*__updtr_status_json_obj(ldmsd_updtr_t updtr)
{
	struct ldmsd_name_match *prdcr_filt;
	struct ldmsd_name_match *match;
	int count;
	size_t cnt;
	struct ldmsd_msg_buf *buf = ldmsd_msg_buf_new(1024);
	if (!buf)
		return NULL;

	cnt = ldmsd_msg_buf_append(buf, "{\"name\":\"%s\","
					"\"interval\":\"%ld\",",
					updtr->obj.name,
					updtr->sched.intrvl_us);
	if (cnt < 0) {
		LDMSD_LOG_ENOMEM();
		goto enomem;
	}

	if (updtr->sched.offset_us == LDMSD_UPDT_HINT_OFFSET_NONE) {
		cnt = ldmsd_msg_buf_append(buf, "\"offset\":\"NONE\","
						"\"sync\":\"false\",");
	} else {
		cnt = ldmsd_msg_buf_append(buf, "\"offset\":\"%ld\","
						"\"sync\":\"true\",",
						updtr->sched.offset_us);
	}
	if (cnt < 0) {
		LDMSD_LOG_ENOMEM();
		goto enomem;
	}

	cnt = ldmsd_msg_buf_append(buf,
				"\"mode\":\"%s\","
				"\"auto\":\"%s\","
				"\"state\":\"%s\","
				"\"producer_filter\":[",
				update_mode(updtr->push_flags),
				(updtr->is_auto_task ? "true" : "false"),
				ldmsd_updtr_state_str(updtr->state));
	if (cnt < 0) {
		LDMSD_LOG_ENOMEM();
		goto enomem;
	}

	count = 0;
	for (prdcr_filt = ldmsd_name_match_first(&updtr->prdcr_list); prdcr_filt;
			prdcr_filt = ldmsd_name_match_next(prdcr_filt)) {
		if (count) {
			cnt = ldmsd_msg_buf_append(buf, ",\n");
			if (cnt < 0) {
				LDMSD_LOG_ENOMEM();
				goto enomem;
			}
		}
		count++;
		cnt = ldmsd_msg_buf_append(buf, "\"%s\"", prdcr_filt->regex_str);
		if (cnt < 0) {
			LDMSD_LOG_ENOMEM();
			goto enomem;
		}
	}
	cnt = ldmsd_msg_buf_append(buf, "],"
					"\"match_filter\":[");
	if (cnt < 0) {
		LDMSD_LOG_ENOMEM();
		goto enomem;
	}
	count = 0;
	for (match = ldmsd_name_match_first(&updtr->match_list); match;
			match = ldmsd_name_match_next(match)) {
		if (count) {
			cnt = ldmsd_msg_buf_append(buf, ",\n");
			if (cnt < 0) {
				LDMSD_LOG_ENOMEM();
				goto enomem;
			}
		}
		count++;
		cnt = ldmsd_msg_buf_append(buf,
				"{\"match\":\"%s\","
				"\"selector\":\"%s\"}",
				match->regex_str,
				((match->selector==LDMSD_NAME_MATCH_INST_NAME)?"instance name":"schema name"));
		if (cnt < 0) {
			LDMSD_LOG_ENOMEM();
			goto enomem;
		}
	}
	cnt = ldmsd_msg_buf_append(buf, "],");
	if (cnt < 0) {
		LDMSD_LOG_ENOMEM();
		goto enomem;
	}
	cnt = ldmsd_msg_buf_append(buf, "\"producers\":[");
	if (cnt < 0) {
		LDMSD_LOG_ENOMEM();
		goto enomem;
	}
	return buf;
enomem:
	ldmsd_msg_buf_free(buf);
	return NULL;
}

static struct ldmsd_cfgobj_cfg_rsp *
updtr_status_handler(ldmsd_updtr_t updtr, void *cfg_ctxt)
{
	struct ldmsd_cfgobj_cfg_rsp *rsp;
	struct ldmsd_match_queue *prdcr_list;
	ev_t prdcr_list_ev;

	rsp = calloc(1, sizeof(*rsp));
	if (!rsp) {
		LDMSD_LOG_ENOMEM();
		return NULL;
	}
	rsp->ctxt = __updtr_status_json_obj(updtr);
	if (!rsp->ctxt)
		goto err;
	ldmsd_updtr_get(updtr);

	/* Get the producer list from the prdcr_tree worker */
	prdcr_list_ev = ev_new(prdcr_filter_req_type);
	if (!prdcr_list_ev)
		goto err;
	prdcr_list = &EV_DATA(prdcr_list_ev, struct prdcr_filter_req_data)->filter;
	if (ldmsd_match_queue_copy(&updtr->prdcr_list, prdcr_list)) {
		ev_put(prdcr_list_ev);
		goto err;
	}
	EV_DATA(prdcr_list_ev, struct prdcr_filter_req_data)->resp_worker = updtr->worker;
	EV_DATA(prdcr_list_ev, struct prdcr_filter_req_data)->rsp = rsp;
	EV_DATA(prdcr_list_ev, struct prdcr_filter_req_data)->cfg_ctxt = cfg_ctxt;
	ev_post(updtr->worker, prdcr_tree_w, prdcr_list_ev, NULL);
	return rsp;
err:
	if (rsp && rsp->ctxt)
		ldmsd_msg_buf_free((struct ldmsd_msg_buf *)rsp->ctxt);
	free(rsp);
	errno = ENOMEM;
	return NULL;
}

static struct ldmsd_cfgobj_cfg_rsp *
updtr_stop_handler(ldmsd_updtr_t updtr, void *cfg_ctxt)
{
	struct ldmsd_cfgobj_cfg_ctxt *ctxt = (struct ldmsd_cfgobj_cfg_ctxt *)cfg_ctxt;
	struct ldmsd_cfgobj_cfg_rsp *rsp;
	int rc;

	rsp = calloc(1, sizeof(*rsp));
	if (!rsp)
		goto enomem;

	rc = ldmsd_cfgobj_access_check(&updtr->obj, 0222, &ctxt->sctxt);
	if (rc) {
		rsp->errcode = EPERM;
		rc = asprintf(&rsp->errmsg, "prdcr '%s' permission denied.",
							updtr->obj.name);
		if (rc < 0)
			goto enomem;
		else
			goto out;
	}
	if (updtr->state == LDMSD_UPDTR_STATE_STOPPED)
		goto out;

	if (updtr->state == LDMSD_UPDTR_STATE_STOPPING) {
		rsp->errcode = EBUSY;
		rc = asprintf(&rsp->errmsg, "updtr '%s' already stopped.",
							updtr->obj.name);
		if (rc < 0)
			goto enomem;
		else
			goto out;
	}

	updtr->state = LDMSD_UPDTR_STATE_STOPPING;
	updtr->obj.perm &= ~LDMSD_PERM_DSTART;
	if (updtr->push_flags) {
		rc = __post_updtr_state_ev(updtr, prdcr_tree_w, NULL);
		if (rc == ENOMEM) {
			goto enomem;
		} else if (rc) {
			rsp->errcode = EINTR;
			ldmsd_log(LDMSD_LERROR, "%s to stop updtr '%s'. Error %d\n",
							__func__, updtr->obj.name, rc);
			rc = asprintf(&rsp->errmsg, "Failed to stop updtr '%s'.",
								updtr->obj.name);
			if (rc < 0)
				goto enomem;
			goto out;
		}
	}
	updtr->state = LDMSD_UPDTR_STATE_STOPPED;
out:
	return rsp;
enomem:
	LDMSD_LOG_ENOMEM();
	free(rsp);
	errno = ENOMEM;
	return NULL;
}

static struct ldmsd_cfgobj_cfg_rsp *
updtr_del_handler(ldmsd_updtr_t updtr, struct ldmsd_cfgobj_cfg_ctxt *cfg_ctxt)
{
	int rc;
	struct ldmsd_cfgobj_cfg_ctxt *ctxt = (struct ldmsd_cfgobj_cfg_ctxt *)cfg_ctxt;
	struct ldmsd_cfgobj_cfg_rsp *rsp = calloc(1, sizeof(*rsp));
	if (!rsp) {
		LDMSD_LOG_ENOMEM();
		goto enomem;
	}

	rsp->errcode = ldmsd_cfgobj_access_check(&updtr->obj, 0222, &ctxt->sctxt);
	if (rsp->errcode) {
		rc = asprintf(&rsp->errmsg, "updtr '%s' permission denied.",
							  updtr->obj.name);
		if (rc < 0) {
			LDMSD_LOG_ENOMEM();
			goto enomem;
		} else {
			goto out;
		}
	}

	if (updtr->state != LDMSD_UPDTR_STATE_STOPPED) {
		rsp->errcode = EBUSY;
		rc = asprintf(&rsp->errmsg, "updtr '%s' is in use.", updtr->obj.name);
		if (rc < 0) {
			LDMSD_LOG_ENOMEM();
			goto enomem;
		} else {
			goto out;
		}
	}

	if (ldmsd_cfgobj_refcount(&updtr->obj) > 2) {
		rsp->errcode = EBUSY;
		rc = asprintf(&rsp->errmsg, "updtr '%s' is in use.", updtr->obj.name);
		if (rc < 0) {
			LDMSD_LOG_ENOMEM();
			goto enomem;
		} else {
			goto out;
		}
	}
	rsp->ctxt = ldmsd_updtr_get(updtr);
out:
	return rsp;

enomem:
	errno = ENOMEM;
	free(rsp);
	return NULL;
}

static struct ldmsd_cfgobj_cfg_rsp *
updtr_start_handler(ldmsd_updtr_t updtr, struct ldmsd_cfgobj_cfg_ctxt *cfg_ctxt)
{
	int rc = 0;
	struct updtr_start_ctxt *ctxt = (struct updtr_start_ctxt *)cfg_ctxt;
	struct ldmsd_cfgobj_cfg_rsp *rsp;

	ldmsd_log(LDMSD_LDEBUG, "%s: updtr %s -- deferred: %s\n",
			__func__, updtr->obj.name, (ctxt->is_deferred?"yes":"no"));

	rsp = calloc(1, sizeof(*rsp));
	if (!rsp)
		goto enomem;

	rc = ldmsd_cfgobj_access_check(&updtr->obj, 0222, &ctxt->base.sctxt);
	if (rc) {
		rsp->errcode = EPERM;
		rc = asprintf(&rsp->errmsg, "updtr '%s' permission denied.",
							updtr->obj.name);
		if (rc < 0)
			goto enomem;
		else
			goto out;
	}

	if (updtr->state != LDMSD_UPDTR_STATE_STOPPED) {
		rsp->errcode = EBUSY;
		rc = asprintf(&rsp->errmsg, "updtr '%s' is already running.",
								updtr->obj.name);
		if (rc < 0)
			goto enomem;
		else
			goto out;
	}

	if (ctxt->is_auto >= 0)
		updtr->is_auto_task = ctxt->is_auto;

	if (ctxt->interval_us > 0)
		updtr->sched.intrvl_us = ctxt->interval_us;
	if (ctxt->offset_us > 0)
		updtr->sched.offset_us = ctxt->offset_us;

	updtr->obj.perm |= LDMSD_PERM_DSTART;
	if (ctxt->is_deferred)
		goto out;

	updtr->state = LDMSD_UPDTR_STATE_RUNNING;
	rc = __post_updtr_state_ev(updtr, prdcr_tree_w, NULL);
	if (rc == ENOMEM) {
		goto enomem;
	} else if (rc) {
		rsp->errcode = EINTR;
		ldmsd_log(LDMSD_LERROR, "%s to stop updtr '%s'. Error %d\n",
						__func__, updtr->obj.name, rc);
		rc = asprintf(&rsp->errmsg, "Failed to stop updtr '%s'.",
							updtr->obj.name);
		if (rc < 0)
			goto enomem;
	}
out:
	return rsp;
enomem:
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
	free(rsp);
	errno = ENOMEM;
	return NULL;
}

static struct ldmsd_cfgobj_cfg_rsp *
updtr_match_filter_handler(ldmsd_updtr_t updtr, struct ldmsd_cfgobj_cfg_ctxt *cfg_ctxt,
								uint32_t req_id)
{
	int rc = 0;
	struct updtr_filter_ctxt *ctxt = (struct updtr_filter_ctxt *)cfg_ctxt;
	struct ldmsd_cfgobj_cfg_rsp *rsp;
	struct ldmsd_name_match *m;

	ldmsd_log(LDMSD_LDEBUG, "UPDTR '%s': received match_add\n", updtr->obj.name);

	rsp = calloc(1, sizeof(*rsp));
	if (!rsp)
		goto enomem;

	rc = ldmsd_cfgobj_access_check(&updtr->obj, 0222, &ctxt->base.sctxt);
	if (rc) {
		rsp->errcode = EPERM;
		rc = asprintf(&rsp->errmsg, "updtr '%s' permission denied.",
							   updtr->obj.name);
		if (rc < 0)
			goto enomem;
		else
			goto out;
	}
	if (updtr->state != LDMSD_UPDTR_STATE_STOPPED) {
		rsp->errcode = EBUSY;
		rc = asprintf(&rsp->errmsg, "Configuration changes cannot be made "
				"while the updater is running");
		if (rc < 0)
			goto enomem;
		else
			goto out;
	}

	m = updtr_find_match_ex(updtr, ctxt->match->selector, ctxt->match->regex_str);
	if (LDMSD_UPDTR_MATCH_ADD_REQ == req_id) {
		if (!m) {
			m = ldmsd_name_match_copy(ctxt->match);
			TAILQ_INSERT_TAIL(&updtr->match_list, m, entry);
		}
	} else {
		if (m) {
			TAILQ_REMOVE(&updtr->match_list, m, entry);
			ldmsd_name_match_free(m);
		}
	}
out:
	return rsp;
enomem:
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
	free(rsp);
	errno = ENOMEM;
	return NULL;
}

static struct ldmsd_cfgobj_cfg_rsp *
updtr_prdcr_filter_handler(ldmsd_updtr_t updtr, struct ldmsd_cfgobj_cfg_ctxt *cfg_ctxt,
								uint32_t req_id)
{
	int rc = 0;
	struct updtr_filter_ctxt *ctxt = (struct updtr_filter_ctxt *)cfg_ctxt;
	struct ldmsd_cfgobj_cfg_rsp *rsp;
	struct ldmsd_name_match *m;

	ldmsd_log(LDMSD_LDEBUG, "%s: updtr %s -- %s\n",
			__func__, updtr->obj.name, ldmsd_req_id2str(req_id));

	rsp = calloc(1, sizeof(*rsp));
	if (!rsp)
		goto enomem;

	rc = ldmsd_cfgobj_access_check(&updtr->obj, 0222, &ctxt->base.sctxt);
	if (rc) {
		rsp->errcode = EPERM;
		rc = asprintf(&rsp->errmsg, "updtr '%s' permission denied.",
							   updtr->obj.name);
		if (rc < 0)
			goto enomem;
		else
			goto out;
	}
	if (updtr->state != LDMSD_UPDTR_STATE_STOPPED) {
		rsp->errcode = EBUSY;
		rc = asprintf(&rsp->errmsg, "Configuration changes cannot be made "
				"while the updater is running");
		if (rc < 0)
			goto enomem;
		else
			goto out;
	}

	m = updtr_find_match_ex(updtr, ctxt->match->selector, ctxt->match->regex_str);
	if (LDMSD_UPDTR_PRDCR_ADD_REQ == req_id) {
		if (!m) {
			m = ldmsd_name_match_copy(ctxt->match);
			if (!m)
				goto enomem;
			TAILQ_INSERT_TAIL(&updtr->prdcr_list, m, entry);
		}
	} else {
		if (m) {
			TAILQ_REMOVE(&updtr->prdcr_list, m, entry);
			ldmsd_name_match_free(m);
		}
	}
out:
	return rsp;
enomem:
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
	errno = ENOMEM;
	free(rsp);
	return NULL;
}

int updtr_cfg_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e)
{
	if (EV_OK != status)
		return 0;

	int rc = 0;
	struct ldmsd_cfgobj_cfg_rsp *rsp;
	ev_t rsp_ev;

	ldmsd_updtr_t updtr = (ldmsd_updtr_t)EV_DATA(e, struct cfgobj_data)->obj;
	struct ldmsd_cfgobj_cfg_ctxt *cfg_ctxt = EV_DATA(e, struct cfgobj_data)->ctxt;
	ldmsd_req_ctxt_t reqc = cfg_ctxt->reqc;

	errno = 0;
	switch (reqc->req_id) {
	case LDMSD_UPDTR_PRDCR_ADD_REQ:
	case LDMSD_UPDTR_PRDCR_DEL_REQ:
		rsp = updtr_prdcr_filter_handler(updtr, cfg_ctxt, reqc->req_id);
		break;
	case LDMSD_UPDTR_MATCH_ADD_REQ:
	case LDMSD_UPDTR_MATCH_DEL_REQ:
		rsp = updtr_match_filter_handler(updtr, cfg_ctxt, reqc->req_id);
		break;
	case LDMSD_UPDTR_START_REQ:
	case LDMSD_UPDTR_DEFER_START_REQ:
		rsp = updtr_start_handler(updtr, cfg_ctxt);
		break;
	case LDMSD_UPDTR_DEL_REQ:
		rsp = updtr_del_handler(updtr, cfg_ctxt);
		break;
	case LDMSD_UPDTR_STOP_REQ:
		rsp = updtr_stop_handler(updtr, cfg_ctxt);
		break;
	case LDMSD_UPDTR_STATUS_REQ:
		rsp = updtr_status_handler(updtr, cfg_ctxt);
		if (!rsp)
			goto enomem;
		goto out;
	default:
		ldmsd_log(LDMSD_LERROR, "%s received an unsupported request ID %d.\n",
							__func__, reqc->req_id);
		assert(0);
		rc = EINTR;
		goto err;
	}

	if (!rsp) {
		if (EBUSY == errno) {
			/* The response is not done */
			goto out;
		} else {
			goto enomem;
		}
	}

	rsp_ev = ev_new(cfgobj_rsp_type);
	if (!rsp_ev)
		goto enomem;
	EV_DATA(rsp_ev, struct cfgobj_rsp_data)->ctxt = cfg_ctxt;
	EV_DATA(rsp_ev, struct cfgobj_rsp_data)->rsp = rsp;
	ev_post(updtr->worker, updtr_tree_w, rsp_ev, 0);
	goto out;

enomem:
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
	rc = ENOMEM;
err:
	free(rsp);
out:
	ldmsd_updtr_put(updtr);
	ev_put(e);
	return rc;
}

int updtr_prdset_state_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e)
{
	if (EV_OK != status)
		goto out;

	int rc;
	char *s;
	struct ldmsd_name_match *prdcr_filt;
	struct ldmsd_name_match *match;
	ldmsd_updtr_t updtr = (ldmsd_updtr_t)EV_DATA(e, struct prdset_state_data)->obj;
	struct prdset_state_data *prdset_data = EV_DATA(e, struct prdset_state_data);
	struct prdset_info *prdset_info = prdset_data->prdset_info;

	ldmsd_log(LDMSD_LDEBUG, "updtr '%s' received prdset_state from "
			"prdset '%s' with state %s\n",
			updtr->obj.name, prdset_info->inst_name,
			ldmsd_prdcr_set_state_str(prdset_data->state));

	if (LDMSD_UPDTR_STATE_RUNNING != updtr->state)
		goto out;

	for (prdcr_filt = ldmsd_name_match_first(&updtr->prdcr_list); prdcr_filt;
			prdcr_filt = ldmsd_name_match_next(prdcr_filt)) {
		if (prdcr_filt->is_regex) {
			rc = regexec(&prdcr_filt->regex, prdset_info->prdcr_name, 0, NULL, 0);
		} else {
			rc = strcmp(prdcr_filt->regex_str, prdset_info->prdcr_name);
		}
		if (0 == rc) {
			/* Producer matched */
			goto match;
		}
	}
	/* Producer not matched */
	goto out;

match:
	if (TAILQ_EMPTY(&updtr->match_list)) {
		ref_get(&prdset_info->ref, "updtr2prdset");
		rc = __post_updtr_state_ev(updtr, prdset_info->prdset_worker,
							   prdset_info);
		if (rc)
			ref_put(&prdset_info->ref, "updtr2prdset");
	} else {
		for (match = ldmsd_name_match_first(&updtr->match_list); match;
				match = ldmsd_name_match_next(match)) {
			if (LDMSD_NAME_MATCH_INST_NAME == match->selector)
				s = prdset_info->inst_name;
			else
				s = prdset_info->schema_name;
			rc = regexec(&match->regex, s, 0, NULL, 0);
			if (0 == rc) {
				/* matched */
				ref_get(&prdset_info->ref, "updtr2prdset");
				rc = __post_updtr_state_ev(updtr, prdset_info->prdset_worker,
								   prdset_info);
				if (rc)
					ref_put(&prdset_info->ref, "updtr2prdset");
				break;
			}
		}
	}

out:
	ldmsd_updtr_put(updtr);
	ref_put(&prdset_info->ref, "updtr_tree2updtr");
	ev_put(e);
	return 0;
}

static int tree_updtr_match_filter_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	char *updtr_name, *attr_name;
	updtr_name = NULL;
	struct updtr_filter_ctxt *ctxt;
	struct ldmsd_name_match *match;
	char *mtype;
	ldmsd_updtr_t updtr;

	ctxt = calloc(1, sizeof(*ctxt));
	if (!ctxt)
		goto enomem;
	ref_init(&ctxt->base.ref, "create", __updtr_filter_ctxt_destory, ctxt);
	match = malloc(sizeof(struct ldmsd_name_match));
	if (!match)
		goto enomem;
	match->is_regex = 1;
	ctxt->match = match;

	reqc->errcode = 0;

	attr_name = "name";
	updtr_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!updtr_name)
		goto einval;

	if (0 == strncmp(LDMSD_FAILOVER_NAME_PREFIX, updtr_name,
			 sizeof(LDMSD_FAILOVER_NAME_PREFIX) - 1)) {
		goto ename;
	}

	attr_name = "regex";
	match->regex_str = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_REGEX);
	if (!match->regex_str)
		goto einval;
	rc = ldmsd_compile_regex(&match->regex, match->regex_str,
				reqc->line_buf, reqc->line_len);
	if (rc)
		goto send_reply;

	mtype = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_MATCH);
	if (!mtype)
		match->selector = LDMSD_NAME_MATCH_INST_NAME;
	else if (0 == strcasecmp(mtype, "schema"))
		match->selector = LDMSD_NAME_MATCH_SCHEMA_NAME;
	else if (0 == strcasecmp(mtype, "inst"))
		match->selector = LDMSD_NAME_MATCH_INST_NAME;
	else {
		(void) linebuf_printf(reqc, "The match value '%s' is invalid.", mtype);
		reqc->errcode = EINVAL;
		goto send_reply;
	}

	ldmsd_req_ctxt_sec_get(reqc, &ctxt->base.sctxt);

	updtr = ldmsd_updtr_find(updtr_name);
	if (!updtr) {
		reqc->errcode = ENOENT;
		(void) linebuf_printf(reqc, "updtr '%s' does not exist.", updtr_name);
		goto send_reply;
	}

	ctxt->base.is_all = 1;
	ref_get(&ctxt->base.ref, "updtr_tree");
	rc = ldmsd_cfgtree_post2cfgobj(&updtr->obj, updtr_tree_w,
					updtr->worker, reqc, &ctxt->base);
	ldmsd_updtr_put(updtr);
	if (rc) {
		ref_put(&ctxt->base.ref, "updtr_tree");
		if (ENOMEM == rc)
			goto enomem;
		reqc->errcode = EINTR;
		(void) linebuf_printf(reqc, "Failed to handle the updtr_prdcr_add command");
		goto send_reply;
	}

	free(updtr_name);
	free(mtype);
	return 0;

enomem:
	reqc->errcode = ENOMEM;
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
	(void) linebuf_printf(reqc, "LDMSD: out of memory.");
	free(ctxt);
	goto send_reply;
ename:
	reqc->errcode = EINVAL;
	(void) linebuf_printf(reqc, "Bad updtr name");
	goto send_reply;
einval:
	reqc->errcode = EINVAL;
	(void) linebuf_printf(reqc,
			"The attribute '%s' is required by %s.",
			attr_name, ldmsd_req_id2str(reqc->req_id));
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	ldmsd_name_match_free(match);
	free(ctxt);
	free(updtr_name);
	free(mtype);
	return 0;
}

static int tree_updtr_prdcr_filter_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	char *updtr_name, *prdcr_regex, *attr_name;
	updtr_name = prdcr_regex = NULL;
	struct updtr_filter_ctxt *ctxt;
	struct ldmsd_name_match *match;
	ldmsd_updtr_t updtr;

	ctxt = calloc(1, sizeof(*ctxt));
	if (!ctxt)
		goto enomem;
	ref_init(&ctxt->base.ref, "create", __updtr_filter_ctxt_destory, ctxt);
	match = calloc(1, sizeof(*match));
	if (!match)
		goto enomem;
	match->is_regex = 1;
	ctxt->match = match;

	reqc->errcode = 0;

	attr_name = "name";
	updtr_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!updtr_name)
		goto einval;

	if (0 == strncmp(LDMSD_FAILOVER_NAME_PREFIX, updtr_name,
			 sizeof(LDMSD_FAILOVER_NAME_PREFIX) - 1)) {
		goto ename;
	}

	attr_name = "regex";
	match->regex_str = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_REGEX);
	if (!match->regex_str)
		goto einval;
	rc = ldmsd_compile_regex(&match->regex, match->regex_str,
				reqc->line_buf, reqc->line_len);
	if (rc)
		goto send_reply;

	ldmsd_req_ctxt_sec_get(reqc, &ctxt->base.sctxt);

	updtr = ldmsd_updtr_find(updtr_name);
	if (!updtr) {
		reqc->errcode = ENOENT;
		(void) linebuf_printf(reqc, "updtr '%s' does not exist.", updtr_name);
		goto send_reply;
	}

	ctxt->base.is_all = 1;
	ref_get(&ctxt->base.ref, "updtr_tree");
	rc = ldmsd_cfgtree_post2cfgobj(&updtr->obj, updtr_tree_w,
					updtr->worker, reqc, &ctxt->base);
	ldmsd_updtr_put(updtr);
	if (rc) {
		ref_put(&ctxt->base.ref, "updtr_tree");
		if (ENOMEM == rc)
			goto enomem;
		reqc->errcode = EINTR;
		(void) linebuf_printf(reqc, "Failed to handle the %s command",
						ldmsd_req_id2str(reqc->req_id));
		goto send_reply;
	}

	free(updtr_name);
	return 0;

enomem:
	reqc->errcode = ENOMEM;
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
	(void) linebuf_printf(reqc, "LDMSD: out of memory.");
	free(ctxt);
	goto send_reply;
ename:
	reqc->errcode = EINVAL;
	(void) linebuf_printf(reqc, "Bad prdcr name");
	goto send_reply;
einval:
	reqc->errcode = EINVAL;
	(void) linebuf_printf(reqc,
			"The attribute '%s' is required by %s.", attr_name,
			"updtr_prdcr_add");
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(updtr_name);
	free(match->regex_str);
	free(match);
	free(ctxt);
	return 0;
}

static int __tree_forward2updtr(ldmsd_req_ctxt_t reqc)
{
	int rc;
	char *name = NULL;
	ldmsd_updtr_t updtr;
	struct ldmsd_cfgobj_cfg_ctxt *ctxt = NULL;

	ctxt = calloc(1, sizeof(*ctxt));
	if (!ctxt)
		goto enomem;
	ref_init(&ctxt->ref, "create", free, ctxt);
	reqc->errcode = 0;

	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!name) {
		reqc->errcode = EINVAL;
		rc = linebuf_printf(reqc,
				"The attribute 'name' is required.");
		goto send_reply;
	}

	ldmsd_req_ctxt_sec_get(reqc, &ctxt->sctxt);

	updtr = ldmsd_updtr_find(name);
	if (!updtr) {
		reqc->errcode = ENOENT;
		rc = linebuf_printf(reqc, "updtr '%s' does not exist.", name);
		goto send_reply;
	}

	ctxt->is_all = 1;
	ref_get(&ctxt->ref, "updtr_tree");
	rc = ldmsd_cfgtree_post2cfgobj(&updtr->obj, updtr_tree_w,
					updtr->worker, reqc, ctxt);
	ldmsd_updtr_put(updtr); /* Put back ldmsd_prdcr_find()'s reference */
	if (rc) {
		ref_put(&ctxt->ref, "updtr_tree");
		if (ENOMEM == rc)
			goto enomem;
		reqc->errcode = EINTR;
		rc = linebuf_printf(reqc,
				"Failed to handle the %s command.",
					ldmsd_req_id2str(reqc->req_id));
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

static inline int tree_updtr_del_handler(ldmsd_req_ctxt_t reqc)
{
	return __tree_forward2updtr(reqc);
}

static inline int tree_updtr_stop_handler(ldmsd_req_ctxt_t reqc)
{
	return __tree_forward2updtr(reqc);
}

static int tree_updtr_start_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	char *updtr_name, *interval_str, *offset_str, *auto_interval;
	updtr_name = interval_str = offset_str = auto_interval = NULL;
	struct updtr_start_ctxt *ctxt;
	ldmsd_updtr_t updtr;

	ctxt = calloc(1, sizeof(*ctxt));
	if (!ctxt)
		goto enomem;

	ref_init(&ctxt->base.ref, "create", free, ctxt);
	if (LDMSD_UPDTR_DEFER_START_REQ == reqc->req_id)
		ctxt->is_deferred = 1;

	reqc->errcode = 0;

	updtr_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!updtr_name) {
		reqc->errcode = EINVAL;
		(void) linebuf_printf(reqc, "The updater name must be specified.");
		goto send_reply;
	}
	interval_str = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_INTERVAL);
	offset_str  = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_OFFSET);
	auto_interval = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_AUTO_INTERVAL);

	ldmsd_req_ctxt_sec_get(reqc, &ctxt->base.sctxt);

	updtr = ldmsd_updtr_find(updtr_name);
	if (!updtr) {
		reqc->errcode = ENOENT;
		(void) linebuf_printf(reqc, "updtr '%s' does not exist.", updtr_name);
		goto send_reply;
	}
	ctxt->base.is_all = 1;
	ctxt->is_auto = (auto_interval)?1:-1;
	ctxt->interval_us = (interval_str)?strtol(interval_str, NULL, 0):-1;
	ctxt->offset_us = (offset_str)?strtol(offset_str, NULL, 0):-1;

	ref_get(&ctxt->base.ref, "updtr_tree");
	rc = ldmsd_cfgtree_post2cfgobj(&updtr->obj, updtr_tree_w,
					updtr->worker, reqc, &ctxt->base);
	ldmsd_updtr_put(updtr);
	if (rc)  {
		ref_put(&ctxt->base.ref, "updtr_tree");
		if (ENOMEM == rc)
			goto enomem;
		reqc->errcode = EINTR;
		(void) linebuf_printf(reqc,
				"Failed to handle the updtr_start command.");
		goto send_reply;
	}

	goto out;

enomem:
	reqc->errcode = ENOMEM;
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
	(void) linebuf_printf(reqc, "LDMSD: out of memory.");
	free(ctxt);

send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
out:
	free(updtr_name);
	free(interval_str);
	free(offset_str);
	return 0;
}

static int tree_updtr_add_handler(ldmsd_req_ctxt_t reqc)
{
	char *name, *offset_str, *interval_str, *push, *auto_interval;
	name = offset_str = interval_str = push = auto_interval = NULL;
	uid_t uid;
	gid_t gid;
	int perm;
	char *perm_s = NULL;
	char *endptr;
	int push_flags, is_auto_task;
	long interval, offset;

	reqc->errcode = 0;

	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!name) {
		reqc->errcode = EINVAL;
		(void) linebuf_printf(reqc, "The attribute 'name' is required.");
		goto send_reply;
	}
	if (0 == strncmp(LDMSD_FAILOVER_NAME_PREFIX, name,
			 sizeof(LDMSD_FAILOVER_NAME_PREFIX)-1)) {
		reqc->errcode = EINVAL;
		(void) linebuf_printf(reqc, "%s is an invalid updtr name", name);
		goto send_reply;
	}

	push = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_PUSH);

	interval_str = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_INTERVAL);
	if (!interval_str) {
		if (!push) {
			reqc->errcode = EINVAL;
			(void) linebuf_printf(reqc, "The 'interval' attribute is required.");
			goto send_reply;
		}
	} else {
		/*
		 * Verify that the given interval value is valid.
		 */
		if ('\0' == interval_str[0]) {
			reqc->errcode = EINVAL;
			(void) linebuf_printf(reqc, "The given update interval "
						  "value is an empty string.");
			goto send_reply;
		}
		interval = strtol(interval_str, &endptr, 0);
		if ('\0' != endptr[0]) {
			reqc->errcode = EINVAL;
			(void) linebuf_printf(reqc, "The given update interval "
						"value (%s) is not a number.",
								interval_str);
			goto send_reply;
		} else {
			if (0 >= interval) {
				reqc->errcode = EINVAL;
				(void) linebuf_printf(reqc,
						"The update interval value must "
						"be larger than 0.");
				goto send_reply;
			}
		}
	}

	offset_str = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_OFFSET);
	if (offset_str) {
		/*
		 * Verify that the given offset value is valid.
		 */
		if ('\0' == offset_str[0]) {
			reqc->errcode = EINVAL;
			(void) linebuf_printf(reqc, "The given update offset "
						 "value is an empty string.");
			goto send_reply;
		}
		offset = strtol(offset_str, &endptr, 0);
		if ('\0' != endptr[0]) {
			reqc->errcode = EINVAL;
			(void) linebuf_printf(reqc, "The given update offset "
					"value (%s) is not a number.", offset_str);
			goto send_reply;
		}
		if (interval_str && (interval < labs(offset) * 2)) {
			reqc->errcode = EINVAL;
			(void) linebuf_printf(reqc,
					"The absolute value of the offset value "
					"must not be larger than the half of "
					"the update interval.");
			goto send_reply;
		}
	}

	auto_interval = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_AUTO_INTERVAL);

	struct ldmsd_sec_ctxt sctxt;
	ldmsd_req_ctxt_sec_get(reqc, &sctxt);
	uid = sctxt.crd.uid;
	gid = sctxt.crd.gid;

	perm = 0770;
	perm_s = ldmsd_req_attr_str_value_get_by_name(reqc, "perm");
	if (perm_s)
		perm = strtoul(perm_s, NULL, 0);

	if (auto_interval) {
		if (0 == strcasecmp(auto_interval, "true")) {
			if (push) {
				reqc->errcode = EINVAL;
				(void) linebuf_printf(reqc,
						"auto_interval and push are "
						"incompatible options");
				goto send_reply;
			}
			is_auto_task = 1;
		} else if (0 == strcasecmp(auto_interval, "false")) {
			is_auto_task = 0;
		} else {
			reqc->errcode = EINVAL;
			(void) linebuf_printf(reqc,
				       "The auto_interval option requires "
				       "either 'true', or 'false'\n");
			goto send_reply;
		}
	} else {
		is_auto_task = 0;
	}
	push_flags = 0;
	if (push) {
		if (0 == strcasecmp(push, "onchange")) {
			push_flags = LDMSD_UPDTR_F_PUSH | LDMSD_UPDTR_F_PUSH_CHANGE;
		} else if (0 == strcasecmp(push, "true") || 0 == strcasecmp(push, "yes")) {
			push_flags = LDMSD_UPDTR_F_PUSH;
		} else {
			reqc->errcode = EINVAL;
			(void) linebuf_printf(reqc,
				       "The valud push options are \"onchange\", \"true\" "
				       "or \"yes\"\n");
			goto send_reply;
		}
		is_auto_task = 0;
	}

	ldmsd_log(LDMSD_LDEBUG, "%s: updtr %s\n", __func__, name);

	ldmsd_updtr_t updtr = ldmsd_updtr_new_with_auth(name, interval_str,
							offset_str ? offset_str : "0",
							push_flags,
							is_auto_task,
							uid, gid, perm);
	if (!updtr) {
		reqc->errcode = errno;
		if (errno == EEXIST) {
			(void) linebuf_printf(reqc, "The updtr %s already exists.", name);
		} else if (errno == ENOMEM) {
			(void) linebuf_printf(reqc, "Out of memory");
		} else {
			if (!reqc->errcode)
				reqc->errcode = EINVAL;
			(void) linebuf_printf(reqc, "The updtr could not be created.");
		}
	}

send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(name);
	free(interval_str);
	free(auto_interval);
	free(offset_str);
	free(push);
	free(perm_s);
	return 0;
}

static int tree_updtr_status_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	char *name;
	ldmsd_updtr_t updtr, nxt_updtr;
	struct ldmsd_cfgobj_cfg_ctxt *ctxt = calloc(1, sizeof(*ctxt));
	if (!ctxt)
		goto enomem;
	ref_init(&ctxt->ref, "create", free, ctxt);
	ldmsd_req_ctxt_sec_get(reqc, &ctxt->sctxt);

	rc = linebuf_printf(reqc, "[");
	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (name) {
		updtr = ldmsd_updtr_find(name);
		if (!updtr) {
			reqc->errcode = ENOENT;
			(void) linebuf_printf(reqc, "updtr '%s' not found.", name);
			goto send_reply;
		} else {
			ref_get(&ctxt->ref, "updtr_tree");
			rc = ldmsd_cfgtree_post2cfgobj(&updtr->obj, updtr_tree_w,
							updtr->worker, reqc, ctxt);
			ldmsd_updtr_put(updtr);
			if (rc) {
				ref_put(&ctxt->ref, "updtr_tree");
				if (ENOMEM == rc)
					goto enomem;
				reqc->errcode = EINTR;
				rc = linebuf_printf(reqc,
						"Failed to handle the updtr_start command.");
				goto send_reply;
			}
			ctxt->is_all = 1;
		}
	} else {
		updtr = ldmsd_updtr_first();
		while (updtr) {
			nxt_updtr = ldmsd_updtr_next(updtr);
			if (!nxt_updtr)
				ctxt->is_all = 1;
			ref_get(&ctxt->ref, "updtr_tree");
			rc = ldmsd_cfgtree_post2cfgobj(&updtr->obj, updtr_tree_w,
							updtr->worker, reqc, ctxt);
			if (rc) {
				ref_put(&ctxt->ref, "updtr_tree");
				if (ENOMEM == rc)
					goto enomem;
				reqc->errcode = EINTR;
				(void) snprintf(reqc->line_buf, reqc->line_len,
						"Failed to handle the updtr_start command.");
				goto send_reply;
			}
			updtr = nxt_updtr;
		}
	}

	goto out;
enomem:
	reqc->errcode = ENOMEM;
	LDMSD_LOG_ENOMEM();
	(void) linebuf_printf(reqc, "LDMSD: out of memory.");
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(ctxt);
out:
	free(name);
	return rc;

}

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

	if (1 < ctxt->num_recv)
		(void) linebuf_printf(reqc, ",");
	(void) linebuf_printf(reqc, "%s", buf->buf);

	if (ldmsd_cfgtree_done(ctxt)) {
		(void) linebuf_printf(reqc, "]");
	}


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
			(void) linebuf_printf(reqc, ",");
		(void)linebuf_printf(reqc, "%s", rsp->errmsg);
	}
	free(rsp->errmsg);
	free(rsp->ctxt);
	free(rsp);
	return 0;
}

static int
tree_filter_rsp_handler(ldmsd_req_ctxt_t reqc,
				struct ldmsd_cfgobj_cfg_rsp *rsp,
				struct ldmsd_cfgobj_cfg_ctxt *ctxt)
{
	struct updtr_filter_ctxt *fctxt = (struct updtr_filter_ctxt *)ctxt;
	ldmsd_name_match_free(fctxt->match);
	return tree_cfg_rsp_handler(reqc, rsp, ctxt);
}

extern int
__post_prdset_state_ev(struct prdset_info *prdset_info,
			    enum ldmsd_prdcr_set_state state,
			    void *obj, ev_worker_t src,
			    ev_worker_t dst, struct timespec *to);
int updtr_tree_prdset_state_actor(ev_worker_t src, ev_worker_t dst,
					ev_status_t status, ev_t e)
{
	if (EV_OK != status)
		return 0;

	int rc = 0;
	ldmsd_updtr_t updtr;
	struct prdset_state_data *prdset_state = EV_DATA(e, struct prdset_state_data);
	struct prdset_info *prdset_info = prdset_state->prdset_info;

	for (updtr = ldmsd_updtr_first(); updtr; updtr = ldmsd_updtr_next(updtr)) {
		ref_get(&prdset_info->ref, "updtr_tree2updtr");
		ldmsd_updtr_get(updtr);
		rc = __post_prdset_state_ev(prdset_info, prdset_state->state,
							 updtr, updtr_tree_w,
							 updtr->worker, NULL);
		if (rc) {
			LDMSD_LOG_ENOMEM();
			ldmsd_updtr_put(updtr);
			ref_put(&prdset_info->ref, "updtr_tree2updtr");
			rc = ENOMEM;
		}
	}
	ref_put(&prdset_info->ref, "prdset2updtr_tree");
	ev_put(e);
	return rc;
}

extern int ldmsd_cfgobj_tree_del_rsp_handler(ldmsd_req_ctxt_t reqc,
				struct ldmsd_cfgobj_cfg_rsp *rsp,
				struct ldmsd_cfgobj_cfg_ctxt *ctxt,
				enum ldmsd_cfgobj_type type);
int updtr_tree_cfg_rsp_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e)
{
	if (EV_OK != status)
		return 0;

	struct ldmsd_cfgobj_cfg_rsp *rsp = EV_DATA(e, struct cfgobj_rsp_data)->rsp;
	struct ldmsd_cfgobj_cfg_ctxt *ctxt = EV_DATA(e, struct cfgobj_rsp_data)->ctxt;
	struct ldmsd_req_ctxt *reqc = ctxt->reqc;
	int rc = 0;

	ctxt->num_recv++;
	if (!rsp)
		goto out;

	switch (reqc->req_id) {
	case LDMSD_UPDTR_DEL_REQ:
		rc = ldmsd_cfgobj_tree_del_rsp_handler(reqc, rsp, ctxt,
						   LDMSD_CFGOBJ_UPDTR);
		break;
	case LDMSD_UPDTR_PRDCR_ADD_REQ:
	case LDMSD_UPDTR_PRDCR_DEL_REQ:
	case LDMSD_UPDTR_MATCH_ADD_REQ:
	case LDMSD_UPDTR_MATCH_DEL_REQ:
		rc = tree_filter_rsp_handler(reqc, rsp, ctxt);
		break;
	case LDMSD_UPDTR_START_REQ:
	case LDMSD_UPDTR_DEFER_START_REQ:
	case LDMSD_UPDTR_STOP_REQ:
		rc = tree_cfg_rsp_handler(reqc, rsp, ctxt);
		break;
	case LDMSD_UPDTR_STATUS_REQ:
		rc = tree_status_rsp_handler(reqc, rsp, ctxt);
		if (ldmsd_cfgtree_done(ctxt)) {
			/* All updaters have sent back the responses */
			ldmsd_send_json_response(reqc, reqc->line_buf);
			ref_put(&ctxt->ref, "create");
		}
		goto out;
	default:
		assert(0 == "impossible case");
		rc = EINTR;
		goto out;
	}

	if (rc) {
		reqc->errcode = EINTR;
		(void) linebuf_printf(reqc, "LDMSD: failed to construct the response");
		rc = 0;
	}

	if (ldmsd_cfgtree_done(ctxt)) {
		/* All updaters have sent back the responses */
		ldmsd_send_req_response(reqc, reqc->line_buf);
		ref_put(&ctxt->ref, "create");
	}

out:
	ref_put(&ctxt->ref, "updtr_tree");
	ev_put(e);
	return rc;
}

int updtr_tree_cfg_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e)
{
	if (EV_OK != status)
		return 0;

	struct ldmsd_req_ctxt *reqc = EV_DATA(e, struct cfg_data)->reqc;
	int rc = 0;

	switch (reqc->req_id) {
	case LDMSD_UPDTR_ADD_REQ:
		rc = tree_updtr_add_handler(reqc);
		break;
	case LDMSD_UPDTR_DEL_REQ:
		rc = tree_updtr_del_handler(reqc);
		break;
	case LDMSD_UPDTR_PRDCR_ADD_REQ:
	case LDMSD_UPDTR_PRDCR_DEL_REQ:
		rc = tree_updtr_prdcr_filter_handler(reqc);
		break;
	case LDMSD_UPDTR_MATCH_ADD_REQ:
	case LDMSD_UPDTR_MATCH_DEL_REQ:
		rc = tree_updtr_match_filter_handler(reqc);
		break;
	case LDMSD_UPDTR_DEFER_START_REQ:
	case LDMSD_UPDTR_START_REQ:
		rc = tree_updtr_start_handler(reqc);
		break;
	case LDMSD_UPDTR_STOP_REQ:
		rc = tree_updtr_stop_handler(reqc);
		break;
	case LDMSD_UPDTR_STATUS_REQ:
		rc = tree_updtr_status_handler(reqc);
		break;
	default:
		ldmsd_log(LDMSD_LERROR, "%s not support req_id %d\n",
						__func__, reqc->req_id);
		rc = ENOTSUP;
		goto out;
	}

out:
	ev_put(e);
	return rc;
}
