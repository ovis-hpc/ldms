/*
 * Portions of this code  as marked are
 * Copyright (c) 2021 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2021 Open Grid Computing, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 *
 * Notes:
 * Much of this code is copied from forkstat by Colin Ian King.

 * This code filters netlink messages into notices for LDMS delivery.
 * Filtering on path names accounts for the fact that the executable name
 * seen in /proc changes during the fork/exec cycle.
 * The format of the code follows forkstat.c so that comparison to
 * updates of forkstat.c is kept simple.
 */

/*
 * Copyright (C) 2014-2018 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Written by Colin Ian King <colin.king@canonical.com>
 *
 * Some of this code originally derived from eventstat and powerstat
 * also by the same author.
 *
 */
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <libgen.h>
#include <inttypes.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>
#include <time.h>
#include <getopt.h>
#include <sched.h>
#include <pwd.h>

#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <linux/version.h>
#include <linux/connector.h>
#include <linux/netlink.h>
#include <linux/cn_proc.h>

#include <assert.h>
#include <pthread.h>
#include <math.h>
#include <stddef.h>
#include <sys/queue.h>
#include "simple_lps.h"

FILE *debug_log;
#define DEBUG_EMITTER 0
/* set 1 to trace which path emits the message by adding emitter field to messages */

/* provide thread-safe io macros */
//#define debug_err_lock 1
#ifdef debug_err_lock
#define EPRINTF(...) do { \
	fprintf(debug_log, "  locking err at %s:%d\n",__FILE__,__LINE__); \
	pthread_mutex_lock(&err_lock); \
	fprintf(debug_log, __VA_ARGS__); \
	pthread_mutex_unlock(&err_lock); \
	fprintf(debug_log, "unlocking err\n"); \
} while (0)
#else
#define EPRINTF(...) do { \
	pthread_mutex_lock(&log_lock); \
	fprintf(stderr, __VA_ARGS__); \
	pthread_mutex_unlock(&log_lock); \
} while (0)
#endif

//#define debug_log_lock 1
#ifdef debug_log_lock
#define PRINTF(...) do { \
	fprintf(debug_log, "  locking log at %s:%d\n",__FILE__,__LINE__); \
	pthread_mutex_lock(&log_lock); \
	fprintf(debug_log, __VA_ARGS__); \
	pthread_mutex_unlock(&log_lock); \
	fprintf(debug_log, "unlocking log\n"); \
} while (0)
#else
#define PRINTF(...) do { \
	pthread_mutex_lock(&log_lock); \
	fprintf(debug_log, __VA_ARGS__); \
	pthread_mutex_unlock(&log_lock); \
} while (0)
#endif

//#define debug_lock 1
#ifdef debug_lock
#define LPRINTF PRINTF
#else
#define LPRINTF(...)
#endif

#define VERSION "0.02.16" /* from forkstat base */
#define APP_NAME		"netlink-notifier"
/* make max_ids tunable */
#define MAX_PIDS		(ft->max_pids)	/* Hash Max PIDs */
#define MAX_UIDS		(1237)		/* Hash Max UIDs */
#define MAX_TTYS		(199)		/* Hash Max TTYs */

#define TTY_NAME_LEN		(16)		/* Max TTY name length */

#define NULL_STEP_ID		"-1"
#define NULL_COUNT		"0"
#define NULL_PID		(pid_t)(-1)
#define NULL_UID		(uid_t)(-1)
#define NULL_GID		(gid_t)(-1)
#define NULL_TTY		(dev_t)(-1)
/* add hpc support */
#define NULL_JOBID		(-1)


#define OPT_CMD_LONG		(0x00000001)	/* Long command line info */
#define OPT_CMD_SHORT		(0x00000002)	/* Short command line info */
#define OPT_CMD_DIRNAME_STRIP	(0x00000004)	/* Strip dirpath from command */
/* stats feature removed; redundant with LDMS samplers */
#define OPT_QUIET		(0x00000010)	/* Run quietly */
#define OPT_REALTIME		(0x00000020)	/* Run with Real Time scheduling */
#define OPT_EXTRA		(0x00000040)	/* Show extra stats */
#define OPT_GLYPH		(0x00000080)	/* Show glyphs */
#define OPT_COMM		(0x00000100)	/* Show comm info */
#define OPT_UMIN		(0x00000200)	/* Show uids below umin */

#define OPT_EV_FORK		(0x00100000)	/* Fork event */
#define OPT_EV_EXEC		(0x00200000)	/* Exec event */
#define OPT_EV_EXIT		(0x00400000)	/* Exit event */
#define OPT_EV_CORE		(0x00800000)	/* Coredump event */
#define OPT_EV_COMM		(0x01000000)	/* Comm proc info event */
#define OPT_EV_CLNE		(0x02000000)	/* Clone event */
#define OPT_EV_PTRC		(0x04000000)	/* Ptrace event */
#define OPT_EV_UID		(0x08000000)	/* UID event */
#define OPT_EV_SID		(0x10000000)	/* SID event */
#define OPT_EV_MASK		(0x1ff00000)	/* Event mask */
#define OPT_EV_ALL		(OPT_EV_MASK)	/* All events */

#define	GOT_TGID		(0x01)
#define GOT_PPID		(0x02)
#define GOT_ALL			(GOT_TGID | GOT_PPID)

#ifndef LINUX_VERSION_CODE
#define LINUX_VERSION_CODE KERNEL_VERSION(2,0,0)
#endif

#define UNUSED_EVENTS 0 /* suppress most handling of events not important to ldms */

/* resource manager to support in event generation. */
enum rm_type {
	RM_UNKNOWN, /* we haven't looked it up yet */
	RM_SLURM, /* found SLURM_JOB_ID */
	RM_LSF, /* found LSB_JOBID */
	RM_NONE, /* found no known scheduler */
	RM_END = RM_NONE
};

const char *rm_names[] = {
	"none",
	"slurm",
	"lsf",
	/* insert additional here when supported. */
	NULL
};

/* /proc info cache */
typedef struct proc_info {
	struct proc_info *next;	/* next proc info in hashed linked list */
	char	*cmdline;	/* /proc/pid/cmdline text */
	dev_t	tty;		/* TTY dev */
	pid_t	pid;		/* Process ID */
	uid_t	uid;		/* User ID */
	gid_t	gid;		/* GUID */
	uid_t	euid;		/* EUID */
	struct timeval start;	/* epoch time when process started */
	uint64_t start_tick;	/* local tick when process started */
	bool	kernel_thread;	/* true if a kernel thread */
	char	*exe;		/* /proc/pid/exe link content */
	int64_t serno;
	enum rm_type rm_type;	/* resource manager type. */
	char **pidenv;		/* environment pointers, when jobid has been detectd. */
	char *jobid;		/* resource manager jobid, if present in env, or null if not */

	bool	is_thread;	/* true if a application thread */
	bool    excluded;	/* true if matches excluded path lists or uid bound */
	bool    excluded_short;	/* true if matches excluded_short path list */
	int	emitted;	/* values from EMIT_* below */
} proc_info_t;

#define EMIT_NONE 0x0
#define EMIT_ADD 0x1
#define EMIT_EXIT 0x2

struct pi_list {
	struct proc_info *pi;
	pthread_mutex_t lock; /* lock on all elements for a given pid bucket */
};

/* For kernel task checking */
typedef struct {
	char *task;		/* Name of kernel task */
	size_t len;		/* Length */
} kernel_task_info;

/* For UID to name cache */
typedef struct uid_name_info {
	struct uid_name_info *next;
	char	*name;		/* User name */
	uid_t	uid;		/* User ID */
} uid_name_info_t;

typedef enum {
	STAT_FORK = 0,		/* Fork */
	STAT_EXEC,		/* Exec */
	STAT_EXIT,		/* Exit */
	STAT_CORE,		/* Core dump */
	STAT_COMM,		/* Proc comm field change */
	STAT_CLNE,		/* Clone */
	STAT_PTRC,		/* Ptrace */
	STAT_UID,		/* UID change */
	STAT_SID,		/* SID change */
	STAT_LAST		/* Always last sentinal */
} event_t;

typedef struct {
	const char *event;	/* Event name */
	const char *label;	/* Human readable label */
	const int flag;		/* option flag */
	const event_t stat;	/* stat enum */
} ev_map_t;

/* scaling factor */
typedef struct {
	const char ch;		/* Scaling suffix */
	const uint32_t base;	/* Base of part following . point */
	const uint64_t scale;	/* Amount to scale by */
} time_scale_t;

/* Mapping of event names to option flags and event_t types */
static const ev_map_t ev_map[] = {
	{ "fork", "Fork", 	OPT_EV_FORK,	STAT_FORK },
	{ "exec", "Exec", 	OPT_EV_EXEC,	STAT_EXEC },
	{ "exit", "Exit", 	OPT_EV_EXIT,	STAT_EXIT },
	{ "core", "Coredump",	OPT_EV_CORE,	STAT_CORE },
	{ "comm", "Comm", 	OPT_EV_COMM,	STAT_COMM },
	{ "clone","Clone",	OPT_EV_CLNE,	STAT_CLNE },
	{ "ptrce","Ptrace",	OPT_EV_PTRC,	STAT_PTRC },
	{ "uid",  "Uid",	OPT_EV_UID,	STAT_UID  },
	{ "sid",  "Sid",	OPT_EV_SID,	STAT_SID  },
	{ "all",  "",		OPT_EV_ALL,	0 },
	{ NULL,	  NULL, 	0,		0 }
};

#define KERN_TASK_INFO(str)	{ str, sizeof(str) - 1 }

/* keep most globals in a struct for easier debugging. */
typedef void (*print_f)(const char *fmt, ...);
typedef struct forkstat {
	bool stop_recv;				/* true if monitor exit wanted */
	bool sane_procs;			/* true if not inside a container */
	unsigned max_pids;			/* array size; >= INT_MAX if not ready */
	struct pi_list *proc_info;		/* Proc array w/locks*/
	struct pi_list *proc_info_head;	/* Proc array -1th ptr */
	uid_name_info_t *uid_name_info[MAX_UIDS]; /* UID to name hash table [maxuids] */
	unsigned int opt_flags;			/* Default option */
	unsigned opt_uidmin;			/* uids < uidmin omit if uids collected */
	double opt_duration_min;		/* min seconds a proc must run before being notified about */
	unsigned opt_trace;			/* print event processing details */
	FILE *json_log;				/* where to send json debug output, and if to send */
	struct slps *ln;			/* ldms notification channel */
	print_f log;
	print_f print;
	uint64_t msg_serno;
	double opt_wake_interval;
} forkstat_t;


static char unknown[] = "<unknown>";
#ifdef debug_err_lock
static pthread_mutex_t err_lock = PTHREAD_MUTEX_INITIALIZER;
#endif
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t stop_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t serial_lock = PTHREAD_MUTEX_INITIALIZER;

#define THREADED 1
#define UNTHREADED 1
static proc_info_t *proc_info_add(const pid_t pid, const struct timeval * const tv, int threaded, forkstat_t *ft, uint64_t tick);

/* Default void no process info struct */
static proc_info_t no_info = {
	.pid = NULL_PID,
	.uid = NULL_UID,
	.gid = NULL_GID,
	.cmdline = unknown,
	.kernel_thread = false,
	.start = { 0, 0 },
	.next = NULL,
	.euid = NULL_UID,
	.jobid = NULL,
};

/*
 *  Attempt to catch a range of signals so
 *  we can clean
 */
static const int signals[] = {
	/* POSIX.1-1990 */
#ifdef SIGALRM
	SIGALRM,
#endif
#ifdef SIGHUP
	SIGHUP,
#endif
#ifdef SIGINT
	SIGINT,
#endif
#ifdef SIGQUIT
	SIGQUIT,
#endif
#ifdef SIGFPE
	SIGFPE,
#endif
#ifdef SIGTERM
	SIGTERM,
#endif
#ifdef SIGUSR1
	SIGUSR1,
#endif
#ifdef SIGUSR2
	SIGUSR2,
	/* POSIX.1-2001 */
#endif
#ifdef SIGXCPU
	SIGXCPU,
#endif
#ifdef SIGXFSZ
	SIGXFSZ,
#endif
	/* Linux various */
#ifdef SIGIOT
	SIGIOT,
#endif
#ifdef SIGSTKFLT
	SIGSTKFLT,
#endif
#ifdef SIGPWR
	SIGPWR,
#endif
#ifdef SIGINFO
	SIGINFO,
#endif
#ifdef SIGVTALRM
	SIGVTALRM,
#endif
	-1,
};

/* add several structures related to filtering. */
#define default_send_log NULL
#define default_exclude_programs "(nullexe):<unknown>"
#define default_exclude_dirs "/sbin"
#define default_short_path "/bin:/usr"
#define default_short_time "1"

struct path_name {
	char *n; /* single file or dir name with trailing / */
	size_t s; /* strlen of n */
	uint64_t hits;	/*< number of matches excluded with this name */
};

struct path_elt {
	const char *pval;
	int ncolon;
	char *words;
	LIST_ENTRY(path_elt) entry;
};


enum valtype {
	VT_NONE,
	VT_FILE,
	VT_DIR,
	VT_SCALAR
};

LIST_HEAD(path_list, path_elt);
struct exclude_arg {
	const char *defval;	/* default */
	int valtype;
	int parsed;		/* seen in options */
	const char *env;	/* env var name */
	struct path_name *paths;/* sorted, normalized match names */
	size_t len_paths;	/* number of elements in paths */
	struct path_list path_list;	/* elements parsed from opts, env, or defaults */
};
static struct exclude_arg *bin_exclude;
static struct exclude_arg *dir_exclude;
static struct exclude_arg *short_dir_exclude;
static struct exclude_arg *duration_exclude;

/* but jobid is handled as a special case because of RM detection. */
struct env_attr {
	char *attr;
	char *env;
	char *v_default;
	int quoted;
};

static void path_elt_destroy(struct path_elt *p)
{
	if (!p)
		return;
	free(p->words);
	free(p);
}

static void reset_excludes(struct exclude_arg *e)
{
	if (!e)
		return;
	size_t k;
	for (k = 0; k < e->len_paths; k++) {
		free(e->paths[k].n);
	}
	free(e->paths);
	e->len_paths = 0;
	while (!LIST_EMPTY(&e->path_list)) {
		struct path_elt *pe = LIST_FIRST(&e->path_list);
		LIST_REMOVE(pe, entry);
		path_elt_destroy(pe);
	}
}

static int count_colon(const char *s)
{
	int c = 0;
	const char *colon = s;
	colon = strchr(s, ':');
	while (colon) {
		c++;
		colon = strchr(colon + 1, ':');
	}
	return c;
}

static int split_path(int *pos, struct exclude_arg *e, struct path_elt *pe)
{
	if (!pos || !e || !pe || !pe->pval) {
		return EINVAL;
	}
	pe->words = strdup(pe->pval);
	if (!pe->words)
		return ENOMEM;
	char *saveptr = NULL;
	char *m, *mnorm;
	int k;
	for (m = strtok_r(pe->words, ":", &saveptr);
	     m; m = strtok_r(NULL, ":", &saveptr)) {
		size_t mlen = strlen(m);
		if (e->valtype == VT_DIR && m[mlen - 1] != '/') {
			mnorm = malloc(mlen + 2);
			if (!mnorm) {
				return ENOMEM;
			}
			strcpy(mnorm, m);
			mnorm[mlen] = '/';
			mnorm[mlen + 1] = '\0';
		} else {
			mnorm = strdup(m);
			if (!mnorm) {
				return ENOMEM;
			}
		}
		int found = 0;
		for (k = 0; e->paths[k].n; k++) {
			if (strcmp(e->paths[k].n, mnorm) == 0) {
				found = 1;
				free(mnorm);
				break;
			}
		}
		if (!found) {
			e->paths[*pos].n = mnorm;
			e->paths[*pos].s = strlen(mnorm);
			(*pos)++;
		}
	}
	return 0;
}

static int cmp_str(const void *a, const void *b)
{
	const char **i = (const char **)a, **j = (const char **)b;
	return strcmp(*i, *j);
}

static int normalize_exclude(struct exclude_arg *e)
{
	if (!e)
		return EINVAL;
	int rc;
	struct path_elt *pe;
	int npath;
	switch (e->valtype) {
	case VT_SCALAR:
		LIST_FOREACH(pe, &e->path_list, entry) {
			if (pe && pe->pval) {
				e->paths = malloc(sizeof(struct path_name));
				if (!e->paths)
					return ENOMEM;
				e->paths[0].n = strdup(pe->pval);
				if (!e->paths[0].n)
					return ENOMEM;
				e->len_paths = 1;
				break;
			}
		}
		break;
	case VT_DIR:
	case VT_FILE:
		npath = 0;
		LIST_FOREACH(pe, &e->path_list, entry) {
			if (pe && pe->pval) {
				pe->ncolon = count_colon(pe->pval);
				npath += pe->ncolon + 1;
			}
		}
		e->paths = calloc(npath, sizeof(struct path_name));
		e->len_paths = npath;
		npath = 0;
		LIST_FOREACH(pe, &e->path_list, entry) {
			if (pe && pe->pval) {
				rc = split_path(&npath, e, pe);
				if (rc) {
					return rc;
				}
			}
		}
		e->len_paths = npath;
		qsort(e->paths, e->len_paths, sizeof(e->paths[0]), cmp_str);
		break;
	}
	return rc;
}

/* number of processes excluded because they were so short we
 * couldn't get a name. very common. */
static uint64_t excluded_gone;
static uint64_t excluded_uid;

static inline void override_emitted( proc_info_t const *info1, int b) { ((proc_info_t *)info1)->emitted = b; }
static inline void override_excluded( proc_info_t const *info1, bool b) { ((proc_info_t *)info1)->excluded = b; }

/* create forkstat token */
static forkstat_t *forkstat_create();

/*
 *   monitor()
 *	monitor system activity
 */
struct monitor_args {
	int sock;	/* netlink socket to follow. */
	int ret;	/* result */
	pthread_t tid;	/* thread of the monitoring; set in forkstat_monitor.*/
	forkstat_t *ft; /* data handle of the monitor from forkstat_create. */
};

/* after configuration, init ma and hook forkstat handle to kernel. */
static int forkstat_connect(forkstat_t *ft, struct monitor_args *ma);

/* after forkstat_connect, start thread tracking kernel data */
static int forkstat_monitor(forkstat_t *ft, struct monitor_args *ma);

/* free forkstat token not in use (after ma.tid is gone). */
static void forkstat_destroy(forkstat_t *ft);

struct dump_args {
	pthread_t tid;
	forkstat_t *ft;
}; // thread_args;

static int emit_info(forkstat_t *ft, struct proc_info *info, const char *type, int step, bool lock, jbuf_t *jbd);
static int forkstat_stop_requested(forkstat_t *ft);

#define BUFSZ 4096

static proc_info_t *proc_info_get(pid_t pid, forkstat_t *ft);
static void proc_info_release(const pid_t pid, forkstat_t *ft);

/* seconds scale suffixes, secs, mins, hours, etc */
static const time_scale_t second_scales[] = {
	{ 's',	1000, 1 },
	{ 'm',	 600, 60 },
	{ 'h',   600, 3600 },
	{ 'd',  1000, 24 * 3600 },
	{ 'w',  1000, 7 * 24 * 3600 },
	{ 'y',  1000, 365 * 24 * 3600 },
	{ ' ',  1000,  INT64_MAX },
};

/*
 *  proc_comm_dup()
 *	duplicate a comm filed string, if it fails, return unknown
 */
static char *proc_comm_dup(const char *str)
{
	char *comm = strdup(str);

	if (!comm)
		return unknown;

	return comm;
}

/*
 *  get_proc_self_stat_field()
 *	find nth field of /proc/$PID/stat data. This works around
 *	the problem that the comm field can contain spaces and
 *	multiple ) so sscanf on this field won't work.  The returned
 *	pointer is the start of the Nth field and it is up to the
 *	caller to determine the end of the field
 */
static const char *get_proc_self_stat_field(const char *buf, const int num)
{
	const char *ptr = buf, *comm_end;
	int n;

	if (num < 1 || !buf || !*buf)
		return NULL;
	if (num == 1)
		return buf;
	if (num == 2)
		return strstr(buf, "(");

	comm_end = NULL;
	for (ptr = buf; *ptr; ptr++) {
		if (*ptr == ')')
			comm_end = ptr;
	}
	if (!comm_end)
		return NULL;
	comm_end++;
	n = num - 2;

	ptr = comm_end;
	while (*ptr) {
		while (*ptr && *ptr == ' ')
			ptr++;
		n--;
		if (n <= 0)
			break;
		while (*ptr && *ptr != ' ')
			ptr++;
	}

	return ptr;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,14)
/*
 *  secs_to_str()
 *	report seconds in different units. Wall time from start to end when not quiet.
 */
static char *secs_to_str(const double secs)
{
	static char buf[16];
	size_t i;
	double s = secs, fract;

	for (i = 0; i < 5; i++) {
		if (s <= second_scales[i + 1].scale)
			break;
	}
	s /= second_scales[i].scale;
	s += 0.0005;	/* Round up */
	fract = (s * second_scales[i].base) - (double)((int)s * second_scales[i].base);
	(void)snprintf(buf, sizeof(buf), "%3u.%3.3u%c",
		(unsigned int)s, (unsigned int)fract, second_scales[i].ch);
	return buf;
}
#endif

/*
 *  get_username()
 *	get username from a given user id in nonquiet mode.
 */
static char *get_username(const uid_t uid, forkstat_t *ft)
{
	struct passwd *pwd;
	static char buf[12];
	const size_t hash = uid % MAX_UIDS;
	uid_name_info_t *uni = ft->uid_name_info[hash];
	char *name;

	/*
	 *  Try and find it in cache first
	 */
	while (uni) {
		if (uni->uid == uid)
			return uni->name;
		uni = uni->next;
	}

	pwd = getpwuid(uid);
	if (pwd) {
		name = pwd->pw_name;
	} else {
		(void)snprintf(buf, sizeof(buf), "%d", uid);
		name = buf;
	}

	/*
	 *  Try and allocate a cached uid name mapping
	 *  but don't worry if we can't as we can look
	 *  it up if we run out of memory next time round
	 */
	int dummy = 0;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &dummy);
	uni = malloc(sizeof(*uni));
	if (!uni) {
		pthread_setcancelstate(dummy, &dummy);
		return name;
	}
	uni->name = strdup(name);
	if (!uni->name) {
		free(uni);
		pthread_setcancelstate(dummy, &dummy);
		return name;
	}
	uni->uid = uid;
	uni->next = ft->uid_name_info[hash];
	ft->uid_name_info[hash] = uni;
	pthread_setcancelstate(dummy, &dummy);

	return uni->name;
}

/*
 *  uid_name_info_free()
 *	free uid name info cache
 */
static void uid_name_info_free(forkstat_t *ft)
{
	size_t i;
	if (!ft || !ft->uid_name_info)
		return;

	int dummy = 0;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &dummy);
	for (i = 0; i < MAX_UIDS; i++) {
		uid_name_info_t *uni = ft->uid_name_info[i];

		while (uni) {
			uid_name_info_t *next = uni->next;

			free(uni->name);
			free(uni);
			uni = next;
		}
	}
	pthread_setcancelstate(dummy, &dummy);
}

/*
 *  proc_name_clean()
 *	clean up unwanted chars from process name
 */
static void proc_name_clean(char *buffer, const int len, forkstat_t *ft)
{
	char *ptr;

	/*
 	 *  Convert '\r' and '\n' into spaces
	 */
	for (ptr = buffer; *ptr && (ptr < buffer + len); ptr++) {
		if ((*ptr == '\r') || (*ptr =='\n'))
			*ptr = ' ';
	}
	/*
	 *  OPT_CMD_SHORT option we discard anything after a space
	 */
	if (ft->opt_flags & OPT_CMD_SHORT) {
		for (ptr = buffer; *ptr && (ptr < buffer + len); ptr++) {
			if (*ptr == ' ') {
				*ptr = '\0';
				break;
			}
		}
	}
}

/*
 *  proc_comm()
 *	get process name from comm field
 */
static char *proc_comm(const pid_t pid, forkstat_t *ft)
{
	int fd;
	ssize_t ret;
	char buffer[BUFSZ];

	(void)snprintf(buffer, sizeof(buffer), "/proc/%d/comm", pid);
	if ((fd = open(buffer, O_RDONLY)) < 0)
		return NULL;
	if ((ret = read(fd, buffer, sizeof(buffer) - 1)) <= 0) {
		(void)close(fd);
		return NULL;
	}
	(void)close(fd);
	buffer[ret - 1] = '\0';		/* remove trailing '\n' */
	proc_name_clean(buffer, ret, ft);

	return strdup(buffer);
}


/*
 *  get_extra()
 *	quick and dirty way to get UID, EUID and GID from a PID
 */
static void get_extra(const pid_t pid, proc_info_t * const info, forkstat_t *ft)
{
	FILE *fp;
	char path[PATH_MAX];
	char buffer[BUFSZ];
	struct stat buf;

	info->uid = NULL_UID;
	info->gid = NULL_GID;
	info->euid = NULL_UID;

	if (!(ft->opt_flags & OPT_EXTRA))
		return;

	(void)snprintf(path, sizeof(path), "/proc/%u/status", pid);
	fp = fopen(path, "r");
	if (!fp)
		return;

	(void)memset(buffer, 0, sizeof(buffer));
	while (fgets(buffer, sizeof(buffer) - 1, fp)) {
		int gid, uid, euid;

		if (!strncmp(buffer, "Uid:", 4)) {
			if (sscanf(buffer + 4, "%d %d", &uid, &euid) == 2) {
				info->uid = uid;
				info->euid = euid;
			}
		}
		if (!strncmp(buffer, "Gid:", 4)) {
			if (sscanf(buffer + 4, "%d", &gid) == 1) {
				info->gid = gid;
			}
		}
		if ((info->uid != NULL_UID) &&
		    (info->euid != NULL_UID) &&
		    (info->gid != NULL_GID))
			break;
	}
	(void)fclose(fp);

	(void)snprintf(path, sizeof(path), "/proc/%u/stat", pid);
	fp = fopen(path, "r");
	if (!fp)
		return;

	/*
	 *  Failed to parse /proc/$PID/status? then at least get
	 *  some info by stat'ing /proc/$PID/stat
	 */
	if ((info->uid == NULL_UID) || (info->gid == NULL_GID)) {
		if (fstat(fileno(fp), &buf) == 0) {
			info->uid = buf.st_uid;
			info->gid = buf.st_gid;
		}
	}

	(void)fclose(fp);
}

/*
 *  pid_max_digits()
 *	determine (or guess) maximum digits of pids
 */
static int pid_max_digits(void)
{
	static int max_digits;
	ssize_t n;
	int fd;
	const int default_digits = 6;
	const int min_digits = 5;
	char buf[32];

	if (max_digits)
		goto ret;

	max_digits = default_digits;
	fd = open("/proc/sys/kernel/pid_max", O_RDONLY);
	if (fd < 0)
		goto ret;
	n = read(fd, buf, sizeof(buf) - 1);
	(void)close(fd);
	if (n < 0)
		goto ret;

	buf[n] = '\0';
	max_digits = 0;
	while (buf[max_digits] >= '0' && buf[max_digits] <= '9')
		max_digits++;
	if (max_digits < min_digits)
		max_digits = min_digits;
ret:
	return max_digits;

}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,14)
/*
 *  get_parent_pid()
 *	get parent pid and set is_thread to true if process
 *	not forked but a newly created thread
 *
 * returns -1 if cannot get parent pid.
 */
static pid_t get_parent_pid(const pid_t pid, bool * const is_thread)
{
	FILE *fp;
	pid_t tgid = 0, ppid = 0;
	unsigned int got = 0;
	char path[PATH_MAX];
	char buffer[BUFSZ];

	*is_thread = false;
	(void)snprintf(path, sizeof(path), "/proc/%u/status", pid);
	fp = fopen(path, "r");
	if (!fp)
		return -1;

	while (((got & GOT_ALL) != GOT_ALL) &&
	       (fgets(buffer, sizeof(buffer), fp) != NULL)) {
		if (!strncmp(buffer, "Tgid:", 5)) {
			if (sscanf(buffer + 5, "%u", &tgid) == 1) {
				got |= GOT_TGID;
			} else {
				tgid = 0;
			}
		}
		if (!strncmp(buffer, "PPid:", 5)) {
			if (sscanf(buffer + 5, "%u", &ppid) == 1)
				got |= GOT_PPID;
			else
				ppid = -1;
		}
	}
	(void)fclose(fp);

	if ((got & GOT_ALL) == GOT_ALL) {
		/*  TGID and PID are not the same if it is a thread */
		if (tgid != pid) {
			/* In this case, the parent is the TGID */
			ppid = tgid;
			*is_thread = true;
		}
	} else {
		ppid = -1;
	}

	return ppid;
}
#endif

/*
 *  sane_proc_pid_info()
 *	detect if proc info mapping from /proc/timer_stats
 *	maps to proc pids OK. If we are in a container or
 *	we can't tell, return false.
 */
static bool sane_proc_pid_info(void)
{
	static const char pattern[] = "container=";
	FILE *fp;
	bool ret = true;
	const char *ptr = pattern;

	/* Fast check */
	if (access("/run/systemd/container", R_OK) == 0)
		return true;

	/* Privileged slower check */
	fp = fopen("/proc/1/environ", "r");
	if (!fp)
		return false;

	while (!feof(fp)) {
		int ch = getc(fp);

		if (*ptr == ch) {
			ptr++;
			/* Match? So we're inside a container */
			if (*ptr == '\0') {
				ret = false;
				break;
			}
		} else {
			/* No match, skip to end of var and restart scan */
			do {
				ch = getc(fp);
			} while ((ch != EOF) && (ch != '\0'));
			ptr = pattern;
		}
	}

	(void)fclose(fp);

	return ret;
}

/*
 *  pid_a_kernel_thread
 *	is a process a kernel thread?
 */
static bool pid_a_kernel_thread(const char * const task, const pid_t id, forkstat_t *ft)
{
	if (ft->sane_procs) {
		return getpgid(id) == 0;
	} else {
		/* In side a container, make a guess at kernel threads */
		int i;
		const pid_t pgid = getpgid(id);

		/* This fails for kernel threads inside a container */
		if (pgid >= 0)
			return pgid == 0;

		/*
		 * This is not exactly accurate, but if we can't look up
		 * a process then try and infer something from the comm field.
		 * Until we have better kernel support to map /proc/timer_stats
		 * pids to containerised pids this is the best we can do.
		 */
		static const kernel_task_info kernel_tasks[] = {
			KERN_TASK_INFO("swapper/"),
			KERN_TASK_INFO("kworker/"),
			KERN_TASK_INFO("ksoftirqd/"),
			KERN_TASK_INFO("watchdog/"),
			KERN_TASK_INFO("migration/"),
			KERN_TASK_INFO("irq/"),
			KERN_TASK_INFO("mmcqd/"),
			KERN_TASK_INFO("jbd2/"),
			KERN_TASK_INFO("kthreadd"),
			KERN_TASK_INFO("kthrotld"),
			KERN_TASK_INFO("kswapd"),
			KERN_TASK_INFO("ecryptfs-kthrea"),
			KERN_TASK_INFO("kauditd"),
			KERN_TASK_INFO("kblockd"),
			KERN_TASK_INFO("kcryptd"),
			KERN_TASK_INFO("kdevtmpfs"),
			KERN_TASK_INFO("khelper"),
			KERN_TASK_INFO("khubd"),
			KERN_TASK_INFO("khugepaged"),
			KERN_TASK_INFO("khungtaskd"),
			KERN_TASK_INFO("flush-"),
			KERN_TASK_INFO("bdi-default-"),
			{ NULL, 0 }
		};

		for (i = 0; kernel_tasks[i].task != NULL; i++) {
			if (strncmp(task, kernel_tasks[i].task, kernel_tasks[i].len) == 0)
				return true;
		}
	}
	return false;
}


/*
 *  print_heading()
 *	print heading to output
 */
static void print_heading(forkstat_t *ft)
{

	if (ft->opt_flags & OPT_QUIET)
		return;
	if (!ft->opt_trace)
		return;
	int pid_size;

	pid_size = pid_max_digits();

	PRINTF("Time     Event %*.*s %s%sInfo   Duration Process\n",
		pid_size, pid_size, "PID",
		(ft->opt_flags & OPT_EXTRA) ? "   UID    EUID TTY    " : "",
		(ft->opt_flags & OPT_GLYPH) ? " " : "");
}

/*
 *  timeval_to_double()
 *      convert timeval to seconds as a double
 */
static inline double timeval_to_double(const struct timeval * const tv)
{
	return (double)tv->tv_sec + ((double)tv->tv_usec / 1000000.0);
}

/*
 *   proc_info_get_timeval()
 *	get time when process started
 */
static void proc_info_get_timeval(const pid_t pid, struct timeval * const tv, uint64_t *start_tick)
{
	int fd;
	unsigned long long starttime = 0;
	unsigned long jiffies = 0;
	char path[PATH_MAX];
	char buffer[BUFSZ];
	const char *ptr = NULL;
	double uptime_secs = 0, secs = 0;
	struct timeval now;
	ssize_t n = 0;

	(void)snprintf(path, sizeof(path), "/proc/%d/stat", pid);

	memset(buffer, 0, BUFSZ);
	fd = open("/proc/uptime", O_RDONLY);
	if (fd < 0)
		return;
	n = read(fd, buffer, sizeof(buffer) - 1);
	if (n <= 0) {
		(void)close(fd);
		return;
	}
	buffer[n] = '\0';
	(void)close(fd);
	n = sscanf(buffer, "%lg", &uptime_secs);
	if (n != 1)
		return;
	if (uptime_secs < 0.0)
		return;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return;
	n = read(fd, buffer, sizeof(buffer) - 1);
	if (n <= 0) {
		(void)close(fd);
		return;
	}
	buffer[n] = '\0';
	(void)close(fd);

	ptr = get_proc_self_stat_field(buffer, 22);
	if (!ptr)
		return;
	n = sscanf(ptr, "%llu", &starttime);
	if (n != 1)
		return;
	*start_tick = starttime;

	errno = 0;
	jiffies = sysconf(_SC_CLK_TCK);
	if (errno)
		return;
	secs = uptime_secs - ((double)starttime / (double)jiffies);
	if (secs < 0.0)
		return;

	if (gettimeofday(&now, NULL) < 0)
		return;

	secs = timeval_to_double(&now) - secs;
	tv->tv_sec = secs;
	tv->tv_usec = (suseconds_t)secs % 1000000;
}


/*
 *  proc_info_hash()
 * 	hash on PID
 * As constructed, will always return pid unless the system pid limit raised after start.
 */
static inline size_t proc_info_hash(const pid_t pid, forkstat_t *ft)
{
	return pid % MAX_PIDS;
}

static int init_proc_info(forkstat_t *ft)
{
	/* global max_pids */
	FILE *f = fopen("/proc/sys/kernel/pid_max", "r");
	if (!f) {
		ft->proc_info = NULL;
		ft->max_pids = UINT_MAX;
		return ENOENT;
	}
	int cnt = fscanf(f, "%d", &ft->max_pids);
	fclose(f);
	if (cnt != 1) {
		ft->proc_info = NULL;
		ft->max_pids = UINT_MAX;
		return ENOMSG;
	}
	ft->max_pids++;
	ft->proc_info_head = calloc( (ft->max_pids + 1) , sizeof(struct pi_list));

	if (!ft->proc_info_head) {
		ft->proc_info = NULL;
		ft->max_pids = UINT_MAX;
		return ENOMEM;
	}
	ft->proc_info = ft->proc_info_head + 1;
	int j,k, e;
	j = ft->max_pids + 1;
	pthread_mutexattr_t mattr;
	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_ERRORCHECK);
	if (ft->opt_trace)
		PRINTF("INIT_PROC_INFO mutexes\n");
	for (k = 0; k < j; k++) {
		e = pthread_mutex_init(&(ft->proc_info_head[k].lock), &mattr);
		if (e) {
			PRINTF("unable to init mutex for pid %d\n", e);
			return e;
		}
	}
	pthread_mutexattr_destroy(&mattr);

	return 0;
}

static void free_proc_info(forkstat_t *ft)
{
	int dummy = 0;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &dummy);

	ft->proc_info = NULL;
	free(ft->proc_info_head);

	ft->max_pids = UINT_MAX;
	pthread_setcancelstate(dummy, &dummy);
}


/*
 *  proc_stats_free()
 *	free stats list
 * proc_stats subsystem removed.
 */


/*
 *  free_proc_comm()
 *	free comm field only if it's not the static unknown string
 */
static void free_proc_comm(char *comm)
{
	if (comm != unknown)
		free(comm);
}

/*
 *  proc_cmdline
 * 	get process's /proc/pid/cmdline
 */
static char *proc_cmdline(const pid_t pid, forkstat_t *ft)
{
	int fd;
	ssize_t ret;
	char buffer[BUFSZ];

	if (pid == 0)
		return proc_comm_dup("[swapper]");

	if (ft->opt_flags & OPT_COMM)
		return proc_comm(pid, ft);

	(void)snprintf(buffer, sizeof(buffer), "/proc/%d/cmdline", pid);
	if ((fd = open(buffer, O_RDONLY)) < 0)
		return proc_comm(pid, ft);

	(void)memset(buffer, 0, sizeof(buffer));
	if ((ret = read(fd, buffer, sizeof(buffer) - 1)) <= 0) {
		(void)close(fd);
		return proc_comm(pid, ft);
	}
	(void)close(fd);
	buffer[ret] = '\0';	/* Keeps coverity scan happy */

	/*
	 *  OPT_CMD_LONG option we get the full cmdline args
	 */
	if (ft->opt_flags & OPT_CMD_LONG) {
		char *ptr;

		for (ptr = buffer; ptr < buffer + ret; ptr++) {
			if (*ptr == '\0') {
				if (*(ptr + 1) == '\0')
					break;
				*ptr = ' ';
			}
		}
	}

	proc_name_clean(buffer, ret, ft);

	if (ft->opt_flags & OPT_CMD_DIRNAME_STRIP)
		return proc_comm_dup(basename(buffer));

	return proc_comm_dup(buffer);
}


/*
 *  proc_exe
 * 	get process's /proc/pid/exe link location
 */
static char *proc_exe(const pid_t pid)
{
	ssize_t ret;
	char buffer[BUFSZ];
	char path[32];
	snprintf(path, sizeof(path), "/proc/%d/exe", pid);

	ret = readlink(path, buffer, BUFSZ - 1);
	if (ret < 0)
		sprintf(buffer,"(nullexe)");
	else
		buffer[ret] = '\0';
	return strdup(buffer);
}


/* may need to be called multiple times to handle rename of exe in fork/exec. */
static void set_excludes(proc_info_t *info, int opt_trace, unsigned uidmin)
{
	if (!info)
		return;
	if (!info->exe) {
		excluded_gone++;
		return;
	}
	info->excluded = false;
	if (info->uid < uidmin) {
		if (opt_trace)
			PRINTF("exclude_uidmin: %d < %u %s\n", info->uid,
				uidmin, info->exe);
		info->excluded = true;
		excluded_uid++;
	}
	size_t i;
	for (i = 0; i < dir_exclude->len_paths; i++) {
		if (strncmp(dir_exclude->paths[i].n, info->exe,
			dir_exclude->paths[i].s) == 0) {
			info->excluded = true;
			dir_exclude->paths[i].hits++;
			if (opt_trace)
				PRINTF("exclude_dirs: %s matching %s\n",
					info->exe, dir_exclude->paths[i].n);
			break;
		}
	}
	for (i = 0; i < bin_exclude->len_paths; i++) {
		if (strcmp(bin_exclude->paths[i].n, info->exe) == 0) {
			info->excluded = true;
			bin_exclude->paths[i].hits++;
			if (opt_trace)
				PRINTF("exclude_bin: %s matching %s\n",
					info->exe, bin_exclude->paths[i].n);
			break;
		}
	}
	info->excluded_short = false;
	for (i = 0; i < short_dir_exclude->len_paths; i++) {
		if (strncmp(short_dir_exclude->paths[i].n, info->exe,
			short_dir_exclude->paths[i].s) == 0) {
			info->excluded_short = true;
			short_dir_exclude->paths[i].hits++;
			if (opt_trace)
				PRINTF("exclude_short: %d %s matching %s\n",
					info->pid, info->exe, short_dir_exclude->paths[i].n);
			break;
		}
	}
}



/*
 *  proc_info_get()
 *	get proc info on a given pid
 */
static proc_info_t *proc_info_get(const pid_t pid, forkstat_t *ft)
{
	const size_t i = proc_info_hash(pid, ft);
	LPRINTF("%s:%d: locking %zu\n", __func__ ,__LINE__, i);
	pthread_mutex_lock(&(ft->proc_info[i].lock));
	proc_info_t *info = ft->proc_info[i].pi;
	struct timeval tv;

	while (info) {
		if (info->pid == pid) {
			if (info->cmdline == unknown)
				info->cmdline = proc_cmdline(pid, ft);
			return info;
		}
		info = info->next;
	}

	/* Hrm, not already cached, so get new info */
	(void)memset(&tv, 0, sizeof(tv));
	uint64_t tick = 0;
	proc_info_get_timeval(pid, &tv, &tick);
	info = proc_info_add(pid, &tv, THREADED, ft, tick);
	if (!info)
		info = &no_info;

	return info;
}

static void proc_info_release(const pid_t pid, forkstat_t *ft) {
	const size_t i = proc_info_hash(pid, ft);
	LPRINTF("%s:%d: unlocking %zu\n", __func__ ,__LINE__, i);
	pthread_mutex_unlock(&(ft->proc_info[i].lock));
}

static void free_env(char **e);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,14)
/*
 *  proc_info_free()
 *	free cached process info and remove from hash table
 */
static void proc_info_free(const pid_t pid, forkstat_t *ft)
{
	const size_t i = proc_info_hash(pid, ft);
	int dummy = 0;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &dummy);
	LPRINTF("%s:%d: locking %zu\n", __func__ ,__LINE__, i);
	pthread_mutex_lock(&(ft->proc_info[i].lock));
	proc_info_t *info = ft->proc_info[i].pi;
	proc_info_t **ref = &(ft->proc_info[i].pi);

	while (info) {
		if (info->pid == pid) {
			*ref = info->next;
			info->pid = NULL_PID;
			info->uid = NULL_UID;
			info->gid = NULL_GID;
			info->euid = NULL_UID;
			free(info->exe);
			free_env(info->pidenv);
			info->pidenv = NULL;
			free(info->jobid);
			info->jobid = NULL;
			info->exe = NULL;
			free_proc_comm(info->cmdline);
			info->cmdline = NULL;
			free(info);
			goto out;
		}
		ref = &info->next;
		info = info->next;
	}
out:
	LPRINTF("%s:%d: unlocking %zu\n", __func__ ,__LINE__, i);
	pthread_mutex_unlock(&(ft->proc_info[i].lock));
	pthread_setcancelstate(dummy, &dummy);
}
#endif

/*
 *   proc_info_unload()
 *	free all hashed proc info entries
 */
static void proc_info_unload(forkstat_t *ft)
{
	size_t i;
	if (!ft)
		return;
	if (!ft->proc_info)
		return;

	int dummy = 0;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &dummy);
	for (i = 0; i < MAX_PIDS; i++) {
		proc_info_t *info = ft->proc_info[i].pi;

		while (info) {
			proc_info_t *next = info->next;
			free_proc_comm(info->cmdline);
			free(info->exe);
			info->pid = NULL_PID;
			info->uid = NULL_UID;
			info->gid = NULL_GID;
			free_env(info->pidenv);
			info->pidenv = NULL;
			free(info->jobid);
			info->jobid = NULL;
			free(info);
			info = next;
		}
	}
	pthread_setcancelstate(dummy, &dummy);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,14)
/*
 *  proc_info_update()
 *	update process name, for example, if exec has occurred
 */
static proc_info_t *proc_info_update(const pid_t pid, forkstat_t *ft)
{
	int dummy = 0;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &dummy);
	proc_info_t * const info = proc_info_get(pid, ft);
	char *newcmd, *newexe;

	if (info == &no_info) {
		goto out2;
	}
	newcmd = proc_cmdline(pid, ft);

	/*
	 *  Don't update if newcmd is unknown, at least
	 *  we temporarily keep the parent's name or
	 *  the processes old name
	 */
	if (!newcmd) {
		goto out2;
	}
	if (newcmd != unknown) {
		newexe = proc_exe(pid);
		free_proc_comm(info->cmdline);
		info->cmdline = newcmd;
		free(info->exe);
		info->exe = newexe;
		set_excludes(info, ft->opt_trace, ft->opt_uidmin);
	}

	proc_info_release(pid, ft);
	pthread_setcancelstate(dummy, &dummy);
	return info;

out2:
	proc_info_release(pid, ft);
	pthread_setcancelstate(dummy, &dummy);
	return &no_info;
}
#endif

/* local serial number (non-unique) with more bits than a pid.
 * It is unlikely that $host/$jobid/$proc_start_sec.usec/$pid will collide.
 * If we discover a many-core case where it can, use serno instead of pid.
 */
static int64_t serno;
static int64_t procinfo_get_serial();
static uint64_t forkstat_get_serial(forkstat_t *ft);

/*
 *   proc_info_add()
 *	add processes info of a given pid to the hash table
 */
static proc_info_t *proc_info_add(const pid_t pid, const struct timeval * const tv, int threaded, forkstat_t *ft, uint64_t tick)
{
	if (ft->opt_trace)
		PRINTF("proc_info_add %d\n", pid);
	const size_t i = proc_info_hash(pid, ft);
	proc_info_t *info;
	char *cmdline;
	int dummy = 0;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &dummy);

	cmdline = proc_cmdline(pid, ft);
	if (!cmdline) {
		pthread_setcancelstate(dummy, &dummy);
		return NULL;
	}
	char *newexe = proc_exe(pid);
	if (!newexe) {
		free(cmdline);
		pthread_setcancelstate(dummy, &dummy);
		return NULL;
	}

	if (threaded) {
		LPRINTF("%s:%d: locking %zu\n", __func__ ,__LINE__, i);
		pthread_mutex_lock(&(ft->proc_info[i].lock));
	}
	/* Re-use any info on the list if it's free */
	info = ft->proc_info[i].pi;
	while (info) {
		if (info->pid == NULL_PID)
			break;
		info = info->next;
	}

	if (!info) {
		info = calloc(1, sizeof(proc_info_t));
		if (!info) {
			EPRINTF("Cannot allocate all proc info\n");
			free_proc_comm(cmdline);
			if (threaded) {
				LPRINTF("%s:%d: unlocking %zu\n", __func__ ,__LINE__, i);
				pthread_mutex_unlock(&(ft->proc_info[i].lock));
			}
			pthread_setcancelstate(dummy, &dummy);
			return NULL;
		}
		info->next = ft->proc_info[i].pi;
		ft->proc_info[i].pi = info;
	}
	info->serno = procinfo_get_serial();
	info->cmdline = cmdline;
	info->exe = newexe;
	get_extra(pid, info, ft);
	info->pid = pid;
	info->kernel_thread = pid_a_kernel_thread(cmdline, pid, ft);
	set_excludes(info, ft->opt_trace, ft->opt_uidmin);
	info->start = *tv;
	info->start_tick = tick;
	if (threaded) {
		LPRINTF("%s:%d: unlocking %zu\n", __func__ ,__LINE__, i);
		pthread_mutex_unlock(&(ft->proc_info[i].lock));
	}

	pthread_setcancelstate(dummy, &dummy);
	return info;
}

/*
 *  proc_thread_info_add()
 *	Add a processes' thread into proc cache
 */
static void proc_thread_info_add(const pid_t pid, const struct timeval *const parent_tv, int threaded, forkstat_t *ft)
{
	DIR *dir;
	struct dirent *dirent;
	char path[PATH_MAX];

	(void)snprintf(path, sizeof(path), "/proc/%i/task", pid);

	dir = opendir(path);
	if (!dir)
		return;

	while ((dirent = readdir(dir))) {
		if (isdigit(dirent->d_name[0])) {
			pid_t tpid;
			struct timeval tv;

			errno = 0;
			tpid = (pid_t)strtol(dirent->d_name, NULL, 10);
			if ((!errno) && (tpid != pid)) {
				tv = *parent_tv;
				uint64_t tick = 0;
				proc_info_get_timeval(pid, &tv, &tick);
				(void)proc_info_add(tpid, &tv, threaded, ft, tick);
			}
		}
	}

	(void)closedir(dir);
}

/*
 *  proc_info_load()
 *	load up all current processes info into hash table
 */
static int proc_info_load(forkstat_t *ft)
{
	DIR *dir;
	struct dirent *dirent;

	dir = opendir("/proc");
	if (!dir)
		return -1;

	int rc = init_proc_info(ft);
	if (rc != 0) {
		EPRINTF( "init_proc_info fail %d\n", rc);
		return -2;
	}

	while ((dirent = readdir(dir))) {
		if (isdigit(dirent->d_name[0])) {
			pid_t pid;
			struct timeval tv;

			errno = 0;
			pid = (pid_t)strtol(dirent->d_name, NULL, 10);
			if (!errno) {
				(void)memset(&tv, 0, sizeof(tv));
				uint64_t tick = 0;
				proc_info_get_timeval(pid, &tv, &tick);
				(void)proc_info_add(pid, &tv, UNTHREADED, ft, tick);
				proc_thread_info_add(pid, &tv, UNTHREADED, ft);
			}
		}
	}

	(void)closedir(dir);
	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,14)
/*
 *  extra_info()
 *	fetch, cache, & format up extra process information if selected
 */
static char *extra_info(const pid_t pid, forkstat_t *ft, char *buf, size_t bufsiz)
{
	if (bufsiz < 28 || !buf) {
		return "bufsiz_error";
	}
	*buf = '\0';
	if (ft->opt_flags & OPT_EXTRA) {
		const proc_info_t *info = proc_info_get(pid, ft);
		proc_info_release(pid, ft);

		if (info && info->uid != NULL_UID)
			(void)snprintf(buf, bufsiz, "%7.7s %7.7s %6.6s ",
				get_username(info->uid, ft),
				get_username(info->euid, ft),
				"notty");
		else
			(void)snprintf(buf, bufsiz, "%14s", "");
	}

	return buf;
}
#endif

static void forkstat_stop(forkstat_t *ft, int sig);
static forkstat_t *shft; /* forkstat_t singleton as set by main(); */
/*
 *  handle_sig()
 *	catch signal and flag a stop
 */
static void handle_sig(int sig)
{
	forkstat_stop(shft, sig);
}

/*
 *  netlink_connect()
 *	connect to netlink socket
 */
static int netlink_connect(void)
{
	int sock;
	struct sockaddr_nl addr;

	if ((sock = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_CONNECTOR)) < 0) {
		if (errno == EPROTONOSUPPORT)
			return -EPROTONOSUPPORT;
		EPRINTF( "socket failed: errno=%d (%s)\n",
			errno, strerror(errno));
		return -1;
	}

	(void)memset(&addr, 0, sizeof(addr));
	addr.nl_pid = getpid();
	addr.nl_family = AF_NETLINK;
	addr.nl_groups = CN_IDX_PROC;

	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		EPRINTF( "bind failed: errno=%d (%s)\n",
			errno, strerror(errno));
		(void)close(sock);
		return -1;
	}

	return sock;
}

/*
 *  netlink_listen()
 *	proc connector listen
 */
static int netlink_listen(const int sock)
{
	enum proc_cn_mcast_op op;
	struct nlmsghdr nlmsghdr;
	struct cn_msg cn_msg;
	struct iovec iov[3];

	(void)memset(&nlmsghdr, 0, sizeof(nlmsghdr));
	nlmsghdr.nlmsg_len = NLMSG_LENGTH(sizeof(cn_msg) + sizeof(op));
	nlmsghdr.nlmsg_pid = getpid();
	nlmsghdr.nlmsg_type = NLMSG_DONE;
	iov[0].iov_base = &nlmsghdr;
	iov[0].iov_len = sizeof(nlmsghdr);

	(void)memset(&cn_msg, 0, sizeof(cn_msg));
	cn_msg.id.idx = CN_IDX_PROC;
	cn_msg.id.val = CN_VAL_PROC;
	cn_msg.len = sizeof(enum proc_cn_mcast_op);
	iov[1].iov_base = &cn_msg;
	iov[1].iov_len = sizeof(cn_msg);

	op = PROC_CN_MCAST_LISTEN;
	iov[2].iov_base = &op;
	iov[2].iov_len = sizeof(op);

	return writev(sock, iov, 3);
}


static int short_exceeded(forkstat_t *ft, struct proc_info *info, const struct timeval *now)
{
	if (info->start.tv_sec) {
		double d1, d2;
		d1 = timeval_to_double(&info->start);
		d2 = timeval_to_double(now);
		if ( (d2 - d1) > ft->opt_duration_min)
			return 1;
		else
			return 0;

	}
	override_excluded(info, true); // exclude timeless processes
	return 0;

}
/*
 *   monitor()
 *	monitor system activity
 */
static void *monitor(void *vp)
{
	struct monitor_args *arg = vp;
	forkstat_t *ft = arg->ft;
	struct nlmsghdr *nlmsghdr;
	const int pid_size = pid_max_digits();
	char eibuf[32]; // extra_info output space

	print_heading(ft);

	jbuf_t jbd = jbuf_new();
	jbuf_t jb = jbd;
	while (jb && !forkstat_stop_requested(ft)) {
		ssize_t len;
		char __attribute__ ((aligned(NLMSG_ALIGNTO)))buf[BUFSZ];

		if ((len = recv(arg->sock, buf, sizeof(buf), 0)) == 0) {
			arg->ret = 0;
			break;
		}

		if (len == -1) {
			const int err = errno;

			switch (err) {
			case EINTR:
				arg->ret = 0;
				return NULL;
			case ENOBUFS: {
				time_t now;
				struct tm tm;

				now = time(NULL);
				if (now == ((time_t) -1)) {
					PRINTF("--:--:-- recv ----- "
						"nobufs %8.8s (%s)\n",
						"", strerror(err));
				} else {
					(void)localtime_r(&now, &tm);
					PRINTF("%2.2d:%2.2d:%2.2d recv ----- "
						"nobufs %8.8s (%s)\n",
						tm.tm_hour, tm.tm_min, tm.tm_sec, "",
						strerror(err));
				}
				break;
			}
			default:
				EPRINTF("recv failed: errno=%d (%s)\n",
					err, strerror(err));
				arg->ret = -1;
				return NULL;
			}
		}

		for (nlmsghdr = (struct nlmsghdr *)buf;
			NLMSG_OK (nlmsghdr, len);
			nlmsghdr = NLMSG_NEXT (nlmsghdr, len)) {

			struct cn_msg *cn_msg;
			struct proc_event *proc_ev;
			struct tm tm;
			char when[10];
			time_t now;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,14)
			pid_t pid, ppid;
			bool is_thread;
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,14)
			struct timeval tv;
			struct timeval starttv;
			char duration[32];
			proc_info_t *info1, *info2;
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,1,0)
#if UNUSED_EVENTS
			char *comm;
#endif /* UNUSED_EVENTS */
#endif

			if ((nlmsghdr->nlmsg_type == NLMSG_ERROR) ||
			    (nlmsghdr->nlmsg_type == NLMSG_NOOP))
				continue;

			cn_msg = NLMSG_DATA(nlmsghdr);
			if ((cn_msg->id.idx != CN_IDX_PROC) ||
			    (cn_msg->id.val != CN_VAL_PROC))
				continue;

			proc_ev = (struct proc_event *)cn_msg->data;

			now = time(NULL);
			if (now == ((time_t) -1)) {
				(void)snprintf(when, sizeof(when), "--:--:--");
			} else {
				(void)localtime_r(&now, &tm);
				(void)snprintf(when, sizeof(when), "%2.2d:%2.2d:%2.2d",
					tm.tm_hour, tm.tm_min, tm.tm_sec);
			}

			switch (proc_ev->what) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,14)
			case PROC_EVENT_FORK:
				ppid = get_parent_pid(proc_ev->event_data.fork.child_pid, &is_thread);
				pid = proc_ev->event_data.fork.child_pid;
				if (gettimeofday(&tv, NULL) < 0)
					(void)memset(&tv, 0, sizeof tv);
				(void)memset(&starttv, 0, sizeof starttv);
				uint64_t tick = 0;
				proc_info_get_timeval(pid, &starttv, &tick);
				info1 = proc_info_get(ppid, ft);
				info2 = proc_info_add(pid, &tv, THREADED, ft, tick);
				proc_info_release(ppid, ft);
				if (!(ft->opt_flags & OPT_QUIET) &&
					(((ft->opt_flags & OPT_EV_FORK) && !is_thread) ||
					 ((ft->opt_flags & OPT_EV_CLNE) && is_thread))) {
					if (info1 != NULL && info2 != NULL) {
						if (ft->opt_flags & (OPT_UMIN | OPT_EXTRA)) {
							if (info2->uid < ft->opt_uidmin) {
								break;
							}
						}

						const char * const type = is_thread ? "clone" : "fork";
						if (is_thread)
							info2->is_thread = 1;
						if (ft->opt_trace)
							PRINTF("PROC_EVENT_FORK\n");

						if (ft->opt_trace) {
							extra_info(ppid,ft, eibuf, sizeof(eibuf));
							PRINTF("%s %-5.5s %*d %s%sparent %8s %s%s%s\n",
								when,
								type,
								pid_size, ppid,
								eibuf,
								// (ft->opt_flags & OPT_GLYPH) ? "\u252c" : "",
								(ft->opt_flags & OPT_GLYPH) ? "+" : "",
								"",
								info1->kernel_thread ? "[" : "",
								info1->exe,
								info1->kernel_thread ? "]" : "");
							extra_info(pid,ft, eibuf, sizeof(eibuf));
							PRINTF("%s %-5.5s %*d %s%s%6.6s %8s %s%s%s\n",
								when,
								type,
								pid_size, pid,
								eibuf,
								// (ft->opt_flags & OPT_GLYPH) ? "\u2514" : "",
								(ft->opt_flags & OPT_GLYPH) ? "L" : "",
								is_thread ? "thread" : "child ",
								"",
								info1->kernel_thread ? "[" : "",
								info2->exe,
								info1->kernel_thread ? "]" : "");
						}
						emit_info(ft, info2, type, EMIT_ADD, true, &jb);
						if (!jb)
							jbuf_free(jbd);
						jbd = jb;
					}
				}
				break;
			case PROC_EVENT_EXEC:
				if (ft->opt_trace)
					PRINTF("PROC_EVENT_EXEC\n");
				pid = proc_ev->event_data.exec.process_pid;
				info1 = proc_info_update(pid, ft);
				if (ft->opt_flags & (OPT_UMIN | OPT_EXTRA)) {
					if (info1->uid < ft->opt_uidmin) {
						if (ft->opt_trace)
							PRINTF("excluding uid %d\n", info1->uid);
						override_excluded(info1, true);
						break;
					}
				}
				if (info1->exe == NULL) {
					if (ft->opt_trace)
						PRINTF("excluding noexe process %d\n", pid);
					override_excluded(info1, true);
					break;
				}
				if (info1->excluded || info1->excluded_short) {
					if (ft->opt_trace)
						PRINTF("excluded %d %s\n", pid,
							info1->exe ? info1->exe : "null");
					break;
				}
				/* we will have to reinspect the list in some other thread to check for infos
				* unemitted but later exceeding short. */
				if (!(ft->opt_flags & OPT_QUIET) && (ft->opt_flags & OPT_EV_EXEC)) {
					extra_info(pid,ft, eibuf, sizeof(eibuf));
					if (ft->opt_trace) {
						PRINTF("%s exec  %*d %s%s       %8s %s%s%s %lu.%06lu\n",
							when,
							pid_size, pid,
							eibuf,
							// (ft->opt_flags & OPT_GLYPH) ? "\u2192" : "",
							(ft->opt_flags & OPT_GLYPH) ? ">" : "",
							"",
							(info1->kernel_thread ? "[" : ""),
							info1->exe,
							(info1->kernel_thread ? "]" : ""),
							info1->start.tv_sec,
							info1->start.tv_usec
							);
					}
					emit_info(ft, info1, "exec", EMIT_ADD, true, &jb);
					if (!jb)
						jbuf_free(jbd);
					jbd = jb;
				}
				break;
			case PROC_EVENT_EXIT:
				if (!(ft->opt_flags & OPT_QUIET) && (ft->opt_flags & OPT_EV_EXIT)) {
					pid = proc_ev->event_data.exit.process_pid;
					info1 = proc_info_get(pid, ft);
					proc_info_release(pid, ft);
					if (info1->start.tv_sec) {
						double d1, d2;

						if (gettimeofday(&tv, NULL) < 0) {
							(void)memset(&tv, 0, sizeof tv);
						}
						d1 = timeval_to_double(&info1->start);
						d2 = timeval_to_double(&tv);
						(void)snprintf(duration, sizeof(duration), "%8s", secs_to_str(d2 - d1));
					} else {
						(void)snprintf(duration, sizeof(duration), "unknown");
					}
					if (ft->opt_flags & (OPT_UMIN | OPT_EXTRA)) {
						if (info1->uid < ft->opt_uidmin) {
							goto case_exit_out;
						}
					}
					if (!info1->emitted)
						/* no exit notice for unemitted processes.
						Might add counter processing here eventually. */
						goto case_exit_out;
					if (ft->opt_trace) {
						PRINTF("PROC_EVENT_EXIT\n");
						extra_info(pid,ft, eibuf, sizeof(eibuf));
						PRINTF("%s exit  %*d %s%s%6d %8s %s%s%s\n",
							when,
							pid_size, pid,
							eibuf,
							// (ft->opt_flags & OPT_GLYPH) ? "\u21e5" : "",
							(ft->opt_flags & OPT_GLYPH) ? ">|" : "",
							proc_ev->event_data.exit.exit_code,
							duration,
							info1->kernel_thread ? "[" : "",
							info1->cmdline,
							info1->kernel_thread ? "]" : "");
					}
					emit_info(ft, info1, "exit",
						info1->emitted | EMIT_EXIT, true, &jb);
					if (!jb)
						jbuf_free(jbd);
					jbd = jb;
				}
			case_exit_out:
				proc_info_free(proc_ev->event_data.exit.process_pid, ft);
				break;
			case PROC_EVENT_UID:
				if (ft->opt_trace)
					PRINTF("PROC_EVENT_UID\n");
				info1 = proc_info_update(proc_ev->event_data.exec.process_pid, ft);
				if (!(ft->opt_flags & OPT_QUIET) && (ft->opt_flags & OPT_EV_UID)) {
					pid = proc_ev->event_data.exec.process_pid;
					if (proc_ev->what == PROC_EVENT_UID) {
						extra_info(pid,ft, eibuf, sizeof(eibuf));
						if (ft->opt_trace) {
							PRINTF("%s uid   %*d %s%s%6s %8s %s%s%s\n",
								when,
								pid_size, pid,
								eibuf,
								(ft->opt_flags & OPT_GLYPH) ? " " : "",
								get_username(proc_ev->event_data.id.e.euid, ft),
								"",
								info1->kernel_thread ? "[" : "",
								info1->cmdline,
								info1->kernel_thread ? "]" : "");
						}
					} else {
						extra_info(pid,ft, eibuf, sizeof(eibuf));
						if (ft->opt_trace) {
							PRINTF("%s gid   %*d %6s %s%8s %s%s%s\n",
								when,
								pid_size, pid,
								eibuf,
								get_username(proc_ev->event_data.id.e.euid, ft),
								"",
								info1->kernel_thread ? "[" : "",
								info1->cmdline,
								info1->kernel_thread ? "]" : "");
						}
					}
				}
				break;
			case PROC_EVENT_SID:
				if (ft->opt_trace)
					PRINTF("PROC_EVENT_SID\n");
				info1 = proc_info_update(proc_ev->event_data.exec.process_pid, ft);
				if (!(ft->opt_flags & OPT_QUIET) && (ft->opt_flags & OPT_EV_UID)) {
					if (ft->opt_trace) {
						pid = proc_ev->event_data.exec.process_pid;
						extra_info(pid,ft, eibuf, sizeof(eibuf));
						PRINTF("%s sid   %*d %s%s%6d %8s %s%s%s\n",
							when,
							pid_size, pid,
							eibuf,
							(ft->opt_flags & OPT_GLYPH) ? " " : "",
							proc_ev->event_data.sid.process_pid,
							"",
							info1->kernel_thread ? "[" : "",
							info1->cmdline,
							info1->kernel_thread ? "]" : "");
					}
				}
				break;
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
			case PROC_EVENT_COREDUMP:
				if (ft->opt_trace)
					PRINTF("PROC_EVENT_COREDUMP\n");
#if UNUSED_EVENTS
				if (!(ft->opt_flags & OPT_QUIET) && (ft->opt_flags & OPT_EV_CORE)) {
					pid = proc_ev->event_data.coredump.process_pid;
					info1 = proc_info_get(pid, ft);
					proc_info_release(pid, ft);
					extra_info(pid,ft, eibuf, sizeof(eibuf));
					if (ft->opt_trace) {
						PRINTF("%s core  %*d %s%s       %8s %s%s%s\n",
							when,
							pid_size, pid,
							eibuf,
							// (ft->opt_flags & OPT_GLYPH) ? "\u2620" : "",
							(ft->opt_flags & OPT_GLYPH) ? "X" : "",
							"",
							info1->kernel_thread ? "[" : "",
							info1->cmdline,
							info1->kernel_thread ? "]" : "");
					}
				}
#endif /* UNUSED_EVENTS */
				break;
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0)
			case PROC_EVENT_PTRACE:
				if (ft->opt_trace)
					PRINTF("PROC_EVENT_PTRACE\n");
#if UNUSED_EVENTS
				if (!(ft->opt_flags & OPT_QUIET) && (ft->opt_flags & OPT_EV_PTRC)) {
					const bool attach = (proc_ev->event_data.ptrace.tracer_pid != 0);

#if 0
					pid = attach ? proc_ev->event_data.ptrace.tracer_pid :
						       proc_ev->event_data.ptrace.process_pid;
#else
					pid = proc_ev->event_data.ptrace.process_pid;
#endif
					info1 = proc_info_get(pid, ft);
					proc_info_release(pid, ft);
					if (ft->opt_trace) {
						extra_info(pid,ft, eibuf, sizeof(eibuf));
						PRINTF("%s ptrce %*d %s%s%6s %8s %s%s%s\n",
							when,
							pid_size, pid,
							eibuf,
							(ft->opt_flags & OPT_GLYPH) ? " " : "",
							attach ? "attach" : "detach",
							"",
							info1->kernel_thread ? "[" : "",
							attach ? info1->cmdline : "",
							info1->kernel_thread ? "]" : "");
					}
				}
#endif /* UNUSED_EVENTS */
				break;
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,1,0)
			case PROC_EVENT_COMM:
				if (ft->opt_trace)
					PRINTF("PROC_EVENT_COMM\n");
#if UNUSED_EVENTS
				if (!(ft->opt_flags & OPT_QUIET) && (ft->opt_flags & OPT_EV_COMM)) {
					pid = proc_ev->event_data.comm.process_pid;
					info1 = proc_info_get(pid, ft);
					proc_info_release(pid, ft);
					comm = proc_cmdline(pid, ft);
					if (!comm)
						break;

					extra_info(pid,ft, eibuf, sizeof(eibuf));
					if (ft->opt_trace) {
						PRINTF("%s comm  %*d %s%s%s       %8s %s%s%s -> %s\n",
							when,
							pid_size, pid,
							eibuf,
							(ft->opt_flags & OPT_GLYPH) ? "\u21bb" : "",
							"",
							"",
							info1->kernel_thread ? "[" : "",
							info1->cmdline,
							info1->kernel_thread ? "]" : "",
							comm);
					}
					free_proc_comm(comm);
					proc_info_update(pid, ft);
				}
#endif /* UNUSED_EVENTS */
				break;
#endif
			default:
				break;
			}
		}
	}
	jbuf_free(jbd);
	return NULL;
}

static forkstat_t *forkstat_create()
{
	forkstat_t *ft = calloc(1,sizeof(*ft));
	if (!ft) {
		errno = ENOMEM;
		return NULL;
	}
	/* init nonzero stuff */
	ft->max_pids = INT_MAX;
	ft->opt_flags = OPT_CMD_LONG;
	ft->sane_procs = sane_proc_pid_info();
	return ft;
}

static void forkstat_destroy(forkstat_t *ft)
{
	if (!ft)
		return;
	proc_info_unload(ft);
	uid_name_info_free(ft);
	free_proc_info(ft);
	if (ft->json_log) {
		fclose(ft->json_log);
		ft->json_log = NULL;
	}
	free(ft);
}

#define PLINIT { 0 }

static struct exclude_arg excludes[] = {
	{default_exclude_programs, VT_FILE, 0, "NOTIFIER_EXCLUDE_PROGRAMS", NULL, 0, PLINIT},
	{default_exclude_dirs, VT_DIR, 0, "NOTIFIER_EXCLUDE_DIR_PATH", NULL, 0, PLINIT},
	{default_short_path, VT_DIR, 0, "NOTIFIER_EXCLUDE_SHORT_PATH", NULL, 0, PLINIT},
	{default_short_time, VT_SCALAR, 0, "NOTIFIER_EXCLUDE_SHORT_TIME", NULL, 0, PLINIT},
	{"slurm", VT_SCALAR, 0, "NOTIFIER_LDMS_STREAM", NULL, 0, PLINIT},
	{"sock", VT_SCALAR, 0, "NOTIFIER_LDMS_XPRT", NULL, 0, PLINIT},
	{"localhost", VT_SCALAR, 0, "NOTIFIER_LDMS_HOST", NULL, 0, PLINIT},
	{"411", VT_SCALAR, 0, "NOTIFIER_LDMS_PORT", NULL, 0, PLINIT},
	{"munge", VT_SCALAR, 0, "NOTIFIER_LDMS_AUTH", NULL, 0, PLINIT},
	{"600", VT_SCALAR, 0, "NOTIFIER_LDMS_RECONNECT", NULL, 0, PLINIT},
	{"1", VT_SCALAR, 0, "NOTIFIER_LDMS_TIMEOUT", NULL, 0, PLINIT},
	{default_send_log, VT_FILE, 0, "NOTIFIER_SEND_LOG", NULL, 0, PLINIT},
};
static struct exclude_arg *bin_exclude = &excludes[0];
static struct exclude_arg *dir_exclude = &excludes[1];
static struct exclude_arg *short_dir_exclude = &excludes[2];
static struct exclude_arg *duration_exclude = &excludes[3];
static struct exclude_arg *stream_arg = &excludes[4];
static struct exclude_arg *xprt_arg = &excludes[5];
static struct exclude_arg *host_arg = &excludes[6];
static struct exclude_arg *port_arg = &excludes[7];
static struct exclude_arg *auth_arg = &excludes[8];
static struct exclude_arg *reconnect_arg = &excludes[9];
static struct exclude_arg *timeout_arg = &excludes[10];
static struct exclude_arg *send_log_arg = &excludes[11];

static struct option long_options[] = {
	{"exclude-programs", optional_argument, 0, 0},
	{"exclude-dir-path", optional_argument, 0, 0},
	{"exclude-short-path", optional_argument, 0, 0},
	{"exclude-short-time", optional_argument, 0, 0},
	{"stream", required_argument, 0, 0},
	{"xprt", required_argument, 0, 0},
	{"host", required_argument, 0, 0},
	{"port", required_argument, 0, 0},
	{"auth", required_argument, 0, 0},
	{"reconnect", required_argument, 0, 0},
	{"timeout", required_argument, 0, 0},
	{"send-log", required_argument, 0, 0},
	{0, 0, 0, 0}
};

#define ATTR_UNQUOTED 0
#define ATTR_QUOTED 1
/* note: jobid is a special case and is not in these lists */
static struct env_attr slurm_env_start_default[] = {
	{ "cluster", "SLURM_CLUSTER_NAME", "undefined", ATTR_QUOTED },
	{ "step_id", "SLURM_STEP_ID", NULL_STEP_ID, ATTR_UNQUOTED },
	{ "task_pid", "SLURM_TASK_PID", NULL_STEP_ID, ATTR_UNQUOTED },
	{ "nodeid", "SLURM_NODEID", NULL_STEP_ID, ATTR_UNQUOTED },
	{ "uid", "SLURM_JOB_UID", NULL_STEP_ID, ATTR_UNQUOTED },
	{ "gid", "SLURM_JOB_GID", NULL_STEP_ID, ATTR_UNQUOTED },
	{ "nnodes", "SLURM_JOB_NUM_NODES", NULL_COUNT, ATTR_UNQUOTED },
	{ "total_tasks", "SLURM_NTASKS", NULL_COUNT, ATTR_UNQUOTED },
};
static struct env_attr slurm_env_end_default[] = {
	{ "cluster", "SLURM_CLUSTER_NAME", "undefined", ATTR_QUOTED },
	{ "step_id", "SLURM_STEP_ID", NULL_STEP_ID, ATTR_UNQUOTED },
	{ "task_pid", "SLURM_TASK_PID", NULL_STEP_ID, ATTR_UNQUOTED },
	{ "nodeid", "SLURM_NODEID", NULL_STEP_ID, ATTR_UNQUOTED },
};

static struct env_attr lsf_env_start_default[] = {
	{ "task_pid", "LS_JOBPID", NULL_STEP_ID, 0 }
};

static int nlongopt = sizeof(excludes)/sizeof(excludes[0]) ;

/*
 *  show_help()
 *	simple help
 */
static void show_help(char *const argv[])
{
	PRINTF("%s, version %s\n\n", APP_NAME, VERSION);
	PRINTF("usage: %s [options]\n", argv[0]);
	PRINTF(
		"-c\tuse task comm field for process name.\n"
		"-d\tstrip off directory path from process name.\n"
		"-D\tspecify run duration in seconds.\n"
		"-e\tselect which events to monitor.\n"
		"-E\tequivalent to -e all.\n"
		"-g\tshow glyphs for event types in debug mode.\n"
		"-h\tshow this help.\n"
		"-i seconds\t time (float) to sleep between checks for processes exceeding the short dir filter time.\n"
		"\t\t If the -i value > the --exclude-short-time value, \n"
		"\t\t -i may effectively filter out additional processes.\n"
		"-j file\t file to log json messages and transmission status.\n"
		"-l\tforce stdout line buffering.\n"
		"-L file\tredirect stdout to a log file\n"
		"-r\trun with real time FIFO scheduler.\n"
		"-s\tshow short process name in debugging.\n"
		"-t\tshow debugging trace messages.\n"
		"-u umin\tignore processes with uid < umin\n"
		"-v <int>\tlog detail level for stream library messages. Higher is quieter.\n"
		"-q\trun quietly\n"
		"-x\tshow extra process information.\n"
		"-X\tequivalent to -Egrx.\n");
	int c;
	PRINTF("The ldmsd connection and commonly uninteresting or short-lived processes may be"
		" specified with the options or environment variables below.\n");
	PRINTF("The 'short' options do not override the exclude entirely options.\n");
	for (c = 0; c < nlongopt; c++) {
		switch (excludes[c].valtype) {
		case VT_SCALAR:
			if (long_options[c].has_arg == required_argument)
				PRINTF("--%s[=]<val>\t change the default value of %s.\n"
				       "\t If repeated, the last value given wins.\n"
				       "\t The default %s is used if env %s is not set.\n",
					long_options[c].name, long_options[c].name,
					excludes[c].defval, excludes[c].env);
			else
				PRINTF("--%s[=][val]\t change the default value of %s.\n"
				       "\t If repeated, the last value given wins.\n"
				       "\t If given with no value, the default %s becomes 0 unless\n"
				       "\t the environment variable %s is set.\n",
					long_options[c].name, long_options[c].name,
					excludes[c].defval, excludes[c].env);
			break;
		case VT_DIR:
		case VT_FILE:
			PRINTF("--%s[=]<path>\t change the default value of %s\n"
			       "\t When repeated, all values are concatenated.\n"
			       "\t If given with no value, the default %s is removed.\n"
			       "\t If not given, the default is used unless\n"
			       "\t the environment variable %s is set.\n",
				long_options[c].name, long_options[c].name,
				excludes[c].defval, excludes[c].env);
			break;
		case VT_NONE:
			break;
		}
	}
}

/*
 *  set_ev()
 *	parse event strings, turn into flag mask
 */
static int set_ev(char * const arg, forkstat_t *ft)
{
	char *str, *token;

	char *xarg = strdup(arg);
	if (!xarg) {
		EPRINTF( "out of memory parsing -e argument %s\n", arg);
		return -1;
	}
	for (str = xarg; (token = strtok(str, ",")) != NULL; str = NULL) {
		size_t i;
		bool found = false;

		for (i = 0; ev_map[i].event; i++) {
			if (!strcmp(token, ev_map[i].event)) {
				ft->opt_flags |= ev_map[i].flag;
				found = true;
				break;
			}
		}
		if (!found) {
			EPRINTF( "Unknown event '%s'. Allowed events:", token);
			for (i = 0; ev_map[i].event; i++)
				PRINTF(" %s", ev_map[i].event);
			PRINTF("\n");
			free(xarg);
			return -1;
		}
	}
	free(xarg);
	return 0;
}

static int set_interval(char *optarg, forkstat_t *ft)
{
	if (!optarg || !ft)
		return -1;
	char *end = NULL;
	errno = 0;
	ft->opt_wake_interval = strtod(optarg, &end);
	if (errno || !isfinite(ft->opt_wake_interval)) {
		EPRINTF( "bad -i <sec>, not %s\n", optarg);
		ft->opt_wake_interval = 1;
		return EINVAL;
	}
	if (*end != '\0') {
		EPRINTF( "-i %s has unexpected characters.\n", optarg);
		ft->opt_wake_interval = 1;
		return EINVAL;
	}
	if (ft->opt_trace)
		PRINTF("wake interval %g\n", ft->opt_wake_interval);
	return 0;
}

static int set_duration_min(char *optarg, forkstat_t *ft)
{
	if (!optarg || !ft)
		return -1;
	char *end = NULL;
	errno = 0;
	ft->opt_duration_min = strtod(optarg, &end);
	if (errno || !isfinite(ft->opt_duration_min)) {
		EPRINTF( "bad --exclude-short-time <sec>, not %s\n", optarg);
		ft->opt_duration_min = 0;
		return EINVAL;
	}
	if (*end != '\0') {
		EPRINTF( "--exclude-short-time %s has unexpected characters.\n", optarg);
		ft->opt_duration_min = 0;
		return EINVAL;
	}
	if (ft->opt_trace)
		PRINTF("duration_min %g\n", ft->opt_duration_min);
	return 0;
}

static int set_uidmin(char *arg, forkstat_t *ft)
{
	if (!arg || !ft)
		return EINVAL;
	char *end = NULL;
	ft->opt_uidmin = strtoul(arg, &end, 10);
	if (ft->opt_uidmin > INT_MAX) {
		ft->print("Illegal uid minimum: %s\n", arg);
		return EINVAL;
	}
	if (*end != '\0') {
		ft->print("-u <integer> needed, not %s\n", optarg);
		return EINVAL;
	}
	ft->opt_flags |= OPT_UMIN;
	return 0;
}

static int forkstat_connect(forkstat_t *ft, struct monitor_args *ma)
{
	if (!ma || !ft)
		return EINVAL;
	ma->ft = ft;
	ma->tid = -1;
	int s = proc_info_load(ft);
	if (s < 0) {
		EPRINTF( "Cannot load process cache. Is /proc mounted?\n");
		return s;
	}
	ma->sock = netlink_connect();
	if (ma->sock == -EPROTONOSUPPORT) {
		EPRINTF( "Cannot show process activity with this kernel, netlink required.\n");
		return EPROTONOSUPPORT;
	}
	if (ma->sock < 0)
		return EINVAL;
	if (netlink_listen(ma->sock) < 0) {
		EPRINTF( "netlink listen failed: errno=%d (%s)\n",
			errno, strerror(errno));
		close(ma->sock);
		ma->sock = -1;
		return EIO;
	}
	return 0;
}

static int forkstat_monitor(forkstat_t *ft, struct monitor_args *ma)
{
	if (!ma || !ft)
		return EINVAL;
	pthread_attr_t attr;
	int s = pthread_attr_init(&attr);
	if (s != 0) {
		ft->print("forkstat_monitor pthread_attr_init fail\n");
		return s;
	}
	if (ft->opt_flags & OPT_REALTIME) {
		s = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
		if (s != 0) {
			ft->print("forkstat_monitor pthread_attr_setinheritsched fail\n");
			goto out;
		}
		struct sched_param param;
		int max_prio;
		const int policy = SCHED_FIFO;
		max_prio = sched_get_priority_max(policy);
		if (max_prio < 0) {
			s = errno;
			EPRINTF( "sched_get_priority_max failed: errno=%d (%s)\n",
				s, strerror(s));
			goto out;
		}
		(void)memset(&param, 0, sizeof(param));
		param.sched_priority = max_prio;

		s = pthread_attr_setschedpolicy(&attr, policy);
		if (s != 0) {
			ft->print("forkstat_monitor pthread_attr_setschedpolicy fail\n");
			goto out;
		}
		s = pthread_attr_setschedparam(&attr, &param);
		if (s != 0) {
			ft->print("forkstat_monitor pthread_attr_setschedparam fail\n");
			goto out;
		}

	}

	pthread_create(&(ma->tid), &attr, monitor, ma);
	goto done;
out:
	close(ma->sock);
	ma->sock = -1;
done:
	pthread_attr_destroy(&attr);
	return s;
}


/*
 * The remainder of the code here, except for final function main(), Copyright
 * NTESS and Open Grid Computing as noted at the file head.
 */
static void forkstat_option_dump(forkstat_t *ft, struct exclude_arg *excludes)
{
	printf("forkstat struct:\n");
	printf("\tstop_recv= %d\n", (int)ft->stop_recv);
	printf("\tsane_procs= %d\n", (int)ft->sane_procs);
	printf("\tmax_pids= %u\n", ft->max_pids);
	printf("\topt_flags= %lu\n", (unsigned long) ft->opt_flags);
	printf("\topt_uidmin= %u\n", ft->opt_uidmin);
	printf("\topt_duration_min= %g\n", ft->opt_duration_min);
	printf("\topt_trace = %u\n", ft->opt_trace);
	size_t k;
	int c;
	for (c = 0; c < nlongopt; c++) {
		PRINTF("\tcomputed %s:\n", excludes[c].env);
		for (k = 0; k < excludes[c].len_paths; k++)
			PRINTF("\t\telt[%zu]: %s\n", k, excludes[c].paths[k].n);
	}
}

static int forkstat_stop_requested(forkstat_t *ft)
{
	int r = 0;
	int dummy = 0;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &dummy);
	pthread_mutex_lock(&stop_lock);
	if (ft)
		r = ft->stop_recv;
	pthread_mutex_unlock(&stop_lock);
	pthread_setcancelstate(dummy, &dummy);
	return r;
}

void forkstat_stop(forkstat_t *ft, int sig)
{
	int dummy = 0;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &dummy);
	if (ft && ft->opt_trace)
		PRINTF("STOP forkstat_stop %d\n", sig);
	pthread_mutex_lock(&stop_lock);
	if (ft)
		ft->stop_recv = 1;
	pthread_mutex_unlock(&stop_lock);
	pthread_setcancelstate(dummy, &dummy);
}

static int log_level = 3;
static void llog(int lvl, const char *fmt, ...) {
	if (lvl < log_level)
		return;
	pthread_mutex_lock(&log_lock);
	switch (lvl) {
	case 3:
		printf("ERR: ");
	default:
		printf("Level-%d: ", lvl);
	}
        va_list ap;
        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);
	pthread_mutex_unlock(&log_lock);
}

/* return positive parsed value or -errno. */
static int get_int(const char *v)
{
	if (!v || v[0] == '\0')
		return -EINVAL;;
	char *end;
	long x = strtol(v, &end, 10);
	if (*end == '\0' && x < INT_MAX && x >= 0)
		return x;
	return -ERANGE;
}

static int forkstat_init_ldms_stream(forkstat_t *ft)
{
	(void)ft;
	int reconnect = get_int(reconnect_arg->paths[0].n);
	int timeout = get_int(timeout_arg->paths[0].n);
	int port = get_int(port_arg->paths[0].n);
	if (port < 1 || timeout < 1 || reconnect < 1) {
		PRINTF("Invalid port(%s), reconnect(%s), or timeout(%s).\n",
			port_arg->paths[0].n, reconnect_arg->paths[0].n,
			timeout_arg->paths[0].n);
		PRINTF("Impossible slps_create\n");
		return 1;
	}
	slps_init();
	ft->ln = slps_create(stream_arg->paths[0].n, SLPS_NONBLOCKING, llog,
		xprt_arg->paths[0].n, host_arg->paths[0].n, port,
		auth_arg->paths[0].n, reconnect, timeout,
		send_log_arg->len_paths ? send_log_arg->paths[0].n : NULL);
	if (!ft->ln) {
		PRINTF("FAILED slps_create\n");
		return 1;
	}
	PRINTF("Stream handle created for stream=%s xprt=%s host=%s "
		"port=%d auth=%s reconnect=%d timeout=%d log=%s\n",
		stream_arg->paths[0].n,
		xprt_arg->paths[0].n, host_arg->paths[0].n, port,
		auth_arg->paths[0].n, reconnect, timeout,
		(send_log_arg->len_paths ?  send_log_arg->paths[0].n : "none"));
	return 0;
}

static int forkstat_finalize_ldms_stream(forkstat_t *ft)
{
	if (!ft)
		return EINVAL;
	slps_destroy(ft->ln);
	ft->ln = NULL;
	slps_finalize();
	return 0;
}


/* /////////// functions to get env var from a pid////////////////////// */
static char *null_result[1] = {NULL};

/* linear variable lookup from array e. */
static const char * get_var(const char * v, char **e, forkstat_t *ft)
{
	size_t i = 0;
	if (!v || !e)
		return NULL;
	size_t l = strlen(v);
	while (e[i] != NULL) {
		if (strncmp(e[i], v, l) == 0 && e[i][l] == '=') {
			if (ft->opt_trace)
				PRINTF("Found env(%s): %s val %s\n", v, e[i],
					&(e[i][l+1]));
			return &(e[i][l+1]);
		}
		i++;
	}
	if (ft->opt_trace)
		PRINTF("Not Found env(%s)\n", v);
	return NULL;
}

static const char * info_get_var(const char * v, const struct proc_info *info, forkstat_t *ft)
{
	if (info->pidenv) {
		if (ft->opt_trace)
			PRINTF("PID %d: ", info->pid );
		const char *s = get_var(v, info->pidenv, ft);
		if (s && !isprint(s[0])) {
			if (ft->opt_trace)
				PRINTF("Found not printable.\n");
			return NULL;
		}
		return s;
	}
	return NULL;
}

/* deallocate a null terminated array of strngs, unless it is the dummy. */
static void free_env(char **e)
{
        if (!e || e == null_result)
                return;
        size_t i = 0;
        while (e[i] != NULL) {
                free(e[i]);
		i++;
	}
        free(e);
}

static size_t count_vars(const char *e, size_t elen)
{
	size_t n, i;
	i = n = 0;
	while (i < elen) {
		while (i < elen && e[i] != '\0' ) /* skip string */
			i++;
		n++;
		while (i < elen && e[i] == '\0') /* eat nuls */
			i++;
	}
	return n;
}

/* copy env pieces from e to array.
 * update count out_len of used elements in r.
 * */
static int add_var(char **r, char *e, size_t elen, size_t *out_len, size_t len_r)
{
	if (!elen || !e || !r)
		return EINVAL;
	size_t i = 0;
	char *h = e;

	/* find next string, copy last string, until done */
	while (i < elen) {
		if (*out_len >= len_r) {
			return ENOMEM;
		}
		while (e[i] != '\0')
			i++;
		if (!strlen(h))
			goto skip_copy;
		char *v = strdup(h);
		if (!v)
			return ENOMEM;
		r[*out_len] = v;
		(*out_len) += 1;
	skip_copy:
		h = &(e[i+1]);
		i++;
	}
	return 0;
}

/* create null terminated array from /proc/$pid/environ */
static char **load_pid_env(pid_t pid, size_t *out_len)
{
        char **result = NULL;
        size_t elen;
        char fname[32];
        *out_len = 0;
        if (pid < 1) {
		errno = EINVAL;
		goto err;
	}

        snprintf(fname, sizeof(fname), "/proc/%d/environ", pid);
        FILE *f = fopen(fname, "r");
	if (!f) {
		goto err;
	}
	elen = 16384;
	char * eb = malloc(elen);
	char * e = eb;
	if (!e) {
		errno = ENOMEM;
		goto err;
	}

	ssize_t rc = getline(&e, &elen, f);
	size_t rsize = count_vars(e, rc + 1);
	result = calloc( 2 * (rsize + 1), sizeof(result[0]));
	while (rc >= 0) {
		rc = add_var(result, e, rc, out_len, rsize);
		if (rc) {
			goto out;
		}
		rc = getline(&e, &elen, f);
	}
out:
	fclose(f);
	free(e);
	return result;
err:
	free(result);
	return null_result;
}
/* /////////// end of functions to get env var from a pid ///////////////// */
#include "ovis_json/ovis_json.h"

#define info_jobid_str(info) info->jobid ? info->jobid : "0"

static int add_env_attr(struct env_attr *a, jbuf_t *jb, const struct proc_info *info, forkstat_t *ft)
{
	const char *s = info_get_var(a->env, info, ft);
	if (!s)
		s = a->v_default;
	if (a->quoted == ATTR_QUOTED) {
		*jb = jbuf_append_attr(*jb, a->attr, "\"%s\",", s );
	} else {
		*jb = jbuf_append_attr(*jb, a->attr, "%s,", s);
	}
	if (!*jb)
		return errno;
	return 0;
}

static jbuf_t make_process_start_data_linux(forkstat_t *ft, const struct proc_info *info,
#if DEBUG_EMITTER
					const char *type,
#endif
					jbuf_t jbd)
{
	(void)ft;
	jbuf_t jb = jbd;
	if (!jb)
		return NULL;
	jb = jbuf_append_str(jb,
		"{"
		"\"msgno\":%" PRIu64 ","
		"\"schema\":\"linux_task_data\","
		"\"event\":\"task_init_priv\","
		"\"timestamp\":%d,"
#if DEBUG_EMITTER
		"\"emitter\":\"%s\","
#endif
		"\"context\":\"*\","
		"\"data\":"
			"{"
			"\"start\":\"%lu.%06lu\","
			/* format start_tick as string because u64 is out
			 * of ovis_json signed int range */
			"\"start_tick\":\"%" PRIu64 "\","
			"\"job_id\":\"%s\","
			"\"serial\":%" PRId64 ","
			"\"os_pid\":%" PRId64 ","
			"\"uid\":%" PRId64 ","
			"\"gid\":%" PRId64 ","
			"\"task_pid\":%d,"
			"\"task_global_id\":" NULL_STEP_ID ","
			"\"is_thread\":%d,"
			"\"exe\":\"%s\""
			"}"
		"}",
		forkstat_get_serial(ft),
		time(NULL),
#if DEBUG_EMITTER
		type,
#endif
			info->start.tv_sec, info->start.tv_usec,
			info->start_tick,
			info_jobid_str(info),
			info->serno,
			(int64_t)info->pid,
			(int64_t)info->uid,
			(int64_t)info->gid,
			(int)info->pid,
			(int)info->is_thread,
			info->exe);
	return jb;

}

static jbuf_t make_process_end_data_linux(forkstat_t *ft, const struct proc_info *info, jbuf_t jbd)
{
	jbuf_t jb = jbd;
	(void)ft;

	if (!jb)
		return NULL;
	jb = jbuf_append_str(jb,
		"{"
		"\"msgno\":%" PRIu64 ","
		"\"schema\":\"linux_task_data\","
		"\"event\":\"task_exit\","
		"\"timestamp\":%d,"
		"\"context\":\"*\","
		"\"data\":"
			"{"
			"\"start\":\"%lu.%06lu\","
			/* format start_tick as string because u64
			* is out of ovis_json signed int range */
			"\"start_tick\":\"%" PRIu64 "\","
			"\"job_id\":\"%s\","
			"\"serial\":%" PRId64 ","
			"\"os_pid\":%" PRId64 ","
			"\"task_pid\":%d"
			"}"
		"}",
		forkstat_get_serial(ft),
		time(NULL),
			info->start.tv_sec, info->start.tv_usec,
			info->start_tick,
			info_jobid_str(info),
			info->serno,
			(int64_t)info->pid,
			(int)info->pid);

	return jb;
}

static jbuf_t make_process_start_data_lsf(forkstat_t *ft, const struct proc_info *info, jbuf_t jbd)
{
	(void)ft;
	jbuf_t jb = jbd;
	if (!jb)
		return NULL;
	jb = jbuf_append_str(jb,
		"{"
		"\"msgno\":%" PRIu64 ","
		"\"schema\":\"lsf_task_data\","
		"\"event\":\"task_init_priv\","
		"\"timestamp\":%d,"
		"\"context\":\"*\","
		"\"data\":"
			"{"
			"\"start\":\"%lu.%06lu\","
			"\"job_id\":\"%s\","
			"\"serial\":%" PRId64 ","
			"\"os_pid\":%" PRId64 ",",
		forkstat_get_serial(ft),
		time(NULL),
			info->start.tv_sec, info->start.tv_usec,
			info_jobid_str(info),
			info->serno,
			(int64_t)info->pid);
	if (!jb)
		goto out_1;
	size_t i, iend;
	iend = sizeof(lsf_env_start_default)/sizeof(lsf_env_start_default[0]);
	for (i = 0 ; i < iend; i++)
		if (add_env_attr(&lsf_env_start_default[i], &jb, info, ft))
			goto out_1;
	jb = jbuf_append_str(jb,
			"\"uid\":%" PRId64 ","
			"\"gid\":%" PRId64 ","
			"\"is_thread\":%d,"
			"\"exe\":\"%s\""
			"}"
		"}",
			(int64_t)info->uid,
			(int64_t)info->gid,
			(int)info->is_thread,
			info->exe);

 out_1:
	return jb;
}

static jbuf_t make_process_end_data_lsf(forkstat_t *ft, const struct proc_info *info, jbuf_t jbd)
{
	(void)ft;
	jbuf_t jb = jbd;
	if (!jb)
		return NULL;
	jb = jbuf_append_str(jb,
		"{"
		"\"msgno\":%" PRIu64 ","
		"\"schema\":\"lsf_task_data\","
		"\"event\":\"task_exit\","
		"\"timestamp\":%d,"
		"\"context\":\"*\","
		"\"data\":"
			"{"
			"\"start\":\"%lu.%06lu\","
			"\"job_id\":\"%s\","
			"\"serial\":%" PRId64 ","
			"\"os_pid\":%" PRId64 ",",
		forkstat_get_serial(ft),
		time(NULL),
			info->start.tv_sec, info->start.tv_usec,
			info_jobid_str(info),
			info->serno,
			(int64_t)info->pid);
	if (!jb)
		goto out_1;
	size_t i, iend;
	iend = sizeof(lsf_env_start_default)/sizeof(lsf_env_start_default[0]);
	for (i = 0 ; i < iend; i++)
		if (add_env_attr(&lsf_env_start_default[i], &jb, info, ft))
			goto out_1;
	jb = jbuf_append_str(jb,
			"\"uid\":%" PRId64
			"}"
		"}",
			(int64_t)info->uid);

 out_1:
	return jb;
}

static jbuf_t make_process_start_data_slurm(forkstat_t *ft, const struct proc_info *info,
#if DEBUG_EMITTER
					const char *type,
#endif
					jbuf_t jbd)
{
	(void)ft;
	jbuf_t jb = jbd;
	if (!jb)
		return NULL;
	jb = jbuf_append_str(jb,
		"{"
		"\"msgno\":%" PRIu64 ","
		"\"schema\":\"slurm_task_data\","
		"\"event\":\"task_init_priv\","
		"\"timestamp\":%d,"
#if DEBUG_EMITTER
		"\"emitter\":\"%s\","
#endif
		"\"context\":\"*\","
		"\"data\":"
			"{"
			"\"job_id\":\"%s\","
			"\"serial\":%" PRId64 ","
			"\"os_pid\":%" PRId64 ",",
		forkstat_get_serial(ft),
		time(NULL),
#if DEBUG_EMITTER
		type,
#endif
			info_jobid_str(info),
			info->serno,
			(int64_t)info->pid);
	size_t i, iend;
	iend = sizeof(slurm_env_start_default)/sizeof(slurm_env_start_default[0]);
	for (i = 0 ; i < iend; i++)
		if (add_env_attr(&slurm_env_start_default[i], &jb, info, ft))
			goto out_1;
	jb = jbuf_append_str(jb,
			"\"task_id\":" NULL_STEP_ID ","
			"\"task_global_id\":" NULL_STEP_ID ","
			"\"is_thread\":%d,"
			"\"exe\":\"%s\","
			"\"ncpus\":" NULL_STEP_ID ","
			"\"local_tasks\":" NULL_STEP_ID
			"}"
		"}",
			(int)info->is_thread,
			info->exe);
 out_1:
	return jb;

}

static jbuf_t make_process_end_data_slurm(forkstat_t *ft, const struct proc_info *info, jbuf_t jbd)
{
	jbuf_t jb = jbd;
	(void)ft;

	if (!jb)
		return NULL;
	jb = jbuf_append_str(jb,
		"{"
		"\"msgno\":%" PRIu64 ","
		"\"schema\":\"slurm_task_data\","
		"\"event\":\"task_exit\","
		"\"timestamp\":%d,"
		"\"context\":\"*\","
		"\"data\":"
			"{"
			"\"job_id\":\"%s\","
			"\"serial\":%" PRId64 ","
			"\"os_pid\":%" PRId64 ",",
		forkstat_get_serial(ft),
		time(NULL),
			info_jobid_str(info),
			info->serno,
			(int64_t)info->pid);
	if (!jb)
		goto out_1;
	int i, iend;
	iend = sizeof(slurm_env_end_default)/sizeof(slurm_env_end_default[0]);
	for (i = 0 ; i < iend; i++)
		if (add_env_attr(&slurm_env_start_default[i], &jb, info, ft))
			goto out_1;
	jb = jbuf_append_str(jb,
			"\"task_id\":" NULL_STEP_ID ","
			"\"task_global_id\":" NULL_STEP_ID ","
			"\"task_exit_status\":\"*\""
			"}"
		"}");

 out_1:
	return jb;
}

/* find resource manager type, jobid */
static enum rm_type get_rm_from_env(char **pidenv, struct proc_info *info, forkstat_t *ft)
{
	if (!pidenv)
		return RM_NONE;
	if (ft->opt_trace)
		PRINTF("Pid %d: ", info->pid );
	const char *s = get_var("SLURM_JOB_ID", pidenv, ft);
	if (s) {
		info->jobid = strdup(s);
#if 0 /* treat all descendent process as slurm-related */
		int tpid = -1;
		s = get_var("SLURM_TASK_PID", pidenv, ft);
		if (s)
			tpid = atoi(s);
		if (tpid == info->pid)
			/* we call it a slurm pid only if slurm knows about it */
#endif
			return RM_SLURM;
#if 0
		else
			return RM_NONE;
#endif
	}
	if (ft->opt_trace)
		PRINTF("Pid %d: ", info->pid );
	s = get_var("LSB_JOBID", pidenv, ft);
	if (s) {
		info->jobid = strdup(s);
#if 0 /* treat all descendent process as lsf-related */
		int tpid = -1;
		s = get_var("LS_JOBPID", pidenv, ft);
		if (s)
			tpid = atoi(s);
		if (tpid == info->pid)
			/* we call it a lsf pid only if lsf/sbatchd knows about it */
#endif
			return RM_LSF;
#if 0
		else
			return RM_NONE;
#endif
	}
	return RM_NONE;
}

static jbuf_t make_ldms_message(forkstat_t *ft, struct proc_info *info, const char *type, int emit_event, jbuf_t jbd)
{
	(void)type;
	char **pidenv = NULL;
	size_t pesize = 0;
	if (!jbd)
		return NULL;
	jbuf_reset(jbd);
	if (info->rm_type == RM_UNKNOWN) {
		pidenv = load_pid_env(info->pid, &pesize);
		info->rm_type = get_rm_from_env(pidenv, info, ft);
		if (info->rm_type != RM_NONE)
			info->pidenv = pidenv;
		else {
			info->pidenv = NULL;
			free_env(pidenv);
		}
	}

	if (emit_event & EMIT_EXIT) {
		switch (info->rm_type) {
		case RM_NONE:
			return make_process_end_data_linux(ft, info, jbd);
		case RM_SLURM:
			return make_process_end_data_slurm(ft, info, jbd);
		case RM_LSF:
			return make_process_end_data_lsf(ft, info, jbd);
		default:
			break;
		}
	}

	if (emit_event & EMIT_ADD) {
		switch (info->rm_type) {
		case RM_NONE:
			return make_process_start_data_linux(ft, info
#if DEBUG_EMITTER
								, type
#endif
								, jbd);
		case RM_SLURM:
			return make_process_start_data_slurm(ft, info
#if DEBUG_EMITTER
								, type
#endif
								, jbd);
		case RM_LSF:
			return make_process_start_data_lsf(ft, info, jbd);
		default:
			break;
		}
	}
	return NULL;
}

static uint64_t forkstat_get_serial(forkstat_t *ft)
{
	uint64_t r;
	pthread_mutex_lock(&serial_lock);
	r = ft->msg_serno++;
	pthread_mutex_unlock(&serial_lock);
	return r;
}

/* increment and fetch counter. returned values will always be >= 0. */
static int64_t procinfo_get_serial()
{
	int64_t r;
	pthread_mutex_lock(&serial_lock);
	serno++;
	if (serno < 0)
		serno = 0;
	r = serno;
	pthread_mutex_unlock(&serial_lock);
	return r;
}

static int send_ldms_message(forkstat_t *ft, jbuf_t jb);

/* type is exec, exit, execlong, (fork,clone) */
static int emit_info(forkstat_t *ft, struct proc_info *info, const char *type, int emit_event, bool lock, jbuf_t *jbd)
{
	// this is a proxy for the ldms stream notifier
	if (!info || !ft || !type || !jbd || !(*jbd))
		return EINVAL;
	pid_t pid = info->pid;
	if (lock)
		proc_info_get(pid, ft); // lock the bucket so info doesn't disappear during emit.
	if (info->exe && strcmp(info->exe, "(nullexe)") != 0 &&
		(!info->emitted || (strcmp(type,"exit") == 0 &&
					info->emitted < EMIT_EXIT) )) {
		/*
		PRINTF("SENDING NOTICE for  %d: %s %s (start=%lu.%06lu)\n",
			pid, type, info->exe, info->start.tv_sec,
			info->start.tv_usec);
		*/
		if (type[0] == 'e') { /* exec, execlong, exit only go to stream*/
			jbuf_t jb = make_ldms_message(ft, info, type, emit_event, *jbd);
			if (jb) {
				int rc = send_ldms_message(ft, jb);
				if (!rc)
					override_emitted(info, emit_event);
				else
					PRINTF("FAILED sending for %d\n", pid);
			} else {
				PRINTF("FAILED make_ldms_message for %d event %d\n", pid, emit_event);
			}
			*jbd = jb;
		}
	}
	if (lock)
		proc_info_release(pid, ft);
	return 0;
}

static int send_ldms_message(forkstat_t *ft, jbuf_t jb)
{
	if (ft->json_log) {
		fprintf(ft->json_log, "%s", jb->buf);
	}
	struct slps_send_result r = LN_NULL_RESULT;
	if (ft->ln)
		r = slps_send_event(ft->ln, jb);
	if (ft->json_log) {
		char *end_msgno, *start_msgno;
		/* extract number xxx from '{"msgno":xxx', */
		start_msgno = strchr(jb->buf, ':');
		end_msgno = strchr(jb->buf, ',');
		if (start_msgno && end_msgno && end_msgno > start_msgno) {
			ptrdiff_t n = end_msgno - start_msgno;
			fprintf(ft->json_log, " {\"msgno\"=%.*s,status=\"%d:%s\"}\n",
				(int)n-1, start_msgno + 1, r.rc, r.publish_count ? "SENT" : "FAIL");
		} else {
			fprintf(ft->json_log, " {\"msgno\"=\"undefined\", \"status\"=\%d:%s\"}\n", r.rc, r.publish_count ? "SENT" : "FAIL");
		}
	}
	return 0;
}

static int forkstat_set_debug_log(forkstat_t *ft, const char *fname)
{
	if (!ft || !fname)
		return EINVAL;
	debug_log = fopen(fname, "a");
	if (!debug_log)
		return errno;
	return 0;
}

static int forkstat_set_json_log(forkstat_t *ft, const char *fname)
{
	if (!ft || !fname)
		return EINVAL;
	ft->json_log = fopen(fname, "a");
	if (!ft->json_log)
		return errno;
	return 0;
}


// static int opt_trace;
static forkstat_t *shft;

#define DFLT_SORT_SZ 128
static void *dump_pids(void *vp)
{
	struct dump_args *args = vp;
	if (!args) {
		PRINTF("FAIL1 printer\n");
		return NULL;
	}
	forkstat_t *ft = args->ft;
	if (!ft) {
		PRINTF("FAIL3 printer %p\n", ft);
		return NULL;
	}
	if (ft->opt_trace)
		PRINTF("START printer\n");
	if (!ft->proc_info) {
		PRINTF("FAIL4 printer %p\n", ft->proc_info);
		return NULL;
	}
	struct timespec s = {(time_t) floor(ft->opt_wake_interval),
		(long)(1000000000*(ft->opt_wake_interval - floor(ft->opt_wake_interval))) };
	if (!s.tv_sec && !s.tv_nsec) {
		s.tv_sec = 1;
		s.tv_nsec = 0;
	}
	jbuf_t jbd = jbuf_new();
	jbuf_t jb = jbd;
	while (1 && jb) {
		if (forkstat_stop_requested(ft))
			break;
		size_t i;
		if (ft->opt_trace)
			PRINTF("start UPDATE\n");
		struct timeval tv;
		if (gettimeofday(&tv, NULL) < 0)
			(void)memset(&tv, 0, sizeof tv);
		for (i = 0; i < MAX_PIDS; i++) {
			LPRINTF("%s:%d: locking %zu\n", __func__ ,__LINE__, i);
			pthread_mutex_lock(&(ft->proc_info[i].lock));

			// check for not emitted and not excluded or over short limit and emit here.
			proc_info_t *info = ft->proc_info[i].pi;
			while (info) {
				if (!(info->emitted & EMIT_ADD) &&
					!info->excluded &&
					!info->kernel_thread &&
					short_exceeded(ft, info, &tv)) {
					emit_info(ft, info, "execlong",
						EMIT_ADD | info->emitted,
						false, &jb);
					if (!jb)
						jbuf_free(jbd);
					jbd = jb;
				}
				info = info->next;
			}
			LPRINTF("%s:%d: unlocking %zu\n", __func__ ,__LINE__, i);
			pthread_mutex_unlock(&(ft->proc_info[i].lock));
		}
		if (ft->opt_trace)
			PRINTF("end UPDATE\n");

		nanosleep(&s, NULL);
	}
	jbuf_free(jb);
	if (ft->opt_trace)
		PRINTF("END printer %d\n", ft->stop_recv);
	return NULL;
}

/*
 * This portion of the code extended by NTESS from forkstat.
 */

int main(int argc, char * argv[])
{
	debug_log = stdout;
	long int opt_duration = -1;	/* duration, < 0 means run forever */
	size_t i;
	int ret = EXIT_FAILURE;
	struct sigaction new_action;
	forkstat_t *ft = NULL;
	struct dump_args thread_args;
	memset(&thread_args, 0, sizeof(thread_args));
	if (geteuid() != 0) {
		(void)fprintf(stderr, "Need to run with root access.\n");
		goto done;
	}
	ft = forkstat_create(printf, printf);
	if (!ft) {
		fprintf(stderr, "%s: out of memory.\n", argv[0]);
		return ENOMEM;
	}
	shft = ft;
	thread_args.ft = ft;
	unsigned int *opt_flags = &(ft->opt_flags);
	char ** args = argv;
	char *default_args[] = {
		argv[0],
		"-u", "1",
		"-x",
		"-e", "exec,clone,exit",
		"-r",
		"-i", "0.5",
		"-q" };

	int c;

	for (c = 0; c < nlongopt; c++) {
		LIST_INIT(&(excludes[c].path_list));
	}
	if (argc < 2) {
		args = default_args;
		argc = sizeof(default_args) / sizeof(default_args[0]);
	}

	while (1) {
		// int this_option_optind = optind ? optind : 1;
		int option_index = 0;
		c = getopt_long(argc, args, "cdD:e:Eghi:j:L:lm:rstqxXu:v:",
				long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 0:
			excludes[option_index].parsed = 1;
			if (optarg) {
				struct path_elt *elt = calloc(1, sizeof(*elt));
				elt->pval = optarg;
				LIST_INSERT_HEAD(&excludes[option_index].
						 path_list, elt, entry);
			}
			break;
		case 'c':
			*opt_flags |= OPT_COMM;
			break;
		case 'd':
			*opt_flags |= OPT_CMD_DIRNAME_STRIP;
			break;
		case 'D':
			opt_duration = strtol(optarg, NULL, 10);
			if (opt_duration <= 0) {
				(void)fprintf(stderr, "Illegal duration.\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 'e':
			if (set_ev(optarg, ft) < 0) {
				(void)fprintf(stderr, "-e failed.\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 'E':
			*opt_flags |= OPT_EV_ALL;
			break;
		case 'g':
			*opt_flags |= OPT_GLYPH;
			break;
		case 'h':
			show_help(argv);
			ret = EXIT_SUCCESS;
			goto abort_sock;
		case 'i':
			if (set_interval(optarg, ft)) {
				(void)fprintf(stderr, "-i failed.\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 'j':
			if (forkstat_set_json_log(ft, optarg)) {
				(void)fprintf(stderr, "-f %s failed.\n", optarg);
				exit(EXIT_FAILURE);
			}
			break;
		case 'L':
			if (forkstat_set_debug_log(ft, optarg)) {
				(void)fprintf(stderr, "-L %s failed.\n", optarg);
				exit(EXIT_FAILURE);
			}
			break;
		case 'r':
			*opt_flags |= OPT_REALTIME;
			break;
		case 's':
			*opt_flags &= ~OPT_CMD_LONG;
			*opt_flags |= OPT_CMD_SHORT;
			break;
		case 't':
			ft->opt_trace = 1;
			// opt_trace = 1;
			break;
		case 'q':
			*opt_flags |= OPT_QUIET;
			break;
		case 'l':
			if (setvbuf(stdout, NULL, _IOLBF, 0) != 0) {
				(void)fprintf(stderr, "Error setting line buffering.\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 'x':
			*opt_flags |= OPT_EXTRA;
			break;
		case 'X':
			*opt_flags |= (OPT_EV_ALL | OPT_GLYPH | OPT_EXTRA | OPT_REALTIME);
			break;
		case 'u':
			if (set_uidmin(optarg, ft)) {
				(void)fprintf(stderr, "-u failed.\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 'v':
			log_level = get_int(optarg);
			break;
		default:
			fprintf(stderr, "Unexpected option -%c\n", c);
			show_help(argv);
			exit(EXIT_FAILURE);
		}
	}
	if ((*opt_flags & OPT_EV_MASK) == 0)
		*opt_flags |= (OPT_EV_FORK | OPT_EV_EXEC | OPT_EV_EXIT | OPT_EV_CLNE | OPT_EV_PTRC);


	/* for long opts, take aggregate options, then env if present,
	 * then default. */
	for (c = 0; c < nlongopt; c++) {
		if (LIST_EMPTY(&(excludes[c].path_list)) && !excludes[c].parsed) {
			struct path_elt *elt = calloc(1, sizeof(*elt));
			char *env = getenv(excludes[c].env);
			if (env)
				elt->pval = env;
			else
				elt->pval = excludes[c].defval;
			LIST_INSERT_HEAD(&excludes[c].path_list, elt, entry);
		}
		normalize_exclude(&excludes[c]);
	}
	if (set_duration_min(duration_exclude->paths[0].n, ft)) {
		fprintf(stderr, "Bad value %s for %s or %s.\n",
			 duration_exclude->paths[0].n, long_options[3].name, duration_exclude->env);
		ret = EXIT_FAILURE;
	}

	if (ft->opt_trace)
		forkstat_option_dump(ft, excludes);

	(void)memset(&new_action, 0, sizeof(new_action));
	for (i = 0; signals[i] != -1; i++) {
		new_action.sa_handler = handle_sig;
		sigemptyset(&new_action.sa_mask);
		new_action.sa_flags = 0;

		if (sigaction(signals[i], &new_action, NULL) < 0) {
			(void)fprintf(stderr, "sigaction failed: errno=%d (%s)\n",
				errno, strerror(errno));
			goto abort_sock;
		}
	}

	struct monitor_args ma;
	ma.ret = 0;
	int conn_err = forkstat_connect(ft, &ma);
	if (conn_err) {
		PRINTF("forkstat_connect error: %d\n", conn_err);
		goto abort_sock;
	}
	assert(ft->proc_info != NULL);

	if (opt_duration > 0)
		(void)alarm(opt_duration);

 	int trackout = 0;
	if (ft->opt_trace)
		forkstat_option_dump(ft, excludes);

/* netlink/ldms threaded region */
	forkstat_init_ldms_stream(ft);
	int start_err = forkstat_monitor(ft, &ma); // thread to follow kernel netlink sock
	if (start_err)
		goto close_abort;
	if (ft->opt_trace)
		PRINTF("MON-THREAD: %lu\n", ma.tid);
	dump_pids(&thread_args);
	if (ft->opt_trace)
		PRINTF("DUMP done\n");
	// main thread follows data structures
	int s = pthread_cancel(ma.tid);
	if (ft->opt_trace)
		PRINTF("CANCEL done\n");
	if (s) {
		(void)fprintf(stderr, "pthread_cancel failed: errno=%d (%s)\n",
			s, strerror(s));
	}
	void *res = NULL;
	pthread_join(ma.tid, &res);
	if (ft->opt_trace)
		PRINTF("JOIN done w/%p\n", res);
	forkstat_finalize_ldms_stream(ft);
/* resume unthreaded region */
	if (ft->opt_trace)
		forkstat_option_dump(ft, excludes);
	if (ft->opt_trace)
		PRINTF("ma.ret=%d\n", ma.ret);
	trackout = ma.ret;
	if (trackout == 0) {
		ret = EXIT_SUCCESS;
	}

close_abort:
abort_sock:
	forkstat_destroy(ft);
done:
	for (c = 0; c < nlongopt; c++)
		reset_excludes(&excludes[c]);
	if (debug_log != stdout)
		fclose(debug_log);
	exit(ret);
}
