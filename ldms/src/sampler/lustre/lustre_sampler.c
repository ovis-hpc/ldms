/*
 * Copyright (c) 2013 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013 Sandia Corporation. All rights reserved.
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
/**
 * \file lustre_sampler.c
 * \brief Lustre sampler common routine implementation.
 *
 * \author Narate Taerat (narate at ogc dot us)
 */

#include <stdlib.h>
#include <dirent.h>
#include <wordexp.h>
#include "lustre_sampler.h"
#include <assert.h>

static ldmsd_msg_log_f msglog = NULL;

void lustre_sampler_set_msglog(ldmsd_msg_log_f f)
{
	/* We want to set this only once */
	if (!msglog)
		msglog = f;
}

struct lustre_svc_stats* lustre_svc_stats_alloc(const char *path, int mlen)
{
	struct lustre_svc_stats *s = calloc(1, sizeof(*s) +
			sizeof(s->mctxt[0])*mlen);
	if (!s)
		goto err0;
	s->lms.path = strdup(path);
	s->lms.type = LMS_SVC_STATS;
	if (!s->lms.path)
		goto err1;
	s->tv_cur = &s->tv[0];
	s->tv_prev = &s->tv[1];
	return s;
err1:
	free(s);
err0:
	return NULL;
}

struct lustre_single* lustre_single_alloc(const char *path)
{
	struct lustre_single *l = calloc(1, sizeof(*l));
	if (!l)
		goto err0;
	l->lms.path = strdup(path);
	l->lms.type = LMS_SINGLE;
	if (!l->lms.path)
		goto err1;
	return l;
err1:
	free(l);
err0:
	return NULL;
}

void __lms_content_free(struct lustre_metric_src *lms)
{
	free(lms->path);
}

void lustre_svc_stats_free(struct lustre_svc_stats *lss)
{
	__lms_content_free(&lss->lms);
	free(lss);
}

void lustre_single_free(struct lustre_single *ls)
{
	__lms_content_free(&ls->lms);
	free(ls);
}

void lustre_metric_src_list_free(struct lustre_metric_src_list *list)
{
	struct lustre_metric_src *l;
	while (l = LIST_FIRST(list)) {
		LIST_REMOVE(l, link);
		switch (l->type) {
		case LMS_SVC_STATS:
			lustre_svc_stats_free((void*)l);
			break;
		case LMS_SINGLE:
			lustre_single_free((void*)l);
			break;
		}
	}
}

/**
 * \returns 0 on success.
 * \returns Error code on error.
 */
int __add_lss_metric_routine(ldms_schema_t schema, uint64_t udata,
			 const char *metric_name, struct str_map *id_map,
			 const char *key, struct lustre_svc_stats *lss)
{
	char rate_key[128];
	uint64_t id = str_map_get(id_map, key);
	if (!id) {
		/* Unknown IDs ... this is bad */
		return ENOENT;
	}
	snprintf(rate_key, 128, "%s.rate", key);
	uint64_t id_rate = str_map_get(id_map, rate_key);
	/* metric is valid, add it */
	enum ldms_value_type vt = LDMS_V_U64;
	if (strstr(key, ".rate"))
		vt = LDMS_V_F32;
	lss->mctxt[id].metric_idx = ldms_add_metric(schema, metric_name, vt);
	lss->mctxt[id].rate_ref = id_rate;
	lss->mctxt[id].udata = udata;
	return 0;
}

int lms_open_file(struct lustre_metric_src *lms)
{
	if (lms->f)
		return EEXIST;
	wordexp_t p = {0};
	int rc;
	rc = wordexp(lms->path, &p, 0);
	if (rc) {
		if (rc == WRDE_NOSPACE)
			rc = ENOMEM;
		else
			rc = ENOENT;
		goto out;
	}
	if (p.we_wordc > 1) {
		rc = EINVAL;
		goto out;
	}
	lms->f = fopen(p.we_wordv[0], "rt");
	if (!lms->f)
		rc = errno;
out:
	wordfree(&p);
	return rc;
}

int lms_close_file(struct lustre_metric_src *lms)
{
	fclose(lms->f);
	lms->f = NULL;
}

int stats_construct_routine(ldms_schema_t schema,
			    uint64_t comp_id,
			    const char *stats_path,
			    const char *prefix,
			    const char *suffix,
			    struct lustre_metric_src_list *list,
			    char **keys, int nkeys,
			    struct str_map *key_id_map)
{
	char metric_name[128];
	int rc;
	struct lustre_svc_stats *lss =
		lustre_svc_stats_alloc(stats_path, nkeys + 1);
	if (!lss)
		return ENOMEM;
	lss->key_id_map = key_id_map;
	LIST_INSERT_HEAD(list, &lss->lms, link);
	int j;
	for (j=0; j<nkeys; j++) {
		sprintf(metric_name, "%s%s%s", prefix, keys[j], suffix);
		rc = __add_lss_metric_routine(schema, comp_id, metric_name,
				key_id_map, keys[j],
				lss);
		if (rc)
			return rc;
	}
	if ((j = str_map_get(key_id_map, "status")))
		lss->mh_status_idx = lss->mctxt[j].metric_idx;

	return 0;
}

int single_construct_routine(ldms_schema_t schema,
			     uint64_t comp_id,
			     const char *metric_path,
			     const char *prefix,
			     const char *suffix,
			     struct lustre_metric_src_list *list)
{
	const char *name = strrchr(metric_path, '/');
	char metric_name[128];
	if (!name)
		return EINVAL;
	name++;
	struct lustre_single *ls = lustre_single_alloc(metric_path);
	if (!ls)
		goto err0;
	snprintf(metric_name, 128, "%s%s%s", prefix, name, suffix);
	ls->sctxt.metric_idx = ldms_add_metric(schema, metric_name, LDMS_V_U64);
	if (ls->sctxt.metric_idx < 0)
		goto err1;
	LIST_INSERT_HEAD(list, &ls->lms, link);
	ls->sctxt.udata = comp_id;
	return 0;
err1:
	lustre_single_free(ls);
err0:
	return ENOMEM;
}

#define __LBUF_SIZ 256
int __lss_sample(ldms_set_t set, struct lustre_svc_stats *lss)
{
	int rc = 0;
	if (!lss->lms.f) {
		ldms_set_midx_u64(set, lss->mh_status_idx, 0);
		rc = lms_open_file(&lss->lms);
		if (rc)
			goto out;
	}
	ldms_set_midx_u64(set, lss->mh_status_idx, 1);

	fseek(lss->lms.f, 0, SEEK_SET);
	char lbuf[__LBUF_SIZ];
	char name[64];
	char unit[16];
	uint64_t count, min, max, sum, sum2;
	union ldms_value value;
	struct str_map *id_map = lss->key_id_map;
	/* The first line is timestamp, we can ignore that */
	fgets(lbuf, __LBUF_SIZ, lss->lms.f);

	gettimeofday(lss->tv_cur, 0);
	struct timeval dtv;
	timersub(lss->tv_cur, lss->tv_prev, &dtv);
	float dt = dtv.tv_sec + dtv.tv_usec / 1e06;

	while (fgets(lbuf, __LBUF_SIZ, lss->lms.f)) {
		sscanf(lbuf, "%s %lu samples %s %lu %lu %lu %lu",
				name, &count, unit, &min, &max, &sum, &sum2);

		uint64_t id = str_map_get(id_map, name);
		uint64_t rate_id = lss->mctxt[id].rate_ref;

		if (!id)
			continue;

		if (strcmp("[regs]", unit) == 0)
			/* We track the count for reqs */
			value.v_u64 = count;
		else
			/* and track sum for everything else */
			value.v_u64 = sum;

		if (rate_id) {
			uint64_t prev_counter =
				ldms_get_midx_u64(set, lss->mctxt[id].metric_idx);
			union ldms_value rate;
			rate.v_f = (value.v_u64 - prev_counter) / dt;
			ldms_set_midx(set, lss->mctxt[rate_id].metric_idx, &rate);
		}
		ldms_set_midx(set, lss->mctxt[id].metric_idx, &value);
	}

	struct timeval *tmp = lss->tv_cur;
	lss->tv_cur = lss->tv_prev;
	lss->tv_prev = tmp;
out:
	if (lss->lms.f) {
		lms_close_file(&lss->lms);
	}
	return rc;
}

int __single_sample(ldms_set_t set, struct lustre_single *ls)
{
	int rc = 0;
	union ldms_value v = {0};

	if (!ls->lms.f) {
		rc = lms_open_file(&ls->lms);
		if (rc)
			goto out;
	}
	fseek(ls->lms.f, 0, SEEK_SET);
	char line[64], *s;
	s = fgets(line, 64, ls->lms.f);
	if (!s) {
		rc = ENOENT;
		goto out;
	}
	rc = sscanf(s, "%"PRIu64, &v.v_u64);
	if (rc < 1) {
		v.v_u64 = 0;
		rc = errno;
		goto out;
	}
	while (fgets(line, 64, ls->lms.f)) {
		/* read until end of file */
	}
	rc = 0;
out:
	if (ls->lms.f)
		lms_close_file(&ls->lms);
	ldms_set_midx(set, ls->sctxt.metric_idx, &v);
	return rc;
}

int lms_sample(ldms_set_t set, struct lustre_metric_src *lms)
{
	int rc;
	switch (lms->type) {
	case LMS_SVC_STATS:
		rc = __lss_sample(set, (struct lustre_svc_stats*) lms);
		break;
	case LMS_SINGLE:
		rc = __single_sample(set, (struct lustre_single*) lms);
		break;
	default:
		assert(0 == "Unknown type");
	}
	return rc;
}

void free_str_list(struct str_list_head *h)
{
	struct str_list *sl;
	while (sl = LIST_FIRST(h)) {
		LIST_REMOVE(sl, link);
		free(sl->str);
		free(sl);
	}
	free(h);
}

struct str_list_head* construct_str_list(const char *strlist)
{
	struct str_list_head *h = calloc(1, sizeof(*h));
	struct str_list *sl;
	static const char *delim = ",";
	if (!strlist)
		return NULL;

	char *tmp = strdup(strlist);
	char *s = strtok(tmp, delim);
	while (s) {
		sl = calloc(1, sizeof(*sl));
		if (!sl)
			goto err1;
		sl->str = strdup(s);
		if (!sl->str)
			goto err1;
		LIST_INSERT_HEAD(h, sl, link);
		s = strtok(NULL, delim);
	}
	free(tmp);
	return h;
err1:
	free_str_list(h);
err0:
	return NULL;
}

struct str_list_head* construct_dir_list(const char *path)
{
	DIR *d;
	struct dirent *dir;
	d = opendir(path);
	if (!d)
		goto err0;
	struct str_list_head *h = calloc(1, sizeof(*h));
	struct str_list *sl;
	while (dir = readdir(d)) {
		if (dir->d_type & DT_DIR) {
			if (strcmp(dir->d_name, ".")==0 ||
					strcmp(dir->d_name, "..")==0)
				continue;
			sl = calloc(1, sizeof(*sl));
			if (!sl)
				goto err1;
			sl->str = strdup(dir->d_name);
			if (!sl->str)
				goto err1;
			LIST_INSERT_HEAD(h, sl, link);
		}
	}
	return h;
err1:
	free_str_list(h);
err0:
	return NULL;
}

/* EOF */
