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
#include "ldmsd_plug_api.h"
#include "sampler_base.h"
#include "stdbool.h"
#include "parse_stat.h"
#include "mmalloc/mmalloc.h"
#include "ldmsd_plugattr.h"

#define SAMP "dstat"

#define PID_IO 0x1
#define PID_STAT 0x2
#define PID_STATM 0x4
#define PID_MMALLOC 0x8
#define PID_FD 0x10
#define PID_FDTYPES 0x20
#define PID_TICK 0x40

#define PID_DATA \
	struct proc_pid_fd fd; \
	struct proc_pid_io io; \
	struct proc_pid_stat stat; \
	struct proc_pid_statm statm; \
	struct mm_stat mms; \
	struct mm_metrics mmm

struct mm_metrics {
	uint64_t used_holes;
};

static ovis_log_t mylog;

static int pidopts = 0; /* pid-opts: bitwise-or of the PID_ flags */
static long tick;

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
		ovis_log(mylog, OVIS_LERROR, "bad bool value %s for %s\n",
			val, name);
		return false;
	}
}

/* set pidopts and compute schema name from them in buf.
 * If pidopts is 0 on return, sampler has nothing to do.
 * Caller must free result.
 */
static char * compute_pidopts_schema(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *buf;
	size_t bsz;
	char *doio, *dostat, *dotick, *dostatm, *dommalloc, *dofd, *dofdtypes;
	char *schema_name = av_value(avl, "schema");
	dotick = av_value(avl, "sc_clk_tck");
	doio = av_value(avl, "io");
	dofd = av_value(avl, "fd");
	dofdtypes = av_value(avl, "fdtypes");
	dostat = av_value(avl, "stat");
	dostatm = av_value(avl, "statm");
	dommalloc = av_value(avl, "mmalloc");
	char *as = av_value(avl, "auto-schema");
	bool dosuffix = (as && get_bool(as, "auto-schema") );
	if (!doio && !dostat && !dostatm && !dommalloc &&
		!dofd && !dofdtypes && !dotick) {
		pidopts = ( PID_IO | PID_STAT | PID_STATM );
	} else {
		if (doio && get_bool(doio, "io"))
			pidopts |= PID_IO;
		if (dostat && get_bool(dostat, "stat"))
			pidopts |= PID_STAT;
		if (dotick && get_bool(dotick, "sc_clk_tck")) {
			pidopts |= PID_TICK;
			tick = sysconf(_SC_CLK_TCK);
		}
		if (dostatm && get_bool(dostatm, "statm"))
			pidopts |= PID_STATM;
		if (dommalloc && get_bool(dommalloc, "mmalloc"))
			pidopts |= PID_MMALLOC;
		if (dofd && get_bool(dofd, "fd"))
			pidopts |= PID_FD;
		if (dofdtypes && get_bool(dofdtypes, "fdtypes"))
			pidopts |= PID_FDTYPES;
		if (pidopts & PID_FDTYPES)
			pidopts |= PID_FD;
	}

	if (schema_name) {
		return strdup(schema_name);
	}
	schema_name = "dstat";
	bsz = strlen(schema_name) + 18; /* len(schema_name) + len("_<HEX>") + 1 */
	buf = malloc(bsz);
	if (!buf)
		return NULL;
	if (dosuffix) {
		snprintf(buf, bsz, "%s_%x", schema_name, pidopts);
	} else {
		snprintf(buf, bsz, "%s", schema_name);
	}
	return buf;
}

#ifndef MAIN
static ldms_set_t set = NULL;
static ldms_schema_t schema;
static base_data_t base;
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

#define FDLIST(WRAP) \
	WRAP("fd_count", fd_count, LDMS_V_U64, pos_fd_count)

#define FDTYPESLIST(WRAP) \
	WRAP("fd_max", fd_max, LDMS_V_U64, pos_fd_max) \
	WRAP("fd_socket", fd_socket, LDMS_V_U64, pos_fd_socket) \
	WRAP("fd_dev", fd_dev, LDMS_V_U64, pos_fd_dev) \
	WRAP("fd_anon_inode", fd_anon_inode, LDMS_V_U64, pos_fd_anon_inode) \
	WRAP("fd_pipe", fd_pipe, LDMS_V_U64, pos_fd_pipe) \
	WRAP("fd_path", fd_path, LDMS_V_U64, pos_fd_path)

#define DECLPOS(n, m, t, p) static int p = -1;

IOLIST(DECLPOS);
static int pos_pid = -1;
static int pos_ppid = -1;
static int pos_tick = -1;
STATLIST(DECLPOS);
STATMLIST(DECLPOS);
MMALLOCLIST(DECLPOS);
FDLIST(DECLPOS);
FDTYPESLIST(DECLPOS);

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
	int rc = 0;

	int iorc = -1;
	int statrc = -1;
	int statmrc = -1;
	int fdrc = -1;
	PID_DATA;
	if (pidopts & PID_IO) {
		iorc = parse_proc_pid_io(&io, pids);
		if (iorc != 0) {
			ovis_log(mylog, OVIS_LERROR, "unable to read /proc/%s/io (%s)\n",
				pids, STRERROR(iorc));
			return iorc;
		}
	}
	if (pidopts & PID_STAT) {
		statrc = parse_proc_pid_stat(&stat, pids);
		if (statrc != 0) {
			ovis_log(mylog, OVIS_LERROR, "unable to read /proc/%s/stat (%s)\n",
				pids, STRERROR(statrc));
			return statrc;
		}
	}
	if (pidopts & PID_STATM) {
		statmrc = parse_proc_pid_statm(&statm, pids);
		if (statmrc != 0) {
			ovis_log(mylog, OVIS_LERROR, "unable to read /proc/%s/statm (%s)\n",
				pids, STRERROR(statmrc));
			return statmrc;
		}
	}
	if (pidopts & (PID_FD|PID_FDTYPES)) {
		bool details = ((pidopts & PID_FDTYPES) != 0);
		fdrc = parse_proc_pid_fd(&fd, pids, details);
		if (fdrc != 0) {
			ovis_log(mylog, OVIS_LERROR, "unable to read /proc/%s/fd (%s)\n",
				pids, STRERROR(fdrc));
			return fdrc;
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
	if (pidopts & PID_FD) {
		FDLIST(METRIC);
	}
	if (pidopts & PID_FDTYPES) {
		FDTYPESLIST(METRIC);
	}
	if (pidopts & PID_TICK) {
		META("sc_clk_tck", sc_clk_tck, LDMS_V_U64, pos_tick);
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

#define FDSAMPLE(n, m, t, p) \
	ldms_metric_set_u32(set, p, (uint32_t)fd.m);

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
		ovis_log(mylog, OVIS_LERROR, "sample " n " not correctly defined.\n"); \
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
	if (pidopts & PID_FD) {
		FDLIST(FDSAMPLE);
	}
	if (pidopts & PID_FDTYPES) {
		FDTYPESLIST(FDSAMPLE);
	}
	if (pidopts & PID_TICK) {
		ldms_metric_set_u64(set, pos_tick, (uint64_t)tick);
	}

	base_sample_end(base);
	return 0;

 err:
	if (schema)
		ldms_schema_delete(schema);

	return rc;
}




static const char *usage(ldmsd_plug_handle_t handle)
{
	return  "config name=" SAMP " " BASE_CONFIG_USAGE
		" [io=<bool>] [stat=<bool>] [statm=<bool>] [mmalloc=<bool>] [fd=<bool>] [fdtypes=<bool>] [sc_clk_tck=<1/*>\n"
		"    If none of io, stat, statm, mmalloc is given, all but mmalloc, fd & fdtypes default true.\n"
		"    If any of io, stat, statm is given, any unmentioned ones default false.\n"
		"    Enabling mmalloc, fd, or fdtypes is potentially expensive at high frequencies on aggregators.\n"
		"    <bool>       Enable data subset if true.\n";
}

static const char *dstat_opts[] = {
	"debug",
	"schema",
	"instance",
	"producer",
	"component_id",
	"uid",
	"gid",
	"perm",
	"job_set",
	"set_array_card",
	"io",
	"stat",
	"statm",
	"mmalloc",
	"fd",
	"fdtypes",
	"auto-schema",
	"sc_clk_tck",
	NULL
};

static const char *dstat_words[] = {
	"debug",
	NULL
};
/**
 * \brief Configuration
 *
 * See usage().
 */
static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	int rc;

	if (set) {
		ovis_log(mylog, OVIS_LERROR, "Set already created.\n");
		return EINVAL;
	}
	ovis_log(mylog, OVIS_LDEBUG, "config started.\n");

	rc = ldmsd_plugattr_config_check(dstat_opts, dstat_words, avl, kwl,
		NULL, SAMP);
	if (rc) {
		ovis_log(mylog, OVIS_LERROR, "bad config options.\n");
		return EINVAL;
	}

	char *sbuf = compute_pidopts_schema(kwl, avl);
	if (!sbuf) {
		ovis_log(mylog, OVIS_LERROR, "configure out of memory.\n");
		rc = EINVAL;
		goto err;
	} else {
		ovis_log(mylog, OVIS_LDEBUG, "configure dstat with schema %s.\n",
		sbuf);
	}

	if (!pidopts) {
		ovis_log(mylog, OVIS_LERROR, "configured with nothing to do.\n");
		rc = EINVAL;
		goto err;
	}

	base = base_config(avl, ldmsd_plug_cfg_name_get(handle), sbuf, mylog);
	if (!base) {
		rc = errno;
		goto err;
	}

	rc = create_metric_set(base);
	if (rc) {
		ovis_log(mylog, OVIS_LERROR, "failed to create a metric set.\n");
		goto err;
	}
	ovis_log(mylog, OVIS_LDEBUG, "config done.\n");
	free(sbuf);
	return 0;
 err:
	base_del(base);
	if (sbuf)
		free(sbuf);
	ovis_log(mylog, OVIS_LDEBUG, "config fail.\n");
	return rc;
}

static int sample(ldmsd_plug_handle_t handle)
{
	int rc = 0;
	if (!set) {
		ovis_log(mylog, OVIS_LDEBUG, "plugin not initialized\n");
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
	if (pidopts & (PID_FD | PID_FDTYPES)) {
		bool details = ((pidopts & PID_FDTYPES) != 0);
		int fdrc = parse_proc_pid_fd(&fd, pids, details);
		if (!fdrc) {
			FDLIST(FDSAMPLE);
			if (details) {
				FDTYPESLIST(FDSAMPLE);
			}
		}
	}
	base_sample_end(base);
	return rc;
}

static int constructor(ldmsd_plug_handle_t handle)
{
	mylog = ldmsd_plug_log_get(handle);
        set = NULL;

        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
	if (base)
		base_del(base);
	if (set)
		ldms_set_delete(set);
	set = NULL;
}

struct ldmsd_sampler ldmsd_plugin_interface = {
	.base = {
		.type = LDMSD_PLUGIN_SAMPLER,
		.config = config,
		.usage = usage,
		.constructor = constructor,
		.destructor = destructor,
	},
	.sample = sample,
};

#else /* MAIN */
#include "dstring.h"
#include "ctype.h"
int main(int argc, char **argv)
{
	int rc;
        dstring_t ds;
        dstr_init2(&ds, 1024);
	char *sbuf = 0;
        int i;
        for (i = 1; i < argc; i++) {
                dstrcat(&ds, argv[i], DSTRING_ALL);
                dstrcat(&ds, " ", 1);
        }
        char *buf = dstr_extract(&ds);
        int size = 1;
        char *t = buf;
        while (t[0] != '\0') {
                if (isspace(t[0])) size++;
                t++;
        }
        struct attr_value_list *avl = av_new(size);
        struct attr_value_list *kwl = av_new(size);
        rc = tokenize(buf, kwl, avl);
        if (rc) {
                fprintf(stderr, SAMP " failed to parse arguments. %s\n", buf);
                rc = EINVAL;
                goto out;
        }
	rc = 0;
	sbuf = compute_pidopts_schema(kwl, avl);
        if (!sbuf || !strlen(sbuf)) {
                fprintf(stderr, "could not create schema from options\n");
                rc = EINVAL;
                goto out;
        }
        printf("%s\n", sbuf);
out:
	free(sbuf);
	free(buf);
        av_free(kwl);
        av_free(avl);
	return rc;
}

#endif
