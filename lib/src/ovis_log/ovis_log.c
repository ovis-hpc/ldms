/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2022 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2022 Open Grid Computing, Inc. All rights reserved.
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

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <syslog.h>
#include "ovis_ev/ev.h"
#include "ovis_util/util.h"

#include "ovis_log.h"

ev_worker_t logger_w;
ev_type_t log_type;
int is_init;
enum ovis_loglevel log_level_thr = OVIS_LERROR;
FILE *log_fp;
char *progname;

int stdout_fd_cache;
int stderr_fd_cache;
FILE *stdout_cache;
FILE *stderr_cache;

#define OVIS_DEFAULT_FILE_PERM 0600

struct log_data {
	enum op_type {
		OVIS_LOG_O_LOG = 1,
		OVIS_LOG_O_CLOSE,
	} type;
	uint64_t level; /* feature|level */
	char *msg;
	struct timeval tv;
	struct tm tm;
};

const char* ovis_loglevel_names[] = {
	LOGLEVELS(OVIS_STR_WRAP)
	NULL
};

int ovis_loglevel_set(char *verbose_level)
{
	int level = -1;
	if (0 == strcmp(verbose_level, "QUIET")) {
		level = OVIS_LLASTLEVEL;
	} else {
		level = ovis_str_to_loglevel(verbose_level);
	}
	if (level < 0)
		return level;
	log_level_thr = level;
	return 0;
}

enum ovis_loglevel ovis_loglevel_get()
{
	return log_level_thr;
}

int ovis_loglevel_to_syslog(enum ovis_loglevel level)
{
	switch (level) {
	case OVIS_LDEBUG: return LOG_DEBUG;
	case OVIS_LINFO: return LOG_INFO;
	case OVIS_LWARNING: return LOG_WARNING;
	case OVIS_LERROR: return LOG_ERR;
	case OVIS_LCRITICAL: return LOG_CRIT;
	case OVIS_LALL: return LOG_ALERT;
	default:
		return LOG_ERR;
	}
}

enum ovis_loglevel ovis_str_to_loglevel(const char *level_s)
{
	int i;
	for (i = 0; i < OVIS_LLASTLEVEL; i++)
		if (0 == strcasecmp(level_s, ovis_loglevel_names[i]))
			return i;
	if (strcasecmp(level_s,"QUIET") == 0) {
		return OVIS_LALL;
	}
	if (strcasecmp(level_s,"ALWAYS") == 0) {
		return OVIS_LALL;
	}
	if (strcasecmp(level_s,"CRIT") == 0) {
		return OVIS_LCRITICAL;
	}
	return OVIS_LNONE;
}

const char *ovis_loglevel_to_str(enum ovis_loglevel level)
{
	if ((level >= OVIS_LDEBUG) && (level < OVIS_LLASTLEVEL))
		return ovis_loglevel_names[level];
	return "OVIS_LNONE";
}

FILE *__log_open(const char *path)
{
	FILE *f;
	if (strcasecmp(path,"syslog") == 0) {
		ovis_log(OVIS_LDEBUG, "Printing messages to syslog.\n");
		openlog(progname, LOG_NDELAY|LOG_PID, LOG_DAEMON);
		return OVIS_LOG_SYSLOG;
	}

	f = fopen_perm(path, "a", OVIS_DEFAULT_FILE_PERM);
	if (!f) {
		ovis_log(OVIS_LERROR, "Could not open the log file named '%s'\n",
							path);
		errno = EINVAL;
		return NULL;
	}
	int fd = fileno(f);
	if (dup2(fd, 1) < 0) {
		ovis_log(OVIS_LERROR, "Cannot redirect log to %s\n",
						path);
		errno = EINTR;
		return NULL;
	}
	if (dup2(fd, 2) < 0) {
		ovis_log(OVIS_LERROR, "Cannot redirect log to %s\n",
						path);
		errno = EINTR;
		return NULL;
	}
	return f;
}

int __log_close()
{
	fflush(log_fp);
	dup2(stdout_fd_cache, 1);
	dup2(stderr_fd_cache, 2);

	fclose(log_fp);
	log_fp = stdout = stdout_cache;
	stderr = stderr_cache;
	return 0;
}

static int log_time_sec = -1;
int __log(enum ovis_loglevel level, char *msg, struct timeval *tv, struct tm *tm)
{
	if (log_fp == OVIS_LOG_SYSLOG) {
		syslog(ovis_loglevel_to_syslog(level), "%s", msg);
		return 0;
	}

	if (log_time_sec) {
		fprintf(log_fp, "%lu.%06lu: ", tv->tv_sec, tv->tv_usec);
	} else {
		char dtsz[200];
		if (strftime(dtsz, sizeof(dtsz), "%a %b %d %H:%M:%S %Y", tm))
			fprintf(log_fp, "%s: ", dtsz);
	}

	if (level < OVIS_LALL) {
		fprintf(log_fp, "%-10s: ", ovis_loglevel_names[level]);
	}

	fprintf(log_fp, "%s", msg);

	return 0;
}

int log_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t ev)
{
	enum ovis_loglevel level = EV_DATA(ev, struct log_data)->level;
	char *msg = EV_DATA(ev, struct log_data)->msg;
	struct timeval *tv = &EV_DATA(ev, struct log_data)->tv;
	struct tm *tm = &EV_DATA(ev, struct log_data)->tm;
	enum op_type type = EV_DATA(ev, struct log_data)->type;
	int rc;

	switch (type) {
	case OVIS_LOG_O_LOG:
		rc = __log(level, msg, tv, tm);
		if (0 == ev_pending(logger_w))
			fflush(log_fp);
		free(msg);
		break;
	case OVIS_LOG_O_CLOSE:
		__log_close();
		break;
	default:
		ovis_log(OVIS_LCRITICAL, "ovis_log: Unrecognized operation %d\n", type);
		assert(0);
		break;
	}

	ev_put(ev);
	return rc;
}

int ovis_log_init(const char *name)
{
	if (is_init)
		return 0;

	int rc;
	int need_free = 0;
	char s[PATH_MAX];
	if (name) {
		progname = strdup(name);
		if (!progname)
			return ENOMEM;
		need_free = 1;
	} else {
		progname = "";
	}

	log_fp = stdout;
	snprintf(s, PATH_MAX, "%s:ovis_log", progname);
	log_type = ev_type_new(s, sizeof(struct log_data));
	if (!log_type) {
		rc = ENOMEM;
		goto err;
	}
	snprintf(s, PATH_MAX, "%s:logger", progname);
	logger_w = ev_worker_new(s, log_actor);
	if (!logger_w) {
		rc = ENOMEM;
		goto err;
	}
	is_init = 1;
	return 0;
err:
	if (need_free)
		free(progname);
	free(log_type);
	return rc;
}

/** return a file pointer or a special syslog pointer */
int ovis_log_open(const char *path)
{
	int rc;

	stdout_cache = stdout;
	stderr_cache = stderr;
	rc = dup2(1, stdout_fd_cache);
	if (rc) {
		ovis_log(OVIS_LERROR, "Failed to cache stdout original fd. "
							  "Error %d\n", rc);
		return rc;
	}
	rc = dup2(2, stderr_fd_cache);
	if (rc) {
		ovis_log(OVIS_LERROR, "Failed to cache stderr original fd."
							  "Error %d\n", rc);
		return rc;
	}

	if (!path)
		return EINVAL;

	log_fp = __log_open(path);
	if (!log_fp) {
		rc = errno;
		return rc;
	}

	stdout = stderr = log_fp;
	return 0;
}

void ovis_vlog(enum ovis_loglevel level, const char *fmt, va_list ap)
{
	ev_t log_ev;
	char *msg;
	int rc;
	struct timeval tv;
	struct tm tm;
	time_t t;

	if ((level != OVIS_LALL) &&
			((0 <= level) && (level < log_level_thr)))
		return;

	if (log_time_sec == -1) {
		char * lt = getenv("OVIS_LOG_TIME_SEC");
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

	if (!is_init) {
		/* No workers, so directly log to the file */
		(void) __log(level, msg, &tv, &tm);
		return;
	}

	log_ev = ev_new(log_type);
	if (!log_ev)
		return;
	EV_DATA(log_ev, struct log_data)->msg = msg;
	EV_DATA(log_ev, struct log_data)->level = level;
	EV_DATA(log_ev, struct log_data)->type = OVIS_LOG_O_LOG;

	if (log_time_sec)
		EV_DATA(log_ev, struct log_data)->tv = tv;
	else
		EV_DATA(log_ev, struct log_data)->tm = tm;
	ev_post(NULL, logger_w, log_ev, NULL);
}

void ovis_log(enum ovis_loglevel level, const char *fmt, ...)
{
	va_list ap;
	if (!is_init)
		log_fp = stdout;
	va_start(ap, fmt);
	ovis_vlog(level, fmt, ap);
	va_end(ap);
}

int ovis_log_flush()
{
	return fflush(log_fp);
}

int ovis_log_reopen(const char *path)
{
	FILE *f, *tmp;
	int rc;

	fflush(log_fp);

	f = __log_open(path);
	if (!f) {
		rc = errno;
		return rc;
	}

	tmp = log_fp;
	log_fp = f;
	fclose(tmp);
	stdout = stderr = log_fp;

	return 0;
}

int ovis_log_close()
{
	ev_t close_ev;
	int rc;

	close_ev = ev_new(log_type);
	if (!close_ev) {
		/* Attempt to close the log file immediately */
		rc = __log_close();
	} else {
		EV_DATA(close_ev, struct log_data)->type = OVIS_LOG_O_CLOSE;
		rc = ev_post(NULL, logger_w, close_ev, NULL);
	}
	return rc;
}

/* All messages from the ldms library are of e level.*/
#define OVIS_LOG_AT(e,fsuf) \
void ovis_l##fsuf(const char *fmt, ...) \
{ \
	va_list ap; \
	va_start(ap, fmt); \
	ovis_vlog(e, fmt, ap); \
	va_end(ap); \
}

OVIS_LOG_AT(OVIS_LDEBUG, debug);
OVIS_LOG_AT(OVIS_LINFO, info);
OVIS_LOG_AT(OVIS_LWARNING, warning);
OVIS_LOG_AT(OVIS_LERROR, error);
OVIS_LOG_AT(OVIS_LCRITICAL, critical);
OVIS_LOG_AT(OVIS_LALL, all);
