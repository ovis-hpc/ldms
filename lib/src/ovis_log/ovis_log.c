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
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <syslog.h>
#include <sys/types.h>
#include <regex.h>
#include "ovis_ev/ev.h"
#include "ovis_util/util.h"

#include "ovis_log.h"

static ev_worker_t logger_w;
static ev_type_t log_type;
static int is_init;
static FILE *log_fp;
static int default_modes = OVIS_LOG_M_DT;
static char *progname;

static struct ovis_log_s default_log = {
	.name = "",
	.desc = "The default log subsystem",
	.level = OVIS_LERROR | OVIS_LCRITICAL,
	.ref_count = 1,
};

static int stdout_fd_cache;
static int stderr_fd_cache;
static FILE *stdout_cache;
static FILE *stderr_cache;

static int subsys_cmp(void *a, const void *b)
{
	return strcmp(a, b);
}

static struct rbt subsys_tree = RBT_INITIALIZER(subsys_cmp);
static pthread_mutex_t subsys_tree_lock = PTHREAD_MUTEX_INITIALIZER;

#define OVIS_DEFAULT_FILE_PERM 0600

struct log_data {
	enum op_type {
		OVIS_LOG_O_LOG = 1,
		OVIS_LOG_O_OPEN,
		OVIS_LOG_O_CLOSE,
	} type;
	uint64_t level; /* feature|level */
	char *msg;
	ovis_log_t log;
	struct timeval tv;
	struct tm tm;
};

const char* ovis_loglevel_names[] = {
	[OVIS_LQUIET] = "QUIET",
	[OVIS_LDEBUG] = "DEBUG",
	[OVIS_LINFO]  = "INFO",
	[OVIS_LWARN]  = "WARNING",
	[OVIS_LERROR] = "ERROR",
	[OVIS_LCRIT]  = "CRITICAL",
	[OVIS_LALWAYS] = "ALWAYS",
	NULL
};

struct ovis_loglevel_s {
	int value;
	const char *name;
};
/*
 * The log level is order by severity
 */
struct ovis_loglevel_s level_tbl[] = {
		{ OVIS_LQUIET,  "QUIET" },
		{ OVIS_LDEBUG,	"DEBUG" },
		{ OVIS_LINFO,	"INFO" },
		{ OVIS_LWARN,	"WARNING" },
		{ OVIS_LERROR,	"ERROR" },
		{ OVIS_LCRIT,	"CRITICAL" },
		{ 0, NULL },
};

static int is_level_valid(int level)
{
	return ((level == OVIS_LDEFAULT) || !level || !(level & (~OVIS_ALL_LEVELS)));
}

static int is_mode_valid(int flags)
{
	return ((flags == OVIS_LOG_M_DT) ||
			(flags == OVIS_LOG_M_TS)) ||
			(flags == OVIS_LOG_M_TS_NONE);
}

static ovis_log_t __find_log(const char *subsys_name)
{
	ovis_log_t log = NULL;
	struct rbn *rbn = rbt_find(&subsys_tree, subsys_name);
	if (rbn)
		log = container_of(rbn, struct ovis_log_s, rbn);
	return log;
}

int ovis_log_set_level(ovis_log_t mylog, int level)
{
	ovis_log_t log = mylog;
	if (!is_level_valid(level))
		return EINVAL;
	if ((!mylog || (mylog == &default_log)) && (level == OVIS_LDEFAULT)) {
		/*
		 * The caller is trying to change the log level of
		 * the default logger to the default log level.
		 *
		 * Ignore it!
		 */
		return 0;
	}
	if (!mylog)
		log = &default_log;
	log->level = level;
	return 0;
}

int ovis_log_set_level_by_name(const char *name, int level)
{
	ovis_log_t log = NULL;
	if (name) {
		log = __find_log(name);
		if (!log)
			return ENOENT;
	}
	return ovis_log_set_level(log, level);
}

int ovis_log_set_level_by_regex(const char *regex_s, int level)
{
	int rc = 0;
	int cnt = 0;
	ovis_log_t l;
	struct rbn *rbn;
	regex_t regex;
	char msg[128];

	memset(&regex, 0, sizeof(regex));
	rc = regcomp(&regex, regex_s, REG_EXTENDED|REG_NOSUB);
	if (rc) {
		(void) regerror(rc, &regex, msg, 128);
		ovis_log(NULL, OVIS_LERROR, "Failed to compile the regex '%s': %s\n",
							regex_s, msg);
		rc = EINVAL;
		goto out;
	}

	pthread_mutex_lock(&subsys_tree_lock);
	RBT_FOREACH(rbn, &subsys_tree) {
		l = container_of(rbn, struct ovis_log_s, rbn);
		rc = regexec(&regex, l->name, 0, NULL, 0);
		if (rc)
			continue;
		ovis_log_set_level(l, level);
		cnt++;
	}
	pthread_mutex_unlock(&subsys_tree_lock);
	if (cnt == 0) {
		/* regex_s doesn't match any subsystems. */
		rc = ENOENT;
	}
out:
	regfree(&regex);
	return rc;
}

int ovis_log_get_level(ovis_log_t mylog)
{
	return mylog->level;
}

int ovis_log_get_level_by_name(const char *name)
{
	ovis_log_t log;
	if (!name) {
		log = &default_log;
	} else {
		log = __find_log(name);
		if (!log)
			return -ENOENT;
	}
	return log->level;
}

static int __log_level_to_syslog(int level)
{
	switch (level) {
	case OVIS_LDEBUG: return LOG_DEBUG;
	case OVIS_LINFO: return LOG_INFO;
	case OVIS_LWARNING: return LOG_WARNING;
	case OVIS_LERROR: return LOG_ERR;
	case OVIS_LCRITICAL: return LOG_CRIT;
	case OVIS_LALWAYS: return LOG_ALERT;
	default:
		return LOG_ERR;
	}
}

static int __str_to_loglevel(const char *level_s)
{
	if (strcasecmp(level_s, "QUIET") == 0) {
		return OVIS_LQUIET;
	} else if (strcasecmp(level_s, "ALWAYS") == 0) {
		return OVIS_LALWAYS;
	} else if ((strcasecmp(level_s, "CRIT") == 0) ||
			(strcasecmp(level_s, "CRITICAL") == 0)) {
		return OVIS_LCRITICAL;
	} else if (strcasecmp(level_s, "ERROR") == 0) {
		return OVIS_LERROR;
	} else if ((strcasecmp(level_s, "WARN") == 0) ||
			(strcasecmp(level_s, "WARNING") == 0)) {
		return OVIS_LWARN;
	} else if (strcasecmp(level_s, "INFO") == 0) {
		return OVIS_LINFO;
	} else if (strcasecmp(level_s, "DEBUG") == 0) {
		return OVIS_LDEBUG;
	} else {
		return -EINVAL;
	}
}

int ovis_log_str_to_level(const char *level_s)
{
	int level;
	int i, rc;
	char *s, *ptr, *tok;

	if (strchr(level_s, ','))
		goto parse_log_str;

	/*
	 * A single log level name is given.
	 */
	for (i = 0; strcasecmp(level_tbl[i].name, level_s); i++);
	if (!i)
		return OVIS_LQUIET;
	if (!level_tbl[i].value)
		return ENOENT;	/* level_s not found */
	for (rc = 0; level_tbl[i].value; i++)
		rc |= level_tbl[i].value;
	return rc;

parse_log_str:
	s = strdup(level_s);
	if (!s)
		return -ENOMEM;
	for (level = 0, tok = strtok_r(s, ",", &ptr); tok;
			tok = strtok_r(NULL, ",", &ptr)) {
		rc = __str_to_loglevel(tok);
		if (rc <= 0)
			/* Not found or QUIET. QUIET cannot be combined
			 * with any other log level */
			goto err;
		level |= rc;
	}
	free(s);
	return level;
err:
	free(s);
	return rc;
}

char *ovis_log_level_to_str(int level)
{
	if (!is_level_valid(level))
		return NULL;

	int count = 0;
	struct ovis_loglevel_s *l;
	size_t cnt = 0;
	char *str = malloc(128);
	if (!str) {
		errno = ENOMEM;
		goto out;
	}
	if (level == OVIS_LQUIET) {
		sprintf(str, "QUIET");
		goto out;
	}

	l = &level_tbl[0];
	while (l->name) {
		if (!(level & l->value))
			goto next;
		if (!count)
			cnt += sprintf(&str[cnt], "%s", l->name);
		else
			cnt += sprintf(&str[cnt], ",%s", l->name);
		count++;
	next:
		l++;
	}
	if (count == 1)
		cnt += sprintf(&str[cnt], ",");
out:
	return str;
}

static void __free_log(ovis_log_t log)
{
	free((char *)log->name);
	free((char *)log->desc);
	free(log);
}

static ovis_log_t __ovis_log_get(ovis_log_t log)
{
	__sync_fetch_and_add(&log->ref_count, 1);
	return log;
}

static void __ovis_log_put(ovis_log_t log)
{
	if (0 == __sync_sub_and_fetch(&log->ref_count, 1)) {
		pthread_mutex_lock(&subsys_tree_lock);
		rbt_del(&subsys_tree, &log->rbn);
		pthread_mutex_unlock(&subsys_tree_lock);
		__free_log(log);
	}
}

void ovis_log_destroy(ovis_log_t log)
{
	__ovis_log_put(log);
}

ovis_log_t ovis_log_register(const char *subsys_name, const char *desc)
{
	ovis_log_t log;

	if (!subsys_name || !desc) {
		errno = EINVAL;
		return NULL;
	}

	log = __find_log(subsys_name);
	if (log) {
		errno = EEXIST;
		return NULL;
	}
	log = malloc(sizeof(*log));
	if (!log) {
		errno = ENOMEM;
		return NULL;
	}
	log->name = strdup(subsys_name);
	if (!log->name) {
		errno = ENOMEM;
		goto free_log;
	}
	log->desc = strdup(desc);
	if (!log->desc) {
		errno = ENOMEM;
		goto free_log;
	}
	log->ref_count = 1;
	log->level = OVIS_LDEFAULT;
	rbn_init(&log->rbn, (void *)log->name);
	pthread_mutex_lock(&subsys_tree_lock);
	rbt_ins(&subsys_tree, &log->rbn);
	pthread_mutex_unlock(&subsys_tree_lock);
	return log;
free_log:
	__free_log(log);
	return NULL;
}

struct ovis_log_buf {
	char *buf;
	size_t sz;
	size_t off;
};

static int __buf_append(struct ovis_log_buf *buf, const char *fmt, ...)
{
	va_list ap, ap_dup;
	size_t cnt;
	va_start(ap, fmt);
	va_copy(ap_dup, ap);
	while (1) {
		cnt = vsnprintf(&buf->buf[buf->off], buf->sz - buf->off, fmt, ap_dup);
		va_end(ap_dup);
		if (cnt >= buf->sz - buf->off) {
			char *tmp = realloc(buf->buf, buf->sz * 2);
			if (!tmp)
				return ENOMEM;
			buf->buf = tmp;
			buf->sz *= 2;
			va_copy(ap_dup, ap);
			continue;
		}
		break;
	}
	buf->off += cnt;
	va_end(ap);
	return 0;
}

static int __get_log_info(ovis_log_t l, struct ovis_log_buf *buf)
{
	int rc;
	char *level_s;

	if (l->level == OVIS_LDEFAULT)
		level_s = "default";
	else
		level_s = ovis_log_level_to_str(l->level);

	rc = __buf_append(buf, "{\"name\":\"%s\","
			        "\"desc\":\"%s\","
			        "\"level\":\"%s\"}",
				l->name, l->desc,
				level_s);
	return rc;
}

char *ovis_log_list(const char *subsys)
{
	int rc;
	ovis_log_t l = NULL;
	struct rbn *rbn;
	struct ovis_log_buf *buf;
	char *s;
	int cnt = 0;

	if (subsys) {
		l = __find_log(subsys);
		if (!l) {
			errno = ENOENT;
			return NULL;
		}
	}

	buf = malloc(sizeof(*buf));
	if (!buf) {
		errno = ENOMEM;
		return NULL;
	}
	buf->sz = 512;
	buf->off = 0;
	buf->buf = malloc(buf->sz);
	if (!buf->buf) {
		errno = ENOMEM;
		free(buf);
		return NULL;
	}

	rc = __buf_append(buf, "[");
	if (rc) {
		errno = rc;
		goto err;
	}

	rc = __buf_append(buf, "{\"name\":\"%s (default)\","
			        "\"desc\":\"%s\","
			        "\"level\":\"%s\"}",
				default_log.name,
				default_log.desc,
				ovis_log_level_to_str(default_log.level));
	if (rc) {
		errno = rc;
		goto err;
	}

	if (l) {
		rc = __buf_append(buf, ",");
		if (rc) {
			errno = rc;
			goto err;
		}
		rc = __get_log_info(l, buf);
		if (rc) {
			errno = rc;
			goto err;
		}
	} else {
		RBT_FOREACH(rbn, &subsys_tree) {
			rc = __buf_append(buf, ",");
			if (rc) {
				errno = rc;
				goto err;
			}
			l = container_of(rbn, struct ovis_log_s, rbn);
			rc = __get_log_info(l, buf);
			if (rc) {
				errno = rc;
				goto err;
			}
			cnt++;
		}
	}

	rc = __buf_append(buf, "]");
	if (rc) {
		errno = rc;
		goto err;
	}
	s = buf->buf;
	free(buf);
	return s;

err:
	free(buf->buf);
	free(buf);
	return NULL;
}

static FILE *__log_open(const char *path)
{
	FILE *f;
	if (strcasecmp(path,"syslog") == 0) {
		ovis_log(NULL, OVIS_LDEBUG,
				"Printing messages to Syslog.\n");
		openlog(progname, LOG_NDELAY|LOG_PID, LOG_DAEMON);
		return OVIS_LOG_SYSLOG;
	}

	f = fopen_perm(path, "a", OVIS_DEFAULT_FILE_PERM);
	if (!f) {
		ovis_log(NULL, OVIS_LERROR, "Could not open the "
					"log file named '%s'\n", path);
		errno = EINVAL;
		return NULL;
	}
	int fd = fileno(f);
	if (dup2(fd, 1) < 0) {
		ovis_log(NULL, OVIS_LERROR,
				"Cannot redirect log to %s\n", path);
		errno = EINTR;
		return NULL;
	}
	if (dup2(fd, 2) < 0) {
		ovis_log(NULL, OVIS_LERROR,
				"Cannot redirect log to %s\n", path);
		errno = EINTR;
		return NULL;
	}
	return f;
}

static int __log_reopen(const char *path)
{
	int rc;
	if (!log_fp) {
		log_fp = __log_open(path);
		if (!log_fp) {
			rc = errno;
			ovis_log(NULL, OVIS_LERROR, "Failed to open the log "
					"file at %s. Error %d\n", path, rc);
			return rc;
		}
	} else {
		FILE *fp_bk = log_fp;
		fflush(log_fp);
		log_fp = __log_open(path);
		if (!log_fp) {
			/* Fall back to the previous log. */
			rc = errno;
			log_fp = fp_bk;
			ovis_log(NULL, OVIS_LERROR, "Failed to open the log "
					"file at %s. Error %d\n", path, rc);
			return rc;
		}
		fclose(fp_bk);
	}
	stdout = stderr = log_fp;
	return 0;
}

static int __log_close()
{
	if (log_fp == OVIS_LOG_SYSLOG) {
		closelog();
	} else {
		fflush(log_fp);
		dup2(stdout_fd_cache, 1);
		dup2(stderr_fd_cache, 2);
		fclose(log_fp);
	}
	log_fp = stdout = stdout_cache;
	stderr = stderr_cache;
	return 0;
}

static int __log(ovis_log_t log, int level, char *msg,
			    struct timeval *tv, struct tm *tm)
{
	if (log_fp == OVIS_LOG_SYSLOG) {
		syslog(__log_level_to_syslog(level), "%s: %s", log->name, msg);
		return 0;
	}

	int rc;
	FILE *f;
	if (!log_fp)
		f = stdout;
	else
		f = log_fp;
	if (default_modes & OVIS_LOG_M_TS) {
		rc = fprintf(f, "%lu.%06lu:", tv->tv_sec, tv->tv_usec);
	} else if (default_modes & OVIS_LOG_M_DT) {
		char dtsz[200];
		if (strftime(dtsz, sizeof(dtsz), "%a %b %d %H:%M:%S %Y", tm))
			rc = fprintf(f, "%s:", dtsz);
		else
			rc = -EINVAL; /* not expected with gnu libc */
	} else {
		rc = 0;
	}
	if (rc < 0)
		return rc;

	/* Print the level name */
	rc = fprintf(f, "%9s:", ((level == OVIS_LALWAYS)?"":ovis_loglevel_names[level]));
	if (rc < 0)
		return rc;

	rc = fprintf(f, " %s: %s", log->name, msg);
	if (rc < 0)
		return rc;
	return 0;
}

static int log_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t ev)
{
	int level;
	char *msg;
	struct timeval *tv;
	struct tm *tm;
	enum op_type type = EV_DATA(ev, struct log_data)->type;
	ovis_log_t log;
	char *path;

	switch (type) {
	case OVIS_LOG_O_LOG:
		level = EV_DATA(ev, struct log_data)->level;
		msg = EV_DATA(ev, struct log_data)->msg;
		tv = &EV_DATA(ev, struct log_data)->tv;
		tm = &EV_DATA(ev, struct log_data)->tm;
		log = EV_DATA(ev, struct log_data)->log;

		(void) __log(log, level, msg, tv, tm);
		if (0 == ev_pending(logger_w)) {
			if (log_fp != OVIS_LOG_SYSLOG)
				fflush(log_fp);
		}
		free(msg);
		__ovis_log_put(log);
		break;
	case OVIS_LOG_O_OPEN:
		path = EV_DATA(ev, struct log_data)->msg;
		(void) __log_reopen(path);
		free(path);
		break;
	case OVIS_LOG_O_CLOSE:
		__log_close();
		break;
	default:
		ovis_log(NULL, OVIS_LCRITICAL,
				"Unrecognized ovis_log's operation %d\n", type);
		assert(0);
		break;
	}

	ev_put(ev);
	return 0;
}

static int __cache_stdout_stderr()
{
	int rc;
	stdout_cache = stdout;
	stderr_cache = stderr;

	rc = dup2(1, stdout_fd_cache);
	if (rc) {
		ovis_log(NULL, OVIS_LERROR,
				"Failed to cache stdout original fd. "
						  "Error %d\n", rc);
		return rc;
	}
	rc = dup2(2, stderr_fd_cache);
	if (rc) {
		ovis_log(NULL, OVIS_LERROR,
				"Failed to cache stderr original fd."
						  "Error %d\n", rc);
		return rc;
	}
	return 0;
}

int ovis_log_init(const char *name, int default_level, int modes)
{
	if (is_init) {
		/* The worker has been initialized already. Do nothing. */
		return 0;
	}

	int rc;
	int need_free = 0;
	char s[PATH_MAX];

	if (!is_level_valid(default_level))
		return EINVAL;

	if (modes == 0) {
		default_modes = OVIS_LOG_M_DEFAULT;
	} else {
		if (!is_mode_valid(modes))
			return EINVAL;
		default_modes = modes;
	}
	if (name) {
		progname = strdup(name);
		if (!progname)
			return ENOMEM;
		need_free = 1;
	} else {
		progname = "";
	}

	default_log.name = progname;
	if (!default_log.name) {
		rc = ENOMEM;
		goto err;
	}

	default_log.level = default_level;

	/* Set up the worker */
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
	if (default_log.name[0] != '\0')
		free((char *)default_log.name);
	free(log_type);
	return rc;
}

void ovis_log_set_mode(int modes)
{
	default_modes = modes;
}

/** return a file pointer or a special syslog pointer */
int ovis_log_open(const char *path)
{
	int rc = 0;

	if (!path)
		return EINVAL;

	if (!stdout_cache) {
		/*
		 * The original stdout and stderr have never been cached.
		 * Cache them here.
		 */
		rc = __cache_stdout_stderr();
		if (rc)
			return rc;
	}

	if (!logger_w) {
		/*
		 * No log worker.
		 */
		log_fp = __log_open(path);
		if (!log_fp) {
			rc = errno;
			return rc;
		}
		stdout = stderr = log_fp;
	} else {
		/*
		 * Post the open event to the worker's queue.
		 */
		ev_t open_ev;
		char *s;

		s = strdup(path);
		if (!s)
			return ENOMEM;

		open_ev = ev_new(log_type);
		if (!open_ev) {
			free(s);
			return ENOMEM;
		}

		EV_DATA(open_ev, struct log_data)->type = OVIS_LOG_O_OPEN;
		EV_DATA(open_ev, struct log_data)->msg = s;
		rc = ev_post(NULL, logger_w, open_ev, NULL);
	}
	return rc;
}

int ovis_log_flush()
{
	if (log_fp && log_fp != OVIS_LOG_SYSLOG)
		return fflush(log_fp);
	return 0;
}

int ovis_log_close()
{
	ev_t close_ev;
	int rc;

	if (!logger_w) {
		/*
		 * No log workers. Close the log file now.
		 */
		rc = __log_close();
	} else {
		close_ev = ev_new(log_type);
		EV_DATA(close_ev, struct log_data)->type = OVIS_LOG_O_CLOSE;
		rc = ev_post(NULL, logger_w, close_ev, NULL);
	}
	return rc;
}

int ovis_log_rotate(const char *path){
	return ENOSYS;
}

int ovis_vlog(ovis_log_t log, int level, const char *fmt, va_list ap)
{
	ev_t log_ev;
	char *msg;
	int rc = 0;
	struct timeval tv;
	struct tm tm;
	time_t t;
	int lmask;

	if (!log)
		log = &default_log;
	/*
	 * Take a log handle reference to prevent it
	 * being destroyed while in this function.
	 */
	(void) __ovis_log_get(log);

	lmask = ((log->level == OVIS_LDEFAULT)?default_log.level:log->level);

	/*
	 * Assume that \c level is one of
	 * OVIS_LDEBUG, OVIS_LINFO, OVIS_LWARN, OVIS_LERROR, OVIS_LCRITICAL,
	 * and OVIS_LALWAYS.
	 */
	if (!(lmask & level)) {
		/* The given level is disabled. Do nothing. */
		goto out;
	}

	if (default_modes & OVIS_LOG_M_TS) {
		gettimeofday(&tv, NULL);

	} else {
		t = time(NULL);
		localtime_r(&t, &tm);
	}

	rc = vasprintf(&msg, fmt, ap);
	if (rc < 0) {
		rc = -ENOMEM;
		goto out;
	}

	if (!logger_w) {
		/* No workers, so directly log to the file. */
		rc = __log(log, level, msg, &tv, &tm);
		free(msg);
		goto out;
	}

	log_ev = ev_new(log_type);
	if (!log_ev) {
		rc = -ENOMEM;
		goto out;
	}
	EV_DATA(log_ev, struct log_data)->msg = msg;
	EV_DATA(log_ev, struct log_data)->level = level;
	EV_DATA(log_ev, struct log_data)->type = OVIS_LOG_O_LOG;
	EV_DATA(log_ev, struct log_data)->log = __ovis_log_get(log);

	if (default_modes & OVIS_LOG_M_TS)
		EV_DATA(log_ev, struct log_data)->tv = tv;
	else
		EV_DATA(log_ev, struct log_data)->tm = tm;
	ev_post(NULL, logger_w, log_ev, NULL);
out:
	__ovis_log_put(log); /* Put back the reference taken at the top of the function. */
	return rc;
}

int ovis_log(ovis_log_t log, int level, const char *fmt, ...)
{
	int rc;
	va_list ap;
	if (!is_init)
		log_fp = stdout;
	va_start(ap, fmt);
	rc = ovis_vlog(log, level, fmt, ap);
	va_end(ap);
	return rc;
}

/* All messages from the ldms library are of e level.*/
#define OVIS_LOG_AT(e,fsuf) \
int ovis_l##fsuf(ovis_log_t log, const char *fmt, ...) \
{ \
	int rc; \
	va_list ap; \
	va_start(ap, fmt); \
	rc = ovis_vlog(log, e, fmt, ap); \
	va_end(ap); \
	return rc; \
}

OVIS_LOG_AT(OVIS_LDEBUG, debug);
OVIS_LOG_AT(OVIS_LINFO, info);
OVIS_LOG_AT(OVIS_LWARNING, warning);
OVIS_LOG_AT(OVIS_LERROR, error);
OVIS_LOG_AT(OVIS_LCRITICAL, critical);
OVIS_LOG_AT(OVIS_LALWAYS, all);
