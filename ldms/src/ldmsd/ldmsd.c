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
#include "ldms_xprt.h"
#include "ldmsd_request.h"
#include "config.h"
#include "kldms_req.h"

#include "ovis_event/ovis_event.h"

#ifdef DEBUG
#include <mcheck.h>
#endif /* DEBUG */

#define LDMSD_AUTH_ENV "LDMS_AUTH_FILE"

#define LDMSD_SETFILE "/proc/sys/kldms/set_list"
#define LDMSD_LOGFILE "/var/log/ldmsd.log"
#define LDMSD_PIDFILE_FMT "/var/run/%s.pid"

#define FMT "A:a:B:c:FH:kl:m:n:P:r:s:u:Vv:x:"

#define LDMSD_MEM_SIZE_ENV "LDMSD_MEM_SZ"
#define LDMSD_MEM_SIZE_DEFAULT "512kB"
#define LDMSD_BANNER_DEFAULT 1
#define LDMSD_VERBOSITY_DEFAULT LDMSD_LERROR
#define LDMSD_EV_THREAD_CNT_DEFAULT 1

#define LDMSD_KEEP_ALIVE_30MIN 30*60*1000000 /* 30 mins */

char ldmstype[20];
char *bannerfile;
size_t max_mem_size;
char *progname;
uid_t proc_uid;
gid_t proc_gid;
uint8_t is_ldmsd_initialized;

uint8_t ldmsd_is_initialized()
{
	return is_ldmsd_initialized;
}
pthread_t event_thread = (pthread_t)-1;
pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

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

int passive = 0;
int quiet = 0; /* Is verbosity quiet? 0 for no and 1 for yes */

const char* ldmsd_loglevel_names[] = {
	LOGLEVELS(LDMSD_STR_WRAP)
	NULL
};

struct ldmsd_cmd_line_args cmd_line_args;

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

int ldmsd_loglevel_set(char *verbose_level)
{
	int level = -1;
	if (0 == strcmp(verbose_level, "QUIET")) {
		quiet = 1;
		level = LDMSD_LLASTLEVEL;
	} else {
		level = ldmsd_str_to_loglevel(verbose_level);
		quiet = 0;
	}
	if (level < 0)
		return level;
	cmd_line_args.verbosity = level;
	return 0;
}

enum ldmsd_loglevel ldmsd_loglevel_get()
{
	return cmd_line_args.verbosity;
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
			(quiet || ((0 <= level) && (level < ldmsd_loglevel_get()))))
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

static char msg_buf[4096];
void ldmsd_msg_logger(enum ldmsd_loglevel level, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(msg_buf, sizeof(msg_buf), fmt, ap);
	ldmsd_log(level, "%s", msg_buf);
}

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

const char *ldmsd_myhostname_get()
{
	return cmd_line_args.myhostname;
}

const char *ldmsd_myname_get()
{
	return cmd_line_args.daemon_name;
}

const char *ldmsd_auth_name_get(ldmsd_listen_t listen)
{
	if (listen && listen->auth_name)
		return listen->auth_name;
	else
		return cmd_line_args.auth_name;
}

struct attr_value_list *ldmsd_auth_attr_get(ldmsd_listen_t listen)
{
	if (listen && listen->auth_name)
		return listen->auth_attrs;
	else
		return cmd_line_args.auth_attrs;
}

mode_t ldmsd_inband_cfg_mask_get()
{
	return inband_cfg_mask;
}

void ldmsd_inband_cfg_mask_set(mode_t mask)
{
	inband_cfg_mask = mask;
}

void ldmsd_inband_cfg_mask_add(mode_t mask)
{
	inband_cfg_mask |= mask;
}

void ldmsd_inband_cfg_mask_rm(mode_t mask)
{
	inband_cfg_mask &= ~mask;
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

	if (!cmd_line_args.foreground && cmd_line_args.pidfile) {
		unlink(cmd_line_args.pidfile);
		free(cmd_line_args.pidfile);
		cmd_line_args.pidfile = NULL;
		if (bannerfile) {
			if (cmd_line_args.banner < 2) {
				unlink(bannerfile);
			}
			free(bannerfile);
			bannerfile = NULL;
		}
	}
	if (cmd_line_args.pidfile) {
		free(cmd_line_args.pidfile);
		cmd_line_args.pidfile = NULL;
	}
	ldmsd_log(llevel, "LDMSD_ cleanup end.\n");
	if (cmd_line_args.log_path) {
		free(cmd_line_args.log_path);
		cmd_line_args.log_path = NULL;
	}

	exit(x);
}

/** return a file pointer or a special syslog pointer */
FILE *ldmsd_open_log()
{
	FILE *f;
	if (strcasecmp(cmd_line_args.log_path, "syslog")==0) {
		ldmsd_log(LDMSD_LDEBUG, "Switching to syslog.\n");
		f = LDMSD_LOG_SYSLOG;
		openlog(progname, LOG_NDELAY|LOG_PID, LOG_DAEMON);
		return f;
	}

	f = fopen_perm(cmd_line_args.log_path, "a", LDMSD_DEFAULT_FILE_PERM);
	if (!f) {
		ldmsd_log(LDMSD_LERROR, "Could not open the log file named '%s'\n",
						cmd_line_args.log_path);
		cleanup(9, "log open failed");
	} else {
		int fd = fileno(f);
		if (dup2(fd, 1) < 0) {
			ldmsd_log(LDMSD_LERROR, "Cannot redirect log to %s\n",
						cmd_line_args.log_path);
			cleanup(10, "error redirecting stdout");
		}
		if (dup2(fd, 2) < 0) {
			ldmsd_log(LDMSD_LERROR, "Cannot redirect log to %s\n",
						cmd_line_args.log_path);
			cleanup(11, "error redirecting stderr");
		}
		stdout = f;
		stderr = f;
	}
	return f;
}

int ldmsd_logrotate() {
	int rc;
	if (!cmd_line_args.log_path) {
		ldmsd_log(LDMSD_LERROR, "Received a logrotate command but "
			"the log messages are printed to the standard out.\n");
		return EINVAL;
	}
	if (log_fp == LDMSD_LOG_SYSLOG) {
		/* nothing to do */
		return 0;
	}
	struct timeval tv;
	char ofile_name[PATH_MAX];
	gettimeofday(&tv, NULL);
	sprintf(ofile_name, "%s-%ld", cmd_line_args.log_path, tv.tv_sec);

	pthread_mutex_lock(&log_lock);
	if (!log_fp) {
		pthread_mutex_unlock(&log_lock);
		return EINVAL;
	}
	fflush(log_fp);
	fclose(log_fp);
	rename(cmd_line_args.log_path, ofile_name);
	log_fp = fopen_perm(cmd_line_args.log_path, "a", LDMSD_DEFAULT_FILE_PERM);
	if (!log_fp) {
		printf("%-10s: Failed to rotate the log file. Cannot open a new "
			"log file\n", "ERROR");
		fflush(stdout);
		rc = errno;
		goto err;
	}
	int fd = fileno(log_fp);
	if (dup2(fd, 1) < 0) {
		rc = errno;
		goto err;
	}
	if (dup2(fd, 2) < 0) {
		rc = errno;
		goto err;
	}
	stdout = stderr = log_fp;
	pthread_mutex_unlock(&log_lock);
	return 0;
err:
	pthread_mutex_unlock(&log_lock);
	return rc;
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
	printf("    -u name	   List named plugin if available, and where possible its usage,\n");
	printf("		   then exit. Name all, sampler, and store limit output.[CMD LINE ONLY]\n");
	printf("    -c path	   The path to configuration file (optional, default: <none>).\n");
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
int ev_thread_count = 1;
ovis_scheduler_t *ovis_scheduler;
pthread_t *ev_thread;		/* sampler threads */
int *ev_count;			/* number of hosts/samplers assigned to each thread */

int find_least_busy_thread()
{
	int i;
	int idx = 0;
	int count = ev_count[0];
	for (i = 1; i < ev_thread_count; i++) {
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
	data_addr = (struct ldms_data_hdr *)((unsigned char*)meta_addr + sh->meta_sz);
	rc = ldms_mmap_set(meta_addr, data_addr, &map_set);
	if (rc) {
		munmap(meta_addr, set_size);
		ldmsd_lerror("Error %d mmapping the set '%s'\n", rc, set_name);
		return;
	}
	sh = meta_addr;
	sprintf(sh->producer_name, "%s", cmd_line_args.myhostname);
}

pthread_t k_thread;
void *k_proc(void *arg)
{
	int rc, map_fd;
	int i, j;
	int set_no;
	int set_size;
	char set_name[128];
	FILE *fp;
	union kldms_req k_req;

	fp = fopen(cmd_line_args.kernel_setfile, "r");
	if (!fp) {
		ldmsd_lerror("The specified kernel metric set file '%s' "
			     "could not be opened.\n", cmd_line_args.kernel_setfile);
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
		errno = rc;
		return LDMSD_TASK_STATE_STARTED;
	}
	return LDMSD_TASK_STATE_STOPPED;
}

static void task_cb_fn(ovis_event_t ev)
{
	ldmsd_task_t task = ev->param.ctxt;
	enum ldmsd_task_state next_state;

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
 out:
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

char *ldmsd_get_max_mem_sz_str()
{
	return cmd_line_args.mem_sz_str;
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

	producer = ev_worker_new("producer", default_actor);
	updater = ev_worker_new("updater", default_actor);
	sampler = ev_worker_new("sampler", default_actor);
	storage = ev_worker_new("storage", default_actor);

	ev_dispatch(sampler, smplr_sample_type, sample_actor);
	ev_dispatch(updater, prdcr_set_update_type, prdcr_set_update_actor);
	ev_dispatch(updater, prdcr_set_state_type, prdcr_set_state_actor);
	ev_dispatch(updater, prdcr_start_type, prdcr_start_actor);
	ev_dispatch(updater, prdcr_stop_type, prdcr_stop_actor);
	ev_dispatch(producer, prdcr_connect_type, prdcr_connect_actor);
	ev_dispatch(producer, updtr_start_type, updtr_start_actor);
	ev_dispatch(producer, updtr_stop_type, updtr_stop_actor);
}

void ldmsd_thread_count_set(char *str)
{
	int cnt;
	cnt = atoi(str);
	if (cnt < 1 )
		cnt = 1;
	if (cnt > EVTH_MAX)
		cnt = EVTH_MAX;
	cmd_line_args.ev_thread_count = cnt;
}

int ldmsd_auth_opt_add(struct attr_value_list *auth_attrs, char *name, char *val)
{
	struct attr_value *attr;
	attr = &(auth_attrs->list[auth_attrs->count]);
	if (auth_attrs->count == auth_attrs->size) {
		ldmsd_log(LDMSD_LERROR, "Too many auth options\n");
		return EINVAL;
	}
	attr->name = strdup(name);
	if (!attr->name)
		return ENOMEM;
	attr->value = strdup(val);
	if (!attr->value)
		return ENOMEM;
	auth_attrs->count++;
	return 0;
}

struct attr_value_list *ldmsd_auth_opts_str2avl(const char *auth_opts_s)
{
	struct attr_value_list *avl;
	char *ptr, *name, *value, *s;

	errno = 0;
	s = strdup(auth_opts_s);
	if (!s) {
		errno = ENOMEM;
		return NULL;
	}
	avl = av_new(LDMSD_AUTH_OPT_MAX);
	if (!avl) {
		errno = ENOMEM;
		goto out;
	}
	name = strtok_r(s, " \t", &ptr);
	while (name) {
		value = strchr(name, '=');
		if (value) {
			value[0] = '\0';
			value++;
		} else {
			errno = EINVAL;
			goto err;
		}
		ldmsd_auth_opt_add(avl, name, value);
		name = strtok_r(NULL, " \t", &ptr);
	}
	goto out;
err:
	av_free(avl);
	avl = NULL;
out:
	free(s);
	return avl;
}

void ldmsd_listen___del(ldmsd_cfgobj_t obj)
{
	ldmsd_listen_t listen = (ldmsd_listen_t)obj;
	if (listen->x)
		ldms_xprt_put(listen->x);
	if (listen->xprt)
		free(listen->xprt);
	if (listen->host)
		free(listen->host);
	if (listen->auth_name)
		free(listen->auth_name);
	if (listen->auth_attrs)
		av_free(listen->auth_attrs);
	ldmsd_cfgobj___del(obj);
}

ldmsd_listen_t ldmsd_listen_new(char *xprt, char *port, char *host,
		char *auth, struct attr_value_list *auth_args)
{
	char *name;
	size_t cnt, len;
	char *lval, *delim;
	struct ldmsd_sec_ctxt sctxt;
	struct ldmsd_listen *listen = NULL;

	if (!port)
		port = TOSTRING(LDMS_DEFAULT_PORT);

	len = strlen(xprt) + strlen(port) + 2; /* +1 for ':' and +1 for \0 */
	name = malloc(len);
	if (!name)
		return NULL;
	(void) snprintf(name, len, "%s:%s", xprt, port);
	listen = (struct ldmsd_listen *)
		ldmsd_cfgobj_new_with_auth(name, LDMSD_CFGOBJ_LISTEN,
				sizeof *listen, ldmsd_listen___del,
				getuid(), getgid(), 0550); /* No one can alter it */
	if (!listen)
		return NULL;

	listen->xprt = strdup(xprt);
	if (!listen->xprt)
		goto err;
	listen->port_no = atoi(port);
	if (host) {
		listen->host = strdup(host);
		if (!listen->host)
			goto err;
	}
	if (auth) {
		listen->auth_name = strdup(auth);
		if (!listen->auth_name)
			goto err;
		if (auth_args) {
			listen->auth_attrs = av_copy(auth_args);
			if (!listen->auth_attrs)
				goto err;
		}
	}
	ldmsd_cfgobj_unlock(&listen->obj);
	return listen;
err:
	errno = ENOMEM;
	ldmsd_cfgobj_unlock(&listen->obj);
	ldmsd_cfgobj_put(&listen->obj);
	return NULL;
}

/*
 * \return EPERM if the value is already given.
 *
 * The command-line options processed in the function
 * can be specified both at the command line and in configuration files.
 */
int ldmsd_process_cmd_line_arg(char opt, char *value)
{
	char *lval, *rval;
	int op, rc;
	ldmsd_listen_t listen;
	switch (opt) {
	case 'A':
		/* (multiple) auth options */
		lval = strtok(value, "=");
		if (!lval) {
			printf("ERROR: Expecting -A name=value\n");
			return EINVAL;
		}
		rval = strtok(NULL, "");
		if (!rval) {
			printf("ERROR: Expecting -A name=value\n");
			return EINVAL;
		}
		rc = ldmsd_auth_opt_add(cmd_line_args.auth_attrs, lval, rval);
		if (rc)
			return rc;
		break;
	case 'a':
		/* auth name */
		if (cmd_line_args.auth_name) {
			ldmsd_log(LDMSD_LERROR, "Default-auth was already "
					"specified to %s.\n",
					cmd_line_args.auth_name);
			return EPERM;
		} else {
			cmd_line_args.auth_name = strdup(value);
		}
		break;
	case 'B':
		if (check_arg("B", value, LO_UINT))
			return EINVAL;
		cmd_line_args.banner = atoi(value);
		break;
	case 'c':
		/*
		 * Must be specified at the command line.
		 * Handle separately in the main() function.
		 */
		break;
	case 'F':
		/*
		 * Must be specified at the command line.
		 * Handle separately in the main() function.
		 */
		break;
	case 'H':
		if (check_arg("H", value, LO_NAME))
			return EINVAL;
		if (cmd_line_args.myhostname[0] != '\0') {
			ldmsd_log(LDMSD_LERROR, "LDMSD hostname was already "
				"specified to %s\n", cmd_line_args.myhostname);
			return EPERM;
		} else {
			snprintf(cmd_line_args.myhostname,
				sizeof(cmd_line_args.myhostname), "%s", value);
		}
		break;
	case 'k':
		cmd_line_args.do_kernel = 1;
		break;
	case 'l':
		if (check_arg("l", value, LO_PATH))
			return EINVAL;
		if (cmd_line_args.log_path) {
			ldmsd_log(LDMSD_LERROR, "The log path is already "
					"specified to %s\n",
					cmd_line_args.log_path);
			return EPERM;
		} else {
			cmd_line_args.log_path = strdup(value);
			log_fp = ldmsd_open_log();
		}
		break;
	case 'm':
		if (cmd_line_args.mem_sz_str) {
			ldmsd_log(LDMSD_LERROR, "The memory limit was already "
					"set to '%s'\n",
					cmd_line_args.mem_sz_str);
			return EPERM;
		} else {
			cmd_line_args.mem_sz_str = strdup(value);
			if (!cmd_line_args.mem_sz_str) {
				ldmsd_log(LDMSD_LERROR, "Out of memory\n");
				return ENOMEM;
			}
		}
		break;
	case 'n':
		if (cmd_line_args.daemon_name[0] != '\0') {
			ldmsd_log(LDMSD_LERROR, "LDMSD daemon name was "
					"already set to %s\n",
					cmd_line_args.daemon_name);
			return EPERM;
		} else {
			snprintf(cmd_line_args.daemon_name,
				sizeof(cmd_line_args.daemon_name), "%s", value);
		}
		break;
	case 'P':
		if (check_arg("P", value, LO_UINT))
			return 1;
		if (cmd_line_args.ev_thread_count > 0) {
			ldmsd_log(LDMSD_LERROR, "LDMSD number of worker threads "
					"was already set to %d\n",
					cmd_line_args.ev_thread_count);
			return EPERM;
		} else {
			ldmsd_thread_count_set(value);
		}
		break;
	case 'r':
		if (check_arg("r", value, LO_PATH))
			return 1;
		if (cmd_line_args.pidfile) {
			ldmsd_log(LDMSD_LERROR, "The pidfile is already "
					"specified to %s\n",
					cmd_line_args.pidfile);
			return EPERM;
		} else {
			cmd_line_args.pidfile = strdup(value);
		}
		break;

	case 's':
		if (check_arg("s", value, LO_PATH))
			return 1;
		if (cmd_line_args.kernel_setfile) {
			ldmsd_log(LDMSD_LERROR, "The kernel set file is already "
					"specified to %s\n",
					cmd_line_args.kernel_setfile);
			return EPERM;
		} else {
			cmd_line_args.kernel_setfile = strdup(value);
		}
		break;
	case 'v':
		if (check_arg("v", value, LO_NAME))
			return 1;
		rc = ldmsd_loglevel_set(value);
		if (rc < 0) {
			printf("Invalid verbosity levels '%s'. "
				"See -v option.\n", value);
			return 1;
		}
		break;
	case 'x':
		if (check_arg("x", value, LO_NAME))
			return 1;
		rval = strchr(value, ':');
		if (!rval) {
			printf("Bad xprt format, expecting XPRT:PORT, "
					"but got: %s\n", value);
			return EINVAL;
		}
		rval[0] = '\0';
		rval = rval+1;
		/* Use the default auth */
		listen = ldmsd_listen_new(value, rval, NULL, NULL, NULL);
		if (!listen) {
			rc = errno;
			printf("Error %d: failed to add listening endpoint: %s:%s\n",
					rc, value, rval);
			return rc;
		}
		break;
	case '?':
		printf("Error: unknown argument: %c\n", opt);
	default:
		return ENOENT;
	}
	return 0;
}

void handle_pidfile_banner()
{
	struct ldms_version ldms_v;
	struct ldmsd_version ldmsd_v;

	ldms_version_get(&ldms_v);
	ldmsd_version_get(&ldmsd_v);

	char *pidfile;
	if (!cmd_line_args.foreground) {
		/* Create pidfile for daemon that usually goes away on exit. */
		/* user arg, then env, then default to get pidfile name */
		if (!cmd_line_args.pidfile) {
			char *pidpath = getenv("LDMSD_PIDFILE");
			if (!pidpath) {
				pidfile = malloc(strlen(LDMSD_PIDFILE_FMT)
						+ strlen(basename(progname) + 1));
				if (pidfile)
					sprintf(pidfile, LDMSD_PIDFILE_FMT, basename(progname));
			} else {
				pidfile = strdup(pidpath);
			}
			if (!pidfile) {
				ldmsd_log(LDMSD_LERROR, "Out of memory\n");
				exit(1);
			}
			cmd_line_args.pidfile = pidfile;
		}
		if (!access(pidfile, F_OK)) {
			ldmsd_log(LDMSD_LERROR, "Existing pid file named '%s': %s\n",
				pidfile, "overwritten if writable");
		}
		FILE *pfile = fopen_perm(pidfile,"w", LDMSD_DEFAULT_FILE_PERM);
		if (!pfile) {
			int piderr = errno;
			ldmsd_log(LDMSD_LERROR, "Could not open the pid file named '%s': %s\n",
				pidfile, strerror(piderr));
			free(pidfile);
			pidfile = NULL;
		} else {
			pid_t mypid = getpid();
			fprintf(pfile,"%ld\n", (long)mypid);
			fclose(pfile);
		}
		if (pidfile && cmd_line_args.banner) {
			char *suffix = ".version";
			bannerfile = malloc(strlen(suffix) + strlen(pidfile) + 1);
			if (!bannerfile) {
				ldmsd_log(LDMSD_LCRITICAL, "Memory allocation failure.\n");
				exit(1);
			}
			sprintf(bannerfile, "%s%s", pidfile, suffix);
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

				if (0 == strcmp(cmd_line_args.auth_name, "none"))
					fprintf(bfile, BANNER_PART1_NOA BANNER_PART2);
				else
					fprintf(bfile, BANNER_PART1_A BANNER_PART2);
				fclose(bfile);
			}
		}
	}
}

void cmd_line_value_init()
{
	cmd_line_args.auth_attrs = av_new(LDMSD_AUTH_OPT_MAX);
	if (!cmd_line_args.auth_attrs)
		cleanup(ENOMEM, "Memory allocation failure.");

	cmd_line_args.verbosity = LDMSD_VERBOSITY_DEFAULT;
	cmd_line_args.banner = -1;
}

void ldmsd_init()
{
	int rc, i;
	size_t mem_sz;

	if (!cmd_line_args.mem_sz_str) {
		cmd_line_args.mem_sz_str = getenv(LDMSD_MEM_SIZE_ENV);
		if (!cmd_line_args.mem_sz_str) {
			cmd_line_args.mem_sz_str = strdup(LDMSD_MEM_SIZE_DEFAULT);
			if (!cmd_line_args.mem_sz_str)
				cleanup(ENOMEM, "Memory allocation failure.");
		}

	}
	if ((mem_sz = ovis_get_mem_size(cmd_line_args.mem_sz_str)) == 0) {
		printf("Invalid memory size '%s'. See the -m option.\n",
						cmd_line_args.mem_sz_str);
		usage();
		cleanup(EINVAL, "invalid -m value");
	}
	if (ldms_init(mem_sz)) {
		ldmsd_log(LDMSD_LCRITICAL, "LDMS could not pre-allocate "
				"the memory of size %s.\n",
				cmd_line_args.mem_sz_str);
		cleanup(ENOMEM, "Memory allocation failure.");
	}

	/*
	 * No default authentication was specified.
	 * Set the default authentication method to 'none'.
	 */
	if (!cmd_line_args.auth_name)
		cmd_line_args.auth_name = strdup("none");

	if (cmd_line_args.myhostname[0] == '\0') {
		rc = gethostname(cmd_line_args.myhostname,
				sizeof(cmd_line_args.myhostname));
		if (rc)
			cmd_line_args.myhostname[0] = '\0';
	}

	if (cmd_line_args.daemon_name[0] == '\0') {
		ldmsd_listen_t listen = (ldmsd_listen_t)
					ldmsd_cfgobj_first(LDMSD_CFGOBJ_LISTEN);
		if (listen) {
			snprintf(cmd_line_args.daemon_name,
					sizeof(cmd_line_args.daemon_name),
					"%s:%hu", cmd_line_args.myhostname,
					listen->port_no);
		} else {
			snprintf(cmd_line_args.daemon_name,
					sizeof(cmd_line_args.daemon_name),
					"%s:", cmd_line_args.myhostname);
		}
	}

	/* Initialize event threads and event schedulers */
	if (cmd_line_args.ev_thread_count == 0)
		cmd_line_args.ev_thread_count = LDMSD_EV_THREAD_CNT_DEFAULT;
	ev_count = calloc(cmd_line_args.ev_thread_count, sizeof(int));
	if (!ev_count)
		cleanup(ENOMEM, "Memory allocation failure.");
	ovis_scheduler = calloc(cmd_line_args.ev_thread_count, sizeof(*ovis_scheduler));
	if (!ovis_scheduler)
		cleanup(ENOMEM, "Memory allocation failure.");
	ev_thread = calloc(cmd_line_args.ev_thread_count, sizeof(pthread_t));
	if (!ev_thread)
		cleanup(ENOMEM, "Memory allocation failure.");
	for (i = 0; i < cmd_line_args.ev_thread_count; i++) {
		ovis_scheduler[i] = ovis_scheduler_new();
		if (!ovis_scheduler[i]) {
			ldmsd_log(LDMSD_LERROR, "Error creating an OVIS scheduler.\n");
			cleanup(6, "OVIS scheduler create failed");
		}
		rc = pthread_create(&ev_thread[i], NULL, event_proc, ovis_scheduler[i]);
		if (rc) {
			ldmsd_log(LDMSD_LERROR, "Error %d creating the event "
					"thread.\n", rc);
			cleanup(7, "event thread create fail");
		}
		pthread_setname_np(ev_thread[i], "ldmsd:scheduler");
	}

	/* handle kernel sets */
	if (!cmd_line_args.kernel_setfile) {
		cmd_line_args.kernel_setfile = strdup(LDMSD_SETFILE);
		if (!cmd_line_args.kernel_setfile)
			cleanup(ENOMEM, "Memory allocation failure.");
	}
	if (cmd_line_args.do_kernel && publish_kernel(cmd_line_args.kernel_setfile))
		cleanup(3, "start kernel sampler failed");

	if (cmd_line_args.banner < 0)
		cmd_line_args.banner = LDMSD_BANNER_DEFAULT;

	is_ldmsd_initialized = 1;
}

void handle_listening_endpoints()
{
	struct ldmsd_listen *listen;
	int rc;
	for (listen = (ldmsd_listen_t)ldmsd_cfgobj_first(LDMSD_CFGOBJ_LISTEN);
		listen; listen = (ldmsd_listen_t)ldmsd_cfgobj_next(&listen->obj)) {
		rc = listen_on_ldms_xprt(listen);
		if (rc)
			cleanup(rc, "error listening on transport");
	}
}


int main(int argc, char *argv[])
{
#ifdef DEBUG
	mtrace();
#endif /* DEBUG */
	progname = argv[0];
	struct ldms_version ldms_version;
	struct ldmsd_version ldmsd_version;
	ldms_version_get(&ldms_version);
	ldmsd_version_get(&ldmsd_version);
	int ret;
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
	cmd_line_value_init();

	/* Process the options given at the command line. */
	opterr = 0;
	while ((op = getopt(argc, argv, FMT)) != -1) {
		switch (op) {
		case 'F':
			cmd_line_args.foreground = 1;
			break;
		case 'u':
			if (check_arg("u", optarg, LO_NAME))
				return 1;
			ldmsd_plugins_usage(optarg);
			exit(0);
			break;
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
		default:
			ret = ldmsd_process_cmd_line_arg(op, optarg);
			if (ret) {
				if (ret == ENOENT)
					usage(argv);
				cleanup(ret, "");
			}

			break;
		}
	}

	if (!cmd_line_args.foreground) {
		if (daemon(1, 1)) {
			perror("ldmsd: ");
			cleanup(8, "daemon failed to start");
		}
	}

	/* Initialize LDMSD events */
	ldmsd_ev_init();

	umask(0);
	/* Process configuration files */
	opterr = 0;
	optind = 0;
	while ((op = getopt(argc, argv, FMT)) != -1) {
		char *dup_arg;
		int lln = -1;
		switch (op) {
		case 'c':
			dup_arg = strdup(optarg);
			ret = process_config_file(dup_arg, &lln, 1);
			free(dup_arg);
			if (ret) {
				char errstr[128];
				snprintf(errstr, sizeof(errstr),
					 "Error %d processing configuration file '%s'",
					 ret, optarg);
				cleanup(ret, errstr);
			}
			ldmsd_log(LDMSD_LINFO,
				"Processing the config file '%s' is done.\n", optarg);
			break;
		}
	}

	ldmsd_init(argv);

	handle_pidfile_banner();
	handle_listening_endpoints();

	if (ldmsd_use_failover) {
		/* failover will be the one starting cfgobjs */
		ret = ldmsd_failover_start();
		if (ret) {
			ldmsd_log(LDMSD_LERROR,
				  "failover_start failed, rc: %d\n", ret);
			cleanup(100, "failover start failed");
		}
	} else {
		/* we can start cfgobjs right away */
		ret = ldmsd_ourcfg_start_proc();
		if (ret) {
			ldmsd_log(LDMSD_LERROR,
				  "config start failed, rc: %d\n", ret);
			cleanup(100, "config start failed");
		}
		ldmsd_linfo("Enabling in-band config\n");
		ldmsd_inband_cfg_mask_add(0777);
	}

	/* Keep the process alive */
	do {
		usleep(LDMSD_KEEP_ALIVE_30MIN);
	} while (1);

	cleanup(0,NULL);
	return 0;
}
