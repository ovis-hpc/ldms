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
/**
 * \file bsos_msg.h
 * \author Narate Taerat (narate at ogc dot us)
 */
#ifndef __BSOS_MSG_H
#define __BSOS_MSG_H

#include "endian.h"
#include "sos/sos.h"

/* little endian */
#define BSOS_MSG_PTN_ID  0
#define BSOS_MSG_SEC     1
#define BSOS_MSG_USEC    2
#define BSOS_MSG_COMP_ID 3
#define BSOS_MSG_ARGV_0  4

#define BSOS_MSG_IDX_PTH_NAME "index_pth"
#define BSOS_MSG_IDX_TH_NAME "index_th"

/*
 * The key in the sos index is big endian, because key_UINT96 uses memcmp() to
 * compare two keys.
 */
struct __attribute__ ((__packed__)) bsos_msg_key_ptc {
	/* lower bytes, more precedence */
	uint32_t ptn_id;
	uint32_t sec;
	uint32_t comp_id;
	/* higher bytes */
};

static inline
void bsos_msg_key_ptc_set_ptn_id(struct bsos_msg_key_ptc *k, uint32_t ptn_id)
{
	k->ptn_id = htobe32(ptn_id);
}

static inline
void bsos_msg_key_ptc_set_comp_id(struct bsos_msg_key_ptc *k, uint32_t comp_id)
{
	k->comp_id = htobe32(comp_id);
}

static inline
void bsos_msg_key_ptc_set_sec(struct bsos_msg_key_ptc *k, uint32_t sec)
{
	k->sec = htobe32(sec);
}

static inline
void bsos_msg_key_ptc_htobe(struct bsos_msg_key_ptc *k)
{
	k->ptn_id = htobe32(k->ptn_id);
	k->comp_id = htobe32(k->comp_id);
	k->sec = htobe32(k->sec);
}

struct __attribute__ ((packed)) bsos_msg_key_tc {
	/* this structure is uint64_t, lower byte has less precedence */
	/* NOTE: We don't care about big-endian machine at the moment */
	uint32_t comp_id;
	uint32_t sec;
};

static inline
void bsos_msg_key_tc_set_comp_id(struct bsos_msg_key_tc *k, uint32_t comp_id)
{
	k->comp_id = comp_id;
}

static inline
void bsos_msg_key_tc_set_sec(struct bsos_msg_key_tc *k, uint32_t sec)
{
	k->sec = sec;
}

#define __BSOSMSG_CMP(a0, a1, field, op) (((a0)->data.uint32_[field] op (a1)->data.uint32_[field]))

static inline
int bsos_msg_obj_cmp(sos_array_t msg0, sos_array_t msg1)
{
	if (__BSOSMSG_CMP(msg0, msg1, BSOS_MSG_SEC, <))
		return -1;
	if (__BSOSMSG_CMP(msg0, msg1, BSOS_MSG_SEC, >))
		return 1;
	if (__BSOSMSG_CMP(msg0, msg1, BSOS_MSG_COMP_ID, <))
		return -1;
	if (__BSOSMSG_CMP(msg0, msg1, BSOS_MSG_COMP_ID, >))
		return 1;
	if (__BSOSMSG_CMP(msg0, msg1, BSOS_MSG_PTN_ID, <))
		return -1;
	if (__BSOSMSG_CMP(msg0, msg1, BSOS_MSG_PTN_ID, >))
		return 1;
	return 0;
}

/**
 * \brief Message SOS wrapper
 */
struct bsos_msg {
	sos_t sos;
	sos_index_t index_ptc; /* PTH: PatternID, Timestamp, CompID */
	sos_index_t index_tc; /* TH: Timestamp, CompID */
};

typedef struct bsos_msg *bsos_msg_t;

/**
 * \brief Open Baler SOS Image store.
 *
 * \retval handle The store handle, if success.
 * \retval NULL if failed. The \c errno is also set.
 */
bsos_msg_t bsos_msg_open(const char *path, int create);

/**
 * \brief Close Baler SOS Image store.
 *
 * \param bsos_msg The store handle.
 * \param commit The SOS commit type.
 *
 * \note After the store is closed, application must not use \c handle again.
 */
void bsos_msg_close(bsos_msg_t bsos_msg, sos_commit_t commit);

#endif
