/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2010 Sandia Corporation. All rights reserved.
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
 * \file perfevent.c
 * \brief perfevent data provider
 *
 * Reads perf counters.
 */
//FIXME: needs documentation
#include <inttypes.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <linux/perf_event.h>
#include <math.h>
#include "ldms.h"
#include "ldmsd.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))
#endif

#ifndef __linux__ /* modern linux provides either bitness in asm/unistd.h as needed */
#if defined(__i386__)
#include "/usr/include/asm/unistd.h"
#endif

#if defined(__x86_64__)
#include "/usr/include/asm-x86_64/unistd.h"
#endif
#else /* __linux__ */
#include <asm/unistd.h>
#endif /* __linux__ */

struct pe_sample {
	uint64_t value;
	uint64_t time_running;
};

struct pevent {
	struct perf_event_attr attr;
	char *name;		/* name given by the user for this event */
	int pid;
	int cpu;
	int fd;
	uint64_t val;
	uint64_t tstamp;
	int metric_value;
	int metric_mean;
	int metric_variance;
	int metric_stddev;
	double mean;
	double variance;
	double card;		/* samples taken */
	int warmup;
	struct pe_sample sample;
	uint64_t last_time_running;
	uint64_t last_value;
	LIST_ENTRY(pevent) entry;
};
LIST_HEAD(pevent_list, pevent) pevent_list;

struct kw {
	char *token;
	int (*action)(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg);
};

static int kw_comparator(const void *a, const void *b)
{
	struct kw *_a = (struct kw *)a;
	struct kw *_b = (struct kw *)b;
	return strcmp(_a->token, _b->token);
}

static inline int
pe_open(struct perf_event_attr *attr, pid_t pid, int cpu, int group_fd,
	unsigned long flags)
{
        attr->size = sizeof(*attr);
        return syscall(__NR_perf_event_open, attr, pid, cpu,
                       group_fd, flags);
}

static char *setname;
static ldms_set_t set;
static ldms_schema_t schema;
static ldmsd_msg_log_f msglog;

static const char *usage(void)
{
	return  "    config perfevent add_event(name=<name>,\n"
		"                               pid=<pid>,\n"
		"                               cpu=<cpu>,\n"
		"                               type=<event_type>,\n"
		"                               id=<event_id>)\n"
		"            name   - The metric name for the event\n"
		"            pid    - The PID for the process being monitored\n"
		"                     The counter will follow the process to\n"
		"                     whichever CPU/core is in use. Note that\n"
		"                     'pid' and 'cpu' are mutually exclusive.\n"
		"            cpu    - Count this event on the specified CPU.\n"
		"                     This will accumulate events across all PID\n"
		"                     that land on the specified CPU/core. Note\n"
		"                     that 'pid' and 'cpu' are mutually\n"
		"                     exclusive.\n"
		"            type   - The event type.\n"
		"            id     - The event id.\n"
		"    config perfevent ls\n"
		"            - List the currently configured events.\n"
		;
}

/**
 * Specify the textual name that will appear for this event in the metric set.
 * Format: name=%s
 */
static int add_event_name(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg)
{
	struct pevent *pe = arg;
	pe->name = strdup(av_value(avl, "name"));
	return 0;
}

/**
 * Specify the event type
 */
static int add_event_type(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg)
{
	struct pevent *pe = arg;
	pe->attr.type = strtol(av_value(avl, "type"), NULL, 0);
	return 0;
}

/**
 * Specify the pid
 */
static int add_event_pid(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg)
{
	struct pevent *pe = arg;
	pe->pid = strtol(av_value(avl, "pid"), NULL, 0);
	pe->cpu = -1;
	return 0;
}

/**
 * Specify the event id
 */
static int add_event_id(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg)
{
	struct pevent *pe = arg;
	pe->attr.config = strtol(av_value(avl, "id"), NULL, 0);
	return 0;
}

/**
 * Specify the cpuc core
 */
static int add_event_cpu(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg)
{
	struct pevent *pe = arg;
	pe->cpu = strtol(av_value(avl, "cpu"), NULL, 0);
	pe->pid = -1;
	return 0;
}

struct kw add_token_tbl[] = {
	{ "cpu", add_event_cpu },
	{ "id", add_event_id },
	{ "name", add_event_name },
	{ "pid", add_event_pid },
	{ "type", add_event_type },
};

static struct pevent *find_event(char *name)
{
	struct pevent *pe;
	LIST_FOREACH(pe, &pevent_list, entry) {
		if (strcmp(pe->name, name) == 0)
			return pe;
	}
	return NULL;
}

static char metric_name[128];
 static int add_event(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg)
{
	int rc;
	struct pevent *pe;
	int i;

	pe = calloc(1, sizeof *pe);
	if (!pe)
		goto err;

	pe->warmup = 2;
	pe->attr.size = sizeof(pe);
	pe->attr.read_format = PERF_FORMAT_TOTAL_TIME_RUNNING;
	pe->attr.inherit = 1;

	for (i = 0; i < avl->count; i++) {
		struct kw key;
		struct kw *kw;
		char *token;
		char *value;

		token = av_name(avl, i);
		value = av_value_at_idx(avl, i);

		key.token = token;
		kw = bsearch(&key, add_token_tbl, ARRAY_SIZE(add_token_tbl),
			     sizeof(*kw), kw_comparator);
		if (!kw) {
			msglog("Uncrecognized keyword '%s' in "
			       "configuration string.\n", token);
			return -1;
		}
		rc = kw->action(kwl, avl, pe);
		if (rc)
			return rc;
	}
	if (!pe->name) {
		msglog("An event name must be specifed.\n");
		goto err;
	}
	pe->fd = pe_open(&pe->attr, pe->pid, pe->cpu, -1, 0);
	if (pe->fd < 0) {
		msglog("Error adding event '%s', errno %d\n", pe->name, pe->fd);
		goto err;
	}
	sprintf(metric_name, "%s/%s", pe->name, "value");
	pe->metric_value = ldms_add_metric(schema, metric_name, LDMS_V_U64);
	if (!pe->metric_value) {
		msglog("Could not create the metric for event '%s'\n",
		       metric_name);
		goto err;
	}
	sprintf(metric_name, "%s/%s", pe->name, "mean");
	pe->metric_mean = ldms_add_metric(schema, metric_name, LDMS_V_U64);
	if (!pe->metric_mean) {
		msglog("Could not create the metric for event '%s'\n",
		       metric_name);
		goto err;
	}
	sprintf(metric_name, "%s/%s", pe->name, "variance");
	pe->metric_variance = ldms_add_metric(schema, metric_name, LDMS_V_U64);
	if (!pe->metric_variance) {
		msglog("Could not create the metric for event '%s'\n",
		       metric_name);
		goto err;
	}
	sprintf(metric_name, "%s/%s", pe->name, "stddev");
	pe->metric_stddev = ldms_add_metric(schema, metric_name, LDMS_V_U64);
	if (!pe->metric_stddev) {
		msglog("Could not create the metric for event '%s'\n",
		       metric_name);
		goto err;
	}
	LIST_INSERT_HEAD(&pevent_list, pe, entry);
	return 0;
 err:
	if (pe->name)
		free(pe->name);
	free(pe);
	return -1;
}

static int del_event(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg)
{
	char *name = av_value(avl, "name");
	struct pevent *pe = find_event(name);
	if (pe) {
		LIST_REMOVE(pe, entry);
		close(pe->fd);
		free(pe->name);
		free(pe);
	}
	return 0;
}

static int list(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg)
{
	struct pevent *pe;
	msglog("%-24s %8s %8s %8s %8s %16s\n",
	       "Name", "Pid", "Cpu", "Fd", "Type", "Event");
	msglog("%-24s %8s %8s %8s %8s %16s\n",
	       "------------------------",
	       "--------", "--------", "--------",
	       "--------", "----------------");
	msglog("Name Fd Type Config\n");
	LIST_FOREACH(pe, &pevent_list, entry) {
		msglog("%-24s %8d %8d %8d %8d %16llx\n",
		       pe->name, pe->pid, pe->cpu,
		       pe->fd, pe->attr.type, pe->attr.config);
	}
	return 0;
}

static int init(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg)
{
	/* Create the metric set */
	char *av = av_value(avl, "set");
	if (!av)
		return EINVAL;
	setname = strdup(av);
	schema = ldms_create_schema("perfevent");
	return 0;
}

struct kw kw_tbl[] = {
	{ "add", add_event },
	{ "del", del_event },
	{ "init", init },
	{ "ls", list },
};

static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	struct kw *kw;
	struct kw key;
	int rc;
	char *action = av_value(avl, "action");

	if (!action)
		goto err0;

	key.token = action;
	kw = bsearch(&key, kw_tbl, ARRAY_SIZE(kw_tbl),
		     sizeof(*kw), kw_comparator);
	if (!kw)
		goto err1;

	rc = kw->action(kwl, avl, NULL);
	if (rc)
		goto err2;
	return 0;
 err0:
	msglog(usage());
	goto err2;
 err1:
	msglog("Invalid configuration keyword '%s'\n", action);
 err2:
	return 0;
}

static ldms_set_t get_set()
{
	return set;
}

static int sample(void)
{
	int rc;
	struct pevent *pe;
	if (!set) {
		rc = ldms_create_set(setname, schema, &set);
		if (!set)
			return rc;
	}
	ldms_begin_transaction(set);
	LIST_FOREACH(pe, &pevent_list, entry) {
		pe->last_value = pe->sample.value;
		rc = read(pe->fd, &pe->sample, sizeof(pe->sample));
		if (rc != sizeof(pe->sample)) {
			msglog("Error %d sampling event '%s'\n",
			       errno, pe->name);
			continue;
		}
		if (pe->warmup) {
			/* We need to be sure we have a 'last_value'
			   that was sampled from a complete
			   interval */
			pe->warmup--;
			continue;
		}
		double value, diff;
		value = pe->sample.value - pe->last_value;
		pe->mean = ((pe->mean * pe->card) + value) / (pe->card + 1.0);
		diff = value - pe->mean;
		pe->variance = ((pe->variance * (pe->card * pe->card)) + (diff * diff));
		pe->card =  pe->card + 1.0;
		pe->variance = pe->variance / (pe->card * pe->card);

		ldms_set_midx_u64(set, pe->metric_value, value);
		ldms_set_midx_u64(set, pe->metric_mean, pe->mean);
		ldms_set_midx_u64(set, pe->metric_variance, pe->variance);
		ldms_set_midx_u64(set, pe->metric_stddev, sqrt(pe->variance));
	}
	ldms_end_transaction(set);
 	return 0;
}

static void term(void)
{
	if (schema)
		ldms_destroy_schema(schema);
	schema = NULL;
	if (set)
		ldms_destroy_set(set);
	set = NULL;
}

static struct ldmsd_sampler pe_plugin = {
	.base = {
		.name = "perfevent",
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
	return &pe_plugin.base;
}
