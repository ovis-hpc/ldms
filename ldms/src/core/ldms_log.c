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

#include "ldms_log.h"
#include "ovis_util/util.h"
#include <sys/time.h>

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

#ifdef DEBUG
#include <mcheck.h>
#endif /* DEBUG */

static char *logfile = NULL;
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
static FILE *log_fp;
static int log_level_thr = LDMSD_LERROR;  /* log level threshold */
static int quiet = 0; /* Is verbosity quiet? 0 for no and 1 for yes */

const char* ldms_loglevel_names[] = {
	LOGLEVELS(LDMSD_STR_WRAP)
	NULL
};

int ldms_loglevel_set(char *verbose_level)
{
	int level = -1;
	if (0 == strcmp(verbose_level, "QUIET")) {
		quiet = 1;
		level = LDMSD_LLASTLEVEL;
	} else {
		level = ldms_str_to_loglevel(verbose_level);
		quiet = 0;
	}
	if (level < 0)
		return level;
	log_level_thr = level;
	return 0;
}

enum ldms_loglevel ldms_loglevel_get()
{
	return log_level_thr;
}

int ldms_loglevel_to_syslog(enum ldms_loglevel level)
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

void __ldms_log(enum ldms_loglevel level, const char *fmt, va_list ap)
{
	if ((level != LDMSD_LALL) &&
			(quiet || ((0 <= level) && (level < log_level_thr))))
		return;
	if (log_fp == LDMSD_LOG_SYSLOG) {
		vsyslog(ldms_loglevel_to_syslog(level),fmt,ap);
		return;
	}
	time_t t;
	struct tm tm;
	char dtsz[200];

	pthread_mutex_lock(&log_lock);
	if (!log_fp) {
		pthread_mutex_unlock(&log_lock);
		return;
	}
	t = time(NULL);
	localtime_r(&t, &tm);
	if (strftime(dtsz, sizeof(dtsz), "%a %b %d %H:%M:%S %Y", &tm))
		fprintf(log_fp, "%s: ", dtsz);

	if (level < LDMSD_LALL) {
		fprintf(log_fp, "%-10s: ", ldms_loglevel_names[level]);
	}

	vfprintf(log_fp, fmt, ap);
	fflush(log_fp);
	pthread_mutex_unlock(&log_lock);
}

void ldms_log(enum ldms_loglevel level, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	__ldms_log(level, fmt, ap);
	va_end(ap);
}

/* All messages from the ldms library are of e level.*/
#define LDMSD_LOG_AT(e,fsuf) \
void ldms_l##fsuf(const char *fmt, ...) \
{ \
	va_list ap; \
	va_start(ap, fmt); \
	__ldms_log(e, fmt, ap); \
	va_end(ap); \
}

LDMSD_LOG_AT(LDMSD_LDEBUG,debug);
LDMSD_LOG_AT(LDMSD_LINFO,info);
LDMSD_LOG_AT(LDMSD_LWARNING,warning);
LDMSD_LOG_AT(LDMSD_LERROR,error);
LDMSD_LOG_AT(LDMSD_LCRITICAL,critical);
LDMSD_LOG_AT(LDMSD_LALL,all);

static char msg_buf[4096];
void ldms_msg_logger(enum ldms_loglevel level, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(msg_buf, sizeof(msg_buf), fmt, ap);
	ldms_log(level, "%s", msg_buf);
}

enum ldms_loglevel ldms_str_to_loglevel(const char *level_s)
{
	int i;
	for (i = 0; i < LDMSD_LLASTLEVEL; i++)
		if (0 == strcasecmp(level_s, ldms_loglevel_names[i]))
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

const char *ldms_loglevel_to_str(enum ldms_loglevel level)
{
	if ((level >= LDMSD_LDEBUG) && (level < LDMSD_LLASTLEVEL))
		return ldms_loglevel_names[level];
	return "LDMSD_LNONE";
}

/** return a file pointer or a special syslog pointer */
int ldms_log_open(const char *progname, const char* xlogfile, const char **errstr)
{
	if (!progname || ! xlogfile || !errstr) {
		ldms_log(LDMSD_LERROR, "invalid arguments to ldms_open_log: %p %p %p\n",
			progname, xlogfile, errstr);
		return EINVAL;
	}
	FILE *f;
	if (strcasecmp(xlogfile,"syslog")==0) {
		ldms_log(LDMSD_LDEBUG, "Switching to syslog.\n");
		f = LDMSD_LOG_SYSLOG;
		openlog(progname, LOG_NDELAY|LOG_PID, LOG_DAEMON);
		log_fp = f;
		return 0;
	}

	logfile = strdup(xlogfile);
	if (!logfile) {
		return ENOMEM;
	}
	f = fopen_perm(logfile, "a", LDMS_DEFAULT_FILE_PERM);
	if (!f) {
		ldms_log(LDMSD_LERROR, "Could not open the log file named '%s'\n",
							logfile);
		*errstr = "log open failed";
		return 9;
	} else {
		int fd = fileno(f);
		if (dup2(fd, 1) < 0) {
			ldms_log(LDMSD_LERROR, "Cannot redirect log to %s\n",
							logfile);
			*errstr =  "error redirecting stdout";
			return 10;
		}
		if (dup2(fd, 2) < 0) {
			ldms_log(LDMSD_LERROR, "Cannot redirect log to %s\n",
							logfile);
			*errstr =  "error redirecting stderr";
			return 11;
		}
		stdout = f;
		stderr = f;
	}
	log_fp = f;
	return 0;
}

int ldms_logrotate() {
	int rc;
	if (!logfile) {
		ldms_log(LDMSD_LERROR, "Received a logrotate command but "
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

	pthread_mutex_lock(&log_lock);
	if (!log_fp) {
		pthread_mutex_unlock(&log_lock);
		return EINVAL;
	}
	fflush(log_fp);
	fclose(log_fp);
	rename(logfile, ofile_name);
	log_fp = fopen_perm(logfile, "a", LDMS_DEFAULT_FILE_PERM);
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

int ldms_log_parse_verbosity(const char *optarg) {
	if (0 == strcmp(optarg, "QUIET")) {
		quiet = 1;
		log_level_thr = LDMSD_LLASTLEVEL;
	} else {
		log_level_thr = ldms_str_to_loglevel(optarg);
	}
	if (log_level_thr < 0) {
		return -1;
	}
	return 0;
}

void ldms_log_close() {
	if (logfile) {
		free(logfile);
		logfile = NULL;
	}
}

static __attribute__((constructor))
void __init()
{
	log_fp = stdout;
}
