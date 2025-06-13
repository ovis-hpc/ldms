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
#ifndef _OVIS_LOG_H_
#define _OVIS_LOG_H_

#include <stdio.h>
#include "coll/rbt.h"

#define OVIS_DEFAULT_FILE_PERM 0600

/* Impossible file pointer as syslog-use sentinel */
#define OVIS_LOG_SYSLOG ((FILE*)0x7)

/**
 * \brief ovis log levels
 *
 * The ovis log levels, in order of increasing severity, are
 *  - DEBUG
 *  - INFO
 *  - WARNING
 *  - ERROR
 *  - CRITICAL
 * Two special levels are ALWAYS and QUIET.
 *  QUIET means to disable all log messages. On the other hand,
 *  any log messages belong to the ALWAYS level will be reported unless
 *  QUIET is enabled.
 */

#define OVIS_LDEFAULT	-1 /* Use the default level */
#define OVIS_LQUIET	0
#define OVIS_LDEBUG	0x01
#define OVIS_LINFO	0x02
#define OVIS_LWARN	0x04
#define OVIS_LWARNING	OVIS_LWARN
#define OVIS_LERROR	0x08
#define OVIS_LCRITICAL	0x10
#define OVIS_LCRIT	OVIS_LCRITICAL
/* OVIS_LALWAYS is always enabled unless QUIET is given. */
#define OVIS_LALWAYS	(OVIS_LDEBUG|OVIS_LINFO|OVIS_LWARN|OVIS_LERROR|OVIS_LCRITICAL)
#define OVIS_ALL_LEVELS	OVIS_LALWAYS

typedef struct ovis_log_s {
	const char *name;
	const char *desc;
	int level;
	struct rbn rbn;
	int ref_count;
} *ovis_log_t;

/**
 * \brief Initialize the logging system
 *
 * This funciton should be called by the application main initialization logic.
 * It should not be called by individual subsystems.
 *
 * \c ovis_log_init() creates the logging worker, sets the default values, and
 *  configures the logging modes.
 *
 * These settings are used when NULL is passed as the log handle to
 * ovis_log API.
 *
 * \c app_name  The application name.
 * \c level     The default log level or the bitwise-or of multiple log levels.
 * \c modes     Defines different logging behaviors.
 *
 * Available modes:
 *  OVIS_LOG_M_DT date-time format (%a %b %d %H:%M:%S %Y) is used to format
 *               message timestamps.
 *  OVIS_LOG_M TS timestamp (%lu) is used to format message timestamps.
 *  OVIS_LOG_M_TS_NONE Timestamps are not included in log messages. (default)
 *
 * The \c ovis_log_init() call makes \c ovis_log_open, \c ovis_log_close,
 *  \c ovis_log, and \c ovis_vlog asynchronous.
 *
 * \param subsys_name	The default log subsystem name, e.g., the application name.
 * \param level		The default log level.
 * \param modes		Logging modes. 0 means the default mode.
 *
 * \return 0 on success.
 *         ENOMEM is returned if it fails create the worker and the event type.
 *
 * \see ovis_log_open, ovis_log, ovis_log_close
 */
#define OVIS_LOG_M_DT	1	/* Date-time format */
#define OVIS_LOG_M_TS	2	/* Timestamp in seconds format */
#define OVIS_LOG_M_TS_NONE 4	/* No message timestamps */
/*
 * Currently, the time format is the only log mode.
 * Further on, we could extend the default mode to include other mode types
 * by using the bitwise-or operation.
 */
#define OVIS_LOG_M_DEFAULT OVIS_LOG_M_TS_NONE

int ovis_log_init(const char *default_subsys_name, int default_level, int modes);

/**
 * \brief Set the message format
 *
 *  See the available modes \c ovis_log_init
 *
 * \param mode   a mode
 *
 * \see ovis_log_init
 */
void ovis_log_set_mode(int mode);

/**
 * \brief Open a log file
 *
 * Call \c ovis_log_open() to redirect the log messages to a log file.
 *  If \c path is 'syslog', the log messages go to Syslog.
 *
 * The process of opening a log file is as follows.
 *  - If a log file exists,
 *    - the outstanding messages are flushed, and
 *    - the current log file is closed.
 *  - If \c path is Syslog, the subsequent messages go to Syslog.
 *  - Otherwise, a new log file is opened at \c path.
 *  - Point both stdout and stderr to the new log file.
 *
 * The above steps happen asynchronously to the \c ovis_log_open() call.
 *  \c ovis_log_open() posts a event to the log worker when \c ovis_log_init()
 *  is called. The success return value of \c ovis_log_open() does not mean
 *  that the log file is opened successfully.

 * \c ovis_log_open() is synchronous if applications have not called \c ovis_log_init().
 *
 * All log messages go to stdout when no log files are successfully opened.
 *
 *
 * \param path   Path to the log file or 'syslog'
 *
 * \return 0 on success. Otherwise, an errno is returned.
 * \see ovis_log_init, ovis_log, ovis_log_close, ovis_log_flush
 */
int ovis_log_open(const char *path);

/**
 * \brief Flush the outstanding messages to the log file
 *
 * \c ovis_log_flush() is equivalent to calling \c fflush().
 *  If the log messages go to Syslog, the function is a no-op.
 *
 * \return 0 on success. Otherwise, an errno is returned.
 */
int ovis_log_flush();

/**
 * \brief Close the current log file
 *
 * After calling \c ovis_log_close(), \c ovis_log() prints the subsequent messages
 *  to stdout. A call to \c ovis_log_close() is discourage unless
 *  applications will not log any more messages.
 *
 * \return 0 on success. Otherwise, an errno is returned.
 * \see ovis_log_reopen, ovis_log_open
 */
int ovis_log_close();

/**
 * \brief  Rotate the log file
 *
 * Not supported yet.
 * Please use \c ovis_log_open to implement your log rotate functionality.
 *
 * The current log file is renamed and then closed.
 *  The new log file is opened at \c path.
 *  If \c path is NULL, the log file is opened at the previous path.
 *
 * If prior to the call messages going to Syslog, the function is a no-op.
 *
 * \param path   Path of the new log file. NULL to used the path of the current file.
 *
 * \return 0 on success. Otherwise, an errno is returned.
 */
int ovis_log_rotate(const char *path);

/**
 * \brief Register a substem log
 *
 * \param subsys_name   The subsystem name
 * \param desc		A description of the subsystem
 *
 * \return The log handle of the subsystem.
 *         On error, NULL is returned, and errno is se as follows:
 *            EINVAL   Either \c subsys_name or \c desc is NULL.
 *            EEXIST   The log of the subsystem \c subsys_name already exists.
 *            ENOMEM   Memory allocation failure
 */
ovis_log_t ovis_log_register(const char *subsys_name, const char *desc);

/**
 * \brief Destroy the log of a subsystem
 *
 * \param log   the log handle of a subsystem
 */
void ovis_log_deregister(ovis_log_t log);

/**
 * \brief List the information of a log subsystem or all log subsystems
 *
 * The returned string is a JSON-formatted string.
 * [{"name": "<subsystem's name>",
 *   "desc": "<subsystem's description>",
 *   "level": "<subsystem's enabled level string>"
 *  },
 *  ...
 * ]
 *
 * The caller is responsible for freeing the returned string.
 *
 * \param subsys   A subsystem name. If this is NULL,
 *                 the information of all subsystems is returned.
 *
 * \return a string on success. Otherwise, NULL is returned, and errno is set.
 *
 */
char *ovis_log_list(const char *subsys);

/**
 * \brief Log a message
 *
 * Both \c ovis_log() and \c ovis_vlog() log a message. The difference is that
 *  \c ovis_log() receives variable length arguments, but \c ovis_vlog()
 *  receives \c va_list.
 *
 * The format of the log messages is
 *            <logging time>: <log level>: <subsystem name>: <message>.
 *  The <logging time> is either a timestamp or a date-time string. Specify
 *  the mode when calling \c ovis_log_init() to configure the format.
 *  The <log level> will not show if the message belongs to the OVIS_LALWAYS level.
 *  The <subsystem name> is the subsystem the message belongs to.
 *
 * \param log     The log handle of a subsystem
 * \param level   Log level of the message
 *
 * \return 0 on success. On errors, a negative number is returned.
 */
__attribute__(( format(printf, 3, 4) ))
int ovis_log(ovis_log_t log, int level, const char *fmt, ...);
int ovis_vlog(ovis_log_t log, int level, const char *fmt, va_list ap);

/**
 * \brief Set the log level of a subsystem.
 *
 * If \c level is OVIS_LDEFAULT, the subsystem is set to the default level.
 *  If \c subsys_name or \c mylog is NULL, and \c level is OVIS_LDEFAULT,
 *  \c level is ignored and 0 is returned.
 *
 * \param regex_s         A regular expression string to match subsystem names
 * \param subsys_name     The name of the subsystem to set the log level.
 *                        If NULL is given, the default log level will be set.
 * \param mylog           A log subsystem handle
 * \param level           The log levels to be enabled. This could be
 *                        the bitwise-or of multiple log levels.
 *
 * \return 0 on success. On error, an errno is returned.
 *         ENOENT   Subsystem \c subsys_name does not exist.
 *         EINVAL   \c level is invalid.
 */
int ovis_log_set_level_by_regex(const char *regex_s, int level);
int ovis_log_set_level_by_name(const char *subsys_name, int level);
int ovis_log_set_level(ovis_log_t mylog, int level);

/**
 * \brief Set the level of the default log.
 *
 * If \c level is OVIS_LDEFAULT, the subsystem is set to the default level.
 *
 * \param level           The log levels to be enabled. This could be
 *                        the bitwise-or of multiple log levels.
 *
 * \return 0 on success. On error, an errno is returned.
 *         EINVAL   \c level is invalid.
 */
int ovis_log_default_set_level(int level);

/**
 * \brief Get the log level of a subsystem
 *
 * \param subsys_name    A subsystem name. If NULL is given, the default log level is returned.
 * \param mylog          A log subsystem handle
 *
 * \return the bitwise-or of the enabled log levels.
 *         The OVIS_LDEFAULT value is returned if the subsystem uses the default level.
 *         On error, a negative errno is returned.
 *          -ENOENT        Subsystem \c subsys_name does not exist.
 */
int ovis_log_get_level_by_name(const char *subsys_name);
int ovis_log_get_level(ovis_log_t mylog);

/*
 * \brief Convert a string to the bitwise-or of log level integers.
 *
 * For example,
 *    "QUIET" is converted to 0.
 *    "ERROR,INFO" is converted to OVIS_LERROR|OVIS_LINFO.
 *    "ERROR," is converted to OVIS_LERROR.
 *    "ERROR" is converted to OVIS_LERROR|OVIS_LCRITICAL, i.e., the log levels equal and above.
 *
 * \param level_s   The string 'QUIET' or a comma-seprated string of log level names.
 *
 * \return 0 or the bitwise-or of log levels. On errors, a negative errno is returned.
 *        -EINVAL is returned if the string contains an unrecognized level name.
 */
int ovis_log_str_to_level(const char *level_s);

/*
 * \brief Return a comma-separated string of the level names
 *
 * If \c level is the bitwise-or of multiple log levels,
 * the returned string is the comma-separated string of each log level name.
 *
 * If \c level is the value of a single log level,
 * the returned string is the log level name appended with a comma "<level>,".
 *
 * NOTE: The caller must free the returned string.
 *
 * For example,
 *   \c str = ovis_log_level_to_str(OVIS_LDEBUG|OVIS_LERROR)
 *   str is "DEBUG,ERROR"
 *
 *   \c str = ovis_log_level_to_str(OVIS_LERROR|OVIS_LCRITICAL)
 *   str is "ERROR,CRITICAL"
 *
 *   \c str = ovis_log_level_to_str(OVIS_LCRITICAL)
 *   str is "CRITICAL,"
 *
 * \param level   a log level value or the bitwise-or of multiple log levels.
 *
 * \return a string. On errors, NULL is returned, and errno is set.
 */
char *ovis_log_level_to_str(int level);

#endif
