/**
 * Copyright (c) 2015-2017 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2015-2017 Open Grid Computing, Inc. All rights reserved.
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
 *  Dynamic String Utilities
*  These routines are modifications of TCL code by John Osterhout at
 *  Berkeley, as allowed by the TCL distribution license.  See dstring.c
 *  for the details. There are minor differences internally.
 * */

#include "dstring.h"

#include <stdlib.h>
#include <assert.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

# define MAX(a,b) ( (a) < (b) ? (b) : (a) )
# define MIN(a,b) ( (a) < (b) ? (a) : (b) )

int dstrlen(const dstring_t * dsPtr)
{
	return dsPtr->length;
}

const char *dstrval(const dstring_t * dsPtr)
{
	return dsPtr->string;
}

void dstr_init(dstring_t * dsPtr)
{
	assert(NULL != dsPtr);
	dsPtr->string = dsPtr->staticSpace;
	dsPtr->length = 0;
	dsPtr->dead = 0;
	dsPtr->capacity = DSTRING_STATIC_SIZE;
	dsPtr->staticSpace[0] = '\0';
	/* protect against read through end of array at least until overwritten. */
	dsPtr->staticSpace[DSTRING_STATIC_SIZE - 1] = '\0';
}

void dstr_init2(dstring_t * dsPtr, int cap)
{
	dstr_init(dsPtr);
	char *newString;
	if (cap >= dsPtr->capacity) {
		dsPtr->capacity = cap;
		dsPtr->dead = 0;
		newString = (char *)malloc((size_t) dsPtr->capacity);
		if (!newString) {
			dsPtr->dead = 1;
			return;
		}
		newString[0] = '\0';
		dsPtr->string = newString;
	}
}

char *dstr_set(dstring_t * dsPtr, const char *string)
{
	size_t input_length;
	int length;
	char *newString = NULL;

	if (NULL == dsPtr || NULL == string) {
		return NULL;
	}
	input_length = strlen(string);
	if (INT_MAX < input_length) {
		string = "input_too_big";
	}
	length = (int)input_length;
	dsPtr->dead = 0;	/* set can revive a dead string if it fits in static space */

	/*
	 * Allocate a larger buffer for the string if the current one isn't
	 * large enough.  Allocate extra space in the new buffer so that there
	 * will be room to grow before we have to allocate again.
	 */
	if (length >= dsPtr->capacity) {
		int oldSize = dsPtr->capacity;
		if (length > INT_MAX / 2) {
			dsPtr->capacity = INT_MAX;
		} else {
			dsPtr->capacity = length * 2;
		}
		newString = malloc((size_t) dsPtr->capacity);
		if (!newString) {
			dsPtr->dead = 1;
			dsPtr->capacity = oldSize;
			return NULL;
		}
		if (dsPtr->string != dsPtr->staticSpace) {
			free(dsPtr->string);
		}
		dsPtr->string = newString;
	}

	/*
	 * Copy the new string into the buffer
	 */
	strncpy(dsPtr->string, string, input_length);
	dsPtr->length = length;
	dsPtr->string[dsPtr->length] = '\0';
	return dsPtr->string;
}

char *dstr_set_int(dstring_t * dsPtr, int64_t val)
{
	size_t input_length;
	int length;
	char *newString = NULL;
#define FMT_INT64_LEN 32
	char string[FMT_INT64_LEN];

	sprintf(string, "%" PRId64, val);
	input_length = strlen(string);
	length = (int)input_length;
	dsPtr->dead = 0;	/* set can revive a dead string if it fits in static space */

	/*
	 * Allocate a larger buffer for the string if the current one isn't
	 * large enough.  Allocate extra space in the new buffer so that there
	 * will be room to grow before we have to allocate again.
	 * For DSTRING_STATIC_SIZE > FMT_INT64_LEN this is unreachable.
	 * As DSTRING_STATIC_SIZE may evolve, leave the check in.
	 */
	if (length >= dsPtr->capacity) {
		int oldSize = dsPtr->capacity;
		if (length > INT_MAX / 2) {
			dsPtr->capacity = INT_MAX;
		} else {
			dsPtr->capacity = length * 2;
		}
		newString = malloc((size_t) dsPtr->capacity);
		if (!newString) {
			dsPtr->dead = 1;
			dsPtr->capacity = oldSize;
			return NULL;
		}
		if (dsPtr->string != dsPtr->staticSpace) {
			free(dsPtr->string);
		}
		dsPtr->string = newString;
	}

	strcpy(dsPtr->string, string);
	dsPtr->length = length;
	return dsPtr->string;
}

char *dstrcat_int(dstring_t * dsPtr, int64_t val)
{
	char string[FMT_INT64_LEN];

	sprintf(string, "%" PRId64, val);
	return dstrcat(dsPtr, string, DSTRING_ALL);

}

char *dstrcat_uint(dstring_t * dsPtr, uint64_t val)
{
	char string[FMT_INT64_LEN];

	sprintf(string, "%" PRIu64, val);
	return dstrcat(dsPtr, string, DSTRING_ALL);

}

char *dstrcat(dstring_t * dsPtr, const char *string, int length)
{
	size_t input_length = 0;
	int str_length = -1;
	char *newString = NULL;
	size_t newSize = 0;

	if (NULL == dsPtr || NULL == string || dsPtr->dead) {
		return NULL;
	}
	input_length = strlen(string);
	if (INT_MAX < input_length) {
		return NULL;
	}
	str_length = (int)input_length;

	if (length < 0) {	/* _ALL case */
		length = str_length;
	} else {
		length = MIN(length, str_length);
	}

	newSize = length + dsPtr->length;
	if (newSize > (INT_MAX - 1)) {
		return NULL;
	}

	/*
	 * Allocate a larger buffer for the string if the current one isn't
	 * large enough.  Allocate extra space in the new buffer so that there
	 * will be room to grow before we have to allocate again.
	 */
	if (newSize >= dsPtr->capacity) {
		int oldSize = dsPtr->capacity;
		if (newSize > INT_MAX / 2) {
			dsPtr->capacity = INT_MAX;
		} else {
			dsPtr->capacity = newSize * 2;
		}
		newString = malloc((size_t) dsPtr->capacity);
		if (!newString) {
			dsPtr->dead = 1;
			dsPtr->capacity = oldSize;
			return NULL;
		}
		strncpy(newString, dsPtr->string, dsPtr->length);
		if (dsPtr->string != dsPtr->staticSpace) {
			free(dsPtr->string);
		}
		dsPtr->string = newString;
	}

	/*
	 * Copy the new string into the buffer at the end of the old one.
	 */
	strncpy(dsPtr->string + dsPtr->length, string, (size_t) length);
	dsPtr->length += length;
	dsPtr->string[newSize] = '\0';
	return dsPtr->string;
}

void dstr_trunc(dstring_t * dsPtr, int length)
{
	assert(NULL != dsPtr);

	if (length < 0) {
		length = 0;
	}
	if (length < dsPtr->length) {
		dsPtr->length = length;
		dsPtr->string[length] = '\0';
	}
}

void dstr_free(dstring_t * dsPtr)
{
	if (!dsPtr)
		return;
	if (dsPtr->string != dsPtr->staticSpace) {
		free(dsPtr->string);
	}
	dsPtr->string = dsPtr->staticSpace;
	dsPtr->length = 0;
	dsPtr->dead = 0;
	dsPtr->capacity = DSTRING_STATIC_SIZE;
	dsPtr->staticSpace[0] = '\0';
}

char *dstr_extract(dstring_t * dsPtr)
{
	char *result;

	assert(NULL != dsPtr);
	result = (char *)malloc(strlen(dsPtr->string) + 1);
	if (result) {
		strcpy(result, dsPtr->string);
	}
	dstr_free(dsPtr);
	return result;
}
