/*
 * Copyright (c) 2010-16 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2010-16 Sandia Corporation. All rights reserved.
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
 *  dynamic string macro template with arbitrary static size.
 * This file defines code, not just signatures.
 */

 /*
  * Example:
  char *createfoo ()
  {
	  int i;
	dsinit;
	for (i=0; i < 10000; i++) {
		dscat("1 year good luck ");
	}
	return dsdone;
  }
  */

#ifndef BIG_DSTRING_H
#define BIG_DSTRING_H

#include "../ovis_util/dstring.h"

#include <assert.h>
#undef DSTRING_STATIC_SIZE

/* big_dstring_t safely castable to dstring_t for functions
 * which do not rely on knowing DSTRING_STATIC_SIZE
 * (init, free, extract rely on DSTRING_STATIC_SIZE).
 */

#define BIG_DSTRING_TYPE(DSTRING_STATIC_SIZE) \
typedef struct big_dstring { \
	char *string;  \
	int length;  \
	int capacity;  \
	int dead; \
	char staticSpace[DSTRING_STATIC_SIZE]; \
} big_dstring_t; \
 \
static void bdstr_free(big_dstring_t *dsPtr); \
static void bdstr_init(big_dstring_t *dsPtr); \
static inline char *bdstr_set(big_dstring_t *dsPtr, const char *string); \
static inline char *bdstr_set_int(big_dstring_t *dsPtr, int64_t val); \
static inline char *bdstrcat( big_dstring_t *dsPtr, const char * string, int len); \
static inline void bdstr_trunc(big_dstring_t *dsPtr, int length); \
static char *bdstr_extract(big_dstring_t *dsPtr); \
static inline int bdstrlen(const big_dstring_t *dsPtr); \
static inline int bdstrcurmaxlen(const big_dstring_t *dsPtr); \
static inline const char *bdstrval(const big_dstring_t *dsPtr); \
 \
 \
static void bdstr_free(big_dstring_t *dsPtr) \
{ \
	if (!dsPtr) \
		return; \
	if (dsPtr->string != dsPtr->staticSpace) { \
		free(dsPtr->string); \
	} \
	dsPtr->string = dsPtr->staticSpace; \
	dsPtr->length = 0; \
	dsPtr->dead = 0; \
	dsPtr->capacity = DSTRING_STATIC_SIZE; \
	dsPtr->staticSpace[0] = '\0'; \
} \
 \
 \
static void bdstr_init(big_dstring_t *dsPtr) \
{ \
	assert(NULL != dsPtr); \
	dsPtr->string = dsPtr->staticSpace; \
	dsPtr->length = 0; \
	dsPtr->dead = 0; \
	dsPtr->capacity = DSTRING_STATIC_SIZE; \
	dsPtr->staticSpace[0] = '\0'; \
} \
 \
 \
static char *bdstr_extract(big_dstring_t *dsPtr) \
{ \
	char *result; \
	assert (NULL != dsPtr); \
	result = (char *)malloc(strlen(dsPtr->string)+1); \
	if (result) strcpy(result,dsPtr->string); \
	bdstr_free(dsPtr);  \
	return result; \
} \
 \
static inline int bdstrlen(const big_dstring_t *dsPtr) \
{ \
	return dsPtr->length;  \
} \
\
static inline int bdstrcurmaxlen(const big_dstring_t *dsPtr) \
{ \
	return dsPtr->capacity - 1;  \
} \
\
static inline const char *bdstrval(const big_dstring_t *dsPtr) \
{ \
	return dsPtr->string; \
} \
\
static inline char *bdstrcat( big_dstring_t *dsPtr, \
                     const char *string, \
                     int length) \
{  \
	return dstrcat((dstring_t*)dsPtr, string,length);  \
}\
\
static inline void bdstr_trunc(big_dstring_t *dsPtr, int length) \
{ \
	dstr_trunc((dstring_t*)dsPtr, length); \
} \
\
static inline char *bdstr_set(big_dstring_t *dsPtr, const char *string) \
{ \
	return dstr_set((dstring_t*)dsPtr, string); \
} \
\
static inline char *bdstr_set_int(big_dstring_t *dsPtr, int64_t val) \
{ \
	return dstr_set_int((dstring_t*)dsPtr, val); \
}

#endif /* BIG_DSTRING_H */
