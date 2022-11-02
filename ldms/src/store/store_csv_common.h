/**
 * Copyright (c) 2016-2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2016-2019 Open Grid Computing, Inc. All rights reserved.
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
#include "ovis_log/ovis_log.h"
#include "ldmsd.h"
#include "ldmsd_plugattr.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))
#endif

/** Common override parameters for "config action=custom" settings. */
#define STOREK_COMMON \
	/** The full path template for renaming closed outputs. NULL indicates no renames wanted. */ \
        char *rename_template; \
	uid_t rename_uid; \
	gid_t rename_gid; \
	unsigned rename_perm; \
	uid_t create_uid; \
	gid_t create_gid; \
	unsigned create_perm; \
	time_t otime; \
	bool ietfcsv; \
	int altheader; \
	int typeheader; \
	int time_format; \
	int buffer_type; \
	int buffer_sz; \
	char *store_key; /* this is the container/schema */

struct storek_common {
	STOREK_COMMON;
};

/* Convert private storek handle pointer into storek_common pointer, if
 * storek handle was declared containing a STOREK_COMMON block.
 */
#define CSKC(x) \
	((struct storek_common *)&((x)->rename_template))

/* containment for globals. */
struct csv_plugin_static {
	const char *pname; /**< plugin full name */
	ovis_log_t mylog;
} plugin_globals;
#define PG plugin_globals

#define ROLL_COMMON \
	char *filename; \
	char *headerfilename; \
	char *typefilename

/* Instance data we need commonly */
#define CSV_STORE_HANDLE_COMMON \
	char *container; \
	char *schema; \
	/* \
	 * 1 means each array element is stored it its own column. \
	 * 0 means all array elements are stored in a column as a commn-separated string. \
	 * The default is 0. \
	 */ \
	int expand_array; \
	/* Separating character between array elements when expand_array is false */ \
	char array_sep; \
	/* Left quote of an array sample */ \
	char array_lquote; \
	/* Right quote of an array sample */ \
	char array_rquote; \
	/* handle roll_common strings with replace_string in store handle */ \
	ROLL_COMMON; \
	STOREK_COMMON

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
#define parse_bool(a,b,c,d) parse_bool2(b, c, d)

/** As parse_bool, but log pointer and source instead of plugin pointer */
int parse_bool2(struct attr_value_list *avl, const char *param_name, bool *param_value, const char *source) __attribute__ ((deprecated("Use ldmsd_plugattr_bool instead")));

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

/* file type names */
#define FTYPE_DATA "DATA"
#define FTYPE_HDR "HEADER"
#define FTYPE_KIND "KIND"
#define FTYPE_UNIT "UNITS"
#define FTYPE_CNAM "CNAMES"
#define FTYPE_PNAM "PYNAMES"
#define FTYPE_SUMM "SUMMARY"

/**
 * Make a directory and apply permissions to any new intermediate directories
 * needed in the process. (perm on existing directories are not modified).
 * \param path
 * \param s_handle the store instance
 * \param cps the store plugin instance
 * \return 0 or errno value, in which case see log messages.
 */
int create_outdir(const char *path,
	struct csv_store_handle_common *s_handle,
	struct csv_plugin_static *cps);

/**
 * Rename a closed file, following the rename template,
 * and applying permissions, uid, gid.
 * The rename_template of s_handle is a string containing a path including
 * optionally the following substitutions:
 *	%P expands to plugin name,
 *	%C expands to container,
 *	%S expands to schema,
 *	%T expands to ftype.
 *	%B expands to basename(name),
 *	%D expands to dirname(name),
 *	%s timestamp suffix, if it exists.
 *	%{var} expands to env(var)
 *
 * The expanded name must be on the same file system (mount point)
 * as the original file, or the underlying C rename() call will fail.
 * This is not an interface that will implicitly copy and remove files.
 * \param name a file just closed.
 * \param filetype the type of file, e.g. FTYPE_DATA
 * \param s_handle the store instance
 * \param cps the store plugin instance
 *
 * Rename failures will be logged; there is no way to detect them here.
 */
void rename_output(const char *name, const char *filetype,
	struct csv_store_handle_common *s_handle,
	struct csv_plugin_static *cps);

/**
 * Chmod/chown a new output file per the create_ parameters.
 * Failures will be logged; there is no way to detect them here.
 */
void ch_output(FILE *f, const char *name,
	struct csv_store_handle_common *s_handle,
	struct csv_plugin_static *cps);

/** Format a metric names line to a file given following the
 * metric_array indices and conventions of csv_format_common.
 * \param file destination ready to use.
 * \param fpath name of f for error messages.
 * \param sh used for ietf (or not) formatting.
 * \param doudata used for udata (or not) formatting.
 * \param cps used for error message logging.
 * \param set data to manage output
 * \param metric_array list of indices to use from set.
 * \param metric_count length of metric_array.
 * \return 0 or errno value.
 */
int csv_format_header_common(FILE *file, const char *fpath, const struct csv_store_handle_common *sh, int doudata, struct csv_plugin_static *cps, ldms_set_t set, int *metric_array, size_t metric_count, int time_format);

int csv_row_format_header(FILE *file, const char *fpath,
		const struct csv_store_handle_common *sh, int doudata,
		struct csv_plugin_static *cps, ldms_set_t set,
		struct ldmsd_row_s *row,
		int time_format);

/** Format a metric types line to a file given following the
 * metric_array indices and conventions of csv_format_common.
 * String quoting is never used.
 * \param typeformat: 0 (do nothing),
 * 	1 expand arrays (ldmstype),
 * 	2 'ldmstype[]len' for arrays
 * 	All other arguments ignored if typeformat is 0.
 * 	CHAR_ARRAYs are never expanded, as with csv_format_common.
 * \param f destination ready to use.
 * \param fpath name of f for error messages.
 * \param sh used for udata (or not) formatting.
 * \param cps used for error message logging.
 * \param set data to manage output
 * \param metric_array list of indices to use from set.
 * \param metric_count length of metric_array.
 * \param time_format 0 for traditional time format, 1 for alternate milliseconds-since-epoch time format
 * \return 0 or errno value.
 */
extern int csv_format_types_common(int typeformat, FILE* f, const char *fpath, const struct csv_store_handle_common *sh, int doudata, struct csv_plugin_static *cps, ldms_set_t set, int *metric_array, size_t metric_count);

int csv_row_format_types_common(int typeformat, FILE* file, const char *fpath,
		const struct csv_store_handle_common *sh, int doudata,
		struct csv_plugin_static *cps, ldms_set_t set,
		struct ldmsd_row_s *row);

#define OPEN_STORE_COMMON(pa, h) open_store_common(pa, CSHC(h), &PG)
/**
 * configurations for the store plugin.
 */
int open_store_common(struct plugattr *pa, struct csv_store_handle_common *s_handle, struct csv_plugin_static *cps);

#define CLOSE_STORE_COMMON(h) close_store_common(CSHC(h), &PG)
/** \brief clean up handle fields configured by open_store_common */
extern void close_store_common(struct csv_store_handle_common *s_handle, struct csv_plugin_static *cps);

/** \brief Dump the common csv handle to log */
void print_csv_store_handle_common(struct csv_store_handle_common *s_handle, struct csv_plugin_static *cps);

/** include the common config items in the anames array for store_config_check, if wanted. */
#define CSV_STORE_ATTR_COMMON \
	"opt_file", \
	"ietfcsv", \
	"altheader", \
	"typeheader", \
	"time_format", \
	"buffer", \
	"buffertype", \
	"rename_template", \
	"rename_uid", \
	"rename_gid", \
	"rename_perm", \
	"create_uid", \
	"create_gid", \
	"create_perm", \
	"expand_array", \
	"array_sep", \
	"array_lquote", \
	"array_rquote"

/** Loop unrolled array formatting of typeheader */
#define TH_UNROLL 1
/** Array metrics formatting of typeheader */
#define TH_ARRAY 2
/** Maximum valid typeheader value */
#define TH_MAX TH_ARRAY

/* Time field format options */
#define TF_CLASSIC 0
#define TF_MILLISEC 1
#define TF_MAX 1

#define FILE_PROPS_USAGE \
		"         - rename_template  The template string for closed output renaming.\n" \
		"         - rename_uid  The numeric user id for output renaming.\n" \
		"         - rename_gid  The numeric group id for output renaming.\n" \
		"         - rename_perm  The octal permission bits for output renaming.\n" \
		"         - create_uid  The numeric user id for output creation.\n" \
		"         - create_gid  The numeric group id for output creation.\n" \
		"         - create_perm  The octal permission bits for output creation.\n"

#define LIB_CTOR_COMMON(cps)

#define LIB_DTOR_COMMON(cps)

#endif /* store_csv_common_h_seen */
