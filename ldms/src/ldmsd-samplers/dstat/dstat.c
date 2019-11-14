/* Copyright © 2018 National Technology & Engineering Solutions of Sandia,
 * LLC (NTESS). Under the terms of Contract DE-NA0003525 with NTESS, the
 * U.S. Government retains certain rights in this software.
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
  * \file dstat.c
  * \brief info about ldmsd itself.
  * This is who monitors the monitors.
  *
  * Collects os view of ldmsd.
  * /proc/self/io
  * /proc/self/stat [subset of items up to #42]
  * /proc/self/statm
  * Count of actively used mmalloc bytes.
  * Eventual other items may include the file descriptor count.
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
#include <stdbool.h>

#include "parse_stat.h"
#include "mmalloc/mmalloc.h"

#include "ldmsd.h"
#include "ldmsd_sampler.h"

#define PID_IO 0x1
#define PID_STAT 0x2
#define PID_STATM 0x4
#define PID_MMALLOC 0x8

#define PID_DATA \
	struct proc_pid_io io; \
	struct proc_pid_stat stat; \
	struct proc_pid_statm statm; \
	struct mm_stat mms; \
	struct mm_metrics mmm

#define IOLIST(WRAP) \
	WRAP("rchar", rchar, LDMS_V_U64, pos_rchar) \
	WRAP("wchar", wchar, LDMS_V_U64, pos_wchar) \
	WRAP("syscr", syscr, LDMS_V_U64, pos_syscr) \
	WRAP("syscw", syscr, LDMS_V_U64, pos_syscw) \
	WRAP("read_bytes", read_bytes, LDMS_V_U64, pos_read_bytes) \
	WRAP("write_bytes", write_bytes, LDMS_V_U64, pos_write_bytes) \
	WRAP("cancelled_write_bytes", cancelled_write_bytes, LDMS_V_U64, pos_cancelled_write_bytes)

#define STATLIST(WRAP) \
	WRAP("minflt", minflt, LDMS_V_U64, pos_minflt) \
	WRAP("cminflt", cminflt, LDMS_V_U64, pos_cminflt) \
	WRAP("majflt", majflt, LDMS_V_U64, pos_majflt) \
	WRAP("cmajflt", cmajflt, LDMS_V_U64, pos_cmajflt) \
	WRAP("utime", utime, LDMS_V_U64, pos_utime) \
	WRAP("stime", stime, LDMS_V_U64, pos_stime) \
	WRAP("cutime", cutime, LDMS_V_S64, pos_cutime) \
	WRAP("cstime", cstime, LDMS_V_S64, pos_cstime) \
	WRAP("priority", priority, LDMS_V_S64, pos_priority) \
	WRAP("nice", nice, LDMS_V_S64, pos_nice) \
	WRAP("num_threads", num_threads, LDMS_V_U64, pos_num_threads) \
	WRAP("vsize", vsize, LDMS_V_U64, pos_vsize) \
	WRAP("rss", rss, LDMS_V_U64, pos_rss) \
	WRAP("rsslim", rsslim, LDMS_V_U64, pos_rsslim) \
	WRAP("signal", signal, LDMS_V_U64, pos_signal) \
	WRAP("processor", processor, LDMS_V_U64, pos_processor) \
	WRAP("rt_priority", rt_priority, LDMS_V_U64, pos_rt_priority) \
	WRAP("policy", policy, LDMS_V_U64, pos_policy) \
	WRAP("delayacct_blkio_ticks", delayacct_blkio_ticks, LDMS_V_U64, pos_delayacct_blkio_ticks)

#define STATMLIST(WRAP) \
	WRAP("VmSize", size, LDMS_V_U64, pos_size) \
	WRAP("VmRSS", resident, LDMS_V_U64, pos_resident) \
	WRAP("share_pages", share, LDMS_V_U64, pos_share) \
	WRAP("text_pages", text, LDMS_V_U64, pos_text) \
	WRAP("lib_pages", lib, LDMS_V_U64, pos_lib) \
	WRAP("data_pages", data, LDMS_V_U64, pos_data) \
	WRAP("dirty_pages", dt, LDMS_V_U64, pos_dt)

#define MMALLOCLIST(WRAP) \
	WRAP("mmalloc_bytes_used_p_holes", used_holes, LDMS_V_U64, pos_used_holes)

#define DECLPOS(n, m, t, p) int p;
#define INITPOS(n, m, t, p) .p = -1 ,

struct mm_metrics {
	uint64_t used_holes;
};

#define INST(x) ((ldmsd_plugin_inst_t)(x))
#define INST_LOG(inst, lvl, fmt, ...) \
		ldmsd_log((lvl), "%s: " fmt, INST(inst)->inst_name, \
								##__VA_ARGS__)

typedef struct dstat_inst_s *dstat_inst_t;
struct dstat_inst_s {
	struct ldmsd_plugin_inst_s base;
	/* Extend plugin-specific data here */
	int pidopts; /* pid-opts: bitwise-or of the PID_ flags */
	const char *pids;

	int pos_pid;
	int pos_ppid;

	IOLIST(DECLPOS);
	STATLIST(DECLPOS);
	STATMLIST(DECLPOS);
	MMALLOCLIST(DECLPOS);
};

/* ============== Sampler Plugin APIs ================= */

#define META(n, m, t, p) \
	rc = ldms_schema_meta_add(schema, n, t, ""); \
	if (rc < 0) \
		return -rc; /* rc = -errno */ \
	\
	inst->p = rc;

#define METRIC(n, m, t, p) \
	rc = ldms_schema_metric_add(schema, n, t, ""); \
	if (rc < 0) \
		return -rc; /* rc = -errno */ \
	\
	inst->p = rc;

#define IOSAMPLE(n, m, t, p) \
	ldms_metric_set_u64(set, inst->p, (uint64_t)io.m);

#define STATMSAMPLE(n, m, t, p) \
	ldms_metric_set_u64(set, inst->p, (uint64_t)statm.m);

#define MMALLOCSAMPLE(n, m, t, p) \
	ldms_metric_set_u64(set, inst->p, (uint64_t)mmm.m);

#define STATSAMPLE(n, m, t, p) \
	switch (t) { \
	case LDMS_V_U64: \
		ldms_metric_set_u64(set, inst->p, (uint64_t)stat.m); \
		break; \
	case LDMS_V_S64: \
		ldms_metric_set_s64(set, inst->p, (int64_t)stat.m); \
		break; \
	default: \
		rc = EINVAL; \
		INST_LOG(inst, LDMSD_LERROR, \
			 "sample " n " not correctly defined.\n"); \
	}

static
int dstat_update_schema(ldmsd_plugin_inst_t pi, ldms_schema_t schema)
{
	dstat_inst_t inst = (void*)pi;

	int rc;

	if (inst->pidopts & PID_IO) {
		IOLIST(METRIC);
	}
	if (inst->pidopts & PID_STAT) {
		META("pid", pid, LDMS_V_U64, pos_pid);
		META("ppid", ppid, LDMS_V_U64, pos_ppid);
		STATLIST(METRIC);
	}
	if (inst->pidopts & PID_STATM) {
		STATMLIST(METRIC);
	}
	if (inst->pidopts & PID_MMALLOC) {
		MMALLOCLIST(METRIC);
	}

	return 0;
}

static
int dstat_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set, void *ctxt)
{
	dstat_inst_t inst = (void*)pi;
	PID_DATA;
	int rc;

	if (inst->pidopts & PID_IO) {
		rc = parse_proc_pid_io(&io, inst->pids);
		if (rc)
			return rc;
		IOLIST(IOSAMPLE);
	}
	if (inst->pidopts & PID_STAT) {
		rc = parse_proc_pid_stat(&stat, inst->pids);
		if (rc)
			return rc;
		STATLIST(STATSAMPLE);
		if (rc)
			return rc;
	}
	if (inst->pidopts & PID_STATM) {
		rc = parse_proc_pid_statm(&statm, inst->pids);
		if (rc)
			return rc;
		STATMLIST(STATMSAMPLE);
	}
	if (inst->pidopts & PID_MMALLOC) {
		mm_stats(&mms);
		mmm.used_holes = mms.size - mms.grain * mms.largest;
		MMALLOCLIST(MMALLOCSAMPLE);
	}
	return 0;
}


/* ============== Common Plugin APIs ================= */

static
const char *dstat_desc(ldmsd_plugin_inst_t pi)
{
	return "dstat - dstat sampler plugin";
}

static
char *_help = "\
dstat config synopsis:\n\
config name=<INST> [COMMON_OPTIONS] \n\
		   [io=<bool>] [stat=<bool>] [statm=<bool>] [mmalloc=<bool>]\n\
\n\
Option descriptions:\n\
    If none of io, stat, statm, mmalloc is given, all but mmalloc default true.\n\
    If any of io, stat, statm is given, unmentioned ones default false.\n\
    Enabling mmalloc is potentially expensive at high frequencies on aggregators.\n\
    <bool>       Enable data subset if true.\n\
";

static
const char *dstat_help(ldmsd_plugin_inst_t pi)
{
	return _help;
}

static bool get_bool(dstat_inst_t inst, const char *val, char *name)
{
	if (!val)
		return false;

	switch (val[0]) {
	case '1':
	case 't':
	case 'T':
	case 'y':
	case 'Y':
		return true;
	case '0':
	case 'f':
	case 'F':
	case 'n':
	case 'N':
		return false;
	default:
		INST_LOG(inst, LDMSD_LERROR, "bad bool value %s for %s\n",
			 val, name);
		return false;
	}
}

static
int dstat_config(ldmsd_plugin_inst_t pi, json_entity_t json,
				      char *ebuf, int ebufsz)
{
	dstat_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)inst->base.base;
	ldms_set_t set;
	json_entity_t doio, dostat, dostatm, dommalloc;
	struct proc_pid_stat stat;
	int rc;

	rc = samp->base.config(pi, json, ebuf, ebufsz);
	if (rc)
		return rc;

	doio = json_value_find(json, "io");
	dostat = json_value_find(json, "stat");
	dostatm = json_value_find(json, "statm");
	dommalloc = json_value_find(json, "mmalloc");
	if (!doio && !dostat && !dostatm && !dommalloc) {
		inst->pidopts = ( PID_IO | PID_STAT | PID_STATM );
	} else {
		if (doio) {
			if (doio->type != JSON_STRING_VALUE) {
				ldmsd_log(LDMSD_LERROR, "%s: "
						"The given 'io' value is not "
						"a string.\n", pi->inst_name);
				return EINVAL;
			}
			if (get_bool(inst, json_value_str(doio)->str, "io"))
				inst->pidopts |= PID_IO;
		}
		if (dostat) {
			if (dostat->type != JSON_STRING_VALUE) {
				ldmsd_log(LDMSD_LERROR, "%s: "
						"The given 'stat' value is not "
						"a string.\n", pi->inst_name);
				return EINVAL;
			}
			if (get_bool(inst, json_value_str(dostat)->str, "stat"))
				inst->pidopts |= PID_STAT;
		}
		if (dostatm) {
			if (dostatm->type != JSON_STRING_VALUE) {
				ldmsd_log(LDMSD_LERROR, "%s: "
						"The given 'statm' value is not "
						"a string.\n", pi->inst_name);
				return EINVAL;
			}
			if (get_bool(inst, json_value_str(dostatm)->str, "statm"))
				inst->pidopts |= PID_STATM;
		}
		if (dommalloc) {
			if (dommalloc->type != JSON_STRING_VALUE) {
				INST_LOG(inst, LDMSD_LERROR,
						"The given 'mmalloc' value is not "
						"a string.\n");
				return EINVAL;
			}
			if (get_bool(inst, json_value_str(dommalloc)->str, "mmalloc"))
				inst->pidopts |= PID_MMALLOC;
		}
	}
	if (!inst->pidopts) {
		snprintf(ebuf, ebufsz, "configured with nothing to do.\n");
		return EINVAL;
	}

	/* create schema + set */
	samp->schema = samp->create_schema(pi);
	if (!samp->schema)
		return errno;
	set = samp->create_set(pi, samp->set_inst_name, samp->schema, NULL);
	if (!set)
		return errno;

	/* set pid & ppid meta metrics */
	if (inst->pidopts & PID_STAT) {
		rc = parse_proc_pid_stat(&stat, inst->pids);
		if (rc != 0) {
			INST_LOG(inst, LDMSD_LERROR,
				 "unable to read /proc/%s/stat (%s)\n",
				 inst->pids, strerror(rc));
			return rc;
		}
		ldms_metric_set_u64(set, inst->pos_pid, stat.pid);
		ldms_metric_set_u64(set, inst->pos_ppid, stat.ppid);
	}
	return 0;
}

static
void dstat_del(ldmsd_plugin_inst_t pi)
{
	/* do nothing */
}

static
int dstat_init(ldmsd_plugin_inst_t pi)
{
	dstat_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)inst->base.base;
	/* override update_schema() and update_set() */
	samp->update_schema = dstat_update_schema;
	samp->update_set = dstat_update_set;

	/* NOTE More initialization code here if needed */
	return 0;
}

static
struct dstat_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "sampler",
		.plugin_name = "dstat",

                /* Common Plugin APIs */
		.desc   = dstat_desc,
		.help   = dstat_help,
		.init   = dstat_init,
		.del    = dstat_del,
		.config = dstat_config,
	},

	.pids = "self",
	.pos_pid = -1,
	.pos_ppid = -1,

	IOLIST(INITPOS)
	STATLIST(INITPOS)
	STATMLIST(INITPOS)
	MMALLOCLIST(INITPOS)

};

ldmsd_plugin_inst_t new()
{
	dstat_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
