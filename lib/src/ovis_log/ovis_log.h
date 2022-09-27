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

#define OVIS_DEFAULT_FILE_PERM 0600

/* Impossible file pointer as syslog-use sentinel */
#define OVIS_LOG_SYSLOG ((FILE*)0x7)

#define OVIS_STR_WRAP(NAME) #NAME
#define OVIS_LWRAP(NAME) OVIS_L ## NAME
/**
 * \brief ldmsd log levels
 *
 * The ldmsd log levels, in order of increasing importance, are
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

enum ovis_loglevel {
	OVIS_LNONE = -1,
	LOGLEVELS(OVIS_LWRAP)
};

/**
 * \brief Initialize the logging system
 *
 * It creates a log worker thread. By calling this,
 * \c ovis_log() is a non-block call. The slowness of the file system
 * will not affect the thread that calls \c ovis_log().
 *
 * \param name   Name of logger (worker)
 *
 * \return 0 on success.
 *         ENOMEM is returned if it fails create the worker and the event type.
 * \see ovis_log_open, ovis_log
 */
int ovis_log_init(const char *name);

/* \brief Open a log file
 *
 * The function opens the given log file. If the log file exists,
 * calling \c ovis_log() will append the messages to the existing file.
 * It also points both stdout and stderr to the opened file.
 *
 * If the string 'syslog' is given, the messages will be sent to syslog.
 *
 * If applications do not call this function,
 * the messages will be logged to stdout.
 *
 * Applications must call \c ovis_log_open only once. To change the log file,
 *  applications should call \c ovis_log_reopen().
 *
 * \param logfile   Path to the log file or 'syslog'
 *
 * \return 0 on success. Otherwise, an errno is returned.
 * \see ovis_log_init, ovis_log
 */
int ovis_log_open(const char *logfile);

/**
 * \brief Flush the outstanding messages
 *
 * \c ovis_log_fflush() is equivalent to calling \c fflush().
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
 * Do not call \c ovis_log_reopen() after calling this function. To close and open,
 *  call \c ovis_log_reopen() will suffice.
 *
 * \return 0 on success. Otherwise, an errno is returned.
 * \see ovis_log_reopen, ovis_log_open
 */
int ovis_log_close();

/**
 * \brief Open a new log file
 *
 * \c ovis_log_reopen() closes the current log file and opens a new file at \c path,
 *  although \c path is the same as the path of the current log file. The function
 *  will print the subsequent messages to the new file.
 *
 * If \c path is 'syslog', \c ovis_log_reopen() will close the current log file
 *  ane print the subsequent messages to Syslog.
 *
 * Calling \c ovis_log_reopen() after calling \c ovis_log_close() may result
 *  in a crash.
 *
 * \param new_file   The path of the new log file.
 *
 * \return 0 on success. Otherwise, an errno is returned.
 * \see ovis_log_close, ovis_log_open
 */
int ovis_log_reopen(const char *path);

/*
 * \brief Log a message
 *
 * Both \c ovis_log() and \c ovis_vlog() log a message. The difference is that
 *  \c ovis_log() receives variable length arguments, but \c ovis_vlog()
 *  receives \c va_list.
 *
 * \param level   Log level of the message
 */
void ovis_log(enum ovis_loglevel level, const char *fmt, ...);
void ovis_vlog(enum ovis_loglevel level, const char *fmt, va_list ap);

/*
 * \brief Set the log level
 *
 * \param verbose_level   Log level string
 *
 * \return 0 on success. If \c verbose_level is invalid, -1 is returned.
 */
int ovis_loglevel_set(char *verbose_level);

/*
 * \brief Get the log level threshold
 *
 * \return An enumerate of a log level
 */
enum ovis_loglevel ovis_loglevel_get();

/*
 * \brief Convert a string to a log level enumerate
 *
 * The valid strings are DEBUG, INFO, WARNING, ERROR, CRITICAL, ALL.
 *
 * \param level_s   A string
 *
 * return log level enumerate. -1 is returned if the string is unrecognized.
 */
enum ovis_loglevel ovis_str_to_loglevel(const char *level_s);

/*
 * \brief Convert a log level enumerate to a string
 *
 * \param level   an integer
 *
 * return a log level string. 'OVIS_LNONE' is returned if the given integer is invalid.
 */
const char *ovis_loglevel_to_str(enum ovis_loglevel level);

/** Get syslog int value for a level.
 *  \return LOG_CRIT for invalid inputs, NONE, & ENDLEVEL.
 */
int ovis_loglevel_to_syslog(enum ovis_loglevel level);

#endif
