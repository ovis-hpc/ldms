/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2020-2021 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2020-2021 Open Grid Computing, Inc. All rights reserved.
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
 * \file linux_proc_sampler.c
 */

#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdarg.h>
#include <dirent.h>
#include <ctype.h>
#include <assert.h>
#include <sys/sysmacros.h>

#include <coll/rbt.h>

#include "ldmsd.h"
#include "../sampler_base.h"
#include "ldmsd_stream.h"
#include "mmalloc.h"
#define DSTRING_USE_SHORT
#include "ovis_util/dstring.h"

#define SAMP "linux_proc_sampler"

#define DEFAULT_STREAM "slurm"

#define INST_LOG(inst, lvl, fmt, ...) \
		 inst->log((lvl), "%s: " fmt, SAMP, ##__VA_ARGS__)

#ifndef ARRAY_LEN
#define ARRAY_LEN(a) (sizeof((a))/sizeof((a)[0]))
#endif

/*#define LPDEBUG */

typedef enum linux_proc_sampler_metric linux_proc_sampler_metric_e;
enum linux_proc_sampler_metric {
	APP_ALL = 0,

	/* /proc/[pid]/cmdline */
	APP_CMDLINE_LEN, _APP_CMDLINE_FIRST = APP_CMDLINE_LEN,
			 _APP_FIRST = _APP_CMDLINE_FIRST,
	APP_CMDLINE, _APP_CMDLINE_LAST = APP_CMDLINE,

	/* number of open files */
	APP_N_OPEN_FILES,

	/* io */
	APP_IO_READ_B, _APP_IO_FIRST = APP_IO_READ_B,
	APP_IO_WRITE_B,
	APP_IO_N_READ,
	APP_IO_N_WRITE,
	APP_IO_READ_DEV_B,
	APP_IO_WRITE_DEV_B,
	APP_IO_WRITE_CANCELLED_B, _APP_IO_LAST = APP_IO_WRITE_CANCELLED_B,

	APP_OOM_SCORE,

	APP_OOM_SCORE_ADJ,

	APP_ROOT,

	/* /proc/[pid]/stat */
	APP_STAT_PID, _APP_STAT_FIRST = APP_STAT_PID,
	APP_STAT_COMM,
	APP_STAT_STATE,
	APP_STAT_PPID,
	APP_STAT_PGRP,
	APP_STAT_SESSION,
	APP_STAT_TTY_NR,
	APP_STAT_TPGID,
	APP_STAT_FLAGS,
	APP_STAT_MINFLT,
	APP_STAT_CMINFLT,
	APP_STAT_MAJFLT,
	APP_STAT_CMAJFLT,
	APP_STAT_UTIME,
	APP_STAT_STIME,
	APP_STAT_CUTIME,
	APP_STAT_CSTIME,
	APP_STAT_PRIORITY,
	APP_STAT_NICE,
	APP_STAT_NUM_THREADS,
	APP_STAT_ITREALVALUE,
	APP_STAT_STARTTIME,
	APP_STAT_VSIZE,
	APP_STAT_RSS,
	APP_STAT_RSSLIM,
	APP_STAT_STARTCODE,
	APP_STAT_ENDCODE,
	APP_STAT_STARTSTACK,
	APP_STAT_KSTKESP,
	APP_STAT_KSTKEIP,
	APP_STAT_SIGNAL,
	APP_STAT_BLOCKED,
	APP_STAT_SIGIGNORE,
	APP_STAT_SIGCATCH,
	APP_STAT_WCHAN,
	APP_STAT_NSWAP,
	APP_STAT_CNSWAP,
	APP_STAT_EXIT_SIGNAL,
	APP_STAT_PROCESSOR,
	APP_STAT_RT_PRIORITY,
	APP_STAT_POLICY,
	APP_STAT_DELAYACCT_BLKIO_TICKS,
	APP_STAT_GUEST_TIME,
	APP_STAT_CGUEST_TIME,
	APP_STAT_START_DATA,
	APP_STAT_END_DATA,
	APP_STAT_START_BRK,
	APP_STAT_ARG_START,
	APP_STAT_ARG_END,
	APP_STAT_ENV_START,
	APP_STAT_ENV_END,
	APP_STAT_EXIT_CODE, _APP_STAT_LAST = APP_STAT_EXIT_CODE,

	APP_STATUS_NAME, _APP_STATUS_FIRST = APP_STATUS_NAME,
	APP_STATUS_UMASK,
	APP_STATUS_STATE,
	APP_STATUS_TGID,
	APP_STATUS_NGID,
	APP_STATUS_PID,
	APP_STATUS_PPID,
	APP_STATUS_TRACERPID,
	APP_STATUS_UID,
	APP_STATUS_GID,
	APP_STATUS_FDSIZE,
	APP_STATUS_GROUPS,
	APP_STATUS_NSTGID,
	APP_STATUS_NSPID,
	APP_STATUS_NSPGID,
	APP_STATUS_NSSID,
	APP_STATUS_VMPEAK,
	APP_STATUS_VMSIZE,
	APP_STATUS_VMLCK,
	APP_STATUS_VMPIN,
	APP_STATUS_VMHWM,
	APP_STATUS_VMRSS,
	APP_STATUS_RSSANON,
	APP_STATUS_RSSFILE,
	APP_STATUS_RSSSHMEM,
	APP_STATUS_VMDATA,
	APP_STATUS_VMSTK,
	APP_STATUS_VMEXE,
	APP_STATUS_VMLIB,
	APP_STATUS_VMPTE,
	APP_STATUS_VMPMD,
	APP_STATUS_VMSWAP,
	APP_STATUS_HUGETLBPAGES,
	APP_STATUS_COREDUMPING,
	APP_STATUS_THREADS,
	APP_STATUS_SIG_QUEUED,
	APP_STATUS_SIG_LIMIT,
	APP_STATUS_SIGPND,
	APP_STATUS_SHDPND,
	APP_STATUS_SIGBLK,
	APP_STATUS_SIGIGN,
	APP_STATUS_SIGCGT,
	APP_STATUS_CAPINH,
	APP_STATUS_CAPPRM,
	APP_STATUS_CAPEFF,
	APP_STATUS_CAPBND,
	APP_STATUS_CAPAMB,
	APP_STATUS_NONEWPRIVS,
	APP_STATUS_SECCOMP,
	APP_STATUS_SPECULATION_STORE_BYPASS,
	APP_STATUS_CPUS_ALLOWED,
	APP_STATUS_CPUS_ALLOWED_LIST,
	APP_STATUS_MEMS_ALLOWED,
	APP_STATUS_MEMS_ALLOWED_LIST,
	APP_STATUS_VOLUNTARY_CTXT_SWITCHES,
	APP_STATUS_NONVOLUNTARY_CTXT_SWITCHES,
	_APP_STATUS_LAST = APP_STATUS_NONVOLUNTARY_CTXT_SWITCHES,

	APP_SYSCALL,
	APP_SYSCALL_NAME, /* string */

	APP_TIMERSLACK_NS,

	APP_WCHAN, /* string */

	APP_TIMING,

	_APP_LAST = APP_TIMING,
};

typedef struct linux_proc_sampler_metric_info_s *linux_proc_sampler_metric_info_t;
struct linux_proc_sampler_metric_info_s {
	linux_proc_sampler_metric_e code;
	const char *name;
	const char *unit;
	enum ldms_value_type mtype;
	int array_len; /* in case mtype is _ARRAY */
	int is_meta;
};

/* CMDLINE_SZ is used for loading /proc/pid/[status,cmdline,syscall,stat] */
#define CMDLINE_SZ 4096
/* space for a /proc/$pid/<leaf> name */
#define PROCPID_SZ 64
/* #define MOUNTINFO_SZ 8192 */
#define ROOT_SZ 4096
#define STAT_COMM_SZ 4096
#define WCHAN_SZ 128 /* NOTE: KSYM_NAME_LEN = 128 */
#define SPECULATION_SZ 64
#define SYSCALL_MAX 64 /* longest syscall name */
#define GROUPS_SZ 16
#define NS_SZ 16
#define CPUS_ALLOWED_SZ 4 /* 4 x 64 = 256 bits max */
#define MEMS_ALLOWED_SZ 128 /* 128 x 64 = 8192 bits max */
#define CPUS_ALLOWED_LIST_SZ 128 /* length of list string */
#define MEMS_ALLOWED_LIST_SZ 128 /* length of list string */

struct linux_proc_sampler_metric_info_s metric_info[] = {
	[APP_ALL] = { APP_ALL, "ALL", "", 0, 0, 0 },
	/* /proc/[pid]/cmdline */
	[APP_CMDLINE_LEN] = { APP_CMDLINE_LEN, "cmdline_len", "" , LDMS_V_U16, 0, 1 },
	[APP_CMDLINE] = { APP_CMDLINE, "cmdline", "", LDMS_V_CHAR_ARRAY, CMDLINE_SZ, 1 },

	/* number of open files */
	[APP_N_OPEN_FILES] = { APP_N_OPEN_FILES, "n_open_files", "", LDMS_V_U64, 0, 0 },

	/* io */
	[APP_IO_READ_B] = { APP_IO_READ_B, "io_read_b", "B", LDMS_V_U64, 0, 0 },
	[APP_IO_WRITE_B] = { APP_IO_WRITE_B, "io_write_b", "B", LDMS_V_U64, 0, 0 },
	[APP_IO_N_READ] = { APP_IO_N_READ, "io_n_read", "", LDMS_V_U64, 0, 0 },
	[APP_IO_N_WRITE] = { APP_IO_N_WRITE, "io_n_write", "", LDMS_V_U64, 0, 0 },
	[APP_IO_READ_DEV_B] = { APP_IO_READ_DEV_B, "io_read_dev_b", "B", LDMS_V_U64, 0, 0 },
	[APP_IO_WRITE_DEV_B] = { APP_IO_WRITE_DEV_B, "io_write_dev_b", "B", LDMS_V_U64, 0, 0 },
	[APP_IO_WRITE_CANCELLED_B] = { APP_IO_WRITE_CANCELLED_B, "io_write_cancelled_b", "B", LDMS_V_U64, 0, 0 },

	[APP_OOM_SCORE] = { APP_OOM_SCORE, "oom_score", "", LDMS_V_U64, 0, 0 },

	[APP_OOM_SCORE_ADJ] = { APP_OOM_SCORE_ADJ, "oom_score_adj", "", LDMS_V_U64, 0, 0 },

	[APP_ROOT] = { APP_ROOT, "root", "", LDMS_V_CHAR_ARRAY, ROOT_SZ, 1 },

	/* /proc/[pid]/stat */
	[APP_STAT_PID] = { APP_STAT_PID, "stat_pid", "", LDMS_V_U64, 0, 0 },
	[APP_STAT_COMM] = { APP_STAT_COMM, "stat_comm", "", LDMS_V_CHAR_ARRAY, STAT_COMM_SZ, 1 },
	[APP_STAT_STATE] = { APP_STAT_STATE, "stat_state", "", LDMS_V_CHAR, 0, 0 },
	[APP_STAT_PPID] = { APP_STAT_PPID, "stat_ppid", "", LDMS_V_U64, 0, 0 },
	[APP_STAT_PGRP] = { APP_STAT_PGRP, "stat_pgrp", "", LDMS_V_U64, 0, 0 },
	[APP_STAT_SESSION] = { APP_STAT_SESSION, "stat_session", "", LDMS_V_U64, 0, 0 },
	[APP_STAT_TTY_NR] = { APP_STAT_TTY_NR, "stat_tty_nr", "", LDMS_V_U64, 0, 0 },
	[APP_STAT_TPGID] = { APP_STAT_TPGID, "stat_tpgid", "", LDMS_V_U64, 0, 0 },
	[APP_STAT_FLAGS] = { APP_STAT_FLAGS, "stat_flags", "", LDMS_V_U64, 0, 0 },
	[APP_STAT_MINFLT] = { APP_STAT_MINFLT, "stat_minflt", "", LDMS_V_U64, 0, 0 },
	[APP_STAT_CMINFLT] = { APP_STAT_CMINFLT, "stat_cminflt", "", LDMS_V_U64, 0, 0 },
	[APP_STAT_MAJFLT] = { APP_STAT_MAJFLT, "stat_majflt", "", LDMS_V_U64, 0, 0 },
	[APP_STAT_CMAJFLT] = { APP_STAT_CMAJFLT, "stat_cmajflt", "", LDMS_V_U64, 0, 0 },
	[APP_STAT_UTIME] = { APP_STAT_UTIME, "stat_utime", "ticks", LDMS_V_U64, 0, 0 },
	[APP_STAT_STIME] = { APP_STAT_STIME, "stat_stime", "ticks", LDMS_V_U64, 0, 0 },
	[APP_STAT_CUTIME] = { APP_STAT_CUTIME, "stat_cutime", "ticks", LDMS_V_U64, 0, 0 },
	[APP_STAT_CSTIME] = { APP_STAT_CSTIME, "stat_cstime", "ticks", LDMS_V_U64, 0, 0 },
	[APP_STAT_PRIORITY] = { APP_STAT_PRIORITY, "stat_priority", "", LDMS_V_U64, 0, 0 },
	[APP_STAT_NICE] = { APP_STAT_NICE, "stat_nice", "", LDMS_V_U64, 0, 0 },
	[APP_STAT_NUM_THREADS] =
		{ APP_STAT_NUM_THREADS, "stat_num_threads", "", LDMS_V_U64, 0, 0 },
	[APP_STAT_ITREALVALUE] =
		{ APP_STAT_ITREALVALUE, "stat_itrealvalue", "ticks", LDMS_V_U64, 0, 0 },
	[APP_STAT_STARTTIME] =
		{ APP_STAT_STARTTIME, "stat_starttime", "ticks", LDMS_V_U64, 0, 0 },
	[APP_STAT_VSIZE] = { APP_STAT_VSIZE, "stat_vsize", "B", LDMS_V_U64, 0, 0 },
	[APP_STAT_RSS] = { APP_STAT_RSS, "stat_rss", "PG", LDMS_V_U64, 0, 0 },
	[APP_STAT_RSSLIM] = { APP_STAT_RSSLIM, "stat_rsslim", "B", LDMS_V_U64, 0, 0 },
	[APP_STAT_STARTCODE] = { APP_STAT_STARTCODE, "stat_startcode", "PTR", LDMS_V_U64, 0, 0 },
	[APP_STAT_ENDCODE] = { APP_STAT_ENDCODE, "stat_endcode", "PTR", LDMS_V_U64, 0, 0 },
	[APP_STAT_STARTSTACK] =
		{ APP_STAT_STARTSTACK, "stat_startstack", "PTR", LDMS_V_U64, 0, 0 },
	[APP_STAT_KSTKESP] = { APP_STAT_KSTKESP, "stat_kstkesp", "PTR", LDMS_V_U64, 0, 0 },
	[APP_STAT_KSTKEIP] = { APP_STAT_KSTKEIP, "stat_kstkeip", "PTR", LDMS_V_U64, 0, 0 },
	[APP_STAT_SIGNAL] = { APP_STAT_SIGNAL, "stat_signal", "", LDMS_V_U64, 0, 0 },
	[APP_STAT_BLOCKED] = { APP_STAT_BLOCKED, "stat_blocked", "", LDMS_V_U64, 0, 0 },
	[APP_STAT_SIGIGNORE] = { APP_STAT_SIGIGNORE, "stat_sigignore", "", LDMS_V_U64, 0, 0 },
	[APP_STAT_SIGCATCH] = { APP_STAT_SIGCATCH, "stat_sigcatch", "", LDMS_V_U64, 0, 0 },
	[APP_STAT_WCHAN] = { APP_STAT_WCHAN, "stat_wchan", "PTR", LDMS_V_U64, 0, 0 },
	[APP_STAT_NSWAP] = { APP_STAT_NSWAP, "stat_nswap", "PG", LDMS_V_U64, 0, 0 },
	[APP_STAT_CNSWAP] = { APP_STAT_CNSWAP, "stat_cnswap", "PG", LDMS_V_U64, 0, 0 },
	[APP_STAT_EXIT_SIGNAL] = { APP_STAT_EXIT_SIGNAL, "stat_exit_signal", "", LDMS_V_U64, 0, 0 },
	[APP_STAT_PROCESSOR] = { APP_STAT_PROCESSOR, "stat_processor", "", LDMS_V_U64, 0, 0 },
	[APP_STAT_RT_PRIORITY] = { APP_STAT_RT_PRIORITY, "stat_rt_priority", "", LDMS_V_U64, 0, 0 },
	[APP_STAT_POLICY] = { APP_STAT_POLICY, "stat_policy", "", LDMS_V_U64, 0, 0 },
	[APP_STAT_DELAYACCT_BLKIO_TICKS] = { APP_STAT_DELAYACCT_BLKIO_TICKS, "stat_delayacct_blkio_ticks", "ticks", LDMS_V_U64, 0, 0 },
	[APP_STAT_GUEST_TIME] = { APP_STAT_GUEST_TIME, "stat_guest_time", "ticks", LDMS_V_U64, 0, 0 },
	[APP_STAT_CGUEST_TIME] = { APP_STAT_CGUEST_TIME, "stat_cguest_time", "ticks", LDMS_V_U64, 0, 0 },
	[APP_STAT_START_DATA] = { APP_STAT_START_DATA, "stat_start_data", "PTR", LDMS_V_U64, 0, 0 },
	[APP_STAT_END_DATA] = { APP_STAT_END_DATA, "stat_end_data", "PTR", LDMS_V_U64, 0, 0 },
	[APP_STAT_START_BRK] = { APP_STAT_START_BRK, "stat_start_brk", "PTR", LDMS_V_U64, 0, 0 },
	[APP_STAT_ARG_START] = { APP_STAT_ARG_START, "stat_arg_start", "PTR", LDMS_V_U64, 0, 0 },
	[APP_STAT_ARG_END] = { APP_STAT_ARG_END, "stat_arg_end", "PTR", LDMS_V_U64, 0, 0 },
	[APP_STAT_ENV_START] = { APP_STAT_ENV_START, "stat_env_start", "PTR", LDMS_V_U64, 0, 0 },
	[APP_STAT_ENV_END] = { APP_STAT_ENV_END, "stat_env_end", "PTR", LDMS_V_U64, 0, 0 },
	[APP_STAT_EXIT_CODE] = { APP_STAT_EXIT_CODE, "stat_exit_code", "", LDMS_V_U64, 0, 0 },

	/* /proc/[pid]/status */
	[APP_STATUS_NAME] = { APP_STATUS_NAME, "status_name", "", LDMS_V_CHAR_ARRAY, STAT_COMM_SZ, 1 },
	[APP_STATUS_UMASK] = { APP_STATUS_UMASK, "status_umask", "", LDMS_V_U32, 0, 0 },
	[APP_STATUS_STATE] = { APP_STATUS_STATE, "status_state", "", LDMS_V_CHAR, 0, 0 },
	[APP_STATUS_TGID] = { APP_STATUS_TGID, "status_tgid", "", LDMS_V_U64, 0, 0 },
	[APP_STATUS_NGID] = { APP_STATUS_NGID, "status_ngid", "", LDMS_V_U64, 0, 0 },
	[APP_STATUS_PID] = { APP_STATUS_PID, "status_pid", "" , LDMS_V_U64, 0, 0},
	[APP_STATUS_PPID] = { APP_STATUS_PPID, "status_ppid", "" , LDMS_V_U64, 0, 0},
	[APP_STATUS_TRACERPID] =
		{ APP_STATUS_TRACERPID, "status_tracerpid", "" , LDMS_V_U64, 0, 0},
	[APP_STATUS_UID] = { APP_STATUS_UID, "status_uid", "", LDMS_V_U64_ARRAY, 4, 0 },
	[APP_STATUS_GID] = { APP_STATUS_GID, "status_gid", "" , LDMS_V_U64_ARRAY, 4, 0},
	[APP_STATUS_FDSIZE] = { APP_STATUS_FDSIZE, "status_fdsize", "" , LDMS_V_U64, 0, 0},
	[APP_STATUS_GROUPS] = { APP_STATUS_GROUPS, "status_groups", "", LDMS_V_U64_ARRAY, GROUPS_SZ, 0 },
	[APP_STATUS_NSTGID] = { APP_STATUS_NSTGID, "status_nstgid", "", LDMS_V_U64_ARRAY, NS_SZ, 0 },
	[APP_STATUS_NSPID] = { APP_STATUS_NSPID, "status_nspid", "", LDMS_V_U64_ARRAY, NS_SZ, 0 },
	[APP_STATUS_NSPGID] = { APP_STATUS_NSPGID, "status_nspgid", "", LDMS_V_U64_ARRAY, NS_SZ, 0 },
	[APP_STATUS_NSSID] = { APP_STATUS_NSSID, "status_nssid", "", LDMS_V_U64_ARRAY, NS_SZ, 0 },
	[APP_STATUS_VMPEAK] = { APP_STATUS_VMPEAK, "status_vmpeak", "kB", LDMS_V_U64, 0, 0 },
	[APP_STATUS_VMSIZE] = { APP_STATUS_VMSIZE, "status_vmsize", "kB", LDMS_V_U64, 0, 0 },
	[APP_STATUS_VMLCK] = { APP_STATUS_VMLCK, "status_vmlck", "kB", LDMS_V_U64, 0, 0 },
	[APP_STATUS_VMPIN] = { APP_STATUS_VMPIN, "status_vmpin", "kB", LDMS_V_U64, 0, 0 },
	[APP_STATUS_VMHWM] = { APP_STATUS_VMHWM, "status_vmhwm", "kB", LDMS_V_U64, 0, 0 },
	[APP_STATUS_VMRSS] = { APP_STATUS_VMRSS, "status_vmrss", "kB", LDMS_V_U64, 0, 0 },
	[APP_STATUS_RSSANON] = { APP_STATUS_RSSANON, "status_rssanon", "kB", LDMS_V_U64, 0, 0 },
	[APP_STATUS_RSSFILE] = { APP_STATUS_RSSFILE, "status_rssfile", "kB", LDMS_V_U64, 0, 0 },
	[APP_STATUS_RSSSHMEM] = { APP_STATUS_RSSSHMEM, "status_rssshmem", "kB", LDMS_V_U64, 0, 0 },
	[APP_STATUS_VMDATA] = { APP_STATUS_VMDATA, "status_vmdata", "kB", LDMS_V_U64, 0, 0 },
	[APP_STATUS_VMSTK] = { APP_STATUS_VMSTK, "status_vmstk", "kB", LDMS_V_U64, 0, 0 },
	[APP_STATUS_VMEXE] = { APP_STATUS_VMEXE, "status_vmexe", "kB", LDMS_V_U64, 0, 0 },
	[APP_STATUS_VMLIB] = { APP_STATUS_VMLIB, "status_vmlib", "kB", LDMS_V_U64, 0, 0 },
	[APP_STATUS_VMPTE] = { APP_STATUS_VMPTE, "status_vmpte", "kB", LDMS_V_U64, 0, 0 },
	[APP_STATUS_VMPMD] = { APP_STATUS_VMPMD, "status_vmpmd", "kB", LDMS_V_U64, 0, 0 },
	[APP_STATUS_VMSWAP] = { APP_STATUS_VMSWAP, "status_vmswap", "kB", LDMS_V_U64, 0, 0 },
	[APP_STATUS_HUGETLBPAGES] = { APP_STATUS_HUGETLBPAGES, "status_hugetlbpages", "kB", LDMS_V_U64, 0, 0 },
	[APP_STATUS_COREDUMPING] = { APP_STATUS_COREDUMPING, "status_coredumping", "bool", LDMS_V_U8, 0, 0 },
	[APP_STATUS_THREADS] = { APP_STATUS_THREADS, "status_threads", "", LDMS_V_U64, 0, 0 },
	[APP_STATUS_SIG_QUEUED] = { APP_STATUS_SIG_QUEUED, "status_sig_queued", "", LDMS_V_U64, 0, 0 },
	[APP_STATUS_SIG_LIMIT] = { APP_STATUS_SIG_LIMIT, "status_sig_limit", "", LDMS_V_U64, 0, 0},
	[APP_STATUS_SIGPND] = { APP_STATUS_SIGPND, "status_sigpnd", "", LDMS_V_U64, 0, 0 },
	[APP_STATUS_SHDPND] = { APP_STATUS_SHDPND, "status_shdpnd", "", LDMS_V_U64, 0, 0 },
	[APP_STATUS_SIGBLK] = { APP_STATUS_SIGBLK, "status_sigblk", "", LDMS_V_U64, 0, 0 },
	[APP_STATUS_SIGIGN] = { APP_STATUS_SIGIGN, "status_sigign", "", LDMS_V_U64, 0, 0 },
	[APP_STATUS_SIGCGT] = { APP_STATUS_SIGCGT, "status_sigcgt", "", LDMS_V_U64, 0, 0 },
	[APP_STATUS_CAPINH] = { APP_STATUS_CAPINH, "status_capinh", "", LDMS_V_U64, 0, 0 },
	[APP_STATUS_CAPPRM] = { APP_STATUS_CAPPRM, "status_capprm", "", LDMS_V_U64, 0, 0 },
	[APP_STATUS_CAPEFF] = { APP_STATUS_CAPEFF, "status_capeff", "", LDMS_V_U64, 0, 0 },
	[APP_STATUS_CAPBND] = { APP_STATUS_CAPBND, "status_capbnd", "", LDMS_V_U64, 0, 0 },
	[APP_STATUS_CAPAMB] = { APP_STATUS_CAPAMB, "status_capamb", "", LDMS_V_U64, 0, 0 },
	[APP_STATUS_NONEWPRIVS] = { APP_STATUS_NONEWPRIVS, "status_nonewprivs", "", LDMS_V_U64, 0, 0 },
	[APP_STATUS_SECCOMP] = { APP_STATUS_SECCOMP, "status_seccomp", "", LDMS_V_U64, 0, 0 },
	[APP_STATUS_SPECULATION_STORE_BYPASS] = { APP_STATUS_SPECULATION_STORE_BYPASS, "status_speculation_store_bypass", "", LDMS_V_CHAR_ARRAY, SPECULATION_SZ, 0 },
	[APP_STATUS_CPUS_ALLOWED] = { APP_STATUS_CPUS_ALLOWED, "status_cpus_allowed", "", LDMS_V_U32_ARRAY, CPUS_ALLOWED_SZ, 0 },
	[APP_STATUS_CPUS_ALLOWED_LIST] = { APP_STATUS_CPUS_ALLOWED_LIST, "status_cpus_allowed_list", "", LDMS_V_CHAR_ARRAY, CPUS_ALLOWED_LIST_SZ, 0 },
	[APP_STATUS_MEMS_ALLOWED] = { APP_STATUS_MEMS_ALLOWED, "status_mems_allowed", "", LDMS_V_U32_ARRAY, MEMS_ALLOWED_SZ, 0 },
	[APP_STATUS_MEMS_ALLOWED_LIST] = { APP_STATUS_MEMS_ALLOWED_LIST, "status_mems_allowed_list", "", LDMS_V_CHAR_ARRAY, MEMS_ALLOWED_LIST_SZ, 0 },
	[APP_STATUS_VOLUNTARY_CTXT_SWITCHES] = { APP_STATUS_VOLUNTARY_CTXT_SWITCHES, "status_voluntary_ctxt_switches", "", LDMS_V_U64, 0, 0 },
	[APP_STATUS_NONVOLUNTARY_CTXT_SWITCHES] = { APP_STATUS_NONVOLUNTARY_CTXT_SWITCHES, "status_nonvoluntary_ctxt_switches", "", LDMS_V_U64, 0, 0 },

	[APP_SYSCALL] = { APP_SYSCALL, "syscall", "", LDMS_V_U64_ARRAY, 9, 0 },
	[APP_SYSCALL_NAME] = { APP_SYSCALL_NAME, "syscall_name", "",
				LDMS_V_CHAR_ARRAY, SYSCALL_MAX, 0 },

	[APP_TIMERSLACK_NS] = { APP_TIMERSLACK_NS, "timerslack_ns", "ns", LDMS_V_U64, 0, 0 },

	[APP_WCHAN] = { APP_WCHAN, "wchan", "", LDMS_V_CHAR_ARRAY, WCHAN_SZ, 0 },
	[APP_TIMING] = { APP_TIMING, "sample_us", "", LDMS_V_U64, 0, 0 },

};

/* This array is initialized in __init__() below */
linux_proc_sampler_metric_info_t metric_info_idx_by_name[_APP_LAST + 1];

struct set_key {
	uint64_t start_tick;
	int64_t os_pid;
};

struct linux_proc_sampler_set {
	struct set_key key;
	ldms_set_t set;
	int64_t task_rank;
	struct rbn rbn; /* hook for app_set list */
	int dead;
	int fd_skip;
	char *fd_ident; /* json prefix for all file messages */
	size_t fd_ident_sz; /* json prefix for all file messages */
	struct rbt fn_rbt; /* tree for fd numbers of this process */
	LIST_ENTRY(linux_proc_sampler_set) del;
};
LIST_HEAD(set_del_list, linux_proc_sampler_set);

typedef struct linux_proc_sampler_inst_s *linux_proc_sampler_inst_t;
typedef int (*handler_fn_t)(linux_proc_sampler_inst_t inst, pid_t pid, ldms_set_t set);
struct handler_info {
	handler_fn_t fn;
	const char *fn_name;
};

struct linux_proc_sampler_inst_s {
	struct ldmsd_sampler samp;

	ldmsd_msg_log_f log;
	base_data_t base_data;
	char *instance_prefix;
	bool exe_suffix;
	int argv_fmt;
	bool argv_msg;
	bool env_msg;
	bool env_use_regex; /* match object is ready */
	regex_t env_regex; /* match object for env_exclude */
	int fd_msg; /* N for rescan fd every n-th sample */
	bool fd_use_regex; /* match object is ready */
	regex_t fd_regex; /* match object for fd_exclude */
	long sc_clk_tck;
	struct timeval sample_start;

	struct rbt set_rbt;
	pthread_mutex_t mutex;

	char *stream_name;
	char *env_stream;
	char *argv_stream;
	char *fd_stream;
	char *recycle_buf;
	size_t recycle_buf_sz;
	ldmsd_stream_client_t stream;
	int log_send;
	char *argv_sep;
	char *syscalls; /* file of 'int name' lines with # comments */
	int n_syscalls; /* maximum number of syscalls aliased/mapped */
	char **name_syscall; /* syscall names index by call's int id */

	struct handler_info fn[17];
	int n_fn;

	int task_rank_idx;
	int start_time_idx;
	int start_tick_idx;
	int exe_idx;
	int sc_clk_tck_idx;
	int is_thread_idx;
	int parent_pid_idx;
	int metric_idx[_APP_LAST+1]; /* 0 means disabled */
};

static void data_set_key(linux_proc_sampler_inst_t inst, struct linux_proc_sampler_set *as, uint64_t tick, int64_t os_pid)
{
	if (!as)
		return;
	as->key.os_pid = os_pid;
	as->key.start_tick = tick;
#ifdef LPDEBUG
	INST_LOG(inst, LDMSD_LDEBUG,"Creating key at %p: %" PRIu64 " , %" PRId64 "\n",
		as, tick, os_pid);
#endif

}

static int cmdline_handler(linux_proc_sampler_inst_t, pid_t, ldms_set_t);
static int n_open_files_handler(linux_proc_sampler_inst_t, pid_t, ldms_set_t);
static int io_handler(linux_proc_sampler_inst_t, pid_t, ldms_set_t);
static int oom_score_handler(linux_proc_sampler_inst_t, pid_t, ldms_set_t);
static int oom_score_adj_handler(linux_proc_sampler_inst_t, pid_t, ldms_set_t);
static int root_handler(linux_proc_sampler_inst_t, pid_t, ldms_set_t);
static int stat_handler(linux_proc_sampler_inst_t, pid_t, ldms_set_t);
static int status_handler(linux_proc_sampler_inst_t, pid_t, ldms_set_t);
static int syscall_handler(linux_proc_sampler_inst_t, pid_t, ldms_set_t);
static int timerslack_ns_handler(linux_proc_sampler_inst_t, pid_t, ldms_set_t);
static int wchan_handler(linux_proc_sampler_inst_t, pid_t, ldms_set_t);
static int timing_handler(linux_proc_sampler_inst_t, pid_t, ldms_set_t);

/* mapping metric -> handler */
struct handler_info handler_info_tbl[] = {
	[_APP_CMDLINE_FIRST ... _APP_CMDLINE_LAST] = { .fn = cmdline_handler, .fn_name = "cmdline_handler" },
	[APP_N_OPEN_FILES] = { .fn = n_open_files_handler, .fn_name = "n_open_files_handler" },
	[_APP_IO_FIRST ... _APP_IO_LAST] = { .fn = io_handler, .fn_name= "io_handler" },
	[APP_OOM_SCORE] = { .fn = oom_score_handler, .fn_name= "oom_score_handler" },
	[APP_OOM_SCORE_ADJ] = { .fn = oom_score_adj_handler, .fn_name= "oom_score_adj_handler" },
	[APP_ROOT] = { .fn = root_handler, .fn_name= "root_handler" },
	[_APP_STAT_FIRST ... _APP_STAT_LAST] = { .fn = stat_handler, .fn_name = "stat_handler"},
	[_APP_STATUS_FIRST ... _APP_STATUS_LAST] = { .fn = status_handler, .fn_name = "status_handler" },
	[APP_SYSCALL] = { .fn = syscall_handler, .fn_name = "syscall_handler" },
	[APP_SYSCALL_NAME] = { .fn = syscall_handler, .fn_name = "syscall_handler" },
	[APP_TIMERSLACK_NS] = { .fn = timerslack_ns_handler, .fn_name = "timerslack_ns_handler" },
	[APP_WCHAN] = { .fn = wchan_handler, .fn_name = "wchan_handler" },
	[APP_TIMING] = { .fn = timing_handler, .fn_name = "timing_handler"}
};

static inline linux_proc_sampler_metric_info_t find_metric_info_by_name(const char *name);

/* ============ Handlers ============ */

/*
 * Read content of the file (given `path`) into string metric at `idx` in `set`,
 * with maximum length `max_len`.
 *
 * **REMARK**: The metric is not '\0'-terminated.
 */
static int __read_str(ldms_set_t set, int idx, const char *path, int max_len)
{
	int fd, rlen;
	ldms_mval_t str = ldms_metric_get(set, idx);
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return errno;
	rlen = read(fd, str->a_char, max_len);
	close(fd);
	return rlen;
}

/* like `__read_str()`, but also '\0'-terminate the string */
static int __read_str0(ldms_set_t set, int idx, const char *path, int max_len)
{
	ldms_mval_t str = ldms_metric_get(set, idx);
	int rlen = __read_str(set, idx, path, max_len);
	if (rlen == max_len)
		rlen = max_len - 1;
	str->a_char[rlen] = '\0';
	return rlen;
}

/* Convenient functions that set `set[midx] = val` if midx > 0 */
static inline void __may_set_u64(ldms_set_t set, int midx, uint64_t val)
{
	if (midx > 0)
		ldms_metric_set_u64(set, midx, val);
}

static inline void __may_set_char(ldms_set_t set, int midx, char val)
{
	if (midx > 0)
		ldms_metric_set_char(set, midx, val);
}

static inline void __may_set_str(ldms_set_t set, int midx, const char *s)
{
	ldms_mval_t mval;
	int len;
	if (midx <= 0)
		return;
	mval = ldms_metric_get(set, midx);
	len = ldms_metric_array_get_len(set, midx);
	snprintf(mval->a_char, len, "%s", s);
}

/* reformat nul-delimited argv per sep given.
 * return new len, or -1 if sep is invalid.
 * bsiz maximum space available in b.
 * len space used in b and nul terminated.
 * sep: a character or character code
 */
static int quote_argv(linux_proc_sampler_inst_t inst, int len, char *b, int bsiz, const char *sep)
{
	int i = 0;
	if (!sep || !strlen(sep))
		return len; /* unmodified nul sep */
	if ( strlen(sep) == 1) {
		for ( ; i < (len-1); i++)
			if (b[i] == '\0')
				b[i] = sep[0];
		return len;
	}
	char csep = '\0';
	if (strlen(sep) == 2 && sep[0] == '\\') {
		switch (sep[1]) {
		case '0':
			return len;
		case 'b':
			csep  = ' ';
			break;
		case 't':
			csep  = '\t';
			break;
		case 'n':
			csep  = '\n';
			break;
		case 'v':
			csep  = '\v';
			break;
		case 'r':
			csep  = '\r';
			break;
		case 'f':
			csep  = '\f';
			break;
		default:
			return -1;
		}
	}
	for (i = 0; i < (len-1); i++)
		if (b[i] == '\0')
			b[i] = csep;
	return len;
}

static int check_sep(linux_proc_sampler_inst_t inst, const char *sep)
{
	INST_LOG(inst, LDMSD_LDEBUG,"check_sep: %s\n", sep);
	char testb[6] = "ab\0cd";
	if (quote_argv(inst, 6, testb, 6, sep) == -1) {
		return -1;
	}
	return 0;
}

static int cmdline_handler(linux_proc_sampler_inst_t inst, pid_t pid, ldms_set_t set)
{
	/* populate `cmdline` and maybe `cmdline_len` */
	ldms_mval_t cmdline;
	int len;
	char path[CMDLINE_SZ];
	cmdline = ldms_metric_get(set, inst->metric_idx[APP_CMDLINE]);
	if (cmdline->a_char[0])
		return 0; /* already set */
	snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
	len = __read_str(set, inst->metric_idx[APP_CMDLINE], path, CMDLINE_SZ);
	cmdline->a_char[CMDLINE_SZ - 1] = 0; /* in case len == CMDLINE_SZ */
	len = quote_argv(inst, len, cmdline->a_char, CMDLINE_SZ, inst->argv_sep);
	if (inst->metric_idx[APP_CMDLINE_LEN] > 0)
		ldms_metric_set_u64(set, inst->metric_idx[APP_CMDLINE_LEN], len);
	return 0;
}

static int n_open_files_handler(linux_proc_sampler_inst_t inst, pid_t pid,
				ldms_set_t set)
{
	/* populate n_open_files */
	char path[PROCPID_SZ];
	DIR *dir;
	struct dirent *dent;
	int n;
	snprintf(path, sizeof(path), "/proc/%d/fd", pid);
	dir = opendir(path);
	if (!dir)
		return errno;
	n = 0;
	while ((dent = readdir(dir))) {
		if (strcmp(dent->d_name, ".") == 0)
			continue; /* skip self */
		if (strcmp(dent->d_name, "..") == 0)
			continue; /* skip parent */
		n += 1;
	}
	ldms_metric_set_u64(set, inst->metric_idx[APP_N_OPEN_FILES], n);
	closedir(dir);
	return 0;
}

static int io_handler(linux_proc_sampler_inst_t inst, pid_t pid, ldms_set_t set)
{
	/* populate io_* */
	char path[PROCPID_SZ];
	uint64_t val[7];
	int n, rc;
	snprintf(path, sizeof(path), "/proc/%d/io", pid);
	FILE *f = fopen(path, "r");
	if (!f)
		return errno;
	n = fscanf(f,   "rchar: %lu\n"
			"wchar: %lu\n"
			"syscr: %lu\n"
			"syscw: %lu\n"
			"read_bytes: %lu\n"
			"write_bytes: %lu\n"
			"cancelled_write_bytes: %lu\n",
			val+0, val+1, val+2, val+3, val+4, val+5, val+6);
	if (n != 7) {
		rc = EINVAL;
		goto out;
	}
	__may_set_u64(set, inst->metric_idx[APP_IO_READ_B]	   , val[0]);
	__may_set_u64(set, inst->metric_idx[APP_IO_WRITE_B]	  , val[1]);
	__may_set_u64(set, inst->metric_idx[APP_IO_N_READ]	   , val[2]);
	__may_set_u64(set, inst->metric_idx[APP_IO_N_WRITE]	  , val[3]);
	__may_set_u64(set, inst->metric_idx[APP_IO_READ_DEV_B]       , val[4]);
	__may_set_u64(set, inst->metric_idx[APP_IO_WRITE_DEV_B]      , val[5]);
	__may_set_u64(set, inst->metric_idx[APP_IO_WRITE_CANCELLED_B], val[6]);
	rc = 0;
 out:
	fclose(f);
	return rc;
}

static int oom_score_handler(linux_proc_sampler_inst_t inst, pid_t pid, ldms_set_t set)
{
	/* according to `proc_oom_score()` in Linux kernel src tree, oom_score
	 * is `unsigned long` */
	char path[PROCPID_SZ];
	uint64_t x;
	int n;
	snprintf(path, sizeof(path), "/proc/%d/oom_score", pid);
	FILE *f = fopen(path, "r");
	if (!f)
		return errno;
	n = fscanf(f, "%lu", &x);
	fclose(f);
	if (n != 1)
		return EINVAL;
	ldms_metric_set_u64(set, inst->metric_idx[APP_OOM_SCORE], x);
	return 0;
}

static int oom_score_adj_handler(linux_proc_sampler_inst_t inst, pid_t pid, ldms_set_t set)
{
	/* according to `proc_oom_score_adj_read()` in Linux kernel src tree,
	 * oom_score_adj is `short` */
	char path[PROCPID_SZ];
	int x;
	int n;
	snprintf(path, sizeof(path), "/proc/%d/oom_score_adj", pid);
	FILE *f = fopen(path, "r");
	if (!f)
		return errno;
	n = fscanf(f, "%d", &x);
	fclose(f);
	if (n != 1)
		return EINVAL;
	ldms_metric_set_s64(set, inst->metric_idx[APP_OOM_SCORE_ADJ], x);
	return 0;
}

static int root_handler(linux_proc_sampler_inst_t inst, pid_t pid, ldms_set_t set)
{
	char path[PROCPID_SZ];
	ssize_t len;
	int midx = inst->metric_idx[APP_ROOT];
	assert(midx > 0);
	ldms_mval_t mval = ldms_metric_get(set, midx);
	int alen = ldms_metric_array_get_len(set, midx);
	/* /proc/<PID>/root is a soft link */
	snprintf(path, sizeof(path), "/proc/%d/root", pid);
	len = readlink(path, mval->a_char, alen - 1);
	if (len < 0) {
		mval->a_char[0] = '\0';
		return errno;
	}
	mval->a_char[len] = '\0';
	return 0;
}

static int stat_handler(linux_proc_sampler_inst_t inst, pid_t pid, ldms_set_t set)
{
	char buff[CMDLINE_SZ];
	char *str;
	char name[128]; /* should be enough to hold program name */
	int off, n;
	uint64_t val;
	char state;
	pid_t _pid;
	linux_proc_sampler_metric_e code;
	FILE *f;
	snprintf(buff, sizeof(buff), "/proc/%d/stat", pid);
	f = fopen(buff, "r");
	if (!f)
		return errno;
	char *s;
	errno = 0;
	s = fgets(buff, sizeof(buff), f);
	fclose(f);
	str = buff;
	if (s != buff) {
		if (errno) {
			INST_LOG(inst, LDMSD_LDEBUG,
				"error reading /proc/%d/stat %s\n", pid, STRERROR(errno));
			return errno;
		}
		return EINVAL;
	}
	n = sscanf(str, "%d (%[^)]) %c%n", &_pid, name, &state, &off);
	if (n != 3)
		return EINVAL;
	str += off;
	if (_pid != pid)
		return EINVAL; /* should not happen */
	__may_set_u64(set, inst->metric_idx[APP_STAT_PID], _pid);
	__may_set_str(set, inst->metric_idx[APP_STAT_COMM], name);
	__may_set_char(set, inst->metric_idx[APP_STAT_STATE], state);
	for (code = APP_STAT_PPID; code <= _APP_STAT_LAST; code++) {
		n = sscanf(str, "%lu%n", &val, &off);
		if (n != 1)
			return EINVAL;
		str += off;
		__may_set_u64(set, inst->metric_idx[code], val);
	}
	return 0;
}

typedef struct status_line_handler_s {
	const char *key;
	linux_proc_sampler_metric_e code;
	int (*fn)(linux_proc_sampler_inst_t inst, ldms_set_t set, const char *linebuf,
		  linux_proc_sampler_metric_e code);
} *status_line_handler_t;

static
int __line_dec(linux_proc_sampler_inst_t inst, ldms_set_t set, const char *linebuf, linux_proc_sampler_metric_e code);
static
int __line_dec_array(linux_proc_sampler_inst_t inst, ldms_set_t set, const char *linebuf, linux_proc_sampler_metric_e code);
static
int __line_hex(linux_proc_sampler_inst_t inst, ldms_set_t set, const char *linebuf, linux_proc_sampler_metric_e code);
static
int __line_oct(linux_proc_sampler_inst_t inst, ldms_set_t set, const char *linebuf, linux_proc_sampler_metric_e code);
static
int __line_char(linux_proc_sampler_inst_t inst, ldms_set_t set, const char *linebuf, linux_proc_sampler_metric_e code);
static
int __line_sigq(linux_proc_sampler_inst_t inst, ldms_set_t set, const char *linebuf, linux_proc_sampler_metric_e code);
static
int __line_str(linux_proc_sampler_inst_t inst, ldms_set_t set, const char *linebuf, linux_proc_sampler_metric_e code);
static
int __line_bitmap(linux_proc_sampler_inst_t inst, ldms_set_t set, const char *linebuf, linux_proc_sampler_metric_e code);

static status_line_handler_t find_status_line_handler(const char *key);

/* the following are not optional */
static char *metrics_always[] = {
	"job_id",
	"task_rank",
	"start_time",
	"start_tick",
	"is_thread",
	"parent",
	"exe"
};

struct metric_sub {
	char *alias;
	char *name;
};

static struct metric_sub alii[] = {
	{"parent_pid", "nothing"},
	{"ppid", "nothing"},
	{"os_pid", "stat_pid"},
	{"pid", "stat_pid"}
};

/* This table will later be sorted alphabetically in __init__() */
struct status_line_handler_s status_line_tbl[] = {
/* linux/fs/proc/array.c:task_state() */
	{ "Name", APP_STATUS_NAME, __line_str},
	{ "Umask", APP_STATUS_UMASK, __line_oct},
	{ "State", APP_STATUS_STATE,__line_char },
	{ "Tgid", APP_STATUS_TGID, __line_dec},
	{ "Ngid", APP_STATUS_NGID, __line_dec},
	{ "Pid", APP_STATUS_PID, __line_dec},
	{ "PPid", APP_STATUS_PPID, __line_dec},
	{ "TracerPid", APP_STATUS_TRACERPID, __line_dec},
	{ "Uid", APP_STATUS_UID, __line_dec_array},
	{ "Gid", APP_STATUS_GID, __line_dec_array},
	{ "FDSize", APP_STATUS_FDSIZE, __line_dec},
	{ "Groups", APP_STATUS_GROUPS, __line_dec_array},
	{ "NStgid", APP_STATUS_NSTGID, __line_dec_array},
	{ "NSpid", APP_STATUS_NSPID, __line_dec_array},
	{ "NSpgid", APP_STATUS_NSPGID, __line_dec_array},
	{ "NSsid", APP_STATUS_NSSID, __line_dec_array},

	/* linux/fs/proc/task_mmu.c:task_mem() */
	{ "VmPeak", APP_STATUS_VMPEAK, __line_dec},
	{ "VmSize", APP_STATUS_VMSIZE, __line_dec},
	{ "VmLck", APP_STATUS_VMLCK, __line_dec},
	{ "VmPin", APP_STATUS_VMPIN, __line_dec},
	{ "VmHWM", APP_STATUS_VMHWM, __line_dec},
	{ "VmRSS", APP_STATUS_VMRSS, __line_dec},
	{ "RssAnon", APP_STATUS_RSSANON, __line_dec},
	{ "RssFile", APP_STATUS_RSSFILE, __line_dec},
	{ "RssShmem", APP_STATUS_RSSSHMEM, __line_dec},
	{ "VmData", APP_STATUS_VMDATA, __line_dec},
	{ "VmStk", APP_STATUS_VMSTK, __line_dec},
	{ "VmExe", APP_STATUS_VMEXE, __line_dec},
	{ "VmLib", APP_STATUS_VMLIB, __line_dec},
	{ "VmPTE", APP_STATUS_VMPTE, __line_dec},
	{ "VmPMD", APP_STATUS_VMPMD, __line_dec},
	{ "VmSwap", APP_STATUS_VMSWAP, __line_dec},
	{ "HugetlbPages", APP_STATUS_HUGETLBPAGES, __line_dec},

	/* linux/fs/proc/array.c:task_core_dumping() */
	{ "CoreDumping", APP_STATUS_COREDUMPING, __line_dec},

	/* linux/fs/proc/array.c:task_sig() */
	{ "Threads", APP_STATUS_THREADS, __line_dec},
	{ "SigQ", APP_STATUS_SIG_QUEUED, __line_sigq},
	{ "SigPnd", APP_STATUS_SIGPND, __line_hex},
	{ "ShdPnd", APP_STATUS_SHDPND, __line_hex},
	{ "SigBlk", APP_STATUS_SIGBLK, __line_hex},
	{ "SigIgn", APP_STATUS_SIGIGN, __line_hex},
	{ "SigCgt", APP_STATUS_SIGCGT, __line_hex},

	/* linux/fs/proc/array.c:task_cap() */
	{ "CapInh", APP_STATUS_CAPINH, __line_hex},
	{ "CapPrm", APP_STATUS_CAPPRM, __line_hex},
	{ "CapEff", APP_STATUS_CAPEFF, __line_hex},
	{ "CapBnd", APP_STATUS_CAPBND, __line_hex},
	{ "CapAmb", APP_STATUS_CAPAMB, __line_hex},

	/* linux/fs/proc/array.c:task_seccomp() */
	{ "NoNewPrivs", APP_STATUS_NONEWPRIVS, __line_dec},
	{ "Seccomp", APP_STATUS_SECCOMP, __line_dec},
	{ "Speculation_Store_Bypass", APP_STATUS_SPECULATION_STORE_BYPASS, __line_str},

	/* linux/fs/proc/array.c:task_cpus_allowed() */
	{ "Cpus_allowed", APP_STATUS_CPUS_ALLOWED, __line_bitmap},
	{ "Cpus_allowed_list", APP_STATUS_CPUS_ALLOWED_LIST, __line_str},

	/* linux/kernel/cgroup/cpuset.c:cpuset_task_status_allowed() */
	{ "Mems_allowed", APP_STATUS_MEMS_ALLOWED, __line_bitmap},
	{ "Mems_allowed_list", APP_STATUS_MEMS_ALLOWED_LIST, __line_str},

	/* linux/fs/proc/array.c:task_context_switch_counts() */
	{ "voluntary_ctxt_switches", APP_STATUS_VOLUNTARY_CTXT_SWITCHES, __line_dec},
	{ "nonvoluntary_ctxt_switches", APP_STATUS_NONVOLUNTARY_CTXT_SWITCHES, __line_dec},
};

static int status_handler(linux_proc_sampler_inst_t inst, pid_t pid, ldms_set_t set)
{
	char buff[CMDLINE_SZ];
	FILE *f;
	char *line, *key, *ptr;
	status_line_handler_t sh;

	snprintf(buff, sizeof(buff), "/proc/%d/status", pid);
	f = fopen(buff, "r");
	if (!f)
		return errno;
	while ((line = fgets(buff, sizeof(buff), f))) {
		key = strtok_r(line, ":", &ptr);
		sh = find_status_line_handler(key);
		if (!sh)
			continue;
		while (isspace(*ptr)) {
			ptr++;
		}
		ptr[strlen(ptr)-1] = '\0'; /* eliminate trailing space */
		if (inst->metric_idx[sh->code] > 0
				|| sh->code == APP_STATUS_SIG_QUEUED) {
			sh->fn(inst, set, ptr, sh->code);
		}
	}
	fclose(f);
	return 0;
}

static
int __line_dec(linux_proc_sampler_inst_t inst, ldms_set_t set,
		const char *linebuf, linux_proc_sampler_metric_e code)
{
	/* scan for a uint64_t */
	int n;
	uint64_t x;
	n = sscanf(linebuf, "%ld", &x);
	if (n != 1)
		return EINVAL;
	ldms_metric_set_u64(set, inst->metric_idx[code], x);
	return 0;
}

static
int __line_dec_array(linux_proc_sampler_inst_t inst, ldms_set_t set,
		const char *linebuf, linux_proc_sampler_metric_e code)
{
	int i, n, alen, pos;
	int midx = inst->metric_idx[code];
	const char *ptr = linebuf;
	uint64_t val;
	alen = ldms_metric_array_get_len(set, midx);
	for (i = 0; i < alen; i++) {
		n = sscanf(ptr, "%lu%n", &val, &pos);
		if (n != 1)
			break;
		ldms_metric_array_set_u64(set, midx, i, val);
		ptr += pos;
	}
	return 0;
}

static
int __line_hex(linux_proc_sampler_inst_t inst, ldms_set_t set,
		const char *linebuf, linux_proc_sampler_metric_e code)
{
	int n;
	uint64_t x;
	n = sscanf(linebuf, "%lx", &x);
	if (n != 1)
		return EINVAL;
	ldms_metric_set_u64(set, inst->metric_idx[code], x);
	return 0;
}

static
int __line_oct(linux_proc_sampler_inst_t inst, ldms_set_t set,
		const char *linebuf, linux_proc_sampler_metric_e code)
{
	int n;
	uint64_t x;
	n = sscanf(linebuf, "%lo", &x);
	if (n != 1)
		return EINVAL;
	ldms_metric_set_u64(set, inst->metric_idx[code], x);
	return 0;
}

static
int __line_char(linux_proc_sampler_inst_t inst, ldms_set_t set,
		const char *linebuf, linux_proc_sampler_metric_e code)
{
	int n;
	char x;
	n = sscanf(linebuf, "%c", &x);
	if (n != 1)
		return EINVAL;
	ldms_metric_set_char(set, inst->metric_idx[code], x);
	return 0;
}

static
int __line_sigq(linux_proc_sampler_inst_t inst, ldms_set_t set,
		const char *linebuf, linux_proc_sampler_metric_e code)
{
	uint64_t q, l;
	int n;
	n = sscanf(linebuf, "%lu/%lu", &q, &l);
	if (n != 2)
		return EINVAL;
	__may_set_u64(set, inst->metric_idx[APP_STATUS_SIG_QUEUED], q);
	__may_set_u64(set, inst->metric_idx[APP_STATUS_SIG_LIMIT] , l);
	return 0;
}

static
int __line_str(linux_proc_sampler_inst_t inst, ldms_set_t set, const char *linebuf,
		linux_proc_sampler_metric_e code)
{
	int midx = inst->metric_idx[code];
	int alen = ldms_metric_array_get_len(set, midx);
	ldms_mval_t mval = ldms_metric_get(set, midx);
	snprintf(mval->a_char, alen, "%s", linebuf);
	ldms_metric_array_set_char(set, midx, alen-1, '\0'); /* to update data generation number */
	return 0;
}

static
int __line_bitmap(linux_proc_sampler_inst_t inst, ldms_set_t set, const char *linebuf,
		  linux_proc_sampler_metric_e code)
{
	/* line format: <4-byte-hex>,<4-byte-hex>,...<4-byte-hex>
	 * line ordering: high-byte ... low-byte
	 * ldms array ordering: low-byte .. high-byte
	 * high-byte will be truncated if the ldms array is too short.
	 */
	const char *s;
	int n = strlen(linebuf);
	int midx = inst->metric_idx[code];
	int alen = ldms_metric_array_get_len(set, midx);
	int i, val;
	for (i = 0; i < alen && n; i++) {
		/* reverse scan */
		s = memrchr(linebuf, ',', n);
		if (!s) {
			s = linebuf;
			sscanf(s, "%x", &val);
		} else {
			sscanf(s, ",%x", &val);
		}
		ldms_metric_array_set_u32(set, midx, i, val);
		n = s - linebuf;
	}
	return 0;
}

static char *get_syscall_name(linux_proc_sampler_inst_t inst, int call)
{
	if (call >=0 && call <= inst->n_syscalls)
		return inst->name_syscall[call];
	if (call == -1)
		return "blocked";
	return NULL;
}

static int load_syscall_names(linux_proc_sampler_inst_t inst)
{
	char buf[ROOT_SZ];
	char buf2[ROOT_SZ];
	if (!inst)
		return EINVAL;
	if (!inst->syscalls)
		return 0;
	int rc = 0;
	INST_LOG(inst, LDMSD_LDEBUG, "linux_proc_sampler: reading %s\n",
		inst->syscalls);
	FILE *f = fopen(inst->syscalls, "r");
	if (!f) {
		rc = errno;
		INST_LOG(inst, LDMSD_LERROR,
			"linux_proc_sampler: unable to load %s. %s\n",
			inst->syscalls, STRERROR(rc));
		return rc;
	}
	char *ret;
	int call = -1;
	/* scan for max call number */
	int line = 0;
	while ( (ret = fgets(buf, sizeof(buf), f))) {
		line++;
		if (buf[0] == '#' || buf[0] == '\n' || buf[0] == '\0')
			continue;
		int items = sscanf(buf, "%d %s", &call, buf2);
		if (items != 2) {
			INST_LOG(inst, LDMSD_LERROR,
				"linux_proc_sampler: %s. Error at line %d: %s\n",
				inst->syscalls, line, buf);
			rc = EINVAL;
			goto err;
		}
		if (call > inst->n_syscalls)
			inst->n_syscalls = call;
		if (strlen(buf2) >= SYSCALL_MAX) {
			INST_LOG(inst, LDMSD_LERROR,
				"linux_proc_sampler: %s. name too long at line %d: %s\n",
				inst->syscalls, line, buf2);
			rc = EINVAL;
			goto err;
		}
	}
	/* load the data */
	if (inst->n_syscalls < 0) {
		rc = 0;
		goto err;
	}
	inst->name_syscall = calloc( inst->n_syscalls + 1, sizeof(char *));
	if (!inst->syscalls) {
		INST_LOG(inst, LDMSD_LERROR,
			"linux_proc_sampler: parsing %s. out of memory.\n",
			inst->syscalls);
		rc = ENOMEM;
		goto err;
	}
	rewind(f);
	while ( (ret = fgets(buf, sizeof(buf), f)) ) {
		if (buf[0] == '#' || buf[0] == '\n' || buf[0] == '\0')
			continue;
		int items = sscanf(buf, "%d %s", &call, buf2);
		if (items == 2) {
			if (call < 0 || call > inst->n_syscalls) {
				INST_LOG(inst, LDMSD_LERROR, "linux_proc_sampler"
					" read call %d out of range.'%s'\n", call,
					buf);
				continue;
			}
			char *s = malloc(SYSCALL_MAX);
			if (!s) {
				INST_LOG(inst, LDMSD_LERROR,
					"linux_proc_sampler: parsing %s."
					" Out of memory.\n",
					inst->syscalls);
				rc = ENOMEM;
				goto err;
			}
			inst->name_syscall[call] = s;
			strncpy(s, buf2, SYSCALL_MAX);
		}
	}
	fclose(f);
	f = NULL;
	/* fill holes in the data */
	for (call = 0; call <= inst->n_syscalls; call++) {
		if (inst->name_syscall[call])
			continue;
		inst->name_syscall[call] = malloc(SYSCALL_MAX);
		if (!inst->name_syscall[call]) {
			INST_LOG(inst, LDMSD_LERROR,
				"linux_proc_sampler: parsing %s."
				" Out of memory.\n",
				inst->syscalls);
			rc = ENOMEM;
			goto err;
		}
		sprintf(inst->name_syscall[call], "SYS_%d", call);
	}

	INST_LOG(inst, LDMSD_LDEBUG, "linux_proc_sampler: read %d\n",
		inst->n_syscalls);
	return 0;
err:
	if (f)
		fclose(f);
	if (inst->syscalls)
		for (call = 0; call <= inst->n_syscalls; call++)
			free(inst->name_syscall[call]);
	free(inst->name_syscall);
	inst->name_syscall = NULL;
	inst->n_syscalls = -1;
	return rc;
}

static int syscall_handler(linux_proc_sampler_inst_t inst, pid_t pid, ldms_set_t set)
{
	char buff[CMDLINE_SZ];
	FILE *f;
	char *ret;
	int i, n;
	uint64_t val[9] = {0};
	snprintf(buff, sizeof(buff), "/proc/%d/syscall", pid);
	/*
	 * NOTE: The file contains single line wcich could be:
	 * - "running": the process is running.
	 * - "-1 <STACK_PTR> <PROGRAM_CTR>": the process is blocked, but not in
	 *   a syscall.
	 * - "<SYSCALL_NUM> <ARG0> ... <ARG5> <STACK_PTR> <PROGRAM_CTR>": the
	 *   syscall number, 6 arguments, stack pointer and program counter.
	 */
	f = fopen(buff, "r");
	if (!f)
		return errno;
	ret = fgets(buff, sizeof(buff), f);
	fclose(f);
	int call = -1;
	if (!ret)
		return errno;
	if (0 == strncmp(buff, "running", 7)) {
		n = 0;
	} else {
		n = sscanf(buff, "%d %lx %lx %lx %lx %lx %lx %lx %lx",
				 &call, val+1, val+2, val+3, val+4,
				 val+5, val+6, val+7, val+8);
		if (n)
			val[0] = (uint64_t)call;
	}
	if (inst->metric_idx[APP_SYSCALL] > 0) {
		for (i = 0; i < n; i++) {
			ldms_metric_array_set_u64(set,
						inst->metric_idx[APP_SYSCALL],
						i, val[i]);
		}
		for (i = n; i < 9; i++) {
			/* zero */
			ldms_metric_array_set_u64(set,
						inst->metric_idx[APP_SYSCALL],
						i, 0);
		}
	}
	if (inst->metric_idx[APP_SYSCALL_NAME] > 0) {
		char *name0[SYSCALL_MAX];
		char *name = (char*)name0;
		if (n > 0) {
			if (inst->n_syscalls != -1) {
				name = get_syscall_name(inst, call);
				if (!name) {
					sprintf(name, "SYS_%d", call);
				}
			} else {
				if (call == -1) {
					name = "blocked";
				} else {
					sprintf(name, "SYS_%d", call);
				}
			}
		} else {
			name = "running";
		}
		ldms_metric_array_set_str(set, inst->metric_idx[APP_SYSCALL_NAME], name);
	}
	return 0;
}

static int timerslack_ns_handler(linux_proc_sampler_inst_t inst, pid_t pid, ldms_set_t set)
{
	char path[PROCPID_SZ];
	uint64_t x;
	int n;
	snprintf(path, sizeof(path), "/proc/%d/timerslack_ns", pid);
	FILE *f = fopen(path, "r");
	if (!f && errno != ENOENT)
		return errno;
	if (!f) {
		ldms_metric_set_u64(set, inst->metric_idx[APP_TIMERSLACK_NS], 0);
		return 0;
	}
	n = fscanf(f, "%lu", &x);
	fclose(f);
	if (n != 1)
		return EINVAL;
	ldms_metric_set_u64(set, inst->metric_idx[APP_TIMERSLACK_NS], x);
	return 0;
}

static int wchan_handler(linux_proc_sampler_inst_t inst, pid_t pid, ldms_set_t set)
{
	char path[PROCPID_SZ];
	snprintf(path, sizeof(path), "/proc/%d/wchan", pid);
	__read_str0(set, inst->metric_idx[APP_WCHAN], path, WCHAN_SZ);
	return 0;
}


static int timing_handler(linux_proc_sampler_inst_t inst, pid_t pid, ldms_set_t set)
{
	struct timeval t2;
	gettimeofday(&t2, NULL);
	uint64_t x_us = (t2.tv_sec - inst->sample_start.tv_sec)*1000000;
	int64_t d_us = (int64_t)t2.tv_usec - (int64_t)inst->sample_start.tv_usec;
	if (d_us < 0)
		x_us += (uint64_t)(1000000 + d_us);
	else
		x_us += (uint64_t) d_us;

	ldms_metric_set_u64(set, inst->metric_idx[APP_TIMING], x_us);
	inst->sample_start.tv_sec = 0;
	inst->sample_start.tv_usec = 0;
#ifdef LPDEBUG
	INST_LOG(inst, LDMSD_LDEBUG, "In %" PRIu64 " microseconds\n", x_us);
#endif
	return 0;
}


/* ============== Sampler Plugin APIs ================= */

static int
linux_proc_sampler_update_schema(linux_proc_sampler_inst_t inst, ldms_schema_t schema)
{
	int i, idx;
	linux_proc_sampler_metric_info_t mi;

	inst->task_rank_idx = ldms_schema_meta_add(schema, "task_rank",
						   LDMS_V_U64);
	inst->start_time_idx = ldms_schema_meta_array_add(schema, "start_time",
						LDMS_V_CHAR_ARRAY, 20);
	inst->start_tick_idx = ldms_schema_meta_add(schema, "start_tick",
						LDMS_V_U64);
	inst->is_thread_idx = ldms_schema_meta_add(schema, "is_thread",
						LDMS_V_U8);
	inst->parent_pid_idx = ldms_schema_meta_add(schema, "parent",
						LDMS_V_S64);
	inst->exe_idx = ldms_schema_meta_array_add(schema, "exe",
						LDMS_V_CHAR_ARRAY, 512);
	if (inst->sc_clk_tck)
		inst->sc_clk_tck_idx = ldms_schema_meta_add(schema, "sc_clk_tck",
						LDMS_V_S64);

	/* Add app metrics to the schema */
	for (i = 1; i <= _APP_LAST; i++) {
		if (!inst->metric_idx[i])
			continue;
		mi = &metric_info[i];
		INST_LOG(inst, LDMSD_LDEBUG, "Add metric %s\n", mi->name);
		if (ldms_type_is_array(mi->mtype)) {
			if (mi->is_meta) {
				idx = ldms_schema_meta_array_add(schema,
						mi->name, mi->mtype,
						mi->array_len);
			} else {
				idx = ldms_schema_metric_array_add(schema,
						mi->name, mi->mtype,
						mi->array_len);
			}
		} else {
			if (mi->is_meta) {
				idx = ldms_schema_meta_add(schema, mi->name,
						mi->mtype);
			} else {
				idx = ldms_schema_metric_add(schema, mi->name,
						mi->mtype);
			}
		}
		if (idx < 0) {
			/* error */
			INST_LOG(inst, LDMSD_LERROR,
				 "Error %d adding metric %s into schema.\n",
				 -idx, mi->name);
			return -idx;
		}
		inst->metric_idx[i] = idx;
	}
	return 0;
}

void fn_rbt_destroy(linux_proc_sampler_inst_t inst, struct rbt *t);

void app_set_destroy(linux_proc_sampler_inst_t inst, struct linux_proc_sampler_set *a)
{
#ifdef LPDEBUG
	INST_LOG(inst, LDMSD_LDEBUG, "Removing set %s\n",
		ldms_set_instance_name_get(a->set));
	INST_LOG(inst, LDMSD_LDEBUG,"Uncreating key at %p: %" PRIu64 " , %" PRId64 "\n",
		&a->key, a->key.start_tick, a->key.os_pid);
#else
	(void)inst;
#endif
	if (!a)
		return;
	a->dead = 0xDEADBEEF;
	if (a->set) {
		ldmsd_set_deregister(ldms_set_instance_name_get(a->set), SAMP);
		ldms_set_unpublish(a->set);
		ldms_set_delete(a->set);
		a->set = NULL;
	}
	a->key.start_tick = 0;
	a->key.os_pid = 0;
	free(a->fd_ident);
	fn_rbt_destroy(inst, &a->fn_rbt);
	free(a);
}

static int publish_fd_pid(linux_proc_sampler_inst_t inst, struct linux_proc_sampler_set *app_set);

static int linux_proc_sampler_sample(struct ldmsd_sampler *pi)
{
	linux_proc_sampler_inst_t inst = (void*)pi;
	int i, rc;
	struct rbn *rbn;
#ifdef LPDEBUG
	INST_LOG(inst, LDMSD_LDEBUG, "Sampling\n");
#endif
	struct linux_proc_sampler_set *app_set;
	struct set_del_list del_list;
	LIST_INIT(&del_list);
	pthread_mutex_lock(&inst->mutex);
	RBT_FOREACH(rbn, &inst->set_rbt) {
		app_set = container_of(rbn, struct linux_proc_sampler_set, rbn);
		if (app_set->dead ) {
			LIST_INSERT_HEAD(&del_list, app_set, del);
			continue;
		}
		ldms_transaction_begin(app_set->set);
		gettimeofday(&inst->sample_start, NULL);
		for (i = 0; i < inst->n_fn; i++) {
			rc = inst->fn[i].fn(inst, app_set->key.os_pid, app_set->set);
			if (rc) {
#ifdef LPDEBUG
				INST_LOG(inst, LDMSD_LDEBUG,
					"Removing set %s. Error %d(%s) from %s\n",
					ldms_set_instance_name_get(app_set->set),
					rc, STRERROR(rc), inst->fn[i].fn_name);
#endif
				LIST_INSERT_HEAD(&del_list, app_set, del);
				app_set->dead = rc;
				break;
			}
		}
		app_set->fd_skip++;
		if (!app_set->dead && inst->fd_msg &&
			(app_set->fd_skip % inst->fd_msg == 0)) {
			rc = publish_fd_pid(inst, app_set);
			if (rc) {
				INST_LOG(inst, LDMSD_LDEBUG, "Removing set %s."
					" Error %d(%s) from publish_fd_pid\n",
					ldms_set_instance_name_get(app_set->set),
					rc, STRERROR(rc));
				LIST_INSERT_HEAD(&del_list, app_set, del);
				app_set->dead = rc;
				break;
			}
		}
#ifdef LPDEBUG
		INST_LOG(inst, LDMSD_LDEBUG, "Got data for %s\n",
			ldms_set_instance_name_get(app_set->set));
#endif
		ldms_transaction_end(app_set->set);
	}
	while (!LIST_EMPTY(&del_list)) {
                app_set = LIST_FIRST(&del_list);
		rbn = rbt_find(&inst->set_rbt, &app_set->key);
		if (rbn)
			rbt_del(&inst->set_rbt, rbn);
                LIST_REMOVE(app_set, del);
                app_set_destroy(inst, app_set);
        }
	pthread_mutex_unlock(&inst->mutex);
	return 0;
}


/* ============== Common Plugin APIs ================= */

static
char *_help = "\
linux_proc_sampler config synopsis: \n\
    config name=linux_proc_sampler [COMMON_OPTIONS] [stream=STREAM]\n\
	    [sc_clk_tck=1] [metrics=METRICS] [cfg_file=FILE] [exe_suffix=1]\n\
            [env_msg=1] [argv_msg=1] [argv_fmt=<1,2>] [env_exclude=EFILE]\n\
            [fd_msg=N] [fd_exclude=EFILE]\n\
\n\
Option descriptions:\n\
    instance_prefix    The prefix for generated instance names. Typically a cluster name\n\
		when needed to disambiguate producer names that appear in multiple clusters.\n\
	      (default: no prefix).\n\
    exe_suffix  Append executable path to set instance names.\n\
    stream    The name of the `ldmsd_stream` to listen for SLURM job events.\n\
	      (default: slurm).\n\
    sc_clk_tck=1 Include sc_clk_tck, the ticks per second, in the set.\n\
              The default is to exclude sc_clk_tck.\n\
    metrics   The comma-separated list of metrics to monitor.\n\
	      The default is \"\" (empty), which is equivalent to monitor ALL\n\
	      metrics.\n\
    argv_sep  The separator character to replace nul with in the cmdline string.\n\
              Special specifiers \n,\t,\b etc are also supported.\n\
    env_msg   Enable streaming the environment of each process out.\n\
    env_exclude Name of a file with 1 regular expression per line.\n\
    argv_msg  Enable streaming the argv of each process out.\n\
    argv_fmt  Select format of argv elements.\n\
    fd_msg=N  Enable /proc/$pid/fd detail reporting every N-th sample\n\
    fd_exclude Name of a file with 1 regular expression per line.\n\
    cfg_file  The alternative config file in JSON format. The file is\n\
	      expected to have an object that contains the following \n\
	      attributes:\n\
	      - \"stream\": \"STREAM_NAME\"\n\
	      - \"metrics\": [ METRICS ]\n\
	      If the `cfg_file` is given, most other options are ignored.\n\
	      Other options listed here may also be given in the cfg_file.\n\
\n\
The sampler creates and destroys sets according to events received from \n\
LDMSD stream. The sets share the same schema which is contructed according \n\
to the given `metrics` option or \"metrics\" in the JSON `cfg_file`.\n\
The sampling frequency is controlled by LDMSD `smplr` object.\n\
\n\
The following is an example of cfg_file:\n\
```\n\
{\n\
  \"stream\": \"slurm\",\n\
  \"instance_prefix\": \"cluster2\",\n\
  \"metrics\": [\n\
    \"stat_comm\",\n\
    \"stat_pid\",\n\
    \"stat_cutime\"\n\
  ]\n\
}\n\
```\n\
\n\
The following metadata metrics are always reported:\n\
name\ttype\tnotes\n\
task_rank u64 The PID or if slurm present the SLURM_TASK_RANK\n\
              (inherited by child processes). \n\
start_time char[20] The epoch time when the process started, as\n\
                    estimated from the start_tick.\n\
start_tick u64 The node local start time in jiffies from /proc/$pid/stat.\n\
is_thread u8 Boolean value noting if the process is a child thread in\n\
             e.g. an OMP application.\n\
parent s64 The parent pid of the process.\n\
exe char[] The full path of the executable.\n\
\n\
The config file, but not the command line, allow the option\n\
  \"log_send\":1\n\
which adds logging of stream message events (less the json).\n\
";

static char *help_all;
static void compute_help() {
	linux_proc_sampler_metric_info_t mi;
	char name_buf[80];
	int i;
	dsinit2(ds, CMDLINE_SZ);
	dscat(ds, _help);
	dscat(ds, "\nThe list of optional metric names and types is:\n");
	for (i = 1; i <= _APP_LAST; i++) {
		mi = &metric_info[i];
		snprintf(name_buf, sizeof name_buf, "\n%10s%s\t%s",
			ldms_metric_type_to_str(mi->mtype),
			(mi->is_meta ? ", meta-data," : ",      data,"),
			mi->name);
		dscat(ds, name_buf);
	}
	dscat(ds, "\nThe following metrics are not optional:\n");
	for (i = 0; i < ARRAY_LEN(metrics_always); i++) {
		dscat(ds, "\t");
		dscat(ds, metrics_always[i]);
		dscat(ds, "\n");
	}
	help_all = dsdone(ds);
	if (!help_all)
		help_all = _help;
}

static const char *linux_proc_sampler_usage(struct ldmsd_plugin *self)
{
	if (!help_all)
		compute_help();
	return help_all;
}

static void missing_metric(linux_proc_sampler_inst_t inst, const char *tkn)
{
	size_t k, ma = ARRAY_LEN(metrics_always);
	for (k = 0; k < ma; k++) {
		if (strcmp(metrics_always[k], tkn) == 0) {
			INST_LOG(inst, LDMSD_LERROR,
			"metric '%s' is not optional. Remove it.\n", tkn);
			return;
		}
	}
	ma = ARRAY_LEN(alii);
	for (k = 0; k < ma; k++) {
		if (strcmp(alii[k].alias, tkn) == 0) {
			INST_LOG(inst, LDMSD_LERROR,
			"metric '%s' is not supported. Replace with %s.\n",
				tkn, alii[k].name);
			return;
		}
	}
	INST_LOG(inst, LDMSD_LERROR,
		 "optional metric '%s' is unknown.\n", tkn);
}

/*  BEGIN of env collection filtering support functions. */

struct env_match_expr {
	int notfirst;
	dstring_t dp;
};

/* strip trailing space/cr/lf from line (if from file but not json)
 * and add to regex string.
 */
static
int add_match(linux_proc_sampler_inst_t inst, struct env_match_expr *eme, char *line)
{
	if (eme->notfirst) {
		if (!dscat(eme->dp, "|"))
			goto err;
	} else {
		eme->notfirst = 1;
	}
	if (!dscat(eme->dp, "("))
		goto err;
	size_t len = strlen(line);
	while ( len > 0 && isspace(line[len-1])) {
		line[len-1] = '\0';
		len--;
	}
	if (!dscat(eme->dp, line))
		goto err;
	if (!dscat(eme->dp, ")"))
		goto err;
	return 0;
err:
	return ENOMEM;
}

/* parse file and assemble regex str. all # as first char comment lines */
static
char *file_to_regex(linux_proc_sampler_inst_t inst, const char *fname)
{
	FILE *f;
	struct env_match_expr eme;
	eme.notfirst = 0;
	dstr_init(&eme.dp);
	f = fopen(fname, "r");
	int rc;
	if (!f) {
		INST_LOG(inst, LDMSD_LERROR, "cannot open env_exclude %s\n",
			fname);
		goto err;
	}
	char *line;
	char buf[CMDLINE_SZ];
	int ln = 0;
	while ((line = fgets(buf, sizeof(buf), f))) {
		ln++;
		if (buf[0] == '#' || buf[0] == '\n' || buf[0] == '\0')
                        continue;
		rc = add_match(inst, &eme, line);
		if (rc) {
			INST_LOG(inst, LDMSD_LERROR,
				"out of memory or bad env_exclude line %d: %s\n",
				ln, line);
			goto err;
		}
	}
	return dsdone(eme.dp);
err:
	dstr_free(&eme.dp);
	return NULL;
}

/* parse env_exclude entity for filename or list and assemble regex str */
static
char *json_to_regex(linux_proc_sampler_inst_t inst, json_entity_t env_exclude)
{
	if (env_exclude->type == JSON_STRING_VALUE) {
		return file_to_regex(inst, json_value_cstr(env_exclude));
	}
	if (env_exclude->type != JSON_LIST_VALUE) {
		INST_LOG(inst, LDMSD_LERROR,
			"env_exclude value is not a list or filename\n");
		return NULL;
	}
	struct env_match_expr eme;
	eme.notfirst = 0;
	dstr_init(&eme.dp);
	json_entity_t c;
	int rc;
	for (c = json_item_first(env_exclude); c; c = json_item_next(c)) {
		if (c->type != JSON_STRING_VALUE) {
			INST_LOG(inst, LDMSD_LERROR,
				"env_exclude list element is not a string.\n");
			goto err;
		}
		rc = add_match(inst, &eme, (char *)json_value_cstr(c));
		if (rc) {
			INST_LOG(inst, LDMSD_LERROR,
				"out of memory making env_exclude regex\n");
			goto err;
		}
	}
	return dsdone(eme.dp);
err:
	dstr_free(&eme.dp);
	return NULL;
}

static int init_regex_filter(linux_proc_sampler_inst_t inst, char *regex_str, char *filt, bool *use, regex_t *r)
{
	const size_t errbuf_len = 1024;
	char errbuf[errbuf_len];
	int rc = ldmsd_compile_regex(r, regex_str, errbuf, errbuf_len);
	if (rc) {
		INST_LOG(inst, LDMSD_LERROR, "unable to compile %s filter %s\n",
			filt, regex_str);
		INST_LOG(inst, LDMSD_LERROR, "regex message %s.\n", errbuf);
	} else {
		INST_LOG(inst, LDMSD_LDEBUG, "regex %s excluding %s\n",
			filt, regex_str);
		*use = 1;
	}
	return rc;
}

/*  END of env collection filtering support functions. */

static
int __handle_cfg_file(linux_proc_sampler_inst_t inst, char *val)
{
	int i, fd, rc = -1, off;
	ssize_t bsz, sz;
	char *buff = NULL;
	json_parser_t jp = NULL;
	json_entity_t jdoc = NULL;
	json_entity_t list, ent;
	linux_proc_sampler_metric_info_t minfo;

	fd = open(val, O_RDONLY);
	if (fd < 0) {
		INST_LOG(inst, LDMSD_LERROR, "Cannot open %s\n", val);
		return errno;
	}
	sz = lseek(fd, 0, SEEK_END);
	if (sz < 0) {
		rc = errno;
		INST_LOG(inst, LDMSD_LERROR,
			 "lseek() failed, errno: %d\n", errno);
		goto out;
	}
	bsz = sz;
	lseek(fd, 0, SEEK_SET);
	buff = malloc(sz+1);
	if (!buff) {
		rc = errno;
		INST_LOG(inst, LDMSD_LERROR, "Out of memory parsing %s\n", val);
		goto out;
	}
	off = 0;
	while (sz) {
		rc = read(fd, buff + off, sz);
		if (rc < 0) {
			rc = errno;
			INST_LOG(inst, LDMSD_LERROR, "read() error: %d in %s\n",
				errno, val);
			goto out;
		}
		off += rc;
		sz -= rc;
	}

	jp = json_parser_new(0);
	if (!jp) {
		rc = errno;
		INST_LOG(inst, LDMSD_LERROR, "json_parser_new() error: %d\n",
			errno);
		goto out;
	}
	rc = json_parse_buffer(jp, buff, bsz, &jdoc);
	if (rc) {
		INST_LOG(inst, LDMSD_LERROR, "JSON parse failed: %d\n", rc);
		INST_LOG(inst, LDMSD_LINFO, "input from %s was: %s\n",
			val, buff);
		goto out;
	}

	ent = json_value_find(jdoc, "argv_sep");
	if (ent) {
		if (ent->type != JSON_STRING_VALUE) {
			rc = EINVAL;
			INST_LOG(inst, LDMSD_LERROR,
				"Error: `argv_sep` must be a string.\n");
			goto out;
		}
		inst->argv_sep = strdup(json_value_cstr(ent));
		if (!inst->argv_sep) {
			rc = ENOMEM;
			INST_LOG(inst, LDMSD_LERROR,
				"Out of memory while configuring argv_sep\n");
			goto out;
		}
		if (check_sep(inst, inst->argv_sep)) {
			rc = ERANGE;
			INST_LOG(inst, LDMSD_LERROR,
				"Config argv_sep='%s' is not supported.\n",
				inst->argv_sep);
			goto out;
		}
	}
	ent = json_value_find(jdoc, "instance_prefix");
	if (ent) {
		if (ent->type != JSON_STRING_VALUE) {
			rc = EINVAL;
			INST_LOG(inst, LDMSD_LERROR,
				"Error: `instance_prefix` must be a string.\n");
			goto out;
		}
		inst->instance_prefix = strdup(json_value_cstr(ent));
		if (!inst->instance_prefix) {
			rc = ENOMEM;
			INST_LOG(inst, LDMSD_LERROR,
				"Out of memory while configuring.\n");
			goto out;
		}
	}
	ent = json_value_find(jdoc, "exe_suffix");
	if (ent) {
		inst->exe_suffix = 1;
	}
	ent = json_value_find(jdoc, "sc_clk_tck");
	if (ent) {
		inst->sc_clk_tck = sysconf(_SC_CLK_TCK);
		if (!inst->sc_clk_tck) {
			INST_LOG(inst, LDMSD_LERROR,
				"sysconf(_SC_CLK_TCK) returned 0.\n");
		} else {
			INST_LOG(inst, LDMSD_LINFO,
				"sysconf(_SC_CLK_TCK) = %ld.\n",
				inst->sc_clk_tck);
		}
	}
	ent = json_value_find(jdoc, "stream");
	if (ent) {
		if (ent->type != JSON_STRING_VALUE) {
			rc = EINVAL;
			INST_LOG(inst, LDMSD_LERROR,
				"Error: `stream` must be a string.\n");
			goto out;
		}
		inst->stream_name = strdup(json_value_cstr(ent));
		if (!inst->stream_name) {
			rc = ENOMEM;
			INST_LOG(inst, LDMSD_LERROR,
				"Out of memory while configuring.\n");
			goto out;
		}
	} /* else, caller will later set default stream_name */
	ent = json_value_find(jdoc, "syscalls");
	if (ent) {
		if (ent->type != JSON_STRING_VALUE) {
			rc = EINVAL;
			INST_LOG(inst, LDMSD_LERROR,
				"Error: `syscalls` must be a path.\n");
			goto out;
		}
		inst->syscalls = strdup(json_value_cstr(ent));
		if (!inst->syscalls) {
			rc = ENOMEM;
			INST_LOG(inst, LDMSD_LERROR,
				"Out of memory while configuring.\n");
			goto out;
		}
	}
	ent = json_value_find(jdoc, "argv_fmt");
	if (ent) {
		if (ent->type != JSON_INT_VALUE) {
			rc = EINVAL;
			INST_LOG(inst, LDMSD_LERROR,
				"Error: `argv_fmt` must be integer.\n");
			goto out;
		}
		inst->argv_fmt = json_value_int(ent);
	}
	ent = json_value_find(jdoc, "argv_msg");
	if (ent) {
		if (ent->type != JSON_INT_VALUE) {
			rc = EINVAL;
			INST_LOG(inst, LDMSD_LERROR,
				"Error: `argv_msg` must be 1 or 0.\n");
			goto out;
		}
		inst->argv_msg = (json_value_int(ent) != 0);
	}
	ent = json_value_find(jdoc, "fd_msg");
	if (ent) {
		if (ent->type != JSON_INT_VALUE) {
			rc = EINVAL;
			INST_LOG(inst, LDMSD_LERROR,
				"Error: `fd_msg` must be integer.\n");
			goto out;
		}
		inst->fd_msg = json_value_int(ent);
		if (inst->fd_msg < 0) {
			inst->fd_msg = 0;
			INST_LOG(inst, LDMSD_LERROR,
				"Error: `fd_msg` must be positive/0 integer.\n");
			goto out;
		}
		if (inst->fd_msg) {
			ent = json_value_find(jdoc, "fd_exclude");
			if (ent) {
				char *fregex = json_to_regex(inst, ent);
				if (!fregex) {
					INST_LOG(inst, LDMSD_LERROR,
						"fd_exclude failed.\n");
					goto out;
				}
				rc = init_regex_filter(inst, fregex, "fd",
					&inst->fd_use_regex, &inst->fd_regex);
				free(fregex);
				if (rc)
					goto out;
				INST_LOG(inst, LDMSD_LDEBUG,
					"fd_exclude parsed.\n");
			}
		}
	}
	ent = json_value_find(jdoc, "env_msg");
	if (ent) {
		if (ent->type != JSON_INT_VALUE) {
			rc = EINVAL;
			INST_LOG(inst, LDMSD_LERROR,
				"Error: `env_msg` must be 1 or 0\n");
			goto out;
		}
		inst->env_msg = (json_value_int(ent) != 0);
		if (inst->env_msg) {
			ent = json_value_find(jdoc, "env_exclude");
			if (ent) {
				char *eregex = json_to_regex(inst, ent);
				if (!eregex) {
					INST_LOG(inst, LDMSD_LERROR,
						"env_exclude failed.\n");
					goto out;
				}
				rc = init_regex_filter(inst, eregex, "env",
					&inst->env_use_regex, &inst->env_regex);
				free(eregex);
				if (rc)
					goto out;
				INST_LOG(inst, LDMSD_LDEBUG,
					"env_exclude parsed.\n");
			}
		}
	}
	ent = json_value_find(jdoc, "log_send");
	if (ent) {
		if (ent->type != JSON_INT_VALUE) {
			rc = EINVAL;
			INST_LOG(inst, LDMSD_LERROR,
				"Error: `log_send` must be 1 or 0\n");
			goto out;
		}
		inst->log_send = (json_value_int(ent) != 0);
	}
	list = json_value_find(jdoc, "metrics");
	if (list) {
		for (ent = json_item_first(list); ent;
					ent = json_item_next(ent)) {
			if (ent->type != JSON_STRING_VALUE) {
				rc = EINVAL;
				INST_LOG(inst, LDMSD_LERROR,
					 "Error: metric must be a string.\n");
				goto out;
			}
			minfo = find_metric_info_by_name(json_value_cstr(ent));
			if (!minfo) {
				rc = ENOENT;
				missing_metric(inst, json_value_cstr(ent));
				goto out;
			}
			inst->metric_idx[minfo->code] = -1;
			/* Non-zero to enable. This will be replaced with the
			 * actual metric index later. */
		}
	} else {
		for (i = _APP_FIRST; i <= _APP_LAST; i++) {
			inst->metric_idx[i] = -1;
		}
	}
	rc = 0;

 out:
	close(fd);
	if (buff)
		free(buff);
	if (jp)
		json_parser_free(jp);
	if (jdoc)
		json_entity_free(jdoc);
	return rc;
}

/* verifies named entity has type et unless et is JSON_NULL_VALUE */
uint64_t get_field_value_u64(linux_proc_sampler_inst_t inst, json_entity_t src, enum json_value_e et, const char *name)
{
	json_entity_t e = json_value_find(src, name);
	if (!e) {
		INST_LOG(inst, LDMSD_LDEBUG, "no json attribute %s found.\n", name);
		errno = ENOKEY;
		return 0;
	}
	if ( e->type != et && et != JSON_NULL_VALUE) {
		INST_LOG(inst, LDMSD_LDEBUG, "wrong type found for %s: %s. Expected %s.\n",
			name, json_type_name(e->type), json_type_name(et));
		errno = EINVAL;
		return 0;
	}
	uint64_t u64;
	switch (et) {
	case JSON_STRING_VALUE:
		if (sscanf(json_value_cstr(e),"%" SCNu64, &u64) == 1) {
			return u64;
		} else {
			INST_LOG(inst, LDMSD_LDEBUG, "unconvertible to uint64_t: %s from %s.\n",
				json_value_cstr(e), name);
			errno = EINVAL;
			return 0;
		}
	case JSON_INT_VALUE:
		if ( json_value_int(e) < 0) {
			INST_LOG(inst, LDMSD_LDEBUG, "unconvertible to uint64_t: %" PRId64 " from %s.\n",
				json_value_int(e), name);
			errno = ERANGE;
			return 0;
		}
		return (uint64_t)json_value_int(e);
	case JSON_FLOAT_VALUE:
		if ( json_value_float(e) < 0 || json_value_float(e) > UINT64_MAX) {
			errno = ERANGE;
			INST_LOG(inst, LDMSD_LDEBUG, "unconvertible to uint64_t: %g from %s.\n",
				json_value_float(e), name);
			return 0;
		}
		return (uint64_t)json_value_float(e);
	default:
		errno = EINVAL;
		return 0;
	}
}

static json_entity_t get_field(linux_proc_sampler_inst_t inst, json_entity_t src, enum json_value_e et, const char *name)
{
#ifndef LPDEBUG
	(void)inst;
#endif
	json_entity_t e = json_value_find(src, name);
	if (!e) {
#ifdef LPDEBUG
		INST_LOG(inst, LDMSD_LDEBUG, "no json attribute %s found.\n", name);
#endif
		return NULL;
	}
	if ( e->type != et) {
#ifdef LPDEBUG
		INST_LOG(inst, LDMSD_LDEBUG, "wrong type found for %s: %s. Expected %s.\n",
			name, json_type_name(e->type), json_type_name(et));
#endif
		return NULL;
	}
	return e;
}

/* functions lifted from forkstat split out here for copyright notice considerations */
#include "get_stat_field.c"

static uint64_t get_start_tick(linux_proc_sampler_inst_t inst, json_entity_t data, int64_t pid)
{
	if (!inst || !data)
		return 0;
	uint64_t start_tick = get_field_value_u64(inst, data, JSON_STRING_VALUE, "start_tick");
	if (start_tick)
		return start_tick;
	char path[PATH_MAX];
	char buf[STAT_COMM_SZ];
	snprintf(path, sizeof(path), "/proc/%" PRId64 "/stat", pid);
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return 0;
	ssize_t n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return 0;
	buf[n] = '\0';
	const char* ptr = get_proc_self_stat_field(buf, 22);
	uint64_t start = 0;
	int n2 = sscanf(ptr, "%" PRIu64, &start);
        if (n2 != 1)
		return 0;
        return start;
}

/* BEGIN of support functions for collecting/publishing env and cmdline
 * stream messages. */

/* return array of strings o_count long read from /proc/pid/n,
 * set bytes to total bytes read, len_max to longest single item. */
static char **get_proc_strings(pid_t pid, const char *n, int *o_count, ssize_t *bytes, size_t *len_max)
{
	char fname[64];
	snprintf(fname, 64, "/proc/%d/%s", pid, n);

	ssize_t read = 0;
	size_t rcap = 256;
	char **result = malloc(rcap*sizeof(char *));
	size_t rlen = 0;
	size_t slen = 4096;
	FILE *f = NULL;
	*bytes = 0;
	char *s = malloc(slen);
	if (!s || !result || !o_count || !bytes) {
		goto err;
		errno = EINVAL;
	}
	f = fopen(fname, "r");
	if (!f)
		goto err;
	*len_max = 0;
	while ( (read = getdelim(&s, &slen, '\0', f)) != -1) {
		*bytes += read;
		if (read > *len_max)
			*len_max = read;
		if (rlen == rcap) {
			rcap *= 2;
			char **newresult = realloc(result, rcap *sizeof(char *));
			if (!newresult) {
				errno = ENOMEM;
				goto err;
			}
			result = newresult;
		}
		result[rlen] = malloc(read);
		if (!result[rlen]) {
			errno = ENOMEM;
			goto err;
		}
		strncpy(result[rlen], s, read);
		rlen++;
	}
	if (rlen < rcap)
		result[rlen] = NULL;
	free(s);
	s = NULL;
	*o_count = rlen;
	fclose(f);
	return result;
err:
	if (result) {
		while (rlen > 0) {
			rlen--;
			free(result[rlen]);
			result[rlen] = NULL;
		}
		free(result);
		result = NULL;
	}
	if (f)
		fclose(f);
	free(s);
	*o_count = 0;
	return result;
}

static void release_proc_strings(char **result, int argc)
{
	if (!result)
		return;
	int i;
	for (i = 0; i < argc; i++)
		free(result[i]);
	free(result);
}

/* return 1 if bad (vsub not defined),
 * fill vsub with escaped string.
 * json escape chars become \X, bit-8 chars are bad. */
int string_clean_json(const char *v, char *vsub, size_t vsub_sz)
{
	int i = 0;
	int j = 0;
	if (!v || !vsub)
		return 1;
	while (v[i] != '\0' && j < vsub_sz) {
		if (v[i] > 126)
			return 1;
		switch(v[i]) {
		case '\\':
			vsub[j++] = '\\';
			vsub[j++] = '\\';
			i++;
			continue;
		case '\"':
			vsub[j++] = '\\';
			vsub[j++] = '"';
			i++;
			continue;
		case '\b':
			vsub[j++] = '\\';
			vsub[j++] = 'b';
			i++;
			continue;
		case '\f':
			vsub[j++] = '\\';
			vsub[j++] = 'f';
			i++;
			continue;
		case '\n':
			vsub[j++] = '\\';
			vsub[j++] = 'n';
			i++;
			continue;
		case '\r':
			vsub[j++] = '\\';
			vsub[j++] = 'r';
			i++;
			continue;
		case '\t':
			vsub[j++] = '\\';
			vsub[j++] = 't';
			i++;
			continue;
		default:
			break;
		}
#ifdef REMAP_CTL_CHAR
		if (v[i] < 32) {
			sprintf(&vsub[j], "\\u%.04x", v[i]);
			j += 6;
			continue;
		}
#endif
		vsub[j++] = v[i++];
	}
	if (j < vsub_sz)
		vsub[j++] = '\0';
	else
		return 2;
	return 0;
}

/* put a lock around here if we need it, but don't if publication
 * is serialized at the set addition/callback, as it seems to be */
static char *get_buf(linux_proc_sampler_inst_t inst, size_t n)
{
	if (!inst->recycle_buf) {
		inst->recycle_buf = malloc(2*n);
		if (!inst->recycle_buf)
			return NULL;
		inst->recycle_buf_sz = 2*n;
		return inst->recycle_buf;
	}
	if (inst->recycle_buf_sz < 2*n) {
		char *s = malloc(2*n);
		if (!s)
			return NULL;
		free(inst->recycle_buf);
		inst->recycle_buf = s;
		inst->recycle_buf_sz = 2*n;
	}
	return inst->recycle_buf;
}

/* the env/argv data to be published can disappear under us, so there's
 * no "fail" for these publication. Best-effort only.
 */
static int publish_env_pid(linux_proc_sampler_inst_t inst, struct linux_proc_sampler_set *app_set, const char *start, const char *exe, bool is_thread, pid_t parent, uint64_t jobid)
{
	if (!app_set || app_set->dead)
		return EINVAL;
	int rc = 0;
	int64_t task_rank = app_set->task_rank;
	uint64_t compid = inst->base_data->component_id;
	pid_t pid = app_set->key.os_pid;
	const char *producer = inst->base_data->producer_name;
	int argc = 0;
	ssize_t bytes;
	size_t item_max = 0;
	char *vsub = NULL;

	char **envp = get_proc_strings(pid, "environ", &argc, &bytes, &item_max);
	if (!envp) {
		return EBADF;
	}
	/* maxlensum: prod compid pid jobid time rank parent thread */
	size_t buf_sz = 64 + 20 + 20 + 20 + 26 + 20 + 20 + 6;
	const char* id_format =
		"{"
		"\"producerName\":\"%s\""
		",\"component_id\":%" PRIu64
		",\"pid\":%d"
		",\"job_id\":%" PRIu64
		",\"timestamp\":\"%s\""
		",\"task_rank\":%" PRId64
		",\"parent\":%d"
		",\"is_thread\":%s"
		",\"exe\":\"%s\",\"data\":[";
	static size_t id_format_sz;
	if (!id_format_sz)
		id_format_sz = strlen(id_format);
	const char *element_format = "{\"k\":\"%s\",\"v\":\"%s\"}";
	static size_t element_format_sz;
	if (!element_format_sz)
		element_format_sz = strlen(element_format);
	buf_sz += strlen(exe) + 1;
	/* protocol 1: { idlist, "data":[{"k":"val","v":"val"},...]} */
	buf_sz += strlen("[]") + argc*(1 + element_format_sz) +
		strlen("]}") + id_format_sz; /* json overhead */
	buf_sz += bytes; /* strings data */
	char *buf = get_buf(inst, buf_sz);
	if (!buf) {
		rc = ENOMEM;
		goto out;
	}
	size_t off = snprintf(buf, buf_sz, id_format,
			producer,
			compid,
			pid,
			jobid,
			start,
			task_rank,
			parent,
			is_thread ? "1" : "0",
			exe);

	int i; int more;
	char *k, *v;
	int notfirst = 0;
	size_t vsub_sz = 2*item_max+1;
	vsub = malloc(vsub_sz);
	if (!vsub) {
		rc = ENOMEM;
		goto out;
	}
	for (i = 0; i < argc; i++) {
		k = envp[i];
		if (!k) {
			continue;
		}
		v = strchr(k, '=');
		if (!v) {
			continue;
		}
		v[0] = '\0';
		v++;
		if (inst->env_use_regex &&
			0 == regexec(&(inst->env_regex), k, 0, NULL, 0))
			continue;
		int dirty = string_clean_json(v, vsub, vsub_sz);
		switch (dirty) {
		case 1:
			INST_LOG(inst, LDMSD_LWARNING,
				"cannot send env val of %s for pid %d\n",
				k, pid);
			continue;
		case 2:
			INST_LOG(inst, LDMSD_LWARNING,
				"esc too long of env val %s for pid %d\n",
				k, pid);
			continue;
		default:
			break;
		}
		if (notfirst) {
			more = snprintf(buf+off, buf_sz-off, ",");
			off += more;
		} else {
			notfirst = 1;
		}
		more = snprintf(buf+off, buf_sz-off, element_format, k, vsub);
		off += more;
	}
	more = snprintf(buf+off, buf_sz-off, "]}");
	off += more;
	if (buf_sz < off) {
		INST_LOG(inst, LDMSD_LERROR, "env buf_sz miscalculated.\n");
	}
#ifdef USE_PNAME
	char pname[64+20];
	snprintf(pname, 84, "%s/linux_proc_sampler", producer);
#else
	char *pname= NULL;
#endif
	if (inst->log_send) {
		INST_LOG(inst, LDMSD_LDEBUG,
			"Sending pid %d env with count %d size %zu %s%s\n",
			app_set->key.os_pid, argc, off, pname ? " on" : "",
			pname ? pname : "");
	}
	ldmsd_stream_deliver(inst->env_stream, LDMSD_STREAM_JSON,
				buf, off, NULL, pname);
out:
	free(vsub);
	release_proc_strings(envp, argc);
	return rc;
}

static int publish_argv_pid(linux_proc_sampler_inst_t inst, struct linux_proc_sampler_set *app_set, const char *start, const char *exe, bool is_thread, pid_t parent, uint64_t jobid)
{
	if (!app_set || app_set->dead)
		return EINVAL;
	int64_t task_rank = app_set->task_rank;
	uint64_t compid = inst->base_data->component_id;
	pid_t pid = app_set->key.os_pid;
	const char *producer = inst->base_data->producer_name;
	int argc = 0;
	ssize_t bytes;
	int rc = 0;
	size_t maxlen = 0;
	char *vbuf = NULL;
	char **argv = get_proc_strings(pid, "cmdline", &argc, &bytes, &maxlen);
	if (!argv) {
		return EBADF;
	}

	/* lensum: prod compid pid jobid time rank parent thread */
	size_t buf_sz = 64 + 20 + 20 + 20 + 26 + 20 + 20 + 6;
	buf_sz += strlen(exe) + 1;
	buf_sz += bytes; /* strings, excluding protocol formatting. */
	const char *id_format = "{"
		"\"producerName\":\"%s\""
		",\"component_id\":%" PRIu64
		",\"pid\":%d"
		",\"job_id\":%" PRIu64
		",\"timestamp\":\"%s\""
		",\"task_rank\":%" PRId64
		",\"parent\":%d"
		",\"is_thread\":%s"
		",\"exe\":\"%s\",\"data\":[";
	static size_t id_format_sz;
	if (!id_format_sz)
		id_format_sz = strlen(id_format);
	const char *element_format1 = "\"%s\",";
	static size_t element_format1_sz;
	if (!element_format1_sz)
		element_format1_sz = strlen(element_format1);
	const char *element_format2 = "{\"k\":%d,\"v\":\"%s\"},";
	static size_t element_format2_sz;
	if (!element_format2_sz)
		element_format2_sz = strlen(element_format2);
	switch (inst->argv_fmt) {
	case 1:
		/* protocol 1: { idlist, "data":["val",...]} */
		buf_sz += strlen("[]") + argc*element_format1_sz
			 + strlen("]}") + id_format_sz; /* json overhead */
		break;
	case 2:
		/* protocol 2: { idlist, "data":[{"k":"val","v":"val"},...]} */
		/* allow 10 bytes per array index value; */
		buf_sz += strlen("[]") + argc*(10 + element_format2_sz)
			 + strlen("]}") + id_format_sz; /* json overhead */
		break;
	default:
		INST_LOG(inst, LDMSD_LERROR, "unsupported argv_fmt\n");
		rc = EINVAL;
		goto out;
	}
	char *buf = get_buf(inst, buf_sz);
	if (!buf) {
		rc = ENOMEM;
		goto out;
	}
	size_t vbuf_sz = 2 * maxlen + 1;
	vbuf = malloc(vbuf_sz);
	if (!vbuf) {
		rc = ENOMEM;
		goto out;
	}

	size_t off = snprintf(buf, buf_sz, id_format,
			producer,
			compid,
			pid,
			jobid,
			start,
			task_rank,
			parent,
			is_thread ? "1" : "0",
			exe);

	int i; int more;
	switch (inst->argv_fmt) {
	case 1:
		for (i = 0; i < argc; i++) {
			int ce = string_clean_json(argv[i], vbuf, vbuf_sz);
			if (ce) {
				INST_LOG(inst, LDMSD_LERROR,
					"pid %d argv %d cannot be json\n");
				rc = EINVAL;
				goto out;
			}
			more = snprintf(buf+off, buf_sz-off, element_format1,
					 vbuf);
			off += more;
		}
		more = snprintf(buf+off-1, buf_sz-off+1, "]}") - 1;
		break;
	case 2:
		for (i = 0; i < argc; i++) {
			int ce = string_clean_json(argv[i], vbuf, vbuf_sz);
			if (ce) {
				INST_LOG(inst, LDMSD_LERROR,
					"pid %d argv %d cannot be json\n");
				rc = EINVAL;
				goto out;
			}
			more = snprintf(buf+off, buf_sz-off, element_format2,
				i, argv[i]);
			off += more;
		}
		more = snprintf(buf+off-1, buf_sz-off+1, "]}") -1;
		break;
	default:
		INST_LOG(inst, LDMSD_LERROR, "unsupported argv_fmt\n");
		rc = EINVAL;
		goto out;
	}
	off += more;

	if (inst->recycle_buf_sz < off) {
		INST_LOG(inst, LDMSD_LERROR,
			"argv buf_sz miscalculated (%zu < %zu).\n",
			inst->recycle_buf_sz, off);
	}
#ifdef USE_PNAME
	char pname[HOST_NAME_MAX + 21];
	snprintf(pname, 84, "%s/linux_proc_sampler",
		inst->base_data->producer_name);
#else
	char *pname= NULL;
#endif
	if (inst->log_send) {
		INST_LOG(inst, LDMSD_LDEBUG,
			"Sending pid %d argv %s%s%s\n",
			app_set->key.os_pid, pname ? " on" : "",
			pname ? pname : "");
	}
	ldmsd_stream_deliver(inst->argv_stream, LDMSD_STREAM_JSON, buf, off, NULL, pname);
out:
	release_proc_strings(argv, argc);
	free(vbuf);
	return rc;
}

static int fn_rbn_cmp(void *tree_key, void *key)
{
	int *tk = tree_key;
	int *k = key;
	if (!tree_key || !key) {
		ldmsd_log(LDMSD_LDEBUG,"fn_rbn_cmp NULL: %d %d\n", *tk, *k);
		return -1;
	}
	return *tk - *k;
}

/* info about file linked in fd/ */
struct fentry {
	pid_t pid;
	int l_fd; /* link fd number */
	struct rbn fn_rbn; /* l_fd-indexed tree */
	ino_t l_ino;  /* link inode */
	int ignore; /* ignore this fe until l_ino changes */
	char *name; /* name of file linked to */
	size_t nlen; /* strlen of name */
	int seen; /* visited during dir scan */
};

void fentry_destroy(linux_proc_sampler_inst_t inst, struct fentry *f)
{
	if (!f)
		return;
	free(f->name);
	f->name = NULL;
	free(f);
}

void fn_rbt_destroy(linux_proc_sampler_inst_t inst, struct rbt *t)
{
	struct fentry *fe;
	struct rbn *rbn;
	while ((rbn = rbt_min(t))) {
		rbt_del(t, rbn);
		fe = container_of(rbn, struct fentry, fn_rbn);
		fentry_destroy(inst, fe);
	}
}

static int fentry_mark_seen(struct rbn *r, void *fdata, int i)
{
	(void)i;
	int64_t v = (int64_t)fdata;
	struct fentry *fe = container_of(r, struct fentry, fn_rbn);
	fe->seen = (v != 0);
	return 0;
}

/*
 * send state and stat info to stream.
 */
static int string_send_state(linux_proc_sampler_inst_t inst, struct linux_proc_sampler_set *app_set, struct stat *s, const char *str, size_t nlen, const char *state, int fd)
{
	if (!inst) {
		INST_LOG(inst, LDMSD_LDEBUG, "!inst, pid %d\n", app_set->key.os_pid);
		return 0;
	}
	if (!str ) {
		INST_LOG(inst, LDMSD_LDEBUG, "!str, pid %d\n", app_set->key.os_pid);
		return 0;
	}
	if (!app_set) {
		INST_LOG(inst, LDMSD_LDEBUG, "!app_set, pid %d\n", app_set->key.os_pid);
		return 0;
	}

	const char *stat_format= "%s"
		"{\"file\":\"%s\","
		"\"st_dev\":\"[%lx,%lx]\","
		"\"st_ino\":%ld,"
		"\"st_mode\":%o,"
		"\"st_size\":%zu,"
		"\"mtime\":%ld.%09ld,"
		"\"state\":\"%s\"}]}";
	static size_t stat_format_sz;
	if (!stat_format_sz)
		stat_format_sz = strlen(stat_format);

	size_t slen = app_set->fd_ident_sz + stat_format_sz + nlen + 6*20 + 10;
	size_t ssize;
	char *buf = get_buf(inst, slen);
	if (!buf)
		return ENOMEM;
	if (s)
		ssize = snprintf(buf, slen, stat_format,
			app_set->fd_ident,
			str,
			(long) major(s->st_dev), (long) minor(s->st_dev),
			s->st_ino,
			s->st_mode,
			s->st_size,
			s->st_mtim.tv_sec, s->st_mtim.tv_nsec, state);
	else
		ssize = snprintf(buf, slen, stat_format,
			app_set->fd_ident,
			str,
			-1, -1, -1, 0, -1, 0, 0, state);

	if (inst->recycle_buf_sz < ssize) {
		INST_LOG(inst, LDMSD_LERROR, "fd buf_sz miscalculated. %zu < %zu\n",
			slen, ssize);
	}
#ifdef USE_PNAME
	char pname[HOST_NAME_MAX + 21];
	snprintf(pname, 84, "%s/linux_proc_sampler",
		inst->base_data->producer_name);
#else
	char *pname= NULL;
#endif
	if (inst->log_send) {
		INST_LOG(inst, LDMSD_LDEBUG,
			"Sending pid %d fd %d file %s new state %s%s%s\n",
			app_set->key.os_pid, fd, str, state, pname ? " on" : "",
			pname ? pname : "");
	}
	ldmsd_stream_deliver(inst->fd_stream, LDMSD_STREAM_JSON, buf, ssize,
			 NULL, pname);
	return 0;
}

/*
 * send state and stat info to stream.
 */
static int fentry_send_state(linux_proc_sampler_inst_t inst, struct linux_proc_sampler_set *app_set, struct stat *s, struct fentry *fe, const char *state)
{
	if (!fe ) {
		INST_LOG(inst, LDMSD_LDEBUG, "null fe, pid %d\n", app_set->key.os_pid);
		return 0;
	}
	if (!fe->name) {
		INST_LOG(inst, LDMSD_LDEBUG, "null fe>name, pid %d\n", app_set->key.os_pid);
		return 0;
	}
	if (fe->seen) {
		INST_LOG(inst, LDMSD_LDEBUG, "fe>seen, pid %d\n", app_set->key.os_pid);
		return 0;
	}
	return string_send_state(inst, app_set, s, fe->name, fe->nlen,
				state, fe->l_fd);
}

struct dfentry {
	char *name;
	size_t nlen;
	int fd;
	struct fentry *fe;
	LIST_ENTRY(dfentry) del;
};

LIST_HEAD(dfentry_del_list, dfentry);

#define BUFMAX 4096
/* update file tree and emit open/close messages for changes.
 * shadow all fd/ entries with fentries.
 * mark ignore any of the wrong type.
 * track change of fd:inode pairs
 * scan:
 *   visit tree:
 *     mark seen 0
 *   read each fd
 *     if present
 *        if inode unchanged
 *           mark seen 1
 *        else
 *           add/move old name to dellist
 *           update name, inode
 *     if not already present
 *       create fe
 *     if !ignore
 *       send message
 *   mark seen 1
 *   visit tree:
 *     if unseen
 *       add fe to dellist
 *   clear dellist
 */
static int publish_fd_pid(linux_proc_sampler_inst_t inst, struct linux_proc_sampler_set *app_set)
{
	if (!app_set)
		return EINVAL;
	int erc = 0;

	struct rbn *rbn;
	struct dfentry_del_list del_list;
	struct dfentry *dfe;
	LIST_INIT(&del_list);
	rbt_traverse(&app_set->fn_rbt, (rbn_node_fn)fentry_mark_seen, (void *)0);
	if (app_set->dead) {
		erc = ENOENT;
		goto close_check;
	}

	char path[PROCPID_SZ];
	char dname[PATH_MAX];
	int blen;
	char buf[BUFMAX];
	DIR *dir;
	struct dirent *dent;
	snprintf(path, sizeof(path), "/proc/%" PRId64 "/fd",
		app_set->key.os_pid);
	dir = opendir(path);
	if (!dir) {
		erc = ENOENT;
		goto close_check;
	}
	errno = 0;
	long n;
	char *endptr;
	struct fentry *fe;
	while ((dent = readdir(dir))) {
		fe = NULL;
		buf[0] = '\0';
		n = strtol(dent->d_name, &endptr, 10);
		if (endptr && (endptr[0] != '\0')) {
			/* This is not a file descriptor, e.g., '.' and '..' */
			continue;
		}
		struct rbn *nalready = rbt_find(&app_set->fn_rbt, &n);
		if (nalready) {
			fe = container_of(nalready, struct fentry, fn_rbn);
			/* the file descriptor will not change what file it points
			 * to without a change of inode # in the symlink. */
			if (fe->l_ino == dent->d_ino) {
				fe->seen = 1;
				continue;
			}
			/* recycle fe */
			fe->l_ino = dent->d_ino;
			if (!fe->ignore) {
				dfe = calloc(1, sizeof(*dfe));
				if (!dfe) {
					erc = ENOMEM;
					continue;
				}
				dfe->fd = fe->l_fd;
				dfe->name = fe->name;
				dfe->nlen = fe->nlen;
				LIST_INSERT_HEAD(&del_list, dfe, del);
			} else {
				free(fe->name); /* ever needed? */
			}
			fe->name = NULL;
		}
		buf[0] = '\0';
		snprintf(dname, sizeof(dname), "%s/%s", path, dent->d_name);
		blen = readlink(dname, buf, BUFMAX);
		if (blen < 0 || blen == BUFMAX) /* file closed or too big */
			continue;
		buf[blen] = '\0';
		if (!fe) {
			fe = calloc(1, sizeof(*fe));
			if (!fe) {
				erc = ENOMEM;
				continue;
			}
			fe->l_fd = n;
			fe->l_ino = dent->d_ino;
			fe->pid = app_set->key.os_pid;
			rbn_init(&(fe->fn_rbn), (void*)(&fe->l_fd));
			rbt_ins(&app_set->fn_rbt, &(fe->fn_rbn));
		}
		if (buf[0] != '/' || (inst->fd_use_regex &&
			0 == regexec(&(inst->fd_regex), buf, 0, NULL, 0))) {
			fe->ignore = 1;
			fe->seen = 1;
			continue;
		} else {
			fe->name = malloc(blen+1);
			fe->nlen = blen;
			if (!fe->name) {
				fe->seen = 0;
				erc = ENOMEM;
				continue;
			}
			strcpy(fe->name, buf);
			struct stat statb;
			memset(&statb, 0, sizeof(statb));
			if (stat(buf, &statb)) {
				INST_LOG(inst, LDMSD_LDEBUG, "cannot stat %s: %d %s\n",
					buf, errno, STRERROR(errno));
				continue;
			}
			fentry_send_state(inst, app_set, &statb, fe, "opened");
		}
		fe->seen = 1;
	}

	closedir(dir);
	const char *msg;
	/* scan tree for unseen, send close/deleted, and free */
close_check:
	RBT_FOREACH(rbn, &app_set->fn_rbt) {
		fe = container_of(rbn, struct fentry, fn_rbn);
		if (fe->seen)
			continue;
		if (!fe->ignore) {
			msg = "closed";
			struct stat statb;
			memset(&statb, 0, sizeof(statb));
			struct stat *statp = &statb;
			if (stat(fe->name, statp)) {
				msg = "deleted";
				statp = NULL;
			}
			fentry_send_state(inst, app_set, statp, fe, msg);
		}
		dfe = calloc(1, sizeof(*dfe));
		if (!dfe) {
			erc = ENOMEM;
			continue;
		}
		dfe->fe = fe;
		LIST_INSERT_HEAD(&del_list, dfe, del);
	}
	while (!LIST_EMPTY(&del_list)) {
                dfe = LIST_FIRST(&del_list);
		if (dfe->fe) {
			fe = dfe->fe;
			rbn = rbt_find(&(app_set->fn_rbt), &fe->l_fd);
			if (rbn)
				rbt_del(&app_set->fn_rbt, rbn);
			fentry_destroy(inst, fe);
			dfe->fe = NULL;
		} else {
			msg = "closed";
			struct stat statb;
			memset(&statb, 0, sizeof(statb));
			struct stat *statp = &statb;
			if (stat(dfe->name, statp)) {
				msg = "deleted";
				statp = NULL;
			}
			INST_LOG(inst, LDMSD_LDEBUG, "fd state %s %s\n", msg, dfe->name);
			string_send_state(inst, app_set, statp, dfe->name, dfe->nlen, msg, dfe->fd);
			free(dfe->name);
			dfe->name = NULL;
		}
                LIST_REMOVE(dfe, del);
		free(dfe);
        }
	return erc;
}

/* create identifier string, and then move on to publish_fd_pid */
static int publish_fd_pid_first(linux_proc_sampler_inst_t inst, struct linux_proc_sampler_set *app_set, const char *start, const char *exe, bool is_thread, pid_t parent, uint64_t jobid)
{
	if (!app_set || app_set->dead)
		return EINVAL;
	rbt_init(&(app_set->fn_rbt), (rbn_comparator_t) fn_rbn_cmp);

	int64_t task_rank = app_set->task_rank;
	uint64_t compid = inst->base_data->component_id;
	pid_t pid = app_set->key.os_pid;
	const char *producer = inst->base_data->producer_name;

	const char *id_format = "{"
		"\"producerName\":\"%s\""
		",\"component_id\":%" PRIu64
		",\"pid\":%d"
		",\"job_id\":%" PRIu64
		",\"timestamp\":\"%s\""
		",\"task_rank\":%" PRId64
		",\"parent\":%d"
		",\"is_thread\":%s"
		",\"exe\":\"%s\",\"data\":[";
	static size_t id_format_sz;
	if (!id_format_sz)
		id_format_sz = strlen(id_format);
	/* numeric field sum: prod compid pid jobid time rank parent thread */
	size_t buf_sz = 64 + 20 + 20 + 20 + 26 + 20 + 20 + 6;
	buf_sz += strlen(exe) + 1;
	/* json overhead space */
	buf_sz += id_format_sz;
	char *buf = malloc(buf_sz);
	if (!buf) {
		return ENOMEM;
	}

	size_t off = snprintf(buf, buf_sz, id_format,
			producer,
			compid,
			pid,
			jobid,
			start,
			task_rank,
			parent,
			is_thread ? "1" : "0",
			exe);
	app_set->fd_ident = buf;
	app_set->fd_ident_sz = off + 1;
	return publish_fd_pid(inst, app_set);
}


/* END of support functions for collecting/publishing env, fd and cmdline
 * stream messages. */

static
int __handle_task_init(linux_proc_sampler_inst_t inst, json_entity_t data)
{
	/* create a set per task; deduplicate if multiple spank and linux proc sources
	 * are both active. */
	struct linux_proc_sampler_set *app_set;
	int rc;
	ldms_set_t set;
	int len;
	jbuf_t bjb = NULL;
	json_entity_t os_pid;
	json_entity_t task_pid;
	json_entity_t task_rank;
	json_entity_t exe;
	json_entity_t is_thread;
	json_entity_t parent_pid;
	json_entity_t start;
	char setname[512];
	exe = get_field(inst, data, JSON_STRING_VALUE, "exe");
	start = get_field(inst, data, JSON_STRING_VALUE, "start");
	errno = 0;
	bool job_id = true;
	uint64_t job_id_val =
		get_field_value_u64(inst, data, JSON_NULL_VALUE, "job_id");
	if (errno)
		job_id = false;
	os_pid = get_field(inst, data, JSON_INT_VALUE, "os_pid");
	task_pid = get_field(inst, data, JSON_INT_VALUE, "task_pid");
	parent_pid = get_field(inst, data, JSON_INT_VALUE, "parent_pid");
	is_thread = get_field(inst, data, JSON_INT_VALUE, "is_thread");
	if (!job_id && (!os_pid && !task_pid)) {
		INST_LOG(inst, LDMSD_LINFO, "need job_id or (os_pid & task_pid)\n");
		goto dump;
	}
	pid_t  pid;
	if (os_pid)
		pid = (pid_t)json_value_int(os_pid);
	else
		pid = json_value_int(task_pid); /* from spank plugin */

	bool is_thread_val = false;
	pid_t parent = 0;
	if (is_thread && parent_pid) {
		is_thread_val = json_value_int(is_thread);
		parent = json_value_int(parent_pid);
	} else {
		parent = get_parent_pid(pid, &is_thread_val);
	}
	uint64_t start_tick = get_start_tick(inst, data, pid);
	if (!start_tick) {
		INST_LOG(inst, LDMSD_LDEBUG, "ignoring start-tickless pid %"
			PRId64 "\n", pid);
		return 0;
	}
	const char *start_string;
	char start_string_buf[32];
	if (!start) {
		/* message is not from netlink-notifier. and we have to compensate */
		struct timeval tv = { .tv_sec = 0, .tv_usec = 0 };
		get_timeval_from_tick(start_tick, &tv);
		snprintf(start_string_buf, sizeof(start_string_buf),"%lu.%06lu",
			tv.tv_sec, tv.tv_usec);
		start_string = start_string_buf;
	} else {
		start_string = json_value_cstr(start);
	}
	const char *exe_string;
	char exe_buf[CMDLINE_SZ];
	if (!exe) {
		proc_exe_buf(pid, exe_buf, sizeof(exe_buf));
		exe_string = exe_buf;
	} else {
		exe_string = json_value_cstr(exe);
	}
	int64_t task_rank_val = -1;
	task_rank = get_field(inst, data, JSON_INT_VALUE, "task_global_id");
	if (task_rank) {
		task_rank_val = json_value_int(task_rank);
	}
/* set instance is $iprefix/$producer/$jobid/$start_time/$os_pid
 * unless it came from spank with task_global_id set, in which case it is.
 * set instance is $iprefix/$producer/$jobid/$start_time/rank/$task_rank
 */
	const char *esep = "";
	const char *esuffix = "";
	if (inst->exe_suffix) {
		esep = "/";
		esuffix = exe_string;
	}
	if (task_rank_val < 0) {
		/* we haven't seen the pid as a slurm item yet. */
		/* set instance is $iprefix/$producer/$jobid/$start_time/$os_pid */
		len = snprintf(setname, sizeof(setname), "%s%s%s/%" PRIu64
							"/%s/%" PRId64 "%s%s" ,
				(inst->instance_prefix ? inst->instance_prefix : ""),
				(inst->instance_prefix ? "/" : ""),
				inst->base_data->producer_name,
				job_id_val,
				start_string,
				(int64_t)pid, esep, esuffix);
		if (len >= sizeof(setname)) {
			INST_LOG(inst, LDMSD_LERROR, "set name too big: %s%s%s/%" PRIu64
							"/%s/%" PRId64 "%s%s\n",
				(inst->instance_prefix ? inst->instance_prefix : ""),
				(inst->instance_prefix ? "/" : ""),
				inst->base_data->producer_name,
				job_id_val,
				start_string,
				(int64_t)pid, esep, esuffix);
			return ENAMETOOLONG;
		}
	} else {
		/* set instance is $iprefix/$producer/$jobid/$start_time/rank/$task_rank */
		len = snprintf(setname, sizeof(setname), "%s%s%s/%" PRIu64
							"/%s/rank/%" PRId64 "%s%s",
				(inst->instance_prefix ? inst->instance_prefix : ""),
				(inst->instance_prefix ? "/" : ""),
				inst->base_data->producer_name,
				job_id_val,
				start_string,
				task_rank_val, esep, esuffix);
		if (len >= sizeof(setname)) {
			INST_LOG(inst, LDMSD_LERROR, "set name too big: %s%s%s/%" PRIu64
							"/%s/rank/%" PRId64 "%s%s\n",
				(inst->instance_prefix ? inst->instance_prefix : ""),
				(inst->instance_prefix ? "/" : ""),
				inst->base_data->producer_name,
				job_id_val,
				start_string,
				task_rank_val, esep, esuffix);
			return ENAMETOOLONG;
		}
	}
	app_set = calloc(1, sizeof(*app_set));
	if (!app_set)
		return ENOMEM;
	app_set->task_rank = task_rank_val;
	data_set_key(inst, app_set, start_tick, pid);

	set = ldms_set_new(setname, inst->base_data->schema);
	static int warn_once;
	static int warn_once_dup;
	if (!set) {
		int ec = errno;
		if (ec != EEXIST && !warn_once) {
			warn_once = 1;
			INST_LOG(inst, LDMSD_LWARNING, "Out of set memory. Consider bigger -m.\n");
			struct mm_stat ms;
			mm_stats(&ms);
			INST_LOG(inst, LDMSD_LWARNING, "mm_stat: size=%zu grain=%zu "
				"chunks_free=%zu grains_free=%zu grains_largest=%zu "
				"grains_smallest=%zu bytes_free=%zu bytes_largest=%zu "
				"bytes_smallest=%zu\n",
				ms.size, ms.grain, ms.chunks, ms.bytes, ms.largest,
				ms.smallest, ms.grain*ms.bytes, ms.grain*ms.largest,
				ms.grain*ms.smallest);
		}
		if (ec == EEXIST) {
			if (!warn_once_dup) {
				warn_once_dup = 1;
				INST_LOG(inst, LDMSD_LERROR, "Duplicate set name %s."
					"Check for redundant notifiers running.\n",
					setname);
			}
			ec = 0; /* expected case for misconfigured notifiers */
		}
		app_set_destroy(inst, app_set);
		return ec;
	}
	ldms_set_producer_name_set(set, inst->base_data->producer_name);
	base_auth_set(&inst->base_data->auth, set);
	ldms_metric_set_u64(set, BASE_JOB_ID, job_id_val);
	ldms_metric_set_u64(set, BASE_COMPONENT_ID, inst->base_data->component_id);
	ldms_metric_set_s64(set, inst->task_rank_idx, task_rank_val);
	ldms_metric_array_set_str(set, inst->start_time_idx, start_string);
	ldms_metric_set_u64(set, inst->start_tick_idx, start_tick);
	ldms_metric_set_s64(set, inst->parent_pid_idx, parent);
	ldms_metric_set_u8(set, inst->is_thread_idx, is_thread_val ? 1 : 0);
	ldms_metric_array_set_str(set, inst->exe_idx, exe_string);
	if (inst->sc_clk_tck)
		ldms_metric_set_s64(set, inst->sc_clk_tck_idx, inst->sc_clk_tck);
	app_set->set = set;
	rbn_init(&app_set->rbn, (void*)&app_set->key);

	pthread_mutex_lock(&inst->mutex);
	struct rbn *already = rbt_find(&inst->set_rbt, &app_set->key);
	if (already) {
		struct linux_proc_sampler_set *old_app_set;
		old_app_set = container_of(already,
				struct linux_proc_sampler_set, rbn);
		if (app_set->task_rank != -1) { /* new is from spank */
			if (old_app_set->task_rank == app_set->task_rank) {
				/*skip spank duplicate */
#ifdef LPDEBUG
				INST_LOG(inst, LDMSD_LDEBUG, "Keeping slurm "
					"set %s, dropping duplicate %s\n",
					ldms_set_instance_name_get(old_app_set->set),
					ldms_set_instance_name_get(app_set->set));
#endif
				app_set_destroy(inst, app_set);
			} else {
				/* remove/replace set pointer in app_set with newly named set instance */
#ifdef LPDEBUG
				INST_LOG(inst, LDMSD_LDEBUG,
					"Converting set %s to %s\n",
					ldms_set_instance_name_get(old_app_set->set),
					ldms_set_instance_name_get(set));
#endif
				ldmsd_set_deregister(ldms_set_instance_name_get(old_app_set->set), SAMP);
				ldms_set_unpublish(old_app_set->set);
				ldms_set_delete(old_app_set->set);
				old_app_set->set = set;
				old_app_set->task_rank = task_rank_val;
				ldms_set_publish(set);
				ldmsd_set_register(set, SAMP);
			}
		} else {
			/* keep existing set whether slurm or not. */
			/* unreachable in principle if set_create maintains uniqueness */
#ifdef LPDEBUG
			INST_LOG(inst, LDMSD_LDEBUG, "Keeping existing set %s, dropping %s\n",
				ldms_set_instance_name_get(old_app_set->set),
				ldms_set_instance_name_get(app_set->set));
#endif
			app_set_destroy(inst, app_set);
		}
		pthread_mutex_unlock(&inst->mutex);
		return 0;
	}
	rbt_ins(&inst->set_rbt, &app_set->rbn);
#ifdef LPDEBUG
	INST_LOG(inst, LDMSD_LDEBUG, "Adding set %s\n",
		ldms_set_instance_name_get(app_set->set));
#endif
	ldms_set_publish(set);
	ldmsd_set_register(set, SAMP);
	if (inst->argv_msg) {
		rc = publish_argv_pid(inst, app_set, start_string, exe_string,
			is_thread_val, parent, job_id_val);
		if (rc) {
#ifdef LPDEBUG
			INST_LOG(inst, LDMSD_LDEBUG,
				"publish_argv_pid failed for %d %s\n",
				app_set->key.os_pid, STRERROR(rc) );
#endif
			app_set->dead = rc;
		}
#ifdef LPDEBUG
else {
			INST_LOG(inst, LDMSD_LDEBUG,
				"publish_argv_pid OK for %d\n",
				app_set->key.os_pid);
		}
#endif
	}
	if (inst->env_msg && !app_set->dead) {
		rc = publish_env_pid(inst, app_set, start_string, exe_string,
			is_thread_val, parent, job_id_val);
		if (rc) {
#ifdef LPDEBUG
			INST_LOG(inst, LDMSD_LDEBUG,
				"publish_env_pid failed for %d %s\n",
				app_set->key.os_pid, STRERROR(rc) );
#endif
			app_set->dead = rc;
		}
#ifdef LPDEBUG
else {
			INST_LOG(inst, LDMSD_LDEBUG,
				"publish_env_pid OK for %d\n",
				app_set->key.os_pid);
		}
#endif
	}
	if (inst->fd_msg && !app_set->dead) {
		rc = publish_fd_pid_first(inst, app_set, start_string,
			exe_string, is_thread_val, parent, job_id_val);
		app_set->fd_skip++;
		if (rc) {
			app_set->dead = rc;
		}
	}
	pthread_mutex_unlock(&inst->mutex);
	return 0;
dump:
	bjb = json_entity_dump(bjb, data);
	INST_LOG(inst, LDMSD_LDEBUG, "data was: %s\n", bjb->buf);
	jbuf_free(bjb);
	return EINVAL;
}

int __handle_task_exit(linux_proc_sampler_inst_t inst, json_entity_t data)
{
	struct rbn *rbn;
	struct linux_proc_sampler_set *app_set;
	json_entity_t os_pid;
	os_pid = get_field(inst, data, JSON_INT_VALUE, "os_pid");
	pid_t pid;
	if (!os_pid ) {
		/* from spank plugin */
		json_entity_t task_pid;
		task_pid = get_field(inst, data, JSON_INT_VALUE, "task_pid");
		pid = (pid_t)json_value_int(task_pid);
	} else {
		pid = (pid_t)json_value_int(os_pid);
	}
	uint64_t start_tick = get_start_tick(inst, data, pid);
	if (!start_tick)
		return EINVAL;
	struct linux_proc_sampler_set app_set_search;
	data_set_key(inst, &app_set_search, start_tick, pid);
	pthread_mutex_lock(&inst->mutex);
	rbn = rbt_find(&inst->set_rbt, (void*)&app_set_search.key);
	if (!rbn) {
		pthread_mutex_unlock(&inst->mutex);
		return 0; /* exit occuring of process we didn't catch the start of. */
	}
	rbt_del(&inst->set_rbt, rbn);
	app_set = container_of(rbn, struct linux_proc_sampler_set, rbn);
	publish_fd_pid(inst, app_set);
	pthread_mutex_unlock(&inst->mutex);
	app_set_destroy(inst, app_set);
	return 0;
}

static int __stream_cb(ldmsd_stream_client_t c, void *ctxt,
		ldmsd_stream_type_t stream_type,
		const char *msg, size_t msg_len, json_entity_t entity)
{
	int rc;
	linux_proc_sampler_inst_t inst = ctxt;
	json_entity_t event, data;
	const char *event_name;

	if (stream_type != LDMSD_STREAM_JSON) {
		INST_LOG(inst, LDMSD_LDEBUG, "Unexpected stream type data...ignoring\n");
		INST_LOG(inst, LDMSD_LDEBUG, "%s\n", msg);
		rc = EINVAL;
		goto err;
	}

	event = get_field(inst, entity, JSON_STRING_VALUE, "event");
	if (!event) {
		rc = ENOENT;
		goto err;
	}
	event_name = json_value_cstr(event);
	data = json_value_find(entity, "data");
	if (!data) {
		INST_LOG(inst, LDMSD_LERROR,
			 "'%s' event is missing the 'data' attribute\n",
			 event_name);
		rc = ENOENT;
		goto err;
	}
	if (0 == strcmp(event_name, "task_init_priv")) {
		rc = __handle_task_init(inst, data);
		if (rc) {
			INST_LOG(inst, LDMSD_LERROR,
				"failed to process task_init: %d: %s\n",
				rc, STRERROR(rc));
			goto err;
		}
	} else if (0 == strcmp(event_name, "task_exit")) {
		rc = __handle_task_exit(inst, data);
		if (rc) {
			INST_LOG(inst, LDMSD_LERROR,
				"failed to process task_exit: %d: %s\n",
				rc, STRERROR(rc));
			goto err;
		}
	}
	rc = 0;
	goto out;
 err:
#ifdef LPDEBUG
	INST_LOG(inst, LDMSD_LDEBUG, "Doing nothing with msg %s.\n",
		msg);
#endif
 out:
	return rc;
}

static void linux_proc_sampler_term(struct ldmsd_plugin *pi);

static int
linux_proc_sampler_config(struct ldmsd_plugin *pi, struct attr_value_list *kwl,
					    struct attr_value_list *avl)
{
	linux_proc_sampler_inst_t inst = (void*)pi;
	int i, rc;
	linux_proc_sampler_metric_info_t minfo;
	char *val;

	if (inst->base_data) {
		/* already configured */
		INST_LOG(inst, LDMSD_LERROR, "already configured.\n");
		return EALREADY;
	}

	inst->base_data = base_config(avl, SAMP, SAMP, inst->log);
	if (!inst->base_data) {
		/* base_config() already log error message */
		return errno;
	}
	INST_LOG(inst, LDMSD_LDEBUG, "configuring.\n");

	/* Plugin-specific config here */
	val = av_value(avl, "cfg_file");
	if (val) {
		rc = __handle_cfg_file(inst, val);
		if (rc)
			goto err;
	} else {
		val = av_value(avl, "instance_prefix");
		if (val) {
			inst->instance_prefix = strdup(val);
			if (!inst->instance_prefix) {
				INST_LOG(inst, LDMSD_LERROR, "Config out of"
					" memory for instance_prefix\n");
				rc = ENOMEM;
				goto err;
			}
		}
		val = av_value(avl, "argv_sep");
		if (val) {
			inst->argv_sep = strdup(val);
			if (!inst->argv_sep) {
				INST_LOG(inst, LDMSD_LERROR,
					"Config out of memory for arg_sep\n");
				rc = ENOMEM;
				goto err;
			}
			if (check_sep(inst, inst->argv_sep)) {
				rc = ERANGE;
				INST_LOG(inst, LDMSD_LERROR,
					"Config argv_sep='%s' not supported.\n",
					inst->argv_sep );
				goto err;
			}
		}
		val = av_value(avl, "argv_fmt");
		if (val) {
			int dval = 2;
			int cnt;
			cnt = sscanf(val, "%d", &dval);
			if (cnt != 1) {
				INST_LOG(inst, LDMSD_LERROR,
					"argv_fmt='%s' not integer.\n", val);
				rc = EINVAL;
				goto err;
			} else {
				inst->argv_fmt = dval;
			}
		}
		val = av_value(avl, "fd_msg");
		if (val) {
			int dval = 1;
			int cnt;
			cnt = sscanf(val, "%d", &dval);
			if (cnt != 1) {
				INST_LOG(inst, LDMSD_LERROR,
					"fd_msg='%s' not integer.\n", val);
				rc = EINVAL;
				goto err;
			} else {
				inst->fd_msg = dval;
				val = av_value(avl, "fd_exclude");
				if (val) {
					char *fregex = file_to_regex(inst, val);
					if (!fregex) {
						INST_LOG(inst, LDMSD_LERROR,
							"fd_exclude= failed.\n");
						rc = ENOMEM;
						goto err;
					}
					rc = init_regex_filter(inst, fregex,
						"fd", &inst->fd_use_regex, &inst->fd_regex);
					free(fregex);
					if (rc)
						goto err;
				}
			}
		}
		val = av_value(avl, "argv_msg");
		if (val) {
			inst->argv_msg = true;
		}
		val = av_value(avl, "env_msg");
		if (val) {
			inst->env_msg = true;
			val = av_value(avl, "env_exclude");
			if (val) {
				char *eregex = file_to_regex(inst, val);
				if (!eregex) {
					INST_LOG(inst, LDMSD_LERROR,
						"env_exclude= failed.\n");
					rc = ENOMEM;
					goto err;
				}
				rc = init_regex_filter(inst, eregex,
					"env", &inst->env_use_regex, &inst->env_regex);
				free(eregex);
				if (rc)
					goto err;
			}
		}
		val = av_value(avl, "exe_suffix");
		if (val) {
			inst->exe_suffix = true;
		}
		val = av_value(avl, "sc_clk_tck");
		if (val) {
			inst->sc_clk_tck = sysconf(_SC_CLK_TCK);
		}
		val = av_value(avl, "stream");
		if (val) {
			inst->stream_name = strdup(val);
			if (!inst->stream_name) {
				INST_LOG(inst, LDMSD_LERROR,
					"Config out of memory for stream\n");
				rc = ENOMEM;
				goto err;
			}
		}
		val = av_value(avl, "syscalls");
		if (val) {
			inst->syscalls = strdup(val);
			if (!inst->syscalls) {
				INST_LOG(inst, LDMSD_LERROR,
					"Config out of memory for stream\n");
				rc = ENOMEM;
				goto err;
			}
		}
		val = av_value(avl, "metrics");
		if (val) {
			char *tkn, *ptr;
			tkn = strtok_r(val, ",", &ptr);
			while (tkn) {
				minfo = find_metric_info_by_name(tkn);
				if (!minfo) {
					rc = ENOENT;
					missing_metric(inst, tkn);
					goto err;
				}
				inst->metric_idx[minfo->code] = -1;
				tkn = strtok_r(NULL, ",", &ptr);
			}
		} else {
			for (i = _APP_FIRST; i <= _APP_LAST; i++) {
				inst->metric_idx[i] = -1;
			}
		}
	}

	if (inst->argv_fmt < 1 || inst->argv_fmt > 2) {
		rc = ERANGE;
		INST_LOG(inst, LDMSD_LERROR, "Config argv_fmt='%d' unsupported.\n",
			inst->argv_fmt );
		goto err;
	}

	rc = load_syscall_names(inst);
	if (rc) {
		goto err;
	}
	/* default stream */
	if (!inst->stream_name) {
		inst->stream_name = strdup("slurm");
		if (!inst->stream_name) {
			INST_LOG(inst, LDMSD_LERROR,
				"Config: out of memory for default stream\n");
			rc = ENOMEM;
			goto err;
		}
	}
	if (inst->argv_msg) {
		inst->argv_stream = malloc(2 + strlen(inst->base_data->schema_name) + 5);
		if (!inst->argv_stream)  {
			INST_LOG(inst, LDMSD_LERROR,
				"Config: out of memory for argv stream\n");
			rc = ENOMEM;
			goto err;
		}
		sprintf(inst->argv_stream, "%s_argv", inst->base_data->schema_name);
		INST_LOG(inst, LDMSD_LDEBUG,"argv stream %s\n",
			inst->argv_stream);
	}
	if (inst->env_msg) {
		inst->env_stream = malloc(2 + strlen(inst->base_data->schema_name) + 4);
		if (!inst->env_stream)  {
			INST_LOG(inst, LDMSD_LERROR,
				"Config: out of memory for env stream\n");
			rc = ENOMEM;
			goto err;
		}
		sprintf(inst->env_stream, "%s_env", inst->base_data->schema_name);
		INST_LOG(inst, LDMSD_LDEBUG,"env stream %s\n",
			inst->env_stream);
	}
	if (inst->fd_msg) {
		inst->fd_stream = malloc(2 + strlen(inst->base_data->schema_name) + 6);
		if (!inst->fd_stream)  {
			INST_LOG(inst, LDMSD_LERROR,
				"Config: out of memory for fd stream\n");
			rc = ENOMEM;
			goto err;
		}
		sprintf(inst->fd_stream, "%s_files", inst->base_data->schema_name);
		INST_LOG(inst, LDMSD_LDEBUG,"fd stream %s, freq %d\n",
			inst->fd_stream, inst->fd_msg);
	}

	inst->n_fn = 0;
	for (i = _APP_FIRST; i <= _APP_LAST; i++) {
		if (inst->metric_idx[i] == 0)
			continue;
		if (inst->n_fn && inst->fn[inst->n_fn-1].fn == handler_info_tbl[i].fn)
			continue; /* already added */
		/* add the handler */
		inst->fn[inst->n_fn] = handler_info_tbl[i];
		inst->n_fn++;
	}

	/* create schema */
	if (!base_schema_new(inst->base_data)) {
		INST_LOG(inst, LDMSD_LERROR, "Out of memory making schema\n");
		rc = errno;
		goto err;
	}
	rc = linux_proc_sampler_update_schema(inst, inst->base_data->schema);
	if (rc)
		goto err;

	/* subscribe to the stream */
	inst->stream = ldmsd_stream_subscribe(inst->stream_name, __stream_cb, inst);
	if (!inst->stream) {
		INST_LOG(inst, LDMSD_LERROR,
			 "Error subcribing to stream `%s`: %d\n",
			 inst->stream_name, errno);
		rc = errno;
		goto err;
	}

	return 0;

 err:
	/* undo the config */
	linux_proc_sampler_term(pi);
	return rc;
}

static
void linux_proc_sampler_term(struct ldmsd_plugin *pi)
{
	ldmsd_log(LDMSD_LDEBUG, "terminating plugin linux_proc_sampler\n");
	linux_proc_sampler_inst_t inst = (void*)pi;
	struct rbn *rbn;
	struct linux_proc_sampler_set *app_set;

	if (inst->stream)
		ldmsd_stream_close(inst->stream);
	pthread_mutex_lock(&inst->mutex);
	while ((rbn = rbt_min(&inst->set_rbt))) {
		rbt_del(&inst->set_rbt, rbn);
		app_set = container_of(rbn, struct linux_proc_sampler_set, rbn);
		app_set_destroy(inst, app_set);
	}
	pthread_mutex_unlock(&inst->mutex);
	free(inst->instance_prefix);
	inst->instance_prefix = NULL;
	free(inst->stream_name);
	inst->stream_name = NULL;
	free(inst->argv_sep);
	inst->argv_sep = NULL;
	if (inst->base_data)
		base_del(inst->base_data);
	bzero(inst->fn, sizeof(inst->fn));
	inst->n_fn = 0;
	bzero(inst->metric_idx, sizeof(inst->metric_idx));
	int call;
	if (inst->name_syscall)
		for (call = 0; call <= inst->n_syscalls; call++)
			free(inst->name_syscall[call]);
	free(inst->name_syscall);
	free(inst->syscalls);
	inst->name_syscall = NULL;
	inst->n_syscalls = -1;
	free(inst->env_stream);
	free(inst->argv_stream);
	free(inst->fd_stream);
	inst->fd_stream = inst->env_stream = inst->argv_stream = NULL;
	char *tmp = inst->recycle_buf;
	inst->recycle_buf = NULL;
	inst->recycle_buf_sz = 0;
	if (inst->env_use_regex)
		regfree(&(inst->env_regex));
	if (inst->fd_use_regex)
		regfree(&(inst->fd_regex));
	free(tmp);
}

static int
idx_cmp_by_name(const void *a, const void *b)
{
	/* a, b are pointer to linux_proc_sampler_metric_info_t */
	const linux_proc_sampler_metric_info_t *_a = a, *_b = b;
	return strcmp((*_a)->name, (*_b)->name);
}

static int
idx_key_cmp_by_name(const void *key, const void *ent)
{
	const linux_proc_sampler_metric_info_t *_ent = ent;
	return strcmp(key, (*_ent)->name);
}

static inline linux_proc_sampler_metric_info_t
find_metric_info_by_name(const char *name)
{
	linux_proc_sampler_metric_info_t *minfo = bsearch(name, metric_info_idx_by_name,
			_APP_LAST + 1, sizeof(linux_proc_sampler_metric_info_t),
			idx_key_cmp_by_name);
	return minfo?(*minfo):(NULL);
}

static int
status_line_cmp(const void *a, const void *b)
{
	const struct status_line_handler_s *_a = a, *_b = (void*)b;
	return strcmp(_a->key, _b->key);
}

static int
status_line_key_cmp(const void *key, const void *ent)
{
	const struct status_line_handler_s *_ent = ent;
	return strcmp(key, _ent->key);
}

static status_line_handler_t
find_status_line_handler(const char *key)
{
	return bsearch(key, status_line_tbl, ARRAY_LEN(status_line_tbl),
			sizeof(status_line_tbl[0]), status_line_key_cmp);
}

static
struct linux_proc_sampler_inst_s __inst = {
	.samp = {
		.base = {
			.name = SAMP,
			.type = LDMSD_PLUGIN_SAMPLER,
			.term = linux_proc_sampler_term,
			.config = linux_proc_sampler_config,
			.usage = linux_proc_sampler_usage,
		},
		.sample = linux_proc_sampler_sample,
	},
	.n_syscalls = -1,
	.argv_fmt = 2,
	.log = ldmsd_log
};

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	__inst.log = pf;
	__inst.argv_sep = NULL;
	return &__inst.samp.base;
}

static
int set_rbn_cmp(void *tree_key, const void *key)
{
	const struct set_key *tk = tree_key;
	const struct set_key *k = key;
	if (tk->start_tick != k->start_tick)
		return (int64_t)tk->start_tick - (int64_t)k->start_tick;
	return tk->os_pid - k->os_pid;
}

__attribute__((constructor))
static
void __init__()
{
	/* initialize metric_info_idx_by_name */
	int i;
	for (i = 0; i <= _APP_LAST; i++) {
		metric_info_idx_by_name[i] = &metric_info[i];
	}
	qsort(metric_info_idx_by_name, _APP_LAST + 1,
		sizeof(metric_info_idx_by_name[0]), idx_cmp_by_name);
	qsort(status_line_tbl, ARRAY_LEN(status_line_tbl),
			sizeof(status_line_tbl[0]), status_line_cmp);
	rbt_init(&__inst.set_rbt, set_rbn_cmp);
}

__attribute__((destructor))
static
void __destroy__()
{
	if (help_all != _help)
		free(help_all);
	help_all = NULL;
}
