/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2010-2018 Open Grid Computing, Inc. All rights reserved.
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

#define _GNU_SOURCE
#include <unistd.h>
#include <inttypes.h>
#include <stdarg.h>
#include <getopt.h>
#include <stdlib.h>
#include <sys/errno.h>
#include <stdio.h>
#include <syslog.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <pthread.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/un.h>
#include <ctype.h>
#include <netdb.h>
#include <dlfcn.h>
#include <assert.h>
#include <libgen.h>
#include <time.h>
#include <coll/rbt.h>
#include <coll/str_map.h>
#include <ev/ev.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_plugin.h"
#include "ldmsd_sampler.h"
#include "ldmsd_translator.h"
#include "ldms_xprt.h"
#include "ldmsd_request.h"
#include "config.h"
#include "kldms_req.h"

#include "ovis_event/ovis_event.h"

#ifdef DEBUG
#include <mcheck.h>
#endif /* DEBUG */

#define FMT "A:a:B:c:t:FH:kl:m:n:P:r:s:u:Vv:x:Ct:"

#define LDMSD_KEEP_ALIVE_30MIN 30*60*1000000 /* 30 mins */

char *progname;
uid_t proc_uid;
gid_t proc_gid;
short is_ldmsd_initialized;
short is_syntax_check;
short is_foreground;

short ldmsd_is_initialized()
{
	return is_ldmsd_initialized;
}

short ldmsd_is_foreground()
{
	return is_foreground;
}

const char *ldmsd_progname_get()
{
	return progname;
}

pthread_t event_thread = (pthread_t)-1;
pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

/* NOTE: For determining version by dumping binary string */
char *_VERSION_STR_ = "LDMSD_VERSION " OVIS_LDMS_VERSION;

mode_t inband_cfg_mask = LDMSD_PERM_FAILOVER_ALLOWED;
	/* LDMSD_PERM_FAILOVER_INTERNAL will be added in `failover_start`
	 * command.
	 *
	 * If failover is not in use, 0777 will later be added after
	 * process_config_file.
	 */
int ldmsd_use_failover = 0;

ldms_t ldms;
FILE *log_fp;

int find_least_busy_thread();

const char* ldmsd_loglevel_names[] = {
	LOGLEVELS(LDMSD_STR_WRAP)
	NULL
};

void ldmsd_sec_ctxt_get(ldmsd_sec_ctxt_t sctxt)
{
	sctxt->crd.gid = proc_gid;
	sctxt->crd.uid = proc_uid;
}

void ldmsd_version_get(struct ldmsd_version *v)
{
	v->major = LDMSD_VERSION_MAJOR;
	v->minor = LDMSD_VERSION_MINOR;
	v->patch = LDMSD_VERSION_PATCH;
	v->flags = LDMSD_VERSION_FLAGS;
}

int ldmsd_loglevel_to_syslog(enum ldmsd_loglevel level)
{
	switch (level) {
#define MAPLOG(X,Y) case LDMSD_L##X: return LOG_##Y
	MAPLOG(DEBUG,DEBUG);
	MAPLOG(INFO,INFO);
	MAPLOG(WARNING,WARNING);
	MAPLOG(ERROR,ERR);
	MAPLOG(CRITICAL,CRIT);
	MAPLOG(ALL,ALERT);
	default:
		return LOG_ERR;
	}
#undef MAPLOG
}

/* Impossible file pointer as syslog-use sentinel */
#define LDMSD_LOG_SYSLOG ((FILE*)0x7)

void __ldmsd_log(enum ldmsd_loglevel level, const char *fmt, va_list ap)
{
	if ((level != LDMSD_LALL) &&
			(ldmsd_is_quiet() ||
				((0 <= level) && (level < ldmsd_loglevel_get()))))
		return;
	if (log_fp == LDMSD_LOG_SYSLOG) {
		vsyslog(ldmsd_loglevel_to_syslog(level),fmt,ap);
		return;
	}
	time_t t;
	struct tm *tm;
	char dtsz[200];

	pthread_mutex_lock(&log_lock);
	if (!log_fp) {
		pthread_mutex_unlock(&log_lock);
		return;
	}
	t = time(NULL);
	tm = localtime(&t);
	if (strftime(dtsz, sizeof(dtsz), "%a %b %d %H:%M:%S %Y", tm))
		fprintf(log_fp, "%s: ", dtsz);

	if (level < LDMSD_LALL) {
		fprintf(log_fp, "%-10s: ", ldmsd_loglevel_names[level]);
	}

	vfprintf(log_fp, fmt, ap);
	fflush(log_fp);
	pthread_mutex_unlock(&log_lock);
}

void ldmsd_log(enum ldmsd_loglevel level, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	__ldmsd_log(level, fmt, ap);
	va_end(ap);
}

/* All messages from the ldms library are of e level.*/
#define LDMSD_LOG_AT(e,fsuf) \
void ldmsd_l##fsuf(const char *fmt, ...) \
{ \
	va_list ap; \
	va_start(ap, fmt); \
	__ldmsd_log(e, fmt, ap); \
	va_end(ap); \
}

LDMSD_LOG_AT(LDMSD_LDEBUG,debug);
LDMSD_LOG_AT(LDMSD_LINFO,info);
LDMSD_LOG_AT(LDMSD_LWARNING,warning);
LDMSD_LOG_AT(LDMSD_LERROR,error);
LDMSD_LOG_AT(LDMSD_LCRITICAL,critical);
LDMSD_LOG_AT(LDMSD_LALL,all);

enum ldmsd_loglevel ldmsd_str_to_loglevel(const char *level_s)
{
	int i;
	for (i = 0; i < LDMSD_LLASTLEVEL; i++)
		if (0 == strcasecmp(level_s, ldmsd_loglevel_names[i]))
			return i;
	if (strcasecmp(level_s,"QUIET") == 0) {
		return LDMSD_LALL;
				}
	if (strcasecmp(level_s,"ALWAYS") == 0) {
		return LDMSD_LALL;
	}
	if (strcasecmp(level_s,"CRIT") == 0) {
		return LDMSD_LCRITICAL;
	}
	return LDMSD_LNONE;
}

const char *ldmsd_loglevel_to_str(enum ldmsd_loglevel level)
{
	if ((level >= LDMSD_LDEBUG) && (level < LDMSD_LLASTLEVEL))
		return ldmsd_loglevel_names[level];
	return "LDMSD_LNONE";
}

/*
 * microseconds:	us, microsecond(s)
 * milliseconnds:	ms, millisecond(s)
 * seconds:		s, sec, second(s)
 * minutes:		min, minutes(s)
 * hours:		h, hr(s), hour(s)
 * days:		day(s)
 */
unsigned long ldmsd_time_str2us(const char *s)
{
	int rc;
	char unit[16];
	unsigned long x;
	rc = sscanf(s, "%lu %s", &x, unit);

	if ((rc == 1) || (0 == strcmp(unit, "us")) || (0 == strncmp(unit, "micro", 5))) {
		/* microseconds */
		return x;
	} else if ((0 == strcmp(unit, "ms")) || (0 == strncmp(unit, "milli", 5))) {
		/* milliseconds */
		return x * 1000;
	} else if ((0 == strcmp(unit, "s")) || (0 == strncmp(unit, "sec", 3))) {
		/* seconds */
		return x * 1000000;
	} else if (0 == strncmp(unit, "min", 3)) {
		/* minutes */
		return x * 1000000 * 60;
	} else if (unit[0] == 'h') {
		/* hours */
		return x * 1000000 * 60 * 60;
	} else if (0 == strncmp(unit, "day", 3)) {
		/* days */
		return x * 1000000 * 60 * 60 * 24;
	} else {
		return 0;
	}
}

char *ldmsd_time_us2str(unsigned long us)
{
	char *s = malloc(128);
	if (!s)
		return NULL;
	unsigned long _us = us % 1000;
	if (_us) {
		/* The microsecond value is not a multiple of milliseconds
		 * return the value in microseconds.
		 */
		snprintf(s, 128, "%lu us", us);
		return s;
	}
	unsigned long ms = us / 1000;
	unsigned long _ms = ms % 1000;
	if (_ms) {
		snprintf(s, 128, "%lu ms", ms);
		return s;
	}
	unsigned long sec = ms / 1000;
	unsigned long _sec = sec % 60;
	if (_sec) {
		snprintf(s, 128, "%lu s", sec);
		return s;
	}
	unsigned long m = sec / 60;
	unsigned long _m = m % 60;
	if (_m) {
		snprintf(s, 128, "%lu minutes", m);
		return s;
	}
	unsigned long hr = m / 60;
	unsigned long _hr = hr % 24;
	if (_hr) {
		snprintf(s, 128, "%lu hours", hr);
		return s;
	}
	unsigned long day = hr / 24;
	snprintf(s, 128, "%lu days", day);
	return s;
}

#ifdef LDMSD_UPDATE_TIME
double ldmsd_timeval_diff(struct timeval *start, struct timeval *end)
{
	return (end->tv_sec-start->tv_sec)*1000000.0 + (end->tv_usec-start->tv_usec);
}
#endif /* LDMSD_UPDATE_TIME */

extern void ldmsd_strgp_close();

void cleanup(int x, const char *reason)
{
	int llevel = LDMSD_LINFO;
	ldmsd_listen_t listen, nxt_listen;
	ldmsd_daemon_t pidfile;

	if (x)
		llevel = LDMSD_LCRITICAL;
	ldmsd_mm_status(LDMSD_LDEBUG,"mmap use at exit");
	ldmsd_strgp_close();
	ldmsd_log(llevel, "LDMSD_ LDMS Daemon exiting...status %d, %s\n", x,
		       (reason && x) ? reason : "");

	ldmsd_cfg_lock(LDMSD_CFGOBJ_LISTEN);
	listen = (ldmsd_listen_t)ldmsd_cfgobj_first(LDMSD_CFGOBJ_LISTEN);
	while (listen) {
		nxt_listen = (ldmsd_listen_t)ldmsd_cfgobj_next(&listen->obj);
		ldmsd_cfgobj_put(&listen->obj);
		listen = nxt_listen;
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_LISTEN);

	/* pidfile & banner */
	pidfile = (ldmsd_daemon_t)ldmsd_cfgobj_find("pid_file", LDMSD_CFGOBJ_DAEMON);
	if (pidfile->obj.enabled) {
		unlink(json_value_str(json_value_find(
					pidfile->attr, "path"))->str);
		if (json_value_bool(json_value_find(pidfile->attr, "banner"))) {
			unlink(json_value_str(json_value_find(
					pidfile->attr, "banner_path"))->str);
		}
	}
	ldmsd_log(llevel, "LDMSD_ cleanup end.\n");

	exit(x);
}

/** return a file pointer or a special syslog pointer */
FILE *ldmsd_open_log(const char *path)
{
	FILE *f;
	int rc;
	char *errstr;
	if (strcasecmp(path, "syslog")==0) {
		ldmsd_log(LDMSD_LDEBUG, "Switching to syslog.\n");
		f = LDMSD_LOG_SYSLOG;
		openlog(progname, LOG_NDELAY|LOG_PID, LOG_DAEMON);
		return f;
	}

	f = fopen_perm(path, "a", LDMSD_DEFAULT_FILE_PERM);
	if (!f) {
		ldmsd_log(LDMSD_LERROR, "Could not open the log file named '%s'\n", path);
		rc = 9;
		errstr= "log open failed";
		goto err;
	}
	if (!ldmsd_is_syntax_check()) {
		int fd = fileno(f);
		if (dup2(fd, 1) < 0) {
			ldmsd_log(LDMSD_LERROR, "Cannot redirect log to %s\n", path);
			rc = 10;
			errstr = "error redirecting stdout";
			goto err;
		}
		if (dup2(fd, 2) < 0) {
			ldmsd_log(LDMSD_LERROR, "Cannot redirect log to %s\n", path);
			rc = 11;
			errstr = "error redirecting stderr";
			goto err;
		}
		stdout = f;
		stderr = f;
	}
	return f;

err:
	if (!ldmsd_is_syntax_check())
		cleanup(rc, errstr);
	return f;
}

void cleanup_sa(int signal, siginfo_t *info, void *arg)
{
	ldmsd_log(LDMSD_LINFO, "signo : %d\n", info->si_signo);
	ldmsd_log(LDMSD_LINFO, "si_pid: %d\n", info->si_pid);
	cleanup(0, "signal to exit caught");
}


void usage_hint(char *hint)
{
	printf("%s: [%s]\n", progname, FMT);
	printf("  General Options\n");
	printf("    -V	           Print LDMS version and exit [CMD LINE ONLY]\n");
	printf("    -F	           Foreground mode, don't daemonize the program [false] [CMD LINE ONLY]\n");
	printf("    -C	           Only perform syntax checking [CMD LINE ONLY]\n");
	printf("    -u name	   List named plugin if available, and where possible its usage,\n");
	printf("		   then exit. Name all, sampler, and store limit output.[CMD LINE ONLY]\n");
	printf("    -c path	   The path to configuration file (optional, default: <none>).\n");
	printf("    -t translator  The configuration file translator plugin\n");
	printf("    -B mode        Daemon mode banner file with pidfile [1].\n"
	       "		   modes:0-no banner file, 1-banner auto-deleted, 2-banner left.\n");
	printf("    -m memory size Maximum size of pre-allocated memory for metric sets.\n"
	       "		   The given size must be less than 1 petabytes.\n"
	       "		   The default value is %s\n"
	       "		   For example, 20M or 20mb are 20 megabytes.\n"
	       "		   - The environment variable %s could be set instead of\n"
	       "		   giving the -m option. If both are given, the -m option\n"
	       "		   takes precedence over the environment variable.\n",
	       LDMSD_MEM_SIZE_DEFAULT, LDMSD_MEM_SIZE_ENV);
	printf("    -n daemon_name The name of the daemon. By default, it is \"IHOSTNAME:PORT\".\n");
	printf("    -r pid_file    The path to the pid file for daemon mode.\n"
	       "		   [" LDMSD_PIDFILE_FMT "]\n", basename(progname));
	printf("  Log Verbosity Options\n");
	printf("    -l log_file    The path to the log file for status messages.\n"
	       "		   [" LDMSD_LOGFILE "]\n");
	printf("    -v level       The available verbosity levels, in order of decreasing verbosity,\n"
	       "		   are DEBUG, INFO, ERROR, CRITICAL and QUIET.\n"
	       "		   The default level is ERROR.\n");
	printf("  Communication Options\n");
	printf("    -x xprt:port   Specifies the transport type to listen on. May be specified\n"
	       "		   more than once for multiple transports. The transport string\n"
	       "		   is one of 'rdma', 'sock' or 'ugni'. A transport specific port number\n"
	       "		   is optionally specified following a ':', e.g. rdma:50000.\n");
	printf("    -a auth        Transport authentication plugin (default: 'none')\n");
	printf("    -A key=value   Authentication plugin options (repeatable)\n");
	printf("  Kernel Metric Options\n");
	printf("    -k	           Publish kernel metrics.\n");
	printf("    -s setfile     Text file containing kernel metric sets to publish.\n"
	       "		   [" LDMSD_SETFILE "]\n");
	printf("  Thread Options\n");
	printf("    -P thr_count   Count of event threads to start.\n");
	if (hint) {
		printf("\nHINT: %s\n",hint);
	}
	cleanup(1, "usage provided");
}

void usage() {
	usage_hint(NULL);
}

#define EVTH_MAX 1024
ovis_scheduler_t *ovis_scheduler;
pthread_t *ev_thread;		/* sampler threads */
int *ev_count;			/* number of hosts/samplers assigned to each thread */

int find_least_busy_thread()
{
	int i;
	int idx = 0;
	int count = ev_count[0];
	for (i = 1; i < ldmsd_worker_count_get(); i++) {
		if (ev_count[i] < count) {
			idx = i;
			count = ev_count[i];
		}
	}
	return idx;
}

ovis_scheduler_t get_ovis_scheduler(int idx)
{
	__sync_add_and_fetch(&ev_count[idx], 1);
	return ovis_scheduler[idx];
}

void release_ovis_scheduler(int idx)
{
	__sync_sub_and_fetch(&ev_count[idx], 1);
}

pthread_t get_thread(int idx)
{
	return ev_thread[idx];
}

void kpublish(int map_fd, int set_no, int set_size, char *set_name)
{
	ldms_set_t map_set;
	int rc, id = set_no << 13;
	void *meta_addr, *data_addr;
	struct ldms_set_hdr *sh;

	ldmsd_linfo("Mapping set %d:%d:%s\n", set_no, set_size, set_name);
	meta_addr = mmap((void *)0, set_size,
			 PROT_READ | PROT_WRITE, MAP_SHARED,
			 map_fd, id);
	if (meta_addr == MAP_FAILED) {
		ldmsd_lerror("Error %d mapping %d bytes for kernel "
			     "metric set\n", errno, set_size);
		return;
	}
	sh = meta_addr;
	data_addr = (struct ldms_data_hdr *)((unsigned char*)meta_addr + sh->meta_sz);
	rc = ldms_mmap_set(meta_addr, data_addr, &map_set);
	if (rc) {
		munmap(meta_addr, set_size);
		ldmsd_lerror("Error %d mmapping the set '%s'\n", rc, set_name);
		return;
	}
	sprintf(sh->producer_name, "%s", ldmsd_myname_get());
}

pthread_t k_thread;
void *k_proc(void *arg)
{
	int rc, map_fd;
	int set_no;
	int set_size;
	char set_name[128];
	FILE *fp;
	union kldms_req k_req;
	char *kernel_setfile = (char *)arg;

	fp = fopen(kernel_setfile, "r");
	if (!fp) {
		ldmsd_lerror("The specified kernel metric set file '%s' "
			     "could not be opened.\n", kernel_setfile);
		cleanup(1, "Could not open kldms set file");
	}

	map_fd = open("/dev/kldms0", O_RDWR);
	if (map_fd < 0) {
		ldmsd_lerror("Error %d opening the KLDMS device file "
			     "'/dev/kldms0'\n", map_fd);
		cleanup(1, "Could not open the kernel device /dev/kldms0");
	}

	while (3 == fscanf(fp, "%d %d %128s", &set_no, &set_size, set_name)) {
		kpublish(map_fd, set_no, set_size, set_name);
	}

	/* Read from map_fd and process events as they are delivered by the kernel */
	while (0 < (rc = read(map_fd, &k_req, sizeof(k_req)))) {
		switch (k_req.hdr.req_id) {
		case KLDMS_REQ_HELLO:
			ldmsd_ldebug("KLDMS_REQ_HELLO: %s\n", k_req.hello.msg);
			break;
		case KLDMS_REQ_PUBLISH_SET:
			ldmsd_ldebug("KLDMS_REQ_PUBLISH_SET: set_id %d data_len %zu\n",
				     k_req.publish.set_id, k_req.publish.data_len);
			kpublish(map_fd, k_req.publish.set_id, k_req.publish.data_len, "");
			break;
		case KLDMS_REQ_UNPUBLISH_SET:
			ldmsd_ldebug("KLDMS_REQ_UNPUBLISH_SET: set_id %d data_len %zu\n",
				     k_req.unpublish.set_id, k_req.publish.data_len);
			break;
		case KLDMS_REQ_UPDATE_SET:
			ldmsd_ldebug("KLDMS_REQ_UPDATE_SET: set_id %d\n",
				     k_req.update.set_id);
			break;
		default:
			ldmsd_lerror("Unrecognized kernel request %d\n",
				     k_req.hdr.req_id);
			break;
		}
	}
	return NULL;
}
/*
 * This function opens the device file specified by 'devname' and
 * mmaps the metric set 'set_no'.
 */
int publish_kernel(const char *setfile)
{
	int res = pthread_create(&k_thread, NULL, k_proc, (void *)setfile);
	if (!res)
		pthread_setname_np(k_thread, "ldmsd:kernel");
	return 0;
}

static void resched_task(ldmsd_task_t task)
{
	struct timeval new_tv;
	long adj_interval, epoch_us;

	if (task->flags & LDMSD_TASK_F_IMMEDIATE) {
		adj_interval = random() % 1000000;
		task->flags &= ~LDMSD_TASK_F_IMMEDIATE;
	} else if (task->flags & LDMSD_TASK_F_SYNCHRONOUS) {
		gettimeofday(&new_tv, NULL);
		/* The task is already counted when the task is started */
		epoch_us = (1000000 * (long)new_tv.tv_sec) + (long)new_tv.tv_usec;
		adj_interval = task->sched_us -
			(epoch_us % task->sched_us) + task->offset_us;
		if (adj_interval <= 0)
			adj_interval += task->sched_us; /* Guaranteed to be positive */
	} else {
		adj_interval = task->sched_us;
	}
	task->oev.param.timeout.tv_sec = adj_interval / 1000000;
	task->oev.param.timeout.tv_usec = adj_interval % 1000000;
}

static int start_task(ldmsd_task_t task)
{
	int rc = ovis_scheduler_event_add(task->os, &task->oev);
	if (!rc) {
		return LDMSD_TASK_STATE_STARTED;
	}
	errno = rc;
	return LDMSD_TASK_STATE_STOPPED;
}

static void task_cb_fn(ovis_event_t ev)
{
	ldmsd_task_t task = ev->param.ctxt;
	enum ldmsd_task_state next_state = LDMSD_TASK_STATE_STOPPED;

	pthread_mutex_lock(&task->lock);
	if (task->os) {
		ovis_scheduler_event_del(task->os, ev);
		resched_task(task);
		next_state = start_task(task);
	}
	task->state = LDMSD_TASK_STATE_RUNNING;
	pthread_mutex_unlock(&task->lock);

	task->fn(task, task->fn_arg);

	pthread_mutex_lock(&task->lock);
	if (task->flags & LDMSD_TASK_F_STOP) {
		task->flags &= ~LDMSD_TASK_F_STOP;
		if (task->state != LDMSD_TASK_STATE_STOPPED)
			task->state = LDMSD_TASK_STATE_STOPPED;
	} else
		task->state = next_state;
	if (task->state == LDMSD_TASK_STATE_STOPPED) {
		if (task->os)
			ovis_scheduler_event_del(task->os, &task->oev);
		task->os = NULL;
		release_ovis_scheduler(task->thread_id);
		pthread_cond_signal(&task->join_cv);
	}
	pthread_mutex_unlock(&task->lock);
}

void ldmsd_task_init(ldmsd_task_t task)
{
	memset(task, 0, sizeof *task);
	task->state = LDMSD_TASK_STATE_STOPPED;
	pthread_mutex_init(&task->lock, NULL);
	pthread_cond_init(&task->join_cv, NULL);
}

int ldmsd_task_stop(ldmsd_task_t task)
{
	int rc = 0;
	pthread_mutex_lock(&task->lock);
	if (task->state == LDMSD_TASK_STATE_STOPPED) {
		rc = EBUSY;
		goto out;
	}
	if (task->flags & LDMSD_TASK_F_STOP) {
		rc = EINPROGRESS;
		goto out;
	}
	if (task->state != LDMSD_TASK_STATE_RUNNING) {
		ovis_scheduler_event_del(task->os, &task->oev);
		task->os = NULL;
		release_ovis_scheduler(task->thread_id);
		task->state = LDMSD_TASK_STATE_STOPPED;
		pthread_cond_signal(&task->join_cv);
	} else {
		task->flags |= LDMSD_TASK_F_STOP;
	}
out:
	pthread_mutex_unlock(&task->lock);
	return rc;
}

int ldmsd_task_resched(ldmsd_task_t task, int flags, long sched_us, long offset_us)
{
	int rc = 0;
	pthread_mutex_lock(&task->lock);
	if ((task->state != LDMSD_TASK_STATE_RUNNING)
			&& (task->state != LDMSD_TASK_STATE_STARTED))
		goto out;
	ovis_scheduler_event_del(task->os, &task->oev);
	task->flags = flags;
	task->sched_us = sched_us;
	task->offset_us = offset_us;
	resched_task(task);
	rc = ovis_scheduler_event_add(task->os, &task->oev);
out:
	pthread_mutex_unlock(&task->lock);
	return rc;
}

int ldmsd_task_start(ldmsd_task_t task,
		     ldmsd_task_fn_t task_fn, void *task_arg,
		     int flags, long sched_us, long offset_us)
{
	int rc = 0;
	pthread_mutex_lock(&task->lock);
	if (task->state != LDMSD_TASK_STATE_STOPPED) {
		rc = EBUSY;
		goto out;
	}
	task->thread_id = find_least_busy_thread();
	task->os = get_ovis_scheduler(task->thread_id);
	task->fn = task_fn;
	task->fn_arg = task_arg;
	task->flags = flags;
	task->sched_us = sched_us;
	task->offset_us = offset_us;
	OVIS_EVENT_INIT(&task->oev);
	task->oev.param.type = OVIS_EVENT_TIMEOUT;
	task->oev.param.cb_fn = task_cb_fn;
	task->oev.param.ctxt = task;
	resched_task(task);
	task->state = start_task(task);
	if (task->state != LDMSD_TASK_STATE_STARTED)
		rc = errno;
 out:
	pthread_mutex_unlock(&task->lock);
	return rc;
}

void ldmsd_task_join(ldmsd_task_t task)
{
	pthread_mutex_lock(&task->lock);
	while (task->state != LDMSD_TASK_STATE_STOPPED)
		pthread_cond_wait(&task->join_cv, &task->lock);
	pthread_mutex_unlock(&task->lock);
}

void *event_proc(void *v)
{
	ovis_scheduler_t os = v;
	ovis_scheduler_loop(os, 0);
	ldmsd_log(LDMSD_LINFO, "Exiting the sampler thread.\n");
	return NULL;
}

void ev_log_cb(int sev, const char *msg)
{
	const char *sev_s[] = {
		"EV_DEBUG",
		"EV_MSG",
		"EV_WARN",
		"EV_ERR"
	};
	ldmsd_log(LDMSD_LERROR, "%s: %s\n", sev_s[sev], msg);
}

enum ldms_opttype {
	LO_PATH,
	LO_UINT,
	LO_INT,
	LO_NAME,
};

int check_arg(char *c, char *optarg, enum ldms_opttype t)
{
	if (!optarg)
		return 1;
	switch (t) {
	case LO_PATH:
		av_check_expansion((printf_t)printf, c, optarg);
		if ( optarg[0] == '-'  ) {
			printf("option -%s expected path name, not %s\n",
				c,optarg);
			return 1;
		}
		break;
	case LO_UINT:
		if (av_check_expansion((printf_t)printf, c, optarg))
			return 1;
		if ( optarg[0] == '-' || !isdigit(optarg[0]) ) {
			printf("option -%s expected number, not %s\n",c,optarg);
			return 1;
		}
		break;
	case LO_INT:
		if (av_check_expansion((printf_t)printf, c, optarg))
			return 1;
		if ( optarg[0] == '-' && !isdigit(optarg[1]) ) {
			printf("option -%s expected number, not %s\n",c,optarg);
			return 1;
		}
		break;
	case LO_NAME:
		if (av_check_expansion((printf_t)printf, c, optarg))
			return 1;
		if ( !isalnum(optarg[0]) ) {
			printf("option -%s expected name, not %s\n",c,optarg);
			return 1;
		}
		break;
	}
	return 0;
}

int default_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t ev)
{
	ldmsd_log(LDMSD_LINFO, "Unhandled Event: type=%s, id=%d\n",
		  ev_type_name(ev_type(ev)), ev_type_id(ev_type(ev)));
	ldmsd_log(LDMSD_LINFO, "    status  : %s\n", status ? "FLUSH" : "OK" );
	ldmsd_log(LDMSD_LINFO, "    src     : %s\n", ev_worker_name(src));
	ldmsd_log(LDMSD_LINFO, "    dst     : %s\n", ev_worker_name(dst));
	return 0;
}

void ldmsd_ev_init(void)
{
	smplr_sample_type = ev_type_new("smplr:sample", sizeof(struct sample_data));
	prdcr_connect_type = ev_type_new("prdcr:connect", sizeof(struct connect_data));
	prdcr_set_update_type = ev_type_new("prdcr_set:update", sizeof(struct update_data));
	prdcr_set_store_type = ev_type_new("prdcr_set:store", sizeof(struct store_data));
	prdcr_set_state_type = ev_type_new("prdcr_set:state", sizeof(struct state_data));
	updtr_start_type = ev_type_new("updtr:start", sizeof(struct start_data));
	prdcr_start_type = ev_type_new("prdcr:start", sizeof(struct start_data));
	strgp_start_type = ev_type_new("strgp:start", sizeof(struct start_data));
	smplr_start_type = ev_type_new("smplr:start", sizeof(struct start_data));
	updtr_stop_type = ev_type_new("updtr:stop", sizeof(struct stop_data));
	prdcr_stop_type = ev_type_new("prdcr:stop", sizeof(struct stop_data));
	strgp_stop_type = ev_type_new("strgp:stop", sizeof(struct stop_data));
	smplr_stop_type = ev_type_new("smplr:stop", sizeof(struct stop_data));
	cfg_msg_ctxt_free_type = ev_type_new("cfg:msg_ctxt_free", sizeof(struct msg_ctxt_free_data));
	cfgobj_enabled_type = ev_type_new("cfg:enabled", sizeof(struct start_data));
	cfgobj_disabled_type = ev_type_new("cfg:disabled", sizeof(struct stop_data));

	producer = ev_worker_new("producer", default_actor);
	updater = ev_worker_new("updater", default_actor);
	sampler = ev_worker_new("sampler", default_actor);
	storage = ev_worker_new("storage", default_actor);
	cfg = ev_worker_new("cfg", default_actor);

	ev_dispatch(sampler, smplr_sample_type, sample_actor);
	ev_dispatch(updater, prdcr_set_update_type, prdcr_set_update_actor);
	ev_dispatch(updater, prdcr_set_state_type, prdcr_set_state_actor);
	ev_dispatch(updater, prdcr_start_type, prdcr_start_actor);
	ev_dispatch(updater, prdcr_stop_type, prdcr_stop_actor);
	ev_dispatch(producer, prdcr_connect_type, prdcr_connect_actor);
	ev_dispatch(producer, updtr_start_type, updtr_start_actor);
	ev_dispatch(producer, updtr_stop_type, updtr_stop_actor);
	ev_dispatch(cfg, cfg_msg_ctxt_free_type, cfg_msg_ctxt_free_actor);
	ev_dispatch(cfg, cfgobj_enabled_type, cfgobj_enabled_actor);
	ev_dispatch(cfg, cfgobj_disabled_type, cfgobj_disabled_actor);

}

int handle_cfgobjs()
{
	int i, rc;
	ldmsd_cfgobj_t obj;
	enum ldmsd_cfgobj_type type;
	enum ldmsd_cfgobj_type orders[] = {
			LDMSD_CFGOBJ_AUTH,
			LDMSD_CFGOBJ_LISTEN,
			LDMSD_CFGOBJ_PLUGIN,
			LDMSD_CFGOBJ_SMPLR,
			LDMSD_CFGOBJ_PRDCR,
			LDMSD_CFGOBJ_UPDTR,
			LDMSD_CFGOBJ_STRGP,
			LDMSD_CFGOBJ_SETGRP,
			0
	};

	/*
	 * Handle the daemon cfgobjs separately because the order among them
	 * matters.
	 */
	rc = ldmsd_daemon_cfgobjs();
	if (rc)
		cleanup(rc, "Failed to initialize the process.");

	i = 0;
	type =  orders[i++];
	while (type) {
		for (obj = ldmsd_cfgobj_first(type); obj;
					obj = ldmsd_cfgobj_next(obj)) {
			if (!obj->enabled || !obj->enable)
				continue;
			rc = obj->enable(obj);
			if (rc) {
				ldmsd_log(LDMSD_LERROR, "Failed to enable %s '%s'\n",
						ldmsd_cfgobj_type2str(type), obj->name);
				cleanup(rc, "Failed to start the process");
			}
		}
		type = orders[i++];
	}
	is_ldmsd_initialized = 1;
	return 0;
}

static int submit_cfgobj_request(ldmsd_plugin_inst_t pi, json_entity_t req_obj)
{
	int status, rc;
	ldmsd_req_buf_t buf;
	json_entity_t reply, results, key, v, msg;
	ldmsd_translator_type_t t = (void *)pi->base;
	struct ldmsd_sec_ctxt sctxt;
	ldmsd_sec_ctxt_get(&sctxt);
	int msg_no = json_value_int(json_value_find(req_obj, "id"));

	buf = ldmsd_req_buf_alloc(1024);
	if (!buf)
		goto oom;
	reply = ldmsd_process_cfgobj_requests(req_obj, msg_no, &sctxt);
	if (!reply)
		goto oom;
	if (msg_no == 0) {
		/* Request from the cmd-line */
		status = json_value_int(json_value_find(reply, "status"));
		if (status) {
			ldmsd_log(LDMSD_LCRITICAL, "%s\n",
				json_value_str(json_value_find(reply, "msg"))->str);
			return 0;
		}
		results = json_value_find(reply, "result");
		if (!results)
			return 0;
		for (key = json_attr_first(results); key; key = json_attr_next(key)) {
			v = json_attr_value(key);
			status = json_value_int(json_value_find(v, "status"));
			if (status) {
				msg = json_value_find(v, "msg");
				if (msg) {
					/* TODO: Think about how to report the error.
					 * It is possible that only the status is provided,
					 * no error messages.
					 */
					ldmsd_log(LDMSD_LCRITICAL, "Error %d: %s\n",
						status, json_value_str(msg)->str);
				}
			}
		}
	} else {
		rc = t->error_report(pi, reply);
		if (rc) {
			jbuf_t jb;
			jb = json_entity_dump(NULL, req_obj);
			if (!jb)
				goto oom;
			ldmsd_log(LDMSD_LCRITICAL, "Error %d: Failed to report "
						"configuration error. Request: %s\n",
						rc, jb->buf);
			jbuf_free(jb);
		}
	}
	return 0;
oom:
	if (buf)
		ldmsd_req_buf_free(buf);
	return ENOMEM;
}

static void
__handle_cfgobj_reqs(ldmsd_plugin_inst_t pi, json_entity_t req_list,
					enum ldmsd_cfgobj_type want)
{
	int rc;
	json_entity_t req, req_next, schema;
	char *type_s;
	enum ldmsd_cfgobj_type type;

	req = json_item_first(req_list);
	while (req) {
		req_next = json_item_next(req);
		schema = json_value_find(req, "schema");
		if (!schema) {
			ldmsd_log(LDMSD_LCRITICAL, "'schema' is missing "
						"from a configuration request.");
			cleanup(EINVAL, "Configuration error");
		}
		type_s = json_value_str(schema)->str;
		type = ldmsd_cfgobj_type_str2enum(type_s);
		if (type < 0) {
			ldmsd_log(LDMSD_LCRITICAL, "Unsupported configuration "
						"object '%s'\n", type_s);
			cleanup(EINVAL, "Configuration error");
		}
		if (want != type)
			goto next;
		rc = submit_cfgobj_request(pi, req);
		if (rc)
			cleanup(rc, "Configuration error");
		(void)json_item_rem(req_list, req);
		json_entity_free(req);
	next:
		req = req_next;
	}
}

static void handle_cfgobj_reqs(ldmsd_plugin_inst_t pi, json_entity_t req_list)
{
	int rc;
	json_entity_t req;

	/* Process the environment variables requests */
	__handle_cfgobj_reqs(pi, req_list, LDMSD_CFGOBJ_ENV);

	/* Process the auth cfgobj requests */
	__handle_cfgobj_reqs(pi, req_list, LDMSD_CFGOBJ_AUTH);

	/* Process the daemon cfgobj requests */
	__handle_cfgobj_reqs(pi, req_list, LDMSD_CFGOBJ_DAEMON);

	/* Process the listen cfgobj requests */
	__handle_cfgobj_reqs(pi, req_list, LDMSD_CFGOBJ_LISTEN);

	/* Process the plugin cfgobj requests */
	__handle_cfgobj_reqs(pi, req_list, LDMSD_CFGOBJ_PLUGIN);

	for (req = json_item_first(req_list); req; req = json_item_next(req)) {
		rc = submit_cfgobj_request(pi, req);
		if (rc)
			cleanup(rc, "Configuration error");
	}
}

int process_cli_options(char op, char *value, json_entity_t req_list)
{
	json_entity_t obj, spec;
	char *lval, *rval;
	static char *global_auth = NULL;

	switch (op) {
	case 'A':
		/* (multiple) auth options */
		lval = strtok(value, "=");
		if (!lval) {
			printf("ERROR: Expecting -A name=value\n");
			goto einval;
		}
		rval = strtok(NULL, "");
		if (!rval) {
			printf("ERROR: Expecting -A name=value\n");
			goto einval;
		}
		spec = json_dict_build(NULL,
				JSON_DICT_VALUE, global_auth,
					JSON_STRING_VALUE, lval, rval,
					-2,
				-1);
		if (!spec)
			goto oom;
		obj = ldmsd_cfgobj_update_req_obj_new(0,
				ldmsd_cfgobj_type2str(LDMSD_CFGOBJ_AUTH),
				0, NULL, NULL, spec);
		break;
	case 'a':
		global_auth = value;
		spec = json_dict_build(NULL,
				JSON_DICT_VALUE, value,
					JSON_STRING_VALUE, "plugin", value,
					-2,
				-1);
		if (!spec)
			goto oom;
		obj = ldmsd_cfgobj_create_req_obj_new(0,
				ldmsd_cfgobj_type2str(LDMSD_CFGOBJ_AUTH),
				1, NULL, spec);
		if (!obj)
			goto oom;
		json_item_add(req_list, obj);
		spec = json_dict_build(NULL,
				JSON_DICT_VALUE, "default_auth",
					JSON_STRING_VALUE, "name", value,
					-2,
				-1);
		if (!spec)
			goto oom;
		obj = ldmsd_cfgobj_update_req_obj_new(0,
				ldmsd_cfgobj_type2str(LDMSD_CFGOBJ_DAEMON),
				1, NULL, NULL, spec);
		break;
	case 'B':
		if (check_arg("B", value, LO_UINT)) {
			printf("The -B option is not an integer.\n");
			goto einval;
		}
		spec = json_dict_build(NULL,
				JSON_DICT_VALUE, "pid_file",
					JSON_INT_VALUE, "banner", atoi(value),
					-2,
				-1);
		if (!spec)
			goto oom;
		obj = ldmsd_cfgobj_update_req_obj_new(0,
				ldmsd_cfgobj_type2str(LDMSD_CFGOBJ_DAEMON),
				1, NULL, NULL, spec);
		break;
	case 'C':
		spec = json_dict_build(NULL,
				JSON_DICT_VALUE, "syntax_check",
					-2,
				-1);
		if (!spec)
			goto oom;
		obj = ldmsd_cfgobj_update_req_obj_new(0,
				ldmsd_cfgobj_type2str(LDMSD_CFGOBJ_DAEMON),
				1, NULL, NULL, spec);
		break;
	case 'F':
		spec = json_dict_build(NULL,
				JSON_DICT_VALUE, "daemonize",
					-2,
				-1);
		if (!spec)
			goto oom;
		obj = ldmsd_cfgobj_update_req_obj_new(0,
				ldmsd_cfgobj_type2str(LDMSD_CFGOBJ_DAEMON),
				0, NULL, NULL, spec);
		break;
	case 'k':
		spec = json_dict_build(NULL,
				JSON_DICT_VALUE, "kernel_set_path",
					-2,
				-1);
		if (!spec)
			goto oom;
		obj = ldmsd_cfgobj_update_req_obj_new(0,
				ldmsd_cfgobj_type2str(LDMSD_CFGOBJ_DAEMON),
				1, NULL, NULL, spec);
		break;
	case 'l':
		if (check_arg("l", value, LO_PATH)) {
			printf("The -l option is not a path.\n");
			goto einval;
		}
		spec = json_dict_build(NULL,
				JSON_DICT_VALUE, "log",
					JSON_STRING_VALUE, "path", value,
					-2,
				-1);
		if (!spec)
			goto oom;
		obj = ldmsd_cfgobj_update_req_obj_new(0,
				ldmsd_cfgobj_type2str(LDMSD_CFGOBJ_DAEMON),
				1, NULL, NULL, spec);
		break;
	case 'm':
		spec = json_dict_build(NULL,
				JSON_DICT_VALUE, "set_memory",
					JSON_STRING_VALUE, "size", value,
					-2,
				-1);
		if (!spec)
			goto oom;
		obj = ldmsd_cfgobj_update_req_obj_new(0,
				ldmsd_cfgobj_type2str(LDMSD_CFGOBJ_DAEMON),
				1, NULL, NULL, spec);
		break;
	case 'n':
		spec = json_dict_build(NULL,
				JSON_DICT_VALUE, "ldmsd-id",
					JSON_STRING_VALUE, "name", value,
					-2,
				-1);
		if (!spec)
			goto oom;
		obj = ldmsd_cfgobj_update_req_obj_new(0,
				ldmsd_cfgobj_type2str(LDMSD_CFGOBJ_DAEMON),
				1, NULL, NULL, spec);
		break;
	case 'P':
		if (check_arg("P", value, LO_UINT)) {
			ldmsd_log(LDMSD_LCRITICAL, "The -P option is not an integer.\n");
			goto einval;
		}
		if (atoi(value) > EVTH_MAX) {
			ldmsd_log(LDMSD_LCRITICAL, "The given worker count %s "
					"exceeds the max supported %d.\n",
					value, EVTH_MAX);
			goto einval;
		}
		spec = json_dict_build(NULL,
				JSON_DICT_VALUE, "workers",
					JSON_INT_VALUE, "count", atoi(value),
					-2,
				-1);
		if (!spec)
			goto oom;
		obj = ldmsd_cfgobj_update_req_obj_new(0,
				ldmsd_cfgobj_type2str(LDMSD_CFGOBJ_DAEMON),
				1, NULL, NULL, spec);
		break;
	case 'r':
		if (check_arg("r", value, LO_PATH)) {
			printf("The -r option is not a path.\n");
			goto einval;
		}
		spec = json_dict_build(NULL,
				JSON_DICT_VALUE, "pid_file",
					JSON_STRING_VALUE, "path", value,
					-2,
				-1);
		if (!spec)
			goto oom;
		obj = ldmsd_cfgobj_update_req_obj_new(0,
				ldmsd_cfgobj_type2str(LDMSD_CFGOBJ_DAEMON),
				1, NULL, NULL, spec);
		break;
	case 's':
		if (check_arg("s", value, LO_PATH)) {
			printf("The -s option is not a path.\n");
			goto einval;
		}
		spec = json_dict_build(NULL,
				JSON_DICT_VALUE, "kernel_set_path",
					JSON_STRING_VALUE, "path", value,
					-2,
				-1);
		if (!spec)
			goto oom;
		obj = ldmsd_cfgobj_update_req_obj_new(0,
				ldmsd_cfgobj_type2str(LDMSD_CFGOBJ_DAEMON),
				1, NULL, NULL, spec);
		break;
	case 'u':
		/*
		 * TODO: complete this
		 */
		break;
	case 'v':
		if (check_arg("v", value, LO_NAME)) {
			printf("The -v option is not a name.\n");
			goto einval;
		}
		spec = json_dict_build(NULL,
				JSON_DICT_VALUE, "log",
					JSON_STRING_VALUE, "level", value,
					-2,
				-1);
		if (!spec)
			goto oom;
		obj = ldmsd_cfgobj_update_req_obj_new(0,
				ldmsd_cfgobj_type2str(LDMSD_CFGOBJ_DAEMON),
				1, NULL, NULL, spec);
		break;
	case 'x':
		if (check_arg("x", optarg, LO_NAME)) {
			printf("The -x option is not a string.\n");
			goto einval;
		}
		lval = strdup(optarg);
		if (!lval)
			goto oom;
		rval = strchr(lval, ':');
		if (!rval) {
			printf("Bad -x option format, expecting XPRT:PORT, "
					"but received %s\n", optarg);
			goto einval;
		}
		rval[0] = '\0';
		rval += 1;
		spec = json_dict_build(NULL,
				JSON_DICT_VALUE, optarg,
					JSON_STRING_VALUE, "xprt", lval,
					JSON_STRING_VALUE, "port", rval,
					-2,
				-1);
		free(lval);
		if (!spec)
			goto oom;
		obj = ldmsd_cfgobj_create_req_obj_new(0,
				ldmsd_cfgobj_type2str(LDMSD_CFGOBJ_LISTEN),
				1, NULL, spec);
		if (!obj)
			goto oom;
		break;
	default:
		printf("Error: Invalid CLI option: %c\n", op);
		goto einval;
	}
	if (!obj)
		goto oom;
	json_item_add(req_list, obj);
	return 0;
oom:
	printf("Out of memory\n");
	cleanup(ENOMEM, "Out of memory.");
einval:
	usage();
	cleanup(EINVAL, "Invalid CLI option");
	return EINVAL; /* Prevent a compile error */
}

int handle_pidfile_banner(ldmsd_daemon_t obj)
{
	struct ldms_version ldms_v;
	struct ldmsd_version ldmsd_v;
	char *path = json_value_str(json_value_find(obj->attr, "path"))->str;
	int banner = json_value_bool(json_value_find(obj->attr, "banner"));

	ldms_version_get(&ldms_v);
	ldmsd_version_get(&ldmsd_v);

	if (!access(path, F_OK)) {
		ldmsd_log(LDMSD_LERROR, "Existing pid file named '%s': %s\n",
			path, "overwritten if writable");
	}
	FILE *pfile = fopen_perm(path, "w", LDMSD_DEFAULT_FILE_PERM);
	if (!pfile) {
		int piderr = errno;
		ldmsd_log(LDMSD_LERROR, "Could not open the pid file named '%s': %s\n",
			path, strerror(piderr));
		free(path);
		path = NULL;
	} else {
		pid_t mypid = getpid();
		fprintf(pfile,"%ld\n", (long)mypid);
		fclose(pfile);
	}
	if (path && banner) {
		char *bannerfile;
		char *suffix = ".version";
		bannerfile = malloc(strlen(suffix) + strlen(path) + 1);
		if (!bannerfile)
			goto oom;
		sprintf(bannerfile, "%s%s", path, suffix);
		if (!access(bannerfile, F_OK)) {
			ldmsd_log(LDMSD_LERROR, "Existing banner file named '%s': %s\n",
				bannerfile, "overwritten if writable");
		}
		FILE *bfile = fopen_perm(bannerfile, "w", LDMSD_DEFAULT_FILE_PERM);
		if (!bfile) {
			int banerr = errno;
			ldmsd_log(LDMSD_LERROR, "Could not open the banner file named '%s': %s\n",
				bannerfile, strerror(banerr));
		} else {

#define BANNER_PART1_A "Started LDMS Daemon with default authentication "
#define BANNER_PART1_NOA "Started LDMS Daemon without default authentication "
#define BANNER_PART2 "version %s. LDMSD Interface Version " \
"%hhu.%hhu.%hhu.%hhu. LDMS Protocol Version %hhu.%hhu.%hhu.%hhu. " \
"git-SHA %s\n", PACKAGE_VERSION, \
ldmsd_v.major, ldmsd_v.minor, \
ldmsd_v.patch, ldmsd_v.flags, \
ldms_v.major, ldms_v.minor, ldms_v.patch, \
ldms_v.flags, OVIS_GIT_LONG

			if (0 == strcmp(ldmsd_global_auth_name_get(), "none"))
				fprintf(bfile, BANNER_PART1_NOA BANNER_PART2);
			else
				fprintf(bfile, BANNER_PART1_A BANNER_PART2);
			fclose(bfile);

			obj->attr = json_dict_build(obj->attr,
					JSON_STRING_VALUE, "banner_path", bannerfile,
					-1);
			if (!obj->attr)
				goto oom;
		}
	}
	return 0;
oom:
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
	return ENOMEM;
}

int worker_threads_create(int worker_count)
{
	int i, rc;
	ev_count = calloc(worker_count, sizeof(int));
	if (!ev_count)
		goto oom;
	ovis_scheduler = calloc(worker_count, sizeof(*ovis_scheduler));
	if (!ovis_scheduler)
		goto oom;
	ev_thread = calloc(worker_count, sizeof(pthread_t));
	if (!ev_thread)
		goto oom;
	for (i = 0; i < worker_count; i++) {
		ovis_scheduler[i] = ovis_scheduler_new();
		if (!ovis_scheduler[i]) {
			ldmsd_log(LDMSD_LERROR, "Error creating an OVIS scheduler.\n");
			return ENOMEM;
		}
		rc = pthread_create(&ev_thread[i], NULL, event_proc, ovis_scheduler[i]);
		if (rc) {
			ldmsd_log(LDMSD_LERROR, "Error %d creating the event "
					"thread.\n", rc);
			return rc;
		}
		pthread_setname_np(ev_thread[i], "ldmsd:scheduler");
	}
	return 0;
oom:
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory.\n");
	return ENOMEM;
}

short ldmsd_is_syntax_check()
{
	return is_syntax_check;
}

static int
process_config_file_NEW(ldmsd_plugin_inst_t pi, const char *path,
						json_entity_t req_list)
{
	ldmsd_translator_type_t t = (void *)pi->base;
	json_entity_t l = t->translate(pi, path, req_list);
	if (!l)
		return ENOMEM;
	return 0;
}

ldmsd_plugin_inst_t ldmsd_translator_load(const char *plugin)
{
	int rc;
	ldmsd_req_buf_t buf;
	ldmsd_plugin_inst_t inst;

	buf = ldmsd_req_buf_alloc(1024);
	if (!buf)
		return NULL;
	inst = ldmsd_plugin_inst_load(plugin, plugin, buf->buf, buf->len);
	if (!inst) {
		rc = errno;
		ldmsd_log(LDMSD_LCRITICAL, "Error %d: Failed to load the translator "
							"'%s'\n", rc, plugin);
		goto err;
	}
	ldmsd_req_buf_free(buf);
	return inst;
err:
	ldmsd_req_buf_free(buf);
	cleanup(rc, "Failed to load the translator");
	return NULL;
}

int main(int argc, char **argv) {
#ifdef DEBUG
	mtrace();
#endif /* DEBUG */
	progname = argv[0];
	int rc;
	json_entity_t cfgobj_req_list;
	char *translator_name;
	struct ldms_version ldms_version;
	struct ldmsd_version ldmsd_version;
	ldms_version_get(&ldms_version);
	ldmsd_version_get(&ldmsd_version);
	int op;
	log_fp = stdout;
	struct sigaction action;
	sigset_t sigset;
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGUSR1);

	memset(&action, 0, sizeof(action));
	action.sa_sigaction = cleanup_sa;
	action.sa_flags = SA_SIGINFO;
	action.sa_mask = sigset;

	sigaction(SIGHUP, &action, NULL);
	sigaction(SIGINT, &action, NULL);
	sigaction(SIGTERM, &action, NULL);

	sigaddset(&sigset, SIGHUP);
	sigaddset(&sigset, SIGINT);
	sigaddset(&sigset, SIGTERM);
	sigaddset(&sigset, SIGABRT);

	proc_uid = geteuid();
	proc_gid = getegid();

	umask(0);

	cfgobj_req_list = json_entity_new(JSON_LIST_VALUE);
	if (!cfgobj_req_list)
		cleanup(ENOMEM, "Out of memory");

	/*
	 * Get the configuration file translator
	 */
	opterr = 0;
	while ((op = getopt(argc, argv, FMT)) != -1) {
		switch (op) {
		case 'V':
			printf("LDMSD Version: %s\n", PACKAGE_VERSION);
			printf("LDMS Protocol Version: %hhu.%hhu.%hhu.%hhu\n",
							ldms_version.major,
							ldms_version.minor,
							ldms_version.patch,
							ldms_version.flags);
			printf("LDMSD Plugin Interface Version: %hhu.%hhu.%hhu.%hhu\n",
							ldmsd_version.major,
							ldmsd_version.minor,
							ldmsd_version.patch,
							ldmsd_version.flags);
			printf("git-SHA: %s\n", OVIS_GIT_LONG);
			exit(0);
			break;
		case 't':
			translator_name = strdup(optarg);
			break;
		case 'F':
			is_foreground = 1;
			break;
		case 'C':
			is_syntax_check = 1;
			break;
		case 'A':
		case 'a':
		case 'B':
		case 'c':
		case 'H':
		case 'k':
		case 'l':
		case 'm':
		case 'n':
		case 'P':
		case 'r':
		case 's':
		case 'u':
		case 'v':
		case 'x':
			/* Process later */
			break;
		default:
			usage();
		}
	}

	if (is_syntax_check) {
		is_foreground = 1;
	}
	if (!is_foreground) {
		if (daemon(1, 1)) {
			rc = errno;
			char *s = strerror(rc);
			ldmsd_log(LDMSD_LCRITICAL, "%s\n", s);
			cleanup(rc, "Failed to daemonize the process");
		}
	}
	if (!translator_name)
		cleanup(EINVAL, "No translator plugin is given.");

	ldmsd_plugin_inst_t pi = ldmsd_translator_load(translator_name);

	ldmsd_ev_init();

	if (!translator_name)
		cleanup(EINVAL, "-t is missing.");

	/*
	 * Create the pre-defined configuration objects
	 */
	rc = ldmsd_daemon_create_cfgobjs();
	if (rc)
		cleanup(rc, "Failed to create pre-defined configuration objects.");

	/*
	 * - Get configuration file
	 */
	opterr = 0;
	optind = 0;
	while ((op = getopt(argc, argv, FMT)) != -1) {
		switch (op) {
		case 'c':
			rc = process_config_file_NEW(pi, optarg, cfgobj_req_list);
			if (rc)
				cleanup(rc, "Out of memory");
			break;
		}
	}

	/*
	 * At this point we have all requests from the configuration files.
	 *
	 * Now process the CLI options so that it will take precedence over
	 * the values specified in the configuration files.
	 */
	opterr = 0;
	optind = 0;
	while ((op = getopt(argc, argv, FMT)) != -1) {
		switch (op) {
		case 'C':
		case 'F':
		case 'c':
		case 't':
			/* Handle them already. */
			continue;
		case 'V':
			/* It shouldn't reach here. */
			cleanup(0, "");
			break;
		default:
			rc = process_cli_options(op, optarg, cfgobj_req_list);
			if (rc) {
				if (ENOMEM == rc)
					cleanup(ENOMEM, "Out of memory");
				else if (EINVAL == rc)
					cleanup(EINVAL, "An invalid CLI option");
			}
			break;
		}
	}

	handle_cfgobj_reqs(pi, cfgobj_req_list);

	/*
	 * At this point all cfgobjs were created and updated according to the
	 * configuration files and the CLI options.
	 *
	 * Now take the associated actions of the cfgobjs.
	 */
	handle_cfgobjs();

	/* Keep the process alive */
	do {
		usleep(LDMSD_KEEP_ALIVE_30MIN);
	} while (1);

	cleanup(0, NULL);
	return 0;
}

