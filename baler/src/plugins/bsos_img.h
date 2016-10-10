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
 * \file bsos_img.h
 * \author Narate Taerat (narate at ogc dot us)
 *
 * \brief SOS wrapper for Baler Image data
 */
#ifndef __BSOS_IMG_H
#define __BSOS_IMG_H

#include "endian.h"
#include "sos/sos.h"

#define BSOS_IMG_IDX_NAME "index"

/*
 * The key in the sos index is big endian.
 */
struct __attribute__ ((__packed__)) bsos_img_key {
	/* lower bytes */
	uint32_t ptn_id;
	uint32_t ts;
	uint32_t comp_id;
	/* higher bytes */
};

static inline
void bsos_img_key_htobe(struct bsos_img_key *k)
{
	k->ptn_id = htobe32(k->ptn_id);
	k->ts = htobe32(k->ts);
	k->comp_id = htobe32(k->comp_id);
}

/*
 * These are the array index to access data in the array.
 */
#define BSOS_IMG_PTN_ID		0
#define BSOS_IMG_SEC		1
#define BSOS_IMG_COMP_ID	2
#define BSOS_IMG_COUNT		3

/**
 * \brief Image SOS wrapper
 */
struct bsos_img {
	sos_t sos;
	sos_index_t index;
};

typedef struct bsos_img *bsos_img_t;

/**
 * \brief Open Baler SOS Image store.
 *
 * \param path The path to the store.
 * \param create A \c create boolean flag. If set to 1, a new store will be
 *               created, if it does not exist.
 *
 * \retval handle The store handle, if success.
 * \retval NULL if failed. The \c errno is also set.
 */
bsos_img_t bsos_img_open(const char *path, int create);

/**
 * \brief Close Baler SOS Image store.
 *
 * \param bsos_img The \c bsos_img_t handle.
 * \param commit SOS commit type.
 *
 * \note After the store is closed, application must not use \c handle again.
 */
void bsos_img_close(bsos_img_t bsos_img, sos_commit_t commit);

#endif
