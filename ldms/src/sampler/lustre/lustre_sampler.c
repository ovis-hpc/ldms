/**
 * Copyright (c) 2013-2016,2018-2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2013-2016,2018-2019 Open Grid Computing, Inc. All rights reserved.
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
 *	Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 *
 *	Redistributions in binary form must reproduce the above
 *	copyright notice, this list of conditions and the following
 *	disclaimer in the documentation and/or other materials provided
 *	with the distribution.
 *
 *	Neither the name of Sandia nor the names of any contributors may
 *	be used to endorse or promote products derived from this software
 *	without specific prior written permission.
 *
 *	Neither the name of Open Grid Computing nor the names of any
 *	contributors may be used to endorse or promote products derived
 *	from this software without specific prior written permission.
 *
 *	Modified source versions must be plainly marked as such, and
 *	must not be misrepresented as being the original software.
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
 */
#include <stdlib.h>
#include <dirent.h>
#include <wordexp.h>
#include <pthread.h>
#include <coll/rbt.h>
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "lustre_sampler.h"
#pragma GCC diagnostic warning "-Wunused-variable"
#include <assert.h>

static ldmsd_msg_log_f msglog = NULL;

uint64_t mount_id = 1;
struct mount_context {
	uint64_t context_id;
	uint64_t mount_id;
	struct rbn rbn;
};

static int cmp_context_id(void *a, const void *b)
{
	uint64_t a_ = *(uint64_t *)a;
	uint64_t b_ = *(uint64_t *)b;
	if (a_ < b_)
		return -1;
	if (a_ > b_)
		return 1;
	return 0;
}

struct rbt context_tree = { .comparator = cmp_context_id };
pthread_mutex_t context_lock = PTHREAD_MUTEX_INITIALIZER;

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
	s->mctxt_map = str_map_create(1021);
	if (!s->mctxt_map)
		goto err1;
	s->tv_cur = &s->tv[0];
	s->tv_prev = &s->tv[1];
	s->mlen = mlen;
	return s;
err1:
	lustre_svc_stats_free(s);
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
	if (lms->path) {
		free(lms->path);
		lms->path = NULL;
	}
}

void lustre_svc_stats_free(struct lustre_svc_stats *lss)
{
	__lms_content_free(&lss->lms);
	if (lss->mctxt_map) {
		str_map_free(lss->mctxt_map);
		lss->mctxt_map = NULL;
	}
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
	while ((l = LIST_FIRST(list))) {
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
int __add_lss_metric_routine(ldms_schema_t schema,
			 const char *metric_name,
			 struct lustre_metric_ctxt *ctxt,
			 const char *key, struct lustre_svc_stats *lss)
{
	char rate_key[128];
	snprintf(rate_key, 128, "%s.rate", key);
	enum ldms_value_type vt = LDMS_V_U64;
	if (strstr(key, ".rate"))
		vt = LDMS_V_F32;
	ctxt->metric_idx = ldms_schema_metric_add(schema, metric_name, vt);
	ctxt->rate_ref = str_map_get(lss->mctxt_map, rate_key);
	return 0;
}

void lms_close_file(struct lustre_metric_src *lms);

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

	lms->f = fopen(p.we_wordv[0], "r");
	if (!lms->f) {
		rc = errno;
		goto out;
	}

	/* set unbuffered mode */
	rc = setvbuf(lms->f, NULL, _IONBF, 0);
	if (rc) {
		lms_close_file(lms);
	}
out:
	wordfree(&p);
	return rc;
}

void lms_close_file(struct lustre_metric_src *lms)
{
	fclose(lms->f);
	lms->f = NULL;
}

static int del_str(char *str, char *tgt)
{
	int i;
	size_t rep_len = strlen(tgt);
	char *xstr = strstr(str, tgt);
	if (xstr) {
		i = 0;
		do {
			xstr[i] = xstr[rep_len+i];
		} while (xstr[i++] != '\0');
		return 1;
	}
	return 0;
}

/* Replace the mount 0xfff.... context with a number 1..x */
static void fixup_context(char *str)
{
	struct mount_context *context;
	struct rbn *rbn;
	uint64_t key;
	char skey[32];
	regex_t re;
	regmatch_t match[4];
	int i, j, rc;

	rc = regcomp(&re, "[[:xdigit:]]{16}.*", REG_EXTENDED);
	if (rc)
		return;
	rc = regexec(&re, str, 4, match, 0);
	if (rc)
		return;
	j = 0;
	for (i = match[0].rm_so; i < match[0].rm_eo; i++,j++)
		skey[j] = str[i];
	skey[j] = '\0';
	key = strtoul(skey, NULL, 16);

	pthread_mutex_lock(&context_lock);
	rbn = rbt_find(&context_tree, &key);
	if (!rbn) {
		context = calloc(1, sizeof(*context));
		context->mount_id = __sync_fetch_and_add(&mount_id, 1);
		context->context_id = key;
		rbn_init(&context->rbn, &context->context_id);
		rbt_ins(&context_tree, &context->rbn);
		rbn = &context->rbn;
	}
	pthread_mutex_unlock(&context_lock);
	context = container_of(rbn, struct mount_context, rbn);

	sprintf(&str[match[0].rm_so], "%02ld", context->mount_id);
	j = match[0].rm_eo;
	for (i = 2 + match[0].rm_so; str[j] != '\0'; i++)
		str[i] = str[j];
	str[i] = '\0';
}

int stats_construct_routine(ldms_schema_t schema,
			    const char *stats_path,
			    const char *prefix,
			    const char *suffix,
			    struct lustre_metric_src_list *list,
			    char **keys, int nkeys)
{
	char *strip_suffix = strdup(suffix);
	char metric_name[128];
	struct lustre_metric_ctxt *ctxt;
	int rc;
	int j;

	/*
	 * Strip out osc.lustre-, llite.lustre-, mdc.lustre-, and mdt.lustre- from
	 * the suffix as these are redundant
	 */
	if (!del_str(strip_suffix, "osc.lustre-"))
		if (!del_str(strip_suffix, "llite.lustre-"))
			if (!del_str(strip_suffix, "mdc.lustre-"))
				del_str(strip_suffix, "mdt.lustre-");

	/*
	 * Replace the mount-ptr (fffff...) with a 2 digit #, i.e.up to 100 lustre mounts
	 */
	fixup_context(strip_suffix);

	struct lustre_svc_stats *lss =
		lustre_svc_stats_alloc(stats_path, nkeys);
	if (!lss) {
		rc = errno;
		goto err0;
	}
	/* initializing str_map with metric context, key[j] :-> mctxt[j] */
	for (j = 0; j < nkeys; j++) {
		rc = str_map_insert(lss->mctxt_map, keys[j],
				    (uint64_t)&lss->mctxt[j]);
		if (rc) {
			goto err1;
		}
	}
	LIST_INSERT_HEAD(list, &lss->lms, link);
	for (j = 0; j < nkeys; j++) {
		sprintf(metric_name, "%s%s%s", prefix, keys[j], strip_suffix);
		rc = __add_lss_metric_routine(schema, metric_name,
					      &lss->mctxt[j], keys[j], lss);
		if (rc)
			goto err1;
	}
	if ((ctxt = (void*)str_map_get(lss->mctxt_map, "status")))
		lss->mh_status_idx = ctxt->metric_idx;
	else
		lss->mh_status_idx = -1;

	free(strip_suffix);
	return 0;

err1:
	free(strip_suffix);
	lustre_svc_stats_free(lss);
err0:
	return rc;
}

int single_construct_routine(ldms_schema_t schema,
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
	ls->sctxt.metric_idx = ldms_schema_metric_add(schema, metric_name, LDMS_V_U64);
	if (ls->sctxt.metric_idx < 0)
		goto err1;
	LIST_INSERT_HEAD(list, &ls->lms, link);
	return 0;
err1:
	msglog(LDMSD_LERROR, "lustre sample: metric add failed for %s\n", metric_name);
	lustre_single_free(ls);
	return EINVAL;
err0:
	msglog(LDMSD_LERROR, "lustre sample: out of memory using %s\n", metric_path);
	return ENOMEM;
}

void __lss_reset(ldms_set_t set, struct lustre_svc_stats *lss)
{
	int i;
	union ldms_value value = {0};
	for (i = 0; i < lss->mlen; i++){
		ldms_metric_set(set, lss->mctxt[i].metric_idx, &value);
	}
}

#define __LBUF_SIZ 256
int __lss_sample(ldms_set_t set, struct lustre_svc_stats *lss)
{
	int rc = 0;

	if (!lss->lms.f) {
		rc = lms_open_file(&lss->lms);
		if (rc)
			goto err;
	}

	rc = fseek(lss->lms.f, 0, SEEK_SET);
	if (rc) {
		rc = errno;
		goto err;
	}

	if (lss->mh_status_idx != -1)
		ldms_metric_set_u64(set, lss->mh_status_idx, 1);

	fseek(lss->lms.f, 0, SEEK_SET);
	char lbuf[__LBUF_SIZ];
	char name[64];
	char unit[16];
	uint64_t n, count, min, max, sum, sum2;
	union ldms_value value;
	/* The first line is timestamp, we can ignore that */
	char *s = fgets(lbuf, __LBUF_SIZ, lss->lms.f);
	if (!s)
		goto err;
	gettimeofday(lss->tv_cur, 0);
	struct timeval dtv;
	timersub(lss->tv_cur, lss->tv_prev, &dtv);
	float dt = dtv.tv_sec + dtv.tv_usec / 1e06;

	while (fgets(lbuf, __LBUF_SIZ, lss->lms.f)) {
		n = sscanf(lbuf, "%s %lu samples %s %lu %lu %lu %lu",
				name, &count, unit, &min, &max, &sum, &sum2);

		struct lustre_metric_ctxt *ctxt =
				(void*)str_map_get(lss->mctxt_map, name);
		if (!ctxt)
			continue;

		struct lustre_metric_ctxt *rate_ctxt = (void*)ctxt->rate_ref;

		/*
		 * From http://wiki.lustre.org/Lustre_Monitoring_and_Statistics_Guide#Stats
		 *
		 * There are 3 variations of a stat line:
		 * - {name} {count of events} samples [{units}]
		 * - {name} {count of events} samples [{units}] {min} {max} {sum}
		 * - {name} {count of events} samples [{units}] {min} {max} {sum} {sum-of-square}
		 */
		if (n >= 6) {
			/* `sum` available, use it */
			value.v_u64 = sum;
		} else if (n >= 3) {
			/* otherwise, use count */
			value.v_u64 = count;
		} else {
			/* bad format */
			ldmsd_log(LDMSD_LWARNING, "lustre sample: "
				  "bad line format: %s\n", lbuf);
			continue;
		}

		if (rate_ctxt) {
			uint64_t prev_counter =
				ldms_metric_get_u64(set, ctxt->metric_idx);
			union ldms_value rate;
			rate.v_f = (value.v_u64 - prev_counter) / dt;
			ldms_metric_set(set, rate_ctxt->metric_idx, &rate);
		}
		ldms_metric_set(set, ctxt->metric_idx, &value);
	}

	struct timeval *tmp = lss->tv_cur;
	lss->tv_cur = lss->tv_prev;
	lss->tv_prev = tmp;

	goto out;

err:
	__lss_reset(set, lss);
	if (lss->mh_status_idx != -1)
		ldms_metric_set_u64(set, lss->mh_status_idx, 0);
	if (lss->lms.f) {
		lms_close_file(&lss->lms);
	}
out:
	return rc;
}

int __single_sample(ldms_set_t set, struct lustre_single *ls)
{
	int rc = 0;
	union ldms_value v = {0};

	if (!ls->lms.f) {
		rc = lms_open_file(&ls->lms);
		if (rc)
			goto err;
	}

	rc = fseek(ls->lms.f, 0, SEEK_SET);
	if (rc) {
		rc = errno;
		goto err;
	}

	fseek(ls->lms.f, 0, SEEK_SET);
	char line[64], *s;
	s = fgets(line, 64, ls->lms.f);
	if (!s) {
		rc = ENOENT;
		goto err;
	}
	rc = sscanf(s, "%"PRIu64, &v.v_u64);
	if (rc < 1) {
		v.v_u64 = 0;
		rc = errno;
		goto err;
	}
	while (fgets(line, 64, ls->lms.f)) {
		/* read until end of file */
	}
	rc = 0;

	goto out;
err:
	if (ls->lms.f)
		lms_close_file(&ls->lms);
out:
	ldms_metric_set(set, ls->sctxt.metric_idx, &v);
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
	while ((sl = LIST_FIRST(h))) {
		LIST_REMOVE(sl, link);
		free(sl->str);
		free(sl);
	}
	free(h);
}

struct str_list_head* construct_str_list(const char *strlist)
{
	if (!strlist) {
		return NULL;
	}
	struct str_list_head *h = calloc(1, sizeof(*h));
	if (!h)
		return NULL;

	struct str_list *sl = NULL;
	static const char *delim = ",";

	char *tmp = strdup(strlist);
	if (!tmp)
		goto err1;
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
		sl = NULL;
	}
	free(tmp);
	return h;
err1:
	if (sl)
		free(sl);
	free_str_list(h);
	return NULL;
}

struct str_list_head* construct_dir_list(const char *path)
{
	DIR *d = NULL;
	struct dirent *dir;
	struct str_list_head *h = calloc(1, sizeof(*h));
	if (!h) {
		errno = ENOMEM;
		goto err0;
	}
	d = opendir(path);
	if (!d)
		goto err0;
	struct str_list *sl;
	while ((dir = readdir(d))) {
		if (dir->d_type == DT_DIR) {
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
	closedir(d);
	return h;
err1:
	if (sl)
		free(sl);
err0:
	if (h)
		free_str_list(h);
	if (d)
		closedir(d);
	return NULL;
}
