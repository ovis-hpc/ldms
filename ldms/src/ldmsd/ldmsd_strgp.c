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
#include "config.h"

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
	ldmsd_name_match_t match;
	while (!LIST_EMPTY(&strgp->prdcr_list)) {
		match = LIST_FIRST(&strgp->prdcr_list);
		if (match->regex_str)
			free(match->regex_str);
		regfree(&match->regex);
		LIST_REMOVE(match, entry);
		free(match);
	}
	if (strgp->plugin_name)
		free(strgp->plugin_name);
	if (strgp->decomp_name)
		free(strgp->decomp_name);
	free(strgp->digest);
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

static void strgp_decompose(ldmsd_strgp_t strgp, ldmsd_prdcr_set_t prd_set)
{
	struct ldmsd_row_list_s row_list = TAILQ_HEAD_INITIALIZER(row_list);
	int row_count, rc;
	rc = strgp->decomp->decompose(strgp, prd_set->set, &row_list, &row_count);
	if (rc) {
		ldmsd_log(LDMSD_LERROR, "strgp decompose error: %d\n", rc);
		return;
	}
	rc = strgp->store->commit(strgp, prd_set->set, &row_list, row_count);
	if (rc) {
		ldmsd_log(LDMSD_LERROR, "strgp row commit error: %d\n", rc);
	}
	strgp->decomp->release_rows(strgp, &row_list);
}

/* protected by strgp lock */
static void strgp_update_fn(ldmsd_strgp_t strgp, ldmsd_prdcr_set_t prd_set)
{
	if (strgp->state != LDMSD_STRGP_STATE_RUNNING)
		return;
	if (!strgp->decomp_name)
		goto store_routine;

	/* decomp() interface routine */
	if (!strgp->decomp) {
		strgp->state = LDMSD_STRGP_STATE_STOPPED;
		return;
	}
	strgp_decompose(strgp, prd_set);
	goto out;

	/* store() interface routine */
 store_routine:
	if (!strgp->store_handle) {
		strgp->state = LDMSD_STRGP_STATE_STOPPED;
		return;
	}
	strgp->store->store(strgp->store_handle, prd_set->set,
			    strgp->metric_arry, strgp->metric_count);
 out:
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
	LIST_INIT(&strgp->prdcr_list);
	TAILQ_INIT(&strgp->metric_list);
	ldmsd_task_init(&strgp->task);
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
	return LIST_FIRST(&strgp->prdcr_list);
}

ldmsd_name_match_t ldmsd_strgp_prdcr_next(ldmsd_name_match_t match)
{
	return LIST_NEXT(match, entry);
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
	LIST_FOREACH(match, &strgp->prdcr_list, entry) {
		if (0 == strcmp(match->regex_str, ex))
			return match;
	}
	return NULL;
}

int ldmsd_strgp_prdcr_add(const char *strgp_name, const char *regex_str,
			  char *rep_buf, size_t rep_len, ldmsd_sec_ctxt_t ctxt)
{
	int rc = 0;
	ldmsd_strgp_t strgp = ldmsd_strgp_find(strgp_name);
	if (!strgp)
		return ENOENT;

	ldmsd_strgp_lock(strgp);
	rc = ldmsd_cfgobj_access_check(&strgp->obj, 0222, ctxt);
	if (rc)
		goto out_1;
	if (strgp->state != LDMSD_STRGP_STATE_STOPPED) {
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
	rc = ldmsd_compile_regex(&match->regex, regex_str,
					rep_buf, rep_len);
	if (rc)
		goto out_3;
	match->selector = LDMSD_NAME_MATCH_INST_NAME;
	LIST_INSERT_HEAD(&strgp->prdcr_list, match, entry);
	goto out_1;
out_3:
	free(match->regex_str);
out_2:
	free(match);
out_1:
	ldmsd_strgp_unlock(strgp);
	ldmsd_strgp_put(strgp);
	return rc;
}

int ldmsd_strgp_prdcr_del(const char *strgp_name, const char *regex_str,
			ldmsd_sec_ctxt_t ctxt)
{
	int rc = 0;
	ldmsd_strgp_t strgp = ldmsd_strgp_find(strgp_name);
	if (!strgp)
		return ENOENT;

	ldmsd_strgp_lock(strgp);
	rc = ldmsd_cfgobj_access_check(&strgp->obj, 0222, ctxt);
	if (rc)
		goto out_1;
	if (strgp->state != LDMSD_STRGP_STATE_STOPPED) {
		rc = EBUSY;
		goto out_1;
	}
	ldmsd_name_match_t match = strgp_find_prdcr_ex(strgp, regex_str);
	if (!match) {
		rc = EEXIST;
		goto out_1;
	}
	LIST_REMOVE(match, entry);
	free(match->regex_str);
	regfree(&match->regex);
	free(match);
out_1:
	ldmsd_strgp_unlock(strgp);
	ldmsd_strgp_put(strgp);
	return rc;
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

int ldmsd_strgp_metric_add(const char *strgp_name, const char *metric_name,
			   ldmsd_sec_ctxt_t ctxt)
{
	int rc = 0;
	ldmsd_strgp_t strgp = ldmsd_strgp_find(strgp_name);
	if (!strgp)
		return ENOENT;

	ldmsd_strgp_lock(strgp);
	rc = ldmsd_cfgobj_access_check(&strgp->obj, 0222, ctxt);
	if (rc)
		goto out_1;
	if (strgp->state != LDMSD_STRGP_STATE_STOPPED) {
		rc = EBUSY;
		goto out_1;
	}
	ldmsd_strgp_metric_t metric = strgp_metric_find(strgp, metric_name);
	if (metric) {
		rc = EEXIST;
		goto out_1;
	}
	metric = strgp_metric_new(metric_name);
	if (!metric) {
		rc = ENOMEM;
		goto out_1;
	}
	TAILQ_INSERT_TAIL(&strgp->metric_list, metric, entry);
out_1:
	ldmsd_strgp_unlock(strgp);
	ldmsd_strgp_put(strgp);
	return rc;
}

int ldmsd_strgp_metric_del(const char *strgp_name, const char *metric_name,
			   ldmsd_sec_ctxt_t ctxt)
{
	int rc = 0;
	ldmsd_strgp_t strgp = ldmsd_strgp_find(strgp_name);
	if (!strgp)
		return ENOENT;
	ldmsd_strgp_lock(strgp);
	rc = ldmsd_cfgobj_access_check(&strgp->obj, 0222, ctxt);
	if (rc)
		goto out_1;
	if (strgp->state != LDMSD_STRGP_STATE_STOPPED) {
		rc = EBUSY;
		goto out_1;
	}
	ldmsd_strgp_metric_t metric = strgp_metric_find(strgp, metric_name);
	if (!metric) {
		rc = EEXIST;
		goto out_1;
	}
	TAILQ_REMOVE(&strgp->metric_list, metric, entry);
	free(metric->name);
	free(metric);
out_1:
	ldmsd_strgp_unlock(strgp);
	ldmsd_strgp_put(strgp);
	return rc;
}

static ldmsd_strgp_ref_t strgp_ref_new(ldmsd_strgp_t strgp)
{
	ldmsd_strgp_ref_t ref = calloc(1, sizeof *ref);
	if (ref)
		ref->strgp = ldmsd_strgp_get(strgp);
	return ref;
}

static ldmsd_strgp_ref_t strgp_ref_find(ldmsd_prdcr_set_t prd_set, ldmsd_strgp_t strgp)
{
	ldmsd_strgp_ref_t ref;
	LIST_FOREACH(ref, &prd_set->strgp_list, entry) {
		if (ref->strgp == strgp)
			return ref;
	}
	return NULL;
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

static int strgp_open(ldmsd_strgp_t strgp, ldmsd_prdcr_set_t prd_set)
{
	int i, idx, rc;
	const char *name;
	ldmsd_strgp_metric_t metric;
	struct ldmsd_plugin_cfg *store;
	int alloc_digest = 0;

	if (!prd_set->set)
		return ENOENT;

	if (!strgp->store) {
		store = ldmsd_get_plugin(strgp->plugin_name);
		if (!store)
			return ENOENT;
		strgp->store = store->store;
	}
	/* Build metric list from the schema in the producer set */
	strgp->metric_count = 0;
	strgp->metric_arry = calloc(ldms_set_card_get(prd_set->set), sizeof(int));
	if (!strgp->metric_arry)
		return ENOMEM;

	rc = ENOMEM;
	if (TAILQ_EMPTY(&strgp->metric_list)) {
		/* No metric list was given. Add all metrics in the set */
		for (i = 0; i < ldms_set_card_get(prd_set->set); i++) {
			name = ldms_metric_name_get(prd_set->set, i);
			metric = strgp_metric_new(name);
			if (!metric)
				goto err;
			TAILQ_INSERT_TAIL(&strgp->metric_list, metric, entry);
		}
	}

	if (strgp->digest) {
		/*
		 * The storage policy has been restarted.
		 * We are making sure that we are building the metric array from
		 * the schema of the same digest.
		 */
		if (0 != ldms_digest_cmp(strgp->digest, ldms_set_digest_get(prd_set->set))) {
			ldmsd_log(LDMSD_LERROR,
				  "strgp '%s' ignores set '%s' because "
				  "the metric lists are mismatched.\n",
				  strgp->obj.name, prd_set->inst_name);
			rc = EINVAL;
			goto err;
		}
	} else {
		alloc_digest = 1;
		strgp->digest = calloc(1, sizeof(*strgp->digest));
		if (!strgp->digest) {
			rc = ENOMEM;
			goto err;
		}
	}

	rc = ENOENT;
	for (i = 0, metric = ldmsd_strgp_metric_first(strgp);
	     metric; metric = ldmsd_strgp_metric_next(metric), i++) {
		name = metric->name;
		idx = ldms_metric_by_name(prd_set->set, name);
		if (idx < 0)
			goto err;
		metric->type = ldms_metric_type_get(prd_set->set, idx);
		strgp->metric_arry[i] = idx;
	}
	strgp->metric_count = i;

	strgp->store_handle = ldmsd_store_open(strgp->store, strgp->container,
			strgp->schema, &strgp->metric_list, strgp);
	rc = EINVAL;
	if (!strgp->store_handle)
		goto err;
	if (alloc_digest) {
		memcpy(strgp->digest, ldms_set_digest_get(prd_set->set),
						    LDMS_DIGEST_LENGTH);
	}
	return 0;
err:
	if (alloc_digest) {
		free(strgp->digest);
		strgp->digest = NULL;
	}
	free(strgp->metric_arry);
	strgp->metric_arry = NULL;
	strgp->metric_count = 0;
	return rc;
}

/* protected by strgp lock */
int strgp_decomp_init(ldmsd_strgp_t strgp, ldmsd_req_ctxt_t reqc)
{

	if (!strgp->store) {
		/* load store */
		struct ldmsd_plugin_cfg *store;
		store = ldmsd_get_plugin(strgp->plugin_name);
		if (!store)
			return ENOENT;
		strgp->store = store->store;
	}
	assert(!strgp->decomp);
	return ldmsd_decomp_config(strgp, strgp->decomp_name, reqc);
}

/** Must be called with the producer set lock and the strgp config lock held and in this order*/
int ldmsd_strgp_update_prdcr_set(ldmsd_strgp_t strgp, ldmsd_prdcr_set_t prd_set)
{
	int rc = 0;
	ldmsd_strgp_ref_t ref;

	if (strgp->schema) {
		/* schema exact match */
		if (strcmp(strgp->schema, prd_set->schema_name))
			return ENOENT;
	} else {
		/* regex match */
		if (regexec(&strgp->schema_regex, prd_set->schema_name, 0, NULL, 0))
			return ENOENT;
	}

	ref = strgp_ref_find(prd_set, strgp);
	switch (strgp->state) {
	case LDMSD_STRGP_STATE_STOPPED:
		if (ref) {
			LIST_REMOVE(ref, entry);
			ldmsd_strgp_put(ref->strgp);
			ref->strgp = NULL;
			free(ref);
		}
		break;
	case LDMSD_STRGP_STATE_RUNNING:
		rc = EEXIST;
		if (ref)
			break;
		if (strgp->decomp_name) {
			if (!strgp->decomp) {
				rc = strgp_decomp_init(strgp, NULL);
				if (rc)
					break;
			}
		} else {
			/* legacy store path */
			if (strgp->digest) {
				/*
				 * The strgp has cached a digest and been restarted.
				 */
				if (0 != ldms_digest_cmp(strgp->digest,
						ldms_set_digest_get(prd_set->set))) {
					ldmsd_log(LDMSD_LERROR,
						  "strgp '%s' ignores set '%s' because "
						  "the metric lists are mismatched.\n",
						  strgp->obj.name, prd_set->inst_name);
					break;
				}
			}
			if (!strgp->store_handle) {
				rc = strgp_open(strgp, prd_set);
				if (rc)
					break;
			}
		}
		rc = ENOMEM;
		ref = strgp_ref_new(strgp);
		if (!ref)
			break;
		LIST_INSERT_HEAD(&prd_set->strgp_list, ref, entry);
		rc = 0;
		break;
	default:
		assert(0 == "Bad strgp state");
	}
	return rc;
}

/**
 * Given a producer set, check each storage policy to see if it
 * applies. If it does, add the storage policy to the producer set
 * and open the container if necessary.
 */
void ldmsd_strgp_update(ldmsd_prdcr_set_t prd_set)
{
	ldmsd_strgp_t strgp;
	int rc;
	ldmsd_cfg_lock(LDMSD_CFGOBJ_STRGP);
	for (strgp = ldmsd_strgp_first(); strgp; strgp = ldmsd_strgp_next(strgp)) {
		ldmsd_strgp_lock(strgp);
		ldmsd_name_match_t match = ldmsd_strgp_prdcr_first(strgp);
		for (rc = 0; match; match = ldmsd_strgp_prdcr_next(match)) {
			rc = regexec(&match->regex, prd_set->prdcr->obj.name, 0, NULL, 0);
			if (!rc)
				break;
		}
		if (rc) {
			ldmsd_strgp_unlock(strgp);
			continue;
		}
		ldmsd_strgp_update_prdcr_set(strgp, prd_set);
		ldmsd_strgp_unlock(strgp);
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_STRGP);
}

int __ldmsd_strgp_start(ldmsd_strgp_t strgp, ldmsd_sec_ctxt_t ctxt)
{
	int rc;
	ldmsd_strgp_lock(strgp);
	rc = ldmsd_cfgobj_access_check(&strgp->obj, 0222, ctxt);
	if (rc) {
		goto out;
	}
	if (strgp->state != LDMSD_STRGP_STATE_STOPPED) {
		rc = EBUSY;
		goto out;
	}
	strgp->state = LDMSD_STRGP_STATE_RUNNING;
	clock_gettime(CLOCK_REALTIME, &strgp->last_flush);
	strgp->obj.perm |= LDMSD_PERM_DSTART;
	/* Update all the producers of our changed state */
	ldmsd_prdcr_update(strgp);
out:
	ldmsd_strgp_unlock(strgp);
	return rc;
}

int ldmsd_strgp_start(const char *name, ldmsd_sec_ctxt_t ctxt)
{
	int rc;
	ldmsd_strgp_t strgp = ldmsd_strgp_find(name);
	if (!strgp) {
		return ENOENT;
	}
	rc = __ldmsd_strgp_start(strgp, ctxt);
	ldmsd_strgp_put(strgp);
	return rc;
}

int __ldmsd_strgp_stop(ldmsd_strgp_t strgp, ldmsd_sec_ctxt_t ctxt)
{
	int rc = 0;

	ldmsd_strgp_lock(strgp);
	rc = ldmsd_cfgobj_access_check(&strgp->obj, 0222, ctxt);
	if (rc)
		goto out;
	if (strgp->state != LDMSD_STRGP_STATE_RUNNING) {
		rc = EBUSY;
		goto out;
	}
	ldmsd_task_stop(&strgp->task);
	strgp_close(strgp);
	strgp->state = LDMSD_STRGP_STATE_STOPPED;
	strgp->obj.perm &= ~LDMSD_PERM_DSTART;
	ldmsd_prdcr_update(strgp);
out:
	ldmsd_strgp_unlock(strgp);
	return rc;
}

int ldmsd_strgp_stop(const char *strgp_name, ldmsd_sec_ctxt_t ctxt)
{
	int rc = 0;
	ldmsd_strgp_t strgp = ldmsd_strgp_find(strgp_name);
	if (!strgp)
		return ENOENT;
	rc = __ldmsd_strgp_stop(strgp, ctxt);
	ldmsd_strgp_put(strgp);
	return rc;
}

extern struct rbt *cfgobj_trees[];
extern pthread_mutex_t *cfgobj_locks[];
ldmsd_cfgobj_t __cfgobj_find(const char *name, ldmsd_cfgobj_type_t type);

int ldmsd_strgp_del(const char *strgp_name, ldmsd_sec_ctxt_t ctxt)
{
	int rc = 0;
	ldmsd_strgp_t strgp;

	pthread_mutex_lock(cfgobj_locks[LDMSD_CFGOBJ_STRGP]);
	strgp = (ldmsd_strgp_t)__cfgobj_find(strgp_name, LDMSD_CFGOBJ_STRGP);
	if (!strgp) {
		rc = ENOENT;
		goto out_0;
	}

	ldmsd_strgp_lock(strgp);
	rc = ldmsd_cfgobj_access_check(&strgp->obj, 0222, ctxt);
	if (rc)
		goto out_1;
	if (strgp->state != LDMSD_STRGP_STATE_STOPPED) {
		rc = EBUSY;
		goto out_1;
	}
	if (ldmsd_cfgobj_refcount(&strgp->obj) > 2) {
		rc = EBUSY;
		goto out_1;
	}

	rbt_del(cfgobj_trees[LDMSD_CFGOBJ_STRGP], &strgp->obj.rbn);
	ldmsd_strgp_put(strgp); /* tree reference */

	if (strgp->decomp) {
		strgp->decomp->release_decomp(strgp);
	}

	/* let through */
out_1:
	ldmsd_strgp_unlock(strgp);
out_0:
	pthread_mutex_unlock(cfgobj_locks[LDMSD_CFGOBJ_STRGP]);
	if (strgp)
		ldmsd_strgp_put(strgp); /* `find` reference */
	return rc;
}

void ldmsd_strgp_close()
{
	ldmsd_strgp_t strgp = ldmsd_strgp_first();
	while (strgp) {
		ldmsd_strgp_lock(strgp);
		if (strgp->state != LDMSD_STRGP_STATE_RUNNING) {
			goto next;
		}
		ldmsd_task_stop(&strgp->task);
		strgp_close(strgp);
		strgp->state = LDMSD_STRGP_STATE_STOPPED;
		ldmsd_strgp_unlock(strgp);
next:
		strgp = ldmsd_strgp_next(strgp);
	}
}
