/*
 * Copyright (c) 2016 Sandia Corporation. All rights reserved.
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
/*
 * This file eliminates common code from each csv-oriented store plugin.
 */
#ifndef store_csv_common_h_seen
#define store_csv_common_h_seen

#define _GNU_SOURCE

#include <libgen.h>
#include <stdbool.h>
#include <ovis_util/util.h>
#include <ovis_util/notification.h>
#include "ldmsd.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))
#endif

/** Common override parameters for "config action=custom" settings. */
#define STOREK_COMMON \
	/** The full path of an ovis notification output.  NULL indicates no notices wanted.  */ \
        char *notify; \
	bool notify_isfifo; \
	/** The full path template for renaming closed outputs. NULL indicates no renames wanted. */ \
        char *rename_template; \
	uid_t rename_uid; \
	gid_t rename_gid; \
	unsigned rename_perm; \
	uid_t create_uid; \
	gid_t create_gid; \
	unsigned create_perm;

struct storek_common {
	STOREK_COMMON;
};

/* Convert private storek handle pointer into storek_common pointer, if
 * storek handle was declared containing a STOREK_COMMON block.
 */
#define CSKC(x) \
	((struct storek_common *)&((x)->notify))

#define NOTIFY_COMMON \
	STOREK_COMMON; \
	struct ovis_notification *onp; \
	int hooks_closed

/* containment for globals. Ideally would hold most and not just the new ones
 * for notification. */
struct csv_plugin_static {
	/* notification channel for file events, unless overridden. */
	NOTIFY_COMMON;
	/* plugin full name */
	const char *pname;
	ldmsd_msg_log_f msglog;
} plugin_globals;
#define PG plugin_globals

#define ROLL_COMMON \
	char *filename; \
	char *headerfilename

/* Instance data we need for notification. */
#define CSV_STORE_HANDLE_COMMON \
	char *container; \
	char *schema; \
	/* handle roll_common strings with replace_string in store handle */ \
	ROLL_COMMON; \
	NOTIFY_COMMON

struct roll_common {
	ROLL_COMMON;
};

/* casting base for common bits in csv stores. */
struct csv_store_handle_common {
	CSV_STORE_HANDLE_COMMON;
};

/* convert private store handle pointer into csv_store_handle_common pointer, if
 * store handle was declared containing a CSV_STORE_HANDLE_COMMON block.
 */
#define CSHC(x) \
	((struct csv_store_handle_common *)&(x->container))
/* const version of CSHC */
#define CCSHC(x) \
	((const struct csv_store_handle_common *)&(x->container))

/**
 * Parse a named parameter from the avl structure.
 * Boolean values are strings starting with t/f/1/0/y/n and uppercase of same.
 * If parameter_name is found in avl with the empty string as value, true is
 * taken.
 * \param cps source of logging and plugin name.
 * \param avl source of param/value pairs
 * \param param_name name of parameter to find
 * \param param_value address of bool to assign if parameter found in avl.
 * \return 0 if ok or errno value otherwise.
 */
int parse_bool(struct csv_plugin_static *cps, struct attr_value_list *avl, const char *param_name, bool *param_value);

/** As parse_bool, but log pointer and source instead of plugin pointer */
int parse_bool2(ldmsd_msg_log_f log, struct attr_value_list *avl, const char *param_name, bool *param_value, const char *source);

/* Replace the *strp with allocated duplicate of val.
 * String pointers managed with this function should not be set
 * by any other method.
 * \param strp If strp is NULL, returns EINVAL.
 * \param val If val is NULL, replaces *strp with NULL, deallocating *strp.
 * \return If allocation fails, a fixed value *strp becomes a known
 * value indicating an error ("/malloc/failed") and ENOMEM is returned,
 * else 0 is returned.
 */
int replace_string(char **strp, const char *val);

/* notification message strings  (events and types) */
#define NOTE_OPEN "OPENED"
#define NOTE_CLOSE "CLOSED"
#define NOTE_DAT "data"
#define NOTE_HDR "header"
#define NOTE_SUMM "summary"
#define NOTE_KIND "kind"
#define NOTE_CNAM "cname"
#define NOTE_PNAM "pyname"
#define NOTE_UNIT "units"
/**
 * Send hooks messages about a file close. Used in rollover and store stop.
 * The argument list would be shorter if csv_store_handle_common was a full
 * base type including container, schema, msglog instead of just the
 * common bits for notification.
 */
extern
void notify_output(const char *event, const char *name, const char *type, struct csv_store_handle_common *s_handle, struct csv_plugin_static *cps, const char * container, const char *schema);

/**
 * Rename a closed file, following the rename template,
 * and applying permissions, uid, gid.
 * The rename_template of s_handle is a string containing a path including
 * optionally the following substitutions:
 *	%P expands to plugin name,
 *	%C expands to container,
 *	%S expands to schema,
 *	%T expands to type.
 *	%B expands to basename(name),
 *	%D expands to dirname(name),
 *	%s timestamp suffix, if it exists.
 * Specifying both output event notification and output 
 * renaming produces a race condition between this function
 * and the event-processor and should be avoided.
 * 
 * The expanded name must be on the same file system (mount point)
 * as the original file, or the underlying C rename() call will fail.
 * This is not an interface that will implicitly copy and remove files.
 * \param name a file just closed.
 * \param type the type of file, e.g. NOTE_DAT.
 * \param s_handle the store instance
 * \param cps the store plugin instance
 *
 * Rename failures will be logged; there is no way to detect them here.
 */
void rename_output(const char *name, const char *type,
	struct csv_store_handle_common *s_handle,
	struct csv_plugin_static *cps);

/**
 * Chmod/chown a new output file per the create_ parameters.
 * Failures will be logged; there is no way to detect them here.
 */
void ch_output(FILE *f, const char *name,
	struct csv_store_handle_common *s_handle,
	struct csv_plugin_static *cps);

/**
 * configurations custom for a container+schema that can override
 * the vals in config_init.
 * Locking and cfgstate are caller's job.
 */
int config_custom_common(struct attr_value_list *kwl, struct attr_value_list *avl, struct storek_common *sk, struct csv_plugin_static *cps);

#define CONFIG_INIT_COMMON(k, a, arg) config_init_common(k, a, arg, &plugin_globals )
/**
 * configurations for the whole store. these will be defaults if not overridden.
 * some implementation details are for backwards compatibility
 */
int config_init_common(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg, struct csv_plugin_static *cps);

/** \brief clean up storek fields configured by config_custom_common. */
void clear_storek_common(struct storek_common *skc);

/** \brief init common fields from custom or default. */
void csv_update_handle_common(struct csv_store_handle_common *s_handle, struct storek_common *skc, struct csv_plugin_static *cps);

#define CLOSE_STORE_COMMON(h) close_store_common(CSHC(h), &PG)
/** \brief clean up handle fields configured by CONFIG_INIT_COMMON */
extern void close_store_common(struct csv_store_handle_common *s_handle, struct csv_plugin_static *cps);

/** \brief Dump the common csv handle to log */
void print_csv_store_handle_common(struct csv_store_handle_common *s_handle, struct csv_plugin_static *cps);


#define NOTIFY_USAGE \
		"         - notify  The path for the file event notices.\n" \
		"         - notify_isfifo  0 if not (the default) or 1 if fifo.\n" \
		"         - rename_template  The template string for closed output renaming.\n" \
		"         - rename_uid  The numeric user id for output renaming.\n" \
		"         - rename_gid  The numeric group id for output renaming.\n" \
		"         - rename_perm  The octal permission bits for output renaming.\n" \
		"         - create_uid  The numeric user id for output creation.\n" \
		"         - create_gid  The numeric group id for output creation.\n" \
		"         - create_perm  The octal permission bits for output creation.\n" \


#define LIB_CTOR_COMMON(cps) \
	cps.notify = NULL; \
	cps.notify_isfifo = false; \
	cps.rename_template = false; \
	cps.rename_uid = (uid_t)-1; \
	cps.rename_gid = (gid_t)-1; \
	cps.rename_perm = 0; \
	cps.create_uid = (uid_t)-1; \
	cps.create_gid = (gid_t)-1; \
	cps.create_perm = 0; \
	cps.hooks_closed = 0


#define LIB_DTOR_COMMON(cps) \
	ovis_notification_close(cps.onp); \
	free(cps.notify); \
	free(cps.rename_template)

#endif /* store_csv_common_h_seen */
