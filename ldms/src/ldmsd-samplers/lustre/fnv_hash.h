/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013,2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013,2015 Sandia Corporation. All rights reserved.
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
 *
 * \file fnv_hash.h
 *
 * \defgroup fnv_hash Fowler-Noll-Vo hash functions.
 * \{
 * \brief Implementation of Fowler-Noll-Vo hash functions.
 * The algorithms are developed by Glenn Fowler, London Curt Noll and Phong Vo.
 * For more information about FNV hash functions, please visit
 * http://www.isthe.com/chongo/tech/comp/fnv/index.html
 */

#ifndef __FMV_HASH_H
#define __FMV_HASH_H

#define FNV_32_PRIME 0x01000193
#define FNV_64_PRIME 0x100000001b3ULL

/**
 * \brief An implementation of FNV-A1 hash algorithm, 32-bit hash value.
 * \param str The string (or byte sequence) to be hashed.
 * \param len The length of the string.
 * \param seed The seed of (re-)hash, usually being 0.
 * \return A 32-bit unsigned integer hash value.
 */
static inline
uint32_t fnv_hash_a1_32(const char *str, int len, uint32_t seed)
{
	uint32_t h = seed;
	const char *end = str + len;
	const char *s;
	for (s = str; s < end; s++) {
		h ^= *s;
		h *= FNV_32_PRIME;
	}

	return h;
}

/**
 * \brief An implementation of FNV-A1 hash algorithm, 64-bit hash value.
 * \param str The string (or byte sequence) to be hashed.
 * \param len The length of the string.
 * \param seed The seed of (re-)hash, usually being 0.
 * \return A 64-bit unsigned integer hash value.
 */
static inline
uint64_t fnv_hash_a1_64(const char *str, int len, uint64_t seed)
{
	uint64_t h = seed;
	const char *end = str + len;
	const char *s;
	for (s = str; s < end; s++) {
		h ^= *s;
		h *= FNV_64_PRIME;
	}

	return h;
}

#endif // __FMV_HASH_H
/**\}*/
