/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2015 Sandia Corporation. All rights reserved.
 *
 * Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
 * license for use of this work by or on behalf of the U.S. Government.
 * Export of this program may require a license from the United States
 * Government.
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
#include <time.h>
#include <coll/rbt.h>
#include <ovis_util/util.h>
#include "event.h"
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
	ldmsd_cfgobj___del(obj);
}

static void strgp_update_fn(ldmsd_strgp_t strgp, ldmsd_prdcr_set_t prd_set)
{
	if (strgp->state != LDMSD_STRGP_STATE_RUNNING)
		return;
	strgp->store->store(strgp->store_handle, prd_set->set,
			    strgp->metric_arry, strgp->metric_count);
}

ldmsd_strgp_t
ldmsd_strgp_new(const char *name)
{
	struct ldmsd_strgp *strgp;

	strgp = (struct ldmsd_strgp *)
		ldmsd_cfgobj_new(name, LDMSD_CFGOBJ_STRGP,
				 sizeof *strgp, ldmsd_strgp___del);
	if (!strgp)
		return NULL;

	strgp->state = LDMSD_STRGP_STATE_STOPPED;
	strgp->update_fn = strgp_update_fn;
	LIST_INIT(&strgp->prdcr_list);
	TAILQ_INIT(&strgp->metric_list);
	ldmsd_task_init(&strgp->task);
	ldmsd_cfgobj_unlock(&strgp->obj);
	return strgp;
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

int cmd_strgp_add(char *replybuf, struct attr_value_list *avl, struct attr_value_list *kwl)
{
	char *attr, *name, *plugin, *container, *schema;
	struct ldmsd_plugin_cfg *store;

	attr = "name";
	name = av_value(avl, attr);
	if (!name)
		goto einval;

	attr = "plugin";
	plugin = av_value(avl, attr);
	if (!plugin)
		goto einval;

	/* Make certain the plugin exists */
	store = ldmsd_get_plugin(plugin);
	if (!store) {
		sprintf(replybuf, "%dThe plugin does not exist.\n", EINVAL);
		goto out;
	}

	attr = "container";
	container = av_value(avl, attr);
	if (!container)
		goto einval;

	attr = "schema";
	schema = av_value(avl, attr);
	if (!schema)
		goto einval;

	ldmsd_strgp_t strgp = ldmsd_strgp_new(name);
	if (!strgp) {
		if (errno == EEXIST)
			goto eexist;
		else
			goto enomem;
	}

	strgp->plugin_name = strdup(plugin);
	if (!strgp->plugin_name)
		goto enomem_1;

	strgp->store = store->store;

	strgp->schema = strdup(schema);
	if (!strgp->schema)
		goto enomem_2;

	strgp->container = strdup(container);
	if (!strgp->container)
		goto enomem_3;

	sprintf(replybuf, "0\n");
	goto out;

einval:
	sprintf(replybuf, "%dThe attribute '%s' is missing or invalid.\n",
			EINVAL, attr);
	goto out;
eexist:
	sprintf(replybuf, "%dThe strgp %s already exists.\n", EEXIST, name);
	goto out;
enomem_3:
	free(strgp->schema);
enomem_2:
	free(strgp->plugin_name);
enomem_1:
	free(strgp);
enomem:
	sprintf(replybuf, "%dMemory allocation failed.\n", ENOMEM);
	goto out;
out:
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

int cmd_strgp_prdcr_add(char *replybuf, struct attr_value_list *avl, struct attr_value_list *kwl)
{
	char *attr, *strgp_name, *regex_str;

	attr = "name";
	strgp_name = av_value(avl, attr);
	if (!strgp_name) {
		sprintf(replybuf, "%dThe storage policy name must be specified\n", EINVAL);
		goto out_0;
	}
	regex_str = av_value(avl, "regex");
	if (!regex_str) {
		sprintf(replybuf,
			"%dThe regular expression must be specified.\n",
			EINVAL);
		goto out_0;
	}
	ldmsd_strgp_t strgp = ldmsd_strgp_find(strgp_name);
	if (!strgp) {
		sprintf(replybuf, "%dThe storage policy specified does not exist\n", ENOENT);
		goto out_0;
	}
	ldmsd_strgp_lock(strgp);
	if (strgp->state != LDMSD_STRGP_STATE_STOPPED) {
		sprintf(replybuf, "%dConfiguration changes cannot be made "
			"while the storage policy is running\n", EBUSY);
		goto out_1;
	}
	ldmsd_name_match_t match = calloc(1, sizeof *match);
	if (!match) {
		sprintf(replybuf, "%d\n", ENOMEM);
		goto out_1;
	}
	match->regex_str = strdup(regex_str);
	if (!match->regex_str) {
		sprintf(replybuf, "22\n");
		goto out_2;
	}
	if (ldmsd_compile_regex(&match->regex, regex_str, replybuf, sizeof(replybuf)))
		goto out_3;
	match->selector = LDMSD_NAME_MATCH_INST_NAME;
	LIST_INSERT_HEAD(&strgp->prdcr_list, match, entry);
	strcpy(replybuf, "0\n");
	goto out_1;
out_3:
	free(match->regex_str);
out_2:
	free(match);
out_1:
	ldmsd_strgp_unlock(strgp);
	ldmsd_strgp_put(strgp);
out_0:
	return 0;
}
int cmd_strgp_prdcr_del(char *replybuf, struct attr_value_list *avl, struct attr_value_list *kwl)
{
	char *strgp_name, *regex_str;

	strgp_name = av_value(avl, "name");
	if (!strgp_name) {
		sprintf(replybuf, "%dThe storage policy name must be specified\n", EINVAL);
		goto out_0;
	}
	regex_str = av_value(avl, "regex");
	if (!regex_str) {
		sprintf(replybuf,
			"%dThe regular expression must be specified.\n",
			EINVAL);
		goto out_0;
	}

	ldmsd_strgp_t strgp = ldmsd_strgp_find(strgp_name);
	if (!strgp) {
		sprintf(replybuf, "%dThe storage policy specified does not exist\n", ENOENT);
		goto out_0;
	}
	ldmsd_strgp_lock(strgp);
	if (strgp->state != LDMSD_STRGP_STATE_STOPPED) {
		sprintf(replybuf, "%dConfiguration changes cannot be made "
			"while the storage policy is running\n", EBUSY);
		goto out_1;
	}
	ldmsd_name_match_t match = strgp_find_prdcr_ex(strgp, regex_str);
	if (!match) {
		sprintf(replybuf,
			"%dThe specified regex does not match any condition\n",
			ENOENT);
		goto out_1;
	}
	LIST_REMOVE(match, entry);
	free(match->regex_str);
	regfree(&match->regex);
	free(match);
	strcpy(replybuf, "0\n");
out_1:
	ldmsd_strgp_unlock(strgp);
	ldmsd_strgp_put(strgp);
out_0:
	return 0;
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

int cmd_strgp_metric_add(char *replybuf, struct attr_value_list *avl, struct attr_value_list *kwl)
{
	char *strgp_name, *metric_name;

	strgp_name = av_value(avl, "name");
	if (!strgp_name) {
		sprintf(replybuf, "%dThe storage policy name must be specified\n", EINVAL);
		goto out_0;
	}
	metric_name = av_value(avl, "metric");
	if (!metric_name) {
		sprintf(replybuf, "%dA metric name must be specified\n", EINVAL);
		goto out_0;
	}
	ldmsd_strgp_t strgp = ldmsd_strgp_find(strgp_name);
	if (!strgp) {
		sprintf(replybuf, "%dThe storage policy specified does not exist\n", ENOENT);
		goto out_0;
	}
	ldmsd_strgp_lock(strgp);
	if (strgp->state != LDMSD_STRGP_STATE_STOPPED) {
		sprintf(replybuf, "%dConfiguration changes cannot be made "
			"while the storage policy is running\n", EBUSY);
		goto out_1;
	}
	ldmsd_strgp_metric_t metric = strgp_metric_find(strgp, metric_name);
	if (metric) {
		sprintf(replybuf, "%dThe specified metric is already present.\n", EEXIST);
		goto out_1;
	}
	metric = strgp_metric_new(metric_name);
	if (!metric) {
		sprintf(replybuf, "%dMemory allocation failure.\n", ENOMEM);
		goto out_1;
	}
	TAILQ_INSERT_TAIL(&strgp->metric_list, metric, entry);
	sprintf(replybuf, "0\n");
out_1:
	ldmsd_strgp_unlock(strgp);
	ldmsd_strgp_put(strgp);
out_0:
	return 0;
}

int cmd_strgp_metric_del(char *replybuf, struct attr_value_list *avl, struct attr_value_list *kwl)
{
	char *strgp_name, *metric_name;

	strgp_name = av_value(avl, "name");
	if (!strgp_name) {
		sprintf(replybuf, "%dThe storage policy name must be specified\n", EINVAL);
		goto out_0;
	}
	metric_name = av_value(avl, "metric");
	if (!metric_name) {
		sprintf(replybuf, "%dA metric name must be specified\n", EINVAL);
		goto out_0;
	}
	ldmsd_strgp_t strgp = ldmsd_strgp_find(strgp_name);
	if (!strgp) {
		sprintf(replybuf, "%dThe storage policy specified does not exist\n", ENOENT);
		goto out_0;
	}
	ldmsd_strgp_lock(strgp);
	if (strgp->state != LDMSD_STRGP_STATE_STOPPED) {
		sprintf(replybuf, "%dConfiguration changes cannot be made "
			"while the storage policy is running\n", EBUSY);
		goto out_1;
	}
	ldmsd_strgp_metric_t metric = strgp_metric_find(strgp, metric_name);
	if (!metric) {
		sprintf(replybuf, "%dThe specified metric was not found.\n", EEXIST);
		goto out_1;
	}
	TAILQ_REMOVE(&strgp->metric_list, metric, entry);
	free(metric->name);
	free(metric);
	sprintf(replybuf, "0\n");
out_1:
	ldmsd_strgp_unlock(strgp);
	ldmsd_strgp_put(strgp);
out_0:
	return 0;
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
	strgp->metric_arry = calloc(ldms_set_card_get(prd_set->set), sizeof(int *));
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
	return 0;
err:
	free(strgp->metric_arry);
	strgp->metric_count = 0;
	return rc;
}

/** Must be called with the strgp config lock held */
int ldmsd_strgp_update_prdcr_set(ldmsd_strgp_t strgp, ldmsd_prdcr_set_t prd_set)
{
	int rc = 0;
	ldmsd_strgp_ref_t ref;

	if (strcmp(strgp->schema, prd_set->schema_name))
		return ENOENT;

	ref = strgp_ref_find(prd_set, strgp);
	switch (strgp->state) {
	case LDMSD_STRGP_STATE_STOPPED:
		if (ref) {
			LIST_REMOVE(ref, entry);
			ldmsd_strgp_put(ref->strgp);
			free(ref);
		}
		break;
	case LDMSD_STRGP_STATE_RUNNING:
		rc = EEXIST;
		if (ref)
			break;
		rc = ENOMEM;
		ref = strgp_ref_new(strgp);
		if (!ref)
			break;
		LIST_INSERT_HEAD(&prd_set->strgp_list, ref, entry);
		if (!strgp->store_handle)
			strgp_open(strgp, prd_set);
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
	ldmsd_cfg_lock(LDMSD_CFGOBJ_STRGP);
	for (strgp = ldmsd_strgp_first(); strgp; strgp = ldmsd_strgp_next(strgp)) {
		ldmsd_strgp_lock(strgp);
		ldmsd_strgp_update_prdcr_set(strgp, prd_set);
		ldmsd_strgp_unlock(strgp);
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_STRGP);
}

int cmd_strgp_start(char *replybuf, struct attr_value_list *avl, struct attr_value_list *kwl)
{
	char *strgp_name;
	strgp_name = av_value(avl, "name");
	if (!strgp_name) {
		sprintf(replybuf, "%dThe storage policy name must be specified.\n", EINVAL);
		goto out_0;
	}
	ldmsd_strgp_t strgp = ldmsd_strgp_find(strgp_name);
	if (!strgp) {
		sprintf(replybuf, "%dThe storage policy does not exist.\n", ENOENT);
		goto out_0;
	}
	ldmsd_strgp_lock(strgp);
	if (strgp->state != LDMSD_STRGP_STATE_STOPPED) {
		sprintf(replybuf, "%dThe storage policy is already running\n", EBUSY);
		goto out_1;
	}
	strgp->state = LDMSD_STRGP_STATE_RUNNING;
	/* Update all the producers of our changed state */
	ldmsd_prdcr_update(strgp);
	sprintf(replybuf, "0\n");
out_1:
	ldmsd_strgp_unlock(strgp);
	ldmsd_strgp_put(strgp);
out_0:
	return 0;
}

int cmd_strgp_stop(char *replybuf, struct attr_value_list *avl, struct attr_value_list *kwl)
{
	char *strgp_name;

	strgp_name = av_value(avl, "name");
	if (!strgp_name) {
		sprintf(replybuf, "%dThe storage policy name must be specified\n", EINVAL);
		goto out_0;
	}
	ldmsd_strgp_t strgp = ldmsd_strgp_find(strgp_name);
	if (!strgp) {
		sprintf(replybuf, "%dThe storage policy specified does not exist\n", ENOENT);
		goto out_0;
	}
	ldmsd_strgp_lock(strgp);
	if (strgp->state != LDMSD_STRGP_STATE_RUNNING) {
		sprintf(replybuf, "%dThe storage policy is not running\n", EBUSY);
		goto out_1;
	}
	ldmsd_task_stop(&strgp->task);
	strgp_close(strgp);
	strgp->state = LDMSD_STRGP_STATE_STOPPED;
	ldmsd_prdcr_update(strgp);
	sprintf(replybuf, "0\n");
out_1:
	ldmsd_strgp_unlock(strgp);
	ldmsd_strgp_put(strgp);
out_0:
	return 0;
}

int cmd_strgp_del(char *replybuf, struct attr_value_list *avl, struct attr_value_list *kwl)
{
	char *strgp_name;
	strgp_name = av_value(avl, "name");
	if (!strgp_name) {
		sprintf(replybuf, "%dThe storage policy name must be specified\n", EINVAL);
		goto out_0;
	}
	ldmsd_strgp_t strgp = ldmsd_strgp_find(strgp_name);
	if (!strgp) {
		sprintf(replybuf, "%dThe storage policy specified does not exist\n", ENOENT);
		goto out_0;
	}
	ldmsd_strgp_lock(strgp);
	if (strgp->state != LDMSD_STRGP_STATE_STOPPED) {
		sprintf(replybuf, "%dConfiguration changes cannot be made "
			"while the storage policy is running\n", EBUSY);
		goto out_1;
	}
	if (ldmsd_cfgobj_refcount(&strgp->obj) > 2) {
		sprintf(replybuf, "%dThe storage policy is in use.\n", EBUSY);
		goto out_1;
	}
	/* Put the find reference */
	ldmsd_strgp_put(strgp);
	/* Drop the lock and drop the create reference */
	ldmsd_strgp_unlock(strgp);
	ldmsd_strgp_put(strgp);
	sprintf(replybuf, "0\n");
	goto out_0;
out_1:
	ldmsd_strgp_put(strgp);
	ldmsd_strgp_unlock(strgp);
out_0:
	return 0;
}
