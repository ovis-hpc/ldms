/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2019 Open Grid Computing, Inc. All rights reserved.
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
 * \file syspapi_sampler.c
 *
 * A system-wide PAPI sampler.
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
#include <time.h>
#include <pthread.h>
#include <sys/fcntl.h>
#include <assert.h>

#include "papi.h"
#include "perfmon/pfmlib_perf_event.h"

#include "ldmsd.h"
#include "ldmsd_stream.h"
#include "ldmsd_sampler.h"
#include "json/json_util.h"

#define INST(x) ((ldmsd_plugin_inst_t)(x))
#define INST_LOG(inst, lvl, fmt, ...) \
		ldmsd_log((lvl), "%s: " fmt, INST(inst)->inst_name, \
								##__VA_ARGS__)

typedef struct syspapi_metric_s {
	TAILQ_ENTRY(syspapi_metric_s) entry;
	int midx; /* metric index in the set */
	int init_rc; /* if 0, attr is good for perf_event_open() */
	struct perf_event_attr attr; /* perf attribute */
	char papi_name[256]; /* metric name in PAPI */
	char pfm_name[256]; /* metric name in perfmon (PAPI native) */
	int pfd[]; /* one perf fd per CPU */
} *syspapi_metric_t;
TAILQ_HEAD(syspapi_metric_list, syspapi_metric_s);

typedef struct syspapi_inst_s *syspapi_inst_t;
struct syspapi_inst_s {
	struct ldmsd_plugin_inst_s base;
	int NCPU;
	ldms_set_t set;
	int cumulative;
	int auto_pause;
	pthread_mutex_t mutex;
	int flags;
	struct syspapi_metric_list mlist;
	ldmsd_stream_client_t stream_client;
};

/* flags for flags */
#define  SYSPAPI_PAUSED      0x1
#define  SYSPAPI_OPENED      0x2
#define  SYSPAPI_CONFIGURED  0x4

#define FLAG_ON(var, flag)    (var) |= (flag)
#define FLAG_OFF(var, flag)   (var) &= (~flag)
#define FLAG_CHECK(var, flag) ((var) & (flag))

/* ============== Helping Functions =================== */

static int
syspapi_metric_init(syspapi_inst_t inst, syspapi_metric_t m,
		    const char *papi_name)
{
	int len;
	int i, rc, papi_code;
	PAPI_event_info_t papi_info;
	const PAPI_component_info_t *comp_info;
	const char *pfm_name;

	len = snprintf(m->papi_name, sizeof(m->papi_name), "%s", papi_name);
	if (len >= sizeof(m->papi_name)) {
		INST_LOG(inst, LDMSD_LERROR,
			 "event name too long: %s\n", papi_name);
		return ENAMETOOLONG;
	}
	m->midx = -1;
	for (i = 0; i < inst->NCPU; i++) {
		m->pfd[i] = -1;
	}

	/* get the pfm name */
	rc = PAPI_event_name_to_code((char*)papi_name, &papi_code);
	if (rc != PAPI_OK) {
		INST_LOG(inst, LDMSD_LERROR,
			 "PAPI_event_name_to_code for %s failed, error: %d\n",
			 papi_name, rc);
		return -1;
	}
	rc = PAPI_get_event_info(papi_code, &papi_info);
	if (rc != PAPI_OK) {
		INST_LOG(inst, LDMSD_LERROR,
			 "PAPI_get_event_info for %s failed, error: %d\n",
			 papi_name, rc);
		return -1;
	}
	comp_info = PAPI_get_component_info(papi_info.component_index);
	if (strcmp("perf_event", comp_info->name)) {
		INST_LOG(inst, LDMSD_LERROR, "event %s not supported, "
			 "only events in perf_event are supported.\n",
			 m->papi_name);
		return EINVAL;
	}
	if (comp_info->disabled) {
		INST_LOG(inst, LDMSD_LERROR, "cannot initialize event %s, "
			 "PAPI component `perf_event` disabled, reason: %s\n",
			 m->papi_name, comp_info->disabled_reason);
		return ENODATA;
	}
	if (IS_PRESET(papi_code)) {
		if (strcmp(papi_info.derived, "NOT_DERIVED")) {
			/* not NOT_DERIVED ==> this is a derived preset */
			INST_LOG(inst, LDMSD_LERROR,
				 "Unsupported PAPI derived event: %s\n",
				 m->papi_name);
			return ENOTSUP;
		}
		switch (papi_info.count) {
		case 0:
			/* unavailable */
			INST_LOG(inst, LDMSD_LERROR,
				 "no native event describing papi event %s\n",
				 m->papi_name);
			return ENODATA;
		case 1:
			/* good */
			pfm_name = papi_info.name[0];
			break;
		default:
			/* unsupported */
			INST_LOG(inst, LDMSD_LERROR,
				 "%s not supported: the event contains "
				 "multiple native events.\n", m->papi_name);
			return ENOTSUP;
		}
	} else if (IS_NATIVE(papi_code)) {
		pfm_name = papi_info.symbol;
	} else {
		/* invalid */
		INST_LOG(inst, LDMSD_LERROR,
			 "%s is neither a PAPI-preset event nor a "
			 "native event.\n", m->papi_name);
		return EINVAL;
	}
	snprintf(m->pfm_name, sizeof(m->pfm_name), "%s", pfm_name);

	/* Now, get perf attr */
	bzero(&m->attr, sizeof(m->attr));
	m->attr.size = sizeof(m->attr);
	pfm_perf_encode_arg_t pfm_arg = { .attr = &m->attr,
					  .size = sizeof(pfm_arg) };
	/* populate perf attr using pfm */
	rc = pfm_get_os_event_encoding(pfm_name, PFM_PLM0|PFM_PLM3,
				       PFM_OS_PERF_EVENT, &pfm_arg);
	if (rc) {
		INST_LOG(inst, LDMSD_LERROR,
			 "pfm_get_os_event_encoding for %s failed, error: %d\n",
			 m->papi_name, rc);
	}
	return rc;
}

/* create and add metric (by name) into the mlist */
static int
syspapi_metric_add(syspapi_inst_t inst, const char *name, char *ebuf, int ebufsz)
{
	syspapi_metric_t m;
	m = calloc(1, sizeof(*m) + inst->NCPU*sizeof(int));
	if (!m) {
		snprintf(ebuf, ebufsz, "out of memory.\n");
		return ENOMEM;
	}
	m->init_rc = syspapi_metric_init(inst, m, name);
	/* metrics that failed to initialized are logged, but won't break
	 * the instance configure process. */
	TAILQ_INSERT_TAIL(&inst->mlist, m, entry);
	return 0;
}

static int
populate_mlist(syspapi_inst_t inst, char *events, char *ebuf, int ebufsz)
{
	int rc;
	char *tkn, *ptr;
	tkn = strtok_r(events, ",", &ptr);
	while (tkn) {
		rc = syspapi_metric_add(inst, tkn, ebuf, ebufsz);
		if (rc)
			return rc;
		tkn = strtok_r(NULL, ",", &ptr);
	}
	return 0;
}

static void
purge_mlist(syspapi_inst_t inst)
{
	int i;
	syspapi_metric_t m;
	while ((m = TAILQ_FIRST(&inst->mlist))) {
		TAILQ_REMOVE(&inst->mlist, m, entry);
		for (i = 0; i < inst->NCPU; i++) {
			if (m->pfd[i] < 0)
				continue;
			close(m->pfd[i]);
		}
		free(m);
	}
}

static void
syspapi_close(syspapi_inst_t inst)
{
	syspapi_metric_t m;
	int i;
	TAILQ_FOREACH(m, &inst->mlist, entry) {
		for (i = 0; i < inst->NCPU; i++) {
			if (m->pfd[i] < 0)
				continue;
			ioctl(m->pfd[i], PERF_EVENT_IOC_DISABLE, 0);
			close(m->pfd[i]);
			m->pfd[i] = -1;
		}
	}
}

/* Report errors with helpful info (from perf_event_open(2))*/
static void
syspapi_open_error(syspapi_inst_t inst, syspapi_metric_t m, int rc)
{
	switch (rc) {
	case EACCES:
	case EPERM:
		INST_LOG(inst, LDMSD_LERROR,
			"perf_event_open() failed (Permission "
			"denied) for %s. Please make sure that ldmsd has "
			"CAP_SYS_ADMIN or /proc/sys/kernel/perf_event_paranoid "
			"is permissive (e.g. -1, see "
			"https://www.kernel.org/doc/Documentation/"
			"sysctl/kernel.txt for more info).\n", m->papi_name);
		break;
	case EBUSY:
		INST_LOG(inst, LDMSD_LERROR,
			"perf_event_open() failed (EBUSY) for %s, "
			"another event already has exclusive access to the "
			"PMU.\n", m->papi_name);
		break;
	case EINVAL:
		INST_LOG(inst, LDMSD_LERROR,
			"perf_event_open() failed (EINVAL) for %s, "
			"invalid event\n", m->papi_name);
		break;
	case EMFILE:
		INST_LOG(inst, LDMSD_LERROR,
			"perf_event_open() failed (EMFILE) for %s, "
			"too many open file descriptors.\n", m->papi_name);
		break;
	case ENODEV:
	case ENOENT:
	case ENOSYS:
	case EOPNOTSUPP:
		INST_LOG(inst, LDMSD_LERROR,
			"perf_event_open() failed (%d) for %s, "
			"event not supported.\n", rc, m->papi_name);
		break;
	case ENOSPC:
		INST_LOG(inst, LDMSD_LERROR,
			"perf_event_open() failed (%d) for %s, "
			"too many events.\n", rc, m->papi_name);
		break;
	default:
		INST_LOG(inst, LDMSD_LERROR,
			"perf_event_open() failed for %s, errno: %d\n",
			m->papi_name, rc);
		break;
	}
}

/* perf_event_open for all metrics in mlist */
static int
syspapi_open(syspapi_inst_t inst)
{
	int i, rc = 0;
	syspapi_metric_t m;
	TAILQ_FOREACH(m, &inst->mlist, entry) {
		if (m->init_rc) /* don't open the failed metrics */
			continue;
		for (i = 0; i < inst->NCPU; i++) {
			m->pfd[i] = perf_event_open(&m->attr, -1, i, -1, 0);
			if (m->pfd[i] < 0) {
				rc = errno;
				syspapi_open_error(inst, m, rc);
				/* just print error and continue */
				if (rc == EMFILE) { /* except EMFILE */
					INST_LOG(inst, LDMSD_LERROR,
						 "Too many open files.\n");
					syspapi_close(inst);
					return rc;
				}
			} else {
				INST_LOG(inst, LDMSD_LINFO,
					 "%s successfully added\n",
					 m->papi_name);
			}
		}
	}
	return 0;
}

static int
handle_cfg_file(syspapi_inst_t inst, const char *cfg_file, char *ebuf, int ebufsz)
{
	ldmsd_sampler_type_t samp = LDMSD_SAMPLER(inst);
	int rc = 0, fd = -1;
	ssize_t off, rsz, sz;
	char *buff = NULL;
	json_parser_t parser = NULL;
	json_entity_t json = NULL;
	json_entity_t events;
	json_entity_t event;
	json_entity_t schema;

	fd = open(cfg_file, O_RDONLY);
	if (fd < 0) {
		rc = errno;
		snprintf(ebuf, ebufsz,
			 "open failed on %s, errno: %d\n", cfg_file, errno);
		goto out;
	}
	sz = lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_SET);
	buff = malloc(sz);
	if (!buff) {
		rc = ENOMEM;
		snprintf(ebuf, ebufsz, "out of memory\n");
		goto out;
	}
	off = 0;
	while (off < sz) {
		rsz = read(fd, buff + off, sz - off);
		if (rsz < 0) {
			rc = errno;
			snprintf(ebuf, ebufsz, "cfg_file read error: %d\n", rc);
			goto out;
		}
		if (rsz == 0) {
			rc = EIO;
			snprintf(ebuf, ebufsz, "unexpected EOF.\n");
			goto out;
		}
		off += rsz;
	}

	parser = json_parser_new(0);
	if (!parser) {
		rc = ENOMEM;
		snprintf(ebuf, ebufsz, "out of memory.\n");
		goto out;
	}

	rc = json_parse_buffer(parser, buff, sz, &json);
	if (rc) {
		snprintf(ebuf, ebufsz, "`%s` JSON parse error.\n", cfg_file);
		goto out;
	}

	schema = json_attr_find(json, "schema");
	if (schema) {
		schema = json_attr_value(schema);
		if (json_entity_type(schema) != JSON_STRING_VALUE) {
			snprintf(ebuf, ebufsz, "cfg_file error, `schema` "
				 "attribute must be a string.\n");
			rc = EINVAL;
			goto out;
		}
		if (samp->schema_name)
			free(samp->schema_name);
		samp->schema_name = strdup(json_value_str(schema)->str);
		if (!samp->schema_name) {
			snprintf(ebuf, ebufsz, "out of memory.\n");
			rc = ENOMEM;
			goto out;
		}
	}

	events = json_attr_find(json, "events");
	if (!events) {
		snprintf(ebuf, ebufsz, "cfg_file parse error: `events` "
				"attribute not found.\n");
		rc = ENOENT;
		goto out;
	}
	events = json_attr_value(events);
	if (json_entity_type(events) != JSON_LIST_VALUE) {
		rc = EINVAL;
		snprintf(ebuf, ebufsz, "cfg_file error: `events` must "
				"be a list of strings.\n");
		goto out;
	}

	event = json_item_first(events);
	while (event) {
		if (json_entity_type(event) != JSON_STRING_VALUE) {
			rc = EINVAL;
			snprintf(ebuf, ebufsz, "cfg_file error: "
					"entries in `events` list must be "
					"strings.\n");
			goto out;
		}
		rc = syspapi_metric_add(inst, json_value_str(event)->str,
					ebuf, ebufsz);
		if (rc) {
			goto out;
		}
		event = json_item_next(event);
	}

out:
	if (fd > -1)
		close(fd);
	if (buff)
		free(buff);
	if (parser)
		json_parser_free(parser);
	if (json)
		json_entity_free(json);
	return rc;
}


/* ============== Sampler Plugin APIs ================= */

static
int syspapi_update_schema(ldmsd_plugin_inst_t pi, ldms_schema_t schema)
{
	syspapi_inst_t inst = (void*)pi;
	int rc;
	syspapi_metric_t m;
	TAILQ_FOREACH(m, &inst->mlist, entry) {
		/* use PAPI metric name as metric name */
		rc = ldms_schema_metric_array_add(schema, m->papi_name,
						  LDMS_V_U64_ARRAY, "",
						  inst->NCPU);
		if (rc < 0) {
			rc = -rc; /* rc == -errno */
			goto err;
		}
		m->midx = rc;
	}

	return 0;

 err:
	return rc;
}

static
int syspapi_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set, void *ctxt)
{
	syspapi_inst_t inst = (void*)pi;
	/* ldmsd_sampler_type_t samp = (void*)inst->base.base; */
	syspapi_metric_t m;
	int i;
	uint64_t v;
	/* Populate set metrics */
	if (!inst->set) {
		INST_LOG(inst, LDMSD_LERROR, "plugin not initialized\n");
		return EINVAL;
	}

	pthread_mutex_lock(&inst->mutex);
	if (!FLAG_CHECK(inst->flags, SYSPAPI_OPENED))
		goto out;

	TAILQ_FOREACH(m, &inst->mlist, entry) {
		for (i = 0; i < inst->NCPU; i++) {
			v = 0;
			if (m->pfd[i] >= 0) {
				read(m->pfd[i], &v, sizeof(v));
				if (!inst->cumulative) {
					ioctl(m->pfd[i], PERF_EVENT_IOC_RESET, 0);
				}
			}
			ldms_metric_array_set_u64(set, m->midx, i, v);
		}
	}

 out:
	pthread_mutex_unlock(&inst->mutex);
	return 0;
}


/* ============== Common Plugin APIs ================= */

static
const char *syspapi_desc(ldmsd_plugin_inst_t pi)
{
	return "syspapi_sampler - System-wide PAPI sampler plugin";
}

static
char *_help = "\
syspapi config synopsis:\n\
    config name=INST [COMMON OPTIONS] cfg_file=CFG_PATH [cumulative=0|1]\n\
                     [auto_pause=0|1]\n\
\n\
Option descriptions:\n\
    cfg_file    The path to the configuration file.\n\
    cumulative  0 (default) for non-cumulative counters (reset after read),\n\
                1 for cumulative counters.\n\
    auto_pause  1 (default) to enable pausing syspapi_sampler when getting\n\
                  a notification from papi_sampler,\n\
                0 to ignore papi_sampler notification.\n\
";

static
const char *syspapi_help(ldmsd_plugin_inst_t pi)
{
	return _help;
}

static
int syspapi_config(ldmsd_plugin_inst_t pi, json_entity_t json,
				      char *ebuf, int ebufsz)
{
	syspapi_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)inst->base.base;
	ldms_set_t set;
	int rc;
	const char *value;
	const char *events;
	const char *cfg_file;

	ebuf[0] = '\0';

	pthread_mutex_lock(&inst->mutex);

	if (inst->set) {
		snprintf(ebuf, ebufsz, "Set already created.\n");
		rc = EINVAL;
		goto out;
	}

	cfg_file = json_attr_find_str(json, "cfg_file"); /* JSON config file */
	events = json_attr_find_str(json, "events");

	if (!events && !cfg_file) {
		snprintf(ebuf, ebufsz, "`events` and `cfg_file` "
			 "not specified\n");
		rc = EINVAL;
		goto out;
	}

	rc = samp->base.config(pi, json, ebuf, ebufsz);
	if (rc) /* ebuf already got populated */
		goto out;

	if (cfg_file) {
		rc = handle_cfg_file(inst, cfg_file, ebuf, ebufsz);
		if (rc) /* ebuf already got populated */
			goto err;
	}

	if (events) {
		rc = populate_mlist(inst, (char*)events, ebuf, ebufsz);
		if (rc) /* ebuf already got populated */
			goto err;
	}

	value = json_attr_find_str(json, "auto_pause");
	if (value) {
		inst->auto_pause = atoi(value);
	}

	value = json_attr_find_str(json, "cumulative");
	if (value) {
		inst->cumulative = atoi(value);
	}

	if (!FLAG_CHECK(inst->flags, SYSPAPI_PAUSED)) {
		/* state may be in SYSPAPI_PAUSED, and we won't open fd */
		rc = syspapi_open(inst);
		switch (rc) {
		case 0: /* do nothing */
			break;
		case EMFILE:
			snprintf(ebuf, ebufsz, "Too many open file.\n");
			goto err;
		default:
			snprintf(ebuf, ebufsz, "syspapi_open() error: %d\n", rc);
			goto err;
		}
		FLAG_ON(inst->flags, SYSPAPI_OPENED);
	}

	/* create schema + set */
	samp->schema = samp->create_schema(pi);
	if (!samp->schema) {
		snprintf(ebuf, ebufsz, "failed to create the schema.\n");
		rc = errno;
		goto err;
	}
	set = samp->create_set(pi, samp->set_inst_name, samp->schema, NULL);
	if (!set) {
		snprintf(ebuf, ebufsz, "failed to create a metric set.\n");
		rc = errno;
		goto err;
	}
	FLAG_ON(inst->flags, SYSPAPI_CONFIGURED);
	rc = 0;
	goto out;
	/* Plugin-specific config here */

 err:
	FLAG_OFF(inst->flags, SYSPAPI_CONFIGURED);
	FLAG_OFF(inst->flags, SYSPAPI_OPENED);
	purge_mlist(inst);
 out:
	pthread_mutex_unlock(&inst->mutex);
	if (ebuf[0])
		INST_LOG(inst, LDMSD_LERROR, "%s", ebuf);
	return rc;
}

static
void syspapi_del(ldmsd_plugin_inst_t pi)
{
	syspapi_inst_t inst = (void*)pi;

	/* The undo of syspapi_init and instance cleanup */
	if (inst->set)
		ldms_set_delete(inst->set);
	inst->set = NULL;
	purge_mlist(inst);
	FLAG_OFF(inst->flags, SYSPAPI_CONFIGURED);
	FLAG_OFF(inst->flags, SYSPAPI_OPENED);
}

/* syspapi_mutex is held */
static void
__pause(syspapi_inst_t inst)
{
	if (FLAG_CHECK(inst->flags, SYSPAPI_PAUSED))
		return; /* already paused, do nothing */
	FLAG_ON(inst->flags, SYSPAPI_PAUSED);
	if (FLAG_CHECK(inst->flags, SYSPAPI_OPENED)) {
		syspapi_close(inst);
		FLAG_OFF(inst->flags, SYSPAPI_OPENED);
	}
}

/* syspapi_mutex is held */
static void
__resume(syspapi_inst_t inst)
{
	if (!FLAG_CHECK(inst->flags, SYSPAPI_PAUSED))
		return; /* not paused, do nothing */
	FLAG_OFF(inst->flags, SYSPAPI_PAUSED);
	if (FLAG_CHECK(inst->flags, SYSPAPI_CONFIGURED)) {
		assert(0 == FLAG_CHECK(inst->flags, SYSPAPI_OPENED));
		syspapi_open(inst);
		FLAG_ON(inst->flags, SYSPAPI_OPENED);
	}
}

static int
__stream_cb(ldmsd_stream_client_t c, void *ctxt,
		ldmsd_stream_type_t stream_type,
		const char *data, size_t data_len,
		json_entity_t entity)
{
	syspapi_inst_t inst = ctxt;
	if (!inst->auto_pause)
		return 0;
	pthread_mutex_lock(&inst->mutex);
	if (stream_type != LDMSD_STREAM_STRING)
		goto out;
	if (strcmp("pause", data) == 0) {
		__pause(inst);
	}
	if (strcmp("resume", data) == 0) {
		__resume(inst);
	}
 out:
	pthread_mutex_unlock(&inst->mutex);
	return 0;
}

static int once = 0;

static
int syspapi_init(ldmsd_plugin_inst_t pi)
{
	syspapi_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)inst->base.base;
	/* override update_schema() and update_set() */
	samp->update_schema = syspapi_update_schema;
	samp->update_set = syspapi_update_set;

	if (0 == __sync_fetch_and_or(&once, 1)) {
		PAPI_library_init(PAPI_VERSION);
	}
	/* NOTE More initialization code here if needed */
	TAILQ_INIT(&inst->mlist);
	pthread_mutex_init(&inst->mutex, NULL);
	inst->NCPU = sysconf(_SC_NPROCESSORS_CONF);
	inst->stream_client = ldmsd_stream_subscribe("syspapi_stream",
					__stream_cb, NULL);
	if (!inst->stream_client) {
		INST_LOG(inst, LDMSD_LWARNING,
			 "failed to subscribe to 'syspapi_stream' "
			 "stream, errno: %d\n", errno);
		return errno;
	}
	return 0;
}

static
struct syspapi_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = LDMSD_SAMPLER_TYPENAME,
		.plugin_name = "syspapi_sampler",

                /* Common Plugin APIs */
		.desc   = syspapi_desc,
		.help   = syspapi_help,
		.init   = syspapi_init,
		.del    = syspapi_del,
		.config = syspapi_config,

	},
	/* plugin-specific data initialization (for new()) here */
	.flags = 0,
	.cumulative = 0,
	.auto_pause = 1,
};

ldmsd_plugin_inst_t new()
{
	syspapi_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
