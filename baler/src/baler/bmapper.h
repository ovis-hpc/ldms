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
 * \file bmapper.h
 * \author Narate Taerat (narate@ogc.us)
 * \date Mar 19, 2013
 *
 * \defgroup bmapper Baler Mapper Module
 * \{
 * \brief Baler Mapper module.
 *
 * Baler Mapper module serve mapping functionality of Tokens and Patterns for
 * Baler. In other words, it does seq<-->id.
 */
#ifndef _BMAPPER_H
#define _BMAPPER_H
#include <stdint.h>
#include <sys/mman.h>
#include <wchar.h>
#include <sys/queue.h>
#include <linux/limits.h>
#include <pthread.h>
#include "bmem.h"
#include "btypes.h"
#include "bmlist.h"
#include "bmvec.h"

/**
 * Special ID for error code for Baler Map.
 */
typedef enum {
	BMAP_ID_ERR_BEGIN = 0,
	BMAP_ID_NOTFOUND=0,	/**< For search not foundt */
	BMAP_ID_NONE = BMAP_ID_NOTFOUND,
	BMAP_ID_ERR,		/**< For various error */
	BMAP_ID_INVAL,
	BMAP_ID_ERR_END = 63,
} bmap_id_error_t;

static inline
int bmap_id_is_err(uint32_t id)
{
	return BMAP_ID_ERR_BEGIN <= id && id <= BMAP_ID_ERR_END;
}

#define BMAP_STAR_TEXT "\u2022"

/**
 * Special ID for Baler Map.
 */
typedef enum {
	BMAP_ID_SPECIAL_BEGIN = 64,
	BMAP_ID_STAR=64,	/**< The special ID for KLEEN_STAR */
	BMAP_ID_SPECIAL_END = 127,
	BMAP_ID_BEGIN=128,	/**< The first non-special ID */
} bmap_id_special_t;

/**
 * Information to be saved in additional mmap file as header of ::bmap.
 */
struct bmap_hdr {
	uint32_t next_id;
	uint32_t count;
};

typedef enum {
	BMAP_EVENT_NEW_INSERT,
	BMAP_EVENT_LAST
} bmap_event_t;

typedef struct bmap* bmap_t;

/**
 * Call back interface for bmap events.
 *
 * The actual structure of \c arg depends on event type.
 */
typedef void (*bmap_ev_cb_fn)(bmap_t, bmap_event_t, void *arg);

struct bmap_event_new_insert_arg {
	uint32_t id;
	const struct bstr *bstr;
};

/**
 * \brief Baler Mapper structure.
 *
 * Baler Mapper structure holds information regarding the map, which are the
 * path to the bmap file and memory region that mmapped to that file.
 */
struct bmap {
	/**
	 * \brief A path to bmap file.
	 */
	char path[PATH_MAX];

	/**
	 * \brief A write mutex for the map.
	 */
	pthread_mutex_t mutex;

	/**
	 * The ::bmem that stores header information.
	 */
	struct bmem *mhdr;

	/**
	 * This pointer points to ::bmap::mhdr->ptr for convenient usage.
	 * The rest of the ::bmem structures do not get convenient pointers
	 * because they are dynamically growing and can be re-mapped.
	 * The header, on the other hand, is fixed length.
	 */
	struct bmap_hdr *hdr;

	/**
	 * \brief Hash table.
	 * mhash->ptr is of type ::bvec_u64's
	 */
	struct bmvec_u64 *bmhash;

	/**
	 * \brief Linked list of ID's.
	 * mlist->ptr is of type ::bvec_u32's.
	 * They're stored contiguously in the mapped file. Each of them is
	 * either referred to by Hash table (mhash) or by another one of them
	 * (as a linked list should be)
	 */
	struct bmem *mlist;

	/**
	 * \brief Memory that store ::bstr indices (offset) of strings.
	 * This will be mapped to \a path /mstr_idx.mmap.
	 * ::BPTR(mstr, str_idx->data[ID]) is a
	 * ::bstr pointer, pointing to a Baler string corresponding to
	 * ID.
	 */
	struct bmvec_u64 *bmstr_idx;

	/**
	 * \brief Memory that store ::bstr's.
	 * This part of the memory will be mapped to \a path /str.mmap.
	 */
	struct bmem *mstr;

	/**
	 * \brief User context.
	 */
	void *ucontext;

	/**
	 * \brief BMAP Event callback function.
	 */
	bmap_ev_cb_fn ev_cb;
};

/**
 * \brief Open a bmap file, specified by \a path.
 * \param path The path to bmap file.
 * \return A pointer to bmap structure allocated by this function.
 */
struct bmap* bmap_open(const char *path);

/**
 * \brief Close the ::bmap \a m.
 * \param m The ::bmap to be closed.
 */
void bmap_close_free(struct bmap* m);

/**
 * \brief Initialize the bmap structure.
 * \param map The pointer to bmap structure to be initialized.
 * \param nmemb The number of elements in the hash.
 */
int bmap_init(struct bmap *map, int nmemb);

/**
 * \brief Re-initialize the bmap structure.
 * This function is used in the case of hash resizing + rehashing.
 * \param map The map.
 * \param nmemb The number of elements of new hash.
 */
int bmap_rehash(struct bmap *map, int nmemb);

/**
 * \brief Get ID from a wchar_t sequence.
 * \param map The map to obtain information from.
 * \param s The sequence of wchar_t (describing a token or a pattern).
 * \return ID of \a s.
 */
uint32_t bmap_get_id(struct bmap *map, const struct bstr *s);

/**
 * \brief Get the bstr from \a map, speified by \a id.
 * \param map The map.
 * \param id ID of the wanted ::bstr.
 * \return The wchar_t* to the sequence specified by \a id.
 */
const struct bstr* bmap_get_bstr(struct bmap *map, uint32_t id);

/**
 * Return flags for ::bmap_insert.
 */
typedef enum {
	BMAP_INS_RET_NEW=0, /**< The inserted data is new. */
	BMAP_INS_RET_EXIST, /**< The inserted data existed. */
} bmap_ins_ret_t;

static char* bmap_ins_ret_str[] = {
	"BMAP_INS_RET_NEW",
	"BMAP_INS_RET_EXIST",
};

/**
 * Insert ::bstr \a s into ::bmap \a bm.
 * \param bm The ::bmap structure.
 * \param s The ::bstr structure.
 * \retval ID on success.
 * \retval bmap_id_error_t appropriate special ID for error.
 */
uint32_t bmap_insert(struct bmap *bm, const struct bstr *s);

/**
 * Insert mapping \c id <---> \c s in the map, in which both 'id' and 's' do not
 * exist in the map.
 *
 * \retval _id If success.
 * \retval bmap_id_error_t If error.
 */
uint32_t bmap_insert_with_id(struct bmap *bm, const struct bstr *s,
		uint32_t _id);

void bmap_dump(struct bmap *bmap);
void bmap_dump_inverse(struct bmap *map);

void* bmap_get_ucontext(struct bmap *bmap);

void bmap_set_ucontext(struct bmap *bmap, void *ucontext);

void bmap_set_event_cb(struct bmap *bmap, bmap_ev_cb_fn ev_cb);

/**
 * \brief Unlink the mapper of the given \c path.
 *
 * \param path The path of the bmapper.
 *
 * \retval 0 if no error.
 * \retval errno if error.
 */
int bmap_unlink(const char *path);

/**
 * \brief Refreshing all bmem owned by \c bmap.
 *
 * \retval 0 if OK.
 * \retval errno if error.
 */
int bmap_refresh(struct bmap *bmap);

/**
 * \brief Convert \c comp_id to \c map_id.
 *
 * \retval map_id
 */
static inline
uint32_t bcompid2mapid(uint32_t comp_id)
{
	return comp_id + BMAP_ID_BEGIN;
}

/**
 * \brief Convert baler \c map_id to \c comp_id.
 *
 * \retval comp_id
 */
static inline
uint32_t bmapid2compid(uint32_t map_id)
{
	return map_id - BMAP_ID_BEGIN;
}

#endif // _BMAPPER_H
/**\}*/
