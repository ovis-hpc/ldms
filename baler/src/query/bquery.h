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
 * \file bquery.h
 * \brief Baler Query header.
 *
 * \defgroup bquery
 * \{
 * \brief Query information from Baler store.
 *
 * The following is the brief usage of message querying:
 * <pre>
 * 0. open baler store with bq_open_store()
 * 1. create query handle with bquery_create() with query constrains
 * 2. call bq_query() to get a message from the query.
 * 3. keep calling bq_query() until it returns NULL
 * </pre>
 * Please see bquery_create() and bq_query() for more information.
 */
#ifndef __BQUERY_H
#define __BQUERY_H

#undef _XOPEN_SOURCE
#define _XOPEN_SOURCE
#undef _GNU_SOURCE
#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <sos/sos.h>
#include "baler/bset.h"
#include "baler/btkn.h"
#include "baler/bptn.h"

#include "plugins/sos_img_class_def.h"
#include "plugins/sos_msg_class_def.h"

#define MASK_HSIZE 131071

LIST_HEAD(bsos_wrap_head, bsos_wrap);

typedef struct bsos_wrap {
	sos_t sos;
	char *store_name;
	char *path;
	LIST_ENTRY(bsos_wrap) link;
} *bsos_wrap_t;

/**
 * Open sos and put it into the wrapper.
 * \param path The path to sos.
 * \returns A pointer to ::bsos_wrap on success.
 * \returns NULL on failure.
 */
struct bsos_wrap* bsos_wrap_open(const char *path);

/**
 * Close sos and free related resources used by \c bsw.
 * \param bsw The wrapper handle.
 */
void bsos_wrap_close_free(struct bsos_wrap *bsw);

/**
 * Find the bsos_wrap by \c store_name.
 */
static
struct bsos_wrap* bsos_wrap_find(struct bsos_wrap_head *head,
				 const char *store_name)
{
	struct bsos_wrap *bsw;
	LIST_FOREACH(bsw, head, link) {
		if (strcmp(bsw->store_name, store_name) == 0)
			return bsw;
	}
	return NULL;
}

struct bq_store {
	char *path;
	struct bptn_store *ptn_store;
	struct btkn_store *tkn_store;
	struct btkn_store *cmp_store;
};

/**
 * Open Baler Query Store.
 */
struct bq_store* bq_open_store(const char *path);

/**
 * Query status enumeration.
 */
typedef enum bquery_status {
	BQ_STAT_INIT,
	BQ_STAT_QUERYING,
	BQ_STAT_DONE,
	BQ_STAT_LAST,
} bq_stat_t;

/**
 * Query structure for Baler query library.
 *
 * All of the query fields in this structure can set to be NULL or 0 to
 * disable the condition in that particular category. For example, if \c hst_ids
 * is NULL, then the query will obtain messages with on all hosts. If \c ts_0 is
 * set but \c ts_1 is 0, then the output would be all messages that occurred
 * after \c ts_0. Similarly, with \c ts_1 set and \c ts_0 unset, the query is
 * all messages before \c ts_1.
 *
 * \c store is a pointer to the store handle for this query.
 * \c itr Is the message iterator from the store which will be re-used in the
 * case of long query that cannot get all of the results in the single query
 * call.
 */
struct bquery {
	struct bq_store *store; /**< Store handle */
	sos_iter_t itr; /**< Iterator handle */
	struct bset_u32 *hst_ids; /**< Host IDs (for filtering) */
	struct bset_u32 *ptn_ids; /**< Pattern IDs (for filtering) */
	time_t ts_0; /**< The begin time stamp */
	time_t ts_1; /**< The end time stamp */
	bq_stat_t stat; /**< Query status */
	sos_obj_t obj; /**< Current sos object */
	int text_flag; /**< Non-zero if the query wants text in date-time and
			    host field */
	char sep; /**< Field separator for output */
	bsos_wrap_t bsos; /**< SOS wrap for the query */
	char sos_prefix[PATH_MAX]; /**< SOS prefix. Prefix is also used as a
					     path buffer.*/
	char *sos_prefix_end; /**< Point to the end of the original prefix */
	int sos_number; /**< The current number of SOS store (for rotation) */
};

struct bimgquery {
	struct bquery base;
	char *store_name;
	LIST_HEAD(, brange_u32) *hst_rngs; /**< Ranges of hosts */
	struct brange_u32 *crng; /**< Current range */
};

/**
 * Create query handle with given query conditions.
 *
 * \param store The store handle.
 * \param hst_ids The comma-separated list of host IDs. The range [##-##] in the
 * 	comma-separated list is also supported.
 * \param ptn_ids The comma-separated list of pattern IDs. The range is also
 * 	accepted.
 * \param ts0 The begin time stamp (format: YYYY-MM-DD hh:mm:ss)
 * \param ts1 The begin time stamp (format: YYYY-MM-DD hh:mm:ss)
 * \param is_text Non-zero if the query does not want just numbers in timestamp
 *		  and host fields.
 * \param sep Field separator (default ' ').
 * \param[out] rc The return code.
 * \return NULL on error.
 * \return newly created query handle on success.
 */
struct bquery* bquery_create(struct bq_store *store, const char *hst_ids,
			     const char *ptn_ids, const char *ts0,
			     const char *ts1, int is_text, char sep, int *rc);

/**
 * Create image query handle with given query conditions.
 *
 * \param store The store handle.
 * \param hst_ids The comma-separated list of host IDs. The range [##-##] in the
 * 	comma-separated list is also supported.
 * \param ptn_ids The comma-separated list of pattern IDs. The range is also
 * 	accepted.
 * \param ts0 The begin time stamp (format: YYYY-MM-DD hh:mm:ss)
 * \param ts1 The begin time stamp (format: YYYY-MM-DD hh:mm:ss)
 * \param node_per_pixel The number of nodes per pixel (y-axis). 0 means using
 * 	default (1).
 * \param sec_per_pixel The number of seconds per pixel (x-axis). 0 means using
 * 	default (3600).
 * \return NULL on error.
 * \return newly created query handle on success.
 */
struct bimgquery* bimgquery_create(struct bq_store *store, const char *hst_ids,
				const char *ptn_ids, const char *ts0,
				const char *ts1, const char *img_store_name,
				int *rc);

/**
 * Destroy the query \c q.
 * \param q The query handle.
 */
void bquery_destroy(struct bquery *q);

/**
 * Destroy the image query \c q.
 * \param q The query handle.
 */
void bimgquery_destroy(struct bimgquery *q);

/**
 * Get query stat.
 * \param q The query handle.
 * \returns Query status (see ::bq_stat_t).
 */
bq_stat_t bq_get_stat(struct bquery *q);

/**
 * Perform the query and return a single result.
 *
 * The result will be in the following format: DATE-TIME HOST MESSAGE.
 * If \c is_text flag is set in \c bquery_create, DATE-TIME will be "YYYY-mm-dd
 * HH:MM:SS.uuuuuu" and HOST will be hostname. If \c is_text flag is not set,
 * DATE-TIME will be "(secons since Epoc).(microsecond)" and HOST will be just a
 * number (host_id).
 *
 * If this function is called repeatedly on the same query handle \c q, it will
 * return the next result until there are no more results.
 *
 * ***REMARK*** This function will automatically allocate a buffer for the
 * result. The caller is responsible to free it.
 *
 * \param q The query handle.
 * \param rc The return code. 0 for successful query. \c ENOENT for no more
 * results and other error code for other errors.
 *
 * \return Result string.
 * \return NULL if there are no more results or error. \c *rc will be set
 * 	accordingly.
 */
char* bq_query(struct bquery *q, int *rc);

char* bq_imgquery(struct bimgquery *q, int *rc);

/**
 * Perform the query and put a single result into \c buff.
 *
 * Similar to ::bq_query, this function can be called repeatedly to get the next
 * result. If there are no more result, \c *buff is set to "" and the function
 * returns \c ENOENT.
 *
 * \param q The query handle.
 * \param buff The buffer.
 * \param bufsz The buffer size.
 *
 * \return 0 on success.
 * \return \c ENOENT on no more results.
 * \return error code on other errors.
 */
int bq_query_r(struct bquery *q, char *buff, size_t bufsz);

int bq_imgquery_r(struct bimgquery *q, char *buff, size_t bufsz);

/**
 * \param store The store handle.
 * \returns on success, all patterns in the \c store. Patterns are separated
 * with '\\n'. The returned string is automatically allocated, and the caller is
 * responsible for freeing it.
 * \return NULL on error, and errno will be set properly.
 */
char* bq_get_ptns(struct bq_store *store);

/**
 * Similar to ::get_patterns(), but the result will be stored in \c buf instead
 * of the newly allocated string. The result will be printed to \c buf upto \c
 * buflen - 1 characters and put '\\0' at <tt>buf[buflen-1]</tt>.
 *
 * \param store The store handle.
 * \param buf The buffer.
 * \param buflen The length of the buffer.
 *
 * \returns 0 on success.
 * \returns Error code on error.
 */
int bq_get_ptns_r(struct bq_store *store, char *buf, size_t buflen);

/**
 * Get all tokens of \c ptn_id at the '*' position \c arg_idx.
 * \param store The store handle.
 * \param ptn_id The pattern id.
 * \param arg_idx The index of the pattern argument (*).
 * \returns all tokens at \c arg_idx of \c ptn_id, separated by '\\n'. The
 * returned string is automatically allocated, the caller is responsible to free
 * it.
 */
char* bq_get_ptn_tkns(struct bq_store *store, int ptn_id, int arg_idx);

/**
 * Get host ID.
 * \param store The store handle.
 * \param hostname The hostname.
 * \returns Host ID if \c hostname is found.
 * \returns 0 if \c hostname is not found.
 */
int bq_get_host_id(struct bq_store *store, const char *hostname);

#endif

/** \} */
