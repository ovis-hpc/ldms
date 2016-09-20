/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-15 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013-15 Sandia Corporation. All rights reserved.
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
 * \defgroup libbquery libbquery
 * \{
 * \brief Library for querying information from Baler store.
 *
 * libbquery provides the access to the following information:
 * - messages (in given node-time window)
 * - image pixels (in given node-time window in various granularity)
 *
 * The following is the brief usage for message and image querying:
 * -# open baler store with bq_open_store()
 * -# create message query handle with bquery_create() with query constrains.
 *    For image querying, use bimgquery_create(). Please note that ::bimgquery
 *    extends ::bquery, and \ref bq_entry_access can be used on ::bimgquery.
 * -# call bq_first_entry() (or bq_last_entry()) move the query iterator to the
 *    desired position.
 * -# call bq_entry_print() to print the message, or one of the function in the
 *    \ref bq_entry_access to get the data.
 * -# call bq_next_entry() to move the iterator to the next matched entry, or
 *    bq_prev_entry() to go to the previous matched entry. The return code from
 *    the functions will tell if the iteration comes to an end.
 *
 * bquery requires balerd to use bout_sos_img for image pixel query and
 * bout_sos_msg for message query. The sos files for img's and msg's are assumed
 * to be in the store path as well.
 *
 * Other storage format for various output plugins are not supported.
 *
 * \note Store can contain several images of various granularity. Users
 * can configure multiple bout_sos_img to store multiple images as they pleased.
 * However, bquery does not know about those configuration. Users need to supply
 * the image store (e.g. -I 3600-1) to query against. The image stores
 * inside balerd store are named by SEC_PER_PIXEL-NODE_PER_PIXEL.
 *
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
#include <time.h>

#include "baler/btkn_types.h"

#define MASK_HSIZE 131071

typedef struct bsos_wrap *bsos_wrap_t;

/**
 * Open Baler Query Store.
 */
struct bq_store* bq_open_store(const char *path);

void bq_store_close_free(struct bq_store *store);

/**
 * Query status enumeration.
 */
typedef enum bquery_status {
	BQ_STAT_INIT,
	BQ_STAT_QUERYING,
	BQ_STAT_DONE,
	BQ_STAT_LAST,
} bq_stat_t;

struct bpixel {
	uint32_t sec;
	uint32_t comp_id;
	uint32_t ptn_id;
	uint32_t count;
};

#define hton_bpixel(p) do { \
	(p)->sec = htonl((p)->sec); \
	(p)->comp_id = htonl((p)->comp_id); \
	(p)->ptn_id = htonl((p)->ptn_id); \
	(p)->count = htonl((p)->count); \
} while(0)

#define nton_bpixel(p) do { \
	(p)->sec = ntohl((p)->sec); \
	(p)->comp_id = ntohl((p)->comp_id); \
	(p)->ptn_id = ntohl((p)->ptn_id); \
	(p)->count = ntohl((p)->count); \
} while(0)

/*
 * Structure name declaration to supress compilation warnings.
 */
struct bdstr;
struct bstr;
struct bquery;
struct bimgquery;
struct bmsgquery;
struct bquery_pos;

#define BQUERY_POS(ptr) \
	char __char ## ptr [24]; \
	struct bquery_pos *ptr = (void*) __char ## ptr;

struct bq_formatter {
	int (*ptn_prefix)(struct bq_formatter *fmt, struct bdstr *bdstr,
			uint32_t ptn_id);
	int (*ptn_suffix)(struct bq_formatter *fmt, struct bdstr *bdstr);
	int (*msg_prefix)(struct bq_formatter *fmt, struct bdstr *bdstr);
	int (*msg_suffix)(struct bq_formatter *fmt, struct bdstr *bdstr);
	int (*tkn_begin)(struct bq_formatter *fmt, struct bdstr *bdstr);
	int (*tkn_fmt)(struct bq_formatter *fmt, struct bdstr *bdstr,
			const struct bstr *bstr, struct btkn_attr *attr,
			uint32_t tkn_id);
	int (*tkn_end)(struct bq_formatter *fmt, struct bdstr *bdstr);
	int (*date_fmt)(struct bq_formatter *fmt, struct bdstr *bdstr,
			const struct timeval *tv);
	int (*host_fmt)(struct bq_formatter *fmt, struct bdstr *bdstr,
			const struct bstr *bstr);
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
struct bmsgquery* bmsgquery_create(struct bq_store *store, const char *hst_ids,
			     const char *ptn_ids, const char *ts0,
			     const char *ts1, int is_text, char sep, int *rc);

/**
 * Get default formatter.
 */
struct bq_formatter *bquery_default_formatter();

/**
 * Formatter setter.
 */
void bq_set_formatter(struct bquery *bq, struct bq_formatter *fmt);

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
 * Destroy the message query \c q.
 * \param q The query handle.
 */
void bmsgquery_destroy(struct bmsgquery *q);

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
 * \defgroup bq_entry_nav bquery entry navigation
 * \{
 * \brief Functions to navigate throught entries that matched the query.
 */

/**
 * Go to the first entry that matches the query.
 *
 * \retval 0 OK
 * \retval errno if error.
 */
int bq_first_entry(struct bquery *q);

/**
 * Move the query reference to the next matched entry.
 *
 * \retval 0 OK
 * \retval errno if error.
 */
int bq_next_entry(struct bquery *q);

/**
 * Move the query reference to the previously matched entry.
 *
 * \retval 0 OK
 * \retval errno if error.
 */
int bq_prev_entry(struct bquery *q);

/**
 * Special move for imgquery to move the reference to the first entry of the
 * next pattern.
 *
 * \retval 0 OK
 * \retval errno if error.
 */
int bimgquery_next_ptn(struct bimgquery *imgq);

/**
 * Special move for imgquery to move the reference to the last entry of the
 * previous pattern.
 *
 * \retval 0 OK
 * \retval errno if error.
 */
int bimgquery_prev_ptn(struct bimgquery *imgq);

/**
 * Go to the last entry that matches the query.
 *
 * \retval 0 OK
 * \retval errno if error.
 */
int bq_last_entry(struct bquery *q);

/**
 * Get current position of the query into \c pos.
 *
 * \param q the query handle.
 * \param[out] pos the output parameter to store current iterator position.
 *
 * \retval 0 OK
 * \retval errno if error.
 */
int bq_get_pos(struct bquery *q, struct bquery_pos *pos);

/**
 * Recover the current position of the query from \c pos.
 *
 * \param q the query handle.
 * \param pos the position obtained from ::bq_get_pos().
 *
 * \retval 0 OK
 * \retval errno if error.
 */
int bq_set_pos(struct bquery *q, struct bquery_pos *pos);

/**
 * Print position value into \c bdstr.
 *
 * \param pos position handle.
 * \param[out] bdstr dynamic string buffer.
 *
 * \retval 0 OK.
 * \retval errno Error.
 */
int bquery_pos_print(struct bquery_pos *pos, struct bdstr *bdstr);

/**
 * Recover position value from \c str.
 *
 * \param[out] pos bquery position handler.
 * \param str the string containing printed position value.
 *
 * \retval 0 OK
 * \retval errno Error.
 */
int bquery_pos_from_str(struct bquery_pos *pos, const char *str);

/**
 * \}
 */

/**
 * Print the entry to \c bdstr.
 *
 * If \c bdstr is NULL, a new ::bdstr is allocated.
 *
 * \retval str If \c bdstr is NULL, the caller owns the returned \c str (it is a
 *             detached ::bdstr that is automatically allocated). If \c bdstr is
 *             given, the returned \c str is the C-string inside ::bdstr
 *             structure.
 * \retval NULL If there is an error. In this case, errno is appropriately set
 *              to describe the detail of the error.
 *
 */
char *bq_entry_print(struct bquery *q, struct bdstr *bdstr);

/**
 * \defgroup bq_ptn Pattern-related query.
 * \{
 */

/**
 * \param store The store handle.
 * \returns on success, all patterns in the \c store. Patterns are separated
 * with '\\n'. The returned string is automatically allocated, and the caller is
 * responsible for freeing it.
 * \return NULL on error, and errno will be set properly.
 */
char* bq_get_all_ptns(struct bq_store *store);

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
int bq_get_all_ptns_r(struct bq_store *store, char *buf, size_t buflen);

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
 * Print formatted pattern from the store.
 *
 * \param store The store handle.
 * \param formatter The string formatter. If \c NULL, the default formatter will
 *                  be used.
 * \param ptn_id Pattern ID.
 * \param[out] out Output formatted string of the pattern.
 *
 * \retval 0 OK.
 * \retval errno Error.
 */
int bq_print_ptn(struct bq_store *store, struct bq_formatter *formatter,
						int ptn_id, struct bdstr *out);

/**
 * Check if the given \c ptn_id is a metric pattern.
 *
 * \retval 1 if the pattern is a metric pattern.
 * \retval 0 if it is not.
 */
int bq_is_metric_pattern(struct bq_store *store, int ptn_id);

/**
 * \}
 */

/**
 * Get component name from ::bq_store \c store.
 *
 * \param store The store handle
 * \param cmp_id The component ID (starts from 1)
 * \param[out] out The output ::bdstr.
 */
int bq_get_cmp(struct bq_store *store, int cmp_id, struct bdstr *out);

/**
 * Get host ID.
 * \param store The store handle.
 * \param name The name of the component (host).
 * \returns Host ID if \c hostname is found.
 * \returns -1 if \c hostname is not found.
 */
int bq_get_comp_id(struct bq_store *store, const char *name);

/**
 * \defgroup bq_sub_store sub store access.
 * \{
 * \brief functions to access sub stores (comp store, token store and pattern store).
 */

/**
 * Get component (token) store from \c store.
 */
struct btkn_store *bq_get_cmp_store(struct bq_store *store);

/**
 * ::tkn_store getter.
 */
struct btkn_store *bq_get_tkn_store(struct bq_store *store);

/**
 * ::ptn_store getter.
 */
struct bptn_store *bq_get_ptn_store(struct bq_store *store);

/**
 * \}
 */

/**
 * \defgroup bq_entry_access bquery entry data access functions
 * \{
 * \brief A group of bquery entry data access functions.
 */

/**
 * Get the field of second (time) of the current query entry.
 */
uint32_t bq_entry_get_sec(struct bquery *q);

/**
 * Get usec (micro-second) part of the current query entry.
 */
uint32_t bq_entry_get_usec(struct bquery *q);

/**
 * Get comp_id part of the current query entry.
 */
uint32_t bq_entry_get_comp_id(struct bquery *q);

/**
 * Get pattern ID of the current query entry.
 */
uint32_t bq_entry_get_ptn_id(struct bquery *q);

/**
 * Get Baler-internal strucured message of the current query entry.
 *
 * \note Unlike others, this function cannot be used with ::bimgquery.
 */
struct bmsg *bq_entry_get_msg(struct bquery *q);

/**
 * Get the message token-wise via \c cb() for each token in the message.
 *
 * When \c cb() returns non-zero value, this function will immediately stop
 * and return the \c cb() returned value.
 */
int bq_entry_msg_tkn(struct bquery *q,
		int (*cb)(uint32_t tkn_id, void *ctxt),
		void *ctxt);

/**
 * Get message reference of the current entry.
 *
 * \note Unlike others, this function cannot be used with ::bimgquery.
 */
typedef struct bq_msg_ref_s {
	uint64_t ref[2];
} bq_msg_ref_t;
bq_msg_ref_t bq_entry_get_ref(struct bquery *q);
static inline int bq_msg_ref_equal(bq_msg_ref_t *a, bq_msg_ref_t *b)
{
	return ((a->ref[0] == b->ref[0]) && (a->ref[1] == b->ref[1]));
}

/**
 * Get the counting value from the current image query entry (pixel).
 *
 * \param q the image query handle.
 *
 * \retval count The count value of current pixel.
 */
uint32_t bq_img_entry_get_count(struct bimgquery *q);

/**
 * Get current pixel information from the query \c q.
 *
 * \param[out] p the pixel output parameter.
 *
 * \retval 0 if OK.
 * \retval errno if error.
 */
int bq_img_entry_get_pixel(struct bimgquery *q, struct bpixel *p);

/** \} */

int bq_store_refresh(struct bq_store *store);

/**
 * \brief Iterate through available image store.
 * The callback function will be called for each available baler image store.
 *
 * \param store The ::bq_store handle.
 * \param cb The call-back function.
 * \param ctxt The context to supply to \c cb function.
 *
 * \retval 0 If no error.
 * \retval errno if error.
 */
int bq_imgstore_iterate(struct bq_store *store,
		void (*cb)(const char *imgstore_name, void *ctxt), void *ctxt);
#endif

/** \} */
