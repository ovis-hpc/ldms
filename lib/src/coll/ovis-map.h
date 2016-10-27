/*
 * Copyright (c) 2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2016 Sandia Corporation. All rights reserved.
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
#ifndef ovis_map_h_seen
#define ovis_map_h_seen
/**
Ovis_map provides a growable, fast-lookup, string-keyed map optimized for
long string keys with variations in the suffix and pointer data.
No special memory allocation is used.
NULL values and keys cannot be stored in the map.
The map is not sorted alphabetically.

The following operations are locking:
	_destroy
	_visit
	_insert_fast
	_insert
	_insert_new
	_snapshot

The following operations are not locking:
	_size
	_find
 */
#include <stdint.h>
#include <stdlib.h>
struct ovis_map;

#ifdef OVIS_SYMBOL
/** Many things would be faster if these were used everywhere in LDMS API 
instead of raw strings. */
const struct ovis_symbol {
	const char *key;
	const uint64_t keyhash;
	const size_t keylen;
};
#endif

struct ovis_map_element {
	const char *key;
	uint64_t keyhash;
	void *value;
};

static const struct ovis_map_element OME_NULL = { NULL, 0, NULL };

/** Operate on map element with user supplied context.
	\param e the element being visited.
	\param user-supplied context.
 */
typedef void (*ovis_map_visitor)(struct ovis_map_element *e, void *user);

/** Initialize the ovis_map.
	\return errno value if failing.
 */
extern struct ovis_map *ovis_map_create();

/** Destroy the map.
	\param m to be destroyed.
	\param user's destroy function to call on each map element, or NULL 
		if user will handle element destruction elsewise.
	\param udata user context for destroy calls; may be NULL if not needed.
 */
extern void ovis_map_destroy(struct ovis_map *m, ovis_map_visitor destroy, void *udata);

/** Get count of elements in map. 
\param*/
extern size_t ovis_map_size(struct ovis_map *m);

/** Get hash of string or partial string.
\param key string at least keylen long.
\param keylen bytes to hash.
 */
extern uint64_t ovis_map_keyhash(const char *key, size_t keylen);

/** Apply the same function to all map elements. Map is locked
to prevent insertion or destruction during visit.
	\param m to be visited.
	\param user extra argument for visit function.
*/
extern void ovis_map_visit(struct ovis_map *m, ovis_map_visitor func, void *user);

/** Look up element/value by pre-hashed key.
If the return value is subsequently passed to ovis_map_insert_fast, the
caller should ensure the search key s given here will last as long as
element inserted.
	\param m the map.
	\param phk A ovis_map_element with only the key and keyhash set.
	\return a copy of the element with non-null .value if found, or the
element to pass to ovis_map_insert_fast after setting the .value field.
*/
extern struct ovis_map_element ovis_map_findhash(struct ovis_map *m, struct ovis_map_element phk );

/** Look up element by string key.
If the return value is subsequently passed to ovis_map_insert_fast, the
caller should ensure the search key s given here will last as long as
element inserted.
	\param m the map.
	\param s the key.
	\return a copy of the element with non-null .value if found, or the
element to pass to ovis_map_insert_fast after setting the .value field.
*/
extern struct ovis_map_element ovis_map_find(struct ovis_map *m, const char *s);

/** Insert an element after checking with find that it is not already there.
If multithreaded, the caller must have the map in a locked scope to
prevent races between find and insert.
	\param m the map.
	\param el output of the previous ovis_map_find call.
	\result 0 unless some errno value returned.
*/
extern int ovis_map_insert_fast(struct ovis_map *m, struct ovis_map_element el);

/** Insert a key/value pair. If key is a duplicate, insertion is rejected.
 Neither key nor value are duplicated. Caller manages key lifetime,
 which must exceed map lifetime.
	\param m the map.
	\param key string key.
	\param value object to store
	\result 0 unless some errno value returned.
*/
extern int ovis_map_insert(struct ovis_map *map, const char *key, void *value);

/** Insert a key/value that caller guarantees does not match any existing key.
 Neither key nor value are duplicated. Caller manages key/value lifetime,
 which must exceed map lifetime.
	\param m the map.
	\param key string key.
	\param value object to store
	\result 0 unless some errno value returned.
*/
extern int ovis_map_insert_new(struct ovis_map *map, const char *key, void *value);

/** Gathers up map content into a NULL-terminated snapshot array that can 
be safely used while map continues to grow in another thread.
If it is certain that ovis_map_size will not change, the result of this call 
can be reused since the map is grow-only. The map should not be deleted in 
another thread while the snapshot is in use.
	\param m map with the data to be gathered.
	\param snap array of pointers at least snap_len long to be filled.
	\param len the length of snap available.
	\return 0 if ok, or the bigger length recommended if snap_len too small,
		or -1 if invalid input detected.
*/
extern int64_t ovis_map_snapshot(struct ovis_map *m, struct ovis_map_element **snap, size_t len);

#endif /* ovis_map_h_seen */
