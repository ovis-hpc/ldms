/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-17 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013-17 Sandia Corporation. All rights reserved.
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

#ifndef OVIS_UTIL_H_
#define OVIS_UTIL_H_

#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>
#include <sys/queue.h>

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
	int size;
	int count;
	LIST_HEAD(string_list, string_ref_s) strings;
	struct attr_value list[0];
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
 * \brief Tokenize the string \c cmd into the keyword list \c kwl
 * and the attribute list \c avl
 * \return nonzero if lists give are too small to hold all words or
 * pairs found in cmd, else 0.
 */
int tokenize(char *cmd, struct attr_value_list *kwl,
	     struct attr_value_list *avl);

/**
 * \brief Allocate memory for a new attribute list of size \c size
 */
struct attr_value_list *av_new(size_t size);

/**
 * \brief Free the memory consumed by the avl
 */
void av_free(struct attr_value_list *avl);

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
char *ovis_join(char *joiner, ...);

/**
 * \brief Fill array by joining parts.
 * \param buf character array
 * \param buflen size of buf, or less.
 * \param joiner string used at joins; if NULL given, "/" is used.
 *        Use "" for plain concatenation.
 * \param ... A null terminated list of const char *.
 * Example: ovis_join_buf(buf, 256, "\\","c:\\root", "file.txt", NULL);
 * \return 0 if successful, or errno if input problem detected.
 */
int ovis_join_buf(char *buf, size_t buflen, char *joiner, ...);

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
#endif /* OVIS_UTIL_H_ */
