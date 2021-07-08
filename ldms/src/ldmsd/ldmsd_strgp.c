/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015-2016,2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2015-2016,2018 Open Grid Computing, Inc. All rights reserved.
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
#include <string.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <time.h>
#include <ctype.h>
#include <coll/rbt.h>
#include <ovis_util/util.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldms_xprt.h"
#include "ldmsd_event.h"
#include "ldmsd_cfgobj.h"
#include "config.h"

struct strgp_start_ctxt {
	struct ldmsd_cfgobj_cfg_ctxt base;
	int is_deferred;
};

struct strgp_prdcr_ctxt {
	struct ldmsd_cfgobj_cfg_ctxt base;
	struct ldmsd_name_match *prdcr_match;
};

struct strgp_metric_ctxt {
	struct ldmsd_cfgobj_cfg_ctxt base;
	struct ldmsd_strgp_metric *metric_filt;
};

struct strgp_cleanup_ctxt {
	int is_all;
	int num_sent;
	int num_recv;
	proc_exit_fn exit_fn;
	void *exit_args;
};

extern void ldmsd_req_ctxt_sec_get(ldmsd_req_ctxt_t rctxt, ldmsd_sec_ctxt_t sctxt);

void ldmsd_strgp___del(ldmsd_cfgobj_t obj)
{
	ldmsd_strgp_t strgp = (ldmsd_strgp_t)obj;

	if (strgp->schema)
		free(strgp->schema);
	if (strgp->container)
		free(strgp->container);
	if (strgp->metric_arry)
		free(strgp->metric_arry);

	struct ldmsd_strgp_metric *metric;
	while (!TAILQ_EMPTY(&strgp->metric_list) ) {
		metric = TAILQ_FIRST(&strgp->metric_list);
		if (metric->name)
			free(metric->name);
		TAILQ_REMOVE(&strgp->metric_list, metric, entry);
		free(metric);
	}
	ldmsd_match_queue_free(&strgp->prdcr_list);
	if (strgp->plugin_name)
		free(strgp->plugin_name);
	ldmsd_cfgobj___del(obj);
}

void ldmsd_timespec_diff(struct timespec *start, struct timespec *stop,
			 struct timespec *result)
{
	if ((stop->tv_nsec - start->tv_nsec) < 0) {
		result->tv_sec = stop->tv_sec - start->tv_sec - 1;
		result->tv_nsec = stop->tv_nsec - start->tv_nsec + 1000000000;
	} else {
		result->tv_sec = stop->tv_sec - start->tv_sec;
		result->tv_nsec = stop->tv_nsec - start->tv_nsec;
	}
}

int ldmsd_timespec_cmp(struct timespec *a, struct timespec *b)
{
	if (a->tv_sec < b->tv_sec)
		return -1;
	if (a->tv_sec > b->tv_sec)
		return 1;
	if (a->tv_nsec < b->tv_nsec)
		return -1;
	if (a->tv_nsec > b->tv_nsec)
		return 1;
	return 0;
}

void ldmsd_timespec_add(struct timespec *a, struct timespec *b, struct timespec *result)
{
	result->tv_sec = a->tv_sec + b->tv_sec;
	if (a->tv_nsec + b->tv_nsec < 1000000000) {
		result->tv_nsec = a->tv_nsec + b->tv_nsec;
	} else {
		result->tv_sec += 1;
		result->tv_nsec = a->tv_nsec + b->tv_nsec - 1000000000;
	}
}

int ldmsd_timespec_from_str(struct timespec *result, const char *str)
{
	int rc = 0;
	long long pos, nanoseconds;
	char *units;
	char *input = strdup(str);
	double term;
	assert(input);
	for (pos = 0; input[pos] != '\0'; pos++) {
		if (!isdigit(input[pos]) && input[pos] != '.')
			break;
	}
	if (input[pos] == '\0') {
		/* for backward compatability, raw values are considered microseconds */
		units = strdup("us");
	} else {
		units = strdup(&input[pos]);
		input[pos] = '\0';
	}
	assert(units);
	term = strtod(input, NULL);
	if (0 == strcasecmp(units, "s")) {
		nanoseconds = term * 1000000000;
	} else if (0 == strcasecmp(units, "ms")) {
		nanoseconds = term * 1000000;
	} else if (0 == strcasecmp(units, "us")) {
		nanoseconds = term * 1000;
	} else if (0 == strcasecmp(units, "ns")) {
		nanoseconds = term;
	} else {
		rc = EINVAL;
		goto out;
	}
	result->tv_sec = nanoseconds / 1000000000;
	result->tv_nsec = nanoseconds % 1000000000;
out:
	free(input);
	free(units);
	return rc;
}

static void strgp_store(ldmsd_strgp_t strgp, ldms_set_t set)
{
	if (strgp->state != LDMSD_STRGP_STATE_RUNNING)
		return;
	if (!strgp->store_handle) {
		strgp->state = LDMSD_STRGP_STATE_STOPPED;
		return;
	}
	strgp->store->store(strgp->store_handle, set,
			    strgp->metric_arry, strgp->metric_count);
	if (strgp->flush_interval.tv_sec || strgp->flush_interval.tv_nsec) {
		struct timespec expiry;
		struct timespec now;
		ldmsd_timespec_add(&strgp->last_flush, &strgp->flush_interval, &expiry);
		clock_gettime(CLOCK_REALTIME, &now);
		if (ldmsd_timespec_cmp(&now, &expiry) >= 0) {
			clock_gettime(CLOCK_REALTIME, &strgp->last_flush);
			strgp->store->flush(strgp->store_handle);
		}
	}
}

static void strgp_update_fn(ldmsd_strgp_t strgp, ldmsd_prdcr_set_t prd_set)
{
	strgp_store(strgp, prd_set->set);
}

ldmsd_strgp_t
ldmsd_strgp_new_with_auth(const char *name, uid_t uid, gid_t gid, int perm)
{
	struct ldmsd_strgp *strgp;

	strgp = (struct ldmsd_strgp *)
		ldmsd_cfgobj_new_with_auth(name, LDMSD_CFGOBJ_STRGP,
				 sizeof *strgp, ldmsd_strgp___del,
				 uid, gid, perm);
	if (!strgp)
		return NULL;

	strgp->state = LDMSD_STRGP_STATE_STOPPED;
	strgp->flush_interval.tv_sec = 0;
	strgp->flush_interval.tv_nsec = 0;
	strgp->last_flush.tv_sec = 0;
	strgp->last_flush.tv_nsec = 0;
	strgp->update_fn = strgp_update_fn;
	TAILQ_INIT(&strgp->prdcr_list);
	TAILQ_INIT(&strgp->metric_list);
	strgp->worker = assign_strgp_worker();
	ldmsd_cfgobj_unlock(&strgp->obj);
	return strgp;
}

ldmsd_strgp_t
ldmsd_strgp_new(const char *name)
{
	struct ldmsd_sec_ctxt sctxt;
	ldmsd_sec_ctxt_get(&sctxt);
	return ldmsd_strgp_new_with_auth(name, sctxt.crd.uid, sctxt.crd.gid, 0777);
}

ldmsd_strgp_t ldmsd_strgp_first()
{
	return (ldmsd_strgp_t)ldmsd_cfgobj_first(LDMSD_CFGOBJ_STRGP);
}

ldmsd_strgp_t ldmsd_strgp_next(struct ldmsd_strgp *strgp)
{
	return (ldmsd_strgp_t)ldmsd_cfgobj_next(&strgp->obj);
}

ldmsd_strgp_metric_t ldmsd_strgp_metric_first(ldmsd_strgp_t strgp)
{
	return TAILQ_FIRST(&strgp->metric_list);
}

ldmsd_strgp_metric_t ldmsd_strgp_metric_next(ldmsd_strgp_metric_t metric)
{
	return TAILQ_NEXT(metric, entry);
}

ldmsd_name_match_t ldmsd_strgp_prdcr_first(ldmsd_strgp_t strgp)
{
	return TAILQ_FIRST(&strgp->prdcr_list);
}

ldmsd_name_match_t ldmsd_strgp_prdcr_next(ldmsd_name_match_t match)
{
	return TAILQ_NEXT(match, entry);
}

time_t convert_rotate_str(const char *rotate)
{
	char *units;
	long rotate_interval;

	rotate_interval = strtol(rotate, &units, 0);
	if (rotate_interval <= 0 || *units == '\0')
		return 0;

	switch (*units) {
	case 'm':
	case 'M':
		return rotate_interval * 60;
	case 'h':
	case 'H':
		return rotate_interval * 60 * 60;
	case 'd':
	case 'D':
		return rotate_interval * 24 * 60 * 60;
	}
	return 0;
}

ldmsd_name_match_t strgp_find_prdcr_ex(ldmsd_strgp_t strgp, const char *ex)
{
	ldmsd_name_match_t match;
	TAILQ_FOREACH(match, &strgp->prdcr_list, entry) {
		if (0 == strcmp(match->regex_str, ex))
			return match;
	}
	return NULL;
}

ldmsd_strgp_metric_t strgp_metric_find(ldmsd_strgp_t strgp, const char *name)
{
	ldmsd_strgp_metric_t metric;
	TAILQ_FOREACH(metric, &strgp->metric_list, entry)
		if (0 == strcmp(name, metric->name))
			return metric;
	return NULL;
}

ldmsd_strgp_metric_t strgp_metric_new(const char *metric_name)
{
	ldmsd_strgp_metric_t metric = calloc(1, sizeof *metric);
	if (metric) {
		metric->name = strdup(metric_name);
		if (!metric->name) {
			free(metric);
			metric = NULL;
		}
	}
	return metric;
}

static void strgp_close(ldmsd_strgp_t strgp)
{
	if (strgp->store) {
		if (strgp->store_handle)
			ldmsd_store_close(strgp->store, strgp->store_handle);
		if (strgp->next_store_handle)
			ldmsd_store_close(strgp->store, strgp->next_store_handle);
	}
	strgp->store_handle = NULL;
	strgp->next_store_handle = NULL;
}

static int strgp_open(ldmsd_strgp_t strgp, ldms_set_t set)
{
	int i, idx, rc;
	const char *name;
	ldmsd_strgp_metric_t metric;
	struct ldmsd_plugin_cfg *store;

	if (!set)
		return ENOENT;

	if (!strgp->store) {
		store = ldmsd_get_plugin(strgp->plugin_name);
		if (!store)
			return ENOENT;
		strgp->store = store->store;
	}
	/* Build metric list from the schema in the producer set */
	strgp->metric_count = 0;
	strgp->metric_arry = calloc(ldms_set_card_get(set), sizeof(int));
	if (!strgp->metric_arry)
		return ENOMEM;

	rc = ENOMEM;
	if (TAILQ_EMPTY(&strgp->metric_list)) {
		/* No metric list was given. Add all metrics in the set */
		for (i = 0; i < ldms_set_card_get(set); i++) {
			name = ldms_metric_name_get(set, i);
			metric = strgp_metric_new(name);
			if (!metric)
				goto err;
			TAILQ_INSERT_TAIL(&strgp->metric_list, metric, entry);
		}
	}
	rc = ENOENT;
	for (i = 0, metric = ldmsd_strgp_metric_first(strgp);
	     metric; metric = ldmsd_strgp_metric_next(metric), i++) {
		name = metric->name;
		idx = ldms_metric_by_name(set, name);
		if (idx < 0)
			goto err;
		metric->type = ldms_metric_type_get(set, idx);
		strgp->metric_arry[i] = idx;
	}
	strgp->metric_count = i;

	strgp->store_handle = ldmsd_store_open(strgp->store, strgp->container,
			strgp->schema, &strgp->metric_list, strgp);
	rc = EINVAL;
	if (!strgp->store_handle)
		goto err;
	return 0;
err:
	free(strgp->metric_arry);
	strgp->metric_arry = NULL;
	strgp->metric_count = 0;
	return rc;
}

static void strgp_running(ldmsd_strgp_t strgp, ldms_set_t set)
{
	int rc;

	if (!strgp->store_handle) {
		rc = strgp_open(strgp, set);
		if (rc) {
			ldmsd_log(LDMSD_LERROR, "Failed to open strgp '%s' "
					"with error %d\n", strgp->obj.name, rc);
			return;
		}
	}
	strgp_store(strgp, set);
}

int strgp_prdset_state_actor(ev_worker_t src, ev_worker_t dst,
					ev_status_t status, ev_t e)
{
	if (EV_OK != status)
		goto out;

	int rc;
	struct ldmsd_name_match *prdcr_filt;
	struct prdset_info *prdset_info;
	ldmsd_strgp_t strgp;
	enum ldmsd_prdcr_set_state state;

	prdset_info = EV_DATA(e, struct prdset_state_data)->prdset_info;
	strgp = (ldmsd_strgp_t)EV_DATA(e, struct prdset_state_data)->obj;
	state = EV_DATA(e, struct prdset_state_data)->state;

	ldmsd_log(LDMSD_LINFO, "strgp '%s' received prdset_state from prdset "
					"'%s' with state %d\n", strgp->obj.name,
						 prdset_info->inst_name, state);

	if (LDMSD_STRGP_STATE_RUNNING != strgp->state)
		goto out;

	rc = strcmp(prdset_info->schema_name, strgp->schema);
	if (rc) {
		/* schema not matched */
		goto out;
	}

	if (TAILQ_EMPTY(&strgp->prdcr_list)) {
		strgp_running(strgp, prdset_info->set_snapshot);
	} else {
		TAILQ_FOREACH(prdcr_filt, &strgp->prdcr_list, entry) {
			rc = regexec(&prdcr_filt->regex,  prdset_info->prdcr_name,
								    0, NULL, 0);
			if (0 == rc) {
				strgp_running(strgp, prdset_info->set_snapshot);
				break;
			}
		}
	}

out:
	ldmsd_strgp_put(strgp);
	ref_put(&prdset_info->ref, "strgp_tree2strgp");
	ev_put(e);
	return 0;
}

int strgp_status_prdcr_filter_req_actor(ev_worker_t src, ev_worker_t dst,
						     ev_status_t status, ev_t e)
{
	if (EV_OK != status)
		return 0;

	int rc;
	size_t cnt = 0;
	ev_t rsp_ev;
	struct ldmsd_cfgobj_cfg_rsp *rsp = EV_DATA(e, struct prdcr_filter_req_data)->rsp;
	struct ldmsd_match_queue *prdcr_list = &EV_DATA(e, struct prdcr_filter_req_data)->prdcr_list;
	struct ldmsd_name_match *ent;
	struct ldmsd_msg_buf *buf = rsp->ctxt;
	ev_worker_t strgp_w = EV_DATA(e, struct prdcr_filter_req_data)->resp_worker;
	void *ctxt = EV_DATA(e, struct prdcr_filter_req_data)->cfg_ctxt;

	if (rsp->errcode && (rsp->errcode != EBUSY)) {
		/*
		 * Fail to get the producer list
		 */
		goto out;
	}

	TAILQ_FOREACH(ent, prdcr_list, entry) {
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
	rsp_ev = ev_new(cfgobj_rsp_type);
	if (!rsp_ev)
		goto enomem;
	EV_DATA(rsp_ev, struct cfgobj_rsp_data)->ctxt = ctxt;
	EV_DATA(rsp_ev, struct cfgobj_rsp_data)->rsp = rsp;
	rc = ev_post(strgp_w, strgp_tree_w, rsp_ev, 0);
	if (rc)
		goto err;

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

static struct ldmsd_msg_buf *__strgp_status_json_obj(ldmsd_strgp_t strgp)
{
	int count;
	size_t cnt;
	struct ldmsd_name_match *prdcr_filt;
	ldmsd_strgp_metric_t metric;
	struct ldmsd_msg_buf *buf = ldmsd_msg_buf_new(1024);
	if (!buf)
		return NULL;

	cnt = ldmsd_msg_buf_append(buf,
		       "{\"name\":\"%s\","
		       "\"container\":\"%s\","
		       "\"schema\":\"%s\","
		       "\"plugin\":\"%s\","
		       "\"flush\":\"%ld.%06ld\","
		       "\"state\":\"%s\","
		       "\"producers\":[",
		       strgp->obj.name,
		       strgp->container,
		       strgp->schema,
		       strgp->plugin_name,
		       strgp->flush_interval.tv_sec,
		       strgp->flush_interval.tv_nsec,
		       ldmsd_strgp_state_str(strgp->state));
	if (cnt < 0) {
		LDMSD_LOG_ENOMEM();
		goto enomem;
	}

	count = 0;
	TAILQ_FOREACH(prdcr_filt, &strgp->prdcr_list, entry) {
		if (count) {
			cnt = ldmsd_msg_buf_append(buf, ",");
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
					"\"metrics\":[");
	if (cnt < 0) {
		LDMSD_LOG_ENOMEM();
		goto enomem;
	}

	count = 0;
	for (metric = ldmsd_strgp_metric_first(strgp); metric;
	     metric = ldmsd_strgp_metric_next(metric)) {
		if (count) {
			cnt = ldmsd_msg_buf_append(buf, ",");
			if (cnt < 0) {
				LDMSD_LOG_ENOMEM();
				goto enomem;
			}
		}
		count++;
		cnt = ldmsd_msg_buf_append(buf, "\"%s\"", metric->name);
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
	return buf;
enomem:
	ldmsd_msg_buf_free(buf);
	return NULL;
}

static struct ldmsd_cfgobj_cfg_rsp *
strgp_status_handler(ldmsd_strgp_t strgp, void *cfg_ctxt)
{
	struct ldmsd_cfgobj_cfg_rsp *rsp;

	rsp = calloc(1, sizeof(*rsp));
	if (!rsp) {
		LDMSD_LOG_ENOMEM();
		return NULL;
	}
	rsp->errcode = 0;
	rsp->ctxt = __strgp_status_json_obj(strgp);
	if (!rsp->ctxt)
		goto err;
	ldmsd_strgp_get(strgp);

	return rsp;
err:
	if (rsp && rsp->ctxt)
		ldmsd_msg_buf_free((struct ldmsd_msg_buf *)rsp->ctxt);
	free(rsp);
	return NULL;
}

static struct ldmsd_cfgobj_cfg_rsp *
strgp_metric_filter_handler(ldmsd_strgp_t strgp, struct ldmsd_cfgobj_cfg_ctxt *cfg_ctxt,
								uint32_t req_id)
{
	int rc = 0;
	struct strgp_metric_ctxt *ctxt = (struct strgp_metric_ctxt *)cfg_ctxt;
	struct ldmsd_cfgobj_cfg_rsp *rsp;
	struct ldmsd_strgp_metric *m;

	rsp = calloc(1, sizeof(*rsp));
	if (!rsp)
		goto enomem;

	rc = ldmsd_cfgobj_access_check(&strgp->obj, 0222, &ctxt->base.sctxt);
	if (rc) {
		rsp->errcode = EPERM;
		rc = asprintf(&rsp->errmsg, "strgp '%s' permission denied.",
							   strgp->obj.name);
		if (rc < 0)
			goto enomem;
		else
			goto out;
	}
	if (strgp->state != LDMSD_STRGP_STATE_STOPPED) {
		rsp->errcode = EBUSY;
		rc = asprintf(&rsp->errmsg, "Configuration changes cannot be made "
				"while the strgp is running");
		if (rc < 0)
			goto enomem;
		else
			goto out;
	}

	m = strgp_metric_find(strgp, ctxt->metric_filt->name);
	if (LDMSD_STRGP_METRIC_ADD_REQ == req_id) {
		if (!m) {
			m = strgp_metric_new(ctxt->metric_filt->name);
			if (!m)
				goto enomem;
			TAILQ_INSERT_TAIL(&strgp->metric_list, m, entry);
		}

	} else {
		if (m)
			TAILQ_REMOVE(&strgp->metric_list, m, entry);
		free(m->name);
		free(m);
	}
out:
	return rsp;
enomem:
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
	free(rsp);
	return NULL;
}

static struct ldmsd_cfgobj_cfg_rsp *
strgp_prdcr_filter_handler(ldmsd_strgp_t strgp, struct ldmsd_cfgobj_cfg_ctxt *cfg_ctxt,
								uint32_t req_id)
{
	int rc = 0;
	struct strgp_prdcr_ctxt *ctxt = (struct strgp_prdcr_ctxt *)cfg_ctxt;
	struct ldmsd_cfgobj_cfg_rsp *rsp;
	struct ldmsd_name_match *m;

	rsp = calloc(1, sizeof(*rsp));
	if (!rsp)
		goto enomem;

	rc = ldmsd_cfgobj_access_check(&strgp->obj, 0222, &ctxt->base.sctxt);
	if (rc) {
		rsp->errcode = EPERM;
		rc = asprintf(&rsp->errmsg, "strgp '%s' permission denied.",
							   strgp->obj.name);
		if (rc < 0)
			goto enomem;
		else
			goto out;
	}
	if (strgp->state != LDMSD_STRGP_STATE_STOPPED) {
		rsp->errcode = EBUSY;
		rc = asprintf(&rsp->errmsg, "Configuration changes cannot be made "
				"while the strgp is running");
		if (rc < 0)
			goto enomem;
		else
			goto out;
	}

	m = strgp_find_prdcr_ex(strgp, ctxt->prdcr_match->regex_str);
	if (LDMSD_STRGP_PRDCR_ADD_REQ == req_id) {
		if (!m) {
			m = ldmsd_name_match_copy(ctxt->prdcr_match);
			if (!m)
				goto enomem;
		}
		TAILQ_INSERT_TAIL(&strgp->prdcr_list, m, entry);
	} else {
		if (m) {
			TAILQ_REMOVE(&strgp->prdcr_list, m, entry);
			ldmsd_name_match_free(m);
		}
	}

out:
	return rsp;
enomem:
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
	free(rsp);
	return NULL;
}

static struct ldmsd_cfgobj_cfg_rsp *
strgp_stop_handler(ldmsd_strgp_t strgp, void *cfg_ctxt)
{
	struct ldmsd_cfgobj_cfg_ctxt *ctxt = (struct ldmsd_cfgobj_cfg_ctxt *)cfg_ctxt;
	struct ldmsd_cfgobj_cfg_rsp *rsp;
	int rc;

	rsp = calloc(1, sizeof(*rsp));
	if (!rsp)
		goto enomem;

	rc = ldmsd_cfgobj_access_check(&strgp->obj, 0222, &ctxt->sctxt);
	if (rc) {
		rsp->errcode = EPERM;
		rc = asprintf(&rsp->errmsg, "prdcr '%s' permission denied.",
							strgp->obj.name);
		if (rc < 0)
			goto enomem;
		else
			goto out;
	}
	if (strgp->state != LDMSD_STRGP_STATE_RUNNING) {
		rc = EBUSY;
		goto out;
	}

	strgp_close(strgp);
	strgp->state = LDMSD_STRGP_STATE_STOPPED;
	strgp->obj.perm &= ~LDMSD_PERM_DSTART;

out:
	return rsp;
enomem:
	LDMSD_LOG_ENOMEM();
	free(rsp);
	return NULL;
}

static struct ldmsd_cfgobj_cfg_rsp *
strgp_del_handler(ldmsd_strgp_t strgp, struct ldmsd_cfgobj_cfg_ctxt *cfg_ctxt)
{
	int rc;
	struct ldmsd_cfgobj_cfg_ctxt *ctxt = (struct ldmsd_cfgobj_cfg_ctxt *)cfg_ctxt;
	struct ldmsd_cfgobj_cfg_rsp *rsp = calloc(1, sizeof(*rsp));
	if (!rsp) {
		LDMSD_LOG_ENOMEM();
		goto enomem;
	}

	rsp->errcode = ldmsd_cfgobj_access_check(&strgp->obj, 0222, &ctxt->sctxt);
	if (rsp->errcode) {
		rc = asprintf(&rsp->errmsg, "strgp '%s' permission denied.",
							  strgp->obj.name);
		if (rc < 0) {
			LDMSD_LOG_ENOMEM();
			goto enomem;
		} else {
			goto out;
		}
	}

	if (strgp->state != LDMSD_STRGP_STATE_STOPPED) {
		rsp->errcode = EBUSY;
		rc = asprintf(&rsp->errmsg, "strgp '%s' is in use.", strgp->obj.name);
		if (rc < 0) {
			LDMSD_LOG_ENOMEM();
			goto enomem;
		} else {
			goto out;
		}
	}

	if (ldmsd_cfgobj_refcount(&strgp->obj) > 2) {
		rsp->errcode = EBUSY;
		rc = asprintf(&rsp->errmsg, "strgp '%s' is in use.", strgp->obj.name);
		if (rc < 0) {
			LDMSD_LOG_ENOMEM();
			goto enomem;
		} else {
			goto out;
		}
	}
	rsp->ctxt = ldmsd_strgp_get(strgp);
out:
	return rsp;

enomem:
	free(rsp);
	return NULL;
}

static struct ldmsd_cfgobj_cfg_rsp *
strgp_start_handler(ldmsd_strgp_t strgp, struct ldmsd_cfgobj_cfg_ctxt *cfg_ctxt)
{
	int rc = 0;
	struct strgp_start_ctxt *ctxt = (struct strgp_start_ctxt *)cfg_ctxt;
	struct ldmsd_cfgobj_cfg_rsp *rsp;

	ldmsd_log(LDMSD_LINFO, "STRGP '%s': received strgp_start\n", strgp->obj.name);
	rsp = calloc(1, sizeof(*rsp));
	if (!rsp) {
		LDMSD_LOG_ENOMEM();
		goto enomem;
	}
	rc = ldmsd_cfgobj_access_check(&strgp->obj, 0222, &ctxt->base.sctxt);
	if (rc) {
		rsp->errcode = EPERM;
		rc = asprintf(&rsp->errmsg, "strgp '%s' permission denied.",
							strgp->obj.name);
		if (rc < 0)
			goto enomem;
		else
			goto out;
	}

	if (strgp->state != LDMSD_STRGP_STATE_STOPPED) {
		rsp->errcode = EBUSY;
		rc = asprintf(&rsp->errmsg, "strgp '%s' is already running.",
								strgp->obj.name);
		if (rc < 0)
			goto enomem;
		else
			goto out;
	}

	strgp->obj.perm |= LDMSD_PERM_DSTART;
	if (ctxt->is_deferred)
		goto out;
	strgp->state = LDMSD_STRGP_STATE_RUNNING;
	/*
	 * Do need to tell anyone that it is RUNNING.
	 * It will pick up producer sets when it receives a prdset_state event.
	 */
out:
	return rsp;
enomem:
	free(rsp);
	return NULL;
}

int strgp_cfg_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e)
{
	if (EV_OK != status)
		return 0;

	int rc;
	struct ldmsd_cfgobj_cfg_rsp *rsp;
	ev_t rsp_ev;

	ldmsd_strgp_t strgp = (ldmsd_strgp_t)EV_DATA(e, struct cfgobj_data)->obj;
	struct ldmsd_cfgobj_cfg_ctxt *cfg_ctxt = EV_DATA(e, struct cfgobj_data)->ctxt;
	ldmsd_req_ctxt_t reqc = cfg_ctxt->reqc;

	switch (reqc->req_id) {
	case LDMSD_STRGP_PRDCR_ADD_REQ:
	case LDMSD_STRGP_PRDCR_DEL_REQ:
		rsp = strgp_prdcr_filter_handler(strgp, cfg_ctxt, reqc->req_id);
		break;
	case LDMSD_STRGP_METRIC_ADD_REQ:
	case LDMSD_STRGP_METRIC_DEL_REQ:
		rsp = strgp_metric_filter_handler(strgp, cfg_ctxt, reqc->req_id);
		break;
	case LDMSD_STRGP_START_REQ:
	case LDMSD_STRGP_DEFER_START_REQ:
		rsp = strgp_start_handler(strgp, cfg_ctxt);
		break;
	case LDMSD_STRGP_DEL_REQ:
		rsp = strgp_del_handler(strgp, cfg_ctxt);
		break;
	case LDMSD_STRGP_STOP_REQ:
		rsp = strgp_stop_handler(strgp, cfg_ctxt);
		break;
	case LDMSD_STRGP_STATUS_REQ:
		rsp = strgp_status_handler(strgp, cfg_ctxt);
		break;
	default:
		ldmsd_log(LDMSD_LERROR, "%s received an unsupported request ID %d.\n",
							__func__, reqc->req_id);
		assert(0);
		rc = EINTR;
		goto err;
	}

	if (!rsp)
		goto enomem;

	if (EBUSY == rsp->errcode) {
		/* Response is not ready. */
		goto out;
	}

	rsp_ev = ev_new(cfgobj_rsp_type);
	if (!rsp_ev)
		goto enomem;
	EV_DATA(rsp_ev, struct cfgobj_rsp_data)->ctxt = cfg_ctxt;
	EV_DATA(rsp_ev, struct cfgobj_rsp_data)->rsp = rsp;
	ev_post(strgp->worker, strgp_tree_w, rsp_ev, 0);
	goto out;

enomem:
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
	rc = ENOMEM;
err:
	free(rsp);
out:
	ldmsd_strgp_put(strgp);
	ev_put(e);

	return rc;
}

static int tree_strgp_metric_filter_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	char *strgp_name, *metric_name, *attr_name;
	strgp_name = metric_name = NULL;
	struct strgp_metric_ctxt *ctxt = NULL;
	struct ldmsd_strgp_metric *metric = NULL;
	ldmsd_strgp_t strgp;

	ctxt = calloc(1, sizeof(*ctxt));
	if (!ctxt)
		goto enomem;
	ref_init(&ctxt->base.ref, "create", free, ctxt);
	reqc->errcode = 0;

	attr_name = "name";
	strgp_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!strgp_name)
		goto einval;

	if (0 == strncmp(LDMSD_FAILOVER_NAME_PREFIX, strgp_name,
			 sizeof(LDMSD_FAILOVER_NAME_PREFIX) - 1)) {
		goto ename;
	}

	attr_name = "metric";
	metric_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_METRIC);
	if (!metric_name)
		goto einval;

	metric = strgp_metric_new(metric_name);
	if (!metric)
		goto enomem;

	ctxt->metric_filt = metric;
	ldmsd_req_ctxt_sec_get(reqc, &ctxt->base.sctxt);

	strgp = ldmsd_strgp_find(strgp_name);
	if (!strgp) {
		reqc->errcode = ENOENT;
		(void) linebuf_printf(reqc, "strgp '%s' does not exist.", strgp_name);
		goto send_reply;
	}

	ctxt->base.is_all = 1;
	ref_get(&ctxt->base.ref, "strgp_tree");
	rc = ldmsd_cfgtree_post2cfgobj(&strgp->obj, strgp_tree_w,
					strgp->worker, reqc, &ctxt->base);
	ldmsd_strgp_put(strgp);
	if (rc) {
		ref_put(&ctxt->base.ref, "strgp_tree");
		if (ENOMEM == rc)
			goto enomem;
		reqc->errcode = EINTR;
		(void) linebuf_printf(reqc, "Failed to handle the %s command",
						ldmsd_req_id2str(reqc->req_id));
		goto send_reply;
	}

	free(strgp_name);
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
			"strgp_prdcr_add");
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(strgp_name);
	free(metric_name);
	if (metric) {
		free(metric->name);
		free(metric);
	}
	free(ctxt);
	return 0;
}

static int tree_strgp_prdcr_filter_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	char *strgp_name, *prdcr_regex, *attr_name;
	strgp_name = prdcr_regex = NULL;
	struct strgp_prdcr_ctxt *ctxt;
	struct ldmsd_name_match *match;
	ldmsd_strgp_t strgp;

	ctxt = calloc(1, sizeof(*ctxt));
	if (!ctxt)
		goto enomem;
	ref_init(&ctxt->base.ref, "create", free, ctxt);
	match = malloc(sizeof(struct ldmsd_name_match));
	if (!match)
		goto enomem;
	ctxt->prdcr_match = match;

	reqc->errcode = 0;

	attr_name = "name";
	strgp_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!strgp_name)
		goto einval;

	if (0 == strncmp(LDMSD_FAILOVER_NAME_PREFIX, strgp_name,
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

	strgp = ldmsd_strgp_find(strgp_name);
	if (!strgp) {
		reqc->errcode = ENOENT;
		(void) linebuf_printf(reqc, "strgp '%s' does not exist.", strgp_name);
		goto send_reply;
	}

	ctxt->base.is_all = 1;
	ref_get(&ctxt->base.ref, "strgp_tree");
	rc = ldmsd_cfgtree_post2cfgobj(&strgp->obj, strgp_tree_w,
					strgp->worker, reqc, &ctxt->base);
	ldmsd_strgp_put(strgp);
	if (rc) {
		ref_put(&ctxt->base.ref, "strgp_tree");
		if (ENOMEM == rc)
			goto enomem;
		reqc->errcode = EINTR;
		(void) linebuf_printf(reqc, "Failed to handle the %s command",
						ldmsd_req_id2str(reqc->req_id));
		goto send_reply;
	}

	free(strgp_name);
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
			"strgp_prdcr_add");
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(strgp_name);
	free(match->regex_str);
	free(match);
	free(ctxt);
	return 0;
}

static int tree_strgp_start_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	char *strgp_name = NULL;
	struct strgp_start_ctxt *ctxt;
	ldmsd_strgp_t strgp;

	ctxt = calloc(1, sizeof(*ctxt));
	if (!ctxt)
		goto enomem;
	ref_init(&ctxt->base.ref, "create", free, ctxt);
	if (LDMSD_STRGP_DEFER_START_REQ == reqc->req_id)
		ctxt->is_deferred = 1;

	reqc->errcode = 0;

	strgp_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!strgp_name) {
		reqc->errcode = EINVAL;
		(void) linebuf_printf(reqc, "The updater name must be specified.");
		goto send_reply;
	}
	ldmsd_req_ctxt_sec_get(reqc, &ctxt->base.sctxt);

	strgp = ldmsd_strgp_find(strgp_name);
	if (!strgp) {
		reqc->errcode = ENOENT;
		(void) linebuf_printf(reqc, "strgp '%s' does not exist.", strgp_name);
		goto send_reply;
	}
	ctxt->base.is_all = 1;
	ref_get(&ctxt->base.ref, "strgp_tree");
	rc = ldmsd_cfgtree_post2cfgobj(&strgp->obj, strgp_tree_w,
					strgp->worker, reqc, &ctxt->base);
	ldmsd_strgp_put(strgp);
	if (rc)  {
		ref_put(&ctxt->base.ref, "strgp_tree");
		if (ENOMEM == rc)
			goto enomem;
		reqc->errcode = EINTR;
		(void) linebuf_printf(reqc,
				"Failed to handle the strgp_start command.");
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
	free(strgp_name);
	return 0;
}

static int __tree_forward2strgp(ldmsd_req_ctxt_t reqc)
{
	int rc;
	char *name = NULL;
	ldmsd_strgp_t strgp;
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

	strgp = ldmsd_strgp_find(name);
	if (!strgp) {
		reqc->errcode = ENOENT;
		rc = linebuf_printf(reqc, "strgp '%s' does not exist.", name);
		goto send_reply;
	}

	ctxt->is_all = 1;
	ref_get(&ctxt->ref, "strgp_tree");
	rc = ldmsd_cfgtree_post2cfgobj(&strgp->obj, strgp_tree_w,
					strgp->worker, reqc, ctxt);
	ldmsd_strgp_put(strgp); /* Put back ldmsd_prdcr_find()'s reference */
	if (rc) {
		ref_put(&ctxt->ref, "strgp_tree");
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

static inline int tree_strgp_stop_handler(ldmsd_req_ctxt_t reqc)
{
	return __tree_forward2strgp(reqc);
}

static inline int tree_strgp_del_handler(ldmsd_req_ctxt_t reqc)
{
	return __tree_forward2strgp(reqc);
}

static int tree_strgp_add_handler(ldmsd_req_ctxt_t reqc)
{
	char *attr_name, *name, *plugin, *container, *schema, *interval;
	name = plugin = container = schema = NULL;
	uid_t uid;
	gid_t gid;
	int perm;
	char *perm_s = NULL;
	struct timespec flush_interval = {0, 0};

	reqc->errcode = 0;

	attr_name = "name";
	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!name)
		goto einval;

	attr_name = "plugin";
	plugin = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_PLUGIN);
	if (!plugin)
		goto einval;

	attr_name = "container";
	container = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_CONTAINER);
	if (!container)
		goto einval;

	attr_name = "schema";
	schema = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_SCHEMA);
	if (!schema)
		goto einval;

	attr_name = "flush";
	interval = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_INTERVAL);
	if (interval) {
		int rc = ldmsd_timespec_from_str(&flush_interval, interval);
		if (rc) {
			reqc->errcode = ENOENT;
			(void) linebuf_printf(reqc,
					"The specified flush interval, \"%s\", is invalid.", interval);
			goto send_reply;
		}
	}
	struct ldmsd_plugin_cfg *store;
	store = ldmsd_get_plugin(plugin);
	if (!store) {
		reqc->errcode = ENOENT;
		(void) linebuf_printf(reqc,
				"The plugin %s does not exist. Forgot load?\n", plugin);
		goto send_reply;
	}

	struct ldmsd_sec_ctxt sctxt;
	ldmsd_req_ctxt_sec_get(reqc, &sctxt);
	uid = sctxt.crd.uid;
	gid = sctxt.crd.gid;

	perm = 0770;
	perm_s = ldmsd_req_attr_str_value_get_by_name(reqc, "perm");
	if (perm_s)
		perm = strtol(perm_s, NULL, 0);

	ldmsd_strgp_t strgp = ldmsd_strgp_new_with_auth(name, uid, gid, perm);
	if (!strgp) {
		if (errno == EEXIST)
			goto eexist;
		else
			goto enomem;
	}

	strgp->plugin_name = strdup(plugin);
	if (!strgp->plugin_name)
		goto enomem_1;

	strgp->schema = strdup(schema);
	if (!strgp->schema)
		goto enomem_2;

	strgp->container = strdup(container);
	if (!strgp->container)
		goto enomem_3;

	strgp->flush_interval = flush_interval;
	goto send_reply;

enomem_3:
	free(strgp->schema);
enomem_2:
	free(strgp->plugin_name);
enomem_1:
	free(strgp);
enomem:
	reqc->errcode = ENOMEM;
	(void) linebuf_printf(reqc, "Memory allocation failed.");
	goto send_reply;
eexist:
	reqc->errcode = EEXIST;
	(void) linebuf_printf(reqc, "The prdcr %s already exists.", name);
	goto send_reply;
einval:
	reqc->errcode = EINVAL;
	(void) linebuf_printf(reqc,
			"The attribute '%s' is required by strgp_add.",
		       	attr_name);
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(name);
	free(plugin);
	free(container);
	free(schema);
	free(perm_s);
	return 0;
}

static int tree_strgp_status_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	char *name;
	ldmsd_strgp_t strgp, nxt_strgp;
	struct ldmsd_cfgobj_cfg_ctxt *ctxt = calloc(1, sizeof(*ctxt));
	if (!ctxt)
		goto enomem;
	ref_init(&ctxt->ref, "create", free, ctxt);
	ldmsd_req_ctxt_sec_get(reqc, &ctxt->sctxt);

	rc = linebuf_printf(reqc, "[");
	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (name) {
		strgp = ldmsd_strgp_find(name);
		if (!strgp) {
			reqc->errcode = ENOENT;
			(void) linebuf_printf(reqc, "strgp '%s' not found.", name);
			goto send_reply;
		} else {
			ref_get(&ctxt->ref, "strgp_tree");
			rc = ldmsd_cfgtree_post2cfgobj(&strgp->obj, strgp_tree_w,
							strgp->worker, reqc, ctxt);
			ldmsd_strgp_put(strgp);
			if (rc) {
				ref_put(&ctxt->ref, "strgp_tree");
				if (ENOMEM == rc)
					goto enomem;
				reqc->errcode = EINTR;
				rc = linebuf_printf(reqc,
						"Failed to handle the strgp_start command.");
				goto send_reply;
			}
			ctxt->is_all = 1;
		}
	} else {
		strgp = ldmsd_strgp_first();
		while (strgp) {
			nxt_strgp = ldmsd_strgp_next(strgp);
			if (!nxt_strgp)
				ctxt->is_all = 1;
			ref_get(&ctxt->ref, "strgp_tree");
			rc = ldmsd_cfgtree_post2cfgobj(&strgp->obj, strgp_tree_w,
							strgp->worker, reqc, ctxt);
			if (rc) {
				ref_put(&ctxt->ref, "strgp_tree");
				if (ENOMEM == rc)
					goto enomem;
				reqc->errcode = EINTR;
				(void) snprintf(reqc->line_buf, reqc->line_len,
						"Failed to handle the strgp_start command.");
				goto send_reply;
			}
			strgp = nxt_strgp;
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

extern int
__post_prdset_state_ev(struct prdset_info *prdset_info,
			    enum ldmsd_prdcr_set_state state,
			    void *obj, ev_worker_t src,
			    ev_worker_t dst, struct timespec *to);
int strgp_tree_prdset_state_actor(ev_worker_t src, ev_worker_t dst,
					ev_status_t status, ev_t e)
{
	if (EV_OK != status)
		return 0;

	int rc = 0;
	ldmsd_strgp_t strgp;
	struct prdset_state_data *prdset_data = EV_DATA(e, struct prdset_state_data);

	for (strgp = ldmsd_strgp_first(); strgp; strgp = ldmsd_strgp_next(strgp)) {
		ref_get(&prdset_data->prdset_info->ref, "strgp_tree2strgp");
		ldmsd_strgp_get(strgp);
		rc = __post_prdset_state_ev(prdset_data->prdset_info,
						prdset_data->state,
						strgp, strgp_tree_w,
						strgp->worker, NULL);
		if (rc) {
			ldmsd_strgp_put(strgp);
			ref_put(&prdset_data->prdset_info->ref, "strgp_tree2strgp");
			LDMSD_LOG_ENOMEM();
			rc = ENOMEM;
			goto out;
		}
	}
out:
	ref_put(&prdset_data->prdset_info->ref, "prdset2strgp_tree");
	ev_put(e);
	return rc;
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
tree_prdcr_filter_rsp_handler(ldmsd_req_ctxt_t reqc,
				struct ldmsd_cfgobj_cfg_rsp *rsp,
				struct ldmsd_cfgobj_cfg_ctxt *ctxt)
{
	struct strgp_prdcr_ctxt *pctxt = (struct strgp_prdcr_ctxt *)ctxt;
	ldmsd_name_match_free(pctxt->prdcr_match);
	return tree_cfg_rsp_handler(reqc, rsp, ctxt);
}

static int
tree_metric_filter_rsp_handler(ldmsd_req_ctxt_t reqc,
				struct ldmsd_cfgobj_cfg_rsp *rsp,
				struct ldmsd_cfgobj_cfg_ctxt *ctxt)
{
	struct strgp_metric_ctxt *mctxt = (struct strgp_metric_ctxt *)ctxt;
	free(mctxt->metric_filt->name);
	free(mctxt->metric_filt);

	return tree_cfg_rsp_handler(reqc, rsp, ctxt);
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

extern int ldmsd_cfgobj_tree_del_rsp_handler(ldmsd_req_ctxt_t reqc,
				struct ldmsd_cfgobj_cfg_rsp *rsp,
				struct ldmsd_cfgobj_cfg_ctxt *ctxt,
				enum ldmsd_cfgobj_type type);
int strgp_tree_cfg_rsp_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e)
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
	case LDMSD_STRGP_DEL_REQ:
		rc = ldmsd_cfgobj_tree_del_rsp_handler(reqc, rsp, ctxt,
						   LDMSD_CFGOBJ_STRGP);
		break;
	case LDMSD_STRGP_START_REQ:
	case LDMSD_STRGP_DEFER_START_REQ:
	case LDMSD_STRGP_STOP_REQ:
		rc = tree_cfg_rsp_handler(reqc, rsp, ctxt);
		break;
	case LDMSD_STRGP_PRDCR_ADD_REQ:
	case LDMSD_STRGP_PRDCR_DEL_REQ:
		rc = tree_prdcr_filter_rsp_handler(reqc, rsp, ctxt);
		break;
	case LDMSD_STRGP_METRIC_ADD_REQ:
	case LDMSD_STRGP_METRIC_DEL_REQ:
		rc = tree_metric_filter_rsp_handler(reqc, rsp, ctxt);
		break;
	case LDMSD_STRGP_STATUS_REQ:
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
	ref_put(&ctxt->ref, "strgp_tree");
	ev_put(e);
	return rc;
}

int strgp_tree_cfg_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e)
{
	if (EV_OK != status)
		return 0;

	struct ldmsd_req_ctxt *reqc = EV_DATA(e, struct cfg_data)->reqc;
	int rc = 0;

	switch (reqc->req_id) {
	case LDMSD_STRGP_ADD_REQ:
		rc = tree_strgp_add_handler(reqc);
		break;
	case LDMSD_STRGP_DEL_REQ:
		rc = tree_strgp_del_handler(reqc);
		break;
	case LDMSD_STRGP_PRDCR_ADD_REQ:
	case LDMSD_STRGP_PRDCR_DEL_REQ:
		rc = tree_strgp_prdcr_filter_handler(reqc);
		break;
	case LDMSD_STRGP_METRIC_ADD_REQ:
	case LDMSD_STRGP_METRIC_DEL_REQ:
		rc = tree_strgp_metric_filter_handler(reqc);
		break;
	case LDMSD_STRGP_DEFER_START_REQ:
	case LDMSD_STRGP_START_REQ:
		rc = tree_strgp_start_handler(reqc);
		break;
	case LDMSD_STRGP_STOP_REQ:
		rc = tree_strgp_stop_handler(reqc);
		break;
	case LDMSD_STRGP_STATUS_REQ:
		rc = tree_strgp_status_handler(reqc);
		break;
	default:
		ldmsd_log(LDMSD_LERROR, "%s doesn't handle req_id %d\n",
						__func__, reqc->req_id);
		assert(0);
		rc = EINTR;
		goto out;
	}

out:
	ev_put(e);
	return rc;
}

int strgp_cleanup_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e)
{
	if (EV_OK != status)
		return 0;

	ldmsd_strgp_t strgp = EV_DATA(e, struct strgp_data)->strgp;
	struct strgp_cleanup_ctxt *ctxt = EV_DATA(e, struct strgp_data)->ctxt;
	proc_exit_fn exit_fn = ctxt->exit_fn;
	void *exit_args = ctxt->exit_args;

	strgp_close(strgp);
	strgp->state = LDMSD_STRGP_STATE_STOPPED;

	if ((ctxt->is_all) && (__sync_add_and_fetch(&ctxt->num_recv, 1))) {
		/* This is the last strgp to close */
		free(ctxt);
		exit_fn(exit_args);
	}
	return 0;
}

int strgp_tree_cleanup_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e)
{
	if (EV_OK != status) {
		printf("%s[%d]: received a flushed event, "
			"exiting the process\n", __func__, __LINE__);
		exit(1);
	}

	ev_t ev;
	ldmsd_strgp_t strgp, nxt_strgp;
	struct strgp_cleanup_ctxt *ctxt;
	proc_exit_fn exit_fn = EV_DATA(e, struct cleanup_data)->exit_fn;
	void *exit_args = EV_DATA(e, struct cleanup_data)->exit_args;

	strgp = ldmsd_strgp_first();
	if (!strgp) {
		/* Exit here */
		exit_fn(exit_args);
	}

	ctxt = calloc(1, sizeof(*ctxt));
	ctxt->exit_fn = exit_fn;
	ctxt->exit_args = exit_args;
	while (strgp) {
		if (strgp->state != LDMSD_STRGP_STATE_RUNNING)
			continue;
		ev = ev_new(strgp_cleanup_type);
		EV_DATA(ev, struct strgp_data)->strgp = strgp;
		EV_DATA(ev, struct strgp_data)->ctxt = ctxt;
		ctxt->num_sent++;
		nxt_strgp = ldmsd_strgp_next(strgp);
		if (!nxt_strgp)
			ctxt->is_all = 1;
		ev_post(strgp_tree_w, strgp->worker, ev, 0);
	}
	return 0;
}
