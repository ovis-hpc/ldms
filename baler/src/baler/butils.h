/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013 Sandia Corporation. All rights reserved.
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
 * \file butils.h
 * \author Narate Taerat (narate@ogc.us)
 * \date Mar 20, 2013
 * \ingroup butils
 *
 * \defgroup butils Baler Utility Functions.
 * \{
 *
 * \brief Baler utility functions.
 */

#ifndef _BUTILS_H
#define _BUTILS_H

#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>

#include "btypes.h"

/* *** LOGGING *** */
extern FILE *blog_file;
extern int __blog_level;

extern void __blog(const char *fmt, ...);

#define BLOG_LV_DEBUG	0
#define BLOG_LV_INFO	1
#define BLOG_LV_WARN	2
#define BLOG_LV_ERR	3

#define blog(lv, fmt, ...) do {\
	if (BLOG_LV_ ## lv >= __blog_level) \
		__blog(#lv ": " fmt "\n", ##__VA_ARGS__);\
} while(0)

#ifndef BLOG_LEVEL
# define BLOG_LEVEL BLOG_LV_DEBUG
#endif

/**
 * \brief Print *DEBUG* message to a default log.
 */
#define bdebug(fmt, ...) blog(DEBUG, fmt, ##__VA_ARGS__)

/**
 * \brief Print *INFO* message to a default log.
 */
#define binfo(fmt, ...) blog(INFO, fmt, ##__VA_ARGS__)

/**
 * \brief Print *WARN* message to a default log.
 */
#define bwarn(fmt, ...) blog(WARN, fmt, ##__VA_ARGS__)

/**
 * \brief Print *ERR* message to a default log.
 */
#define berr(fmt, ...) blog(ERR, fmt, ##__VA_ARGS__)

/**
 * \brief Similar to perror(str), but print stuffs to Baler log instead.
 */
#define berror(str) berr("%s:%d, %s, %s: errno: %d, msg: %s\n", \
				__FILE__, __LINE__, __func__, \
				str, errno, sys_errlist[errno])

/**
 * \brief Set log level.
 * \param level One of the
 */
void blog_set_level(int level);

/**
 * \brief Set \a f as a log file.
 * \param f The log file. \a f should be opened.
 */
void blog_set_file(FILE *f);

/**
 * \brief Open \a path for logging.
 * \param path The path to the file to be used for logging.
 * \return 0 on success.
 * \return -1 on failure. (errno should be set accordingly)
 */
int blog_open_file(const char *path);

/**
 * \brief Close the log file.
 * This is essentially a wrapper to calling fclose(blog_file).
 * Hence, errno will be set accordingly on failure.
 * \return 0 on success.
 * \return <b>EOF</b> on failure.
 */
int blog_close_file();

/**
 * \brief Flush the log file.
 * This is essentially a wrapper to calling fflush(blog_file).
 * Hence, errno will be set accordingly on failure.
 * \return 0 on success.
 * \return <b>EOF</b> on failure.
 */
int blog_flush();

/**
 * \brief A convenient function checking if the given \a path exists.
 * \return 1 if the \a path exists. \a path is not necessary a file though.
 * \return 0 if the \a path does not exist.
 */
int bfile_exists(const char *path);

/**
 * \brief Check if the given path is a directory.
 * \return 1 if the given \a path exists and is directory.
 * \return 0 if the given path does not exist or is not a directory.
 */
int bis_dir(const char *path);

/**
 * This behave like mkdir -p, except that it will report errors even in the case
 * of directory/file exists.
 */
int bmkdir_p(const char *path, __mode_t mode);

/* **** Dynamic String **** */
struct bdstr {
	size_t alloc_len;
	size_t str_len;
	char *str;
};

/**
 * Allocate ::bdstr, with initial allocation length \c len.
 * \returns the pointer to the ::bdstr if success.
 * \returns NULL if failed.
 */
struct bdstr* bdstr_new(size_t len);

/**
 * Expand the \c str inside the ::bdstr \c bs to \c new_size.
 * \returns 0 on success, error code on error.
 */
int bdstr_expand(struct bdstr *bs, size_t new_size);

/**
 * Append \c str into \c bs->str, expand \c bs->str if necessary.
 * \returns 0 on success, error code on failure.
 */
int bdstr_append(struct bdstr *bs, const char *str);

/**
 * Baler log rotate utility.
 *
 * \returns 0 if success.
 * \returns errno if failed.
 */
int blog_rotate(const char *path);

#define BMIN(a, b) (((a)<(b))?(a):(b))
#define BMAX(a, b) (((a)>(b))?(a):(b))

/**
 * Levenshtein distance of u32 bstr.
 *
 * \retval -1 if error.
 * \retval dist if success.
 */
int bstr_lev_dist_u32(const struct bstr *a, const struct bstr *b, void *buff,
								size_t buffsz);

/**
 * Longest common subsequence length calculation.
 */
int bstr_lcs_u32(const struct bstr *a, const struct bstr *b, void *buff,
								size_t buffsz);

/**
 * LCS distance between two given bstr (u32 variant)
 */
int bstr_lcs_dist_u32(const struct bstr *a, const struct bstr *b, void *buff,
								size_t buffsz);

/**
 * Utility to parse URL query into key,value pairs.
 *
 * This function also parse %XX encoding and '+' to ' '.
 *
 * \retval 0 if success.
 * \retval errno if error.
 */
int bparse_http_query(const char *query, struct bpair_str_head *head);

#endif // _BUTILS_H
/**\}*/
