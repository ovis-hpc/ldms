/*
 * Copyright (c) 2015-16 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2015-16 Sandia Corporation. All rights reserved.
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
#ifndef rmaninfo_h_seen
#define rmaninfo_h_seen
#include <time.h>
#include <stdint.h>
#include <ovis_util/util.h>
#include <pthread.h>
/*
Singleton(ish) manager of resource_info objects, so that plugins
can share metrics of global interest, e.g. to avoid redundant collection work.
Only one plugin needs to know how to update it, but all can read/update if they 
known the name.
*/

typedef void * resource_info_manager;

resource_info_manager create_resource_info_manager();

enum rim_task {
	rim_init, // tinfo is avl; setup info object.
	rim_update, // update data. tinfo is in self->updateData
	rim_final,  // clear the data. tinfo is in self->updateData
};

struct resource_info {
	char *name; // registered name
	char *type; // resource type name is attached to, e.g. node
	uint64_t generation; // number of updates made.
	union {
		int32_t i32;
		uint32_t u32;
		int64_t i64;
		uint64_t u64;
		float	f32;
		double	f64;
		char *str;
	} v; // would be nice to unify with ldms.h v_* union in v3
	void *data;
};

/*
 Callback signature for shared metric implementer.
 Task t indicates what callee should do:
 switch (t) :
 case rim_init:
	cast tinfo to struct attr_value_list pointer config_args,
	allocate, init, and set self->data.
	return 0 if ok, or an errno value if not.
 case rim_update:
	cast self->data to private type pointer, cast tinfo to
	struct timespec* and perform update if needed.
	Expect calls more frequently than desired sampling
	rate and reuse prior results accordingly.
	self->generation++ when value is reread from source.
	return 0 if ok, or an errno value if not.
 case rim_final:
	clear/deallocate self->data and expect no more calls
	unless a cal with rim_init happens.
	return 0.
*/
typedef int (*rim_updatef)(struct resource_info *self,
		enum rim_task t, void * tinfo);

int register_resource_info(resource_info_manager rim, 
	const char *resource_name,
	const char *type,
	struct attr_value_list *config_args,
	rim_updatef update,
	void *updateData);


/* releases manager memory. outstanding resource_info 
 references will still be valid */
void clear_resource_info_manager(resource_info_manager rim);

/* Fetch a counted reference to the resource named. call release_resource_info(ri) when done with it. May be cached and released when convenient.
*/
struct resource_info *get_resource_info(resource_info_manager rim, const char *resource_name);

/* releaase the counted ri */
void release_resource_info(struct resource_info *ri);

/* update ri->v (if needed). update calls will be serialized.  */
int update_resource_info(struct resource_info *ri);

#endif
