/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2020 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2020 Open Grid Computing, Inc. All rights reserved.
 *
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
#ifndef __LDMS_LOG_H__
#define __LDMS_LOG_H__
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>

/* LDMS_LOG present if ldmsd_log is being retired. */
#define LDMS_LOG
#define LDMS_LOGFILE_DEFAULT "/var/log/ldmsd.log"

#ifndef LDMS_LOG_ONLY
/* compile which CFLAGS += "-DLDMS_LOG_ONLY" to trap all old ldmsd/LDMSD usages.
 * Otherwise, the following back-compatible macros will keep old code working
 * until it is cleaned up.
 * Functions all redefined to their ldms_ equivalent.
 * nm will show no ldmsd_log related functions when this header is in use.
 */
#define LDMSD_STR_WRAP(NAME) #NAME
#define LDMSD_LWRAP(NAME) LDMSD_L ## NAME = LDMS_L ## NAME
#define ldmsd_loglevel ldms_loglevel
#define ldmsd_loglevel_names _Pragma ("GCC warning \"'ldmsd_loglevel_names' is deprecated. use ldms_loglevel_names.\"") ldms_loglevel_names
#define ldmsd_log ldms_log
#define ldmsd_loglevel_set ldms_loglevel_set
#define ldmsd_loglevel_get ldms_loglevel_get
#define ldmsd_str_to_loglevel ldms_str_to_loglevel
#define ldmsd_loglevel_to_str ldms_loglevel_to_str
#define ldmsd_ldebug ldms_ldebug
#define ldmsd_linfo ldms_linfo
#define ldmsd_lwarning ldms_lwarning
#define ldmsd_lerror ldms_lerror
#define ldmsd_lcritical ldms_lcritical
#define ldmsd_lall ldms_lall
#define __ldmsd_log __ldms_log
#define ldmsd_loglevel_to_syslog ldms_loglevel_to_syslog
#define ldmsd_msg_log_f ldms_msg_log_f
#define ldmsd_msg_logger ldms_msg_logger
#define ldmsd_logrotate ldms_logrotate
#define LDMSD_LOGFILE LDMS_LOGFILE_DEFAULT
#endif

/** This section is everything plugin and library clients might need.
 * The assumption for this section is that main() takes care of logging init.
 */
#define LDMS_DEFAULT_FILE_PERM 0600

/* The ldmsd_log facility has been converted to an ldms core library.
 * Aliases in old ldmsd style are provided for everything until the
 * transition is complete.
 */

#define LDMS_STR_WRAP(NAME) #NAME
#define LDMS_LWRAP(NAME) LDMS_L ## NAME
/**
 * \brief ldms log levels
 *
 * The ldms log levels, in order of increasing importance, are
 *  - DEBUG
 *  - INFO
 *  - WARNING
 *  - ERROR
 *  - CRITICAL
 *  - ALL
 *
 * ALL is for messages printed to the log file per users requests,
 * e.g, messages printed from the 'info' command.
 */
#define LOGLEVELS(WRAP) \
	WRAP (DEBUG), \
	WRAP (INFO), \
	WRAP (WARNING), \
	WRAP (ERROR), \
	WRAP (CRITICAL), \
	WRAP (ALL), \
	WRAP (LASTLEVEL),

/* LDMSD_ and LDMS_ loglevels are identical for transition to LDMS logging API */
enum ldms_loglevel {
	LDMS_LNONE = -1,
#ifndef LDMS_LOG_ONLY
	LDMSD_LNONE = -1,
#endif
	LOGLEVELS(LDMS_LWRAP)
#ifndef LDMS_LOG_ONLY
	LOGLEVELS(LDMSD_LWRAP)
#endif
};
#define ldmsd_loglevel ldms_loglevel

extern const char *ldms_loglevel_names[];
#define ldmsd_loglevel_names _Pragma ("GCC warning \"'ldmsd_loglevel_names' is deprecated. use ldms_loglevel_names.\"") ldms_loglevel_names

__attribute__((format(printf, 2, 3)))
void ldms_log(enum ldms_loglevel level, const char *fmt, ...);

int ldms_loglevel_set(char *verbose_level);

enum ldms_loglevel ldms_loglevel_get();

enum ldms_loglevel ldms_str_to_loglevel(const char *level_s);

const char *ldms_loglevel_to_str(enum ldms_loglevel level);

__attribute__((format(printf, 1, 2)))
void ldms_ldebug(const char *fmt, ...);

__attribute__((format(printf, 1, 2)))
void ldms_linfo(const char *fmt, ...);

__attribute__((format(printf, 1, 2)))
void ldms_lwarning(const char *fmt, ...);

__attribute__((format(printf, 1, 2)))
void ldms_lerror(const char *fmt, ...);

__attribute__((format(printf, 1, 2)))
void ldms_lcritical(const char *fmt, ...);

__attribute__((format(printf, 1, 2)))
void ldms_lall(const char *fmt, ...);

void __ldms_log(enum ldms_loglevel level, const char *fmt, va_list ap);

/** Get syslog int value for a level.
 *  \return LOG_CRIT for invalid inputs, NONE, & ENDLEVEL.
 */
int ldms_loglevel_to_syslog(enum ldms_loglevel level);

/** syslog compatible log function pointer. */
typedef void (*ldms_msg_log_f)(enum ldms_loglevel level, const char *fmt, ...);

/** thread unsafe wrapper of ldms_log */
void ldms_msg_logger(enum ldms_loglevel level, const char *fmt, ...);

int ldms_logrotate();

/** END of plugin client section. */

/** This section is for main() usage (init and cleanup) only. 
 * All LDMS plugins and libraries can be written assuming that
 * ldms_log_open has been completed by libldms or ldmsd main. Passing around pointers to
 * ldms_log or lower level log functions is no longer needed.
 */

/** parse verbosity level.
 * \param optarg one of QUIET, ALL,
 *    DEBUG, INFO, WARNING, ERROR, CRITICAL.
 */
int ldms_log_parse_verbosity(const char *optarg);

/** nerr = ldmsd_open_log(pname, lname, &errstr);
 * If not called, the log defaults to stdout.
 * \param pname name of the driver.
 * \param logfile is path of log file or 'syslog' to use vsyslog.
 * \param errstr output string pointer space.
 * \return einval, enomem, or a value 9-11 for cleanup.
 */
int ldms_log_open(const char *progname, const char* logfile, const char **errstr);

/** cleanup internal log data structures.
 */
void ldms_log_close();

#endif
