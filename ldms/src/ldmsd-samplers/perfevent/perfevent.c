/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010-2016,2018-2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2010-2016,2018-2019 Open Grid Computing, Inc. All rights
 * reserved.
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
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <linux/perf_event.h>
#include <math.h>

#include "ldmsd.h"
#include "ldmsd_sampler.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))
#endif

#define INST(x) ((ldmsd_plugin_inst_t)(x))
#define INST_LOG(inst, lvl, fmt, ...) \
		ldmsd_log((lvl), "%s: " fmt, INST(inst)->inst_name, \
								##__VA_ARGS__)

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

struct event_group {
	int leader;
	int pid;
	int cpu;
	unsigned int eventCounter;
	long long *data;
	int *metric_index;
	LIST_ENTRY(event_group) entry;
};

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

typedef struct perfevent_inst_s *perfevent_inst_t;
struct perfevent_inst_s {
	struct ldmsd_plugin_inst_s base;

	int started;
	LIST_HEAD(gevent_list, event_group) gevent_list;
	LIST_HEAD(pevent_list, pevent) pevent_list;

	/* error buffer for config routine */
	char *ebuf;
	int ebufsz;
};

struct kw {
	const char *token;
	int (*action)(perfevent_inst_t inst, json_entity_t json, void *arg);
};

static int kw_comparator(const void *a, const void *b)
{
	struct kw *_a = (struct kw *)a;
	struct kw *_b = (struct kw *)b;
	return strcmp(_a->token, _b->token);
}

static inline int pe_open(struct perf_event_attr *attr, pid_t pid,
			  int cpu, int group_fd, unsigned long flags)
{
	attr->size = sizeof(*attr);
	int fd = syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
	return fd;
}

static const char *__attr_find(perfevent_inst_t inst, json_entity_t dict,
				char *attr_name)
{
	json_entity_t value;

	value = json_value_find(dict, attr_name);
	if (!value) {
		INST_LOG(inst, LDMSD_LERROR, "The given '%s' is missing.\n", attr_name);
		return NULL;
	}
	if (value->type != JSON_STRING_VALUE) {
		INST_LOG(inst, LDMSD_LERROR, "The given '%s' value "
						"is not a string.\n", attr_name);
		return NULL;
	}
	return json_value_str(value)->str;
}

/**
 * Specify the textual name that will appear for this event in the metric set.
 * Format: metricname=%s
 */
static int add_event_name(perfevent_inst_t inst, json_entity_t dict, void *arg)
{
	struct pevent *pe = arg;
	const char *val = __attr_find(inst, dict, "metricname");
	if (!val)
		return EINVAL;
	pe->name = strdup(val);
	return 0;
}

/**
 * Specify the event type
 */
static int add_event_type(perfevent_inst_t inst, json_entity_t dict, void *arg)
{
	struct pevent *pe = arg;
	const char *val = __attr_find(inst, dict, "type");
	if (!val)
		return EINVAL;
	pe->attr.type = strtol(val, NULL, 0);
	return 0;
}

/**
 * Specify the pid
 */
static int add_event_pid(perfevent_inst_t inst, json_entity_t dict, void *arg)
{
	struct pevent *pe = arg;
	const char *val = __attr_find(inst, dict, "pid");
	if (!val)
		return EINVAL;
	pe->pid = strtol(val, NULL, 0);
	return 0;
}

/**
 * Specify the event id
 */
static int add_event_id(perfevent_inst_t inst, json_entity_t dict, void *arg)
{
	struct pevent *pe = arg;
	const char *val = __attr_find(inst, dict, "id");
	if (!val)
		return EINVAL;
	pe->attr.config = strtol(val, NULL, 0);
	return 0;
}

/**
 * Specify the cpuc core
 */
static int add_event_cpu(perfevent_inst_t inst, json_entity_t dict, void *arg)
{
	struct pevent *pe = arg;
	const char *val = __attr_find(inst, dict, "cpu");
	if (!val)
		return EINVAL;
	pe->cpu = strtol(val, NULL, 0);
	return 0;
}

static struct pevent *find_event(perfevent_inst_t inst, const char *name)
{
	struct pevent *pe;
	LIST_FOREACH(pe, &inst->pevent_list, entry) {
		if (strcmp(pe->name, name) == 0)
			return pe;
	}
	return NULL;
}

static struct event_group *find_group(perfevent_inst_t inst, int event_pid,
				      int event_cpu)
{
	struct event_group *eg;
	LIST_FOREACH(eg, &inst->gevent_list, entry) {

		if (eg->pid == -1){
			if (eg->cpu == event_cpu) {
				return eg;
			}
		} else {
			if (eg->pid == event_pid) {
				return eg;
			}
		}
	}
	return NULL;
}

static int add_event(perfevent_inst_t inst, json_entity_t json, void *arg)
{
	struct kw add_token_tbl[] = {
		{ "cpu", add_event_cpu },
		{ "id", add_event_id },
		{ "metricname", add_event_name },
		{ "pid", add_event_pid },
		{ "type", add_event_type },
	};

	int rc;
	struct pevent *pe;
	json_entity_t attr;

	pe = calloc(1, sizeof *pe);
	if (!pe) {
		snprintf(inst->ebuf, inst->ebufsz,
			 "failed to allocate perfevent structure.\n");
		goto err;
	}

	pe->attr.size = sizeof(pe->attr);
	/* changed the read format to do the group read */
	pe->attr.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_TOTAL_TIME_RUNNING;
	pe->attr.exclude_kernel = 1;
	pe->attr.exclude_hv = 1;
	pe->pid = -1;
	pe->cpu = -1;

	for (attr = json_attr_first(json); attr; attr = json_attr_next(attr)) {
		struct kw key;
		struct kw *kw;
		const char *token;

		token = json_attr_name(attr)->str;

		if (0 == strcmp(token, "action")) {
			/* skipping attribute 'action' */
			continue;
		}

		key.token = token;
		kw = bsearch(&key, add_token_tbl, ARRAY_SIZE(add_token_tbl),
			     sizeof(*kw), kw_comparator);

		if (!kw) {
			snprintf(inst->ebuf, inst->ebufsz,
				 "Unrecognized keyword '%s' in configuration "
				 "string.\n", token);
			return -1;
		}
		rc = kw->action(inst, json, pe);
		if (rc)
			return rc;
	}
	if (!pe->name) {
		snprintf(inst->ebuf, inst->ebufsz,
			 "An event name must be specifed.\n");
		goto err;
	}
	if (pe->cpu == -1 && pe->pid == -1) {
		snprintf(inst->ebuf, inst->ebufsz,
			 "Error adding event '%s', PID and CPU can not be -1",
			 pe->name);
		goto err;
	}

	pe->attr.disabled = 0;

	struct event_group *current_group = find_group(inst, pe->pid, pe->cpu);
	int group_leader_fd = -1;
	if(current_group == NULL) /* if this is the group group leader */
		pe->attr.disabled = 1; /* disable the group leader */
	else
		group_leader_fd = current_group->leader;

	pe->fd = pe_open(&pe->attr, pe->pid, pe->cpu, group_leader_fd, 0);
	if (pe->fd < 0) {
		snprintf(inst->ebuf, inst->ebufsz,
			 "Error adding event '%s'"
			 ", errno: %d"
			 ", type: %d"
			 ", size: %d"
			 ", config: %llx"
			 ", pid: %d"
			 ", cpu: %d",
			 pe->name,
			 errno,
			 pe->attr.type,
			 pe->attr.size,
			 pe->attr.config,
			 pe->pid, pe->cpu);
		goto err;
	}

	if (current_group == NULL) { /* if this is the group group leader */
		current_group = calloc(1, sizeof *current_group); /* allocate event group */
		current_group->pid = pe->pid; /*  set pid for the group */
		current_group->cpu = pe->cpu; /*  set cpu for the group */
		current_group->eventCounter = 0; /*  initialize the event counter for the group */
		current_group->leader = pe->fd; /*  set the fd of group leader */
		current_group->metric_index = NULL;
		pe->group_index = 0; /*  initialize the index in group */
		LIST_INSERT_HEAD(&inst->gevent_list, current_group, entry); /*  add the new group to the list of groups */
	}

	pe->group_index = current_group->eventCounter;
	current_group->eventCounter++;



	LIST_INSERT_HEAD(&inst->pevent_list, pe, entry);

	return 0;

err:
	if (pe->name)
		free(pe->name);
	free(pe);
	return -1;
}

static int del_event(perfevent_inst_t inst, json_entity_t dict, void *arg)
{
	const char *name = __attr_find(inst, dict, "metricname");
	if (!name)
		return EINVAL;
	struct pevent *pe = find_event(inst, name);
	if (pe) {
		LIST_REMOVE(pe, entry);
		close(pe->fd);
		free(pe->name);
		free(pe);
	}
	return 0;
}

static int list(perfevent_inst_t inst, json_entity_t dict, void *arg)
{
	struct pevent *pe;
	INST_LOG(inst, LDMSD_LINFO,
		 "%-24s %8s %8s %8s %8s %16s\n",
		 "Name", "Pid", "Cpu", "Fd", "Type", "Event");
	INST_LOG(inst, LDMSD_LINFO,
		 "%-24s %8s %8s %8s %8s %16s\n",
		 "------------------------",
		 "--------", "--------", "--------",
		 "--------", "----------------");
	INST_LOG(inst, LDMSD_LINFO, "Name Fd Type Config\n");
	LIST_FOREACH(pe, &inst->pevent_list, entry) {
		INST_LOG(inst, LDMSD_LINFO, "%-24s %8d %8d %8d %8d %16llx\n",
			 pe->name, pe->pid, pe->cpu,
			 pe->fd, pe->attr.type, pe->attr.config);
	}
	return 0;
}

static int init(perfevent_inst_t inst, json_entity_t json, void *arg)
{
	ldmsd_sampler_type_t samp = (void*)inst->base.base;
	/* Create the metric set */
	int rc;
	ldms_set_t set;

	ldms_schema_t schema;

	rc = samp->base.config(&inst->base, json, inst->ebuf, inst->ebufsz);
	if (rc)
		return rc;

	/* create schema + set */
	schema = samp->create_schema(&inst->base);
	if (!schema)
		return errno;
	set = samp->create_set(&inst->base, samp->set_inst_name, schema,
			       NULL);
	if (!set)
		return errno;
	ldms_schema_delete(schema);
	return 0;
}

/* ============== Sampler Plugin APIs ================= */

static
int perfevent_update_schema(ldmsd_plugin_inst_t pi, ldms_schema_t schema)
{
	perfevent_inst_t inst = (void*)pi;
	struct pevent *pe;
	int rc;

	LIST_FOREACH(pe, &inst->pevent_list, entry) {
		rc = ldms_schema_metric_add(schema, pe->name, LDMS_V_U64, "");
		if (rc < 0) {
			INST_LOG(inst, LDMSD_LERROR,
				 "failed to add event %s to metric set.\n",
				 pe->name);
			return -rc; /* rc == -errno */
		}
		pe->metric_index = rc;

		struct event_group *current_group = find_group(inst, pe->pid,
								       pe->cpu);
		if (current_group->metric_index == NULL) {
			current_group->metric_index =
					calloc(current_group->eventCounter,
								sizeof(int));
			if (!current_group->metric_index)
				return ENOMEM;
		}
		if (current_group->data == NULL) {
			/*based on read format. */
			current_group->data =
					calloc(current_group->eventCounter + 2,
						sizeof(current_group->data[0]));
			if (!current_group->data)
				return ENOMEM;
		}
		current_group->metric_index[pe->group_index] = pe->metric_index;

		INST_LOG(inst, LDMSD_LINFO,
			 "event [name: %s, code: 0x%llx] has been added.\n",
			 pe->name, pe->attr.config);
	}

	return 0;
}

static
int perfevent_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set, void *ctxt)
{
	perfevent_inst_t inst = (void*)pi;
	int rc;

	if (!inst->started){
		struct event_group *eg;
		LIST_FOREACH(eg, &inst->gevent_list, entry) {
			/* reset the values to 0 */
			rc = ioctl(eg->leader, PERF_EVENT_IOC_RESET, 0);
			if (rc == -1) {
				INST_LOG(inst, LDMSD_LERROR,
					 "Error(%d) in starting %d\n",
					 rc, eg->leader);
				return errno;
			}

			/* start counting the values */
			rc = ioctl(eg->leader, PERF_EVENT_IOC_ENABLE, 0);
			if (rc == -1) {
				INST_LOG(inst, LDMSD_LERROR,
					 "Error(%d) in starting %d\n",
					 rc, eg->leader);
				return errno;
			}
		}
		inst->started = 1;
	}

	struct event_group *eg;
	LIST_FOREACH(eg, &inst->gevent_list, entry) {
		/*based on read format. */
		size_t read_size = (eg->eventCounter + 2) * sizeof(long long);
		/* do the read */
		ssize_t rsz = read(eg->leader, eg->data, read_size);
		if (rsz != read_size)
			continue;
		int m;
		for (m = 0; m < eg->eventCounter; m++) {
			ldms_metric_set_u64(set, eg->metric_index[m],
					    eg->data[m+2]);
		}
	}
	return 0;
}


/* ============== Common Plugin APIs ================= */

static
const char *perfevent_desc(ldmsd_plugin_inst_t pi)
{
	return "perfevent - perfevent sampler plugin";
}

static
char *_help = "\
* Add \n\
    config name=<INST> action=add metricname=<string> pid=<int> cpu=<int> \n\
                       type=<int> id=<int>\n\
            <metricname>  The metric name for the event\n\
            <pid>         The PID for the process being monitored\n\
                          The counter will follow the process to\n\
                          whichever CPU/core is in use. Note that\n\
                          'pid' and 'cpu' are mutually exclusive.\n\
            <cpu>         Count this event on the specified CPU.\n\
                          This will accumulate events across all PID\n\
                          that land on the specified CPU/core. Note\n\
                          that 'pid' and 'cpu' are mutually\n\
                          exclusive.\n\
            <type>        The event type.\n\
            <id>          The event id.\n\
\n\
* Deletes the specified event.\n\
    config name=<INST> action=del metricname=<string>\n\
\n\
* List the currently configured events.\n\
    config name=<INST> action=ls\n\
\n\
* Init \n\
    config name=<INST> action=init \n\
\n\
For more information visit: http://man7.org/linux/man-pages/man2/perf_event_open.2.html\n\
";

static
const char *perfevent_help(ldmsd_plugin_inst_t pi)
{
	return _help;
}

static
int perfevent_config(ldmsd_plugin_inst_t pi, json_entity_t json,
				      char *ebuf, int ebufsz)
{
	perfevent_inst_t inst = (void*)pi;
	int rc;

	struct kw kw_tbl[] = {
		{ "add"  , add_event }  ,
		{ "del"  , del_event }  ,
		{ "init" , init      }  ,
		{ "ls"   , list      }  ,
	};

	struct kw *kw;
	struct kw key;
	const char *action = __attr_find(inst, json, "action");
	if (!action)
		return EINVAL;

	key.token = action;
	kw = bsearch(&key, kw_tbl, ARRAY_SIZE(kw_tbl),
		     sizeof(*kw), kw_comparator);
	if (!kw) {
		snprintf(ebuf, ebufsz, "Invalid action '%s'\n", action);
		return EINVAL;
	}

	inst->ebuf = ebuf;
	inst->ebufsz = ebufsz;
	rc = kw->action(inst, json, NULL);
	inst->ebuf = NULL;
	inst->ebufsz = 0;
	return rc;
}

static
void perfevent_del(ldmsd_plugin_inst_t pi)
{
	perfevent_inst_t inst = (void*)pi;

	struct pevent *pe;
	struct event_group *gp;

	while ((pe = LIST_FIRST(&inst->pevent_list))) {
		LIST_REMOVE(pe, entry);
		if (pe->fd >= 0)
			close(pe->fd);
		if (pe->name)
			free(pe->name);
		free(pe);
	}

	while ((gp = LIST_FIRST(&inst->gevent_list))) {
		LIST_REMOVE(gp, entry);
		if (gp->metric_index)
			free(gp->metric_index);
		free(gp);
	}
}

static
int perfevent_init(ldmsd_plugin_inst_t pi)
{
	perfevent_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)inst->base.base;
	/* override update_schema() and update_set() */
	samp->update_schema = perfevent_update_schema;
	samp->update_set = perfevent_update_set;

	return 0;
}

static
struct perfevent_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "sampler",
		.plugin_name = "perfevent",

                /* Common Plugin APIs */
		.desc   = perfevent_desc,
		.help   = perfevent_help,
		.init   = perfevent_init,
		.del    = perfevent_del,
		.config = perfevent_config,
	},
	/* plugin-specific data initialization (for new()) here */
};

ldmsd_plugin_inst_t new()
{
	perfevent_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
