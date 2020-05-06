/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2015 Sandia Corporation. All rights reserved.
 * Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
 * license for use of this work by or on behalf of the U.S. Government.
 * Export of this program may require a license from the United States
 * Government.
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

#include <olog.h>
#include <stdio.h>
#include <pthread.h>
#include <syslog.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define OVIS_LOG_SYSLOG ((FILE*)0x7)
#define OVIS_LOG_CLOSED ((FILE*)0xF7)

FILE *log_fp = NULL;

#define LOG_NAME_MAX 512
char logname[LOG_NAME_MAX];

static ovis_loglevels_t log_level = OL_ERROR; /* default */

static const char* loglevels_names[] = {
	LOGLEVELS(OL_STR_WRAP)
	NULL
};

static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

void ovis_log_level_set(ovis_loglevels_t level)
{
	if ((level > OL_NONE) && (level < OL_ENDLEVEL)) {
		log_level = level;
	}
}

ovis_loglevels_t ovis_log_level_get()
{
	return log_level;
}

/* redirect stdout, stderr to logname. */
static FILE * open_log(int *status) {
	*status = 0;
	FILE *file = fopen(logname, "a");
	if (!file) {
		*status = errno;
		log_fp = stdout;
		olerr("Could not open the log file named '%s'\n", logname);
		return NULL;
	}
	int fd = fileno(file);		     
	if (dup2(fd, 1) < 0) {
		*status = errno;
		olog(OL_ERROR, "Failed stdout>%s\n", logname);
		fclose(file);
		return NULL;
	}
	if (dup2(fd, 2) < 0) {
		*status = errno;
		olog(OL_ERROR, "Failed stderr>%s\n", logname);
		return NULL;
	}
	stdout = file;
	stderr = file;
	return file;
}

void ovis_logrotate() {
	if (log_fp && log_fp != OVIS_LOG_SYSLOG) {
		pthread_mutex_lock(&log_lock);
		int rc = 0;
		FILE *new_log = open_log(&rc);
		if (new_log) {
			fflush(log_fp);
			fclose(log_fp);
			log_fp = new_log;
		} else {
			fprintf(log_fp,"logrotate failed. %d %s\n",
				rc, strerror(rc));
		}
		pthread_mutex_unlock(&log_lock);
	}
}

int ovis_log_init(const char * progname, const char *logfile, const char *s)
{
	int rc = 0;
	if (OL_NONE == ol_to_level(s)) {
		return EINVAL;
	}
	int len = snprintf(logname,LOG_NAME_MAX-1,"%s",logfile);
	if (len >= LOG_NAME_MAX-1) {
		return EINVAL;
	}
	if (strcasecmp(logfile,"syslog")==0) {
		log_fp = OVIS_LOG_SYSLOG;
		openlog(progname, LOG_NDELAY|LOG_PID, LOG_DAEMON);
		return 0;
	}
	log_fp = open_log(&rc);
	if (!log_fp) {
		log_fp = stdout;
		olerr("rc = %d %s\n",rc, strerror(rc));
	}

	return rc;
}

void ovis_log_final()
{
	if (log_fp && log_fp != OVIS_LOG_SYSLOG) {
		pthread_mutex_lock(&log_lock);
		log_fp = OVIS_LOG_CLOSED;
		/* not actually closing file here. */
		pthread_mutex_unlock(&log_lock);
	}
	log_fp = OVIS_LOG_CLOSED;
}

static
void olog_internal(ovis_loglevels_t level, const char *fmt, va_list ap)
{
	/* Don't say a word when the level is below the threshold */
	if (level < log_level || log_fp == OVIS_LOG_CLOSED)
		return;
	if (log_fp == OVIS_LOG_SYSLOG) {
		vsyslog(ol_to_syslog(level),fmt,ap);
		return;
	}
	pthread_mutex_lock(&log_lock);
	if (!log_fp) {
		log_fp = stdout;
	}
#ifdef LOGRTC
	struct timespec ts;
	if (clock_gettime(CLOCK_REALTIME,&ts) != 0) {
		ts.tv_sec= 0;
		ts.tv_nsec=0;
	}
	fprintf(log_fp,"%lu:%9lu: ",ts.tv_sec, ts.tv_nsec);
#else
	time_t t;
	struct tm tm;
	char dtsz[200];

	t = time(NULL);
	localtime_r(&t, &tm);
	if (strftime(dtsz, sizeof(dtsz), "%a %b %d %H:%M:%S %Y", &tm))
		fprintf(log_fp, "%s: ", dtsz);
#endif
	if ((level >= 0) && (level < OL_ENDLEVEL)) {
		fprintf(log_fp, "%-10s: ", loglevels_names[level]);
	}
	vfprintf(log_fp, fmt, ap);
	fflush(log_fp);
	pthread_mutex_unlock(&log_lock);
}

ovis_loglevels_t ol_to_level(const char *level_s)
{
	int i;
	
	for (i = 0; i < OL_ENDLEVEL; i++){
		if (0 == strcasecmp(level_s, loglevels_names[i])){
			return i;
		}
	}
	if (strcasecmp(level_s,"QUIET") == 0) {
		return OL_USER;
	}
	if (strcasecmp(level_s,"ALWAYS") == 0) {
		return OL_USER;
	}
	if (strcasecmp(level_s,"CRIT") == 0) {
		return OL_CRITICAL;
	}

	return OL_NONE;
}

const char *ol_to_string(ovis_loglevels_t level)
{
	if ((level >= OL_DEBUG) && (level < OL_ENDLEVEL))
		return loglevels_names[level];
	return "OL_NONE";
}

void olog(ovis_loglevels_t level, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	olog_internal(level, fmt, ap);
	va_end(ap);
}

#define LVLMSG(upper,lower) \
void ol##lower(const char *fmt, ...) \
{ \
	va_list ap; \
	va_start(ap, fmt); \
	olog_internal(OL_##upper, fmt, ap); \
	va_end(ap); \
}

LVLMSG(DEBUG,debug);
LVLMSG(INFO,info);
LVLMSG(WARN,warn);
LVLMSG(ERROR,err);
LVLMSG(CRITICAL,crit);
LVLMSG(USER,user);

int ol_to_syslog(int level)
{
	switch(level) {
#define MAPLOG(X,Y) case OL_##X: return LOG_##Y
	MAPLOG(DEBUG,DEBUG);
	MAPLOG(INFO,INFO);
	MAPLOG(WARN,WARNING);
	MAPLOG(ERROR,ERR);
	MAPLOG(CRITICAL,CRIT);
	MAPLOG(USER,ALERT);
	default:
		return LOG_ERR;
	}
#undef MAPLOG	
}

int olog_flush()
{
	if (log_fp != OVIS_LOG_SYSLOG) {
		return fflush(log_fp);
	}
	return 0;
}

const char *ovis_rcname(int rc)
{
	static const char *str[] = {
		[EPERM]            =  "EPERM",
		[ENOENT]           =  "ENOENT",
		[ESRCH]            =  "ESRCH",
		[EINTR]            =  "EINTR",
		[EIO]              =  "EIO",
		[ENXIO]            =  "ENXIO",
		[E2BIG]            =  "E2BIG",
		[ENOEXEC]          =  "ENOEXEC",
		[EBADF]            =  "EBADF",
		[ECHILD]           =  "ECHILD",
		[EAGAIN]           =  "EAGAIN",
		[ENOMEM]           =  "ENOMEM",
		[EACCES]           =  "EACCES",
		[EFAULT]           =  "EFAULT",
		[ENOTBLK]          =  "ENOTBLK",
		[EBUSY]            =  "EBUSY",
		[EEXIST]           =  "EEXIST",
		[EXDEV]            =  "EXDEV",
		[ENODEV]           =  "ENODEV",
		[ENOTDIR]          =  "ENOTDIR",
		[EISDIR]           =  "EISDIR",
		[EINVAL]           =  "EINVAL",
		[ENFILE]           =  "ENFILE",
		[EMFILE]           =  "EMFILE",
		[ENOTTY]           =  "ENOTTY",
		[ETXTBSY]          =  "ETXTBSY",
		[EFBIG]            =  "EFBIG",
		[ENOSPC]           =  "ENOSPC",
		[ESPIPE]           =  "ESPIPE",
		[EROFS]            =  "EROFS",
		[EMLINK]           =  "EMLINK",
		[EPIPE]            =  "EPIPE",
		[EDOM]             =  "EDOM",
		[ERANGE]           =  "ERANGE",
		[EDEADLK]          =  "EDEADLK",
		[ENAMETOOLONG]     =  "ENAMETOOLONG",
		[ENOLCK]           =  "ENOLCK",
		[ENOSYS]           =  "ENOSYS",
		[ENOTEMPTY]        =  "ENOTEMPTY",
		[ELOOP]            =  "ELOOP",
		[ENOMSG]           =  "ENOMSG",
		[EIDRM]            =  "EIDRM",
		[ECHRNG]           =  "ECHRNG",
		[EL2NSYNC]         =  "EL2NSYNC",
		[EL3HLT]           =  "EL3HLT",
		[EL3RST]           =  "EL3RST",
		[ELNRNG]           =  "ELNRNG",
		[EUNATCH]          =  "EUNATCH",
		[ENOCSI]           =  "ENOCSI",
		[EL2HLT]           =  "EL2HLT",
		[EBADE]            =  "EBADE",
		[EBADR]            =  "EBADR",
		[EXFULL]           =  "EXFULL",
		[ENOANO]           =  "ENOANO",
		[EBADRQC]          =  "EBADRQC",
		[EBADSLT]          =  "EBADSLT",
		[EBFONT]           =  "EBFONT",
		[ENOSTR]           =  "ENOSTR",
		[ENODATA]          =  "ENODATA",
		[ETIME]            =  "ETIME",
		[ENOSR]            =  "ENOSR",
		[ENONET]           =  "ENONET",
		[ENOPKG]           =  "ENOPKG",
		[EREMOTE]          =  "EREMOTE",
		[ENOLINK]          =  "ENOLINK",
		[EADV]             =  "EADV",
		[ESRMNT]           =  "ESRMNT",
		[ECOMM]            =  "ECOMM",
		[EPROTO]           =  "EPROTO",
		[EMULTIHOP]        =  "EMULTIHOP",
		[EDOTDOT]          =  "EDOTDOT",
		[EBADMSG]          =  "EBADMSG",
		[EOVERFLOW]        =  "EOVERFLOW",
		[ENOTUNIQ]         =  "ENOTUNIQ",
		[EBADFD]           =  "EBADFD",
		[EREMCHG]          =  "EREMCHG",
		[ELIBACC]          =  "ELIBACC",
		[ELIBBAD]          =  "ELIBBAD",
		[ELIBSCN]          =  "ELIBSCN",
		[ELIBMAX]          =  "ELIBMAX",
		[ELIBEXEC]         =  "ELIBEXEC",
		[EILSEQ]           =  "EILSEQ",
		[ERESTART]         =  "ERESTART",
		[ESTRPIPE]         =  "ESTRPIPE",
		[EUSERS]           =  "EUSERS",
		[ENOTSOCK]         =  "ENOTSOCK",
		[EDESTADDRREQ]     =  "EDESTADDRREQ",
		[EMSGSIZE]         =  "EMSGSIZE",
		[EPROTOTYPE]       =  "EPROTOTYPE",
		[ENOPROTOOPT]      =  "ENOPROTOOPT",
		[EPROTONOSUPPORT]  =  "EPROTONOSUPPORT",
		[ESOCKTNOSUPPORT]  =  "ESOCKTNOSUPPORT",
		[EOPNOTSUPP]       =  "EOPNOTSUPP",
		[EPFNOSUPPORT]     =  "EPFNOSUPPORT",
		[EAFNOSUPPORT]     =  "EAFNOSUPPORT",
		[EADDRINUSE]       =  "EADDRINUSE",
		[EADDRNOTAVAIL]    =  "EADDRNOTAVAIL",
		[ENETDOWN]         =  "ENETDOWN",
		[ENETUNREACH]      =  "ENETUNREACH",
		[ENETRESET]        =  "ENETRESET",
		[ECONNABORTED]     =  "ECONNABORTED",
		[ECONNRESET]       =  "ECONNRESET",
		[ENOBUFS]          =  "ENOBUFS",
		[EISCONN]          =  "EISCONN",
		[ENOTCONN]         =  "ENOTCONN",
		[ESHUTDOWN]        =  "ESHUTDOWN",
		[ETOOMANYREFS]     =  "ETOOMANYREFS",
		[ETIMEDOUT]        =  "ETIMEDOUT",
		[ECONNREFUSED]     =  "ECONNREFUSED",
		[EHOSTDOWN]        =  "EHOSTDOWN",
		[EHOSTUNREACH]     =  "EHOSTUNREACH",
		[EALREADY]         =  "EALREADY",
		[EINPROGRESS]      =  "EINPROGRESS",
		[ESTALE]           =  "ESTALE",
		[EUCLEAN]          =  "EUCLEAN",
		[ENOTNAM]          =  "ENOTNAM",
		[ENAVAIL]          =  "ENAVAIL",
		[EISNAM]           =  "EISNAM",
		[EREMOTEIO]        =  "EREMOTEIO",
		[EDQUOT]           =  "EDQUOT",
		[ENOMEDIUM]        =  "ENOMEDIUM",
		[EMEDIUMTYPE]      =  "EMEDIUMTYPE",
		[ECANCELED]        =  "ECANCELED",
		[ENOKEY]           =  "ENOKEY",
		[EKEYEXPIRED]      =  "EKEYEXPIRED",
		[EKEYREVOKED]      =  "EKEYREVOKED",
		[EKEYREJECTED]     =  "EKEYREJECTED",
		[EOWNERDEAD]       =  "EOWNERDEAD",
		[ENOTRECOVERABLE]  =  "ENOTRECOVERABLE",
	};
	static const int len = sizeof(str)/sizeof(*str);
	static char unk[32];
	if (rc < 0 || len <= rc || !str[rc]) {
		snprintf(unk,32,"UNKNOWN(%d)",rc);
		return unk;
	}
	return str[rc];
}
