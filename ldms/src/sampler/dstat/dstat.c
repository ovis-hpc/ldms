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
#include "ldms.h"
#include "ldmsd.h"
#include "sampler_base.h"
#include "stdbool.h"
#include "parse_stat.h"
#include "mmalloc/mmalloc.h"

static ldms_set_t set = NULL;
static ldmsd_msg_log_f msglog;;
static ldms_schema_t schema;
#define SAMP "dstat"

static base_data_t base;

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

struct mm_metrics {
	uint64_t used_holes;
};


static int pidopts = 0; /* pid-opts: bitwise-or of the PID_ flags */
static char *pids = "self";

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

#define DECLPOS(n, m, t, p) static int p = -1;

IOLIST(DECLPOS);
static int pos_pid = -1;
static int pos_ppid = -1;
STATLIST(DECLPOS);
STATMLIST(DECLPOS);
MMALLOCLIST(DECLPOS);

#define META(n, m, t, p) \
	rc = ldms_schema_meta_add(schema, n, t); \
	if (rc < 0) { \
		rc = ENOMEM; \
		goto err; \
	} \
	p = rc;

#define METRIC(n, m, t, p) \
	rc = ldms_schema_metric_add(schema, n, t); \
	if (rc < 0) { \
		rc = ENOMEM; \
		goto err; \
	} \
	p = rc;

static int create_metric_set(base_data_t base)
{
	int rc;
//	union ldms_value v;

	int iorc = -1;
	int statrc = -1;
	int statmrc = -1;
	PID_DATA;
	if (pidopts & PID_IO) {
		iorc = parse_proc_pid_io(&io, pids);
		if (iorc != 0) {
			msglog(LDMSD_LERROR, SAMP ": unable to read /proc/%s/io (%s)\n",
				pids, strerror(iorc)); 
			return iorc;
		}
	}
	if (pidopts & PID_STAT) {
		statrc = parse_proc_pid_stat(&stat, pids);
		if (statrc != 0) {
			msglog(LDMSD_LERROR, SAMP ": unable to read /proc/%s/stat (%s)\n",
				pids, strerror(statrc)); 
			return statrc;
		}
	}
	if (pidopts & PID_STATM) {
		statmrc = parse_proc_pid_statm(&statm, pids);
		if (statmrc != 0) {
			msglog(LDMSD_LERROR, SAMP ": unable to read /proc/%s/statm (%s)\n",
				pids, strerror(statmrc)); 
			return statmrc;
		}
	}

	schema = base_schema_new(base);
	if (!schema) {
		rc = ENOMEM;
		goto err;
	}

	if (pidopts & PID_IO) {
		IOLIST(METRIC);
	}
	if (pidopts & PID_STAT) {
		META("pid", pid, LDMS_V_U64, pos_pid);
		META("ppid", ppid, LDMS_V_U64, pos_ppid);
		STATLIST(METRIC);
	}
	if (pidopts & PID_STATM) {
		STATMLIST(METRIC);
	}
	if (pidopts & PID_MMALLOC) {
		MMALLOCLIST(METRIC);
	}

	set = base_set_new(base);
	if (!set) {
		rc = errno;
		goto err;
	}

	base_sample_begin(base);

#define IOSAMPLE(n, m, t, p) \
	ldms_metric_set_u64(set, p, (uint64_t)io.m);

#define STATMSAMPLE(n, m, t, p) \
	ldms_metric_set_u64(set, p, (uint64_t)statm.m);

#define MMALLOCSAMPLE(n, m, t, p) \
	ldms_metric_set_u64(set, p, (uint64_t)mmm.m);

#define STATSAMPLE(n, m, t, p) \
	switch (t) { \
	case LDMS_V_U64: \
		ldms_metric_set_u64(set, p, (uint64_t)stat.m); \
		break; \
	case LDMS_V_S64: \
		ldms_metric_set_s64(set, p, (int64_t)stat.m); \
		break; \
	default: \
		rc = EINVAL; \
		msglog(LDMSD_LERROR, SAMP ": sample " n " not correctly defined.\n"); \
	}


	if (pidopts & PID_IO) {
		IOLIST(IOSAMPLE);
	}

	if (pidopts & PID_STAT) {
		STATSAMPLE("pid", pid, LDMS_V_U64, pos_pid);
		STATSAMPLE("ppid", ppid, LDMS_V_U64, pos_ppid);
		STATLIST(STATSAMPLE);
	}

	if (pidopts & PID_STATM) {
		STATMLIST(STATMSAMPLE);
	}

	if (pidopts & PID_MMALLOC) {
		mm_stats(&mms);
		mmm.used_holes = mms.size - mms.grain * mms.largest;
		MMALLOCLIST(MMALLOCSAMPLE);
	}

	base_sample_end(base);
	return 0;

 err:
	if (schema)
		ldms_schema_delete(schema);
	
	return rc;
}

static bool get_bool(const char *val, char *name)
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
		msglog(LDMSD_LERROR, "%s: bad bool value %s for %s\n",
			val, name);
		return false;
	}
}

static const char *usage(struct ldmsd_plugin *self)
{
	return  "config name=" SAMP BASE_CONFIG_USAGE
		" [io=<bool>] [stat=<bool>] [statm=<bool>] [mmalloc=<bool>]\n"
		"    If none of io, stat, statm, mmalloc is given, all but mmalloc default true.\n"
		"    If any of io, stat, statm is given, unmentioned ones default false.\n"
		"    Enabling mmalloc is potentially expensive at high frequencies on aggregators.\n"
		"    <bool>       Enable data subset if true.\n";
}

/**
 * \brief Configuration
 *
 * See usage().
 */
static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	int rc;

	if (set) {
		msglog(LDMSD_LERROR, SAMP ": Set already created.\n");
		return EINVAL;
	}
	msglog(LDMSD_LDEBUG, SAMP ": config started.\n");

	base = base_config(avl, SAMP, SAMP, msglog);
	if (!base) {
		rc = errno;
		goto err;
	}

	char *doio, *dostat, *dostatm, *dommalloc;
	doio = av_value(avl, "io");
	dostat = av_value(avl, "stat");
	dostatm = av_value(avl, "statm");
	dommalloc = av_value(avl, "mmalloc");
	if (!doio && !dostat && !dostatm && !dommalloc) {
		pidopts = ( PID_IO | PID_STAT | PID_STATM );
	} else {
		if (doio && get_bool(doio, "io"))
			pidopts |= PID_IO;
		if (dostat && get_bool(dostat, "stat"))
			pidopts |= PID_STAT;
		if (dostatm && get_bool(dostatm, "statm"))
			pidopts |= PID_STATM;
		if (dommalloc && get_bool(dommalloc, "mmalloc"))
			pidopts |= PID_MMALLOC;
	}
	if (!pidopts) {
		msglog(LDMSD_LERROR, SAMP ": configured with nothing to do.\n");
		rc = EINVAL;
		goto err;
	}

	rc = create_metric_set(base);
	if (rc) {
		msglog(LDMSD_LERROR, SAMP ": failed to create a metric set.\n");
		goto err;
	}
	msglog(LDMSD_LDEBUG, SAMP ": config done.\n");
	return 0;
 err:
	base_del(base);
	msglog(LDMSD_LDEBUG, SAMP ": config fail.\n");
	return rc;
}

static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	return set;
}

static int sample(struct ldmsd_sampler *self)
{
	int rc = 0;
	if (!set) {
		msglog(LDMSD_LDEBUG, SAMP ": plugin not initialized\n");
		return EINVAL;
	}
	PID_DATA;
	int iorc = -1, statrc = -1, statmrc = -1;
	base_sample_begin(base);
	if (pidopts & PID_IO) {
		iorc = parse_proc_pid_io(&io, pids);
		if (!iorc) {
			IOLIST(IOSAMPLE);
		}
	}
	if (pidopts & PID_STAT) {
		statrc = parse_proc_pid_stat(&stat, pids);
		if (!statrc) {
			STATLIST(STATSAMPLE);
		}
	}
	if (pidopts & PID_STATM) {
		statmrc = parse_proc_pid_statm(&statm, pids);
		if (!statmrc) {
			STATMLIST(STATMSAMPLE);
		}
	}
	if (pidopts & PID_MMALLOC) {
		mm_stats(&mms);
		mmm.used_holes = mms.size - mms.grain * mms.largest;
		MMALLOCLIST(MMALLOCSAMPLE);
	}
	base_sample_end(base);
	return rc;
}

static void term(struct ldmsd_plugin *self)
{
	if (base)
		base_del(base);
	if (set)
		ldms_set_delete(set);
	set = NULL;
}

static struct ldmsd_sampler meminfo_plugin = {
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
	set = NULL;
	return &meminfo_plugin.base;
}
