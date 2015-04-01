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
 * \file btypes.h
 * \author Narate Taerat (narate@ogc.us)
 * \date Mar 20, 2013
 *
 *
 * \defgroup btype Baler basic types
 * \{
 * \brief Basic types used in Baler.
 */
#ifndef _BTYPES_H
#define _BTYPES_H
#include <wchar.h>
#include <stdint.h>
#include <sys/queue.h>
#include <stdlib.h>
#include <string.h>

/**
 * Pair of strings (s0, s1).
 */
struct bpair_str {
	char *s0; /**< The first string. */
	char *s1; /**< The second string. */
	LIST_ENTRY(bpair_str) link; /**< The link to be used in linked list. */
};

/**
 * The head of list of ::bpair_str.
 */
LIST_HEAD(bpair_str_head, bpair_str);

/**
 * Allocation function for ::bpair_str.
 * This function will not own \a s0 and \a s1, but duplicate them instead.
 * The caller be aware to free the unused \a s0 and \a s1.
 * \param s0 The first string of the pair.
 * \param s1 The second string of the pair.
 */
static inline
struct bpair_str* bpair_str_alloc(const char *s0, const char *s1)
{
	struct bpair_str *pstr = (typeof(pstr)) calloc(1,sizeof(*pstr));
	if (s0) {
		pstr->s0 = strdup(s0);
		if (!pstr->s0)
			goto err0;
	}
	if (s1) {
		pstr->s1 = strdup(s1);
		if (!pstr->s1)
			goto err1;
	}
	return pstr;
err1:
	free(pstr->s0);
err0:
	free(pstr);
	return NULL;
}

/**
 * Free function for ::bpair_str.
 * \param pstr The pair of strings to be freed.
 */
static inline
void bpair_str_free(struct bpair_str *pstr)
{
	free(pstr->s0);
	free(pstr->s1);
	free(pstr);
}

static inline
void bpair_str_list_free(struct bpair_str_head *lh)
{
	struct bpair_str *s;
	while ((s = LIST_FIRST(lh))) {
		LIST_REMOVE(s, link);
		bpair_str_free(s);
	}
}

/**
 * Search (\a s0, \a s1) in the list.
 * If \a s0 is null, only \a s1 will be used for matching in the search.
 * Likewise if \a s1 is null.
 *
 * If \a s0 and \a s1 are both null, this function will return NULL.
 *
 * \param head The head of the pair list.
 * \param s0 The first string of the pair.
 * \param s1 The second string of the pair.
 * \return NULL if there is no pair matched (\a s0, \a s1)
 */
static inline
struct bpair_str* bpair_str_search(struct bpair_str_head *head,
		const char *s0, const char *s1)
{
	struct bpair_str *l;
	if (!s0 && !s1)
		return NULL;
	LIST_FOREACH(l, head, link) {
		if ((!s0 || (strcmp(s0, l->s0)==0))
				&& (!s1 || (strcmp(s1, l->s1)==0)))
			return l;
	}
	return NULL;
}

/**
 * \brief Baler string structure for various types.
 */
struct __attribute__((packed)) bstr {
	uint32_t blen; /**< Byte length of the string */
	union {
		char cstr[0]; /**< For regular char. */
		uint32_t u32str[0]; /**< For uint32_t. */
	};
};

#define BMETRIC_LEAD_TKN "\001\001\001\001"

static struct bstr *BMETRIC_LEAD_TKN_BSTR =
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	(void*)"\004\000\000\000" BMETRIC_LEAD_TKN
#else
	(void*)"\000\000\000\004" BMETRIC_LEAD_TKN
#endif
	;

/**
 * Convenient function to assign cstr to bstr.
 *
 * If \c len is 0, \c strlen(cstr) is used to determine the length.
 * Please be careful not to assign \c cstr that is longer than allocated \c
 * bstr.
 *
 * \param bstr The ::bstr.
 * \param cstr String to be assigned.
 * \param len \c cstr length. 0 means using \c strlen(cstr).
 */
static inline
void bstr_set_cstr(struct bstr *bstr, const char *cstr, int len)
{
	if (!len)
		len = strlen(cstr);
	memcpy(bstr->cstr, cstr, len);
	bstr->blen = len;
}

static inline
void bstr_cpy(struct bstr *dest, const struct bstr *src)
{
	dest->blen = src->blen;
	memcpy(dest->cstr, src->cstr, src->blen);
}

/**
 * Convenient allocation function for ::bstr.
 * \param blen The byte length of the string.
 */
#define bstr_alloc(blen) ((struct bstr*) malloc(sizeof(struct bstr)+blen))

static inline
struct bstr* bstr_alloc_init_cstr(const char *cstr)
{
	int len = strlen(cstr);
	struct bstr *bs = bstr_alloc(len);
	if (!bs)
		return NULL;
	bs->blen = len;
	memcpy(bs->cstr, cstr, len);
	return bs;
}

/**
 * Wrapper of regular free function for ::bstr.
 * This macro exists to complement ::bstr_alloc()
 * \param ptr The pointer to ::bstr.
 */
#define bstr_free(ptr) free(ptr)

static inline
int bstr_cmp(const struct bstr *b0, const struct bstr *b1)
{
	int len = (b0->blen < b1->blen)?(b0->blen):(b1->blen);
	return strncmp(b0->cstr, b1->cstr, len);
}

static inline
uint32_t bstr_len(const struct bstr *s)
{
	return sizeof(*s) + s->blen;
}

static inline
struct bstr *bstr_dup(const struct bstr *b)
{
	struct bstr *out = bstr_alloc(b->blen);
	if (!out)
		return NULL;
	memcpy(out->cstr, b->cstr, b->blen);
	return out;
}

/**
 * Generic bvec structure (using char[] as data array).
 *
 * \note len and alloc_len are in "number of elements" not "bytes".
 * 	The size of an element is handled outside this structure.
 *
 * \note bvec_* are meant to use with bmvec_* as data can grow deliberately
 * 	in a file. To use in-memory dynamic array, please use ::barray.
 */
struct __attribute__((packed)) bvec_generic {
	uint32_t alloc_len;
	uint32_t len;
	char data[0];
};

/**
 * \brief Baler vector structure for u32.
 * \note bvec_* are meant to use with bmvec_* as data can grow deliberately
 * 	in a file. To use in-memory dynamic array, please use ::barray.
 */
struct __attribute__((packed)) bvec_u32 {
	uint32_t alloc_len; ///< The number of allocated cells.
	uint32_t len; ///< The number of occupied cells.
	uint32_t data[0];
};

/**
 * \brief Baler vector structure for u64.
 * \note bvec_* are meant to use with bmvec_* as data can grow deliberately
 * 	in a file. To use in-memory dynamic array, please use ::barray.
 */
struct __attribute__((packed)) bvec_u64 {
	uint32_t alloc_len; ///< The number of allocated cells.
	uint32_t len; ///< The number of occupied cells.
	uint64_t data[0];
};

/**
 * Convenient function for bvec allocation.
 * \param alloc_len The allocation length (in number of element).
 * \param elm_size The size of an element.
 */
static inline
void* bvec_generic_alloc(uint32_t alloc_len, uint32_t elm_size) {
	return malloc(sizeof(struct bvec_generic) + alloc_len*elm_size);
}

/**
 * A macro for defining new BVEC type.
 * \param name The name of the newly define BVEC type.
 * \param type The type of the data.
 */
#define BVEC_DEF(name, type) \
	struct __attribute((packed)) name { \
		uint32_t alloc_len; \
		uint32_t len; \
		type data[0]; \
	};

/**
 * \brief Baler vector structure for char.
 * This can be abused as generic bvec.
 * \note Be aware that the field \a len and \a alloc_len are not in bytes.
 * They are in the number of elements.
 */
BVEC_DEF(bvec_char, char);

/**
 * List head definition for Baler String List.
 */
LIST_HEAD(bstr_list_head, bstr_list_entry);

/**
 * Baler String List Entry definition.
 */
struct bstr_list_entry {
	LIST_ENTRY(bstr_list_entry) link; /**< Link to next/previous entry. */
	struct bstr str; /**< The string. */
};

/**
 * String List Entry allocation convenient function.
 * \param str_blen The byte length of the string
 * \return NULL on error.
 * \return A pointer to ::bstr_list_entry of string lenght \a str_len.
 */
static inline
struct bstr_list_entry* bstr_list_entry_alloc(int str_blen)
{
	struct bstr_list_entry *e = (typeof(e)) malloc(sizeof(*e) + str_blen);
	if (!e)
		return NULL;
	e->str.blen = str_blen;
	return e;
}

/**
 * Similar to ::bstr_list_entry_alloc() but with string initialization.
 * \param str_blen The byte length of the string.
 * \param s The initialization string.
 */
static inline
struct bstr_list_entry* bstr_list_entry_alloci(int str_blen, char *s)
{
	struct bstr_list_entry *e = (typeof(e)) malloc(sizeof(*e) + str_blen);
	if (!e)
		return NULL;
	e->str.blen = str_blen;
	memcpy(e->str.cstr, s, str_blen);
	return e;
}

static inline
void bstr_list_free_entries(struct bstr_list_head *head)
{
	struct bstr_list_entry *x;
	while ((x = LIST_FIRST(head))) {
		LIST_REMOVE(x, link);
		free(x);
	}
}

static inline
void bstr_list_free(struct bstr_list_head *head)
{
	bstr_list_free_entries(head);
	free(head);
}

/**
 * Baler Message.
 * A baler message is defined by its 1-pattern id and the pattern arguments.
 */
struct bmsg {
	uint32_t ptn_id; /** Pattern ID. */
	uint32_t argc; /** Argument count. */
	uint32_t argv[0]; /** Arguments. */
};

/**
 * Convenient size calculation for a given \a msg.
 * \param msg The pointer to ::bmsg.
 */
#define BMSG_SZ(msg_p) (sizeof(struct bmsg) + msg_p->argc*sizeof(*msg_p->argv))

/**
 * Convenient allocation macro for ::bmsg.
 * \param argc The number of arguments for the pattern in the message.
 */
#define bmsg_alloc(argc) ((struct bmsg*)malloc(sizeof(struct bmsg)	\
						+argc*sizeof(uint32_t)))

/**
 * Wrapper for regular free() function (just to match ::bmsg_alloc).
 * \param msg The pointer to ::bmsg to be freed.
 */
#define bmsg_free(msg) free(msg)

#endif // _BTYPES_H
/**\}*/
