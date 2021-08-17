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

#include "mocks.h"
#include "ldms.h"

/* As used by the daos telemetry library, this type is opaque and
 * used for internal recordkeeping. Since we are mocking out the
 * telemetry API, we can use it for our own purposes.
 */
struct d_tm_context {
	int     rank;
};

struct d_tm_context *
d_tm_open(int id)
{
	struct d_tm_context *ctx;

	ctx = calloc(1, sizeof(*ctx));
	if (ctx == NULL)
		return NULL;

	ctx->rank = id;
	return ctx;
}

void
d_tm_close(struct d_tm_context **ctx)
{
	if (ctx == NULL || *ctx == NULL)
		return;

	free(*ctx);
	*ctx = NULL;
}

static struct d_tm_metric_t dummy_metric = {0};
static struct d_tm_node_t dummy_node = {0};
static char *dummy_pool = "dummy_pool";

struct d_tm_node_t *
d_tm_find_metric(struct d_tm_context *ctx, char *path)
{
	struct d_tm_node_t *node = &dummy_node;

	node->dtn_name = path;
	if (!strcmp(path, "/pool")) {
		node->dtn_type = D_TM_DIRECTORY;
	} else {
		node->dtn_type = D_TM_GAUGE;
		node->dtn_metric = &dummy_metric;
		node->dtn_metric->dtm_data.value = ctx->rank;
	}

	return node;
}

int
d_tm_list_subdirs(struct d_tm_context *ctx, struct d_tm_nodeList_t **head,
		  struct d_tm_node_t *node, uint64_t *node_count,
		  int max_depth)
{
	int     rc;

	if (node->dtn_type != D_TM_DIRECTORY)
		return -DER_INVAL;

	if (!strcmp(node->dtn_name, "/pool")) {
		node->dtn_name = dummy_pool;
		rc = d_tm_list_add_node(&dummy_node, head);
		if (rc)
			return rc;
		*node_count = 1;
	} else {
		*head = NULL;
		*node_count = 0;
	}
	return 0;
}

void
d_tm_list_free(struct d_tm_nodeList_t *list)
{
	struct d_tm_nodeList_t *head = NULL;

	while (list) {
		head = list->dtnl_next;
		free(list);
		list = head;
	}
}

char *
d_tm_get_name(struct d_tm_context *ctx, struct d_tm_node_t *node)
{
	if (ctx == NULL || node == NULL)
		return NULL;

	return node->dtn_name;
}

int
d_tm_get_counter(struct d_tm_context *ctx, uint64_t *value,
		 struct d_tm_node_t *node)
{
	if (ctx == NULL || node == NULL || value == NULL)
		return -1;

	*value = node->dtn_metric->dtm_data.value;
	return 0;
}

int
d_tm_get_gauge(struct d_tm_context *ctx, uint64_t *value,
	       struct d_tm_stats_t *stats, struct d_tm_node_t *node)
{
	if (ctx == NULL || node == NULL || value == NULL)
		return -1;

	*value = node->dtn_metric->dtm_data.value;
	return 0;
}

/* stub these out so that we can call the plugin outside of ldmsd */
int ldmsd_set_register(ldms_set_t set, const char *plugin_name) { return 0; }
void ldmsd_set_deregister(const char *inst_name, const char *plugin_name) {}
