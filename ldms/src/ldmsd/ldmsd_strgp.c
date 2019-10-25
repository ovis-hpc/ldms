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

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <time.h>
#include <coll/rbt.h>
#include <ovis_util/util.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_plugin.h"
#include "ldmsd_request.h"
#include "ldmsd_store.h"
#include "ldms_xprt.h"
#include "config.h"

void __strgp_prdcr_list_free(struct ldmsd_strgp_prdcr_list *list)
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

void __strgp_metric_list_free(struct ldmsd_strgp_metric_list *list)
{
	struct ldmsd_strgp_metric *metric;
	while (!TAILQ_EMPTY(list) ) {
		metric = TAILQ_FIRST(list);
		if (metric->name)
			free(metric->name);
		TAILQ_REMOVE(list, metric, entry);
		free(metric);
	}
	free(list);
}

void ldmsd_strgp___del(ldmsd_cfgobj_t obj)
{
	ldmsd_strgp_t strgp = (ldmsd_strgp_t)obj;

	if (strgp->schema)
		free(strgp->schema);
	if (strgp->metric_arry)
		free(strgp->metric_arry);
	__strgp_metric_list_free(strgp->metric_list);
	__strgp_prdcr_list_free(strgp->prdcr_list);
	if (strgp->inst)
		ldmsd_plugin_inst_put(strgp->inst);
	ldmsd_cfgobj___del(obj);
}

static void strgp_update_fn(ldmsd_strgp_t strgp, ldmsd_prdcr_set_t prd_set)
{
	if (strgp->state != LDMSD_STRGP_STATE_OPENED)
		return;
	ldmsd_store_store(strgp->inst, prd_set->set, strgp);
}

int store_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t ev)
{
	ldmsd_strgp_t strgp = EV_DATA(ev, struct store_data)->strgp;
	ldmsd_prdcr_set_t prd_set = EV_DATA(ev, struct store_data)->prd_set;
	strgp->update_fn(strgp, prd_set);
	ldmsd_prdcr_set_ref_put(prd_set, "store_ev");
	return 0;
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
	if (!strgp->metric_list)
		return NULL;
	return TAILQ_FIRST(strgp->metric_list);
}

ldmsd_strgp_metric_t ldmsd_strgp_metric_next(ldmsd_strgp_metric_t metric)
{
	return TAILQ_NEXT(metric, entry);
}

ldmsd_regex_ent_t ldmsd_strgp_prdcr_first(ldmsd_strgp_t strgp)
{
	if (!strgp->prdcr_list)
		return NULL;
	return LIST_FIRST(strgp->prdcr_list);
}

ldmsd_regex_ent_t ldmsd_strgp_prdcr_next(ldmsd_regex_ent_t match)
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

ldmsd_regex_ent_t strgp_find_prdcr_ex(ldmsd_strgp_t strgp, const char *ex)
{
	ldmsd_regex_ent_t match;
	LIST_FOREACH(match, strgp->prdcr_list, entry) {
		if (0 == strcmp(match->regex_str, ex))
			return match;
	}
	return NULL;
}

ldmsd_strgp_metric_t strgp_metric_find(ldmsd_strgp_t strgp, const char *name)
{
	ldmsd_strgp_metric_t metric;
	TAILQ_FOREACH(metric, strgp->metric_list, entry)
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

static ldmsd_strgp_ref_t strgp_ref_new(ldmsd_strgp_t strgp, ldmsd_prdcr_set_t prd_set)
{
	ldmsd_strgp_ref_t ref = calloc(1, sizeof *ref);
	if (ref) {
		ref->strgp = ldmsd_strgp_get(strgp);
		ref->store_ev = ev_new(prdcr_set_store_type);
		EV_DATA(ref->store_ev, struct store_data)->strgp = strgp;
		EV_DATA(ref->store_ev, struct store_data)->prd_set = prd_set;
	}
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
	ldmsd_store_close(strgp->inst);
}

static int strgp_open(ldmsd_strgp_t strgp, ldmsd_prdcr_set_t prd_set)
{
	int i, idx, rc;
	const char *name;
	ldmsd_strgp_metric_t metric;

	if (!prd_set->set)
		return ENOENT;

	/* Build metric list from the schema in the producer set */
	strgp->metric_count = 0;
	strgp->metric_arry = calloc(ldms_set_card_get(prd_set->set), sizeof(int));
	if (!strgp->metric_arry)
		return ENOMEM;

	rc = ENOMEM;
	if (TAILQ_EMPTY(strgp->metric_list)) {
		/* No metric list was given. Add all metrics in the set */
		for (i = 0; i < ldms_set_card_get(prd_set->set); i++) {
			name = ldms_metric_name_get(prd_set->set, i);
			metric = strgp_metric_new(name);
			if (!metric)
				goto err;
			TAILQ_INSERT_TAIL(strgp->metric_list, metric, entry);
		}
	}
	rc = ENOENT;
	for (i = 0, metric = ldmsd_strgp_metric_first(strgp);
	     metric; metric = ldmsd_strgp_metric_next(metric), i++) {
		name = metric->name;
		idx = ldms_metric_by_name(prd_set->set, name);
		if (idx < 0)
			goto err;
		metric->idx = idx;
		metric->type = ldms_metric_type_get(prd_set->set, idx);
		strgp->metric_arry[i] = idx;
	}
	strgp->metric_count = i;
	rc = ldmsd_store_open(strgp->inst, strgp);
	if (rc)
		goto err;
	return 0;
err:
	free(strgp->metric_arry);
	strgp->metric_arry = NULL;
	strgp->metric_count = 0;
	return rc;
}

/** Must be called with the producer set lock and the strgp config lock held and in this order*/
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
			ref->strgp = NULL;
			free(ref);
		}
		break;
	case LDMSD_STRGP_STATE_RUNNING:
		rc = strgp_open(strgp, prd_set);
		if (rc)
			break;
		/* open success */
		strgp->state = LDMSD_STRGP_STATE_OPENED;
		/* let through */
	case LDMSD_STRGP_STATE_OPENED:
		rc = EEXIST;
		if (ref)
			break;
		rc = ENOMEM;
		ref = strgp_ref_new(strgp, prd_set);
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
void ldmsd_strgp_prdset_update(ldmsd_prdcr_set_t prd_set)
{
	ldmsd_strgp_t strgp;
	int rc;
	ldmsd_cfg_lock(LDMSD_CFGOBJ_STRGP);
	for (strgp = ldmsd_strgp_first(); strgp; strgp = ldmsd_strgp_next(strgp)) {
		ldmsd_strgp_lock(strgp);
		ldmsd_regex_ent_t match = ldmsd_strgp_prdcr_first(strgp);
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

void ldmsd_strgp_close()
{
	ldmsd_strgp_t strgp = ldmsd_strgp_first();
	while (strgp) {
		ldmsd_strgp_lock(strgp);
		if (strgp->state == LDMSD_STRGP_STATE_OPENED)
			strgp_close(strgp);
		strgp->state = LDMSD_STRGP_STATE_STOPPED;
		ldmsd_strgp_unlock(strgp);
		/*
		 * ref_count shouldn't reach zero
		 * because the strgp isn't deleted yet.
		 */
		ldmsd_strgp_put(strgp);
		strgp = ldmsd_strgp_next(strgp);
	}
}

int ldmsd_strgp_enable(ldmsd_cfgobj_t obj)
{
	ldmsd_strgp_t strgp = (ldmsd_strgp_t)obj;
	if (strgp->state != LDMSD_STRGP_STATE_STOPPED)
		return EBUSY;
	strgp->state = LDMSD_STRGP_STATE_RUNNING;
	strgp->obj.perm |= LDMSD_PERM_DSTART; /* TODO: remove this? */
	/* Update all the producers of our changed state */
	ldmsd_prdcr_strgp_update(strgp);
	return 0;
}

int ldmsd_strgp_disable(ldmsd_cfgobj_t obj)
{
	ldmsd_strgp_t strgp = (ldmsd_strgp_t)obj;
	if (strgp->state < LDMSD_STRGP_STATE_RUNNING)
		return EBUSY;
	if (strgp->state == LDMSD_STRGP_STATE_OPENED)
		strgp_close(strgp);
	strgp->state = LDMSD_STRGP_STATE_STOPPED;
	strgp->obj.perm &= ~LDMSD_PERM_DSTART; /* TODO remove this? */
	ldmsd_prdcr_strgp_update(strgp);
	return 0;
}

static int __strgp_producers_attr(json_entity_t prdcrs, json_entity_t err,
				struct ldmsd_strgp_prdcr_list **_prdcr_list)
{
	int rc;
	json_entity_t p;
	struct ldmsd_strgp_prdcr_list *list = NULL;
	ldmsd_regex_ent_t match;
	ldmsd_req_buf_t buf = NULL;

	if (!prdcrs)
		goto out;

	list = malloc(sizeof(*list));
	if (!list)
		goto oom;
	LIST_INIT(list);

	buf = ldmsd_req_buf_alloc(1024);
	if (!buf)
		goto oom;

	for (p = json_item_first(prdcrs); p; p = json_item_next(p)) {
		if (JSON_STRING_VALUE != json_entity_type(p)) {
			err = json_dict_build(err,
					JSON_STRING_VALUE, "producers",
						"An element in 'producers' "
						"is not a JSON string.",
					-1);
			if (!err)
				goto oom;
			rc = EINVAL;
			goto err;
		}
		match = malloc(sizeof(*match));
		if (!match)
			goto oom;
		match->regex_str = strdup(json_value_str(p)->str);
		if (!match->regex_str) {
			free(match);
			goto oom;
		}

		rc = ldmsd_compile_regex(&match->regex, match->regex_str,
							buf->buf, buf->len);
		if (rc) {
			err = json_dict_build(err, JSON_STRING_VALUE, "producers",
								buf->buf, -1);
			if (!err) {
				free(match->regex_str);
				free(match);
				goto oom;
			}
		}
		LIST_INSERT_HEAD(list, match, entry);
	}

	ldmsd_req_buf_free(buf);
out:
	*_prdcr_list = list;
	return 0;
oom:
	rc = ENOMEM;
err:
	if (list)
		__strgp_prdcr_list_free(list);
	if (buf)
		ldmsd_req_buf_free(buf);
	*_prdcr_list = NULL;
	return rc;
}

static int __strgp_metrics_attr(json_entity_t metrics, json_entity_t err,
				struct ldmsd_strgp_metric_list **_metric_list)
{
	json_entity_t m;
	struct ldmsd_strgp_metric_list *list = NULL;
	struct ldmsd_strgp_metric *metric;

	if (!metrics)
		goto out;

	list = malloc(sizeof(*list));
	if (!list)
		goto oom;
	TAILQ_INIT(list);

	for (m = json_item_first(metrics); m; m = json_item_next(m)) {
		if (JSON_STRING_VALUE != json_entity_type(m)) {
			err = json_dict_build(err,
					JSON_STRING_VALUE, "metrics",
						"An element in 'metrics' "
						"is not a JSON string.",
					-1);
			if (!err)
				goto oom;
			break;
		}
		metric = strgp_metric_new(json_value_str(m)->str);
		if (!metric) {
			free(list);
			goto oom;
		}
		TAILQ_INSERT_TAIL(list, metric, entry);
	}
out:
	*_metric_list = list;
	return 0;
oom:
	if (list)
		__strgp_metric_list_free(list);
	return ENOMEM;
}

static json_entity_t __strgp_attr_get(json_entity_t dft, json_entity_t spc,
				ldmsd_plugin_inst_t *_inst, char **_schema, int *_perm,
				struct ldmsd_strgp_prdcr_list **_prdcr_list,
				struct ldmsd_strgp_metric_list **_metric_list)
{
	int rc;
	char *s;
	json_entity_t err = NULL;
	ldmsd_plugin_inst_t inst;
	ldmsd_req_buf_t buf;
	json_entity_t container, schema, perm, prdcrs, metrics;
	container = schema = perm = prdcrs = metrics = NULL;

	buf = ldmsd_req_buf_alloc(1024);
	if (!buf)
		goto oom;

	err = json_entity_new(JSON_DICT_VALUE);
	if (!err)
		goto oom;

	if (spc) {
		container = json_value_find(spc, "container");
		schema = json_value_find(spc, "schema");
		perm = json_value_find(spc, "perm");
		prdcrs = json_value_find(spc, "producer_filters");
		metrics = json_value_find(spc, "metrics");
	}

	if (dft) {
		if (!container)
			container = json_value_find(dft, "container");
		if (!schema)
			schema = json_value_find(dft, "schema");
		if (!perm)
			perm = json_value_find(dft, "perm");
		if (!prdcrs)
			prdcrs = json_value_find(dft, "producer_filters");
		if (!metrics)
			metrics = json_value_find(dft, "metrics");
	}

	/* schema */
	if (schema) {
		if (JSON_STRING_VALUE != json_entity_type(schema)) {
			err = json_dict_build(err, JSON_STRING_VALUE,
					"'schema' is not a JSON string.", -1);
			if (!err)
				goto oom;
		} else {
			*_schema = strdup(json_value_str(schema)->str);
			if (!*_schema)
				goto oom;
		}
	} else {
		*_schema = NULL;
	}

	/* perm */
	if (perm) {
		if (JSON_STRING_VALUE != json_entity_type(perm)) {
			err = json_dict_build(err, JSON_STRING_VALUE,
					"'perm' is not a JSON string.", -1);
			if (!err)
				goto oom;
		} else {
			*_perm = strtol(json_value_str(perm)->str, NULL, 0);
		}
	} else {
		*_perm  = LDMSD_ATTR_NA;
	}

	/* container */
	if (container) {
		if (JSON_STRING_VALUE != json_entity_type(container)) {
			err = json_dict_build(err, JSON_STRING_VALUE, "container",
					"'container' is not a JSON string.", -1);
			if (!err)
				goto oom;
		} else {
			s = json_value_str(container)->str;
			inst = ldmsd_plugin_inst_find(s);
			if (!inst) {
				rc = ldmsd_req_buf_append(buf, "Store plugin "
						"instance '%s' not found.", s);
				if (rc < 0)
					goto oom;
				err = json_dict_build(err, JSON_STRING_VALUE,
						"container", buf->buf, -1);
				if (!err)
					goto oom;
			} else {
				*_inst = inst;
			}
		}
	} else {
		*_inst = NULL;
	}

	/* producers */
	rc = __strgp_producers_attr(prdcrs, err, _prdcr_list);
	if (rc)
		goto oom;

	/* metrics */
	rc = __strgp_metrics_attr(metrics, err, _metric_list);
	if (rc)
		goto oom;

	if (0 == json_attr_count(err)) {
		json_entity_free(err);
		err = NULL;
	}

	ldmsd_req_buf_free(buf);
	return err;
oom:
	if (buf)
		ldmsd_req_buf_free(buf);
	if (err)
		json_entity_free(err);
	errno = ENOMEM;
	return NULL;
}

json_entity_t __strgp_export(ldmsd_cfgobj_t obj)
{
	json_entity_t query, l, i;
	ldmsd_regex_ent_t match;
	struct ldmsd_strgp_metric *metric;
	ldmsd_strgp_t strgp = (ldmsd_strgp_t)obj;

	query = ldmsd_cfgobj_query_result_new(obj);
	if (!query)
		goto oom;
	query = json_dict_build(query,
			JSON_STRING_VALUE, "container", strgp->inst->inst_name,
			JSON_STRING_VALUE, "schema", strgp->schema,
			JSON_LIST_VALUE, "producer_filters", -2,
			JSON_LIST_VALUE, "metrics", -2,
			-1);

	/* producers */
	l = json_value_find(query, "producer_filters");
	for (match = ldmsd_strgp_prdcr_first(strgp); match;
			match = ldmsd_strgp_prdcr_next(match)) {
		i = json_entity_new(JSON_STRING_VALUE, match->regex_str);
		if (!i)
			goto oom;
		json_item_add(l, i);
	}

	/* metrics */
	l = json_value_find(query, "metrics");
	for (metric = ldmsd_strgp_metric_first(strgp); metric;
			metric = ldmsd_strgp_metric_next(metric)) {
		i = json_entity_new(JSON_STRING_VALUE, metric->name);
		if (!i) {
			json_entity_free(query);
			goto oom;
		}

		json_item_add(l, i);
	}

	return query;
oom:
	return NULL;
}

json_entity_t ldmsd_strgp_export(ldmsd_cfgobj_t obj)
{
	json_entity_t query = __strgp_export(obj);
	if (!query)
		return NULL;
	return ldmsd_result_new(0, NULL, query);
}

json_entity_t ldmsd_strgp_query(ldmsd_cfgobj_t obj)
{
	ldmsd_strgp_t strgp = (ldmsd_strgp_t)obj;
	json_entity_t query = __strgp_export(obj);
	if (!query)
		return NULL;
	query = json_dict_build(query,
			JSON_STRING_VALUE, "state", ldmsd_strgp_state_str(strgp->state),
			-1);
	if (!query)
		return NULL;
	return ldmsd_result_new(0, NULL, query);
}

json_entity_t ldmsd_strgp_update(ldmsd_cfgobj_t obj, short enabled,
					json_entity_t dft, json_entity_t spc)
{
	json_entity_t err = NULL;
	char *schema;
	int perm;
	ldmsd_plugin_inst_t inst;
	struct ldmsd_strgp_prdcr_list *prdcr_list;
	struct ldmsd_strgp_metric_list *metric_list;
	ldmsd_strgp_t strgp = (ldmsd_strgp_t)obj;
	ldmsd_req_buf_t buf;

	if (obj->enabled && enabled)
		return ldmsd_result_new(EBUSY, NULL, NULL);

	buf = ldmsd_req_buf_alloc(512);
	if (!buf)
		goto oom;

	err = __strgp_attr_get(dft, spc, &inst, &schema, &perm, &prdcr_list, &metric_list);

	if (!err && (ENOMEM == errno))
		goto oom;
	if (err)
		ldmsd_result_new(EINVAL, NULL, err);

	/* container cannot be changed */
	if (inst) {
		err = json_dict_build(err,
				JSON_STRING_VALUE, "container",
					"The container cannot be changed.",
				-1);
		if (!err)
			goto oom;
	}

	/* schema cannot be changed */
	if (schema) {
		err = json_dict_build(err,
				JSON_STRING_VALUE, "container",
					"The schema cannot be changed.",
				-1);
		if (!err)
			goto oom;
	}

	/* permission cannot be changed */
	if (LDMSD_ATTR_NA != perm) {
		err = json_dict_build(err,
				JSON_STRING_VALUE, "container",
					"The permission cannot be changed.",
				-1);
		if (!err)
			goto oom;
	}

	/* Attribute errors */
	if (err)
		return ldmsd_result_new(EINVAL, 0, err);

	/* producers */
	if (prdcr_list) {
		__strgp_prdcr_list_free(strgp->prdcr_list);
		strgp->prdcr_list = prdcr_list;
	}

	/* metrics */
	if (metric_list) {
		__strgp_metric_list_free(strgp->metric_list);
		strgp->metric_list = metric_list;
	}

	obj->enabled = ((0 <= enabled)?enabled:obj->enabled);

	ldmsd_req_buf_free(buf);
	return ldmsd_result_new(0, NULL, NULL);
oom:
	if (buf)
		ldmsd_req_buf_free(buf);
	return NULL;
}

ldmsd_strgp_t
__strgp_new(const char *name, ldmsd_plugin_inst_t inst, char *schema,
		struct ldmsd_strgp_prdcr_list *prdcr_list,
		struct ldmsd_strgp_metric_list *metric_list,
		uid_t uid, gid_t gid, int perm, short enabled)
{
	struct ldmsd_strgp *strgp;

	ev_worker_t worker = NULL;
	ev_t start_ev, stop_ev;
	start_ev = stop_ev = NULL;
	char worker_name[PATH_MAX];

	errno = ENOMEM;
	strgp = (struct ldmsd_strgp *)
		ldmsd_cfgobj_new(name, LDMSD_CFGOBJ_STRGP,
				 sizeof *strgp, ldmsd_strgp___del,
				 ldmsd_strgp_update,
				 ldmsd_cfgobj_delete,
				 ldmsd_strgp_query,
				 ldmsd_strgp_export,
				 ldmsd_strgp_enable,
				 ldmsd_strgp_disable,
				 uid, gid, perm, enabled);
	if (!strgp)
		goto err0;

	strgp->schema = schema;
	strgp->inst = inst;

	if (!prdcr_list) {
		strgp->prdcr_list = malloc(sizeof(*strgp->prdcr_list));
		if (!strgp->prdcr_list)
			goto err1;
		LIST_INIT(strgp->prdcr_list);
	} else {
		strgp->prdcr_list = prdcr_list;
	}
	strgp->prdcr_list = prdcr_list;

	if (!metric_list) {
		strgp->metric_list = malloc(sizeof(*strgp->metric_list));
		if (!strgp->metric_list)
			goto err1;
		TAILQ_INIT(strgp->metric_list);
	} else {
		strgp->metric_list = metric_list;
	}

	strgp->state = LDMSD_STRGP_STATE_STOPPED;
	strgp->update_fn = strgp_update_fn;

	start_ev = ev_new(strgp_start_type);
	if (!start_ev) {
		ldmsd_log(LDMSD_LERROR,
			  "%s: error %d creating %s event\n",
			  __func__, errno, ev_type_name(strgp_start_type));
		goto err1;
	}

	stop_ev = ev_new(strgp_stop_type);
	if (!stop_ev) {
		ldmsd_log(LDMSD_LERROR,
			  "%s: error %d creating %s event\n",
			  __func__, errno, ev_type_name(strgp_stop_type));
		goto err2;
	}

	snprintf(worker_name, PATH_MAX, "strgp:%s", name);
	worker = ev_worker_get(worker_name);
	if (!worker) {
		worker = ev_worker_new(worker_name, store_actor);
		if (!worker) {
			ldmsd_log(LDMSD_LERROR,
				  "%s: error %d creating new worker %s\n",
				  __func__, errno, worker_name);
			goto err2;
		}
	}

	strgp->worker = worker;
	strgp->start_ev = start_ev;
	strgp->stop_ev = stop_ev;
	EV_DATA(strgp->start_ev, struct start_data)->entity = strgp;
	EV_DATA(strgp->stop_ev, struct start_data)->entity = strgp;

	ldmsd_strgp_unlock(strgp);
	return strgp;
err2:
	if (start_ev)
		ev_put(start_ev);
	if (stop_ev)
		ev_put(stop_ev);
err1:
	ldmsd_strgp_unlock(strgp);
	ldmsd_strgp_put(strgp);
err0:
	return NULL;
}

json_entity_t ldmsd_strgp_create(const char *name, short enabled, json_entity_t dft,
				json_entity_t spc, uid_t uid, gid_t gid)
{
	char *schema;
	ldmsd_plugin_inst_t inst;
	struct ldmsd_strgp_prdcr_list *prdcr_list;
	struct ldmsd_strgp_metric_list *metric_list;
	int perm;
	ldmsd_strgp_t strgp;
	json_entity_t err;

	err = __strgp_attr_get(dft, spc, &inst, &schema, &perm,
					&prdcr_list, &metric_list);
	if (!err && (ENOMEM == errno))
		goto oom;
	if (err)
		return ldmsd_result_new(EINVAL, 0, err);

	if (LDMSD_ATTR_NA == perm)
		perm = 0770;

	strgp = __strgp_new(name, inst, schema, prdcr_list, metric_list,
						uid, gid, perm, enabled);
	if (!strgp)
		goto oom;

	return ldmsd_result_new(0, NULL, NULL);
oom:
	if (inst)
		ldmsd_plugin_inst_put(inst);
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
	return NULL;
}
