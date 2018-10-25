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

#ifndef __KVD_LOG__
#define __KVD_LOG__

extern FILE *kvd_log_file;
extern int __kvd_log_level;

extern void __kvd_log(const char *fmt, ...);

enum kvd_log_level {
	KVD_LOG_LV_DEBUG = 0,
	KVD_LOG_LV_INFO,
	KVD_LOG_LV_WARN,
	KVD_LOG_LV_ERR,
	KVD_LOG_LV_QUIET,
	KVD_LOG_LV_LAST,
};

#define kvd_log(lv, fmt, ...) do {\
	if (KVD_LOG_LV_ ## lv >= __kvd_log_level) \
		__kvd_log(#lv ": " fmt "\n", ##__VA_ARGS__);\
} while(0)

#ifndef KVD_LOG_LEVEL
# define KVD_LOG_LEVEL KVD_LOG_LV_DEBUG
#endif

/**
 * \brief Print *DEBUG* message to a default log.
 */
#define kdebug(fmt, ...) kvd_log(DEBUG, fmt, ##__VA_ARGS__)

/**
 * \brief Print *INFO* message to a default log.
 */
#define kinfo(fmt, ...) kvd_log(INFO, fmt, ##__VA_ARGS__)

/**
 * \brief Print *WARN* message to a default log.
 */
#define kwarn(fmt, ...) kvd_log(WARN, fmt, ##__VA_ARGS__)

/**
 * \brief Print *ERR* message to a default log.
 */
#define kerr(fmt, ...) kvd_log(ERR, fmt, ##__VA_ARGS__)

/**
 * \brief Similar to perror(str), but print stuff to the log instead
 */
#define kerror(str) kerr("%s:%d, %s(), %s: errno: %d, msg: %m\n", \
			 __FILE__, __LINE__, __func__,		  \
			 str, errno)

/**
 * \brief Set log level.
 * \param level One of the enumeration ::kvd_log_level.
 */
void kvd_log_set_level(int level);

/**
 * \brief Same as kvd_log_set_level(), but with string option instead.
 *
 * Valid values of \c level include:
 *   - "DEBUG"
 *   - "INFO"
 *   - "WARN"
 *   - "ERROR"
 *   - "QUIET"
 *   - "0"
 *   - "1"
 *   - "2"
 *   - "3"
 *   - "4"
 *   - "D"
 *   - "I"
 *   - "W"
 *   - "E"
 *   - "Q"
 *
 * \retval 0 if OK.
 * \retval EINVAL if the input \c level is invalid.
 */
int kvd_log_set_level_str(const char *level);

/**
 * Get kvd_log level.
 *
 * \retval LVL The current log level.
 */
int kvd_log_get_level();

/**
 * \brief Set \a f as a log file.
 * \param f The log file. \a f should be opened.
 */
void kvd_log_set_file(FILE *f);

/**
 * \brief Open \a path for logging.
 * \param path The path to the file to be used for logging.
 * \return 0 on success.
 * \return -1 on failure. (errno should be set accordingly)
 */
int kvd_log_open(const char *path);

/**
 * \brief Close the log file.
 * This is essentially a wrapper to calling fclose(kvd_log_file).
 * Hence, errno will be set accordingly on failure.
 * \return 0 on success.
 * \return <b>EOF</b> on failure.
 */
int kvd_log_close();

/**
 * \brief Flush the log file.
 * This is essentially a wrapper to calling fflush(kvd_log_file).
 * Hence, errno will be set accordingly on failure.
 * \return 0 on success.
 * \return <b>EOF</b> on failure.
 */
int kvd_log_flush();

#endif
