/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-2020 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2013-2020 Open Grid Computing, Inc. All rights reserved.
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

#ifndef OVIS_UTIL_H_
#define OVIS_UTIL_H_

#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>
#include <sys/queue.h>
#include "ovis-ldms-config.h"

/*
 * This file aggregates simple utilities for
 * key/value list parsing,
 * number scaling by suffix,
 * spawning a trackable child,
 * allocating/nonallocating join of strings,
 * file operations exists, isdir, mkdir -p,
 */

struct attr_value {
	char *name;
	char *value;
};

/**
 * Fixed upper bound in size key/value lists can be created and used
 * as demonstrated in the following example.
 * #include "ovis_util/util.h"
 * int max_pairs = 10;
 * int max_words = 10;_
 * struct attr_value_list *kvl = av_new(max_words);
 * struct attr_value_list *avl = av_new(max_pairs);
 * char *data = "a=b\nc=d e=f\n#g=h\n	i=j k\n";
 * tokenize(data,kvl,avl);
 * // producing list kvl containing "k" and
 * // avl containing pairs [a,b] [c,d] [e,f] [#g,h] [i,j]
 * char *value = av_value(avl,"w");
 * if (value != NULL)
 * 	printf("unexpected element in avl\n");
 *
 * Handling an unbounded list size becomes a bounded problem by
 * estimating the maximum possible tokens as follows for string s.
 * int size = 1;
 * char *t = s;
 * while (t[0] != '\0') {
 * 	if (isspace(t[0])) size++;
 * 	t++;
 * }
 * struct attr_value_list *avl = av_new(size);
 *
 */
typedef struct string_ref_s {
	char *str;
	LIST_ENTRY(string_ref_s) entry;
} *string_ref_t;

struct attr_value_list {
	int size; /* capacity of list */
	int count; /* number of keys in the list */
	LIST_HEAD(string_list, string_ref_s) strings;
	struct attr_value list[OVIS_FLEX];
};

/**
 * \brief Get the value of attribute \c name
 */
char *av_value(struct attr_value_list *av_list, const char *name);

/**
 * \brief Get the attribute name in the \c av_list
 * at the index \c idx
 */
char *av_name(struct attr_value_list *av_list, int idx);

/**
 * \brief Get the value at the index \c idx
 */
char *av_value_at_idx(struct attr_value_list *av_list, int idx);

/**
 * \brief Get index of name, if present, or -1 if not, or -k
 * if the name is repeated k times.
 */
int av_idx_of(const struct attr_value_list *av_list, const char *name);

/**
 * \brief Tokenize the string \c cmd into the keyword list \c kwl
 * and the attribute list \c avl
 * \return nonzero if lists give are too small to hold all words or
 * pairs found in cmd, else 0.
 */
int tokenize(char *cmd, struct attr_value_list *kwl,
	     struct attr_value_list *avl);

#define AV_EXPAND 1 /*< do environment expansion on values. */
#define AV_NL 2 /* separate successive items by \n instead of space */
/**
 * \brief format the list to a string, with optional env expansion.
 * \param av_flags: bitwise or of the AV flags used in formatting the result.
 * \param av_list list to print.
 * \return string the caller must free, or null from bad input.
 */
char *av_to_string(struct attr_value_list *av_list, int av_flags);

/**
 * \brief Allocate memory for a new attribute list of size \c size
 */
struct attr_value_list *av_new(size_t size);

/**
 * \brief Add a new attribute-value pair.
 *
 * \return 0 on success. ENOSPC is returned of the list is full.
 *         ENOMEM is returned out-of-memory occurs.
*/
int av_add(struct attr_value_list *avl, const char *name, const char *value);

/**
 * \brief Duplicate an existing list.
 */
struct attr_value_list *av_copy(struct attr_value_list *src);

/**
 * \brief Free the memory consumed by the avl. Ignores NULL input.
 */
void av_free(struct attr_value_list *avl);

typedef void (*printf_t)(const char *fmt, ...);

/**
 * \brief Check value for $ and if found write message with log
 * about name=value pair.
 * \return 0 if no $ found (all expanded).
 */
int av_check_expansion(printf_t log, const char *name, const char *value);

/**
 * \brief Parse the memory size
 * \return size of memory in bytes
 */
size_t ovis_get_mem_size(const char *s);

/**
 * \brief Fork and exec the given command with /bin/sh.
 *
 * This function call will fork and execute the given command with bash. It is
 * non-blocking, i.e. the function returns right away after fork (not after the
 * child process finished execution). Caller can handle child process
 * termination by handling SIGCHLD (see signal(7)), or using a variation of
 * wait(2).
 *
 * This utility function also close all inherited file descriptors (including
 * STDIN, STDOUT and STDERR).
 *
 * \retval pid The PID of the forked child.
 */
pid_t ovis_execute(const char *command);

/**
 * \brief Allocate a string from joining parts.
 * \param joiner string used at joins; if NULL given, "/" is used.
 * \param ... A null terminated list of const char *.
 * Example: ovis_join("\\","c:\\root", "file.txt", NULL);
 * \return The resulting string caller must free, or NULL if fail(see errno).
 */
__attribute__ ((sentinel)) char *ovis_join(char *joiner, ...);

/**
 * \brief Fill array by joining parts.
 * \param buf character array
 * \param buflen size of buf, or less.
 * \param joiner string used at joins; if NULL given, "/" is used.
 *        Use "" for plain concatenation.
 * \param ... A null terminated list of const char *.
 * Example: ovis_join_buf(buf, 256, "\\","c:\\root", "file.txt", NULL);
 * \return 0 if successful, or errno if input problem detected.
 * Any preexisting buf content is overwritten.
 */
__attribute__ ((sentinel)) int ovis_join_buf(char *buf, size_t buflen, char *joiner, ...);

/**
 * \brief A convenient function checking if the given \a path exists.
 * \return 1 if the \a path exists. \a path is not necessary a file though.
 * \return 0 if the \a path does not exist.
 */
int f_file_exists(const char *path);

/**
 * \brief Check if the given path is a directory.
 * \retval 1 if the given \a path exists and is directory.
 * \retval 0 if the given path does not exist or is not a directory. If the path
 *           exists but is not a directory, \c errno is set to \c ENOTDIR.
 */
int f_is_dir(const char *path);

/**
 * This behave like mkdir -p, except that it will report errors even in the case
 * of directory/file exists.
 *
 * \retval 0 if success.
 * \retval errno if failed.
 */
int f_mkdir_p(const char *path, __mode_t mode);

/**
 * \brief Replace environment variables in a string
 *
 * This function handles the ${<name>} syntax for replacing these
 * strings with the corresponding environment variable value.
 *
 * The syntax is similar to bash, for example if getenv("HOSTNAME") ==
 * "orion-08", then:
 *
 * "${HOSTNAME}/meminfo" becomes "orion-08/meminfo"
 *
 * The supported syntax for the <name> is [[:alnum:]_]+. That is, one
 * or more alpha-numeric or '_' characters. Environment variables that
 * are missing are replaced with "".
 *
 * The function returns memory that was allocated with malloc()
 *
 * \param str The input string.
 * \retval NULL There was insufficient memory to allocate the output string.
 * \retval Ptr to a string with the environment variable values replaced.
 */
char *str_repl_env_vars(const char *str);

/**
 * \brief Expand `$(COMMAND)` with the output of the COMMAND.
 *
 * \retval ptr The expanded string. The caller is responsible for freeing it.
 * \retval NULL If error. \c errno is also set accordingly.
 */
char *str_repl_cmd(const char *_str);

/**
 * \brief The \c fopen() wrapper with permission \c o_mode for new file.
 *
 * Open the file \c path for write (\c f_mode "w" or "w+") or append (\c f_mode
 * "a" or "a+"). Also create the new file if not existed with the file mode \c
 * o_mode.
 *
 * \param path The file path.
 * \param f_mode "w" for write (truncate), "w+" for read and write (truncate),
 *               "a" for append, or "a+" for read and append.
 * \param o_mode The octal file permission.
 *
 * \retval fptr The FILE stream handle.
 * \retval NULL If there is an error trying to open/create the file. \c errno is
 *              also set to describe the error.
 */
FILE *fopen_perm(const char *path, const char *f_mode, int o_mode);

/**
 * \brief Generic object access check function
 *
 * Check if an object of ogid:ouid with permission \c perm is accessible (\c
 * acc) by auid:agid.
 *
 * \param auid accessor's UID
 * \param agid accessor's GID
 * \param acc  access bits (Unix mode 0777 style)
 * \param ouid object owner's UID
 * \param ogid object owner's GID
 * \param perm object's permission
 *
 * \retval 0 if \c acc is allowed.
 * \retval EACCES if \c acc is not allowed.
 *
 */
int ovis_access_check(uid_t auid, gid_t agid, int acc,
		      uid_t ouid, gid_t ogid, int perm);

/**
 * \brief errno to string abbreviation.
 *
 * \retval str The abbrevation of the errno \c e (e.g. "ENOMEM")
 * \retval "UNKNOWN_ERRNO" if the errno \c e is unknown.
 */
const char* ovis_errno_abbvr(int e);

/**
 * \brief no longer our thread-safe strerror. DEPRECATED.
 *
 * This function is not supported in gnu libc versions
 * where sys_errlist has been removed. Use macro STRERROR below instead.
 * This is now just a wrapper on strerror.
 */
const char *ovis_strerror(int e) __attribute__((deprecated("use thread-safe macro STRERROR instead")));

/**
 * \brief convert error value to a string in a thread safe manner.
 *
 * Both flavors (xpg, gnu) of strerror_r are in use in ldms, due to _GNU_SOURCE.
 * n.b. _GNU_SOURCE, if used, must be defined before any #include to avoid
 * type conflicts from using the wrong macro expansion.
 */
#ifdef _GNU_SOURCE
#define STRERROR(_rc_) ({ char *msg=(char *)alloca(80); char * _e_ = strerror_r(_rc_, msg, 80); _e_; })
#else
#define STRERROR(_rc_) ({ char *msg=(char *)alloca(80); long long __attribute__((__unused__)) _e_ = (long long)strerror_r(_rc_, msg, 80); msg; })
#endif

typedef
struct ovis_pgrep_s {
	TAILQ_ENTRY(ovis_pgrep_s) entry;
	pid_t pid;
	char cmd[];
} *ovis_pgrep_t;

typedef
struct ovis_pgrep_array_s {
	int len;
	ovis_pgrep_t ent[];
} *ovis_pgrep_array_t;

/**
 * \brief A `pgrep`-like utility.
 *
 * This is a simplified utility function that acts similar to `pgrep`
 * command-line utility. This function go thorugh all \c /proc/PID/cmdline and
 * put the entries matching the search text (by \c strstr()) into the returned
 * array.
 *
 * The application must free the returned array with \c ovis_pgrep_free().
 *
 * The pgrep entries can be access as the following example:
 *
 * \code
 * arr = ovis_pgrep("some_prog");
 * if (!arr)
 *     goto err;
 * for (i = 0; i < arr->len; i++) {
 *     printf("pid: %d\n", arr->ent[i]->pid);
 *     printf("cmd: %d\n", arr->ent[i]->cmd);
 * }
 * ovis_pgrep_free(arr);
 * \endcode
 *
 * \param text The \c /proc/PID/cmdline text search term
 *
 * \retval array If there is no error.
 * \retval NULL  If there is an error.
 */
ovis_pgrep_array_t ovis_pgrep(const char *text);

/**
 * \brief Free the \c array created by \c ovis_pgrep().
 */
void ovis_pgrep_free(ovis_pgrep_array_t array);

#endif /* OVIS_UTIL_H_ */
