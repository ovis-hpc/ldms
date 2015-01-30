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
#ifndef __BQUERY_PRIV_H
#define __BQUERY_PRIV_H
#include <sos/sos.h>
#include <sys/queue.h>
#include "bquery.h"

#include "baler/bset.h"
#include "baler/btkn.h"
#include "baler/bptn.h"

#include "plugins/sos_img_class_def.h"
#include "plugins/sos_msg_class_def.h"

struct bsos_wrap {
	sos_t sos;
	char *store_name;
	char *path;
	LIST_ENTRY(bsos_wrap) link;
};

LIST_HEAD(bsos_wrap_head, bsos_wrap);

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
struct bsos_wrap* bsos_wrap_find(struct bsos_wrap_head *head,
				 const char *store_name);

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

struct bq_store {
	char *path;
	struct bptn_store *ptn_store;
	struct btkn_store *tkn_store;
	struct btkn_store *cmp_store;
};

#endif
