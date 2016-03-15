/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2010 Sandia Corporation. All rights reserved.
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
#ifndef _LDMS_PRIVATE_H
#define _LDMS_PRIVATE_H
#include <sys/queue.h>
#include <ldms_xprt.h>
#include "ovis_util/os_util.h"

#define LDMS_GN_INCREMENT(_gn) do { \
	(_gn) = __cpu_to_le64(__le64_to_cpu((_gn)) + 1); \
} while (0)

typedef struct ldms_mdef_s {
	char *name;
	enum ldms_value_type type;
	uint32_t flags;	/* DATA/MDATA flag */
	uint32_t count; /* Number of elements in the array if this is of an array type */
	size_t meta_sz;
	size_t data_sz;
	STAILQ_ENTRY(ldms_mdef_s) entry;
} *ldms_mdef_t;

struct ldms_schema_s {
	char *name;
	int card;
	size_t meta_sz;
	size_t data_sz;
	STAILQ_HEAD(metric_list_head, ldms_mdef_s) metric_list;
	LIST_ENTRY(ldms_schema_s) entry;
};

LIST_HEAD(rbd_list, ldms_rbuf_desc);
struct ldms_set {
	unsigned long flags;
	struct ldms_set_hdr *meta;
	struct ldms_data_hdr *data;
	struct rbn rb_node;
	struct rbd_list local_rbd_list;
	struct rbd_list remote_rbd_list;
};

/* Convenience macro to roundup a value to a multiple of the _s parameter */
#define roundup(_v,_s) ((_v + (_s - 1)) & ~(_s - 1))

extern void __ldms_free_rbd(struct ldms_rbuf_desc *rbd);
extern void __ldms_rbd_xprt_release(struct ldms_rbuf_desc *rbd);
extern int __ldms_remote_lookup(ldms_t _x, const char *path,
				enum ldms_lookup_flags flags,
				ldms_lookup_cb_t cb, void *cb_arg);
extern int __ldms_remote_dir(ldms_t x, ldms_dir_cb_t cb, void *cb_arg, uint32_t flags);
extern int __ldms_remote_dir_cancel(ldms_t x);
extern int __ldms_create_set(const char *instance_name, const char *schema_name,
		      size_t meta_len, size_t data_len, size_t card,
		      ldms_set_t *s, uint32_t flags);
extern void __ldms_get_local_set_list_sz(int *set_count, int *set_list_len);
extern int __ldms_get_local_set_list(char *set_list, size_t set_list_len,
				     int *set_count, int *set_list_size);
extern void __ldms_dir_add_set(const char *set_name);
extern void __ldms_dir_del_set(const char *set_name);
extern int __ldms_for_all_sets(int (*cb)(struct ldms_set *, void *), void *arg);
extern size_t __ldms_xprt_max_msg(struct ldms_xprt *x);

extern uint32_t __ldms_set_size_get(struct ldms_set *s);
extern void __ldms_metric_size_get(const char *name, enum ldms_value_type t,
			uint32_t count, size_t *meta_sz, size_t *data_sz);

extern struct ldms_set *__ldms_find_local_set(const char *path);
extern struct ldms_set *__ldms_local_set_first(void);
extern struct ldms_set *__ldms_local_set_next(struct ldms_set *);

extern int __ldms_remote_update(ldms_t t, ldms_set_t s, ldms_update_cb_t cb, void *arg);
extern void __ldms_set_tree_lock();
extern void __ldms_set_tree_unlock();

static int __ldms_set_publish(struct ldms_set *set);
static int __ldms_set_unpublish(struct ldms_set *set);
extern int ldms_set_publish(ldms_set_t sd);
extern int ldms_set_unpublish(ldms_set_t sd);

#endif
