/*
 *  dynamic string macro template with arbitrary static size.
 * This file defines code, not just signatures.
 *  See dstring.h for explanation of behavior.
 *  @TODO stick license here
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
	assert(NULL != dsPtr); \
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
	strcpy(result,dsPtr->string); \
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
static const int bd_static_size=DSTRING_STATIC_SIZE

#endif /* BIG_DSTRING_H */
