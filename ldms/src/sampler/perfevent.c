/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010-2016,2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2010-2016,2018 Open Grid Computing, Inc. All rights reserved.
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

#include <inttypes.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <linux/perf_event.h>
#include <math.h>
#include "ldms.h"
#include "ldmsd.h"
#include "sampler_base.h"

#define SAMP "perfevent"

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

/* variables for group read */
static int started = 0;
struct event_group {
	int leader;
	int pid;
	int cpu;
	unsigned int eventCounter;
	int *metric_index;
	LIST_ENTRY(event_group) entry;
};
LIST_HEAD(gevent_list, event_group) gevent_list;

static ldms_set_t set;
static ldmsd_msg_log_f msglog;
static base_data_t base;


struct pevent {
	struct perf_event_attr attr;
	char *name;   /* name given by the user for this event */
	int pid;
	int cpu;
	int fd;
	int metric_index;
	int group_index;
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

static inline int pe_open(struct perf_event_attr *attr, pid_t pid, int cpu, int group_fd, unsigned long flags)
{
	attr->size = sizeof(*attr);
	int fd = syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
	return fd;
}

static const char *usage(struct ldmsd_plugin* self)
{
	return
		"    config name=perfevent action=init " BASE_CONFIG_USAGE
		"    config name=perfevent action=del metricname=<string>\n"
		"            - Deletes the specified event.\n"
		"    config name=perfevent action=ls\n"
		"            - List the currently configured events.\n"
		"    config name=perfevent action=add metricname=<string> pid=<int> cpu=<int> type=<int> id=<int>\n"
		"            <metricname>  The metric name for the event\n"
		"            <pid>         The PID for the process being monitored\n"
		"                          The counter will follow the process to\n"
		"                          whichever CPU/core is in use. Note that\n"
		"                          'pid' and 'cpu' are mutually exclusive.\n"
		"            <cpu>         Count this event on the specified CPU.\n"
		"                          This will accumulate events across all PID\n"
		"                          that land on the specified CPU/core. Note\n"
		"                          that 'pid' and 'cpu' are mutually\n"
		"                          exclusive.\n"
		"            <type>        The event type.\n"
		"            <id>          The event id.\n"
		" For more information visit: http://man7.org/linux/man-pages/man2/perf_event_open.2.html\n\n";
}

/**
 * Specify the textual name that will appear for this event in the metric set.
 * Format: metricname=%s
 */
static int add_event_name(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg)
{
	struct pevent *pe = arg;
	pe->name = strdup(av_value(avl, "metricname"));
	if (!pe->name)
		return ENOMEM;
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
	return 0;
}

static struct pevent *find_event(char *name)
{
	struct pevent *pe;
	LIST_FOREACH(pe, &pevent_list, entry) {
		if (strcmp(pe->name, name) == 0)
			return pe;
	}
	return NULL;
}

static struct event_group *find_group(int event_pid, int event_cpu)
{
	struct event_group *eg;
	LIST_FOREACH(eg, &gevent_list, entry) {

		if (eg->pid == -1){
			if(eg->cpu == event_cpu){
				return eg;
			}
		}

		else{
			if(eg->pid == event_pid){
				return eg;
			}
		}
	}
	return NULL;
}

static int add_event(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg)
{
	struct kw add_token_tbl[] = {
		{ "cpu", add_event_cpu },
		{ "id", add_event_id },
		{ "metricname", add_event_name },
		{ "pid", add_event_pid },
		{ "type", add_event_type },
	};

	int rc = -1, i;
	struct pevent *pe;

	if (set) {
		msglog(LDMSD_LERROR, "perfevent: metric set has already been created.\n");
		return EINVAL;
	}

	pe = calloc(1, sizeof *pe);
	if (!pe) {
		msglog(LDMSD_LERROR, "perfevent: failed to allocate perfevent structure.\n");
		rc = ENOMEM;
		goto err;
	}

	pe->attr.size = sizeof(pe->attr);
	/* changed the read format to do the group read */
	pe->attr.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_TOTAL_TIME_RUNNING;
	pe->attr.exclude_kernel = 1;
	pe->attr.exclude_hv = 1;
	pe->pid = -1;
	pe->cpu = -1;

	/* skipping 0=name 1=action */
	for (i = 2; i < avl->count; i++) {
		struct kw key;
		struct kw *kw;
		char *token;
		char *value;

		token = av_name(avl, i);
		value = av_value_at_idx(avl, i);

		key.token = token;
		kw = bsearch(&key, add_token_tbl, ARRAY_SIZE(add_token_tbl), sizeof(*kw), kw_comparator);

		if (!kw) {
			msglog(LDMSD_LERROR, "Unrecognized keyword '%s' in configuration string.\n", token);
			free(pe);
			return -1;
		}
		rc = kw->action(kwl, avl, pe);
		if (rc)
			goto err;
	}
	if (!pe->name) {
		msglog(LDMSD_LERROR, "An event name must be specifed.\n");
		goto err;
	}
	if (pe->cpu == -1 && pe->pid == -1) {
		msglog(LDMSD_LERROR, "Error adding event '%s'\n", pe->name);
		msglog(LDMSD_LERROR, "\tPID and CPU can not be -1");
		goto err;
	}

	pe->attr.disabled = 0;

	struct event_group *current_group = find_group(pe->pid, pe->cpu);
	int group_leader_fd = -1;
	if(current_group == NULL) /* if this is the group group leader */
		pe->attr.disabled = 1; /* disable the group leader */
	else
		group_leader_fd = current_group->leader;

	pe->fd = pe_open(&pe->attr, pe->pid, pe->cpu, group_leader_fd, 0);
	if (pe->fd < 0) {
		msglog(LDMSD_LERROR, "Error adding event '%s'\n", pe->name);
		msglog(LDMSD_LERROR, "\terrno: %d\n", pe->fd);
		msglog(LDMSD_LERROR, "\ttype: %d\n", pe->attr.type);
		msglog(LDMSD_LERROR, "\tsize: %d\n", pe->attr.size);
		msglog(LDMSD_LERROR, "\tconfig: %llx\n", pe->attr.config);
		msglog(LDMSD_LERROR, "\tpid: %d\n", pe->pid);
		msglog(LDMSD_LERROR, "\tcpu: %d\n", pe->cpu);

		goto err;
	}

	if(current_group == NULL){ /* if this is the group group leader */
		current_group = calloc(1, sizeof *current_group); /* allocate event group */
		if (!current_group) {
			msglog(LDMSD_LERROR,"add_event out of memory\n");
			goto err;
		}
		current_group->pid = pe->pid; /*  set pid for the group */
		current_group->cpu = pe->cpu; /*  set cpu for the group */
		current_group->eventCounter = 0; /*  initialize the event counter for the group */
		current_group->leader = pe->fd; /*  set the fd of group leader */
		current_group->metric_index = NULL;
		pe->group_index = 0; /*  initialize the index in group */
		LIST_INSERT_HEAD(&gevent_list, current_group, entry); /*  add the new group to the list of groups */
	}

	pe->group_index = current_group->eventCounter;
	current_group->eventCounter++;



	LIST_INSERT_HEAD(&pevent_list, pe, entry);


	return 0;

err:
	if (pe && pe->name)
		free(pe->name);
	free(pe);
	return rc;
}

static int del_event(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg)
{
	char *name = av_value(avl, "metricname");
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
	msglog(LDMSD_LINFO, "%-24s %8s %8s %8s %8s %16s\n",
			"Name", "Pid", "Cpu", "Fd", "Type", "Event");
	msglog(LDMSD_LINFO, "%-24s %8s %8s %8s %8s %16s\n",
			"------------------------",
			"--------", "--------", "--------",
			"--------", "----------------");
	msglog(LDMSD_LINFO, "Name Fd Type Config\n");
	LIST_FOREACH(pe, &pevent_list, entry) {
		msglog(LDMSD_LINFO, "%-24s %8d %8d %8d %8d %16llx\n",
				pe->name, pe->pid, pe->cpu,
				pe->fd, pe->attr.type, pe->attr.config);
	}
	return 0;
}

static int init(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg)
{
	/* Create the metric set */
	int rc;

	ldms_schema_t schema;
	struct pevent *pe;

	if (set) {
		msglog(LDMSD_LERROR, SAMP ": Set already created.\n");
		return EINVAL;
	}

	base = base_config(avl, SAMP, SAMP, msglog);
	if (!base) {
		rc = ENOMEM;
		goto err;
	}

	schema = base_schema_new(base);
	if (!schema) {
		msglog(LDMSD_LERROR,
		       "%s: The schema '%s' could not be created, errno=%d.\n",
		       __FILE__, base->schema_name, errno);
		rc = errno;
		goto err;
	}


	LIST_FOREACH(pe, &pevent_list, entry) {
		rc = ldms_schema_metric_add(schema, pe->name, LDMS_V_U64);
		if (rc < 0) {
			msglog(LDMSD_LERROR, SAMP ": failed to add event %s to metric set.\n", pe->name);
			goto err;
		}
		pe->metric_index = rc;

		struct event_group *current_group = find_group(pe->pid, pe->cpu);
		if(current_group->metric_index == NULL)
			current_group->metric_index = calloc(current_group->eventCounter, sizeof(int));
		current_group->metric_index[pe->group_index] = pe->metric_index;


		msglog(LDMSD_LINFO, SAMP ": event [name: %s, code: 0x%x] has been added.\n", pe->name, pe->attr.config);
	}

	return 0;

err:
	if (base)
		base_del(base);
	return rc;
}

static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	struct kw kw_tbl[] = {
		{ "add", add_event },
		{ "del", del_event },
		{ "init", init },
		{ "ls", list },
	};

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
	msglog(LDMSD_LERROR, usage(self));
	goto err2;
err1:
	msglog(LDMSD_LERROR, "perfevent: Invalid configuration keyword '%s'\n", action);
err2:
	return 0;
}

static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	return set;
}

static int sample(struct ldmsd_sampler *self)
{
	int rc;

	if (!set) {
		msglog(LDMSD_LERROR, SAMP ": plug-in not initialized\n");
		return EINVAL;
	}

	/* if not started yet, start with group_leader_fd */
	if(!started){

		struct event_group *eg;
		LIST_FOREACH(eg, &gevent_list, entry) {
			rc = ioctl(eg->leader, PERF_EVENT_IOC_RESET, 0); /* reset the values to 0 */
			if(rc == -1){
				msglog(LDMSD_LERROR, SAMP "Error(%d) in starting %d\n", rc, eg->leader);
				return rc;
			}

			rc = ioctl(eg->leader, PERF_EVENT_IOC_ENABLE, 0); /* start counting the values */
			if(rc == -1){
				msglog(LDMSD_LERROR, SAMP "Error(%d) in starting %d\n", rc, eg->leader);
				return rc;
			}
		}
		started = 1;
	}

	base_sample_begin(base);
	static int readerrlogged = 0;
	struct event_group *eg;
	LIST_FOREACH(eg, &gevent_list, entry) {
		unsigned int read_size = (eg->eventCounter + 2) * sizeof(long long); /*based on read format. */
		long long *data = calloc(eg->eventCounter + 2, sizeof(long long)); /*allocate memory based on read format. */
		int read_result = read(eg->leader, data, read_size); /* do the read */
		if (read_result < 0) {
			free(data);
			if (!readerrlogged) {
				msglog(LDMSD_LERROR, "perfevent: read event failed.\n");
				readerrlogged = 1;
			}
			break;
		}
		/* int event_index = (read_result / sizeof(long long)) - 1;  start from the last event added to the list? */
		int m = 0;
		for(m = 0; m < eg->eventCounter; m++){
			ldms_metric_set_u64(set, eg->metric_index[m], data[m+2]);
		}
		free(data);
	}

	base_sample_end(base);

	return 0;
}

static void term(struct ldmsd_plugin *self)
{
	struct pevent *pe;
	struct event_group *ge;

	if(started) {
		LIST_FOREACH(pe, &pevent_list, entry) {
			ioctl(pe->fd, PERF_EVENT_IOC_DISABLE, 0);
			close(pe->fd);
		}
	}

	LIST_FOREACH(pe, &pevent_list, entry) {
		LIST_REMOVE(pe, entry);
		free(pe);
	}

	LIST_FOREACH(ge, &gevent_list, entry) {
		free(ge->metric_index);
		LIST_REMOVE(ge, entry);
		free(ge);
	}

	if (base)
		base_del(base);
	base = NULL;

	if (set)
		ldms_set_delete(set);
	set = NULL;

}

static struct ldmsd_sampler pe_plugin = {
	.base = {
		.name = SAMP,
		.type = LDMSD_PLUGIN_SAMPLER,
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
