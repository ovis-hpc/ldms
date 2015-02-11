/*
 *  Dynamic String Utilities
 */

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
	assert(NULL != dsPtr);
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
