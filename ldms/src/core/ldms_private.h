/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010,2013-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2010,2013-2018 Open Grid Computing, Inc. All rights reserved.
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
#include <ref.h>
#include <pthread.h>
#include "ovis_util/os_util.h"

#define LDMS_GN_INCREMENT(_gn) do { \
	(_gn) = __cpu_to_le64(__le64_to_cpu((_gn)) + 1); \
} while (0)

typedef struct ldms_mdef_s {
	char *name;
	char *unit;
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
	int array_card;
	STAILQ_HEAD(metric_list_head, ldms_mdef_s) metric_list;
	LIST_ENTRY(ldms_schema_s) entry;
};

struct ldms_set_info_pair {
	char *key;
	char *value;
	LIST_ENTRY(ldms_set_info_pair) entry;
};
LIST_HEAD(ldms_set_info_list, ldms_set_info_pair);
LIST_HEAD(rbd_list, ldms_rbuf_desc);
struct ldms_set {
	struct ref_s ref;
	unsigned long flags;
	uint64_t set_id;	/* unique identifier for a set in this daemon */
	uint64_t del_time;	/* Unix timestamp when set was deleted */
	struct ldms_set_hdr *meta;
	struct ldms_data_hdr *data; /* points to current entry of data array */
	struct ldms_set_info_list local_info;
	struct ldms_set_info_list remote_info; /*set info from the lookup operation */
	struct rbn rb_node;	/* Indexed by instance name */
	struct rbn id_node;	/* Indexed by set_id */
	struct rbn del_node;	/* Indexed by timestamp */
	struct rbd_list local_rbd_list;
	struct rbd_list remote_rbd_list;
	pthread_mutex_t lock;
	int curr_idx;
	struct ldms_data_hdr *data_array;
};

/* Convenience macro to roundup a value to a multiple of the _s parameter */
#define roundup(_v,_s) ((_v + (_s - 1)) & ~(_s - 1))

extern int __ldms_xprt_push(ldms_set_t s, int push_flags);
extern struct ldms_rbuf_desc *___ldms_alloc_rbd(struct ldms_xprt *,
				struct ldms_set *s, enum ldms_rbd_type type,
				const char *name, const char *func, int line);
#define __ldms_alloc_rbd(_x_, _s_, _t_, _n_) ___ldms_alloc_rbd(_x_, _s_, _t_, _n_, __func__, __LINE__)
extern void ___ldms_free_rbd(struct ldms_rbuf_desc *rbd, const char *name, const char *func, int line);
#define __ldms_free_rbd(_r_, _n_) ___ldms_free_rbd(_r_, _n_, __func__, __LINE__)
extern void __ldms_free_rbd_no_lock(struct ldms_rbuf_desc *rbd, const char *name, const char *func, int line);
extern void __ldms_rbd_xprt_release(struct ldms_rbuf_desc *rbd);
extern int __ldms_remote_lookup(ldms_t _x, const char *path,
				enum ldms_lookup_flags flags,
				ldms_lookup_cb_t cb, void *cb_arg);
extern int __ldms_remote_dir(ldms_t x, ldms_dir_cb_t cb, void *cb_arg, uint32_t flags);
extern int __ldms_remote_dir_cancel(ldms_t x);
extern struct ldms_set *
__ldms_create_set(const char *instance_name, const char *schema_name,
		  size_t meta_len, size_t data_len, size_t card,
		  size_t array_card,
		  uint32_t flags);
extern void __ldms_dir_add_set(struct ldms_set *set);
extern void __ldms_dir_del_set(struct ldms_set *set);
extern void __ldms_dir_upd_set(struct ldms_set *set);
extern int __ldms_delete_remote_set(ldms_t _x, ldms_set_t s);

struct ldms_name_entry {
	LIST_ENTRY(ldms_name_entry) entry;
	char name[OVIS_FLEX];
};
LIST_HEAD(ldms_name_list, ldms_name_entry);

extern struct ldms_set *__ldms_set_by_id(uint64_t id);
extern int __ldms_get_local_set_list(struct ldms_name_list *head);
extern void __ldms_empty_name_list(struct ldms_name_list *name_list);

extern void __ldms_dir_update(ldms_set_t set, enum ldms_dir_type t);
extern size_t __ldms_format_set_meta_as_json(struct ldms_set *set,
					     int need_comma,
					     char *buf, size_t buf_size);
extern int __ldms_for_all_sets(int (*cb)(struct ldms_set *, void *), void *arg);

extern uint32_t __ldms_set_size_get(struct ldms_set *s);
extern void __ldms_metric_size_get(const char *name, const char *unit,
				   enum ldms_value_type t,
				   uint32_t count, size_t *meta_sz, size_t *data_sz);

extern struct ldms_set *__ldms_find_local_set(const char *path);
extern struct ldms_set *__ldms_local_set_first(void);
extern struct ldms_set *__ldms_local_set_next(struct ldms_set *);

extern int __ldms_remote_update(ldms_t t, ldms_set_t s, ldms_update_cb_t cb, void *arg);
extern void __ldms_set_tree_lock();
extern void __ldms_set_tree_unlock();

extern int __ldms_set_info_set(struct ldms_set_info_list *info,
				const char *key, const char *value);
void __ldms_set_info_unset(struct ldms_set_info_pair *pair);
extern void __ldms_set_info_delete(struct ldms_set_info_list *info);
extern struct ldms_set_info_pair *__ldms_set_info_find(struct ldms_set_info_list *info,
								const char *key);
static inline
struct ldms_data_hdr *__set_array_get(struct ldms_set *set, int idx)
{
	return ((void *)set->data_array) + idx * __le32_to_cpu(set->meta->data_sz);
}

static inline
struct ldms_data_hdr *__ldms_set_array_get(ldms_set_t s, int idx)
{
	return __set_array_get(s->set, idx);
}

struct ldms_context *__ldms_alloc_ctxt(struct ldms_xprt *x, size_t sz, ldms_context_type_t type, ...);
void __ldms_free_ctxt(struct ldms_xprt *x, struct ldms_context *ctxt);

#define __put_share_lookup_ref(rbd) do { \
		ref_put(&((rbd)->set->ref), "share_lookup"); \
		ref_put(&((rbd)->ref), "share_lookup"); \
	} while (0)

#endif
