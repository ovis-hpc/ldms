/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2012 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2012 Sandia Corporation. All rights reserved.
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
 * \file sysclassib.c
 * \brief reads from
 * 1) all files in: /sys/class/infiniband/(*)/ports/(*)/counters/
 *    which have well-known names
 * 2) /sys/class/infiniband/(*)/ports/(*)/rate
 *
 * for older kernels, the filehandles have to be opened and closed each time;
 * for newer kernels (>= 2.6.35) they do not.
 *
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/utsname.h>
#include <pthread.h>
#include <wordexp.h>
#include <fnmatch.h>
#include <sys/time.h>
#include "ldms.h"
#include "ldmsd.h"

/**
 * sysclassib metric handle
 */
struct scib_metric {
	int is_rate; /**< 1 if this metric is a rate */
	char *path;
	struct scib_metric *rate_ref; /**< reference to the rate metric */

	/** Previous value (raw) for rate calculation. */
	union ldms_value prev_value;
	char *metric_name;
	FILE *f;
	ldms_metric_t metric;
	struct scib_comp *comp;
	LIST_ENTRY(scib_metric) entry;
};

struct scib_comp {
	char *card_port;
	uint64_t comp_id;
	LIST_ENTRY(scib_comp) entry;
};

LIST_HEAD(scib_mlist, scib_metric) scib_mlist = LIST_HEAD_INITIALIZER(scib_mlist);
LIST_HEAD(scib_clist, scib_comp) scib_clist = LIST_HEAD_INITIALIZER(scib_clist);

static char *counter_paths = "/sys/class/infiniband/*/ports/*/counters/*";
static char *rate_paths = "/sys/class/infiniband/*/ports/*/rate";

ldms_set_t set;
ldmsd_msg_log_f msglog;

#define V1 2
#define V2 6
#define V3 35

int newerkernel;
uint64_t comp_id;

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))
#endif

struct timeval tv[2];
struct timeval *tv_now = &tv[0];
struct timeval *tv_prev = &tv[1];

/**
 * Version comparison.
 *
 * \returns 1 if (a.b.c) > (x.y.z)
 * \returns 0 if (a.b.c) == (x.y.z)
 * \returns -1 if (a.b.c) < (x.y.z)
 */
int compare3(int a, int b, int c, int x, int y, int z)
{
	if (a < x)
		return -1;
	if (a > x)
		return 1;
	if (b < y)
		return -1;
	if (b > y)
		return 1;
	if (c < z)
		return -1;
	if (c > z)
		return 1;
	return 0;
}

void scib_metric_free(struct scib_metric *m)
{
	if (m->metric)
		ldms_metric_release(m->metric);
	if (m->f)
		fclose(m->f);
	if (m->path)
		free(m->path);
	if (m->metric_name)
		free(m->metric_name);
	free(m);
}

/**
 * Obtain scib_comp from \c path.
 */
struct scib_comp *get_scib_comp(const char *card, int port)
{
	char ckey[128];
	sprintf(ckey, "%s.%d", card, port);
	struct scib_comp *c;
	LIST_FOREACH(c, &scib_clist, entry) {
		if (strcmp(c->card_port, ckey) == 0)
			break;
	}

	if (!c) {
		c = calloc(1, sizeof(*c));
		if (!c)
			goto err0;
		c->card_port = strdup(ckey);
		if (!c->card_port)
			goto err1;
		LIST_INSERT_HEAD(&scib_clist, c, entry);
	}

	return c;

err1:
	free(c);
err0:
	return NULL;
}

struct scib_metric *create_metric_with_size(const char *path, size_t *meta_sz,
					    size_t *data_sz)
{
	char metric_name[128];
	char *metric;
	char card[64];
	int port;
	int n, rc;
	n = sscanf(path, "/sys/class/infiniband/%[^/]/ports/%d", card, &port);
	if (n != 2) {
		errno = EINVAL;
		goto err0;
	}
	metric = strrchr(path, '/');
	if (metric) {
		metric++;
	} else {
		errno = EINVAL;
		goto err0;
	}
	struct scib_metric *m = calloc(1, sizeof(*m));
	if (!m)
		goto err0;
	m->path = strdup(path);
	if (!m->path)
		goto err1;

	m->f = fopen(path, "r");
	if (!m->f)
		goto err2;

	snprintf(metric_name, 128, "ib.%s#%s.%d", metric, card, port);
	m->metric_name = strdup(metric_name);
	if (!m->metric_name)
		goto err3;
	m->comp = get_scib_comp(card, port);
	if (!m->comp)
		goto err4;
	/* m->comp is shared, don't free it on error */

	rc = ldms_get_metric_size(metric_name, LDMS_V_U64, meta_sz, data_sz);
	if (rc) {
		errno = rc;
		goto err4;
	}

	return m;

err4:
	free(m->metric_name);
err3:
	fclose(m->f);
err2:
	free(m->path);
err1:
	free(m);
err0:
	return NULL;
}

struct scib_metric *create_rate_metric_with_size(struct scib_metric *raw,
					size_t *meta_sz, size_t *data_sz)
{
	char metric_name[128];
	char *metric;
	char card[64];
	int port;
	int n, rc;

	struct scib_metric *m = calloc(1, sizeof(*m));
	if (!m)
		goto err0;

	m->is_rate = 1;

	char *sharp = strchr(raw->metric_name, '#');
	if (!sharp) {
		msglog("ERROR: %s: invalid metric name: %s\n.", __func__,
				raw->metric_name);
		errno = EINVAL;
		goto err1;
	}

	snprintf(metric_name, 128, "%.*s.rate%s", sharp - raw->metric_name,
			raw->metric_name, sharp);
	m->metric_name = strdup(metric_name);
	if (!m->metric_name)
		goto err1;
	m->comp = raw->comp;
	if (!m->comp) {
		msglog("ERROR: %s: no component\n", __func__);
		goto err1;
	}
	/* m->comp is shared, don't free it on error */

	rc = ldms_get_metric_size(metric_name, LDMS_V_F, meta_sz, data_sz);
	if (rc) {
		errno = rc;
		goto err1;
	}

	return m;
err1:
	free(m);
err0:
	return NULL;
}

/**
 * \param wild_path The path in wild card format.
 * \param[out] meta_sz mata size that will be used for metrics in \c wild_path
 * \param[out] data_sz data size that will be used for metrics in \c wild_path
 */
int create_metric_set_wild(const char *wild_path, size_t *meta_sz,
			   size_t *data_sz)
{
	wordexp_t p = {0};
	int rc;
	rc = wordexp(wild_path, &p, 0);
	if (rc) {
		if (rc == WRDE_NOSPACE)
			return ENOMEM;
		else
			return ENOENT;
	}
	size_t tmz = 0;
	size_t tdz = 0;
	size_t mz, dz;
	int i;
	for (i = 0; i < p.we_wordc; i++) {
		/* create raw metric */
		struct scib_metric *m = create_metric_with_size(p.we_wordv[i],
								&mz, &dz);
		if (!m) {
			rc = -1;
			goto out;
		}
		LIST_INSERT_HEAD(&scib_mlist, m, entry);
		tmz += mz;
		tdz += dz;

		/* create rate metric */
		struct scib_metric *mr = create_rate_metric_with_size(m, &mz,
									&dz);
		if (!mr) {
			rc = -1;
			goto out;
		}
		m->rate_ref = mr;
		tmz += mz;
		tdz += dz;
	}

	*meta_sz = tmz;
	*data_sz = tdz;
out:
	wordfree(&p);
	return rc;
}

/**
 * \param path The set name (e.g. nid00001/sysclassib)
 */
static int create_metric_set(const char *path)
{
	size_t meta_sz, tot_meta_sz;
	size_t data_sz, tot_data_sz;
	int rc, i, j, metric_count;
	char metric_name[128];
	struct scib_metric *m, *mr;
	struct scib_comp *c;

	tot_meta_sz = 0;
	tot_data_sz = 0;

	rc = create_metric_set_wild(counter_paths, &meta_sz, &data_sz);
	if (rc)
		goto err;
	tot_meta_sz += meta_sz;
	tot_data_sz += data_sz;

	rc = create_metric_set_wild(rate_paths, &meta_sz, &data_sz);
	if (rc)
		goto err;
	tot_meta_sz += meta_sz;
	tot_data_sz += data_sz;

	/* Assign component IDs */
	uint64_t cid = comp_id;
	LIST_FOREACH(c, &scib_clist, entry) {
		c->comp_id = cid;
		cid++;
	}

	/* Create the metric set */
	set = NULL;
	rc = ldms_create_set(path, tot_meta_sz, tot_data_sz, &set);
	if (rc)
		goto err;

	LIST_FOREACH(m, &scib_mlist, entry) {
		m->metric = ldms_add_metric(set, m->metric_name, LDMS_V_U64);
		if (!m->metric)
			goto err;
		ldms_set_user_data(m->metric, m->comp->comp_id);
		mr = m->rate_ref;
		if (mr) {
			mr->metric = ldms_add_metric(set, mr->metric_name,
							LDMS_V_F);
			if (!mr->metric)
				goto err;
			ldms_set_user_data(mr->metric, mr->comp->comp_id);
		}
	}

	rc = 0;
	goto out;

err:
	/* clean up */
	while ((m = LIST_FIRST(&scib_mlist))) {
		LIST_REMOVE(m, entry);
		if (m->rate_ref)
			scib_metric_free(m->rate_ref);
		scib_metric_free(m);
	}

	if (set)
		ldms_destroy_set(set);
out:
	return rc;
}

static const char *usage(void)
{
	return
		"config name=sysclassib component_id=<comp_id> set=<setname>\n"
		"    comp_id     The component id value.\n"
		"    setname     The set name.\n";
}

/**
 * \brief Configuration
 *
 * - config procnetdev action=add iface=eth0
 *  (repeat this for each iface)
 * - config procnetdev action=init component_id=<value> set=<setname>
 *  (init must be after all the ifaces are added since it adds the metric set)
 *
 */
static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{

	int rc = 0;
	char *value;

	value = av_value(avl, "component_id");
	if (value)
		comp_id = strtoull(value, NULL, 0);
	else
		comp_id = 0;

	value = av_value(avl, "set");
	if (!value)
		return EINVAL;

	struct utsname u;
	rc = uname(&u);
	if (rc) {
		msglog("uname error: %m\n");
		goto out;
	}

	int version[3] = {0};
	sscanf(u.release, "%d.%d.%d", &version[0], &version[1], &version[2]);
	if (compare3(version[0], version[1], version[2], V1, V2, V3) >= 0){
		newerkernel = 1;
	} else {
		newerkernel = 0;
	}

	rc = create_metric_set(value);
out:
	return rc;
}

static ldms_set_t get_set()
{
	return set;
}

static int sample(void)
{
	int rc;
	char *s;
	char lbuf[32];
	union ldms_value v, vr;
	int i,j;
	struct timeval *tmp;
	struct timeval tv_diff;

	if (!set){
		msglog("sysclassib: plugin not initialized\n");
		return EINVAL;
	}

	gettimeofday(tv_now, 0);
	timersub(tv_now, tv_prev, &tv_diff);
	struct scib_metric *m;
	LIST_FOREACH(m, &scib_mlist, entry) {
		if (newerkernel) {
			fseek(m->f, 0, SEEK_SET);
		} else {
			fclose(m->f);
			m->f = fopen(m->path, "r");
			if (!m->f) {
				msglog("Could not open the sysclassib file"
						" '%s'\n", m->path);
				return ENOENT;
			}
		}
		s = fgets(lbuf, sizeof(lbuf), m->f);
		if (!s) {
			msglog("Could not read file '%s'\n", m->path);
			return EINVAL;
		}
		rc = sscanf(lbuf, "%"PRIu64 "\n", &v.v_u64);
		if (rc != 1){
			msglog("Error while reading file '%s'\n", m->path);
			return EINVAL;
		}
		ldms_set_metric(m->metric, &v);
		float dt = tv_diff.tv_sec + (tv_diff.tv_usec / 1e06);
		vr.v_f = (v.v_u64 - m->prev_value.v_u64) / dt;
		ldms_set_metric(m->rate_ref->metric, &vr);
		m->prev_value = v;

	}

	tmp = tv_now;
	tv_now = tv_prev;
	tv_prev = tmp;
	return 0;
}

static void term(void){

	struct scib_metric *m;
	while ((m = LIST_FIRST(&scib_mlist))) {
		LIST_REMOVE(m, entry);
		if (m->f)
			fclose(m->f);
		if (m->path)
			free(m->path);
		if (m->metric_name)
			free(m->metric_name);
		free(m);
	}

	if (set)
		ldms_destroy_set(set);
	set = NULL;
}

static struct ldmsd_sampler sysclassib_plugin = {
	.base = {
		.name = "sysclassib",
		.term = term,
		.config = config,
		.usage = usage,
	},
	.get_set = get_set,
	.sample = sample,
};

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	msglog = pf;
	return &sysclassib_plugin.base;
}
