/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015-2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2015-2016 Sandia Corporation. All rights reserved.
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
#include "baler/bheap.h"
#include "baler/bmeta.h"

#include "plugins/bsos_img.h"
#include "plugins/bsos_msg.h"

struct bsos_wrap {
	sos_t sos;
	sos_index_t index;
	char *store_name;
	char *path;
	LIST_ENTRY(bsos_wrap) link;
};

struct __attribute__((packed)) bquery_pos {
	union {
		struct {
			sos_pos_t pos;
			uint32_t ptn_id;
			uint32_t dir;
		};
		char data[24];
	};
};

LIST_HEAD(bsos_wrap_head, bsos_wrap);

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
	void *bsos; /**< bsos handle */
	char sos_prefix[PATH_MAX]; /**< SOS prefix. Prefix is also used as a
					     path buffer.*/
	struct bq_formatter *formatter; /**< Formatter */

	struct brange_u32_head hst_rngs; /**< Ranges of hosts */
	struct brange_u32_iter *hst_rng_itr;
	struct brange_u32_head ptn_rngs; /**< Ranges of patterns */
	struct brange_u32_iter *ptn_rng_itr;

	/* value getter interface */
	uint32_t (*get_sec)(struct bquery*);
	uint32_t (*get_usec)(struct bquery*);
	uint32_t (*get_comp_id)(struct bquery*);
	uint32_t (*get_ptn_id)(struct bquery*);

	/* entry iteration interface */
	int (*first_entry)(struct bquery*);
	int (*next_entry)(struct bquery*);
	int (*prev_entry)(struct bquery*);
	int (*last_entry)(struct bquery*);
	int (*get_pos)(struct bquery*, struct bquery_pos *pos);
	int (*set_pos)(struct bquery*, struct bquery_pos *pos);

	/* bsos open/close interface */
	void *(*bsos_open)(const char *path, int create);
	void (*bsos_close)(void *path, sos_commit_t commit);
};

struct bq_msg_ptc_hent;

struct bmsgquery {
	struct bquery base;
	sos_array_t msg; /**< Current msg object */
	enum {
		BMSGIDX_PTC,
		BMSGIDX_TC,
	} idxtype;
	enum {
		BMSGQDIR_FWD,
		BMSGQDIR_BWD,
	} last_dir;
	struct bheap *bheap;
	LIST_HEAD(, bq_msg_ptc_hent) hent_list; /* store heap entries that are
						 * not in the heap */
};

struct bimgquery {
	struct bquery base;
	char *store_name;
	sos_array_t img;	/**< Current img object  */
};

struct bq_store {
	char path[PATH_MAX];
	struct bptn_store *ptn_store;
	struct btkn_store *tkn_store;
	struct btkn_store *cmp_store;
	struct bmptn_store *mptn_store;
};

struct bq_msg_ptc_hent {
	sos_iter_t iter;
	uint32_t ptn_id;
	union {
		struct __attribute__((packed)) {
			uint32_t comp_id;
			uint32_t sec;
		};
		uint64_t sec_comp_id;
	};
	struct brange_u32_iter *hst_rng_itr;
	sos_array_t msg; /* current message */
	sos_obj_t obj; /* current object */
	struct bmsgquery *msgq; /* convenient reference */
	LIST_ENTRY(bq_msg_ptc_hent) link;
};

struct bq_msg_ptc_hent *bq_msg_ptc_hent_new(struct bmsgquery *msgq, uint32_t ptn_id);
void bq_msg_ptc_hent_free(struct bq_msg_ptc_hent *hent);

int bq_msg_ptc_hent_first(struct bq_msg_ptc_hent *hent);
int bq_msg_ptc_hent_next(struct bq_msg_ptc_hent *hent);
int bq_msg_ptc_hent_last(struct bq_msg_ptc_hent *hent);
int bq_msg_ptc_hent_prev(struct bq_msg_ptc_hent *hent);
int bq_msg_ptc_hent_cmp_inc(struct bq_msg_ptc_hent *a, struct bq_msg_ptc_hent *b);
int bq_msg_ptc_hent_cmp_dec(struct bq_msg_ptc_hent *a, struct bq_msg_ptc_hent *b);

typedef enum bq_check_cond {
	BQ_CHECK_COND_OK    =  0x0,
	BQ_CHECK_COND_TS0   =  0x1,
	BQ_CHECK_COND_TS1   =  0x2,
	BQ_CHECK_COND_HST  =  0x4,
	BQ_CHECK_COND_PTN   =  0x8,
} bq_check_cond_e;

#endif
