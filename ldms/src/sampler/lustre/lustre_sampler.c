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
 */

#include <stdlib.h>
#include <dirent.h>
#include <wordexp.h>
#include "lustre_sampler.h"

static ldmsd_msg_log_f msglog = NULL;

void lustre_sampler_set_msglog(ldmsd_msg_log_f f)
{
	/* We want to set this only once */
	if (!msglog)
		msglog = f;
}

struct lustre_svc_stats* lustre_svc_stats_alloc(const char *path, int mlen)
{
	struct lustre_svc_stats *s = calloc(1, sizeof(*s) + sizeof(void*)*mlen);
	if (!s)
		goto err0;
	s->path = strdup(path);
	if (!s->path)
		goto err1;
	return s;
err1:
	free(s);
err0:
	return NULL;
}

void lustre_svc_stats_free(struct lustre_svc_stats *lss)
{
	if (!lss) {
		if (msglog) {
			msglog("%s:%d null lss received\n",__FILE__,__LINE__);
		}
		exit(-1);
	}
	free(lss->name);
	free(lss->path);
	free(lss);
}

void lustre_svc_stats_list_free(struct lustre_svc_stats_head *h)
{
	struct lustre_svc_stats *l;
	while ((l = LIST_FIRST(h))) {
		LIST_REMOVE(l, link);
		lustre_svc_stats_free(l);
	}
}

/**
 * \returns 0 on success.
 * \returns Error code on error.
 */
int __add_metric_routine(ldms_set_t set, uint64_t udata,
			 const char *metric_name, struct str_map *id_map,
			 const char *key, struct lustre_svc_stats *lss)
{
	uint64_t id = str_map_get(id_map, key);
	if (!id) {
		/* Unknown IDs ... this is bad */
		return ENOENT;
	}
	/* metric is valid, add it */
	ldms_metric_t metric = ldms_add_metric(set, metric_name, LDMS_V_U64);
	lss->metrics[id] = metric;
	ldms_set_user_data(metric, udata);
	return 0;
}

int lss_open_file(struct lustre_svc_stats *lss)
{
	if (lss->f)
		return EEXIST;
	wordexp_t p = {0};
	int rc;
	rc = wordexp(lss->path, &p, 0);
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
	lss->f = fopen(p.we_wordv[0], "rt");
	if (!lss->f)
		rc = errno;
out:
	wordfree(&p);
	return rc;
}

void lss_close_file(struct lustre_svc_stats *lss)
{
	if (lss) {
		fclose(lss->f);
		lss->f = NULL;
	}
}

int stats_construct_routine(ldms_set_t set,
			    uint64_t comp_id,
			    const char *stats_path,
			    const char *prefix,
			    const char *suffix,
			    struct lustre_svc_stats_head *stats_head,
			    char **keys, int nkeys,
			    struct str_map *key_id_map)
{
	char metric_name[LUSTRE_NAME_MAX];
	int rc;
	struct lustre_svc_stats *lss =
		lustre_svc_stats_alloc(stats_path, nkeys + 1);
	if (!lss)
		return ENOMEM;
	lss->key_id_map = key_id_map;
	LIST_INSERT_HEAD(stats_head, lss, link);
	int j;
	for (j=0; j<nkeys; j++) {
		snprintf(metric_name, LUSTRE_NAME_MAX, "%s%s%s", prefix,
			keys[j], suffix);
		rc = __add_metric_routine(set, comp_id, metric_name,
				key_id_map, keys[j],
				lss);
		if (rc)
			return rc;
	}
	return 0;
}

#define __LBUF_SIZ 256
int lss_sample(struct lustre_svc_stats *lss)
{
	int rc = 0;
	if (!lss->f) {
		rc = lss_open_file(lss);
		if (rc)
			goto out;
	}
	if (fseek(lss->f, 0, SEEK_SET))
		goto out;
	char lbuf[__LBUF_SIZ];
	char name[64];
	char unit[16];
	uint64_t count, min, max, sum, sum2;
	union ldms_value value;
	struct str_map *id_map = lss->key_id_map;
	/* The first line is timestamp, we can ignore that */
	if (!fgets(lbuf, __LBUF_SIZ, lss->f))
		goto out;

	while (fgets(lbuf, __LBUF_SIZ, lss->f)) {
		sscanf(lbuf, "%s %lu samples %s %lu %lu %lu %lu",
				name, &count, unit, &min, &max, &sum, &sum2);

		uint64_t id = str_map_get(id_map, name);

		if (!id)
			continue;
		if (strcmp("[regs]", unit) == 0)
			/* We track the count for reqs */
			value.v_u64 = count;
		else
			/* and track sum for everything else */
			value.v_u64 = sum;
		ldms_set_metric(lss->metrics[id], &value);
	}
out:
	return rc;
}

void free_str_list(struct str_list_head *h)
{
	if (!h) {
		if ( msglog ) {
			msglog("%s:%d: null given to free_str_list.\n");
		}
		exit(-1);
	}
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
	struct str_list *sl;
	static const char *delim = ",";
	char *saveptr = NULL;
	if (!strlist)
		return NULL;

	struct str_list_head *h = calloc(1, sizeof(*h));
	char *tmp = strdup(strlist);
	char *s = strtok_r(tmp, delim, &saveptr);
	while (s) {
		sl = calloc(1, sizeof(*sl));
		if (!sl)
			goto err1;
		sl->str = strdup(s);
		if (!sl->str)
			goto err1;
		LIST_INSERT_HEAD(h, sl, link);
		s = strtok_r(NULL, delim, &saveptr);
	}
	free(tmp);
	return h;
err1:
	free(sl);
	free_str_list(h);
	free(tmp);
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
	if (!h)
		goto err0;
	struct str_list *sl;
	while ((dir = readdir(d))) {
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
	closedir(d);
	return h;
err1:
	free(sl);
	free_str_list(h);
err0:
	closedir(d);
	return NULL;
}

/* EOF */
