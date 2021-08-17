/* -*- c-basic-offset: 8 -*-
 * (C) Copyright 2021-2022 Intel Corporation.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the BSD-type
 * license below:
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * Subject to the terms and conditions of this license, each copyright holder
 * and contributor hereby grants to those receiving rights under this license
 * a perpetual, worldwide, non-exclusive, no-charge, royalty-free, irrevocable
 * (except for failure to satisfy the conditions of this license) patent
 * license to make, have made, use, offer to sell, sell, import, and otherwise
 * transfer this software, where such license applies only to those patent
 * claims, already acquired or hereafter acquired, licensable by such copyright
 * holder or contributor that are necessarily infringed by:
 *
 * (a) their Contribution(s) (the licensed copyrights of copyright holders and
 *     non-copyrightable additions of contributors, in source or binary form)
 *     alone; or
 *
 * (b) combination of their Contribution(s) with the work of authorship to
 *     which such Contribution(s) was added by such copyright holder or
 *     contributor, if, at the time the Contribution is added, such addition
 *     causes such combination to be necessarily infringed. The patent license
 *     shall not apply to any other combinations which include the
 *     Contribution.
 *
 * Except as expressly stated above, no rights or licenses from any copyright
 * holder or contributor is granted under this license, whether expressly, by
 * implication, estoppel or otherwise.
 *
 * DISCLAIMER
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "coll/rbt.h"
#include "ldms.h"
#include "ldmsd.h"

#include "gurt/telemetry_common.h"
#include "gurt/telemetry_consumer.h"
#include "daos_types.h"
#include "daos_prop.h"

#include "daos.h"

#define INSTANCE_NAME_BUF_LEN (DAOS_SYS_NAME_MAX + \
			       DAOS_PROP_MAX_LABEL_BUF_LEN + 17)

static ldms_schema_t pool_schema;
static ldms_schema_t pool_target_schema;

static struct rbt pool_tree;
static struct rbt pool_targets;

static char *pool_gauges[] = {
	NULL,
};

static char *pool_counters[] = {
	"ops/pool_evict",
	"ops/pool_connect",
	"ops/pool_disconnect",
	"ops/pool_query",
	"ops/pool_query_space",
	"ops/cont_open",
	"ops/cont_close",
	"ops/cont_query",
	"ops/cont_create",
	"ops/cont_destroy",
	NULL,
};

static char *pool_target_gauges[] = {
	"block_allocator/frags/large",
	"block_allocator/frags/small",
	"block_allocator/frags/aging",
	"block_allocator/free_blks",
	"entries/dtx_batched_degree",
	NULL,
};

static char *pool_target_counters[] = {
	"ops/dtx_commit",
	"ops/dtx_abort",
	"ops/dtx_check",
	"ops/dtx_refresh",
	"ops/update",
	"ops/fetch",
	"ops/dkey_enum",
	"ops/akey_enum",
	"ops/recx_enum",
	"ops/obj_enum",
	"ops/obj_punch",
	"ops/dkey_punch",
	"ops/akey_punch",
	"ops/key_query",
	"ops/obj_sync",
	"ops/tgt_update",
	"ops/tgt_punch",
	"ops/tgt_dkey_punch",
	"ops/tgt_akey_punch",
	"ops/migrate",
	"ops/ec_agg",
	"ops/ec_rep",
	"ops/compound",
	"restarted",
	"resent",
	"retry",
	"xferred/fetch",
	"xferred/update",
	"block_allocator/alloc/hint",
	"block_allocator/alloc/large",
	"block_allocator/alloc/small",
	"vos_aggregation/obj_scanned",
	"vos_aggregation/akey_scanned",
	"vos_aggregation/dkey_scanned",
	"vos_aggregation/obj_skipped",
	"vos_aggregation/akey_skipped",
	"vos_aggregation/dkey_skipped",
	"vos_aggregation/obj_deleted",
	"vos_aggregation/akey_deleted",
	"vos_aggregation/dkey_deleted",
	"vos_aggregation/uncommitted",
	"vos_aggregation/csum_errors",
	"vos_aggregation/deleted_sv",
	"vos_aggregation/deleted_ev",
	"vos_aggregation/merged_recs",
	"vos_aggregation/merged_size",
	"entries/dtx_batched_total",
	NULL,
};

struct pool_data {
	char		*system;
	char		*pool;
	uint32_t	 rank;
	ldms_set_t	 metrics;
	struct rbn	 pool_node;
};

struct pool_target_data {
	char		*system;
	char		*pool;
	uint32_t	 rank;
	uint32_t	 target;
	ldms_set_t	 metrics;
	struct rbn	 pool_targets_node;
};

int pool_target_schema_is_initialized(void)
{
	if (pool_target_schema != NULL && pool_schema != NULL)
		return 0;
	return -1;
}

void pool_target_schema_fini(void)
{
	log_fn(LDMSD_LDEBUG, SAMP": pool_target_schema_fini()\n");
	if (pool_target_schema != NULL) {
		ldms_schema_delete(pool_target_schema);
		pool_target_schema = NULL;
	}

	if (pool_schema != NULL) {
		ldms_schema_delete(pool_schema);
		pool_schema = NULL;
	}
}

int pool_target_schema_init(void)
{
	ldms_schema_t	p_sch = NULL;
	ldms_schema_t	pt_sch = NULL;
	int		rc, i;
	char		name[256];

	log_fn(LDMSD_LDEBUG, SAMP": pool_target_schema_init()\n");

	p_sch = ldms_schema_new("daos_pool");
	if (p_sch == NULL)
		goto err1;
	rc = ldms_schema_meta_array_add(p_sch, "system", LDMS_V_CHAR_ARRAY,
					DAOS_SYS_NAME_MAX + 1);
	if (rc < 0)
		goto err2;
	rc = ldms_schema_meta_array_add(p_sch, "rank", LDMS_V_U32, 1);
	if (rc < 0)
		goto err2;
	rc = ldms_schema_meta_array_add(p_sch, "pool", LDMS_V_CHAR_ARRAY,
					DAOS_PROP_MAX_LABEL_BUF_LEN);
	if (rc < 0)
		goto err2;

	for (i = 0; pool_gauges[i] != NULL; i++) {
		snprintf(name, sizeof(name), "%s",
			 pool_gauges[i]);
		rc = ldms_schema_metric_add(p_sch, name, LDMS_V_U64);
		if (rc < 0)
			goto err2;
	}

	for (i = 0; pool_counters[i] != NULL; i++) {
		snprintf(name, sizeof(name), "%s",
			 pool_counters[i]);
		rc = ldms_schema_metric_add(p_sch, name, LDMS_V_U64);
		if (rc < 0)
			goto err2;
	}

	pt_sch = ldms_schema_new("daos_pool_target");
	if (pt_sch == NULL)
		goto err2;
	rc = ldms_schema_meta_array_add(pt_sch, "system", LDMS_V_CHAR_ARRAY,
					DAOS_SYS_NAME_MAX + 1);
	if (rc < 0)
		goto err3;
	rc = ldms_schema_meta_array_add(pt_sch, "rank", LDMS_V_U32, 1);
	if (rc < 0)
		goto err3;
	rc = ldms_schema_meta_array_add(pt_sch, "pool", LDMS_V_CHAR_ARRAY,
					DAOS_PROP_MAX_LABEL_BUF_LEN);
	if (rc < 0)
		goto err3;
	rc = ldms_schema_meta_array_add(pt_sch, "target", LDMS_V_U32, 1);
	if (rc < 0)
		goto err3;

	for (i = 0; pool_target_gauges[i] != NULL; i++) {
		snprintf(name, sizeof(name), "%s",
			 pool_target_gauges[i]);
		rc = ldms_schema_metric_add(pt_sch, name, LDMS_V_U64);
		if (rc < 0)
			goto err3;
	}

	for (i = 0; pool_target_counters[i] != NULL; i++) {
		snprintf(name, sizeof(name), "%s",
			pool_target_counters[i]);
		rc = ldms_schema_metric_add(pt_sch, name, LDMS_V_U64);
		if (rc < 0)
			goto err3;
	}

	pool_schema = p_sch;
	pool_target_schema = pt_sch;
	return 0;

err3:
	ldms_schema_delete(pt_sch);
err2:
	ldms_schema_delete(p_sch);
err1:
	log_fn(LDMSD_LERROR, SAMP": daos_pool_target schema creation failed\n");
	return -1;
}

static struct pool_data *pool_create(const char *system, uint32_t rank,
				     const char *pool, const char *instance_name)
{
	char			*key = NULL;
	struct pool_data	*pd = NULL;
	ldms_set_t		 set;
	int			 index;

	pd = calloc(1, sizeof(*pd));
	if (pd == NULL) {
		errno = ENOMEM;
		return NULL;
	}
	pd->system = strndup(system, DAOS_SYS_NAME_MAX + 1);
	if (pd->system == NULL) {
		errno = ENOMEM;
		goto err1;
	}
	pd->pool = strndup(pool, DAOS_PROP_MAX_LABEL_BUF_LEN);
	if (pd->pool == NULL) {
		errno = ENOMEM;
		goto err2;
	}
	pd->rank = rank;

	key = strndup(instance_name, INSTANCE_NAME_BUF_LEN);
	if (key == NULL) {
		errno = ENOMEM;
		goto err3;
	}
	rbn_init(&pd->pool_node, key);

	set = ldms_set_new(instance_name, pool_schema);
	if (set == NULL)
		goto err4;
	index = ldms_metric_by_name(set, "system");
	ldms_metric_array_set_str(set, index, system);
	index = ldms_metric_by_name(set, "rank");
	ldms_metric_set_u32(set, index, rank);
	index = ldms_metric_by_name(set, "pool");
	ldms_metric_array_set_str(set, index, pool);
	ldms_set_publish(set);

	pd->metrics = set;
	return pd;

err4:
	free(key);
err3:
	free(pd->pool);
err2:
	free(pd->system);
err1:
	free(pd);
	return NULL;
}

static struct pool_target_data *pool_target_create(const char *system, uint32_t rank, const char *pool,
						   uint32_t target, const char *instance_name)
{
	char			*key = NULL;
	struct pool_target_data	*ptd = NULL;
	ldms_set_t		 set;
	int			 index;

	ptd = calloc(1, sizeof(*ptd));
	if (ptd == NULL) {
		errno = ENOMEM;
		return NULL;
	}
	ptd->system = strndup(system, DAOS_SYS_NAME_MAX + 1);
	if (ptd->system == NULL) {
		errno = ENOMEM;
		goto err1;
	}
	ptd->pool = strndup(pool, DAOS_PROP_MAX_LABEL_BUF_LEN);
	if (ptd->pool == NULL) {
		errno = ENOMEM;
		goto err2;
	}
	ptd->rank = rank;
	ptd->target = target;

	key = strndup(instance_name, INSTANCE_NAME_BUF_LEN);
	if (key == NULL) {
		errno = ENOMEM;
		goto err3;
	}
	rbn_init(&ptd->pool_targets_node, key);

	set = ldms_set_new(instance_name, pool_target_schema);
	if (set == NULL)
		goto err4;
	index = ldms_metric_by_name(set, "system");
	ldms_metric_array_set_str(set, index, system);
	index = ldms_metric_by_name(set, "rank");
	ldms_metric_set_u32(set, index, rank);
	index = ldms_metric_by_name(set, "pool");
	ldms_metric_array_set_str(set, index, pool);
	index = ldms_metric_by_name(set, "target");
	ldms_metric_set_u32(set, index, target);
	ldms_set_publish(set);

	ptd->metrics = set;
	return ptd;

err4:
	free(key);
err3:
	free(ptd->pool);
err2:
	free(ptd->system);
err1:
	free(ptd);
	return NULL;
}

static void pool_list_free(char **pools, uint64_t npool)
{
	int i;

	if (pools == NULL)
		return;

	for (i = 0; i < npool; i++) {
		if (pools[i] != NULL)
			free(pools[i]);
	}
	free(pools);
}

static void get_pools(struct d_tm_context *ctx, char ***pools, uint64_t *npool)
{
	struct d_tm_nodeList_t	*list = NULL;
	struct d_tm_nodeList_t	*head = NULL;
	struct d_tm_node_t	*node = NULL;
	uint64_t		 num_pools = 0;
	char			**tmp_pools = NULL;
	int			 i, rc;

	if (pools == NULL || npool == NULL)
		return;

	node = d_tm_find_metric(ctx, "/pool");
	if (node == NULL)
		return;

	rc = d_tm_list_subdirs(ctx, &list, node, &num_pools, 1);
	if (rc != DER_SUCCESS)
		return;
	head = list;

	tmp_pools = calloc(num_pools, sizeof(char *));
	if (tmp_pools == NULL)
		return;

	for (i = 0; list && i < num_pools; i++) {
		char *name = NULL;

		node = list->dtnl_node;
		name = d_tm_get_name(ctx, node);
		if (name == NULL)
			goto err1;

		tmp_pools[i] = strndup(name, DAOS_PROP_MAX_LABEL_BUF_LEN);
		if (tmp_pools[i] == NULL)
			goto err1;

		list = list->dtnl_next;
	}
	d_tm_list_free(head);

	*pools = tmp_pools;
	*npool = num_pools;

	return;

err1:
	pool_list_free(tmp_pools, num_pools);
}

static void pool_destroy(struct pool_data *pd)
{
	if (pd == NULL)
		return;

	if (pd->metrics != NULL) {
		ldms_set_unpublish(pd->metrics);
		ldms_set_delete(pd->metrics);
	}

	free(pd->pool);
	free(pd->system);
	free(pd->pool_node.key);
	free(pd);
}

void pools_destroy(void)
{
	struct rbn *rbn;
	struct pool_data *pd;

	if (rbt_card(&pool_tree) > 0)
		log_fn(LDMSD_LDEBUG, SAMP": destroying %lu pool\n", rbt_card(&pool_tree));

	while (!rbt_empty(&pool_tree)) {
		rbn = rbt_min(&pool_tree);
		pd = container_of(rbn, struct pool_data,
				  pool_node);
		rbt_del(&pool_tree, rbn);
		pool_destroy(pd);
	}
}

static void pool_target_destroy(struct pool_target_data *ptd)
{
	if (ptd == NULL)
		return;

	if (ptd->metrics != NULL) {
		ldms_set_unpublish(ptd->metrics);
		ldms_set_delete(ptd->metrics);
	}

	free(ptd->pool);
	free(ptd->system);
	free(ptd->pool_targets_node.key);
	free(ptd);
}

void pool_targets_destroy(void)
{
	struct rbn *rbn;
	struct pool_target_data *ptd;

	if (rbt_card(&pool_targets) > 0)
		log_fn(LDMSD_LDEBUG, SAMP": destroying %lu pool targets\n", rbt_card(&pool_targets));

	while (!rbt_empty(&pool_targets)) {
		rbn = rbt_min(&pool_targets);
		ptd = container_of(rbn, struct pool_target_data, pool_targets_node);
		rbt_del(&pool_targets, rbn);
		pool_target_destroy(ptd);
	}
}

void pool_targets_refresh(const char *system, int num_engines, int num_targets)
{
	int			 i;
	struct rbt		 new_pools;
	struct rbt		 new_pool_targets;
	int			 target;
	char			 instance_name[INSTANCE_NAME_BUF_LEN];

	rbt_init(&new_pools, string_comparator);
	rbt_init(&new_pool_targets, string_comparator);

	for (i = 0; i < num_engines; i++) {
		char		    **pools = NULL;
		uint64_t	     npools = 0;
		struct d_tm_context *ctx = NULL;
		uint32_t	     rank = -1;
		int		     rc, j;

		ctx = d_tm_open(i);
		if (!ctx) {
			log_fn(LDMSD_LDEBUG, SAMP": d_tm_open(%d) failed\n", i);
			continue;
		}

		rc = get_daos_rank(ctx, &rank);
		if (rc != 0) {
			log_fn(LDMSD_LERROR, SAMP": get_daos_rank() for shm %d failed\n", i);
			continue;
		}

		get_pools(ctx, &pools, &npools);
		log_fn(LDMSD_LDEBUG, SAMP": rank %d, ntarget %d, npools %d\n", rank, num_targets, npools);
		/* iterate through all the pools */
		for (j = 0; j < npools; j++) {
			char *pool = pools[j];
			struct rbn *prbn = NULL;
			struct pool_data *pd = NULL;

			if (pool == NULL) {
				log_fn(LDMSD_LERROR, SAMP": rank %d, idx %d: pool is NULL\n", rank, j);
				continue;
			}

			snprintf(instance_name, sizeof(instance_name), "%s/%d/%s", system, rank, pool);

			prbn = rbt_find(&pool_tree, instance_name);
			if (prbn) {
				pd = container_of(prbn, struct pool_data, pool_node);
				rbt_del(&pool_tree, &pd->pool_node);
				//log_fn(LDMSD_LDEBUG, SAMP": found %s\n", pd->pool_node.key);
			} else {
				pd = pool_create(system, rank, pool, instance_name);
				if (pd == NULL) {
					log_fn(LDMSD_LERROR, SAMP": Failed to create pool %s (%s)\n",
							instance_name, strerror(errno));
					continue;
				}
				//log_fn(LDMSD_LDEBUG, SAMP": created %s\n", pd->pool_node.key);
			}
			rbt_ins(&new_pools, &pd->pool_node);

			for (target = 0; target < num_targets; target++) {
				struct rbn *rbn = NULL;
				struct pool_target_data *ptd = NULL;

				snprintf(instance_name, sizeof(instance_name),
					 "%s/%d/%s/%d", system, rank, pool, target);

				rbn = rbt_find(&pool_targets, instance_name);
				if (rbn) {
					ptd = container_of(rbn, struct pool_target_data,
							   pool_targets_node);
					rbt_del(&pool_targets, &ptd->pool_targets_node);
					//log_fn(LDMSD_LDEBUG, SAMP": found %s\n", ptd->pool_targets_node.key);
				} else {
					ptd = pool_target_create(system, rank, pool, target, instance_name);
					if (ptd == NULL) {
						log_fn(LDMSD_LERROR, SAMP": Failed to create pool target %s (%s)\n",
									instance_name, strerror(errno));
						continue;
					}
					//log_fn(LDMSD_LDEBUG, SAMP": created %s\n", ptd->pool_targets_node.key);
				}

				rbt_ins(&new_pool_targets, &ptd->pool_targets_node);
			}
		}

		pool_list_free(pools, npools);
		d_tm_close(&ctx);
	}

	pool_targets_destroy();
	if (!rbt_empty(&new_pool_targets))
		memcpy(&pool_targets, &new_pool_targets, sizeof(struct rbt));
	pools_destroy();
	if (!rbt_empty(&new_pools))
		memcpy(&pool_tree, &new_pools, sizeof(struct rbt));
}

static struct d_tm_node_t *pool_find_metric(struct d_tm_context *ctx, struct pool_data *pd,
					    const char *metric, char *dtm_name_buf, size_t dtm_name_buf_len)
{
	struct d_tm_node_t	*node;

	snprintf(dtm_name_buf, dtm_name_buf_len, "pool/%s/%s", pd->pool, metric);
	node = d_tm_find_metric(ctx, dtm_name_buf);
	if (node == NULL) {
		log_fn(LDMSD_LERROR, SAMP": Failed to find metric %s\n", dtm_name_buf);
		return NULL;
	}

	return node;
}

static void pool_set_u64(struct pool_data *pd, const char *metric, uint64_t val)
{
	int index = -1;

	index = ldms_metric_by_name(pd->metrics, metric);
	if (index < 0) {
		log_fn(LDMSD_LERROR, SAMP": Failed to fetch index for %s\n", metric);
		return;
	}
	ldms_metric_set_u64(pd->metrics, index, val);
}

static void pool_sample(struct d_tm_context *ctx, struct pool_data *pd)
{
	struct d_tm_node_t	*node;
	char			 dtm_name[256];
	const char		*stat_name;
	uint64_t		 cur;
	int			 rc;
	int			 index;
	int			 i;

	/*log_fn(LDMSD_LDEBUG, SAMP": sampling pool %s/%d/%s\n",
	       pd->system, pd->rank, pd->pool);*/

	ldms_transaction_begin(pd->metrics);
	for (i = 0; pool_gauges[i] != NULL; i++) {
		node = pool_find_metric(ctx, pd, pool_gauges[i],
					dtm_name, sizeof(dtm_name));
		if (node == NULL)
			continue;
		rc = d_tm_get_gauge(ctx, &cur, NULL, node);
		if (rc != DER_SUCCESS) {
			log_fn(LDMSD_LERROR, SAMP": Failed to fetch gauge %s\n", dtm_name);
			continue;
		}

		pool_set_u64(pd, pool_gauges[i], cur);
	}

	for (i = 0; pool_counters[i] != NULL; i++) {
		node = pool_find_metric(ctx, pd, pool_counters[i],
					dtm_name, sizeof(dtm_name));
		if (node == NULL)
			continue;
		rc = d_tm_get_counter(ctx, &cur, node);
		if (rc != DER_SUCCESS) {
			log_fn(LDMSD_LERROR, SAMP": Failed to fetch counter %s\n", dtm_name);
			continue;
		}

		pool_set_u64(pd, pool_counters[i], cur);
	}
	ldms_transaction_end(pd->metrics);
}

static struct d_tm_node_t *pool_target_find_metric(struct d_tm_context *ctx, struct pool_target_data *ptd,
						   const char *metric, char *dtm_name_buf, size_t dtm_name_buf_len)
{
	struct d_tm_node_t	*node;

	snprintf(dtm_name_buf, dtm_name_buf_len, "pool/%s/%s/tgt_%d",
			ptd->pool, metric, ptd->target);
	node = d_tm_find_metric(ctx, dtm_name_buf);
	if (node == NULL) {
		log_fn(LDMSD_LERROR, SAMP": Failed to find metric %s\n", dtm_name_buf);
		return NULL;
	}

	return node;
}

static void pool_target_set_u64(struct pool_target_data *ptd, const char *metric, uint64_t val)
{
	int			 index = -1;

	index = ldms_metric_by_name(ptd->metrics, metric);
	if (index < 0) {
		log_fn(LDMSD_LERROR, SAMP": Failed to fetch index for %s\n", metric);
		return;
	}
	ldms_metric_set_u64(ptd->metrics, index, val);
}

static void pool_target_sample(struct d_tm_context *ctx, struct pool_target_data *ptd)
{
	struct d_tm_node_t	*node;
	char			 dtm_name[256];
	const char		*stat_name;
	uint64_t		 cur;
	int			 rc;
	int			 i;

	/*log_fn(LDMSD_LDEBUG, SAMP": sampling pool target %s/%d/%s/%d\n",
	       ptd->system, ptd->rank, ptd->pool, ptd->target);*/

	ldms_transaction_begin(ptd->metrics);
	for (i = 0; pool_target_gauges[i] != NULL; i++) {
		node = pool_target_find_metric(ctx, ptd, pool_target_gauges[i],
					       dtm_name, sizeof(dtm_name));
		if (node == NULL)
			continue;
		rc = d_tm_get_gauge(ctx, &cur, NULL, node);
		if (rc != DER_SUCCESS) {
			log_fn(LDMSD_LERROR, SAMP": Failed to fetch gauge %s\n", dtm_name);
			continue;
		}

		pool_target_set_u64(ptd, pool_target_gauges[i], cur);
	}

	for (i = 0; pool_target_counters[i] != NULL; i++) {
		node = pool_target_find_metric(ctx, ptd, pool_target_counters[i],
					       dtm_name, sizeof(dtm_name));
		if (node == NULL)
			continue;
		rc = d_tm_get_counter(ctx, &cur, node);
		if (rc != DER_SUCCESS) {
			log_fn(LDMSD_LERROR, SAMP": Failed to fetch counter %s\n", dtm_name);
			continue;
		}

		pool_target_set_u64(ptd, pool_target_counters[i], cur);
	}
	ldms_transaction_end(ptd->metrics);
}

void pool_targets_sample(struct d_tm_context *ctx, uint32_t rank)
{
	struct rbn *rbn;

	RBT_FOREACH(rbn, &pool_tree) {
		struct pool_data *pd;

		pd = container_of(rbn, struct pool_data, pool_node);
		if (pd->rank != rank)
			continue;
		pool_sample(ctx, pd);
	}

	RBT_FOREACH(rbn, &pool_targets) {
		struct pool_target_data *ptd;

		ptd = container_of(rbn, struct pool_target_data, pool_targets_node);
		if (ptd->rank != rank)
			continue;
		pool_target_sample(ctx, ptd);
	}
}
