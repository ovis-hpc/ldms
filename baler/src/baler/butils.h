/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013-2016 Sandia Corporation. All rights reserved.
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
#include <math.h>

#include "btypes.h"

/* *** LOGGING *** */
extern FILE *blog_file;
extern int __blog_level;

extern void __blog(const char *fmt, ...);

enum blog_level {
	BLOG_LV_DEBUG = 0,
	BLOG_LV_INFO,
	BLOG_LV_WARN,
	BLOG_LV_ERR,
	BLOG_LV_QUIET,
	BLOG_LV_LAST,
};

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
#define berror(str) berr("%s:%d, %s(), %s: errno: %d, msg: %m\n", \
				__FILE__, __LINE__, __func__, \
				str, errno)

#define berrorrc(str, _rc) berr("%s:%d, %s(), %s: errno(%d): %s\n", \
				__FILE__, __LINE__, __func__, \
				str, (_rc), brcstr(_rc))

/**
 * \brief Set log level.
 * \param level One of the enumeration ::blog_level.
 */
void blog_set_level(int level);

/**
 * \brief Same as blog_set_level(), but with string option instead.
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
int blog_set_level_str(const char *level);

/**
 * Get blog level.
 *
 * \retval LVL The current log level.
 */
int blog_get_level();

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
 * \retval 1 if the given \a path exists and is directory.
 * \retval 0 if the given path does not exist or is not a directory. If the path
 *           exists but is not a directory, \c errno is set to \c ENOTDIR.
 */
int bis_dir(const char *path);

/**
 * This behave like mkdir -p, except that it will report errors even in the case
 * of directory/file exists.
 *
 * \retval 0 if success.
 * \retval errno if failed.
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
 * Same as ::bdstr_append(), but with ::bstr.
 */
int bdstr_append_bstr(struct bdstr *bdstr, const struct bstr *bstr);

/**
 * Append content pointed by \c mem, for \c len bytes, to \c bdstr.
 *
 * \retval 0 if no error.
 * \retval errno if error.
 */
int bdstr_append_mem(struct bdstr *bdstr, void *mem, size_t len);

/**
 * Same as ::bdstr_append(), but with printf() format.
 */
int bdstr_append_printf(struct bdstr *bdstr, const char *fmt, ...);

/**
 * Append \c c to \c bdstr.
 */
int bdstr_append_char(struct bdstr *bdstr, const char c);

/**
 * Detach \c char* buffer to the caller.
 * \note The caller owns the \c char* buffer after this function returns.
 * \note The input \c bdstr contains no data, but still usable.
 */
char *bdstr_detach_buffer(struct bdstr *bdstr);

/**
 * Reset \c bdstr to empty string.
 *
 * \retval 0 if success.
 * \retval errno if error.
 */
int bdstr_reset(struct bdstr *bdstr);

/**
 * Free \c bdstr and its buffer.
 */
void bdstr_free(struct bdstr *bdstr);


/**
 * ::bdbstr is similar to ::bdbstr, but uses ::bstr instead of \c char*.
 */
struct bdbstr {
	size_t alloc_len;
	struct bstr *bstr;
};

/* bdbstr */

/**
 * Allocate ::bdbstr, with initial allocation length \c len.
 * \returns the pointer to the ::bdbstr if success.
 * \returns NULL if failed.
 */
struct bdbstr* bdbstr_new(size_t len);

/**
 * Expand the \c str inside the ::bdbstr \c bs to \c new_size.
 * \returns 0 on success, error code on error.
 */
int bdbstr_expand(struct bdbstr *bs, size_t new_size);

/**
 * Append \c str into \c bs->str, expand \c bs->str if necessary.
 * \returns 0 on success, error code on failure.
 */
int bdbstr_append(struct bdbstr *bs, const char *str);

/**
 * Same as ::bdbstr_append(), but with ::bstr.
 */
int bdbstr_append_bstr(struct bdbstr *bdbstr, const struct bstr *bstr);

/**
 * Append content pointed by \c mem, for \c len bytes, to \c bdbstr.
 *
 * \retval 0 if no error.
 * \retval errno if error.
 */
int bdbstr_append_mem(struct bdbstr *bdbstr, void *mem, size_t len);

/**
 * Same as ::bdbstr_append(), but with printf() format.
 */
int bdbstr_append_printf(struct bdbstr *bdbstr, const char *fmt, ...);

/**
 * Append \c c to \c bdbstr.
 */
int bdbstr_append_char(struct bdbstr *bdbstr, const char c);

/**
 * Detach \c char* buffer to the caller.
 * \note The caller owns the \c char* buffer after this function returns.
 * \note The input \c bdbstr contains no data, but still usable.
 */
struct bstr *bdbstr_detach_buffer(struct bdbstr *bdbstr);

/**
 * Reset \c bdbstr to empty string.
 *
 * \retval 0 if success.
 * \retval errno if error.
 */
int bdbstr_reset(struct bdbstr *bdbstr);

/**
 * Free \c bdbstr and its buffer.
 */
void bdbstr_free(struct bdbstr *bdbstr);

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
 * Longest common subsequence extraction.
 *
 * \param a The first string.
 * \param b The second string.
 * \param[out] idx The index of the first string elements (\c a) being a part of
 *                 LCS to \c a and \c b
 * \param[in,out] idx_len As an input, it tells the allocated length of \c idx;
 *                        as an output, it tells the the length of the LCS
 *                        result.
 * \param buff Buffer to hold LCS calculation and back tracking.
 * \param buffsz The size of \c buffer.
 *
 * \retval 0 OK
 * \retval errno if error.
 */
int bstr_lcsX_u32(const struct bstr *a, const struct bstr *b, int *idx,
					int *idx_len, void *buff, size_t buffsz);

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

/**** Line-by-line file processing ****/

/**
 * This is a call-back interface for bprocess_file_by_line() function.  The
 * call-back function can return 0 for no error. bprocess_file_by_line() will
 * continue processing the file, line-by-line, until EOF.  Other return values
 * will be treated as an error, and the process_file_by_line() function will
 * stop processing file and return with the received return code.
 *
 * The \c line is owned by bprocess_file_by_line(). The callback function may
 * change it, but the change will be discarded after the callback function
 * returned.
 *
 * \param line The read line from the file.
 * \param ctxt The context provided at the process_file_by_line() function call.
 *
 * \retval 0 will be treat as no error.
 * \retval other will be treat as error, and the process_file_by_line() will
 *               stop processing.
 */
typedef int (*bprocess_file_by_line_cb_t)(char *line, void *ctxt);

/**
 * Process the given \c file, line-by-line with \c cb function.
 *
 * \warning The supported line length is 4095 (including '\n').
 *
 * \param path The path to the file.
 * \param ctxt The context to pass on to \c cb function.
 * \param cb The callback function, which will be called to process each line of
 *           the input.
 */
int bprocess_file_by_line(const char *path, bprocess_file_by_line_cb_t cb,
								void *ctxt);

/**
 * The same as bprocess_file_by_line(), but with a pre-process of removing '#'
 * comment style lines.
 */
int bprocess_file_by_line_w_comment(const char *path,
				bprocess_file_by_line_cb_t cb, void *ctxt);

/**
 * Get a cell of CSV from \c str.
 * \c end will pointed at the end of the cell + 1.
 *
 * \example
 * \code{.c}
 * int rc;
 * char *end;
 * rc = bcsv_get_cell("abc,def", &end);
 * \endcode
 *
 * From the example, \c end will point at ','.
 *
 * \param[in] str
 * \param[out] end
 *
 * \retval 0 if OK
 * \retval ENOENT if the input is incomplete, or no more cell.
 */
int bcsv_get_cell(const char *str, const char **end);

/**
 * Get a line from the file \c f; the \c bdstr is dynamically expanded to
 * contain the whole line.
 *
 * \retval 0 if success.
 * \retval ERRNO if there is no more line to read.
 * \retval errno for other errors.
 */
int bgetline(FILE *f, struct bdstr *bdstr);

/**
 * A convenient function to get a value from an environmental variable.
 * \param name the name of the environmental variable.
 * \param _default the default value.
 * \retval u64 the value from getenv(), or _default if the environment variable
 *             is not found.
 */
static inline
uint64_t bgetenv_u64(const char *name, uint64_t _default)
{
	const char *var = getenv(name);
	if (!var)
		return _default;
	return strtoull(var, NULL, 0);
}

/*** BIN utility ***/

/**
 * Metric bin structure.
 */
struct bmetricbin {
	char metric_name[256];
	int alloc_bin_len;
	int bin_len;
	struct {
		double lower_bound;
		void *ctxt;
	} bin[0];
	/*
	 * NOTE: for bin[x].img is for [bin[x].lower_bound,
	 * bin[x+1].upper_bound). For x == 0, bin[0].lower_bound is -inf.  The
	 * last bin, bin[bin_len - 1], will have lower_bound == inf.
	 */
};

struct bmetricbin *bmetricbin_create(const char *recipe);
struct bmetricbin *bmetricbin_new(int alloc_bin_len);
void bmetricbin_free(struct bmetricbin *bin);
int bmetricbin_addbin(struct bmetricbin *bin, double value);
struct bmetricbin *bmetricbin_expand(struct bmetricbin *bin, int inc_bin_len);
int bmetricbin_getbinidx(struct bmetricbin *bin, double value);

const char *brcstr(int rc);
const char *berrnostr(int _errno);

#endif // _BUTILS_H
/**\}*/
