/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2025 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2025 Open Grid Computing, Inc. All rights reserved.
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

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "ovis_util/util.h"
#include "ovis_ref/ref.h"

#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_tenant.h"
#include "ldmsd_jobmgr.h"

extern ovis_log_t config_log;

void __tenant_metric_destroy(struct ldmsd_tenant_metric_s *tmet)
{
	free((char *)tmet->mtempl.name);
	free((char *)tmet->mtempl.unit);
	free(tmet);
}

pthread_mutex_t tenant_def_list_lock = PTHREAD_MUTEX_INITIALIZER;
LIST_HEAD(ldmsd_tenant_def_list, ldmsd_tenant_def_s)
tenant_def_list;

void __tenant_def_destroy(void *arg)
{
	struct ldmsd_tenant_def_s *tdef = (struct ldmsd_tenant_def_s *)arg;
	struct ldmsd_tenant_metric_s *tmet;

	while ((tmet = TAILQ_FIRST(&tdef->mlist))) {
		TAILQ_REMOVE(&tdef->mlist, tmet, ent);
		__tenant_metric_destroy(tmet);
	}

	free(tdef->name);
	free(tdef->rec_def_tmpl);
	free(tdef);
}

static int __init_query_handle(struct ldmsd_tenant_def_s *tdef)
{
	int i, rc;
	ldmsd_tenant_metric_t tmet;
	const char *metrics[tdef->mcount];

	for (i = 0, tmet = TAILQ_FIRST(&tdef->mlist);
	     i < tdef->mcount && tmet; i++, tmet = TAILQ_NEXT(tmet, ent)) {
		metrics[i] = tmet->mtempl.name;
	}

	tdef->query_handle = ldmsd_jobmgr_query_new(tdef->mcount, metrics);
	if (!tdef->query_handle) {
		rc = errno;
		ovis_log(tenant_log, OVIS_LWARN,
			 "Cannot query the job manager metrics. Error: %s\n",
			 strerror(rc));
		return rc;
	}
	return 0;
}

/* Assume that tdata->mlist has been populated. */
static int __init_tenant_def(struct ldmsd_tenant_def_s *tdef)
{
	int i, rc = 0;
	struct ldmsd_tenant_metric_s *tmet;
	ldms_record_t rec_def;
	size_t rec_sz;

	/* Create one extra element as the termining element of the array */
	tdef->rec_def_tmpl = calloc(1, sizeof(struct ldms_metric_template_s) * (tdef->mcount + 1));
	if (!tdef->rec_def_tmpl) {
		ovis_log(NULL, OVIS_LCRIT, "Memory allocation failure.\n");
		return ENOMEM;
	}

	i = 0;
	tmet = TAILQ_FIRST(&tdef->mlist);
	while (tmet) {
		tdef->rec_def_tmpl[i] = tmet->mtempl;
		tmet->__rent_id = i;
		i++;
		tmet = TAILQ_NEXT(tmet, ent);
	}
	assert((i == tdef->mcount) && !tmet); /* If i and tmet must be aligned. */

	int mids[tdef->mcount];
	rec_def = ldms_record_from_template("tenant_rec", tdef->rec_def_tmpl, mids);
	if (!rec_def) {
		ovis_log(NULL, OVIS_LCRIT, "Memory allocation failure.\n");
		return ENOMEM;
	}

	tdef->rec_def_heap_sz = ldms_record_heap_size_get(rec_def);
	rc = __init_query_handle(tdef);
	if (rc) {
		goto out;
	}
	rec_sz = ldms_record_value_size_get(rec_def);
	if (rec_sz != tdef->query_handle->qres_size) {
		ovis_log(tenant_log, OVIS_LERROR,
			 "Tenant '%s' has sizing mismatched between "
			 "the job manager output and the tenant record.\n",
			 tdef->name);
		rc = EINTR;
		assert(rec_sz != tdef->query_handle->qres_size);
		goto out;
	}
	ldms_record_delete(rec_def);
out:
	return rc;
}

struct ldmsd_tenant_def_s *ldmsd_tenant_def_create(const char *name, struct ldmsd_str_list *str_list)
{
	int rc;
	struct ldmsd_tenant_def_s *tdef;
	struct ldmsd_tenant_metric_s *tmet;
	struct ldmsd_str_ent *str;
	ldms_metric_template_t m;

	tdef = ldmsd_tenant_def_find(name);
	if (tdef) {
		ldmsd_tenant_def_put(tdef);
		errno = EEXIST;
		return NULL;
	}

	tdef = calloc(1, sizeof(*tdef));
	if (!tdef) {
		goto enomem;
	}

	ref_init(&tdef->ref, "create", __tenant_def_destroy, tdef);
	TAILQ_INIT(&tdef->mlist);

	tdef->name = strdup(name);
	if (!tdef->name) {
		ref_put(&tdef->ref, "create");
		goto enomem;
	}

	/* Process the attributes */
	TAILQ_FOREACH(str, str_list, entry)
	{
		m = (struct ldms_metric_template_s *)ldmsd_jobmgr_metric_lookup(str->str);
		if (!m) {
			ovis_log(tenant_log, OVIS_LERROR,
				 "Tenant definition '%s': attribute '%s' is unrecognized.\n",
				 name, str->str);
			rc = EINVAL;
			goto err;
		}

		tmet = calloc(1, sizeof(*tmet));
		if (!tmet) {
			goto enomem;
		}

		memcpy(&tmet->mtempl, m, sizeof(tmet->mtempl));
		tmet->mtempl.name = strdup(m->name);
		if (!tmet->mtempl.name) {
			goto enomem;
		}

		tmet->mtempl.unit = strdup((m->unit ? m->unit : ""));
		if (!tmet->mtempl.unit) {
			goto enomem;
		}

		tdef->mcount++;
		TAILQ_INSERT_TAIL(&tdef->mlist, tmet, ent);
	}

	rc = __init_tenant_def(tdef);
	if (rc) {
		ovis_log(tenant_log, OVIS_LERROR,
			 "Failed to create a handle to job manager. Error: %s\n",
			 strerror(rc));
		goto err;
	}

	pthread_mutex_lock(&tenant_def_list_lock);
	LIST_INSERT_HEAD(&tenant_def_list, tdef, ent);
	pthread_mutex_unlock(&tenant_def_list_lock);
	return tdef;

enomem:
	ovis_log(config_log, OVIS_LCRIT, "Memory failure allocation\n");
	errno = ENOMEM;
	return NULL;
err:
	ref_put(&tdef->ref, "create");
	errno = rc;
	return NULL;
}

void ldmsd_tenant_def_free(struct ldmsd_tenant_def_s *tdef)
{
	ref_put(&tdef->ref, "create");
}

struct ldmsd_tenant_def_s *ldmsd_tenant_def_find(const char *name)
{
	struct ldmsd_tenant_def_s *tdef;

	pthread_mutex_lock(&tenant_def_list_lock);
	LIST_FOREACH(tdef, &tenant_def_list, ent)
	{
		if (0 == strcmp(tdef->name, name)) {
			ref_get(&tdef->ref, "find");
			pthread_mutex_unlock(&tenant_def_list_lock);
			return tdef;
		}
	}
	pthread_mutex_unlock(&tenant_def_list_lock);
	return NULL;
}

void ldmsd_tenant_def_put(struct ldmsd_tenant_def_s *tdef)
{
	ref_put(&tdef->ref, "find");
}

size_t ldmsd_tenant_heap_sz_get(struct ldmsd_tenant_def_s *tdef, int num_tenants)
{
	return tdef->rec_def_heap_sz * num_tenants;
}

int ldmsd_tenant_schema_list_add(struct ldmsd_tenant_def_s *tdef,
				 ldms_schema_t schema, int num_tenants,
				 int *_tenant_rec_def_idx, int *_tenants_idx)
{
	int rc = 0;
	size_t heap_sz;
	int rec_def_idx, list_idx;
	ldms_record_t rec_def;
	int mids[tdef->mcount];

	rec_def = ldms_record_from_template(LDMSD_TENANT_REC_DEF_NAME,
					    tdef->rec_def_tmpl, mids);
	if (!rec_def) {
		ovis_log(NULL, OVIS_LCRIT, "Memory allocation failure.\n");
		return ENOMEM;
	}
	heap_sz = num_tenants * ldms_record_heap_size_get(rec_def);

	rec_def_idx = ldms_schema_record_add(schema, rec_def);
	if (rec_def_idx < 0) {
		rc = -rec_def_idx;
		goto out;
	}
	list_idx = ldms_schema_metric_list_add(schema, LDMSD_TENANT_LIST_NAME,
					       NULL, heap_sz);
	if (list_idx < 0) {
		rc = -list_idx;
		goto out;
	}
	if (_tenant_rec_def_idx)
		*_tenant_rec_def_idx = rec_def_idx;
	if (_tenants_idx)
		*_tenants_idx = list_idx;
out:
	ldms_record_delete(rec_def);
	return rc;
}

int ldmsd_tenant_values_sample(struct ldmsd_tenant_def_s *tdef, ldms_set_t set,
			       int tenant_rec_mid, int tenants_mid)
{
	ldms_mval_t tenants, tenant;
	const char *set_name = ldms_set_instance_name_get(set);
	ldmsd_jobmgr_qres_list_t qres_list;
	ldmsd_jobmgr_qres_t qres;

	tenants = ldms_metric_get(set, tenants_mid);
	if (!tenants) {
		ovis_log(NULL, OVIS_LWARN,
			 "Failed to retrieve the current information "
			 "of tenants for set %s.\n",
			 set_name);
		return ENOENT;
	}

	ldms_list_purge(set, tenants);

	qres_list = ldmsd_jobmgr_query_ls(tdef->query_handle);
	if (!qres_list) {
		/* No jobs running. Nothing else to do */
		return 0;
	}

	for (qres = TAILQ_FIRST(&qres_list->tailq); qres; qres = TAILQ_NEXT(qres, entry)) {
		tenant = ldms_record_alloc(set, tenant_rec_mid);
		if (!tenant) {
			return ENOMEM;
		}
		memcpy(tenant->v_rec_inst.rec_data, qres->data, tdef->query_handle->qres_size);
		ldms_list_append_record(set, tenants, tenant);
	}
	ldmsd_jobmgr_qres_list_free(qres_list);
	return 0;
}