/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013-2016 Sandia Corporation. All rights reserved.
 *
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
 * \file bmhash.h
 * \author Narate Taerat (narate at ogc dot us)
 */
#ifndef __BMHASH_H
#define __BMHASH_H

#include <linux/limits.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "bcommon.h"
#include "bmlist.h"
#include "btypes.h"
#include "bmem.h"

typedef enum {
	BMHASH_FN_FNV,
	BMHASH_FN_MURMUR,
} bmhash_fn_e;

struct bmhash_header {
	union {
		struct {
			char ver[16];
			uint32_t hash_size;
			uint32_t seed;
			bmhash_fn_e fn_enum;
			uint32_t count;
		};
		char __space[4096];
	};
	struct bvec_u64 table[0]; /* Convenience reference to the hash table
				   * that is always next to the header */
};

struct bmhash_entry {
	uint64_t value;
	struct bmlist_link link;
	struct bstr key;
};

/**
 * Baler Utility - Serializable Hash Container.
 *
 * This is an insert-only container. The key of the hash is ::bstr and the value
 * is uint64_t.
 */
struct bmhash {
	/**
	 * \brief A path to bmap file.
	 */
	char path[PATH_MAX];

	/**
	 * Mapped memory region of ::bmhash.
	 */
	struct bmem *mem;

	/**
	 * Hash function.
	 */
	uint32_t (*hash_fn)(const char *key, int len, uint32_t seed);
};

struct bmhash_iter {
	struct bmhash *bmh;
	uint32_t idx;
	struct bmhash_entry *ent;
};

struct bmhash *bmhash_open(const char *path, int flag, ...);

void bmhash_close_free(struct bmhash *bmh);

int bmhash_unlink(const char *path);

struct bmhash_entry *bmhash_entry_set(struct bmhash *bmh,
				      const struct bstr *key, uint64_t value);

struct bmhash_entry *bmhash_entry_get(struct bmhash *bmh,
				      const struct bstr *key);

struct bmhash_iter *bmhash_iter_new(struct bmhash *bmh);

void bmhash_iter_init(struct bmhash_iter *iter, struct bmhash *bmh);

void bmhash_iter_free(struct bmhash_iter *iter);

struct bmhash_entry *bmhash_iter_entry(struct bmhash_iter *iter);

int bmhash_iter_begin(struct bmhash_iter *iter);

int bmhash_iter_next(struct bmhash_iter *iter);
#endif
