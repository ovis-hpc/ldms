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
#ifndef __SOS_IMG_CLASS_DEF_H
#define __SOS_IMG_CLASS_DEF_H

#include "endian.h"
#include "sos/sos.h"

enum {
	SOS_IMG_KEY = 0,
	SOS_IMG_COUNT,
};

/**
 * SOS Object definition for Image SOS.
 *
 * An object in Image SOS is a tuple of \<{ts, comp_id}*, ptn_id*, count \>.
 * The first field is a couple of timestamp (ts) and component ID (comp_id).
 * The second field represents pattern ID, and the third field is the count of
 * occurrences of such pattern at timestamp x component. The fields that marked
 * with '*' are indexed.
 */
static
SOS_OBJ_BEGIN(sos_img_class, "BalerSOSImageClass")
	SOS_OBJ_ATTR_WITH_KEY("ptn_id_time_comp_id", SOS_TYPE_BLOB),
	SOS_OBJ_ATTR("count", SOS_TYPE_UINT32),
SOS_OBJ_END(2);

struct __attribute__ ((__packed__)) bout_sos_img_key {
	struct sos_blob_obj_s sos_blob;
	/* lower bytes */
	uint32_t ptn_id;
	uint32_t ts;
	uint32_t comp_id;
	/* higher bytes */
};

static inline
void bout_sos_img_key_convert(struct bout_sos_img_key *k)
{
	k->ptn_id = htobe32(k->ptn_id);;
	k->ts = htobe32(k->ts);;
	k->comp_id = htobe32(k->comp_id);;
}

#endif
