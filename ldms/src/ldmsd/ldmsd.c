/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010-2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2010-2019 Open Grid Computing, Inc. All rights reserved.
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
#include "ovis_ev/ev.h"
#include "ldms.h"
#include "ldmsd.h"
#include "ldms_xprt.h"
#include "ldmsd_request.h"
#include "ldmsd_event.h"
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

const char *short_opts = "B:l:s:x:P:m:Fkr:v:Vc:y:u:a:A:n:tL:";

struct option long_opts[] = {
	{ "default_auth_args",     required_argument, 0,  'A' },
	{ "default_auth",          required_argument, 0,  'a' },
	{ "banner",                required_argument, 0,  'B' },
	{ "publish_kernel",        optional_argument, 0,  'k' },
	{ "log_file",              required_argument, 0,  'l' },
	{ "set_memory",            required_argument, 0,  'm' },
	{ "daemon_name",           required_argument, 0,  'n' },
	{ "worker_threads",        required_argument, 0,  'P' },
	{ "pid_file",              required_argument, 0,  'r' },
	{ "kernel_file",           required_argument, 0,  's' },
	{ "log_level",             required_argument, 0,  'v' },
	{ "log_config",            required_argument, 0,  'L' },
	{ 0,                       0,                 0,  0 }
};

#define LDMSD_KEEP_ALIVE_30MIN 30*60*1000000 /* 30 mins */

#define LDMSD_MEM_SIZE_ENV "LDMSD_MEM_SZ"
#define LDMSD_MEM_SIZE_STR "512kB"
#define LDMSD_MEM_SIZE_DEFAULT 512L * 1024L

char *progname;
char myname[512]; /* name to identify ldmsd */
		  /* NOTE: fqdn limit: 255 characters */
		  /* DEFAULT: myhostname:port */
char myhostname[80];
char ldmstype[20];
int foreground;
int cfg_cntr = 0;
pthread_t event_thread = (pthread_t)-1;
char *logfile;
int log_truncate = 0;
char *pidfile;
char *bannerfile;

#define DEFAULT_BANNER 1
int banner = -1;
size_t max_mem_size;
char *max_mem_sz_str;

#define DEFAULT_AUTH_NAME "none"
const char *auth_name;
struct attr_value_list *auth_opt = NULL;
const int AUTH_OPT_MAX = 128;
int log_level_thr = LDMSD_LERROR;
int is_loglevel_thr_set; /* set to 1 when the log_level_thr is specified by users */

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

int do_kernel = 0;
char *setfile = NULL;

static int set_cmp(void *a, const void *b)
{
	return strcmp(a, b);
}

static struct rbt set_tree = {
		.root = 0,
		.comparator = set_cmp,
};
static pthread_mutex_t set_tree_lock = PTHREAD_MUTEX_INITIALIZER;

int find_least_busy_thread();

int passive = 0;
int quiet = 0; /* Is verbosity quiet? 0 for no and 1 for yes */

uint8_t is_ldmsd_initialized = 0;

uint8_t ldmsd_is_initialized()
{
	return is_ldmsd_initialized;
}

const char* ldmsd_loglevel_names[] = {
	LOGLEVELS(LDMSD_STR_WRAP)
	NULL
};

void ldmsd_sec_ctxt_get(ldmsd_sec_ctxt_t sctxt)
{
	sctxt->crd.gid = getegid();
	sctxt->crd.uid = geteuid();
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
	log_level_thr = level;
	return 0;
}

enum ldmsd_loglevel ldmsd_loglevel_get()
{
	return log_level_thr;
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

static int log_time_sec = -1;
int __logrotate()
{
	int rc;
	if (!logfile) {
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
	sprintf(ofile_name, "%s-%ld", logfile, tv.tv_sec);

	fflush(log_fp);
	fclose(log_fp);
	rename(logfile, ofile_name);
	log_fp = fopen_perm(logfile, "a", LDMSD_DEFAULT_FILE_PERM);
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
	return 0;
err:
	return rc;
}

int __log(enum ldmsd_loglevel level, char *msg, struct timeval *tv, struct tm *tm)
{
	if (log_fp == LDMSD_LOG_SYSLOG) {
		syslog(ldmsd_loglevel_to_syslog(level), "%s", msg);
		return 0;
	}

	if (log_time_sec) {
		fprintf(log_fp, "%lu.%06lu: ", tv->tv_sec, tv->tv_usec);
	} else {
		char dtsz[200];
		if (strftime(dtsz, sizeof(dtsz), "%a %b %d %H:%M:%S %Y", tm))
			fprintf(log_fp, "%s: ", dtsz);
	}

	if (level < LDMSD_LALL) {
		fprintf(log_fp, "%-10s: ", ldmsd_loglevel_names[level]);
	}

	fprintf(log_fp, "%s", msg);

	return 0;
}

int log_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t ev)
{
	enum ldmsd_loglevel level = EV_DATA(ev, struct log_data)->level;
	char *msg = EV_DATA(ev, struct log_data)->msg;
	uint8_t is_rotate = EV_DATA(ev, struct log_data)->is_rotate;
	struct timeval *tv = &EV_DATA(ev, struct log_data)->tv;
	struct tm *tm = &EV_DATA(ev, struct log_data)->tm;
	int rc;

	if (is_rotate) {
		rc = __logrotate();
	} else {
		rc = __log(level, msg, tv, tm);
		if (0 == ev_pending(logger_w) && log_fp != LDMSD_LOG_SYSLOG)
			fflush(log_fp);
		free(msg);
	}
	ev_put(ev);
	return rc;
}

void __ldmsd_log(enum ldmsd_loglevel level, const char *fmt, va_list ap)
{
	ev_t log_ev;
	char *msg;
	int rc;
	struct timeval tv;
	struct tm tm;
	time_t t;

	if ((level != LDMSD_LALL) &&
			(quiet || ((0 <= level) && (level < log_level_thr))))
		return;

	if (log_time_sec == -1) {
		char * lt = getenv("LDMSD_LOG_TIME_SEC");
		if (lt)
			log_time_sec = 1;
		else
			log_time_sec = 0;
	}
	if (log_time_sec) {
		gettimeofday(&tv, NULL);

	} else {
		t = time(NULL);
		localtime_r(&t, &tm);
	}

	rc = vasprintf(&msg, fmt, ap);
	if (rc < 0)
		return;

	if (!ldmsd_is_initialized()) {
		/* No workers, so directly log to the file */
		(void) __log(level, msg, &tv, &tm);
		free(msg);
		return;
	}
	log_ev = ev_new(log_type);
	if (!log_ev)
		return;
	EV_DATA(log_ev, struct log_data)->msg = msg;
	EV_DATA(log_ev, struct log_data)->level = level;
	EV_DATA(log_ev, struct log_data)->is_rotate = 0;

	if (log_time_sec)
		EV_DATA(log_ev, struct log_data)->tv = tv;
	else
		EV_DATA(log_ev, struct log_data)->tm = tm;
	ev_post(NULL, logger_w, log_ev, NULL);
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

void ldmsd_inc_cfg_cntr()
{
	__atomic_fetch_add(&cfg_cntr, 1, __ATOMIC_SEQ_CST);
}

int ldmsd_cfg_cntr_get()
{
	return cfg_cntr;
}

const char *ldmsd_myname_get()
{
	return myname;
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

extern void ldmsd_strgp_close();

static pthread_mutex_t cleanup_lock = PTHREAD_MUTEX_INITIALIZER;
static int cleaned;
void cleanup(int x, const char *reason)
{
	pthread_mutex_lock(&cleanup_lock);
	if (cleaned) {
		pthread_mutex_unlock(&cleanup_lock);
		exit(x);
	}
	int llevel = LDMSD_LINFO;
	if (x)
		llevel = LDMSD_LCRITICAL;
	ldmsd_mm_status(LDMSD_LDEBUG,"mmap use at exit");
	ldmsd_strgp_close();

	if (!quiet && (llevel >= log_level_thr)) {
		/*
		 * The logger and the log file may not be created and opened
		 * at the time the cleanup() function is called.
		 */
		ldmsd_log(LDMSD_LALL, "LDMSD_ LDMS Daemon exiting...status %d, %s\n", x,
			       (reason && x) ? reason : "");
	}

	if (ldms) {
		/* No need to close the xprt. It has never been connected. */
		ldms_xprt_put(ldms);
		ldms = NULL;
	}

	if (!foreground && pidfile) {
		unlink(pidfile);
		free(pidfile);
		pidfile = NULL;
		if (bannerfile) {
			if ( banner < 2) {
				unlink(bannerfile);
			}
			free(bannerfile);
			bannerfile = NULL;
		}
	}
	if (pidfile) {
		free(pidfile);
		pidfile = NULL;
	}
	if (!quiet && (llevel >= log_level_thr))
		ldmsd_log(LDMSD_LALL, "LDMSD_ cleanup end.\n");

	if (logfile) {
		free(logfile);
		logfile = NULL;
	}

	av_free(auth_opt);
	auth_opt = NULL;
	cleaned = 1;
	pthread_mutex_unlock(&cleanup_lock);
	exit(x);
}

/** return a file pointer or a special syslog pointer */
FILE *ldmsd_open_log()
{
	FILE *f;
	if (strcasecmp(logfile,"syslog")==0) {
		ldmsd_log(LDMSD_LDEBUG, "Switching to syslog.\n");
		f = LDMSD_LOG_SYSLOG;
		openlog(progname, LOG_NDELAY|LOG_PID, LOG_DAEMON);
		return f;
	}

	if (log_truncate) {
		int err = truncate(logfile, 0);
		if (err) {
			ldmsd_log(LDMSD_LERROR, "Could not truncate the log file named '%s'. errno=%d\n",
				logfile, errno);
			cleanup(12, "log truncate failed");
		}
	}

	f = fopen_perm(logfile, "a", LDMSD_DEFAULT_FILE_PERM);
	if (!f) {
		ldmsd_log(LDMSD_LERROR, "Could not open the log file named '%s'\n",
							logfile);
		errno = EINVAL;
		return NULL;
	} else {
		int fd = fileno(f);
		if (dup2(fd, 1) < 0) {
			ldmsd_log(LDMSD_LERROR, "Cannot redirect log to %s\n",
							logfile);
			errno = EINTR;
			return NULL;
		}
		if (dup2(fd, 2) < 0) {
			ldmsd_log(LDMSD_LERROR, "Cannot redirect log to %s\n",
							logfile);
			errno = EINTR;
			return NULL;
		}
		stdout = f;
		stderr = f;
	}
	return f;
}

int ldmsd_logrotate() {
	ev_t ev = ev_new(log_type);
	if (!ev)
		return ENOMEM;
	EV_DATA(ev, struct log_data)->is_rotate = 1;
	EV_DATA(ev, struct log_data)->msg = NULL;
	ev_post(NULL, logger_w, ev, NULL);
	return 0;
}

void cleanup_sa(int signal, siginfo_t *info, void *arg)
{
	ldmsd_log(LDMSD_LINFO, "signo : %d\n", info->si_signo);
	ldmsd_log(LDMSD_LINFO, "si_pid: %d\n", info->si_pid);
	cleanup(0, "signal to exit caught");
}


void usage_hint(char *argv[],char *hint)
{
	printf("%s: [%s]\n", argv[0], short_opts);
	printf("  General Options\n");
	printf("    -F                                            Foreground mode, don't daemonize the program [false].\n");
	printf("    -u name                                       List named plugin if available, and where possible\n");
	printf("                                                  its usage, then exit. Name all, sampler, and store limit output.\n");
	printf("    -B MODE,     --banner MODE                    Daemon mode banner file with pidfile [1].\n"
	       "                                                  modes:0-no banner file, 1-banner auto-deleted, 2-banner left.\n");
	printf("    -m SIZE,     --set_memory SIZE                Maximum size of pre-allocated memory for metric sets.\n"
	       "                                                  The given size must be less than 1 petabytes.\n"
	       "                                                  The default value is %s\n"
	       "                                                  For example, 20M or 20mb are 20 megabytes.\n"
	       "                                                  - The environment variable %s could be set instead of\n"
	       "                                                  giving the -m option. If both are given, the -m option\n"
	       "                                                  takes precedence over the environment variable.\n",
	                                                          LDMSD_MEM_SIZE_STR, LDMSD_MEM_SIZE_ENV);
	printf("    -n NAME,     --daemon_name NAME               The name of the daemon. By default, it is \"HOSTNAME:PORT\".\n");
	printf("                                                  The failover uses the daemon name to verify the buddy name.\n");
	printf("                                                  The producer name of kernel metric sets is the daemon name.\n");
	printf("    -r PATH,     --pid_file PATH                  The path to the pid file for daemon mode.\n"
	       "                                                  [" LDMSD_PIDFILE_FMT "]\n",basename(argv[0]));
	printf("  Log Verbosity Options\n");
	printf("    -l PATH,     --log_file PATH                  The path to the log file for status messages.\n"
	       "                                                  [" LDMSD_LOGFILE "]\n");
	printf("    -v LEVEL,    --log_level LEVEL                The available verbosity levels, in order of decreasing verbosity,\n"
	       "                                                  are DEBUG, INFO, ERROR, CRITICAL and QUIET.\n"
	       "                                                  The default level is ERROR.\n");
	printf("    -t,          --log_truncate                   Truncate the log file at start if the log file exists.\n");
	printf("    -L optlog, --log_config optlog                Log config commands; optlog is INT:PATH\n");
	printf("  Communication Options\n");
	printf("    -x xprt:port:host\n"
	       "                                                  Specifies the transport type to listen on. May be specified\n"
	       "                                                  more than once for multiple transports. The transport string\n"
	       "                                                  is one of 'rdma', 'sock', 'ugni', or 'fabric'.\n"
	       "                                                  A transport specific port number is optionally specified\n"
	       "                                                  following a ':', e.g. rdma:50000. Optional host name\n"
	       "                                                  or address may be given after the port, e.g. rdma:10000:node1-ib,\n"
	       "                                                  to listen to a specific address.\n");
	printf("    -a AUTH,      --default_auth AUTH             Transport authentication plugin (default: 'none')\n");
	printf("    -A KEY=VALUE, --default_auth_args KEY=VALUE   Authentication plugin options (repeatable)\n");
	printf("  Kernel Metric Options\n");
	printf("    -k,           --publish_kernel                Publish kernel metrics.\n");
	printf("    -s PATH,      --kernel_set_path PATH          Text file containing kernel metric sets to publish.\n"
	       "                                                  [" LDMSD_SETFILE "]\n");
	printf("  Thread Options\n");
	printf("    -P COUNT,     --worker_threads COUNT          Count of event threads to start.\n");
	printf("  Configuration Options\n");
	printf("    -c PATH                                       The path to configuration file (optional, default: <none>).\n");
	printf("    -y PATH                                       Path to YAML configuration file (optional, default: <none>).\n");
	printf("    -V                                            Print LDMS version and exit.\n");
	printf("  Deprecated options\n");
	printf("    -H                                            DEPRECATED.\n");
	if (hint) {
		printf("\nHINT: %s\n",hint);
	}
	cleanup(1, "usage provided");
}

void usage(char *argv[]) {
	usage_hint(argv,NULL);
}

#define EVTH_MAX 1024
int ev_thread_count = 0;
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
	sh = meta_addr;
	data_addr = (struct ldms_data_hdr *)((unsigned char*)meta_addr + sh->meta_sz);
	rc = ldms_mmap_set(meta_addr, data_addr, &map_set);
	if (rc) {
		munmap(meta_addr, set_size);
		ldmsd_lerror("Error %d mmapping the set '%s'\n", rc, set_name);
		return;
	}
	sh = meta_addr;
	snprintf(sh->producer_name, sizeof(sh->producer_name), "%.63s", ldmsd_myname_get());
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

	fp = fopen(setfile, "r");
	if (!fp) {
		ldmsd_lerror("The specified kernel metric set file '%s' "
			     "could not be opened.\n", setfile);
		cleanup(1, "Could not open kldms set file");
	}

	map_fd = open("/dev/kldms0", O_RDWR);
	if (map_fd < 0) {
		ldmsd_lerror("Error %d opening the KLDMS device file "
			     "'/dev/kldms0'\n", map_fd);
		cleanup(1, "Could not open the kernel device /dev/kldms0");
	}

	while (3 == fscanf(fp, "%d %d %127s", &set_no, &set_size, set_name)) {
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
	pthread_create(&k_thread, NULL, k_proc, (void *)setfile);
	return 0;
}

static void stop_sampler(struct ldmsd_plugin_cfg *pi)
{
	ovis_scheduler_event_del(pi->os, &pi->oev);
	release_ovis_scheduler(pi->thread_id);
	pi->ref_count--;
	pi->os = NULL;
	pi->thread_id = -1;
}

void plugin_sampler_cb(ovis_event_t oev)
{
	struct ldmsd_plugin_cfg *pi = oev->param.ctxt;
	pthread_mutex_lock(&pi->lock);
	assert(pi->plugin->type == LDMSD_PLUGIN_SAMPLER);
	int rc = pi->sampler->sample(pi->sampler);
	if (rc) {
		/*
		 * If the sampler reports an error don't reschedule
		 * the timeout. This is an indication of a configuration
		 * error that needs to be corrected.
		*/
		ldmsd_log(LDMSD_LERROR, "'%s': failed to sample. Stopping "
				"the plug-in.\n", pi->name);
		stop_sampler(pi);
	}
	pthread_mutex_unlock(&pi->lock);
}

void ldmsd_set_tree_lock()
{
	pthread_mutex_lock(&set_tree_lock);
}

void ldmsd_set_tree_unlock()
{
	pthread_mutex_unlock(&set_tree_lock);
}

/* Caller must hold the set tree lock. */
ldmsd_plugin_set_list_t ldmsd_plugin_set_list_first()
{
	struct rbn *rbn;

	rbn = rbt_min(&set_tree);
	if (!rbn)
		return NULL;
	return container_of(rbn, struct ldmsd_plugin_set_list, rbn);
}

ldmsd_plugin_set_list_t ldmsd_plugin_set_list_next(ldmsd_plugin_set_list_t list)
{
	struct rbn *rbn;
	rbn = rbn_succ(&list->rbn);
	if (!rbn)
		return NULL;
	return container_of(rbn, struct ldmsd_plugin_set_list, rbn);
}

ldmsd_plugin_set_list_t ldmsd_plugin_set_list_find(const char *plugin_name)
{
	struct rbn *rbn;
	rbn = rbt_find(&set_tree, plugin_name);
	if (!rbn) {
		return NULL;
	}
	return container_of(rbn, struct ldmsd_plugin_set_list, rbn);
}

/* Caller must hold the set_tree lock */
ldmsd_plugin_set_t ldmsd_plugin_set_first(const char *plugin_name)
{
	struct rbn *rbn;
	ldmsd_plugin_set_list_t list;
	rbn = rbt_find(&set_tree, plugin_name);
	if (!rbn)
		return NULL;
	list = container_of(rbn, struct ldmsd_plugin_set_list, rbn);
	return LIST_FIRST(&list->list);
}

/* Caller must hold the set_tree lock */
ldmsd_plugin_set_t ldmsd_plugin_set_next(ldmsd_plugin_set_t set)
{
	return LIST_NEXT(set, entry);
}

int ldmsd_set_register(ldms_set_t set, const char *plugin_name)
{
	if (!set || ! plugin_name)
		return EINVAL;
	struct rbn *rbn;
	ldmsd_plugin_set_t s;
	ldmsd_plugin_set_list_t list;
	struct ldmsd_plugin_cfg *pi;
	int rc;

	s = malloc(sizeof(*s));
	if (!s)
		return ENOMEM;
	s->plugin_name = strdup(plugin_name);
	if (!s->plugin_name) {
		rc = ENOMEM;
		goto free_set;
	}
	s->inst_name = strdup(ldms_set_instance_name_get(set));
	if (!s->inst_name) {
		rc = ENOMEM;
		goto free_plugin;
	}
	s->set = ldms_set_by_name(ldms_set_instance_name_get(set));
	if (!s->set) {
		rc = ENOMEM;
		goto free_inst_name;
	}
	ldmsd_set_tree_lock();
	rbn = rbt_find(&set_tree, s->plugin_name);
	if (!rbn) {
		list = malloc(sizeof(*list));
		if (!list) {
			ldmsd_set_tree_unlock();
			ldms_set_put(s->set);
			rc = ENOMEM;
			goto free_inst_name;
		}
		char *pname = strdup(s->plugin_name);
		if (!pname) {
			free(list);
			ldmsd_set_tree_unlock();
			ldms_set_put(s->set);
			rc = ENOMEM;
			goto free_inst_name;
		}
		rbn_init(&list->rbn, pname);
		LIST_INIT(&list->list);
		rbt_ins(&set_tree, &list->rbn);
	} else {
		list = container_of(rbn, struct ldmsd_plugin_set_list, rbn);
	}
	LIST_INSERT_HEAD(&list->list, s, entry);
	ldmsd_set_tree_unlock();

	pi = ldmsd_get_plugin((char *)plugin_name);
	if (!pi) {
		ldmsd_set_deregister(s->inst_name, plugin_name);
		return EINVAL;
	}
	if (pi->plugin->type == LDMSD_PLUGIN_SAMPLER) {
		if (pi->sample_interval_us) {
			/* Add the update hint to the set_info */
			rc = ldmsd_set_update_hint_set(s->set,
					pi->sample_interval_us, pi->sample_offset_us);
			if (rc) {
				/* Leave the ldmsd plugin set in the tree, so return 0. */
				ldmsd_log(LDMSD_LERROR, "Error %d: Failed to add "
						"the update hint to set '%s'\n",
						rc, s->inst_name);
			}
		}
	}
	return 0;
free_inst_name:
	free(s->inst_name);
free_plugin:
	free(s->plugin_name);
free_set:
	free(s);
	return rc;
}

void ldmsd_set_deregister(const char *inst_name, const char *plugin_name)
{
	ldmsd_plugin_set_t set = NULL;
	ldmsd_plugin_set_list_t list;
	struct rbn *rbn;
	ldmsd_set_tree_lock();
	rbn = rbt_find(&set_tree, plugin_name);
	if (!rbn)
		goto out;
	list = container_of(rbn, struct ldmsd_plugin_set_list, rbn);
	LIST_FOREACH(set, &list->list, entry) {
		if (0 == strcmp(set->inst_name, inst_name))
			break;
	}
	if (set) {
		LIST_REMOVE(set, entry);
		free(set->inst_name);
		free(set->plugin_name);
		ldms_set_put(set->set);
		free(set);
	}
	if (LIST_EMPTY(&list->list)) {
		char *pname = list->rbn.key;
		rbt_del(&set_tree, &list->rbn);
		free(pname);
		free(list);
	}
out:
	ldmsd_set_tree_unlock();
}

int ldmsd_set_update_hint_set(ldms_set_t set, long interval_us, long offset_us)
{
	char value[128];
	/*
	 * offset_us can be equal to LDMSD_UPDT_HINT_OFFSET_NONE
	 * if the updater is in the asynchronous mode.
	 */
	if (offset_us == LDMSD_UPDT_HINT_OFFSET_NONE)
		snprintf(value, 127, "%ld:", interval_us);
	else
		snprintf(value, 127, "%ld:%ld", interval_us, offset_us);
	return ldms_set_info_set(set, LDMSD_SET_INFO_UPDATE_HINT_KEY, value);
}

int ldmsd_set_update_hint_get(ldms_set_t set, long *interval_us, long *offset_us)
{
	char *value, *tmp, *endptr;
	*interval_us = 0;
	*offset_us = LDMSD_UPDT_HINT_OFFSET_NONE;
	value = ldms_set_info_get(set, LDMSD_SET_INFO_UPDATE_HINT_KEY);
	if (!value)
		return 0;
	tmp = strtok_r(value, ":", &endptr);
	*interval_us = strtol(tmp, NULL, 0);
	tmp = strtok_r(NULL, ":", &endptr);
	if (tmp)
		*offset_us = strtol(tmp, NULL, 0);
	ldmsd_log(LDMSD_LDEBUG, "set '%s': getting updtr hint '%s'\n",
			ldms_set_instance_name_get(set), value);
	free(value);
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
	enum ldmsd_task_state next_state = 0;

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

void ldmsd_task_stop(ldmsd_task_t task)
{

	pthread_mutex_lock(&task->lock);
	if (task->state == LDMSD_TASK_STATE_STOPPED)
		goto out;
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

char *ldmsd_set_info_origin_enum2str(enum ldmsd_set_origin_type type)
{
	if (type == LDMSD_SET_ORIGIN_PRDCR)
		return "producer";
	else if (type == LDMSD_SET_ORIGIN_SAMP_PI)
		return "sampler plugin";
	else
		return "";
}

void __transaction_end_time_get(struct timespec *start, struct timespec *dur,
							struct timespec *end__)
{
	end__->tv_sec = start->tv_sec + dur->tv_sec;
	end__->tv_nsec = start->tv_nsec + dur->tv_nsec;
	if (end__->tv_nsec > 1000000000) {
		end__->tv_sec += 1;
		end__->tv_nsec -= 1000000000;
	}
}

/*
 * Get the set information
 *
 * When \c info is unused, ldmsd_set_info_delete() must be called to free \c info.
 */
ldmsd_set_info_t ldmsd_set_info_get(const char *inst_name)
{
	ldmsd_set_info_t info;
	struct ldms_timestamp t;
	struct timespec dur;
	struct ldmsd_plugin_set_list *plugn_set_list;
	struct ldmsd_plugin_set *plugn_set = NULL;
	struct ldmsd_plugin_cfg *pi;

	ldms_set_t lset = ldms_set_by_name(inst_name);
	if (!lset)
		return NULL;

	info = calloc(1, sizeof(*info));
	if (!info)
		return NULL;

	info->set = lset;
	/* Determine if the set is responsible by a sampler plugin */
	ldmsd_set_tree_lock();
	plugn_set_list = ldmsd_plugin_set_list_first();
	while (plugn_set_list) {
		LIST_FOREACH(plugn_set, &plugn_set_list->list, entry) {
			if (0 == strcmp(plugn_set->inst_name, inst_name)) {
				break;
			}
		}
		if (plugn_set)
			break;
 		plugn_set_list = ldmsd_plugin_set_list_next(plugn_set_list);
	}
	ldmsd_set_tree_unlock();
	if (plugn_set) {
		/* The set is created by a sampler plugin */
		pi = ldmsd_get_plugin(plugn_set->plugin_name);
		if (!pi) {
			ldmsd_log(LDMSD_LERROR, "Set '%s' is created by "
					"an unloaded plugin '%s'\n",
					inst_name, plugn_set->plugin_name);
		} else {
			pi->ref_count++;
			info->interval_us = pi->sample_interval_us;
			info->offset_us = pi->sample_offset_us;
			info->sync = 1; /* Sampling is always synchronous. */
			info->pi = pi;
		}
		info->origin_name = strdup(plugn_set->plugin_name);
		info->origin_type = LDMSD_SET_ORIGIN_SAMP_PI;

		t = ldms_transaction_timestamp_get(lset);
		info->start.tv_sec = (long int)t.sec;
		info->start.tv_nsec = (long int)t.usec * 1000;
		if (!ldms_set_is_consistent(lset)) {
			info->end.tv_sec = 0;
			info->end.tv_nsec = 0;
		} else {
			t = ldms_transaction_duration_get(lset);
			dur.tv_sec = (long int)t.sec;
			dur.tv_nsec = (long int)t.usec * 1000;
			__transaction_end_time_get(&info->start,
					&dur, &info->end);
		}
		goto out;
	}

	/*
	 * The set isn't created by a sampler plugin.
	 *
	 * Now search in the producer list.
	 */
	ldmsd_prdcr_t prdcr;
	ldmsd_prdcr_set_t prd_set = NULL;
	ldmsd_cfg_lock(LDMSD_CFGOBJ_PRDCR);
	prdcr = ldmsd_prdcr_first();
	while (prdcr) {
		ldmsd_prdcr_lock(prdcr);
		prd_set = ldmsd_prdcr_set_find(prdcr, inst_name);
		if (prd_set) {
			info->origin_name = strdup(prdcr->obj.name);
			ldmsd_prdcr_unlock(prdcr);
			ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR);
			info->origin_type = LDMSD_SET_ORIGIN_PRDCR;
			ldmsd_prdcr_set_ref_get(prd_set);
			info->prd_set = prd_set;
			info->interval_us = prd_set->updt_interval;
			info->offset_us = prd_set->updt_offset;
			info->sync = prd_set->updt_sync;
			info->start = prd_set->updt_stat.start;
			if (prd_set->state == LDMSD_PRDCR_SET_STATE_UPDATING) {
				info->end.tv_sec = 0;
				info->end.tv_nsec = 0;
			} else {
				info->end = prd_set->updt_stat.end;
			}
			goto out;
		}
		ldmsd_prdcr_unlock(prdcr);
		prdcr = ldmsd_prdcr_next(prdcr);
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR);
out:
	ldms_set_put(lset);
	return info;
}

/*
 * Delete the set information
 */
void ldmsd_set_info_delete(ldmsd_set_info_t info)
{
	if (info->set) {
		ldms_set_put(info->set);
		info->set = NULL;
	}
	if (info->origin_name) {
		free(info->origin_name);
		info->origin_name = NULL;
	}
	if ((info->origin_type == LDMSD_SET_ORIGIN_PRDCR) && info->prd_set) {
		ldmsd_prdcr_set_ref_put(info->prd_set);
		info->prd_set = NULL;
	}
	if (info->pi) {
		info->pi->ref_count--;
		info->pi = NULL;
	}
	free(info);
}

int __sampler_set_info_add(struct ldmsd_plugin *pi, long interval_us, long offset_us)
{
	ldmsd_plugin_set_t set;
	int rc;

	if (pi->type != LDMSD_PLUGIN_SAMPLER)
		return EINVAL;
	for (set = ldmsd_plugin_set_first(pi->name); set;
				set = ldmsd_plugin_set_next(set)) {
		rc = ldmsd_set_update_hint_set(set->set, interval_us, offset_us);
		if (rc) {
			ldmsd_log(LDMSD_LERROR, "Error %d: Failed to add "
					"the update hint to set '%s'\n",
					rc, ldms_set_instance_name_get(set->set));
			return rc;
		}
	}
	return 0;
}

/*
 * Start the sampler
 */
int ldmsd_start_sampler(char *plugin_name, char *interval, char *offset)
{
	int rc = 0;
	long sample_interval;
	long sample_offset = 0;
	struct ldmsd_plugin_cfg *pi;

	rc = ovis_time_str2us(interval, &sample_interval);
	if (rc)
		return rc;

	pi = ldmsd_get_plugin((char *)plugin_name);
	if (!pi)
		return ENOENT;

	pthread_mutex_lock(&pi->lock);
	if (pi->plugin->type != LDMSD_PLUGIN_SAMPLER) {
		rc = -EINVAL;
		goto out;
	}
	if (pi->thread_id >= 0) {
		rc = EBUSY;
		goto out;
	}

	pi->sample_interval_us = sample_interval;
	if (offset) {
		rc = ovis_time_str2us(offset, &sample_offset);
		if (rc) {
			rc = EDOM;
			goto out;
		}
		if ( !((sample_interval >= 10) &&
		       (sample_interval >= labs(sample_offset)*2)) ){
			rc = -EDOM;
			goto out;
		}
	}
	pi->sample_offset_us = sample_offset;

	rc = __sampler_set_info_add(pi->plugin, sample_interval,
					         sample_offset);
	if (rc)
		goto out;

	OVIS_EVENT_INIT(&pi->oev);
	pi->oev.param.type = OVIS_EVENT_PERIODIC;
	pi->oev.param.periodic.period_us = sample_interval;
	pi->oev.param.periodic.phase_us = sample_offset;
	pi->oev.param.ctxt = pi;
	pi->oev.param.cb_fn = plugin_sampler_cb;

	pi->ref_count++;

	pi->thread_id = find_least_busy_thread();
	pi->os = get_ovis_scheduler(pi->thread_id);
	rc = ovis_scheduler_event_add(pi->os, &pi->oev);
out:
	pthread_mutex_unlock(&pi->lock);
	return rc;
}

struct oneshot {
	struct ldmsd_plugin_cfg *pi;
	ovis_scheduler_t os;
	struct ovis_event_s oev;
};

void oneshot_sample_cb(ovis_event_t ev)
{
	struct oneshot *os = ev->param.ctxt;
	struct ldmsd_plugin_cfg *pi = os->pi;
	ovis_scheduler_event_del(os->os, ev);
	pthread_mutex_lock(&pi->lock);
	assert(pi->plugin->type == LDMSD_PLUGIN_SAMPLER);
	pi->sampler->sample(pi->sampler);
	pi->ref_count--;
	release_ovis_scheduler(pi->thread_id);
	free(os);
	pthread_mutex_unlock(&pi->lock);
}

int ldmsd_oneshot_sample(const char *plugin_name, const char *ts,
					char *errstr, size_t errlen)
{
	int rc = 0;
	struct ldmsd_plugin_cfg *pi;
	time_t now, sched;
	struct timeval tv;

	if (0 == strncmp(ts, "now", 3)) {
		ts = ts + 4;
		tv.tv_sec = strtoul(ts, NULL, 10);
	} else {
		sched = strtoul(ts, NULL, 10);
		now = time(NULL);
		if (now < 0) {
			snprintf(errstr, errlen, "Failed to get "
						"the current time.");
			rc = errno;
			return rc;
		}
		double diff = difftime(sched, now);
		if (diff < 0) {
			snprintf(errstr, errlen, "The schedule time '%s' "
				 "is ahead of the current time %jd",
				 ts, (intmax_t)now);
			rc = EINVAL;
			return rc;
		}
		tv.tv_sec = diff;
	}
	tv.tv_usec = 0;

	struct oneshot *ossample = malloc(sizeof(*ossample));
	if (!ossample) {
		snprintf(errstr, errlen, "Out of Memory");
		rc = ENOMEM;
		return rc;
	}

	pi = ldmsd_get_plugin((char *)plugin_name);
	if (!pi) {
		rc = ENOENT;
		snprintf(errstr, errlen, "Sampler not found.");
		free(ossample);
		return rc;
	}
	pthread_mutex_lock(&pi->lock);
	if (pi->plugin->type != LDMSD_PLUGIN_SAMPLER) {
		rc = EINVAL;
		snprintf(errstr, errlen,
				"The specified plugin is not a sampler.");
		goto err;
	}
	pi->ref_count++;
	ossample->pi = pi;
	if (pi->thread_id < 0) {
		snprintf(errstr, errlen, "Sampler '%s' not started yet.",
								plugin_name);
		rc = EPERM;
		goto err;
	}
	ossample->os = get_ovis_scheduler(pi->thread_id);
	OVIS_EVENT_INIT(&ossample->oev);
	ossample->oev.param.type = OVIS_EVENT_TIMEOUT;
	ossample->oev.param.ctxt = ossample;
	ossample->oev.param.cb_fn = oneshot_sample_cb;
	ossample->oev.param.timeout = tv;

	rc = ovis_scheduler_event_add(ossample->os, &ossample->oev);

	if (rc)
		goto err;
	goto out;
err:
	free(ossample);
out:
	pthread_mutex_unlock(&pi->lock);
	return rc;
}

/*
 * Stop the sampler
 */
int ldmsd_stop_sampler(char *plugin_name)
{
	int rc = 0;
	struct ldmsd_plugin_cfg *pi;

	pi = ldmsd_get_plugin(plugin_name);
	if (!pi)
		return ENOENT;
	pthread_mutex_lock(&pi->lock);
	/* Ensure this is a sampler */
	if (pi->plugin->type != LDMSD_PLUGIN_SAMPLER) {
		rc = EINVAL;
		goto out;
	}
	if (pi->os) {
		ovis_scheduler_event_del(pi->os, &pi->oev);
		pi->os = NULL;
		release_ovis_scheduler(pi->thread_id);
		pi->thread_id = -1;
		pi->ref_count--;
	} else {
		rc = -EBUSY;
	}
out:
	pthread_mutex_unlock(&pi->lock);
	return rc;
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
	return max_mem_sz_str;
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

ldmsd_listen_t ldmsd_listen_new(char *xprt, char *port, char *host, char *auth)
{
	char *name;
	int len;
	struct ldmsd_listen *listen = NULL;
	ldmsd_auth_t auth_dom = NULL;

	if (!port)
		port = LDMSD_STR_WRAP(LDMS_DEFAULT_PORT);

	len = asprintf(&name, "%s:%s:%s", xprt, port, host?host:"");
	if (len < 0) {
		errno = EINVAL;
		return NULL;
	}
	listen = (struct ldmsd_listen *)
		ldmsd_cfgobj_new_with_auth(name, LDMSD_CFGOBJ_LISTEN,
				sizeof *listen, ldmsd_listen___del,
				getuid(), getgid(), 0550); /* No one can alter it */
	free(name);
	if (!listen) {
		return NULL;
	}

	listen->xprt = strdup(xprt);
	if (!listen->xprt) {
		errno = ENOMEM;
		goto err;
	}
	listen->port_no = atoi(port);
	if (host) {
		listen->host = strdup(host);
		if (!listen->host) {
			errno = ENOMEM;
			goto err;
		}
	}

	if (auth) {
		auth_dom = ldmsd_auth_find(auth);
		if (!auth_dom) {
			ldmsd_log(LDMSD_LERROR, "Auth method '%s' unconfigured\n", auth);
			errno = ENOENT;
			goto err;
		}
		listen->auth_name = strdup(auth_dom->plugin);
		if (!listen->auth_name) {
			errno = ENOMEM;
			goto err;
		}
		listen->auth_dom_name = strdup(auth_dom->obj.name);
		if (!listen->auth_dom_name) {
			errno = ENOMEM;
			goto err;
		}
		if (auth_dom->attrs) {
			listen->auth_attrs = av_copy(auth_dom->attrs);
			if (!listen->auth_attrs) {
				errno = ENOMEM;
				goto err;
			}
		}
		if (auth_dom)
			ldmsd_cfgobj_put(&auth_dom->obj);
	}
	ldmsd_cfgobj_unlock(&listen->obj);
	return listen;
err:
	if (auth_dom)
		ldmsd_cfgobj_put(&auth_dom->obj);
	ldmsd_cfgobj_unlock(&listen->obj);
	ldmsd_cfgobj_put(&listen->obj);
	return NULL;
}

int __listen_auth_set(ldmsd_listen_t listen)
{
	listen->auth_name = strdup(auth_name);
	if (!listen->auth_name)
		return ENOMEM;
	listen->auth_attrs = av_copy(auth_opt);
	if (!listen->auth_attrs) {
		free(listen->auth_name);
		return ENOMEM;
	}
	return 0;
}

const char *ldmsd_auth_name_get(ldmsd_listen_t listen)
{
	if (!listen)
		return auth_name;
	if (!listen->auth_name) {
		if (__listen_auth_set(listen))
			return NULL;
	}
	return listen->auth_name;
}

struct attr_value_list *ldmsd_auth_attr_get(ldmsd_listen_t listen)
{
	if (!listen)
		return auth_opt;
	if (!listen->auth_name) {
		if (__listen_auth_set(listen))
			return NULL;
	}
	return listen->auth_attrs;
}

int ldmsd_listen_start(ldmsd_listen_t listen)
{
	int rc = 0;
	assert(NULL == listen->x);
	listen->x = ldms_xprt_new_with_auth(listen->xprt, ldmsd_linfo,
						ldmsd_auth_name_get(listen),
						ldmsd_auth_attr_get(listen));
	if (!listen->x) {
		rc = errno;
		char *args = av_to_string(listen->auth_attrs, AV_EXPAND);
		ldmsd_log(LDMSD_LERROR,
			  "'%s' transport creation with auth '%s' "
			  "failed, error: %s(%d). args='%s'. Please check transport "
			  "configuration, authentication configuration, "
			  "ZAP_LIBPATH (env var), and LD_LIBRARY_PATH. "
			  "If using Munge, please check the Munge daemon.\n",
			  listen->xprt,
			  listen->auth_name,
			  ovis_errno_abbvr(rc),
			  rc, args ? args : "(empty conf=)");
		free(args);
		goto out;
	}

	rc = listen_on_ldms_xprt(listen);
 out:
	return rc;
}

static int __create_default_auth()
{
	ldmsd_auth_t auth_dom;
	int rc = 0;

	auth_dom = ldmsd_auth_find(DEFAULT_AUTH);
	if (auth_dom)
		return 0;

	auth_dom = ldmsd_auth_new_with_auth(DEFAULT_AUTH, auth_name, auth_opt,
					geteuid(), getegid(), 0600);
	if (!auth_dom) {
		ldmsd_log(LDMSD_LCRITICAL, "Failed to set the default "
				"authentication method, errno %d\n", errno);
		rc = errno;
	}
	return rc;
}

struct ldmsd_str_ent *ldmsd_str_ent_new(char *s)
{
	struct ldmsd_str_ent *ent = malloc(sizeof(*ent));
	if (!ent)
		return NULL;
	ent->str = strdup(s);
	if (!ent->str) {
		free(ent);
		return NULL;
	}
	return ent;
}

void ldmsd_str_ent_free(struct ldmsd_str_ent *ent)
{
	free(ent->str);
	free(ent);
}

void ldmsd_str_list_destroy(struct ldmsd_str_list *list)
{
	struct ldmsd_str_ent *ent;

	while ((ent = TAILQ_FIRST(list))) {
		TAILQ_REMOVE(list, ent, entry);
		ldmsd_str_ent_free(ent);
	}
}

/* if path is NULL, close file.
 * if path is not NULL, open the file.
 */
static int reset_log_config_file(const char *path)
{
	if (path) {
		reset_log_config_file(NULL);
		ldmsd_req_debug_file = fopen(path, "a");
		if (ldmsd_req_debug_file) {
			struct tm tm;
			time_t t;
			gettimeofday(&ldmsd_req_last_time, NULL);
			t = time(NULL);
			localtime_r(&t, &tm);
			int e = 0, fe;
			fe = fprintf(ldmsd_req_debug_file, "# log begin:");
			if (fe < 0)
				e |= fe;
			fprintf(ldmsd_req_debug_file, " %lu.%06lu: ",
					ldmsd_req_last_time.tv_sec,
					ldmsd_req_last_time.tv_usec);
			char dtsz[200];
			strftime(dtsz, sizeof(dtsz), "%a %b %d %H:%M:%S %Y",
				&tm);
			fe = fprintf(ldmsd_req_debug_file, " %s", dtsz);
			if (fe < 0)
				e |= fe;
			fe |= fprintf(ldmsd_req_debug_file, "\n");
			if (fe < 0)
				e |= fe;
			e |= fflush(ldmsd_req_debug_file);
			if (e) {
				ldmsd_req_debug = 0;
				fclose(ldmsd_req_debug_file);
				return EBADFD;
			}
			return 0;
		}
		return errno;
	}
	if (ldmsd_req_debug_file) {
		fprintf(ldmsd_req_debug_file,"# log end\n");
		fclose(ldmsd_req_debug_file);
		ldmsd_req_debug_file = NULL;
	}
	return 0;
}
/* if value is integer, convert to bits and log to regular log.
 * if value is a path, set log_config file to path and assume int=1.
 * if value is int:path, log to path per 0-LRD_ALL
 * numbers out of range mean silence is desired.
 * if value is null, it's a recursive call to set default log and req messages.
 */
static int process_log_config(char *value)
{
	if (!value) {
		ldmsd_req_debug = 1;
		reset_log_config_file(NULL);
		return 0;
	}
	if (value[0] == '-') {
		ldmsd_log(LDMSD_LERROR,
			"-L option is missing an argument. Found %s\n", value);
		return EINVAL;
	}
	int on_off = 0;
	char *path = strdup(value);
	int argc = sscanf(value, "%d:%s", &on_off, path);
	switch (argc) {
	case 0: /* no int: found value is path */
		ldmsd_req_debug = 1;
		free(path);
		return reset_log_config_file(value);
	case 1: /* int only found */
		reset_log_config_file(NULL);
		break;
	case 2: /* both */
		reset_log_config_file(path);
		break;
	default:
		free(path);
		ldmsd_log(LDMSD_LERROR,
			"-L expected CINT:/path Found %s\n", value);
		return EINVAL;
	}
	if (on_off > 0 && on_off <= LRD_ALL)
		ldmsd_req_debug = on_off;
	else {
		ldmsd_req_debug = 0;
		ldmsd_log(LDMSD_LERROR,
			"-L expected CINT <= %d. Got %d\n", LRD_ALL, on_off);
		free(path);
		return EINVAL;
	}
	free(path);
	return 0;
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
	char *dup_auth;
	switch (opt) {
	case 'B':
		if (check_arg("B", value, LO_UINT))
			return EINVAL;
		if (banner != -1) {
			ldmsd_log(LDMSD_LERROR, "LDMSD Banner option was already "
				"specified to %d. Ignore the new value %s\n",
							banner, value);
		} else {
			banner = atoi(value);
		}
		break;
	case 'k':
		do_kernel = 1;
		break;
	case 'r':
		if (check_arg("r", value, LO_PATH))
			return EINVAL;
		if (pidfile) {
			ldmsd_log(LDMSD_LERROR, "The pidfile is already "
					"specified to %s. Ignore the new value %s\n",
							pidfile, value);
		} else {
			pidfile = strdup(value);
			if (!pidfile)
				return ENOMEM;
		}
		break;
	case 'l':
		if (check_arg("l", value, LO_PATH))
			return EINVAL;
		if (logfile) {
			ldmsd_log(LDMSD_LERROR, "The log path is already "
						"specified to %s. Ignore the new value %s\n",
						logfile, value);
		} else {
			logfile = strdup(value);
			if (!logfile)
				return ENOMEM;
			log_fp = ldmsd_open_log();
			if (!log_fp) {
				log_fp = stdout;
				return errno;
			}
		}
		break;
	case 'L':
		 return process_log_config(value);
	case 's':
		if (check_arg("s", value, LO_PATH))
			return EINVAL;
		if (setfile) {
			ldmsd_log(LDMSD_LERROR, "The kernel set file is already "
					"specified to %s. Ignore the new value %s\n",
					setfile, value);
		} else {
			setfile = strdup(value);
			if (!setfile)
				return ENOMEM;
		}
		break;
	case 'v':
		if (check_arg("v", value, LO_NAME))
			return EINVAL;
		if (is_loglevel_thr_set) {
			ldmsd_log(LDMSD_LERROR, "The log level was already "
					"specified to %s. Ignore the new value %s\n",
					ldmsd_loglevel_names[log_level_thr], value);
		} else {
			is_loglevel_thr_set = 1;
			if (0 == strcmp(value, "QUIET")) {
				quiet = 1;
				log_level_thr = LDMSD_LLASTLEVEL;
			} else {
				log_level_thr = ldmsd_str_to_loglevel(value);
			}
			if (log_level_thr < 0) {
				log_level_thr = LDMSD_LERROR;
				return EINVAL;
			}
		}
		break;
	case 'F':
		/*
		 * Must be specified at the command line.
		 * Handle separately in the main() function.
		 */
		break;
	case 'P':
		if (check_arg("P", value, LO_UINT))
			return EINVAL;
		if (ev_thread_count > 0) {
			ldmsd_log(LDMSD_LERROR, "LDMSD number of worker threads "
					"was already set to %d. Ignore the new value %s\n",
					ev_thread_count, value);
		} else {
			ev_thread_count = atoi(value);
			if (ev_thread_count < 1 )
				ev_thread_count = 1;
			if (ev_thread_count > EVTH_MAX)
				ev_thread_count = EVTH_MAX;
		}
		break;
	case 'm':
		if (max_mem_sz_str) {
			ldmsd_log(LDMSD_LERROR, "The memory limit was already "
					"set to '%s'. Ignore the new value '%s'\n",
					max_mem_sz_str, value);
		} else {
			max_mem_sz_str = strdup(value);
			if (!max_mem_sz_str)
				return ENOMEM;
		}
		break;
	case 'c':
		/*
		 * Must be specified at the command line.
		 * Handle separately in the main() function.
		 */
		break;
	case 'y':
		/*
		 * Must be specified at the command line.
		 * Handle separately in the main() function.
		 */
		break;
	case 'a':
		/* auth name */
		if (auth_name) {
			ldmsd_log(LDMSD_LERROR, "Default-auth was already "
					"specified to '%s'. Ignore the new value '%s'\n",
					auth_name, value);
			/* Mark 'count' to ignore additional auth arguments */
			auth_opt->count = -1;
		} else {
			auth_name = strdup(value);
			if (!auth_name)
				return ENOMEM;
		}
		break;
	case 'A':
		if (auth_opt->count) {
			ldmsd_log(LDMSD_LERROR, "Default-auth was already "
					"specified to '%s'. Ignore the additional "
					"auth arguments.\n", auth_name);
		} else {
			/* (multiple) auth options */
			dup_auth = strdup(value);
			if (!dup_auth)
				return ENOMEM;
			lval = strtok(dup_auth, "=");
			if (!lval) {
				ldmsd_log(LDMSD_LERROR, "Expecting -A name=value. "
								"Got %s\n", value);
				free(dup_auth);
				return EINVAL;
			}
			rval = strtok(NULL, "");
			if (!rval) {
				ldmsd_log(LDMSD_LERROR,"Expecting -A name=value. "
								"Got %s\n", value);
				free(dup_auth);
				return EINVAL;
			}
			if (auth_opt->count == auth_opt->size) {
				ldmsd_log(LDMSD_LERROR, "Too many (> %d) auth options %s\n",
							auth_opt->size, value);
				free(dup_auth);
				return EINVAL;
			}
			auth_opt->list[auth_opt->count].name = strdup(lval);
			auth_opt->list[auth_opt->count].value = strdup(rval);
			if (!auth_opt->list[auth_opt->count].name || !auth_opt->list[auth_opt->count].value) {
				return ENOMEM;
			}
			auth_opt->count++;
			free(dup_auth);
		}
		break;
	case 'n':
		if (myname[0] != '\0') {
			ldmsd_log(LDMSD_LERROR, "LDMSD daemon name was "
					"already set to %s. Ignore "
					"the new value %s\n", myname, value);
		} else {
			snprintf(myname, sizeof(myname), "%s", value);
		}
		break;
	case 't':
		log_truncate = 1;
		break;
	case 'x':
		if (check_arg("x", value, LO_NAME))
			return EINVAL;
		char *dup_xtuple = strdup(value);
		if (!dup_xtuple)
			return ENOMEM;
		char *_xprt, *_port, *_host;
		_xprt = dup_xtuple;
		_port = strchr(dup_xtuple, ':');
		if (!_port) {
			ldmsd_log(LDMSD_LERROR, "Bad xprt format, expecting XPRT:PORT, "
						"but got: %s\n", value);
			free(dup_xtuple);
			return EINVAL;
		}
		*_port = '\0';
		_port++;
		/* optional `host` */
		_host = strchr(_port, ':');
		if (_host) {
			*_host = '\0';
			_host++;
		}
		/* Use the default auth domain */
		ldmsd_listen_t listen = ldmsd_listen_new(_xprt, _port, _host, NULL);
		free(dup_xtuple);
		if (!listen) {
			ldmsd_log(LDMSD_LERROR, "Error %d: failed to add listening "
						"endpoint: %s\n", errno, value);
			return ENOMEM;
		}
		break;
	default:
		return ENOENT;
	}
	return 0;
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
	char *plug_name = NULL;
	int list_plugins = 0;
	int ret;
	int op, op_idx;
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

	auth_opt = av_new(AUTH_OPT_MAX);
	if (!auth_opt) {
		printf("Not enough memory!!!\n");
		exit(1);
	}

	opterr = 0;
	while ((op = getopt_long(argc, argv, short_opts, long_opts, NULL)) != -1) {
		switch (op) {
		case 'F':
			foreground = 1;
			break;
		case 'u':
			if (check_arg("u", optarg, LO_NAME))
				return 1;
			list_plugins = 1;
			plug_name = strdup(optarg);
			if (!plug_name) {
				printf("Not enough memory!!!\n");
				exit(1);
			}
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
		case 'c':
			/* Handle below */
			break;
		case 'y':
			/* Handle below */
			break;
		default:
			ret = ldmsd_process_cmd_line_arg(op, optarg);
			if (ret) {
				if (ret == ENOENT)
					usage(argv);
				else if (ret == ENOMEM)
					printf("Out of memory\n");
				cleanup(ret, "");
			}
			break;
		}
	}

	if (list_plugins) {
		if (plug_name) {
			if (strcmp(plug_name,"all") == 0) {
				free(plug_name);
				plug_name = NULL;
			}
		}
		ldmsd_plugins_usage(plug_name);
		if (plug_name)
			free(plug_name);
		av_free(auth_opt);
		exit(0);
	}

	if (!foreground) {
		if (daemon(1, 1)) {
			perror("ldmsd: ");
			cleanup(8, "daemon failed to start");
		}
	}

	ret = ldmsd_ev_init();
	if (ret) {
		printf("Memory allocation failure.\n");
		exit(1);
	}
	ret = ldmsd_worker_init();
	if (ret) {
		printf("Memory allocation failure.\n");
		exit(1);
	}

	/* Process cmd-line options in config files */
	opterr = 0;
	optind = 0;
	struct ldmsd_str_list cfgfile_list;
	struct ldmsd_str_list yamlfile_list;
	TAILQ_INIT(&yamlfile_list);
	TAILQ_INIT(&cfgfile_list);
	struct ldmsd_str_ent *cpath;
	struct ldmsd_str_ent *ypath;
	struct ldmsd_str_ent *config_str;
	char *resp;
	while ((op = getopt_long(argc, argv, short_opts, long_opts, NULL)) != -1) {
		switch (op) {
		case 'y':
			ypath = ldmsd_str_ent_new(optarg);
			TAILQ_INSERT_TAIL(&yamlfile_list, ypath, entry);
			while ((ypath = TAILQ_FIRST(&yamlfile_list))) {
				resp = process_yaml_config_file(ypath->str, myname);
				TAILQ_REMOVE(&yamlfile_list, ypath, entry);
				ldmsd_str_ent_free(ypath);
			}
			config_str = ldmsd_str_ent_new(resp);
			TAILQ_INSERT_TAIL(&yamlfile_list, config_str, entry);
			break;
		case 'c':
			cpath = ldmsd_str_ent_new(optarg);
			TAILQ_INSERT_TAIL(&cfgfile_list, cpath, entry);
			break;
		}
	}

	int lln;
	while ((config_str = TAILQ_FIRST(&yamlfile_list))) {
		lln = -1;
		ret = process_config_str(config_str->str, &lln, 1);
		if (ret) {
			char errstr[128];
			snprintf(errstr, sizeof(errstr),
				 "Error %d processing configuration file '%s'",
				 ret, config_str->str);
			ldmsd_str_list_destroy(&yamlfile_list);
			cleanup(ret, errstr);
		}
		TAILQ_REMOVE(&yamlfile_list, config_str, entry);
		ldmsd_str_ent_free(config_str);
	}
	while ((cpath = TAILQ_FIRST(&cfgfile_list))) {
		lln = -1;
		ret = process_config_file(cpath->str, &lln, 1);
		if (ret) {
			char errstr[128];
			snprintf(errstr, sizeof(errstr),
				 "Error %d processing configuration file '%s'",
				 ret, cpath->str);
			ldmsd_str_list_destroy(&cfgfile_list);
			cleanup(ret, errstr);
		}
		TAILQ_REMOVE(&cfgfile_list, cpath, entry);
		ldmsd_str_ent_free(cpath);
	}

	/* Initialize LDMS */
	umask(0);
	if (!auth_name)
		auth_name = DEFAULT_AUTH_NAME;
	if (-1 == banner)
		banner = DEFAULT_BANNER;
	if (0 == ev_thread_count)
		ev_thread_count = 1;
	if (!max_mem_sz_str) {
		max_mem_sz_str = getenv(LDMSD_MEM_SIZE_ENV);
		if (!max_mem_sz_str)
			max_mem_sz_str = LDMSD_MEM_SIZE_STR;
	}
	if ((max_mem_size = ovis_get_mem_size(max_mem_sz_str)) == 0) {
		ldmsd_log(LDMSD_LCRITICAL, "Invalid memory size '%s'. "
				"See the -m option.\n", max_mem_sz_str);
		usage(argv);
	}
	if (ldms_init(max_mem_size)) {
		ldmsd_log(LDMSD_LCRITICAL, "LDMS could not pre-allocate "
				"the memory of size %s.\n", max_mem_sz_str);
		av_free(auth_opt);
		exit(1);
	}

	if (!foreground) {
		/* Create pidfile for daemon that usually goes away on exit. */
		/* user arg, then env, then default to get pidfile name */
		if (!pidfile) {
			char *pidpath = getenv("LDMSD_PIDFILE");
			if (!pidpath) {
				pidfile = malloc(strlen(LDMSD_PIDFILE_FMT)
						+ strlen(basename(argv[0]) + 1));
				if (pidfile)
					sprintf(pidfile, LDMSD_PIDFILE_FMT, basename(argv[0]));
			} else {
				pidfile = strdup(pidpath);
			}
			if (!pidfile) {
				ldmsd_log(LDMSD_LERROR, "Out of memory\n");
				av_free(auth_opt);
				exit(1);
			}
		}
		if( !access( pidfile, F_OK ) ) {
			ldmsd_log(LDMSD_LERROR, "Existing pid file named '%s': %s\n",
				pidfile, "overwritten if writable");
		}
		FILE *pfile = fopen_perm(pidfile,"w", LDMSD_DEFAULT_FILE_PERM);
		if (!pfile) {
			int piderr = errno;
			ldmsd_log(LDMSD_LERROR, "Could not open the pid file named '%s': %s\n",
				pidfile, STRERROR(piderr));
			free(pidfile);
			pidfile = NULL;
		} else {
			pid_t mypid = getpid();
			fprintf(pfile,"%ld\n",(long)mypid);
			fclose(pfile);
		}
		if (pidfile && banner) {
			char *suffix = ".version";
			bannerfile = malloc(strlen(suffix)+strlen(pidfile)+1);
			if (!bannerfile) {
				ldmsd_log(LDMSD_LCRITICAL, "Memory allocation failure.\n");
				av_free(auth_opt);
				exit(1);
			}
			sprintf(bannerfile, "%s%s", pidfile, suffix);
			if( !access( bannerfile, F_OK ) ) {
				ldmsd_log(LDMSD_LERROR, "Existing banner file named '%s': %s\n",
					bannerfile, "overwritten if writable");
			}
			FILE *bfile = fopen_perm(bannerfile,"w", LDMSD_DEFAULT_FILE_PERM);
			if (!bfile) {
				int banerr = errno;
				ldmsd_log(LDMSD_LERROR, "Could not open the banner file named '%s': %s\n",
					bannerfile, STRERROR(banerr));
				free(bannerfile);
				bannerfile = NULL;
			} else {

#define BANNER_PART1_A "Started LDMS Daemon with authentication "
#define BANNER_PART1_NOA "Started LDMS Daemon without authentication "
#define BANNER_PART2 "version %s. LDMSD Interface Version " \
	"%hhu.%hhu.%hhu.%hhu. LDMS Protocol Version %hhu.%hhu.%hhu.%hhu. " \
	"git-SHA %s\n", PACKAGE_VERSION, \
	ldmsd_version.major, ldmsd_version.minor, \
	ldmsd_version.patch, ldmsd_version.flags, \
	ldms_version.major, ldms_version.minor, ldms_version.patch, \
	ldms_version.flags, OVIS_GIT_LONG

#if OVIS_LDMS_HAVE_AUTH
				fprintf(bfile, BANNER_PART1_A
#else /* OVIS_LDMS_HAVE_AUTH */
				fprintf(bfile, BANNER_PART1_NOA
#endif /* OVIS_LDMS_HAVE_AUTH */
					BANNER_PART2);
				fclose(bfile);
			}
		}
	}

	ev_count = calloc(ev_thread_count, sizeof(int));
	if (!ev_count) {
		ldmsd_log(LDMSD_LCRITICAL, "Memory allocation failure.\n");
		av_free(auth_opt);
		exit(1);
	}
	ovis_scheduler = calloc(ev_thread_count, sizeof(*ovis_scheduler));
	if (!ovis_scheduler) {
		ldmsd_log(LDMSD_LCRITICAL, "Memory allocation failure.\n");
		av_free(auth_opt);
		exit(1);
	}
	ev_thread = calloc(ev_thread_count, sizeof(pthread_t));
	if (!ev_thread) {
		ldmsd_log(LDMSD_LCRITICAL, "Memory allocation failure.\n");
		av_free(auth_opt);
		exit(1);
	}
	for (op = 0; op < ev_thread_count; op++) {
		ovis_scheduler[op] = ovis_scheduler_new();
		if (!ovis_scheduler[op]) {
			ldmsd_log(LDMSD_LERROR, "Error creating an OVIS scheduler.\n");
			cleanup(6, "OVIS scheduler create failed");
		}
		ret = pthread_create(&ev_thread[op], NULL, event_proc, ovis_scheduler[op]);
		if (ret) {
			ldmsd_log(LDMSD_LERROR, "Error %d creating the event "
					"thread.\n", ret);
			cleanup(7, "event thread create fail");
		}
	}

	if (!setfile)
		setfile = LDMSD_SETFILE;

	if (do_kernel && publish_kernel(setfile))
		cleanup(3, "start kernel sampler failed");

	if (__create_default_auth())
		cleanup(20, "Error creating the default authentication.");

	is_ldmsd_initialized = 1;

	/* Start listening on ports */
	ldmsd_listen_t listen;
	for (listen = (ldmsd_listen_t)ldmsd_cfgobj_first(LDMSD_CFGOBJ_LISTEN);
		listen; listen = (ldmsd_listen_t)ldmsd_cfgobj_next(&listen->obj)) {
		ret = ldmsd_listen_start(listen);
		if (ret)
			cleanup(7, "error listening on transport");
	}

	/* Process configuration files */
	int has_config_file = 0;
	opterr = 0;
	optind = 0;
	while ((op = getopt_long(argc, argv, short_opts, long_opts, &op_idx)) != -1) {
		char *dup_arg;
		int lln = -1;
		switch (op) {
		case 'c':
			has_config_file = 1;
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
			ldmsd_log(LDMSD_LINFO, "Processing the config file '%s' is done.\n", optarg);
			break;
		case 'y':
			has_config_file = 1;
			ret = process_config_str(resp, &lln, 1);
			if (ret) {
				char errstr[128];
				snprintf(errstr, sizeof(errstr),
					 "Error %d processing configuration string '%s'",
					 ret, resp);
				cleanup(ret, errstr);
			}
			free(resp);
			break;
		}
	}

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

	/* Check for at least a listening port */
	struct ldmsd_listen *_listen;
	_listen = (ldmsd_listen_t) ldmsd_cfgobj_first(LDMSD_CFGOBJ_LISTEN);
	if (!_listen && !has_config_file) {
		ldmsd_log(LDMSD_LCRITICAL,
			"A config file, (-c) or (-y), or listening port (-x) is required."
			" Specify at least one of these. ... exiting\n");
		cleanup(101, "no config files nor listening ports");
	}

	/* Keep the process alive */
	do {
		usleep(LDMSD_KEEP_ALIVE_30MIN);
	} while (1);

	cleanup(0,NULL);
	return 0;
}
