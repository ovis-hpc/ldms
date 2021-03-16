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

/** \file
 *  Dynamic String Utilities.
 *  These routines are modifications of TCL code by John Osterhout at
 *  Berkeley, as allowed by the TCL distribution license.  See dstring.c
 *  for the details. There are minor differences internally.
 *
 * This implementation is biased toward large strings, because when you
 * know you need only small strings, you can usually determine a good
 * upper bound by inspection and use a fixed buffer.
 *
 * When used with capacity/length under DSTRING_STATIC_SIZE
 * these dstrings will not leak even if you forget to free because
 * malloc will never be needed internally.
 * Of course any char* turned over to the caller may leak.
 *
 * Examples:
 * See test_dstring.c.
 *
 * Extensions:
 * A version with arbitrarily large static size is available
 * in big_dstring.h. It buys speed with a slight binary size increase.
 *
 * Error handling of malloc returning null:
 * When malloc fails, the string will ignore new input
 * and may fill itself with an error message instead.
 * The return of cat functions will be NULL.
 * The alternative is to exit, which is deemed somebody else's problem,
 * but could be easily changed for dstring.
 */

#ifndef DSTRING_H
#define DSTRING_H

/**	\addtogroup ovis_util_dstring General Dynamic String
	@{
*/
#define DSTRING_STATIC_SIZE 200
#define DSTRING_ALL -1
#include <stdint.h>

/** Dynamic string data structure. Max size INT_MAX. */
typedef struct dstring {
	char *string;
		/**< Points to beginning of string:  either
		staticSpace below or a malloc'ed array. */
	int length;
		/**< Number of non-NULL characters in the string. */
	int capacity;
		/**< Total number of bytes available for the
		string and its terminating NULL char. */
	int dead;
		/**< if !=0, malloc failed and we become unwritable. */
	char staticSpace[DSTRING_STATIC_SIZE];
		/**< Space to use in common case where string is small. */
} dstring_t;

/*----------------------------------------------------------------------*/
/**
 * Returns the current length of the string.
 * The return value is the number of non-NULL characters in
 *  the string, an integer.  dsPtr may not be NULL (not
 *  checked - crash probable).
 *
 * \param dsPtr dstring *, pointer to a structure
 *               describing the dynamic string to query.
 */
extern int dstrlen(const dstring_t * dsPtr);

/*----------------------------------------------------------------------*/
/**
 * Returns the current value of the string.
 * The return value is a pointer to the first character in the
 * string, a char*.  The client should not free or modify this
 * value unless they can prove they will not overrun the internal string.
 * dsPtr is never null if the string has been initialized.
 *
 * \param dsPtr dstring *, pointer to a structure
 *               describing the dynamic string to query.
 */
extern const char *dstrval(const dstring_t * dsPtr);

/*----------------------------------------------------------------------*/
/**
 *  Appends more characters to the specified dynamic string.
 *  Length bytes from string (or all of string if length is less
 *  than zero) are added to the current value of the string.  If
 *  string is shorter than length, only the length of string
 *  characters are appended.  The resulting value of the
 *  dynamic string is always null-terminated.
 *  <p />
 *  Memory	gets reallocated if needed to accomodate the string's
 *  new size.  Neither dpPtr nor string may be NULL (checked by
 *  assertion).
 *
 *  \param dsPtr  Structure describing dynamic string (non-NULL).
 *  \param string String to append (non-NULL).  If length is -1
 *                then this must be null-terminated.
 *  \param length Number of characters from string to append. If
 *                < 0, then append all of string, up to null at end.
 *  \return Returns the new value of the dynamic string, or null if
 *  malloc fails in the process.
 */
extern char *dstrcat(dstring_t * dsPtr, const char *string, int length);
/** append formatted int to dstring. */
extern char *dstrcat_int(dstring_t * dsPtr, int64_t);
/** append formatted unsigned to dstring. */
extern char *dstrcat_uint(dstring_t * dsPtr, uint64_t);

/*----------------------------------------------------------------------*/
/**
 *  Frees up any memory allocated for the dynamic string and
 *  reinitializes the string to an empty state.  The previous
 *  contents of the dynamic string are lost, and the new value
 *  is an empty string.  Note that dsPtr may not be NULL
 *  (checked by assertion). Ignore NULL input if assert is inactive.
 *
 *  \param dsPtr Structure describing dynamic string (non-NULL).
*/
extern void dstr_free(dstring_t * dsPtr);

/*----------------------------------------------------------------------*/
/**
 *  Initializes a dynamic string, dropping any previous contents
 *  of the string.  dstr_free() or extract should have been called already
 *  if the dynamic string was previously in use.  The dynamic string
 *  is initialized to be empty.  The passed pointer dsPtr may not
 *  be NULL (checked by assertion).
 *
 *  \param dsPtr Pointer to structure for dynamic string (non-NULL).
 */
extern void dstr_init(dstring_t * dsPtr);

/*----------------------------------------------------------------------*/
/**
 * Init a dstring with capacity cap. If cap is < DSTRING_STATIC_SIZE,
 * then equivalent to dstr_init(dsPtr, DSTRING_STATIC_SIZE);
 * Handy when you want to avoid growing and know you have a big string
 * to build.
 */
extern void dstr_init2(dstring_t * dsPtr, int cap);

/*----------------------------------------------------------------------*/
/**
 *  Returns a *copy* of the string content.
 *  The returned string is owned by the caller, who is responsible
 *  for free'ing it when done with it.  The dynamic string itself
 *  is reinitialized to an empty string.  dsPtr may not be NULL
 *  (checked by assertion).
 *
 *  \param dsPtr dsPtr Dynamic string holding the returned result.
 *  \return Returns a copy of the original value of the dynamic string, or
 *  NULL if malloc fails.
*/
extern char *dstr_extract(dstring_t * dsPtr);

/*----------------------------------------------------------------------*/
/**
 *  Truncates a dynamic string to a given length without freeing
 *  up its storage. 	The length of dsPtr is reduced to length
 *  unless it was already shorter than that.  Passing a length
 *  < 0 sets the new length to zero.  dsPtr may not be NULL
 *  (checked by assertion).
 *
 *  \param dsPtr  Structure describing dynamic string (non-NULL).
 *  \param length New maximum length for the dynamic string.
 */
extern void dstr_trunc(dstring_t * dsPtr, int length);

/*----------------------------------------------------------------------*/
/**
 *  Sets the value of the dynamic string to the specified string.
 *  String must be null-terminated.
 *  Memory gets reallocated if needed to accomodate the string's new
 *  size.  Neither dsPtr nor string may be NULL (checked by assertion).
 *
 *  \param dsPtr   Structure describing dynamic string (non-NULL).
 *  \param string  String to append (non-NULL, null-terminated).
 *  \return Returns the new value of the dynamic string, or null if malloc
 *  fails in the process.
 */
extern char *dstr_set(dstring_t * dsPtr, const char *string);

/*----------------------------------------------------------------------*/
/**
 *  Sets the value of the dynamic string to the string formatted int value.
 *  Memory gets reallocated if needed to accomodate the string's new
 *  size.  dsPtr may not be NULL (checked by assertion).
 *
 *  \param dsPtr   Structure describing dynamic string (non-NULL).
 *  \param val  int to format and append.
 *  \return Returns the new value of the dynamic string, or null if malloc
 *  fails in the process.
 */
extern char *dstr_set_int(dstring_t * dsPtr, int64_t val);

/* C89 and c99 interpretations of 'extern inline' are contradictory at link time
and we must be portable to both for some time to come.
We don't want binary bloat from static inline, so when we don't trust the
optimization pass to get this right, we use the next two macros.
These also work for big_dstrings.
*/
/** Same as dstrlen but for performance junkies. */
#define DSTRLEN(x) (x)->length
/** Same as dstrval but for performance junkies. */
#define DSTRVAL(x) (x)->string

/** clients may define DSTRING_USE_SHORT before include if they want these
 * for handling stack declared dstrings as objects.
 */
#ifdef DSTRING_USE_SHORT
/* macros for convenience in typical use */
/* paste as needed in application code. */
/** Declare and init a dstring named ds */
#define dsinit(ds) \
	dstring_t ds; \
	dstr_init(&ds)

/** Declare and init an oversized dstring named ds with initial capacity cap.*/
#define dsinit2(ds,cap) \
	dstring_t ds; \
	dstr_init2(&ds,cap)

/** Append a dstring with  null terminated string char_star_x.*/
#define dscat(ds, char_star_x) \
	dstrcat(&ds, char_star_x, DSTRING_ALL)

/** create real string (char *) from ds and reset ds, freeing any internal memory allocated. returns a char* the caller must free later. */
#define dsdone(ds) \
	dstr_extract(&ds)

#define dslen(ds) DSTRLEN(&ds)
#define dsval(ds) DSTRVAL(&ds)

#endif

/* @} */

#endif /* DSTRING_H */

