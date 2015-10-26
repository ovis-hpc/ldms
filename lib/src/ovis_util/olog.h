#ifndef ovis_util_log_h_seen
#define ovis_util_log_h_seen
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


/**
 * \brief Log levels
 */
#define OL_STR_WRAP(NAME) #NAME,
#define OL_LEVELWRAP(NAME) OL_ ## NAME,

/* a command line alias of user is ALWAYS or QUIET for back compatibility.
 */
#define LOGLEVELS(WRAP) \
        WRAP (DEBUG) \
        WRAP (INFO) \
        WRAP (WARN) \
        WRAP (ERROR) \
        WRAP (CRITICAL) \
        WRAP (USER) \
        WRAP (ENDLEVEL)

/**
 The USER level is for messages that are requested by the user.
 OL_NONE,OL_ENDLEVEL provide termination tests for iterating.
*/
typedef enum ovis_loglevels {
	OL_NONE = -1,
        LOGLEVELS(OL_LEVELWRAP)
} ovis_loglevels_t;

typedef void (*ovis_log_fn_t)(ovis_loglevels_t level, const char *fmt, ...);

/** Connect logger to file with logname given or syslog if name given contains
  exactly "syslog".
  \param progname argv[0] from the application, normally.
  \return 0 or errno detected if it fails.
 */
extern int ovis_log_init(const char *progname, const char *logname, const char *level);

/** Disconnect logger. */
extern void ovis_log_final();

/** Update the threshold for output. */
extern void ovis_log_level_set(ovis_loglevels_t level);

/** Get the current threshold for output. */
extern ovis_loglevels_t ovis_log_level_get();

/** Get level from a string, including various aliases. 
	\return OL_NONE if string not recognized.
*/
extern ovis_loglevels_t ol_to_level(const char *string);

/** Get canonical level name as string. */
extern const char * ol_to_string(ovis_loglevels_t level);

/** Get syslog int value for a level.
	\return LOG_CRIT for invalid inputs, NONE, & ENDLEVEL.
*/
extern int ol_to_syslog(ovis_loglevels_t level);

/** Log a message at given level. Defaults to stdout if 
 * ovis_log_init is not called successfully.
 */
extern void olog(ovis_loglevels_t level, const char *fmt, ...);

/** Log a debug message */
extern void oldebug(const char *fmt, ...);

/** Log an informational message */
extern void olinfo(const char *fmt, ...);

/** Log a condition that someone might worry about */
extern void olwarn(const char *fmt, ...);

/** Log a condition that someone should fix eventually */
extern void olerr(const char *fmt, ...);

/** Log startup/shutdown or a condition that someone should fix soon */
extern void olcrit(const char *fmt, ...);

/** Log data at the request of the user (aka always, supreme) */
extern void oluser(const char *fmt, ...);

/** Flush log file (if not syslog).
 * \return fflush result or 0.
 */
extern int olflush();

/** Close and reopen log after some other agent has renamed it out
of the way.  If log is syslog, does nothing. */
extern void ovis_logrotate();

/** Return the short string (from errno.h macro definitions) name that
 * matches the errno value. In the event of unknown or negative rc,
 * returns 'UNKNOWN(%d)" formatted result that will not change before
 * the next call to rcname.
 */
extern const char *ovis_rcname(int rc);

/**
 * \brief Similar to perror(str), but print stuffs to ovis log instead.
 */
#define oerror(str) olerr("%s:%d, %s(), %s: errno: %d, msg: %m\n", \
                                __FILE__, __LINE__, __func__, \
                                str, errno)

#define oerrorrc(str, rc) olerr("%s:%d, %s(), %s: errno(%d): %s\n", \
                                __FILE__, __LINE__, __func__, \
                                str, (rc), ovis_rcname(rc))

#endif /* ovis_util_log_h_seen */
