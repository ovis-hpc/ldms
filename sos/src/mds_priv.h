/*
 * Copyright (c) 2012 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2012 Sandia Corporation. All rights reserved.
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

/*
 * Author: Tom Tucker tom at ogc dot us
 */

#ifndef __MDS_PRIV_H
#define __MDS_PRIV_H

#include <stdint.h>
#include <sys/queue.h>
#include "mds.h"
#include "oidx.h"
#include "idx.h"

/*
 * A premise is that the records are ordered by time in the
 * file. Therefore, at offset X <= Y, TS_x <= TS_y
 */
typedef struct mds_record_s {
	uint32_t tv_sec;
	uint32_t tv_usec;
	uint32_t comp_id;

	uint64_t value;
	/* The offset of the next record with the same comp_id */
	uint64_t next_comp;
	/* The offset of the previous record with the same comp_id */
	uint64_t prev_comp;
	/* The offset of the next record with the same timestamp */
	uint64_t next_ts;
	/* The offset of the previous record with the same timestamp */
	uint64_t prev_ts;
} *mds_record_t;

typedef struct mds_index_s {
	uint64_t first;
	uint64_t last;
} *mds_index_t;

#define MDS_SIGNATURE "MDSSTORE"
typedef struct mds_udata_s {
	char signature[8];
	uint32_t ods_extend_sz;
} *mds_udata_t;

struct mds_s {
	/* "Path" to the file. This is used as a prefix for all the
	 *  real file paths */
	const char *path;

	/* ODS containing all index objects, i.e the objects pointed
	 * to by the indices. */
	ods_t ods;
#define MDS_ODS_EXTEND_SZ (16*1024*1024);
	size_t ods_extend_sz;
	mds_udata_t udata;
	size_t udata_sz;

	/* Used if idx_mask includes MDS_COMP_IDX */
	oidx_t comp_idx;

	/* Used if idx_mask includes MDS_TIME_IDX */
	oidx_t time_idx;

	/* Used if idx_mask includes MDS_VALUE_IDX */
	oidx_t value_idx;
};

struct mds_iter_s {
	mds_t mds;
	oidx_t oidx;
	struct mds_key_s key;
	uint64_t start;
	uint64_t next;
};

#endif
