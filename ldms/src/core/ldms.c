/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010-2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2010-2019 Open Grid Computing, Inc. All rights reserved.
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
#define _GNU_SOURCE
#include <inttypes.h>
#include <sys/errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/user.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <limits.h>
#include <assert.h>
#include <mmalloc/mmalloc.h>
#include <pthread.h>
#include <asm/byteorder.h>
#include "ovis_util/os_util.h"
#include "ldms.h"
#include "ldms_xprt.h"
#include "ldms_heap.h"
#include "ldms_private.h"
#include "coll/rbt.h"

#define SET_DIR_PATH "/var/run/ldms"
static char *__set_dir = SET_DIR_PATH;
#define SET_DIR_LEN sizeof(SET_DIR_PATH)
static char __set_path[PATH_MAX];
static void __destroy_set(void *v);

static struct {
	pthread_rwlock_t default_authz_lock;
	uid_t default_authz_uid;
	gid_t default_authz_gid;
	mode_t default_authz_perm;
} __ldms_config;

const char *ldms_xprt_op_names[] = {
	"LOOKUP",
	"UPDATE",
	"PUBLISH",
	"SET_DELETE",
	"DIR_REQ",
	"DIR_REP",
	"SEND",
	"RECV",
};
static char *type_names[];

static struct ldms_digest_s null_digest;

/* This function is useful for displaying data structures stored in
 * mmap'd memory that on some platforms is not accessible to the
 * debugger.
 */
void __dbg_dump_addr(unsigned long *p, size_t size)
{
	while (size > 0) {
		printf("%ld %lx\n", *p, *p);
		p ++;
		size -= 8;
	}
}

char *_create_path(const char *set_name);
static ldms_mval_t __mval_to_get(struct ldms_set *s, int idx, ldms_mdesc_t *pd);

static inline struct ldms_record_type *
__rec_type(struct ldms_record_inst *rec_inst, ldms_mdesc_t *mdesc,
	   struct ldms_set_hdr **meta_out, struct ldms_data_hdr **data_out);

static int set_comparator(void *a, const void *b)
{
	char *x = a;
	char *y = (char *)b;

	return strcmp(x, y);
}

static struct rbt __set_tree = {
	.root = NULL,
	.comparator = set_comparator
};

static int id_comparator(void *a, const void *b)
{
	uint64_t _a = (uint64_t)a;
	uint64_t _b = (uint64_t)b;

	if (_a > _b)
		return 1;
	if (_a < _b)
		return -1;
	return 0;
}

static struct rbt __id_tree = {
	.root = NULL,
	.comparator = id_comparator
};

static pthread_mutex_t __set_tree_lock = PTHREAD_MUTEX_INITIALIZER;

static struct rbt __del_tree = {
	.root = NULL,
	.comparator = id_comparator
};

int ldms_set_count()
{
	return __set_tree.card;
}

int ldms_set_deleting_count()
{
	return __del_tree.card;
}

static pthread_mutex_t __del_tree_lock = PTHREAD_MUTEX_INITIALIZER;

void __ldms_gn_inc(struct ldms_set *set, ldms_mdesc_t desc)
{
	if (desc->vd_flags & LDMS_MDESC_F_DATA) {
		LDMS_GN_INCREMENT(set->data->gn);
	} else {
		LDMS_GN_INCREMENT(set->meta->meta_gn);
		set->data->meta_gn = set->meta->meta_gn;
	}
}

/* Caller must hold the ldms set tree lock. */
struct ldms_set *__ldms_find_local_set(const char *set_name)
{
	struct rbn *z;
	struct ldms_set *s = NULL;

	z = rbt_find(&__set_tree, (void *)set_name);
	if (z) {
		s = container_of(z, struct ldms_set, rb_node);
		ref_get(&s->ref, __func__);
	}
	return s;
}

/* Caller must hold the ldms set tree lock */
struct ldms_set *__ldms_local_set_first()
{
	struct rbn *z;
	struct ldms_set *s = NULL;

	z = rbt_min(&__set_tree);
	if (z)
		s = container_of(z, struct ldms_set, rb_node);
	return s;
}

struct ldms_set *__ldms_local_set_next(struct ldms_set *s)
{
	struct rbn *z;
	z = rbn_succ(&s->rb_node);
	if (z)
		return container_of(z, struct ldms_set, rb_node);
	return NULL;
}

void __ldms_set_tree_lock()
{
	pthread_mutex_lock(&__set_tree_lock);
}

void __ldms_set_tree_unlock()
{
	pthread_mutex_unlock(&__set_tree_lock);
}

ldms_set_t ldms_set_by_name(const char *set_name)
{
	struct ldms_set *set;
	__ldms_set_tree_lock();
	set = __ldms_find_local_set(set_name);
	__ldms_set_tree_unlock();
	return set;
}

uint64_t ldms_set_meta_gn_get(ldms_set_t s)
{
	return __le64_to_cpu(s->meta->meta_gn);
}

uint64_t ldms_set_data_gn_get(ldms_set_t s)
{
	return __le64_to_cpu(s->data->gn);
}

uint64_t ldms_set_heap_gn_get(ldms_set_t s)
{
	if (!s->heap)
		return -1;
	return s->heap->data->gn;
}

size_t ldms_set_heap_size_get(ldms_set_t s)
{
	return s->data->heap.size;
}

void ldms_set_data_copy_set(ldms_set_t s, int on)
{
	if (on)
		s->flags |= LDMS_SET_F_DATA_COPY;
	else
		s->flags &= ~LDMS_SET_F_DATA_COPY;
}

struct cb_arg {
	void *user_arg;
	int (*user_cb)(struct ldms_set *, void *);
};

static int rbn_cb(struct rbn *rbn, void *arg, int level)
{
	struct cb_arg *cb_arg = arg;
	struct ldms_set *set = container_of(rbn, struct ldms_set, rb_node);
	if (set->flags & LDMS_SET_F_PUBLISHED)
		return cb_arg->user_cb(set, cb_arg->user_arg);
	return 0;
}

/* Caller must hold the ldms set tree lock */
int __ldms_for_all_sets(int (*cb)(struct ldms_set *, void *), void *arg)
{
	struct cb_arg user_arg = { arg, cb };
	int rc;
	rc = rbt_traverse(&__set_tree, rbn_cb, &user_arg);
	return rc;
}


struct get_set_names_arg {
	struct ldms_name_list *name_list;
};

/*
 * { "directory" : [
 *   { "name" : <string>,
 *     "schema" : <string>,
 *     "meta-size" : <int>,
 *     "data-size" : <int>,
 *     "uid" : <int>,
 *     "gid" : <int>,
 *     "info" : [
 *       { "name" : <string>, "value" : <string> },
 *       . . .
 *     ]
 *   },
 *   . . .
 *   ]
 * }
 */


#define STATE_BUF_SZ 4
#define PERM_BUF_SZ 16
size_t __ldms_format_set_meta_as_json(struct ldms_set *set,
				      int need_comma,
				      char *buf, size_t buf_size)
{
	size_t cnt;
	char dbuf[2*LDMS_DIGEST_LENGTH+1];
	ldms_digest_t digest = ldms_set_digest_get(set);

	/* format perm */
	uint32_t perm = __le32_to_cpu(set->meta->perm);
	char perm_str[PERM_BUF_SZ];
	char *s = perm_str;
	int i;
	*s = '-';
	s++;
	for (i = 6; i >= 0; i -= 3) {
		uint32_t mask = perm >> i;
		if (mask & 4)
			*s = 'r';
		else
			*s = '-';
		s++;
		if (mask & 2)
			*s = 'w';
		else
			*s = '-';
		s++;
		if (mask & 1)
			*s = 'x';
		else
			*s = '-';
		s++;
	}
	*s = '\0';

	/* format state flags */
	char state[STATE_BUF_SZ];
	if (set->data->trans.flags == LDMS_TRANSACTION_END)
		state[0] = 'C';
	else
		state[0] = ' ';
	if (set->flags & LDMS_SET_F_LOCAL)
		state[1] = 'L';
	else if (set->flags & LDMS_SET_F_REMOTE)
		state[1] = 'R';
	else
		state[1] = ' ';
	if (set->flags & LDMS_SET_F_PUSH_CHANGE)
		state[2] = 'P';
	else
		state[2] = ' ';
	state[3] = '\0';

	cnt = snprintf(buf, buf_size,
		       "%c{"
		       "\"name\":\"%s\","
		       "\"schema\":\"%s\","
		       "\"digest\":\"%s\","
		       "\"flags\":\"%s\","
		       "\"meta_size\":%d,"
		       "\"data_size\":%d,"
		       "\"heap_size\":%d,"
		       "\"uid\":%d,"
		       "\"gid\":%d,"
		       "\"perm\":\"%s\","
		       "\"card\":%d,"
		       "\"array_card\":%d,"
		       "\"meta_gn\":%" PRIu64 ","
		       "\"data_gn\":%" PRIu64 ","
		       "\"timestamp\":{\"sec\":%d,\"usec\":%d},"
		       "\"duration\":{\"sec\":%d,\"usec\":%d},"
		       "\"info\":[",
		       need_comma ? ',' : ' ',
		       get_instance_name(set->meta)->name,
		       get_schema_name(set->meta)->name,
		       digest ? ldms_digest_str(digest, dbuf, sizeof(dbuf)) : "",
		       state,
		       __le32_to_cpu(set->meta->meta_sz),
		       __le32_to_cpu(set->meta->data_sz),
		       __le32_to_cpu(set->meta->heap_sz),
		       __le32_to_cpu(set->meta->uid),
		       __le32_to_cpu(set->meta->gid),
		       perm_str,
		       __le32_to_cpu(set->meta->card),
		       __le32_to_cpu(set->meta->array_card),
		       (uint64_t)__le64_to_cpu(set->meta->meta_gn),
		       (uint64_t)__le64_to_cpu(set->data->gn),
		       __le32_to_cpu(set->data->trans.ts.sec), __le32_to_cpu(set->data->trans.ts.usec),
		       __le32_to_cpu(set->data->trans.dur.sec), __le32_to_cpu(set->data->trans.dur.usec)
		       );
	if (cnt >= buf_size)
		goto out;
	struct ldms_set_info_pair *info, *linfo;
	int comma = 0;
	LIST_FOREACH(info, &set->local_info, entry) {
		if (comma)
			cnt += snprintf(&buf[cnt], buf_size - cnt, ",");
		else
			comma = 1;
		if (cnt >= buf_size)
			goto out;
		cnt += snprintf(&buf[cnt], buf_size - cnt,
				"{\"key\":\"%s\",\"value\":\"%s\"}",
				info->key, info->value);
		if (cnt >= buf_size)
			goto out;
	}
	LIST_FOREACH(info, &set->remote_info, entry) {
		/* Print remote info that is not overriden by local info */
		linfo = __ldms_set_info_find(&set->local_info, info->key);
		if (linfo)
			continue;
		if (comma)
			cnt += snprintf(&buf[cnt], buf_size - cnt, ",");
		else
			comma = 1;
		if (cnt >= buf_size)
			goto out;
		cnt += snprintf(&buf[cnt], buf_size - cnt,
				"{\"key\":\"%s\",\"value\":\"%s\"}",
				info->key, info->value);
		if (cnt >= buf_size)
			goto out;
	}
	if (cnt >= buf_size)
		return cnt;
	cnt += snprintf(&buf[cnt], buf_size - cnt, "]}");
 out:
	return cnt;
}

static int get_set_list_cb(struct ldms_set *set, void *arg)
{
	struct get_set_names_arg *a = arg;
	struct ldms_name_entry *entry;
	ldms_name_t name = get_instance_name(set->meta);

	entry = malloc(sizeof(*entry) + name->len);
	if (!entry)
		return ENOMEM;
	memcpy(entry->name, name->name, name->len);
	LIST_INSERT_HEAD(a->name_list, entry, entry);
	return 0;
}

void __ldms_empty_name_list(struct ldms_name_list *name_list)
{
	while (!LIST_EMPTY(name_list)) {
		struct ldms_name_entry *e = LIST_FIRST(name_list);
		LIST_REMOVE(e, entry);
		free(e);
	}
}

/**
 * \brief Return a list of all of the local set names.
 *
 * This function is called with the set tree lock held. The
 * ldms_name_list returned must be released by the caller using the
 * __ldms_empty_name_list() function.
 */
int __ldms_get_local_set_list(struct ldms_name_list *head)
{
	struct get_set_names_arg arg;
	int rc;

	LIST_INIT(head);
	arg.name_list = head;

	rc = __ldms_for_all_sets(get_set_list_cb, &arg);
	if (rc)
		__ldms_empty_name_list(arg.name_list);

	return rc;
}

uint64_t __next_set_id = 1;

/* The caller must NOT hold the ldms set tree lock. */
static struct ldms_set *
__record_set(const char *instance_name,
	     struct ldms_set_hdr *sh, struct ldms_data_hdr *dh, int flags)
{
	struct ldms_set *set;
	zap_err_t zerr;
	size_t sz;

	__ldms_set_tree_lock();
	set = __ldms_find_local_set(instance_name);
	__ldms_set_tree_unlock();
	if (set) {
		ref_put(&set->ref, "__ldms_find_local_set");
		errno = EEXIST;
		return NULL;
	}

	set = calloc(1, sizeof *set);
	if (!set) {
		errno = ENOMEM;
		goto null;
	}
	LIST_INIT(&set->local_info);
	LIST_INIT(&set->remote_info);
	rbt_init(&set->push_coll, rbn_ptr_cmp);
	rbt_init(&set->lookup_coll, rbn_ptr_cmp);
	pthread_mutex_init(&set->lock, NULL);
	set->curr_idx = __le32_to_cpu(sh->array_card) - 1;
	set->set_id = __sync_fetch_and_add(&__next_set_id, 1);
	set->meta = sh;
	set->data_array = dh;
	set->data = __set_array_get(set, set->curr_idx);
	set->flags = flags;

	sz = __ldms_set_size_get(set);
	zerr = zap_map(&set->lmap, sh, sz, ZAP_ACCESS_READ | ZAP_ACCESS_WRITE);
	if (zerr) {
		errno = ENOMEM;
		goto free_set;
	}

	ref_init(&set->ref, __func__, __destroy_set, set);
	rbn_init(&set->rb_node, get_instance_name(set->meta)->name);
	rbn_init(&set->id_node, (void *)set->set_id);

	__ldms_set_tree_lock();
	/* Check if we lost a race creating this same set name */
	struct ldms_set *nset = __ldms_find_local_set(instance_name);
	if (nset) {
		ref_put(&nset->ref, "__ldms_find_local_set");
		errno = EEXIST;
		free(set);
		set = NULL;
		goto unlock_set_tree;
	}
	rbt_ins(&__set_tree, &set->rb_node);
	rbt_ins(&__id_tree, &set->id_node);

 unlock_set_tree:
	__ldms_set_tree_unlock();
	return set;

 free_set:
	free(set);
 null:
	return NULL;
}

/**
 * Callers must hold the set_tree lock
 */
extern struct ldms_set *__ldms_set_by_id(uint64_t id)
{
	struct ldms_set *set = NULL;
	struct rbn *rbn;
	rbn = rbt_find(&__id_tree, (void *)id);
	if (rbn)
		set = container_of(rbn, struct ldms_set, id_node);
	return set;
}

static
int __ldms_set_publish(struct ldms_set *set)
{
	if (set->flags & LDMS_SET_F_PUBLISHED)
		return EEXIST;

	set->flags |= LDMS_SET_F_PUBLISHED;
	__ldms_dir_add_set(set);
	return 0;
}

uint64_t ldms_set_id(struct ldms_set *set)
{
	if (set)
		return set->set_id;
	return 0;
}

int ldms_set_publish(struct ldms_set *set)
{
	int rc;
	ref_get(&set->ref, "publish");
	rc = __ldms_set_publish(set);
	ref_put(&set->ref, "publish");
	return rc;
}

/* Caller must hold the ldms set tree lock */
static
int __ldms_set_unpublish(struct ldms_set *set)
{
	if (!(set->flags & LDMS_SET_F_PUBLISHED))
		return ENOENT;

	set->flags &= ~LDMS_SET_F_PUBLISHED;
	return 0;
}

int ldms_set_unpublish(struct ldms_set *set)
{
	int rc;
	ref_get(&set->ref, "unpublish");
	rc = __ldms_set_unpublish(set);
	ref_put(&set->ref, "unpublish");
	return rc;
}

#define META_FILE 0
#define DATA_FILE 1

int __open_set_file(const char *name, int type, int creat)
{
	int fd;
	int flags = O_RDWR | (creat?O_CREAT:0);

	if (type == META_FILE)
		snprintf(__set_path, sizeof(__set_path), "%s/%s.META", __set_dir, name);
	else
		snprintf(__set_path, sizeof(__set_path), "%s/%s", __set_dir, name);
	fd = open(__set_path, flags, 0644);
	return fd;
}

void *_open_and_map_file(const char *path, int type, int create, size_t *size)
{
	int fd, rc;
	struct stat stat;
	int data = 0;
	void *p = NULL;

	fd = __open_set_file(path, type, create);
	if (fd < 0) {
		errno = ENOENT;
		goto err_0;
	}
	if (create) {
		lseek(fd, *size, SEEK_SET);
		rc = write(fd, &data, sizeof(int));
		if (rc < sizeof(int)) {
			rc = errno;
			goto err_1;
		}
	} else {
		rc = fstat(fd, &stat);
		if (rc) {
			rc = errno;
			goto err_1;
		}
		*size = stat.st_size;
	}
	p = mmap(NULL, *size, PROT_WRITE | PROT_READ,
		 MAP_FILE | MAP_SHARED, fd, 0);
	if (p == MAP_FAILED) {
		rc = errno;
		p = NULL;
	}
 err_1:
	close(fd);
	if (rc)
		/* close clears the errno */
		errno = rc;
 err_0:
	return p;
}

void ldms_xprt_stats(ldms_t _x, ldms_xprt_stats_t stats)
{
	*stats = _x->stats;
}

static void sync_update_cb(ldms_t x, ldms_set_t s, int status, void *arg)
{
	ldms_set_t *ps = arg;
	x->sem_rc = status;
	if (ps)
		*ps = s;
	sem_post(&x->sem);
}

int ldms_xprt_update(struct ldms_set *set, ldms_update_cb_t cb, void *arg)
{
	ldms_t xprt = ldms_xprt_get(set->xprt);
	int rc;

	assert(set);

	if (0 == (set->flags & LDMS_SET_F_REMOTE)) {
		if (cb)
			cb(set->xprt, set, 0, arg);
		return 0;
	}

	if (!xprt)
		return EINVAL;

	pthread_mutex_lock(&xprt->lock);
	if (!cb) {
		rc = __ldms_remote_update(xprt, set, sync_update_cb, arg);
		pthread_mutex_unlock(&xprt->lock);
		if (rc) {
			ldms_xprt_put(xprt);
			return rc;
		}
		sem_wait(&xprt->sem);
		rc = xprt->sem_rc;
	} else {
		rc = __ldms_remote_update(xprt, set, cb, arg);
		pthread_mutex_unlock(&xprt->lock);
	}
	ldms_xprt_put(xprt);
	return rc;
}

void __ldms_set_on_xprt_term(ldms_set_t set, ldms_t xprt)
{
	struct rbn *rbn;
	struct ldms_push_peer *pp = NULL;
	struct ldms_lookup_peer *np = NULL;
	pthread_mutex_lock(&set->lock);
	rbn = rbt_find(&set->push_coll, xprt);
	if (rbn) {
		rbt_del(&set->push_coll, rbn);
		pp = container_of(rbn, struct ldms_push_peer, rbn);
	}
	rbn = rbt_find(&set->lookup_coll, xprt);
	if (rbn) {
		rbt_del(&set->lookup_coll, rbn);
		np = container_of(rbn, struct ldms_lookup_peer, rbn);
	}
	pthread_mutex_unlock(&set->lock);
	if (pp) {
		ldms_xprt_put(pp->xprt);
		free(pp);
	}
	if (np) {
		ldms_xprt_put(np->xprt);
		free(np);
	}
}

void __ldms_set_info_delete(struct ldms_set_info_list *info)
{
	struct ldms_set_info_pair *pair;
	while (!LIST_EMPTY(info)) {
		pair = LIST_FIRST(info);
		LIST_REMOVE(pair, entry);
		free(pair->key);
		free(pair->value);
		free(pair);
	}
}

static void __destroy_set_no_lock(void *v)
{
	struct ldms_set *set = v;
	struct ldms_xprt *x = set->xprt;
	struct ldms_context *ctxt;
	if (x) {
		/*
		 * Check if there any transports referencing this set
		 * and if so, remove the reference
		 */
		pthread_mutex_lock(&x->lock);
		TAILQ_FOREACH(ctxt, &x->ctxt_list, link) {
			switch (ctxt->type) {
			case LDMS_CONTEXT_LOOKUP_READ:
				if (ctxt->lu_read.s == set)
					ctxt->lu_read.s = NULL;
				break;
			case LDMS_CONTEXT_UPDATE:
			case LDMS_CONTEXT_UPDATE_META:
				if (ctxt->update.s == set)
					ctxt->update.s = NULL;
				break;
			case LDMS_CONTEXT_REQ_NOTIFY:
				if (ctxt->req_notify.s == set)
					ctxt->req_notify.s = NULL;
				break;
			case LDMS_CONTEXT_SET_DELETE:
				if (ctxt->set_delete.s == set)
					ctxt->set_delete.s = NULL;
				break;
			default:
				break;
			}
		}
		pthread_mutex_unlock(&x->lock);
	}

	rbt_del(&__del_tree, &set->del_node);
	mm_free(set->meta);
	__ldms_set_info_delete(&set->local_info);
	__ldms_set_info_delete(&set->remote_info);
	zap_unmap(set->lmap);
	if (set->rmap)
		zap_unmap(set->rmap);
	free(set);
}

static void __destroy_set(void *v)
{
	pthread_mutex_lock(&__del_tree_lock);
	__destroy_set_no_lock(v);
	pthread_mutex_unlock(&__del_tree_lock);
}

void ldms_set_delete(ldms_set_t s)
{
	ldms_t x;
	struct ldms_set *__set;

	__ldms_set_tree_lock();
	__set = __ldms_set_by_id(s->set_id);
	if (!__set) {
		__ldms_set_tree_unlock();
		/*
		 * This is impossible since RBDs are removed.
		 */
		assert(0);
		return;
	}
	rbt_del(&__set_tree, &s->rb_node);
	rbt_del(&__id_tree, &s->id_node);
	__ldms_set_tree_unlock();

	/* NOTE: We will clean up the push and lookup collections
	 *       when we destroy the set. While we wait for the
	 *       SET_DELETE replies from the peers, we iterate
	 *       through the collections to check if there are any peers
	 *       that are connected or not.
	 *
	 *       If no peers are connected, the set will be destroyed
	 *       regardless of its reference count. See delete_proc().
	 */
	pthread_mutex_lock(&s->lock);
	x = s->xprt;
	s->xprt = NULL;
	pthread_mutex_unlock(&s->lock);
	if (x)
		ldms_xprt_put(x);

	/* Notify downstream transports about the set deletion. */
	__ldms_dir_del_set(s);

	/* Add the set to the delete tree with the current timestamp */
	s->del_time = time(NULL);
	rbn_init(&s->del_node, &s->del_time);
	pthread_mutex_lock(&__del_tree_lock);
	rbt_ins(&__del_tree, &s->del_node);
	pthread_mutex_unlock(&__del_tree_lock);

	/* Drop the creation reference */
	ref_put(&s->ref, "__record_set");
}

void ldms_set_put(ldms_set_t s)
{
	if (!s)
		return;
	ref_put(&s->ref, "__ldms_find_local_set");
}

static  void sync_lookup_cb(ldms_t x, enum ldms_lookup_status status, int more,
			    ldms_set_t s, void *arg)
{
	ldms_set_t *ps = arg;
	x->sem_rc = status;
	if (ps)
		*ps = s;
	sem_post(&x->sem);
}

int ldms_xprt_lookup(ldms_t x, const char *path, enum ldms_lookup_flags flags,
		     ldms_lookup_cb_t cb, void *cb_arg)
{
	int rc;
	if ((flags & !cb)
	    || strlen(path) > LDMS_LOOKUP_PATH_MAX)
		return EINVAL;
	if (!cb) {
		rc = __ldms_remote_lookup(x, path, flags, sync_lookup_cb, cb_arg);
		if (rc)
			return rc;
		sem_wait(&x->sem);
		rc = x->sem_rc;
	} else
		rc = __ldms_remote_lookup(x, path, flags, cb, cb_arg);
	return rc;
}

int ldms_xprt_dir(ldms_t x, ldms_dir_cb_t cb, void *cb_arg, uint32_t flags)
{
	return __ldms_remote_dir(x, cb, cb_arg, flags);
}

int ldms_xprt_dir_cancel(ldms_t x)
{
	return __ldms_remote_dir_cancel(x);
}

char *_create_path(const char *set_name)
{
	int tail, rc = 0;
	char *dirc = strdup(set_name);
	char *basec = strdup(set_name);
	if (!dirc || !basec)
		goto out;
	char *dname = dirname(dirc);
	char *bname = basename(basec);
	char *p;

	/* Create each node in the dir. __set_dir is presumed to exist */
	snprintf(__set_path, PATH_MAX, "%s/", __set_dir);
	tail = strlen(__set_path) - 1;
	for (p = strtok(dname, "/"); p; p = strtok(NULL, "/")) {
		/* remove duplicate '/'s */
		if (*p == '/')
			p++;
		if (*p == '\0') {
			rc = ENOENT;
			goto out;
		}
		tail += strlen(p);
		strcat(__set_path, p);
		rc = mkdir(__set_path, 0755);
		if (rc && errno != EEXIST)
			goto out;
		if (__set_path[tail] != '/') {
			__set_path[tail+1] = '/';
			tail++;
			__set_path[tail+1] = '\0';
		}
	}
	strcat(__set_path, bname);
	rc = 0;
 out:
	free(dirc);
	free(basec);
	if (rc)
		return NULL;
	return __set_path;
}

const char *ldms_set_instance_name_get(ldms_set_t s)
{
	ldms_name_t name = get_instance_name(s->meta);
	return name->name;
}

const char *ldms_set_producer_name_get(ldms_set_t s)
{
	return s->meta->producer_name;
}

int ldms_set_producer_name_set(ldms_set_t s, const char *name)
{
	if (LDMS_PRODUCER_NAME_MAX < strlen(name) + 1)
		return EINVAL;

	strncpy(s->meta->producer_name, name, LDMS_PRODUCER_NAME_MAX);
	return 0;
}

/* Caller must NOT hold the ldms set tree lock. */
struct ldms_set *__ldms_create_set(const char *instance_name,
				   const char *schema_name,
				   size_t meta_len, size_t data_heap_len,
				   size_t card,
				   size_t array_card,
				   uint32_t flags)
{
	int i;
	struct ldms_data_hdr *data, *data_base;
	struct ldms_set_hdr *meta;
	struct ldms_set *set = NULL;

	meta = mm_alloc(meta_len + (array_card * (data_heap_len)));
	if (!meta) {
		errno = ENOMEM;
		return NULL;
	}

	memset(meta, 0, meta_len + array_card * data_heap_len);
	LDMS_VERSION_SET(meta->version);
	meta->meta_sz = __cpu_to_le32(meta_len);
	meta->data_sz = __cpu_to_le32(data_heap_len);

	/* Initialize the metric set header */
	meta->meta_gn = __cpu_to_le64(1);
	meta->card = __cpu_to_le32(card);
	meta->array_card = __cpu_to_le32(array_card);
	meta->flags = LDMS_SETH_F_LCLBYTEORDER;

	ldms_name_t lname = get_instance_name(meta);
	lname->len = strlen(instance_name) + 1;
	strcpy(lname->name, instance_name);

	lname = get_schema_name(meta);
	lname->len = strlen(schema_name) + 1;
	strcpy(lname->name, schema_name);

	data_base = (struct ldms_data_hdr *)((uint8_t *)meta + meta_len);
	for (i = 0; i < array_card; i++) {
		data = (struct ldms_data_hdr *)
			((uint8_t *)data_base + (i * (data_heap_len)));
		data->size = __cpu_to_le64(data_heap_len);
		data->curr_idx = __cpu_to_le32(array_card - 1);
		data->gn = data->meta_gn = meta->meta_gn;
	}

	set = __record_set(instance_name, meta, data_base, flags);
	if (set)
		return set;

	mm_free(meta);
	return NULL;
}

uint32_t __ldms_set_size_get(struct ldms_set *s)
{
	return __le32_to_cpu(s->meta->meta_sz) +
		(__le32_to_cpu(s->meta->array_card) *
		 (__le32_to_cpu(s->meta->data_sz)));
}

#define LDMS_GRAIN_MMALLOC 1024

static int delete_thread_initialized = 0;
static pthread_t delete_thread;

void ldms_atfork()
{
	__atomic_store_n(&delete_thread_initialized, 0, __ATOMIC_SEQ_CST);
}

static void *delete_proc(void *arg);

int delete_thread_init_once()
{
	static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

	if (__atomic_load_n(&delete_thread_initialized, __ATOMIC_SEQ_CST))
		return 0;

	pthread_mutex_lock(&mutex);
	/* Check if we lose the race */
	if (__atomic_load_n(&delete_thread_initialized, __ATOMIC_SEQ_CST)) {
		pthread_mutex_unlock(&mutex);
		return 0;
	}

	int rc = pthread_create(&delete_thread, NULL, delete_proc, NULL);
	if (!rc) {
		__atomic_store_n(&delete_thread_initialized, 1, __ATOMIC_SEQ_CST);
		pthread_setname_np(delete_thread, "delete_thread");
		pthread_atfork(NULL, NULL, ldms_atfork);
	}
	pthread_mutex_unlock(&mutex);
	return rc;
}

int ldms_init(size_t max_size)
{
	size_t grain = LDMS_GRAIN_MMALLOC;
	int rc = mm_init(max_size, grain); /* mm_init() returns errno */
	if (rc)
		return rc;

	__ldms_config.default_authz_uid = geteuid();
	__ldms_config.default_authz_gid = getegid();
	__ldms_config.default_authz_perm = 0440;
	pthread_rwlock_init(&__ldms_config.default_authz_lock, NULL);
	return delete_thread_init_once();
}

ldms_schema_t ldms_schema_new(const char *schema_name)
{
	int evp_success;
	ldms_schema_t s = calloc(1, sizeof *s);
	if (!s)
		goto err_0;
	s->name = strdup(schema_name);
	if (!s->name)
		goto err_1;
	s->evp_ctx = EVP_MD_CTX_create();
	if (!s->evp_ctx)
		goto err_2;
	evp_success = EVP_DigestInit_ex(s->evp_ctx, EVP_sha256(), NULL);
	if (!evp_success)
		goto err_3;
	s->meta_sz = sizeof(struct ldms_set_hdr);
	s->data_sz = sizeof(struct ldms_data_hdr);
	s->array_card = 1;
	STAILQ_INIT(&s->metric_list);
	return s;
 err_3:
	EVP_MD_CTX_destroy(s->evp_ctx);
 err_2:
	free(s->name);
 err_1:
	free(s);
 err_0:
	return NULL;
}

const char *ldms_digest_str(ldms_digest_t digest, char *buf, int buf_len)
{
	char *s;
	int i;

	if (buf_len < 2*LDMS_DIGEST_LENGTH + 1) {
		errno = ENOBUFS;
		return NULL;
	}
	s = buf;
	for (i = 0; i < LDMS_DIGEST_LENGTH; i++) {
		sprintf(s, "%02X", digest->digest[i]);
		s += 2;
	}
	return buf;
}

static unsigned char hex_int[] = {
	['0'] = 0,
	['1'] = 1,
	['2'] = 2,
	['3'] = 3,
	['4'] = 4,
	['5'] = 5,
	['6'] = 6,
	['7'] = 7,
	['8'] = 8,
	['9'] = 9,

	['A'] = 10,
	['a'] = 10,
	['B'] = 11,
	['b'] = 11,
	['C'] = 12,
	['c'] = 12,
	['D'] = 13,
	['d'] = 13,
	['E'] = 14,
	['e'] = 14,
	['F'] = 15,
	['f'] = 15,

	[255] = 0,
};

static unsigned char hex_valid[] = {
	['0'] = 1,
	['1'] = 1,
	['2'] = 1,
	['3'] = 1,
	['4'] = 1,
	['5'] = 1,
	['6'] = 1,
	['7'] = 1,
	['8'] = 1,
	['9'] = 1,

	['A'] = 1,
	['a'] = 1,
	['B'] = 1,
	['b'] = 1,
	['C'] = 1,
	['c'] = 1,
	['D'] = 1,
	['d'] = 1,
	['E'] = 1,
	['e'] = 1,
	['F'] = 1,
	['f'] = 1,

	[255] = 0,
};

int ldms_str_digest(const char *str, ldms_digest_t digest)
{
	int len = strlen(str);
	unsigned char *d = digest->digest;
	const unsigned char *c;
	if (len != 2*sizeof(digest->digest))
		return EINVAL;
	for (c = (unsigned char*)str; *c; c += 2) {
		if (!hex_valid[*c] || !hex_valid[*(c+1)])
			return EINVAL;
		*d = (hex_int[*c]<<4) | (hex_int[*(c+1)]);
		d += 1;
	}
	return 0;
}

int ldms_schema_fprint(ldms_schema_t schema, FILE *fp)
{
	ldms_mdef_t m;
	int comma = 0;
	char buf[2*LDMS_DIGEST_LENGTH+1];
	fprintf(fp, "{ \"name\" : \"%s\",\n", schema->name);
	fprintf(fp, "  \"digest\" : \"%s\",\n", ldms_digest_str(&schema->digest, buf, sizeof(buf)));
	fprintf(fp, "  \"attrs\" : [\n");
	STAILQ_FOREACH(m, &schema->metric_list, entry) {
		if (comma) {
			fprintf(fp, ",\n");
		} else {
			comma = 1;
			fprintf(fp, "\n");
		}
		fprintf(fp,
		    "    {\n");
		fprintf(fp,
		    "      \"name\" : \"%s\",\n", m->name);
		fprintf(fp,
		    "      \"type\" : %d,\n", m->type);
		fprintf(fp,
		    "      \"type_name\" : \"%s\",\n", type_names[m->type]);
		fprintf(fp,
		    "      \"flags\" : %d,\n", m->flags);
		fprintf(fp,
		    "      \"count\" : %d,\n", m->count);
		fprintf(fp,
		    "      \"meta_sz\" : %zu,\n", m->meta_sz);
		fprintf(fp,
		    "      \"data_sz\" : %zu\n", m->data_sz);
		fprintf(fp,
		    "    }\n");
	}
	fprintf(fp, "  ]\n");
	fprintf(fp, "}\n");
	return 0;
}

void ldms_record_delete(ldms_record_t rec_def)
{
	ldms_mdef_t m;
	if (!rec_def)
		return;
	if (rec_def->metric_id >= 0)
		return; /* This already belonged to a schema */
	while ((m = STAILQ_FIRST(&rec_def->rec_metric_list))) {
		STAILQ_REMOVE_HEAD(&rec_def->rec_metric_list, entry);
		free(m->name);
		free(m->unit);
		free(m);
	}
	free(rec_def->mdef.name);
	free(rec_def->mdef.unit);
	free(rec_def);
}

void ldms_schema_delete(ldms_schema_t schema)
{
	ldms_mdef_t m;

	if (!schema)
		return;

	while (!STAILQ_EMPTY(&schema->metric_list)) {
		m = STAILQ_FIRST(&schema->metric_list);
		STAILQ_REMOVE_HEAD(&schema->metric_list, entry);
		if (m->type == LDMS_V_RECORD_TYPE) {
			ldms_record_delete((ldms_record_t)m);
			continue;
		}
		free(m->name);
		free(m->unit);
		free(m);
	}
	free(schema->name);
	if (schema->evp_ctx) {
		EVP_MD_CTX_destroy(schema->evp_ctx);
	}
	free(schema);
}

int ldms_schema_metric_count_get(ldms_schema_t schema)
{
	return schema->card;
}

int ldms_schema_array_card_set(ldms_schema_t schema, int card)
{
	if (card < 0)
		return EINVAL;
	schema->array_card = card;
	return 0;
}

size_t __ldms_value_size_get(enum ldms_value_type t, uint32_t count)
{
	size_t vsz;
	switch (t) {
	case LDMS_V_U8:
	case LDMS_V_S8:
	case LDMS_V_CHAR:
		vsz = sizeof(uint8_t);
		break;
	case LDMS_V_U8_ARRAY:
	case LDMS_V_S8_ARRAY:
	case LDMS_V_CHAR_ARRAY:
		vsz = sizeof(uint8_t) * count;
		break;
	case LDMS_V_U16:
	case LDMS_V_S16:
		vsz = sizeof(uint16_t);
		break;
	case LDMS_V_U16_ARRAY:
	case LDMS_V_S16_ARRAY:
		vsz = sizeof(uint16_t) * count;
		break;
	case LDMS_V_U32:
	case LDMS_V_S32:
		vsz = sizeof(uint32_t);
		break;
	case LDMS_V_U32_ARRAY:
	case LDMS_V_S32_ARRAY:
		vsz = sizeof(uint32_t) * count;
		break;
	case LDMS_V_U64:
	case LDMS_V_S64:
		vsz = sizeof(uint64_t);
		break;
	case LDMS_V_U64_ARRAY:
	case LDMS_V_S64_ARRAY:
		vsz = sizeof(uint64_t) * count;
		break;
	case LDMS_V_F32:
		vsz = sizeof(float);
		break;
	case LDMS_V_F32_ARRAY:
		vsz = sizeof(float) * count;
		break;
	case LDMS_V_D64:
		vsz = sizeof(double);
		break;
	case LDMS_V_D64_ARRAY:
		vsz = sizeof(double) * count;
		break;
	case LDMS_V_LIST:
		vsz = sizeof(struct ldms_list);
		break;
	case LDMS_V_LIST_ENTRY:
		vsz = sizeof(struct ldms_list_entry);
		break;
	case LDMS_V_RECORD_TYPE:
		vsz = sizeof(struct ldms_record_type);
		break;
	case LDMS_V_RECORD_INST:
		vsz = sizeof(struct ldms_record_inst);
		break;
	case LDMS_V_RECORD_ARRAY:
		vsz = sizeof(struct ldms_record_array);
		break;
	default:
		assert(0 == "Unknown type");
		return 0;
	}
	/* Values are aligned on 8b boundary */
	return roundup(vsz, 8);
}

void __ldms_metric_size_get(const char *name, const char *unit,
			    enum ldms_value_type t,
			    uint32_t count, size_t *meta_sz, size_t *data_sz)
{
	/* Descriptors are aligned on eight byte boundaries */
	if (unit) {
		*meta_sz = roundup(sizeof(struct ldms_value_desc) +
				   strlen(name) + 1 + strlen(unit) + 1, 8);
	} else {
		*meta_sz = roundup(sizeof(struct ldms_value_desc) +
				   strlen(name) + 1, 8);
	}

	if (data_sz)
		*data_sz = __ldms_value_size_get(t, count);
}

void _ldms_set_ref_get(struct ldms_set *set, const char *name,
				   const char *func, int line)
{
	_ref_get(&set->ref, name, func, line);
}

int _ldms_set_ref_put(struct ldms_set *set, const char *name,
				  const char *func, int line)
{
	return _ref_put(&set->ref, name, func, line);
}

/* in: name, schema;
 * out: set_array_card, meta_sz, array_data_sz assigned.
 * \return 0 on error, or size of set if allocated from name, schema.
 * sets errno if error.
 */
static size_t compute_set_sizes(const char *instance_name, ldms_schema_t schema,
				int *set_array_card,
				size_t *meta_sz, size_t *array_data_sz, size_t *heap_sz)
{
	ldms_mdef_t md;
	size_t _hsz = 0;
	STAILQ_FOREACH(md, &schema->metric_list, entry) {
		if (md->type == LDMS_V_LIST) {
			if (md->count == 0)
				md->count = LDMS_LIST_HEAP;
			_hsz += md->count;
		}
	}
	if (*heap_sz < _hsz)
		*heap_sz = _hsz;
	*heap_sz = ldms_heap_size(*heap_sz);

	if (set_array_card) {
		*set_array_card = schema->array_card;
		if (*set_array_card < 0) {
			errno = EINVAL;
			return 0;
		}
		if (!*set_array_card)
			*set_array_card = 1;
	}

	*meta_sz = schema->meta_sz		/* header + metric dict */
		 + strlen(schema->name) + 2	/* schema name + '\0' + len */
		 + LDMS_DIGEST_LENGTH		/* digest for schema */
		 + strlen(instance_name) + 2;	/* instance name + '\0' + len */
	*meta_sz = roundup(*meta_sz, 8);
	assert(schema->data_sz == roundup(schema->data_sz, 8) ||
			(NULL == "bad schema.data_sz"));
	*array_data_sz = (schema->data_sz + *heap_sz) * *set_array_card;
	return *meta_sz + *array_data_sz;
}

void __make_mdesc(ldms_mdesc_t vd, ldms_mdef_t md, off_t *value_off)
{
	int name_len;
	int unit_len;
	vd->vd_type = md->type;
	vd->vd_flags = md->flags;
	vd->vd_array_count = __cpu_to_le32(md->count);
	name_len = strlen(md->name) + 1;
	strncpy(vd->vd_name_unit, md->name, name_len);
	if (md->unit) {
		unit_len = strlen(md->unit) + 1;
		strncpy(&vd->vd_name_unit[name_len], md->unit, unit_len);
	} else {
		unit_len = 0;
	}
	vd->vd_name_unit_len = name_len + unit_len;
	if (md->flags & LDMS_MDESC_F_DATA) {
		vd->vd_data_offset = __cpu_to_le32((*value_off));
		(*value_off) += __ldms_value_size_get(md->type, md->count);
	} else
		vd->vd_data_offset = 0; /* set after all metrics defined */

}

void __init_rec_array(ldms_set_t set, ldms_schema_t schema)
{
	ldms_mdef_t md;
	ldms_record_array_def_t ra;
	int idx, i, j;
	struct ldms_data_hdr *dh;
	ldms_mdesc_t ra_desc;
	ldms_record_array_t rec_array;
	ldms_record_inst_t rec_inst;

	idx = 0;
	STAILQ_FOREACH(md, &schema->metric_list, entry) {
		if (md->type != LDMS_V_RECORD_ARRAY)
			goto next;
		ra = (void*)md;
		ra_desc = ldms_ptr_(void, set->meta, __le32_to_cpu(set->meta->dict[idx]));
		assert(ra_desc->vd_type == LDMS_V_RECORD_ARRAY);

		/* for each set buffer */
		for (i = 0; i < schema->array_card; i++) {
			dh = __ldms_set_array_get(set, i);
			/* init the rec_array */
			rec_array = ldms_ptr_(void, dh, __le32_to_cpu(ra_desc->vd_data_offset));
			rec_array->array_len = __cpu_to_le32(ra->mdef.count);
			rec_array->inst_sz = __cpu_to_le32(ra->inst_sz);
			rec_array->rec_type = __cpu_to_le32(ra->rec_type);
			for (j = 0; j < ra->mdef.count; j++) {
				/* init each rec_inst in the array */
				rec_inst = ldms_ptr_(void, rec_array->data, j*ra->inst_sz);
				rec_inst->record_type = rec_array->rec_type;
				rec_inst->set_data_off = __cpu_to_le32(ldms_off_(dh, rec_inst));
				rec_inst->hdr.flags = LDMS_RECORD_F_INST;
			}
		}
	next:
		idx++;
	}
}

static void __ldms_schema_finalize(ldms_schema_t schema)
{
	if (memcmp(&schema->digest, &null_digest, LDMS_DIGEST_LENGTH))
		return;
	unsigned int len = LDMS_DIGEST_LENGTH;
	EVP_DigestFinal_ex(schema->evp_ctx, schema->digest.digest, &len);
}

ldms_set_t ldms_set_create(const char *instance_name,
				ldms_schema_t schema,
				uid_t uid, gid_t gid, mode_t perm,
				uint32_t heap_sz)
{
	struct ldms_data_hdr *data, *data_base;
	struct ldms_set_hdr *meta = NULL;
	struct ldms_value_desc *vd;
	size_t meta_sz = 0, array_data_sz = 0, hsz = 0;
	off_t value_off;
	ldms_mdef_t md;
	int metric_idx;
	int i;
	int set_array_card;
	struct ldms_set *set;

	if (delete_thread_init_once())
		return NULL;

	if (!instance_name || !schema) {
		errno = EINVAL;
		return NULL;
	}

	hsz = heap_sz;
	int ssz = compute_set_sizes(instance_name, schema,
				    &set_array_card, &meta_sz,
				    &array_data_sz, &hsz);
	if (!ssz)
		return NULL;

	__ldms_schema_finalize(schema);

	meta = mm_alloc(meta_sz + array_data_sz);
	if (!meta) {
		errno = ENOMEM;
		return NULL;
	}

	/* Initialize the metric set header (metadata part) */
	memset(meta, 0, meta_sz + array_data_sz);
	LDMS_VERSION_SET(meta->version);
	meta->card = __cpu_to_le32(schema->card);
	meta->meta_sz = __cpu_to_le32(meta_sz);
	meta->data_sz = __cpu_to_le32(schema->data_sz) +
			     __cpu_to_le32(hsz);
	meta->heap_sz = __cpu_to_le32(hsz);
	meta->meta_gn = __cpu_to_le64(1);
	meta->flags = LDMS_SETH_F_LCLBYTEORDER;
	meta->uid = __cpu_to_le32(uid);
	meta->gid = __cpu_to_le32(gid);
	meta->perm = __cpu_to_le32(perm);
	meta->array_card = __cpu_to_le32(set_array_card);

	/*
	 * Set the instance name.
	 * NB: Must be set first because get_schema_name uses the
	 * instance name len field.
	 */
	ldms_name_t lname = get_instance_name(meta);
	lname->len = strlen(instance_name) + 1;
	strcpy(lname->name, instance_name);

	/* Set the schema name. */
	lname = get_schema_name(meta);
	size_t schema_sz = strlen(schema->name) + 1;
	strcpy(lname->name, schema->name);
	unsigned char *digest =
		(unsigned char *)&lname->name[schema_sz];
	memcpy(digest, schema->digest.digest, LDMS_DIGEST_LENGTH);
	lname->len = schema_sz + LDMS_DIGEST_LENGTH + 1;

	/* set array element data hdr initialization */
	data_base = (struct ldms_data_hdr *)((void*)meta + meta_sz);
	for (i = 0; i < set_array_card; i++) {
		data = (struct ldms_data_hdr *)
			((uint8_t *)data_base + i * (schema->data_sz + hsz));
		data->size = __cpu_to_le64(schema->data_sz + hsz);
		data->curr_idx = __cpu_to_le32(set_array_card - 1);
		data->gn = data->meta_gn = meta->meta_gn;
		data->set_off = __cpu_to_le32(ldms_off_(meta, data));
		if (hsz) {
			void *hbase = &((uint8_t *)data)[schema->data_sz];
			ldms_heap_init(&data->heap, hbase,
				       hsz, LDMS_LIST_GRAIN);
		}
	}

	/* Add the metrics from the schema */
	vd = get_first_metric_desc(meta);
	/* value_off is the offset from the beginning of data header */
	value_off = roundup(sizeof(*data_base), 8);
	metric_idx = 0;
	size_t vd_size = 0;
	STAILQ_FOREACH(md, &schema->metric_list, entry) {
		/* Add descriptor to dictionary */
		meta->dict[metric_idx] = __cpu_to_le32(ldms_off_(meta, vd));

		/* Build the descriptor */
		__make_mdesc(vd, md, &value_off);

		/* Advance to next descriptor */
		metric_idx++;
		vd = (struct ldms_value_desc *)((char *)vd + md->meta_sz);
		vd_size += md->meta_sz;
	}

	/*
	 * Now that the end of all vd is known, assign the data offsets for the
	 * meta-attributes from the meta data area.
	 * value_off in metadata is the offset from the beginning of the
	 * metadata header.
	 */
	value_off = (uint64_t)((char *)vd - (char *)meta);
	value_off = roundup(value_off, 8);
	md = STAILQ_FIRST(&schema->metric_list);
	for (i = 0; i < schema->card; i++) {
		vd = ldms_ptr_(struct ldms_value_desc, meta, __le32_to_cpu(meta->dict[i]));
		if (vd->vd_flags & LDMS_MDESC_F_DATA)
			goto next;
		vd->vd_data_offset = __cpu_to_le32(value_off);
		if (vd->vd_type != LDMS_V_RECORD_TYPE) {
			value_off += __ldms_value_size_get(vd->vd_type,
					__le32_to_cpu(vd->vd_array_count));
			goto next;
		}
		/* Making LDMS_V_RECORD_TYPE */
		assert(vd->vd_type == LDMS_V_RECORD_TYPE);
		ldms_mdef_t rec_md; /* mdef in rec_type */
		ldms_mdesc_t rec_vd; /* value desc in rec_type */
		ldms_record_t rec_def = (void*)md;
		ldms_record_type_t rec_type = ldms_ptr_(void, meta, value_off);
		rec_type->hdr.flags = LDMS_RECORD_F_TYPE;
		int j;
		off_t rec_type_off = sizeof(*rec_type) + rec_def->n * sizeof(rec_type->dict[0]);
		off_t rec_inst_off = sizeof(struct ldms_record_inst);
		rec_md = STAILQ_FIRST(&rec_def->rec_metric_list);
		for (j = 0; j < rec_def->n; j++) {
			/* Make the value descriptor in the record type */
			rec_type->dict[j] = __cpu_to_le32(rec_type_off);
			rec_vd = ldms_ptr_(void, rec_type, rec_type_off);
			rec_vd->vd_user_data = 0x012345678abcdef;
			__make_mdesc(rec_vd, rec_md, &rec_inst_off);

			assert(sizeof(*rec_vd) + __le32_to_cpu(rec_vd->vd_name_unit_len) <= rec_md->meta_sz);
			rec_type_off += rec_md->meta_sz;
			rec_md = STAILQ_NEXT(rec_md, entry);
		}
		assert(rec_inst_off == rec_def->inst_sz);
		assert(rec_type_off == rec_def->mdef.data_sz);
		rec_type->n = __cpu_to_le32(rec_def->n);
		rec_type->inst_sz = __cpu_to_le32(rec_inst_off);
		value_off += rec_def->mdef.data_sz;
	next:
		md = STAILQ_NEXT(md, entry);
	}

	data_base = (void*)meta + le32toh(meta->meta_sz);
	set = __record_set(instance_name, meta, data_base, LDMS_SET_F_LOCAL);
	if (!set) {
		mm_free(meta);
		return NULL;
	}
	if (meta->heap_sz) {
		set->heap = ldms_heap_get(&set->heap_inst, &set->data->heap,
				&((uint8_t *)set->data)[schema->data_sz]);
	}
	__init_rec_array(set, schema);
	return set;
}

ldms_set_t ldms_set_new_with_auth(const char *instance_name,
				  ldms_schema_t schema,
				  uid_t uid, gid_t gid, mode_t perm)
{
	return ldms_set_create(instance_name, schema, uid, gid, perm, 0);
}

ldms_set_t ldms_set_new_with_heap(const char *instance_name,
				  ldms_schema_t schema, uint32_t heap_sz)
{
	uid_t uid;
	gid_t gid;
	mode_t perm;

	ldms_set_default_authz(&uid, &gid, &perm, DEFAULT_AUTHZ_READONLY);
	return ldms_set_create(instance_name, schema, uid, gid, perm, heap_sz);
}

size_t ldms_list_heap_size_get(enum ldms_value_type type, size_t item_count, size_t array_count)
{
	size_t value_sz, entry_sz;
	if (!ldms_type_is_array(type))
		array_count = 1;
	value_sz = __ldms_value_size_get(type, array_count);
	entry_sz = value_sz + sizeof(struct ldms_list_entry);
	return item_count * ldms_heap_alloc_size(LDMS_LIST_GRAIN, entry_sz);
}

ldms_set_t ldms_set_new(const char *instance_name, ldms_schema_t schema)
{
	uid_t uid;
	gid_t gid;
	mode_t perm;

	ldms_set_default_authz(&uid, &gid, &perm, DEFAULT_AUTHZ_READONLY);
	return ldms_set_create(instance_name, schema, uid, gid, perm, 0);
}

int ldms_set_config_auth(ldms_set_t set, uid_t uid, gid_t gid, mode_t perm)
{
	set->meta->uid = __cpu_to_le32(uid);
	set->meta->gid = __cpu_to_le32(gid);
	set->meta->perm = __cpu_to_le32(perm);
	return 0;
}

const char *ldms_set_name_get(ldms_set_t s)
{
	struct ldms_set_hdr *sh = s->meta;
	return get_instance_name(sh)->name;
}

const char *ldms_set_schema_name_get(ldms_set_t s)
{
	struct ldms_set_hdr *sh = s->meta;
	return get_schema_name(sh)->name;
}

ldms_digest_t ldms_set_digest_get(ldms_set_t s)
{
	ldms_name_t inst = get_instance_name(s->meta);
	ldms_name_t schema = (ldms_name_t)&inst->name[inst->len];
	size_t sz = strlen(schema->name);
	if (schema->len >= sz + LDMS_DIGEST_LENGTH)
		return (ldms_digest_t)&schema->name[sz+1];
	return &null_digest;
}

int ldms_digest_cmp(ldms_digest_t a, ldms_digest_t b)
{
	return memcmp(a, b, LDMS_DIGEST_LENGTH);
}

uint32_t ldms_set_card_get(ldms_set_t s)
{
	return __le32_to_cpu(s->meta->card);
}

uint32_t ldms_set_uid_get(ldms_set_t s)
{
	return __le32_to_cpu(s->meta->uid);
}

int ldms_set_uid_set(ldms_set_t s, uid_t uid)
{
	s->meta->uid = __cpu_to_le32(uid);
	return 0;
}

uint32_t ldms_set_gid_get(ldms_set_t s)
{
	return __le32_to_cpu(s->meta->gid);
}

int ldms_set_gid_set(ldms_set_t s, gid_t gid)
{
	s->meta->gid = __cpu_to_le32(gid);
	return 0;
}

uint32_t ldms_set_perm_get(ldms_set_t s)
{
	return __le32_to_cpu(s->meta->perm);
}

int ldms_set_perm_set(ldms_set_t s, mode_t perm)
{
	s->meta->perm = __cpu_to_le32(perm);
	return 0;
}

void ldms_set_default_authz(uid_t *uid, gid_t *gid, mode_t *perm, int set_flags)
{
	if (set_flags == DEFAULT_AUTHZ_READONLY) {
		pthread_rwlock_rdlock(&__ldms_config.default_authz_lock);
	} else {
		pthread_rwlock_wrlock(&__ldms_config.default_authz_lock);
	}
	if (uid != NULL) {
		if (set_flags & DEFAULT_AUTHZ_SET_UID) {
			__ldms_config.default_authz_uid = *uid;
		} else {
			*uid =__ldms_config.default_authz_uid;
		}
	}
	if (gid != NULL) {
		if (set_flags & DEFAULT_AUTHZ_SET_GID) {
			__ldms_config.default_authz_gid = *gid;
		} else {
			*gid =__ldms_config.default_authz_gid;
		}
	}
	if (perm != NULL) {
		if (set_flags & DEFAULT_AUTHZ_SET_PERM) {
			__ldms_config.default_authz_perm = *perm;
		} else {
			*perm = __ldms_config.default_authz_perm;
		}
	}
	pthread_rwlock_unlock(&__ldms_config.default_authz_lock);
}

extern uint32_t ldms_set_meta_sz_get(ldms_set_t s)
{
	return __le32_to_cpu(s->meta->meta_sz);
}

extern uint32_t ldms_set_data_sz_get(ldms_set_t s)
{
	return __le32_to_cpu(s->meta->data_sz);
}

int ldms_mmap_set(void *meta_addr, void *data_addr, ldms_set_t *ps)
{
	struct ldms_set_hdr *sh = meta_addr;
	struct ldms_data_hdr *dh = data_addr;
	struct ldms_set *set;
	int flags;
	int rc = 0;

	flags = LDMS_SET_F_MEMMAP | LDMS_SET_F_LOCAL;
	set = __record_set(get_instance_name(sh)->name, sh, dh, flags);
	if (!set) {
		rc = errno;
		return rc;
	}
	*ps = set;
	rc = __ldms_set_publish(set);
	return rc;
}

static char *type_names[] = {
	[LDMS_V_NONE] = "none",
	[LDMS_V_CHAR] = "char",
	[LDMS_V_U8] = "u8",
	[LDMS_V_S8] = "s8",
	[LDMS_V_U16] = "u16",
	[LDMS_V_S16] = "s16",
	[LDMS_V_U32] = "u32",
	[LDMS_V_S32] = "s32",
	[LDMS_V_U64] = "u64",
	[LDMS_V_S64] = "s64",
	[LDMS_V_F32] = "f32",
	[LDMS_V_D64] = "d64",
	[LDMS_V_CHAR_ARRAY] = "char[]",
	[LDMS_V_U8_ARRAY] = "u8[]",
	[LDMS_V_S8_ARRAY] = "s8[]",
	[LDMS_V_U16_ARRAY] = "u16[]",
	[LDMS_V_S16_ARRAY] = "s16[]",
	[LDMS_V_U32_ARRAY] = "u32[]",
	[LDMS_V_S32_ARRAY] = "s32[]",
	[LDMS_V_U64_ARRAY] = "u64[]",
	[LDMS_V_S64_ARRAY] = "s64[]",
	[LDMS_V_F32_ARRAY] = "f32[]",
	[LDMS_V_D64_ARRAY] = "d64[]",
	[LDMS_V_LIST] = "list<>",
	[LDMS_V_LIST_ENTRY] = "entry",
	[LDMS_V_RECORD_TYPE] = "record_type",
	[LDMS_V_RECORD_INST] = "record_inst",
	[LDMS_V_RECORD_ARRAY] = "record_array",
	[LDMS_V_TIMESTAMP] = "timestamp",
};

static enum ldms_value_type type_scalar_types[] = {
	[LDMS_V_NONE] = LDMS_V_NONE,
	[LDMS_V_CHAR] = LDMS_V_CHAR,
	[LDMS_V_U8] = LDMS_V_U8,
	[LDMS_V_S8] = LDMS_V_S8,
	[LDMS_V_U16] = LDMS_V_U16,
	[LDMS_V_S16] = LDMS_V_S16,
	[LDMS_V_U32] = LDMS_V_U32,
	[LDMS_V_S32] = LDMS_V_S32,
	[LDMS_V_U64] = LDMS_V_U64,
	[LDMS_V_S64] = LDMS_V_S64,
	[LDMS_V_F32] = LDMS_V_F32,
	[LDMS_V_D64] = LDMS_V_D64,
	[LDMS_V_CHAR_ARRAY] = LDMS_V_CHAR,
	[LDMS_V_U8_ARRAY] = LDMS_V_U8,
	[LDMS_V_S8_ARRAY] = LDMS_V_S8,
	[LDMS_V_U16_ARRAY] = LDMS_V_U16,
	[LDMS_V_S16_ARRAY] = LDMS_V_S16,
	[LDMS_V_U32_ARRAY] = LDMS_V_U32,
	[LDMS_V_S32_ARRAY] = LDMS_V_S32,
	[LDMS_V_U64_ARRAY] = LDMS_V_U64,
	[LDMS_V_S64_ARRAY] = LDMS_V_S64,
	[LDMS_V_F32_ARRAY] = LDMS_V_F32,
	[LDMS_V_D64_ARRAY] = LDMS_V_D64
};

static inline ldms_mdesc_t __desc_get(ldms_set_t s, int idx)
{
	if (idx >= 0 && idx < __le32_to_cpu(s->meta->card))
		return ldms_ptr_(struct ldms_value_desc, s->meta,
				__le32_to_cpu(s->meta->dict[idx]));
	errno = ENOENT;
	return NULL;
}

const char *ldms_metric_name_get(ldms_set_t set, int i)
{
	ldms_mdesc_t desc = __desc_get(set, i);
	if (desc)
		return desc->vd_name_unit;
	return NULL;
}

const char *ldms_metric_unit_get(ldms_set_t set, int i)
{
	char *unit;
	ldms_mdesc_t desc = __desc_get(set, i);
	if (!desc)
		return NULL;
	unit = strrchr(desc->vd_name_unit, '\0');
	if (desc->vd_name_unit + desc->vd_name_unit_len - 1 == unit)
		return NULL;
	else
		return unit + 1;
}

enum ldms_value_type ldms_metric_type_get(ldms_set_t set, int i)
{
	ldms_mdesc_t desc;
	ldms_mval_t mval;
	mval = __mval_to_get(set, i, &desc);
	if (!mval)
		return LDMS_V_NONE;
	return desc->vd_type;
}

int ldms_metric_flags_get(ldms_set_t s, int i)
{
	ldms_mdesc_t desc = __desc_get(s, i);
	if (desc)
		return desc->vd_flags;
	return 0;
}

int ldms_metric_by_name(ldms_set_t set, const char *name)
{
	int i;
	for (i = 0; i < ldms_set_card_get(set); i++) {
		ldms_mdesc_t desc = __desc_get(set, i);
		if (0 == strcmp(desc->vd_name_unit, name))
			return i;
	}
	return -1;
}

int __schema_mdef_add(ldms_schema_t s, ldms_mdef_t m)
{
	/* Digest */
	if (m->type == LDMS_V_RECORD_TYPE) {
		/* If this is a record, digest the members of the record too */
		ldms_record_t rec_def;
		ldms_mdef_t rec_m;
		rec_def = container_of(m, struct ldms_record, mdef);
		STAILQ_FOREACH(rec_m, &rec_def->rec_metric_list, entry) {
			EVP_DigestUpdate(s->evp_ctx, rec_m->name, strlen(rec_m->name));
			EVP_DigestUpdate(s->evp_ctx, &rec_m->type, sizeof(rec_m->type));
		}
	}
	EVP_DigestUpdate(s->evp_ctx, m->name, strlen(m->name));
	EVP_DigestUpdate(s->evp_ctx, &m->type, sizeof(m->type));

	STAILQ_INSERT_TAIL(&s->metric_list, m, entry);
	s->card++;
	s->meta_sz += m->meta_sz + sizeof(uint32_t) /* + dict entry */;
	if (m->flags & LDMS_MDESC_F_DATA) {
		s->data_sz += m->data_sz;
	} else {
		s->meta_sz += m->data_sz;
	}

	return s->card - 1;
}

int __schema_metric_add(ldms_schema_t s, const char *name, const char *unit,
			int flags, enum ldms_value_type type, uint32_t array_count)
{
	ldms_mdef_t m;

	if (!s || !name)
		return -EINVAL;

	/* check if the name is a duplicate */
	STAILQ_FOREACH(m, &s->metric_list, entry) {
		if (!strcmp(m->name, name))
			return -EEXIST;
	}

	/* if the type is a list, it cannot exist in the meta-data section */
	if (flags & LDMS_MDESC_F_META && type == LDMS_V_LIST)
		return -EINVAL;

	m = calloc(1, sizeof *m);
	if (!m)
		return -ENOMEM;

	m->name = strdup(name);
	if (!m->name)
		goto enomem;
	if (unit) {
		m->unit = strdup(unit);
		if (!m->unit)
			goto enomem;
	} else {
		m->unit = NULL;
	}
	m->type = type;
	m->flags = flags;
	m->count = array_count;
	__ldms_metric_size_get(name, unit, type, m->count, &m->meta_sz, &m->data_sz);

	return __schema_mdef_add(s, m);

enomem:
	free(m->name);
	free(m);
	return -ENOMEM;
}

int ldms_schema_metric_add_with_unit(ldms_schema_t s, const char *name,
				     const char *unit, enum ldms_value_type type)
{
	if (ldms_type_is_array(type) || type == LDMS_V_LIST)
		return -EINVAL;
	return __schema_metric_add(s, name, unit, LDMS_MDESC_F_DATA, type, 1);
}


int ldms_schema_metric_add(ldms_schema_t s, const char *name, enum ldms_value_type type)
{
	return ldms_schema_metric_add_with_unit(s, name, "", type);
}

int ldms_schema_meta_add_with_unit(ldms_schema_t s, const char *name,
				   const char *unit, enum ldms_value_type type)
{
	if (type == LDMS_V_LIST || type > LDMS_V_LAST)
		return -EINVAL;
	return __schema_metric_add(s, name, unit, LDMS_MDESC_F_META, type, 1);
}

int ldms_schema_meta_add(ldms_schema_t s, const char *name, enum ldms_value_type type)
{
	return ldms_schema_meta_add_with_unit(s, name, "", type);
}

int ldms_schema_metric_array_add_with_unit(ldms_schema_t s, const char *name,
					   const char *unit,
					   enum ldms_value_type type,
					   uint32_t count)
{
	if (!ldms_type_is_array(type))
		return -EINVAL;
	return __schema_metric_add(s, name, unit, LDMS_MDESC_F_DATA, type, count);
}

int ldms_schema_metric_array_add(ldms_schema_t s, const char *name,
				 enum ldms_value_type type, uint32_t count)
{
	return ldms_schema_metric_array_add_with_unit(s, name, "", type, count);
}

int ldms_schema_meta_array_add_with_unit(ldms_schema_t s, const char *name,
					 const char *unit,
					 enum ldms_value_type type,
					 uint32_t count)
{
	return __schema_metric_add(s, name, unit, LDMS_MDESC_F_META, type, count);
}

int ldms_schema_meta_array_add(ldms_schema_t s, const char *name,
			       enum ldms_value_type type, uint32_t count)
{
	return ldms_schema_meta_array_add_with_unit(s, name, "", type, count);
}

int ldms_schema_metric_list_add(ldms_schema_t s, const char *name, const char *units, uint32_t heap_sz)
{
	return __schema_metric_add(s, name, units, LDMS_MDESC_F_DATA, LDMS_V_LIST, heap_sz);
}

static struct _ldms_type_name_map {
	const char *name;
	enum ldms_value_type type;
} type_name_map[] = {
	/* This map needs to be sorted by name */
	{ "CHAR", LDMS_V_CHAR },
	{ "CHAR_ARRAY", LDMS_V_CHAR_ARRAY },
	{ "D64", LDMS_V_D64, },
	{ "D64_ARRAY", LDMS_V_D64_ARRAY},
	{ "F32", LDMS_V_F32, },
	{ "F32_ARRAY", LDMS_V_F32_ARRAY},
	{ "LIST", LDMS_V_LIST },
	{ "NONE", LDMS_V_NONE, },
	{ "RECORD", LDMS_V_RECORD_INST },
	{ "RECORD_ARRAY", LDMS_V_RECORD_ARRAY },
	{ "RECORD_TYPE", LDMS_V_RECORD_TYPE },
	{ "S16", LDMS_V_S16, },
	{ "S16_ARRAY", LDMS_V_S16_ARRAY},
	{ "S32", LDMS_V_S32, },
	{ "S32_ARRAY", LDMS_V_S32_ARRAY},
	{ "S64", LDMS_V_S64, },
	{ "S64_ARRAY", LDMS_V_S64_ARRAY},
	{ "S8", LDMS_V_S8, },
	{ "S8_ARRAY", LDMS_V_S8_ARRAY},
	{ "TIMESTAMP", LDMS_V_TIMESTAMP},
	{ "TS", LDMS_V_TIMESTAMP},
	{ "U16", LDMS_V_U16, },
	{ "U16_ARRAY", LDMS_V_U16_ARRAY},
	{ "U32", LDMS_V_U32, },
	{ "U32_ARRAY", LDMS_V_U32_ARRAY},
	{ "U64", LDMS_V_U64, },
	{ "U64_ARRAY", LDMS_V_U64_ARRAY},
	{ "U8", LDMS_V_U8, },
	{ "U8_ARRAY", LDMS_V_U8_ARRAY},
};

int comparator(const void *a, const void *b)
{
	const char *n1 = a;
	const struct _ldms_type_name_map *el = b;
	return strcasecmp(n1, el->name);
}

enum ldms_value_type ldms_metric_str_to_type(const char *name)
{
	struct _ldms_type_name_map *p;
	p = bsearch(name,
		    type_name_map,
		    sizeof(type_name_map) / sizeof(type_name_map[0]),
		    sizeof(struct _ldms_type_name_map),
		    comparator);

	if (p)
		return p->type;
	return LDMS_V_NONE;
}

const char *ldms_metric_type_to_str(enum ldms_value_type t)
{
	if (t > LDMS_V_LAST)
		t = LDMS_V_NONE;
	return type_names[t];
}

enum ldms_value_type ldms_metric_type_to_scalar_type(enum ldms_value_type t)
{
	if (t > LDMS_V_LAST || t < 0)
		t = LDMS_V_NONE;
	return type_scalar_types[t];
}

void ldms_metric_user_data_set(ldms_set_t s, int i, uint64_t u)
{
	ldms_mdesc_t desc = __desc_get(s, i);
	if (desc) {
		desc->vd_user_data = __cpu_to_le64(u);
		__ldms_gn_inc(s, desc);
	}
}

uint64_t ldms_metric_user_data_get(ldms_set_t s, int i)
{
	ldms_mdesc_t desc = __desc_get(s, i);
	if (desc)
		return __le64_to_cpu(desc->vd_user_data);
	return (uint64_t)-1;
}

int ldms_type_is_array(enum ldms_value_type t)
{
	return (t >= LDMS_V_CHAR_ARRAY && t <= LDMS_V_D64_ARRAY);
}

static int metric_is_array(ldms_mdesc_t desc)
{
	return ldms_type_is_array(desc->vd_type);
}

int ldms_metric_is_array(ldms_set_t s, int i)
{
	ldms_mdesc_t desc = ldms_ptr_(struct ldms_value_desc, s->meta,
			__le32_to_cpu(s->meta->dict[i]));
	return metric_is_array(desc);
}

void ldms_metric_modify(ldms_set_t s, int i)
{
	ldms_mdesc_t desc = ldms_ptr_(struct ldms_value_desc, s->meta,
			__le32_to_cpu(s->meta->dict[i]));
	if (desc)
		__ldms_gn_inc(s, desc);
	else
		assert(0 == "Invalid metric index");
}

static ldms_mval_t __mval_to_set(struct ldms_set *s, int idx, ldms_mdesc_t *pd)
{
	ldms_mdesc_t desc;

	if (idx < 0 || idx >= __le32_to_cpu(s->meta->card)) {
		errno = ENOENT;
		return NULL;
	}

	desc = ldms_ptr_(struct ldms_value_desc, s->meta,
			__le32_to_cpu(s->meta->dict[idx]));
	if (pd)
		*pd = desc;
	if (desc->vd_flags & LDMS_MDESC_F_DATA) {
		return ldms_ptr_(union ldms_value, s->data,
				 __le32_to_cpu(desc->vd_data_offset));
	}
	return ldms_ptr_(union ldms_value, s->meta,
			__le32_to_cpu(desc->vd_data_offset));
}

static ldms_mval_t __mval_to_get(struct ldms_set *s, int idx, ldms_mdesc_t *pd)
{
	struct ldms_data_hdr *prev_data;
	int n;
	ldms_mdesc_t desc;

	if (idx < 0 || idx >= __le32_to_cpu(s->meta->card)) {
		errno = ENOENT;
		return NULL;
	}
	desc = ldms_ptr_(struct ldms_value_desc, s->meta,
			 __le32_to_cpu(s->meta->dict[idx]));
	if (pd)
		*pd = desc;
	if (desc->vd_flags & LDMS_MDESC_F_DATA) {
		/* Check if it is being called inside a transaction. However,
		 * LIST always return the current buffer. */
		if (s->data->trans.flags != LDMS_TRANSACTION_END &&
		    desc->vd_type != LDMS_V_LIST &&
		    desc->vd_type != LDMS_V_RECORD_ARRAY) {
			/* Inside a transaction */
			n = __le32_to_cpu(s->meta->array_card);
			prev_data = __set_array_get(s, (s->curr_idx + (n - 1)) % n);
			/* Return the metric from the previous set in the ring buffer */
			return ldms_ptr_(union ldms_value, prev_data,
					__le32_to_cpu(desc->vd_data_offset));
		} else {
			return ldms_ptr_(union ldms_value, s->data,
					 __le32_to_cpu(desc->vd_data_offset));
		}
	}
	return ldms_ptr_(union ldms_value, s->meta,
			__le32_to_cpu(desc->vd_data_offset));
}

ldms_mval_t ldms_metric_get(ldms_set_t s, int i)
{
	return __mval_to_get(s, i, NULL);
}

uint32_t ldms_metric_array_get_len(ldms_set_t s, int i)
{
	ldms_mdesc_t desc = ldms_ptr_(struct ldms_value_desc, s->meta,
				      __le32_to_cpu(s->meta->dict[i]));
	if (metric_is_array(desc))
		return __le32_to_cpu(desc->vd_array_count);
	return 1;
}
ldms_mval_t ldms_metric_array_get(ldms_set_t s, int i)
{
	ldms_mdesc_t desc;
	ldms_mval_t ret = __mval_to_get(s, i, &desc);
	if (metric_is_array(desc))
		return ret;
	return NULL;
}

static void __metric_set(ldms_mdesc_t desc, ldms_mval_t mv, ldms_mval_t v)
{
	switch (desc->vd_type) {
	case LDMS_V_CHAR:
	case LDMS_V_U8:
	case LDMS_V_S8:
		mv->v_u8 = v->v_u8;
		break;
	case LDMS_V_U16:
	case LDMS_V_S16:
		mv->v_u16 = __cpu_to_le16(v->v_u16);
		break;
	case LDMS_V_U32:
	case LDMS_V_S32:
		mv->v_u32 = __cpu_to_le32(v->v_u32);
		break;
	case LDMS_V_U64:
	case LDMS_V_S64:
		mv->v_u64 = __cpu_to_le64(v->v_u64);
		break;
	case LDMS_V_F32:
		mv->v_u32 = __cpu_to_le32(v->v_u32);
		break;
	case LDMS_V_D64:
		mv->v_u64 = __cpu_to_le64(v->v_u64);
		break;
	default:
		assert(0 == "unexpected metric type");
		return;
	}
}

void ldms_metric_set(ldms_set_t s, int i, ldms_mval_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv;

	if (i < 0 || i >= __le32_to_cpu(s->meta->card))
		assert(0 == "Invalid metric index");

	mv = __mval_to_set(s, i, &desc);

	__metric_set(desc, mv, v);
	__ldms_gn_inc(s, desc);
}

static void __metric_array_set(ldms_mdesc_t desc, ldms_mval_t dst,
			       int i, ldms_mval_t src)
{
	if (i < 0 || i >= __le32_to_cpu(desc->vd_array_count))
		assert(0 == "Array index is greater than array size");

	switch (desc->vd_type) {
	case LDMS_V_CHAR_ARRAY:
	case LDMS_V_U8_ARRAY:
	case LDMS_V_S8_ARRAY:
		dst->a_u8[i] = src->v_u8;
		break;
	case LDMS_V_U16_ARRAY:
	case LDMS_V_S16_ARRAY:
		dst->a_u16[i] = __cpu_to_le16(src->v_u16);
		break;
	case LDMS_V_U32_ARRAY:
	case LDMS_V_S32_ARRAY:
		dst->a_u32[i] = __cpu_to_le32(src->v_u32);
		break;
	case LDMS_V_U64_ARRAY:
	case LDMS_V_S64_ARRAY:
		dst->a_u64[i] = __cpu_to_le64(src->v_u64);
		break;
	case LDMS_V_F32_ARRAY:
#if LDMS_SETH_F_LCLBYTEORDER == LDMS_SETH_F_LE
		dst->a_f[i] = src->v_f;
#else
		*(uint32_t *)&dst->a_f[i] = __cpu_to_le32(*(uint32_t*)&src->v_f);
#endif
		break;
	case LDMS_V_D64_ARRAY:
#if LDMS_SETH_F_LCLBYTEORDER == LDMS_SETH_F_LE
		dst->a_d[i] = src->v_d;
#else
		*(uint64_t*)&dst->a_d[i] = __cpu_to_le64(*(uint64_t*)&src->v_d);
#endif
		break;
	default:
		assert(0 == "unexpected metric type");
		return;
	}
}

void ldms_metric_array_set_val(ldms_set_t s, int metric_idx, int array_idx, ldms_mval_t src)
{
	ldms_mdesc_t desc;
	ldms_mval_t dst;

	if (metric_idx >= __le32_to_cpu(s->meta->card))
		assert(0 == "Invalid metric index");

	dst = __mval_to_set(s, metric_idx, &desc);

	__metric_array_set(desc, dst, array_idx, src);
	__ldms_gn_inc(s, desc);
}

void ldms_metric_set_char(ldms_set_t s, int i, char v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s, i, &desc);
	if (mv) {
		mv->v_char = v;
		__ldms_gn_inc(s, desc);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_metric_set_u8(ldms_set_t s, int i, uint8_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s, i, &desc);
	if (mv) {
		mv->v_u8 = v;
		__ldms_gn_inc(s, desc);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_metric_set_s8(ldms_set_t s, int i, int8_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s, i, &desc);
	if (mv) {
		mv->v_s8 = v;
		__ldms_gn_inc(s, desc);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_metric_set_u16(ldms_set_t s, int i, uint16_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s, i, &desc);
	if (mv) {
		mv->v_u16 = __cpu_to_le16(v);
		__ldms_gn_inc(s, desc);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_metric_set_s16(ldms_set_t s, int i, int16_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s, i, &desc);
	if (mv) {
		mv->v_s16 = __cpu_to_le16(v);
		__ldms_gn_inc(s, desc);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_metric_set_u32(ldms_set_t s, int i, uint32_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s, i, &desc);
	if (mv) {
		mv->v_u32 = __cpu_to_le32(v);
		__ldms_gn_inc(s, desc);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_metric_set_s32(ldms_set_t s, int i, int32_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s, i, &desc);
	if (mv) {
		mv->v_s32 = __cpu_to_le32(v);
		__ldms_gn_inc(s, desc);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_metric_set_u64(ldms_set_t s, int i, uint64_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s, i, &desc);
	if (mv) {
		mv->v_u64 = __cpu_to_le64(v);
		__ldms_gn_inc(s, desc);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_metric_set_s64(ldms_set_t s, int i, int64_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s, i, &desc);
	if (mv) {
		mv->v_s64 = __cpu_to_le64(v);
		__ldms_gn_inc(s, desc);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_metric_set_float(ldms_set_t s, int i, float v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s, i, &desc);
	if (mv) {
#if LDMS_SETH_F_LCLBYTEORDER == LDMS_SETH_F_LE
		mv->v_f = v;
#else
		*(uint32_t *)&mv->v_f = __cpu_to_le32(*(uint32_t *)&v);
#endif
		__ldms_gn_inc(s, desc);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_metric_set_double(ldms_set_t s, int i, double v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s, i, &desc);
	if (mv) {
#if LDMS_SETH_F_LCLBYTEORDER == LDMS_SETH_F_LE
		mv->v_d = v;
#else
		*(uint64_t *)&mv->v_d = __cpu_to_le64(*(uint64_t *)&v);
#endif
		__ldms_gn_inc(s, desc);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_metric_array_set_str(ldms_set_t s, int mid, const char *str)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s, mid, &desc);
	if (mv) {
		strncpy(mv->a_char, str, desc->vd_array_count);
		__ldms_gn_inc(s, desc);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_metric_array_set_char(ldms_set_t s, int mid, int idx, char v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s, mid, &desc);
	if (mv && (idx < desc->vd_array_count)) {
		mv->a_char[idx] = v;
		__ldms_gn_inc(s, desc);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_metric_array_set_u8(ldms_set_t s, int mid, int idx, uint8_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s, mid, &desc);
	if (mv && (idx < desc->vd_array_count)) {
		mv->a_u8[idx] = v;
		__ldms_gn_inc(s, desc);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_metric_array_set_s8(ldms_set_t s, int mid, int idx, int8_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s, mid, &desc);
	if (mv && (idx < desc->vd_array_count)) {
		mv->a_s8[idx] = v;
		__ldms_gn_inc(s, desc);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_metric_array_set_u16(ldms_set_t s, int mid, int idx, uint16_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s, mid, &desc);
	if (mv && (idx < desc->vd_array_count)) {
		mv->a_u16[idx] = __cpu_to_le16(v);
		__ldms_gn_inc(s, desc);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_metric_array_set_s16(ldms_set_t s, int mid, int idx, int16_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s, mid, &desc);
	if (mv && (idx < desc->vd_array_count)) {
		mv->a_s16[idx] = __cpu_to_le16(v);
		__ldms_gn_inc(s, desc);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_metric_array_set_u32(ldms_set_t s, int mid, int idx, uint32_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s, mid, &desc);
	if (mv && (idx < desc->vd_array_count)) {
		mv->a_u32[idx] = __cpu_to_le32(v);
		__ldms_gn_inc(s, desc);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_metric_array_set_s32(ldms_set_t s, int mid, int idx, int32_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s, mid, &desc);
	if (mv && (idx < desc->vd_array_count)) {
		mv->a_s32[idx] = __cpu_to_le32(v);
		__ldms_gn_inc(s, desc);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_metric_array_set_u64(ldms_set_t s, int mid, int idx, uint64_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s, mid, &desc);
	if (mv && (idx < desc->vd_array_count)) {
		mv->a_u64[idx] = __cpu_to_le64(v);
		__ldms_gn_inc(s, desc);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_metric_array_set_s64(ldms_set_t s, int mid, int idx, int64_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s, mid, &desc);
	if (mv && (idx < desc->vd_array_count)) {
		mv->a_s64[idx] = __cpu_to_le64(v);
		__ldms_gn_inc(s, desc);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_metric_array_set_float(ldms_set_t s, int mid, int idx, float v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s, mid, &desc);
	if (mv && (idx < desc->vd_array_count)) {
#if LDMS_SETH_F_LCLBYTEORDER == LDMS_SETH_F_LE
		mv->a_f[idx] = v;
#else
		/* This type abuse is necessary to avoid the integer cast
		 * that will strip the factional portion of the float value
		 */
		*(uint32_t *)&mv->a_f[idx] = __cpu_to_le32(*(uint32_t *)&v);
#endif
		__ldms_gn_inc(s, desc);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_metric_array_set_double(ldms_set_t s, int mid, int idx, double v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s, mid, &desc);
	if (mv && (idx < desc->vd_array_count)) {
#if LDMS_SETH_F_LCLBYTEORDER == LDMS_SETH_F_LE
		mv->a_d[idx] = v;
#else
		/* See xxx_set_float for type abuse note */
		*(uint64_t *)&mv->a_d[idx] = __cpu_to_le64(*(uint64_t *)&v);
#endif
		__ldms_gn_inc(s, desc);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_metric_array_set(ldms_set_t s, int mid, ldms_mval_t mval,
			   size_t start, size_t count)
{
	ldms_mdesc_t desc = ldms_ptr_(struct ldms_value_desc, s->meta,
				      __le32_to_cpu(s->meta->dict[mid]));
	int i;
	ldms_mval_t val = __mval_to_set(s, mid, &desc);
	switch (desc->vd_type) {
	case LDMS_V_CHAR_ARRAY:
	case LDMS_V_U8_ARRAY:
	case LDMS_V_S8_ARRAY:
		for (i = start; i < start + count && i < desc->vd_array_count; i++)
			val->a_u8[i] = mval->a_u8[i];
		break;
	case LDMS_V_U16_ARRAY:
	case LDMS_V_S16_ARRAY:
		for (i = start; i < start + count && i < desc->vd_array_count; i++)
			val->a_u16[i] = __cpu_to_le16(mval->a_u16[i]);
		break;
	case LDMS_V_U32_ARRAY:
	case LDMS_V_S32_ARRAY:
	case LDMS_V_F32_ARRAY:
		for (i = start; i < start + count && i < desc->vd_array_count; i++)
			val->a_u32[i] = __cpu_to_le32(mval->a_u32[i]);
		break;
	case LDMS_V_U64_ARRAY:
	case LDMS_V_S64_ARRAY:
	case LDMS_V_D64_ARRAY:
		for (i = start; i < start + count && i < desc->vd_array_count; i++)
			val->a_u64[i] = __cpu_to_le64(mval->a_u64[i]);
		break;
	default:
		assert(0 == "Invalid array element type");
	}
	__ldms_gn_inc(s, desc);
}

char ldms_metric_get_char(ldms_set_t s, int i)
{
	ldms_mval_t mv = __mval_to_get(s, i, NULL);
	if (mv)
		return mv->v_char;
	else
		assert(0 == "Invalid metric index");
	return 0;
}

uint8_t ldms_metric_get_u8(ldms_set_t s, int i)
{
	ldms_mval_t mv = __mval_to_get(s, i, NULL);
	if (mv)
		return mv->v_u8;
	else
		assert(0 == "Invalid metric index");
	return 0;
}

int8_t ldms_metric_get_s8(ldms_set_t s, int i)
{
	ldms_mval_t mv = __mval_to_get(s, i, NULL);
	if (mv)
		return mv->v_s8;
	else
		assert(0 == "Invalid metric index");
	return 0;
}

uint16_t ldms_metric_get_u16(ldms_set_t s, int i)
{
	ldms_mval_t mv = __mval_to_get(s, i, NULL);
	if (mv)
		return __le16_to_cpu(mv->v_u16);
	else
		assert(0 == "Invalid metric index");
	return 0;
}

int16_t ldms_metric_get_s16(ldms_set_t s, int i)
{
	ldms_mval_t mv = __mval_to_get(s, i, NULL);
	if (mv)
		return __le16_to_cpu(mv->v_s16);
	else
		assert(0 == "Invalid metric index");
	return 0;
}

uint32_t ldms_metric_get_u32(ldms_set_t s, int i)
{
	ldms_mval_t mv = __mval_to_get(s, i, NULL);
	if (mv)
		return __le32_to_cpu(mv->v_u32);
	else
		assert(0 == "Invalid metric index");
	return 0;
}

int32_t ldms_metric_get_s32(ldms_set_t s, int i)
{
	ldms_mval_t mv = __mval_to_get(s, i, NULL);
	if (mv)
		return __le32_to_cpu(mv->v_s32);
	else
		assert(0 == "Invalid metric index");
	return 0;
}

uint64_t ldms_metric_get_u64(ldms_set_t s, int i)
{
	ldms_mval_t mv = __mval_to_get(s, i, NULL);
	if (mv)
		return __le64_to_cpu(mv->v_u64);
	else
		assert(0 == "Invalid metric index");
	return 0;
}

int64_t ldms_metric_get_s64(ldms_set_t s, int i)
{
	ldms_mval_t mv = __mval_to_get(s, i, NULL);
	if (mv)
		return __le64_to_cpu(mv->v_s64);
	else
		assert(0 == "Invalid metric index");
	return 0;
}

float ldms_metric_get_float(ldms_set_t s, int i)
{
	ldms_mval_t mv = __mval_to_get(s, i, NULL);
	if (mv) {
#if LDMS_SETH_F_LCLBYTEORDER == LDMS_SETH_F_LE
		return mv->v_f;
#else
		uint32_t tmp = __le32_to_cpu(*(uint32_t*)&mv->v_f);
		return *(float *)&tmp;
#endif
	} else
		assert(0 == "Invalid metric index");
	return 0;
}

double ldms_metric_get_double(ldms_set_t s, int i)
{
	ldms_mval_t mv = __mval_to_get(s, i, NULL);
	if (mv) {
#if LDMS_SETH_F_LCLBYTEORDER == LDMS_SETH_F_LE
		return mv->v_d;
#else
		uint64_t tmp = __le64_to_cpu(*(uint64_t*)&mv->v_d);
		return *(double *)&tmp;
#endif
	} else
		assert(0 == "Invalid metric index");
	return 0;
}

const char *ldms_metric_array_get_str(ldms_set_t s, int mid)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_get(s, mid, &desc);
	if (mv)
		return mv->a_char;
	else
		assert(0 == "Invalid metric index");
	return 0;
}

char ldms_metric_array_get_char(ldms_set_t s, int mid, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_get(s, mid, &desc);
	if (mv && (idx < desc->vd_array_count))
		return mv->a_char[idx];
	else
		assert(0 == "Invalid metric or array index");
	return 0;
}

uint8_t ldms_metric_array_get_u8(ldms_set_t s, int mid, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_get(s, mid, &desc);
	if (mv && (idx < desc->vd_array_count))
		return mv->a_u8[idx];
	else
		assert(0 == "Invalid metric or array index");
	return 0;
}

int8_t ldms_metric_array_get_s8(ldms_set_t s, int mid, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_get(s, mid, &desc);
	if (mv && (idx < desc->vd_array_count))
		return mv->a_s8[idx];
	else
		assert(0 == "Invalid metric or array index");
	return 0;
}

uint16_t ldms_metric_array_get_u16(ldms_set_t s, int mid, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_get(s, mid, &desc);
	if (mv && (idx < desc->vd_array_count))
		return mv->a_u16[idx];
	else
		assert(0 == "Invalid metric or array index");
	return 0;
}

int16_t ldms_metric_array_get_s16(ldms_set_t s, int mid, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_get(s, mid, &desc);
	if (mv && (idx < desc->vd_array_count))
		return mv->a_s16[idx];
	else
		assert(0 == "Invalid metric or array index");
	return 0;
}

uint32_t ldms_metric_array_get_u32(ldms_set_t s, int mid, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_get(s, mid, &desc);
	if (mv && (idx < desc->vd_array_count))
		return mv->a_u32[idx];
	else
		assert(0 == "Invalid metric or array index");
	return 0;
}

int32_t ldms_metric_array_get_s32(ldms_set_t s, int mid, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_get(s, mid, &desc);
	if (mv && (idx < desc->vd_array_count))
		return mv->a_s32[idx];
	else
		assert(0 == "Invalid metric or array index");
	return 0;
}

uint64_t ldms_metric_array_get_u64(ldms_set_t s, int mid, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_get(s, mid, &desc);
	if (mv && (idx < desc->vd_array_count))
		return mv->a_u64[idx];
	else
		assert(0 == "Invalid metric or array index");
	return 0;
}

int64_t ldms_metric_array_get_s64(ldms_set_t s, int mid, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_get(s, mid, &desc);
	if (mv && (idx < desc->vd_array_count))
		return mv->a_s64[idx];
	else
		assert(0 == "Invalid metric or array index");
	return 0;
}

float ldms_metric_array_get_float(ldms_set_t s, int mid, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_get(s, mid, &desc);
	if (mv && (idx < desc->vd_array_count)) {
#if LDMS_SETH_F_LCLBYTEORDER == LDMS_SETH_F_LE
		return mv->a_f[idx];
#else
		uint32_t tmp = __le32_to_cpu(*(uint32_t*)&mv->a_f[idx]);
		return *(float *)&tmp;
#endif
	} else
		assert(0 == "Invalid metric or array index");
	return 0;
}

double ldms_metric_array_get_double(ldms_set_t s, int mid, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_get(s, mid, &desc);
	if (mv && (idx < desc->vd_array_count)) {
#if LDMS_SETH_F_LCLBYTEORDER == LDMS_SETH_F_LE
		return mv->a_d[idx];
#else
		uint64_t tmp = __le64_to_cpu(*(uint64_t*)&mv->a_d[idx]);
		return *(double *)&tmp;
#endif
	} else
		assert(0 == "Invalid metric or array index");
	return 0;
}

void ldms_mval_set_char(ldms_mval_t mv, char v)
{
	mv->v_char = v;
}

void ldms_mval_set_u8(ldms_mval_t mv, uint8_t v)
{
	mv->v_u8 = v;
}

void ldms_mval_set_s8(ldms_mval_t mv, int8_t v)
{
	mv->v_s8 = v;
}

void ldms_mval_set_u16(ldms_mval_t mv, uint16_t v)
{
	mv->v_u16 = __cpu_to_le16(v);
}

void ldms_mval_set_s16(ldms_mval_t mv, int16_t v)
{
	mv->v_s16 = __cpu_to_le16(v);
}

void ldms_mval_set_u32(ldms_mval_t mv, uint32_t v)
{
	mv->v_u32 = __cpu_to_le32(v);
}

void ldms_mval_set_s32(ldms_mval_t mv, int32_t v)
{
	mv->v_s32 = __cpu_to_le32(v);
}

void ldms_mval_set_u64(ldms_mval_t mv, uint64_t v)
{
	mv->v_u64 = __cpu_to_le64(v);
}

void ldms_mval_set_s64(ldms_mval_t mv, int64_t v)
{
	mv->v_s64 = __cpu_to_le64(v);
}

void ldms_mval_set_float(ldms_mval_t mv, float v)
{
	/*
	 * This type abuse is necessary to avoid the integer cast
	 * that will strip the fractional portion of the float value
	 */
	*(uint32_t *)&mv->v_f = __cpu_to_le32(*(uint32_t *)&v);
}

void ldms_mval_set_double(ldms_mval_t mv, double v)
{
	*(uint64_t *)&mv->v_d = __cpu_to_le64(*(uint64_t *)&v);
}

void ldms_mval_array_set_str(ldms_mval_t mv, const char *str, size_t count)
{
	strncpy(mv->a_char, str, count);
}

void ldms_mval_array_set_char(ldms_mval_t mv, int idx, char v)
{
	mv->a_char[idx] = v;
}

void ldms_mval_array_set_u8(ldms_mval_t mv, int idx, uint8_t v)
{
	mv->a_u8[idx] = v;
}

void ldms_mval_array_set_s8(ldms_mval_t mv, int idx, int8_t v)
{
	mv->a_s8[idx] = v;
}

void ldms_mval_array_set_u16(ldms_mval_t mv, int idx, uint16_t v)
{
	mv->a_u16[idx] = __cpu_to_le16(v);
}

void ldms_mval_array_set_s16(ldms_mval_t mv, int idx, int16_t v)
{
	mv->a_s16[idx] = __cpu_to_le16(v);
}

void ldms_mval_array_set_u32(ldms_mval_t mv, int idx, uint32_t v)
{
	mv->a_u32[idx] = __cpu_to_le32(v);
}

void ldms_mval_array_set_s32(ldms_mval_t mv, int idx, int32_t v)
{
	mv->a_s32[idx] = __cpu_to_le32(v);
}

void ldms_mval_array_set_u64(ldms_mval_t mv, int idx, uint64_t v)
{
	mv->a_u64[idx] = __cpu_to_le64(v);
}

void ldms_mval_array_set_s64(ldms_mval_t mv, int idx, int64_t v)
{
	mv->a_s64[idx] = __cpu_to_le64(v);
}

void ldms_mval_array_set_float(ldms_mval_t mv, int idx, float v)
{
	/*
	 * This type abuse is necessary to avoid the integer cast
	 * that will strip the fractional portion of the float value
	 */
	*(uint32_t *)&mv->a_f[idx] = __cpu_to_le32(*(uint32_t *)&v);
}

void ldms_mval_array_set_double(ldms_mval_t mv, int idx, double v)
{
	/* See xxx_set_float for type abuse note */
	*(uint64_t *)&mv->a_d[idx] = __cpu_to_le64(*(uint64_t *)&v);
}

char ldms_mval_get_char(ldms_mval_t mv)
{
	return mv->v_char;
}

uint8_t ldms_mval_get_u8(ldms_mval_t mv)
{
	return mv->v_u8;
}

int8_t ldms_mval_get_s8(ldms_mval_t mv)
{
	return mv->v_s8;
}

uint16_t ldms_mval_get_u16(ldms_mval_t mv)
{
	return __le16_to_cpu(mv->v_u16);
}

int16_t ldms_mval_get_s16(ldms_mval_t mv)
{
	return __le16_to_cpu(mv->v_s16);
}

uint32_t ldms_mval_get_u32(ldms_mval_t mv)
{
	return __le32_to_cpu(mv->v_u32);
}

int32_t ldms_mval_get_s32(ldms_mval_t mv)
{
	return __le32_to_cpu(mv->v_s32);
}

uint64_t ldms_mval_get_u64(ldms_mval_t mv)
{
	return __le64_to_cpu(mv->v_u64);
}

int64_t ldms_mval_get_s64(ldms_mval_t mv)
{
	return __le64_to_cpu(mv->v_s64);
}

float ldms_mval_get_float(ldms_mval_t mv)
{
#if LDMS_SETH_F_LCLBYTEORDER == LDMS_SETH_F_LE
	return mv->v_f;
#else
	uint32_t tmp = __le32_to_cpu(*(uint32_t*)&mv->v_f);
	return *(float *)&tmp;
#endif
}

double ldms_mval_get_double(ldms_mval_t mv)
{
#if LDMS_SETH_F_LCLBYTEORDER == LDMS_SETH_F_LE
	return mv->v_d;
#else
	uint64_t tmp = __le64_to_cpu(*(uint64_t*)&mv->v_d);
		return *(double *)&tmp;
#endif
}

const char *ldms_mval_array_get_str(ldms_mval_t mv)
{
	return mv->a_char;
}

char ldms_mval_array_get_char(ldms_mval_t mv, int idx)
{
	return mv->a_char[idx];
}

uint8_t ldms_mval_array_get_u8(ldms_mval_t mv, int idx)
{
	return mv->a_u8[idx];
}

int8_t ldms_mval_array_get_s8(ldms_mval_t mv, int idx)
{
	return mv->a_s8[idx];
}

uint16_t ldms_mval_array_get_u16(ldms_mval_t mv, int idx)
{
	return __le16_to_cpu(mv->a_u16[idx]);
}

int16_t ldms_mval_array_get_s16(ldms_mval_t mv, int idx)
{
	return __le16_to_cpu(mv->a_s16[idx]);
}

uint32_t ldms_mval_array_get_u32(ldms_mval_t mv, int idx)
{
	return __le32_to_cpu(mv->a_u32[idx]);
}

int32_t ldms_mval_array_get_s32(ldms_mval_t mv, int idx)
{
	return __le32_to_cpu(mv->a_s32[idx]);
}

uint64_t ldms_mval_array_get_u64(ldms_mval_t mv, int idx)
{
	return __le64_to_cpu(mv->a_u64[idx]);
}

int64_t ldms_mval_array_get_s64(ldms_mval_t mv, int idx)
{
	return __le64_to_cpu(mv->a_s64[idx]);
}

float ldms_mval_array_get_float(ldms_mval_t mv, int idx)
{
#if LDMS_SETH_F_LCLBYTEORDER == LDMS_SETH_F_LE
	return mv->a_f[idx];
#else
	uint32_t tmp = __le32_to_cpu(*(uint32_t*)&mv->a_f[idx]);
	return *(float *)&tmp;
#endif
}

double ldms_mval_array_get_double(ldms_mval_t mv, int idx)
{
#if LDMS_SETH_F_LCLBYTEORDER == LDMS_SETH_F_LE
	return mv->a_d[idx];
#else
	uint64_t tmp = __le64_to_cpu(*(uint64_t*)&mv->a_d[idx]);
	return *(double *)&tmp;
#endif
}

void __list_append(ldms_heap_t heap, ldms_mval_t lh, ldms_mval_t le)
{
	/* list append routine */
	ldms_mval_t prev;
	uint32_t le_off;

	le_off = ldms_heap_off(heap, le);
	if (!lh->v_lh.head) {
		le->v_le.prev = 0;
		lh->v_lh.head = le_off;
		lh->v_lh.count = 1;
	} else {
		prev = ldms_heap_ptr(heap, lh->v_lh.tail);
		prev->v_le.next = le_off;
		le->v_le.prev = lh->v_lh.tail;
		lh->v_lh.count += 1;
	}
	lh->v_lh.tail = le_off;
	le->v_le.next = 0;
}

ldms_mval_t ldms_list_append_item(ldms_set_t s, ldms_mval_t lh, enum ldms_value_type typ, size_t count)
{
	ldms_mval_t le;
	size_t value_sz = __ldms_value_size_get(typ, count);
	if (!ldms_type_is_array(typ))
		count = 1;
	le = ldms_heap_alloc(s->heap, value_sz + sizeof(struct ldms_list_entry));
	if (!le) {
		errno = ENOMEM;
		return NULL;
	}
	memset(le->v_le.value, 0, value_sz);
	le->v_le.type = typ;
	le->v_le.count = count;
	__list_append(s->heap, lh, le);
	LDMS_GN_INCREMENT(s->data->gn);
	return (ldms_mval_t)le->v_le.value;
}

size_t ldms_list_len(ldms_set_t s, ldms_mval_t lh)
{
	return lh->v_lh.count;
}

ldms_mval_t ldms_list_first(ldms_set_t s, ldms_mval_t lh, enum ldms_value_type *typ, size_t *count)
{
	ldms_mval_t le;
	if (!lh->v_lh.head)
		return NULL;
	if (!s->heap)
		return NULL;
	le = ldms_heap_ptr(s->heap, lh->v_lh.head);
	if (typ)
		*typ = le->v_le.type;
	if (count)
		*count = le->v_le.count;
	return (ldms_mval_t)le->v_le.value;
}

ldms_mval_t ldms_list_next(ldms_set_t s, ldms_mval_t v, enum ldms_value_type *typ, size_t *count)
{
	ldms_mval_t le;

	le = (ldms_mval_t)(&v->v_le - 1);
	if (!le->v_le.next)
		return NULL;
	le = ldms_heap_ptr(s->heap, le->v_le.next);
	if (typ)
		*typ = le->v_le.type;
	if (count)
		*count = le->v_le.count;
	return (ldms_mval_t)le->v_le.value;
}

int ldms_list_remove_item(ldms_set_t s, ldms_mval_t lh, ldms_mval_t v)
{
	ldms_mval_t le, prev, next;

	le = (ldms_mval_t)(&v->v_le - 1);
	prev = ldms_heap_ptr(s->heap, le->v_le.prev);
	next = ldms_heap_ptr(s->heap, le->v_le.next);
	if (prev)
		prev->v_le.next = le->v_le.next;
	else /* first element */
		lh->v_lh.head = le->v_le.next;
	if (next)
		next->v_le.prev = le->v_le.prev;
	else /* last element */
		lh->v_lh.tail = le->v_le.prev;
	if (le->v_le.type == LDMS_V_LIST) {
		ldms_list_purge(s, v);
	}
	ldms_heap_free(s->heap, le);
	lh->v_lh.count--;
	return 0;
}

int ldms_list_purge(ldms_set_t s, ldms_mval_t lh)
{
	ldms_mval_t m;
	while ((m = ldms_list_first(s, lh, NULL, NULL))) {
		ldms_list_remove_item(s, lh, m);
	}
	return 0;
}

int ldms_transaction_begin(ldms_set_t s)
{
	struct ldms_data_hdr *dh, *dh_prev;
	struct timeval tv;
	int i, n;
	void *base;
	size_t data_sz, heap_sz;
	uint32_t set_off;

	pthread_mutex_lock(&s->lock);
	n = __le32_to_cpu(s->meta->array_card);
	if (n == 1)
		goto record_time;
	dh_prev = __ldms_set_array_get(s, s->curr_idx);
	s->curr_idx = (s->curr_idx + 1) % n;
	dh = __ldms_set_array_get(s, s->curr_idx);
	/*   NOTE: heap.size set since ldms_set_new() */
	heap_sz = dh->heap.size;
	data_sz = dh->size - heap_sz;
	base = ((void*)dh) + data_sz;
	set_off = dh->set_off; /* preserve set offset information */
	if (s->flags & LDMS_SET_F_DATA_COPY) {
		/* copy data */
		memcpy(dh, dh_prev, dh->size);
	} else if (dh->heap.gn != dh_prev->heap.gn) {
		/* heap structure changes, need to copy both data and heap as
		 * the listhead lives in the data part. */
		memcpy(dh, dh_prev, dh->size);
	} else {
		*dh = *dh_prev; /* copy only the header contents */
	}
	dh->set_off = set_off;
	/* update s->data and s->heap handles */
	s->data = dh;
	if (s->meta->heap_sz)
		s->heap = ldms_heap_get(&s->heap_inst, &dh->heap, base);
	/* update curr_idx in all headers */
	for (i = 0; i < n; i++) {
		dh = __ldms_set_array_get(s, i);
		dh->curr_idx = __cpu_to_le32(s->curr_idx);
	}
 record_time:
	s->data->trans.flags = LDMS_TRANSACTION_BEGIN;
	(void)gettimeofday(&tv, NULL);
	s->data->trans.ts.sec = __cpu_to_le32(tv.tv_sec);
	s->data->trans.ts.usec = __cpu_to_le32(tv.tv_usec);
	pthread_mutex_unlock(&s->lock);
	return 0;
}

int ldms_transaction_end(ldms_set_t s)
{
	struct ldms_data_hdr *dh;
	struct timeval tv;

	pthread_mutex_lock(&s->lock);
	(void)gettimeofday(&tv, NULL);
	dh = __ldms_set_array_get(s, s->curr_idx);
	dh->trans.dur.sec = tv.tv_sec - __le32_to_cpu(dh->trans.ts.sec);
	dh->trans.dur.usec = tv.tv_usec - __le32_to_cpu(dh->trans.ts.usec);
	if (((int32_t)dh->trans.dur.usec) < 0) {
		dh->trans.dur.sec -= 1;
		dh->trans.dur.usec += 1000000;
	}
	dh->trans.dur.sec = __cpu_to_le32(dh->trans.dur.sec);
	dh->trans.dur.usec = __cpu_to_le32(dh->trans.dur.usec);
	dh->trans.ts.sec = __cpu_to_le32(tv.tv_sec);
	dh->trans.ts.usec = __cpu_to_le32(tv.tv_usec);
	dh->trans.flags = LDMS_TRANSACTION_END;
	LDMS_GN_INCREMENT(s->data->gn);
	pthread_mutex_unlock(&s->lock);
	__ldms_xprt_push(s, LDMS_RBD_F_PUSH_CHANGE);
	return 0;
}

struct ldms_timestamp ldms_transaction_timestamp_get(ldms_set_t s)
{
	struct ldms_data_hdr *dh = s->data;
	struct ldms_timestamp ts = {
		.sec = __le32_to_cpu(dh->trans.ts.sec),
		.usec = __le32_to_cpu(dh->trans.ts.usec),
	};
	return ts;
}

struct ldms_timestamp ldms_transaction_duration_get(ldms_set_t s)
{
	struct ldms_data_hdr *dh = s->data;
	struct ldms_timestamp ts = {
		.sec = __le32_to_cpu(dh->trans.dur.sec),
		.usec = __le32_to_cpu(dh->trans.dur.usec),
	};
	return ts;
}

double ldms_difftimestamp(const struct ldms_timestamp *after, const struct ldms_timestamp *before)
{
	uint32_t dms;
	if (!before || !after)
		return -1.0;
	struct ldms_timestamp t2 = *after;
	if (before->usec > t2.usec) {
		dms = (1000000 + t2.usec) - before->usec;
		t2.sec--;
	} else {
		dms = t2.usec - before->usec;
	}
	uint32_t ds = t2.sec - before->sec;
	if (ds > t2.sec) {
		return 0;
	}

	return (double)ds + 1.0e-6*dms;
};

int ldms_set_is_consistent(ldms_set_t s)
{
	struct ldms_data_hdr *dh = s->data;
	return (dh->trans.flags == LDMS_TRANSACTION_END);
}

void ldms_xprt_cred_get(ldms_t x, ldms_cred_t lcl, ldms_cred_t rmt)
{
	if (lcl) {
		lcl->uid = x->luid;
		lcl->gid = x->lgid;
	}

	if (rmt) {
		rmt->uid = x->ruid;
		rmt->gid = x->rgid;
	}
}

void ldms_local_cred_get(ldms_t x, ldms_cred_t lcl)
{
	ldms_auth_cred_get(x->auth, lcl);
}

/* The caller must hold the set lock */
/*
 * return 0 if there are changes. Otherwise, -1 is returned.
 *        errno is returned on error.
 */
int __ldms_set_info_set(struct ldms_set_info_list *info,
			const char *key, const char *value)
{
	struct ldms_set_info_pair *pair;

	LIST_FOREACH(pair, info, entry) {
		if (0 == strcmp(key, pair->key)) {
			if (0 != strcmp(value, pair->value)) {
				/* reset value */
				free(pair->value);
				goto set_value;
			} else {
				/* no changes */
				return -1;
			}
		}
	}
	/* new key-value pair */
	pair = malloc(sizeof(*pair));
	if (!pair)
		return ENOMEM;

	pair->key = strdup(key);
	if (!pair->key) {
		free(pair);
		return ENOMEM;
	}
	LIST_INSERT_HEAD(info, pair, entry);

set_value:
	pair->value = strdup(value);
	if (!pair->value) {
		LIST_REMOVE(pair, entry);
		free(pair->key);
		free(pair);
		return ENOMEM;
	}
	return 0;
}

int ldms_set_info_set(ldms_set_t s, const char *key, const char *value)
{
	int rc = 0;
	if (!key)
		return EINVAL;

	if (!value)
		return EINVAL;

	pthread_mutex_lock(&s->lock);
	rc = __ldms_set_info_set(&s->local_info, key, value);
	if (rc > 0) {
		/* error */
		goto out;
	}
	if (rc == -1) {
		/* no changes .. nothing else to do */
		rc = 0;
		goto out;
	}
	if (s->flags & LDMS_SET_F_PUBLISHED)
		__ldms_dir_upd_set(s);
out:
	pthread_mutex_unlock(&s->lock);
	return rc;
}

/* The caller must hold the set lock. */
struct ldms_set_info_pair *__ldms_set_info_find(struct ldms_set_info_list *info,
								const char *key)
{
	struct ldms_set_info_pair *pair;
	LIST_FOREACH(pair, info, entry)
		if (0 == strcmp(key, pair->key))
			return pair;
	return NULL;
}

void __ldms_set_info_unset(struct ldms_set_info_pair *pair)
{
	LIST_REMOVE(pair, entry);
	free(pair->key);
	free(pair->value);
	free(pair);
}

void ldms_set_info_unset(ldms_set_t s, const char *key)
{
	struct ldms_set_info_pair *pair;
	pthread_mutex_lock(&s->lock);
	pair = __ldms_set_info_find(&s->local_info, key);
	if (!pair) {
		pthread_mutex_unlock(&s->lock);
		return;
	}
	__ldms_set_info_unset(pair);
	if (s->flags & LDMS_SET_F_PUBLISHED)
		__ldms_dir_upd_set(s);
	pthread_mutex_unlock(&s->lock);
}

char *ldms_set_info_get(ldms_set_t s, const char *key)
{
	struct ldms_set_info_pair *pair;
	char *value = NULL;

	pthread_mutex_lock(&s->lock);
	pair = __ldms_set_info_find(&s->local_info, key);
	if (pair) {
		value = strdup(pair->value);
		goto out;
	}

	pair = __ldms_set_info_find(&s->remote_info, key);
	if (!pair)
		goto out;
	value = strdup(pair->value);
out:
	pthread_mutex_unlock(&s->lock);
	return value;
}

char *ldms_dir_set_info_get(ldms_dir_set_t dset, const char *key)
{
	int i;
	for (i = 0; i < dset->info_count; i++) {
		if (0 == strcmp(key, dset->info[i].key))
			return dset->info[i].value;
	}
	return NULL;
}

int ldms_set_info_traverse(ldms_set_t s, ldms_set_info_traverse_cb_fn cb,
							int flag, void *cb_arg)
{
	struct ldms_set_info_pair *pair;
	struct ldms_set_info_list *list;
	int rc = 0;
	pthread_mutex_lock(&s->lock);
	if (flag == LDMS_SET_INFO_F_LOCAL) {
		list = &s->local_info;
	} else if (flag == LDMS_SET_INFO_F_REMOTE) {
		list = &s->remote_info;
	} else {
		rc = EINVAL;
		goto out;
	}

	LIST_FOREACH(pair, list, entry) {
		rc = cb(pair->key, pair->value, cb_arg);
		if (rc)
			goto out;
	}
out:
	pthread_mutex_unlock(&s->lock);
	return rc;
}

void ldms_version_get(struct ldms_version *v)
{
	LDMS_VERSION_SET(*v);
}

int ldms_version_check(const struct ldms_version *v)
{
	return LDMS_VERSION_EQUAL(*v);
}

int ldms_mval_parse_scalar(ldms_mval_t v, enum ldms_value_type vt, const char *str)
{
	int num = 0;
	errno = 0;
	char *endp = NULL;
	switch (vt) {
	case LDMS_V_CHAR_ARRAY:
	case LDMS_V_U8_ARRAY:
	case LDMS_V_U16_ARRAY:
	case LDMS_V_U32_ARRAY:
	case LDMS_V_U64_ARRAY:
	case LDMS_V_S8_ARRAY:
	case LDMS_V_S16_ARRAY:
	case LDMS_V_S32_ARRAY:
	case LDMS_V_S64_ARRAY:
	case LDMS_V_F32_ARRAY:
	case LDMS_V_D64_ARRAY:
		return EINVAL;
	case LDMS_V_CHAR:
		v->v_char = str[0];
		num = 1;
		break;
	case LDMS_V_U8:
		num = sscanf(str, "%" SCNu8, &(v->v_u8));
		break;
	case LDMS_V_U16:
		num = sscanf(str, "%" SCNu16, &(v->v_u16));
		break;
	case LDMS_V_U32:
		num = sscanf(str, "%" SCNu32, &(v->v_u32));
		break;
	case LDMS_V_U64:
		num = sscanf(str, "%" SCNu64, &(v->v_u64));
		break;
	case LDMS_V_S8:
		num = sscanf(str, "%" SCNi8, &(v->v_s8));
		break;
	case LDMS_V_S16:
		num = sscanf(str, "%" SCNi16, &(v->v_s16));
		break;
	case LDMS_V_S32:
		num = sscanf(str, "%" SCNi32, &(v->v_s32));
		break;
	case LDMS_V_S64:
		num = sscanf(str, "%" SCNi64, &(v->v_s64));
		break;
	case LDMS_V_F32:
		v->v_f = strtof(str, &endp);
		if (errno || endp == str)
			num = 0;
		else
			num = 1;
		break;
	case LDMS_V_D64:
		v->v_d = strtod(str, &endp);
		if (errno || endp == str)
			num = 0;
		else
			num = 1;
		break;
	default:
		return EINVAL;
	}
	if (num != 1)
		return EINVAL;
	return 0;
}

#define DELETE_TIMEOUT	(60)	/* 1 minute */
#define DELETE_CHECK	(15)

extern int ldms_xprt_connected(struct ldms_xprt *x);
static void *delete_proc(void *arg)
{
	struct rbn *rbn, *prev_rbn, *xrbn;
	struct ldms_set *set;
	struct ldms_lookup_peer *lp;
	struct ldms_push_peer *pp;
	ldms_name_t name;
	time_t dur;
	ldms_t x;
	struct ldms_context *ctxt;
	char *to = getenv("LDMS_DELETE_TIMEOUT");
	int timeout = (to ? atoi(to) : DELETE_TIMEOUT);
	if (timeout <= DELETE_CHECK)
		timeout = DELETE_CHECK;
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	do {
		/*
		 * Iterate through the tree from oldest to
		 * newest. Delete any set older than the threshold
		 */
		pthread_mutex_lock(&__del_tree_lock);
		rbn = rbt_max(&__del_tree);
		while (rbn) {
			prev_rbn = rbn_pred(rbn);
			set = container_of(rbn, struct ldms_set, del_node);
			name = get_instance_name(set->meta);
			dur = time(NULL) - set->del_time;
			fprintf(stderr,
				"Dangling set %s with reference count %d, "
				"waiting %jd seconds\n",
				name->name, set->ref.ref_count, dur);
			fflush(stderr);
			if (dur < timeout)
				break;

			/*
			 * Look for a connected push/lookup peer.
			 * If there is a connected peer, we skip the set
			 * and wait for the SET_DELETE reply from the connected peers.
			 *
			 * This prevents the race between the SET_DELETE timeout
			 * and the SET_DELETE reply.
			 */
			pthread_mutex_lock(&set->lock);
			xrbn = rbt_min(&set->lookup_coll);
			while (xrbn) {
				lp = container_of(xrbn, struct ldms_lookup_peer, rbn);
				if (ldms_xprt_connected(lp->xprt)) {
					/*
					 * A push peer is connected... skip the set.
					 */
					pthread_mutex_unlock(&set->lock);
					goto next;
				}
				xrbn = rbn_succ(xrbn);
			}
			xrbn = rbt_min(&set->push_coll);
			while (xrbn) {
				pp = container_of(xrbn, struct ldms_push_peer, rbn);
				if (ldms_xprt_connected(pp->xprt)) {
					/*
					 * A lookup peer is connected ... skip the set.
					 */
					pthread_mutex_unlock(&set->lock);
					goto next;
				}
			}
			pthread_mutex_unlock(&set->lock);
			fprintf(stderr,
				"Deleting dangling set %s with reference "
				"count %d, waited %jd seconds\n",
				name->name, set->ref.ref_count, dur);
			ref_dump(&set->ref, __func__, stderr);

			/*
			 * Since all peers have disconnected, the push and lookup
			 * collections should be empty at this point.
			 *
			 * Below logic is for a corner case and makes sure
			 * that we are not leaking and leaving the transport dangling.
			 */
			/* Clean up the push peer collection */
			while ((rbn = rbt_min(&set->push_coll))) {
				rbt_del(&set->push_coll, rbn);
				pp = container_of(rbn, struct ldms_push_peer, rbn);
				if (!pp->xprt)
					goto free_pp;
				x = pp->xprt;
				pthread_mutex_lock(&x->lock);
				TAILQ_FOREACH(ctxt, &x->ctxt_list, link) {
					switch (ctxt->type) {
					case LDMS_CONTEXT_LOOKUP_READ:
						if (ctxt->lu_read.s == set)
							ctxt->lu_read.s = NULL;
						break;
					case LDMS_CONTEXT_UPDATE:
					case LDMS_CONTEXT_UPDATE_META:
						if (ctxt->update.s == set)
							ctxt->update.s = NULL;
						break;
					case LDMS_CONTEXT_REQ_NOTIFY:
						if (ctxt->req_notify.s == set)
							ctxt->req_notify.s = NULL;
						break;
					case LDMS_CONTEXT_SET_DELETE:
						if (ctxt->set_delete.s == set)
							ctxt->set_delete.s = NULL;
						break;
					default:
						break;
					}
				}
				pthread_mutex_unlock(&x->lock);
				ldms_xprt_put(x);
			free_pp:
				free(pp);
			}

			/*
			 * Clean up the lookup peer collection and
			 * remove set from the transport's set collection.
			 */
			while ((rbn = rbt_min(&set->lookup_coll))) {
				rbt_del(&set->lookup_coll, rbn);
				lp = container_of(rbn, struct ldms_lookup_peer, rbn);
				if (!lp->xprt)
					goto free_lp;
				x = lp->xprt;
				pthread_mutex_lock(&x->lock);
				TAILQ_FOREACH(ctxt, &x->ctxt_list, link) {
					switch (ctxt->type) {
					case LDMS_CONTEXT_LOOKUP_READ:
						if (ctxt->lu_read.s == set)
							ctxt->lu_read.s = NULL;
						break;
					case LDMS_CONTEXT_UPDATE:
					case LDMS_CONTEXT_UPDATE_META:
						if (ctxt->update.s == set)
							ctxt->update.s = NULL;
						break;
					case LDMS_CONTEXT_REQ_NOTIFY:
						if (ctxt->req_notify.s == set)
							ctxt->req_notify.s = NULL;
						break;
					case LDMS_CONTEXT_SET_DELETE:
						if (ctxt->set_delete.s == set)
							ctxt->set_delete.s = NULL;
						break;
					default:
						break;
					}
				}
				pthread_mutex_unlock(&x->lock);
				ldms_xprt_put(x);
			free_lp:
				free(lp);
			}

			__destroy_set_no_lock(set);
		next:
			rbn = prev_rbn;
			fflush(stderr);
		}
		pthread_mutex_unlock(&__del_tree_lock);
		sleep(DELETE_CHECK);
	} while (1);
	return NULL;
}

static void __attribute__ ((destructor)) cs_term(void)
{
	void *dontcare;
	if (__atomic_load_n(&delete_thread_initialized, __ATOMIC_SEQ_CST)) {
		(void)pthread_cancel(delete_thread);
		(void)pthread_join(delete_thread, &dontcare);
	}
}

ldms_mval_t ldms_record_alloc(ldms_set_t set, int metric_id)
{
	/* record object format:  [ ldms_list_entry | ldms_record_inst ] */
	ldms_mdesc_t vd;
	ldms_mval_t mval;
	ldms_list_entry_t list_ent;
	ldms_record_type_t rec_type;
	ldms_record_inst_t rec_inst;
	size_t sz;
	mval = __mval_to_get(set, metric_id, &vd);
	if (vd->vd_type != LDMS_V_RECORD_TYPE)
		goto einval;
	rec_type = &mval->v_rec_type;
	sz = __le32_to_cpu(rec_type->inst_sz) + sizeof(struct ldms_list_entry);
	list_ent = ldms_heap_alloc(set->heap, sz);
	if (!list_ent)
		goto enomem;
	list_ent->next = list_ent->prev = 0;
	list_ent->type = LDMS_V_RECORD_INST;
	list_ent->count = __cpu_to_le32(1);
	rec_inst = (void*)list_ent->value;
	rec_inst->hdr.flags = __cpu_to_le32(LDMS_RECORD_F_INST);
	rec_inst->record_type = __cpu_to_le32(metric_id);
	rec_inst->set_data_off = __cpu_to_le32(ldms_off_(set->data, rec_inst));
	return (void*)rec_inst;

 einval:
	errno = EINVAL;
	return NULL;

 enomem:
	errno = ENOMEM;
	return NULL;
}


static inline struct ldms_record_type *
__rec_type(struct ldms_record_inst *rec_inst, ldms_mdesc_t *mdesc,
	   struct ldms_set_hdr **meta_out, struct ldms_data_hdr **data_out)
{
	struct ldms_set_hdr *meta;
	struct ldms_data_hdr *data;
	ldms_mdesc_t vd;
	struct ldms_record_type *rec_type;
	int type_idx = __le32_to_cpu(rec_inst->record_type);

	if (!rec_inst)
		return NULL;
	data = ((void*)rec_inst) - __le32_to_cpu(rec_inst->set_data_off);
	meta = ((void*)data) - __le32_to_cpu(data->set_off);
	if (!LDMS_VERSION_EQUAL(meta->version))
		return NULL;
	vd = (void*)meta + __le32_to_cpu(meta->dict[type_idx]);
	if ((vd->vd_type != LDMS_V_RECORD_TYPE)         ||
	    (0 == (vd->vd_flags & LDMS_MDESC_F_RECORD)) ||
	    (0 == (vd->vd_flags & LDMS_MDESC_F_META)))
		return NULL;
	if (mdesc)
		*mdesc = vd;
	if (meta_out)
		*meta_out = meta;
	if (data_out)
		*data_out = data;
	rec_type = (void*)meta + __le32_to_cpu(vd->vd_data_offset);
	return rec_type;
}

static inline ldms_mdesc_t __record_mdesc_get(ldms_mval_t rec, int i)
{
	ldms_record_type_t rec_type;
	ldms_record_hdr_t hdr = (ldms_record_hdr_t)&rec->v_rec_inst;
	if (hdr->flags == LDMS_RECORD_F_TYPE) {
		rec_type = &rec->v_rec_type;
	} else {
		rec_type = __rec_type((void*)rec, NULL, NULL, NULL);
	}
	if (i < 0 || __le32_to_cpu(rec_type->n) <= i) {
		errno = ENOENT;
		return NULL;
	}
	return ldms_ptr_(void, rec_type, __le32_to_cpu(rec_type->dict[i]));
}

int ldms_record_type_get(ldms_mval_t mval)
{
	ldms_record_type_t rec_type;
	ldms_mdesc_t mdesc;
	ldms_record_inst_t rec_inst = &mval->v_rec_inst;
	if (rec_inst->hdr.flags != LDMS_RECORD_F_INST)
		return -EINVAL;
	rec_type = __rec_type(rec_inst, &mdesc, NULL, NULL);
	if (!rec_type || mdesc->vd_type != LDMS_V_RECORD_TYPE)
		return -EINVAL;
	return __le32_to_cpu(rec_inst->record_type);
}

int ldms_record_metric_find(ldms_mval_t mval, const char *name)
{
	ldms_record_hdr_t hdr = (ldms_record_hdr_t)&mval->v_rec_inst;
	ldms_record_inst_t rec_inst;
	ldms_mdesc_t vd, type_vd;
	ldms_record_type_t rec_type;
	int i, n;

	if (hdr->flags == LDMS_RECORD_F_TYPE) {
		rec_type = &mval->v_rec_type;
	} else {
		rec_inst = &mval->v_rec_inst;
		rec_type = __rec_type(rec_inst, &type_vd, NULL, NULL);
		if (!rec_type || type_vd->vd_type != LDMS_V_RECORD_TYPE) {
			assert(0 == "Not a record instance");
			return -EINVAL;
		}
	}
	n = __le32_to_cpu(rec_type->n);
	for (i = 0; i < n; i++) {
		vd = ldms_ptr_(void, rec_type, __le32_to_cpu(rec_type->dict[i]));
		if (0 != strcmp(name, vd->vd_name_unit))
			continue;
		return i;
	}
	return -ENOENT;
}

int ldms_schema_record_add(ldms_schema_t s, ldms_record_t rec_def)
{
	ldms_mdef_t m;
	/* check if the name is a duplicate */
	STAILQ_FOREACH(m, &s->metric_list, entry) {
		if (!strcmp(m->name, rec_def->mdef.name))
			return -EEXIST;
	}
	if (rec_def->metric_id >= 0) {
		return -EBUSY; /* already added to a schema */
	}
	rec_def->metric_id = __schema_mdef_add(s, &rec_def->mdef);
	rec_def->schema = s;
	s->meta_sz += rec_def->type_sz;
	return rec_def->metric_id;
}

int ldms_schema_record_array_add(ldms_schema_t s, const char *name,
				 ldms_record_t rec_def, int array_len)
{
	size_t data_sz;
	if (rec_def->metric_id < 0)
		return -EINVAL;
	/* check if the name is a duplicate */
	ldms_mdef_t m;
	ldms_record_array_def_t ra_def;
	STAILQ_FOREACH(m, &s->metric_list, entry) {
		if (!strcmp(m->name, name))
			return -EEXIST;
	}
	ra_def = calloc(1, sizeof *ra_def);
	if (!ra_def)
		return -ENOMEM;
	ra_def->mdef.name = strdup(name);
	if (!ra_def->mdef.name)
		goto enomem;
	ra_def->mdef.unit = NULL;
	ra_def->mdef.type = LDMS_V_RECORD_ARRAY;
	ra_def->mdef.flags = LDMS_MDESC_F_DATA;
	ra_def->mdef.count = array_len;
	/* this only calculate meta_sz (for name and mdesc) */
	__ldms_metric_size_get(name, NULL, LDMS_V_RECORD_ARRAY,
			ra_def->mdef.count, &ra_def->mdef.meta_sz,
			&ra_def->mdef.data_sz);
	/* we need to re-calculate the data_sz as the info provided to the
	 * generic size calculation is not enough. */
	data_sz = sizeof(struct ldms_record_array) + array_len*rec_def->inst_sz;
	ra_def->mdef.data_sz = roundup(data_sz, 8);
	ra_def->inst_sz = rec_def->inst_sz;
	ra_def->rec_type = rec_def->metric_id;

	return __schema_mdef_add(s, &ra_def->mdef);

enomem:
	free(ra_def->mdef.name);
	free(ra_def);
	return -ENOMEM;
}

ldms_record_t ldms_record_create(const char *name)
{
	ldms_record_t rec_def;
	if (!name) {
		errno = EINVAL;
		return NULL;
	}
	rec_def = calloc(1, sizeof(*rec_def));
	if (!rec_def)
		return NULL;
	rec_def->mdef.name = strdup(name);
	if (!rec_def->mdef.name)
		goto err;
	rec_def->inst_sz = sizeof(struct ldms_record_inst);
	rec_def->mdef.count = 0;
	rec_def->mdef.flags = LDMS_MDESC_F_META|LDMS_MDESC_F_RECORD;
	__ldms_metric_size_get(name, NULL, LDMS_V_RECORD_TYPE, 0,
			&rec_def->mdef.meta_sz,
			&rec_def->mdef.data_sz);
	rec_def->mdef.type = LDMS_V_RECORD_TYPE;
	rec_def->mdef.unit = NULL;
	rec_def->schema = NULL;
	rec_def->n = 0;
	rec_def->metric_id = -1;
	STAILQ_INIT(&rec_def->rec_metric_list);
	return rec_def;
 err:
	if (rec_def) {
		if (rec_def->mdef.name)
			free(rec_def->mdef.name);
		free(rec_def);
	}
	return NULL;
}

int ldms_record_metric_add(ldms_record_t rec_def, const char *name,
			   const char *unit, enum ldms_value_type type,
			   size_t count)
{
	ldms_mdef_t mdef;
	int idx;
	size_t tsz;

	mdef = calloc(1, sizeof(*mdef));
	if (!mdef)
		return -ENOMEM;
        if (name != NULL) {
                mdef->name = strdup(name);
                if (!mdef->name)
                        goto err_1;
        } else {
                errno = EINVAL;
                goto err_1;
        }
        if (unit != NULL) {
                mdef->unit = strdup(unit);
                if (!mdef->unit)
                        goto err_2;
        }
	mdef->type = type;
	mdef->count = count;

	mdef->flags = LDMS_MDESC_F_DATA|LDMS_MDESC_F_RECORD;

	__ldms_metric_size_get(name, unit, type, count,
			&mdef->meta_sz, &mdef->data_sz);

	idx = rec_def->n++;
	STAILQ_INSERT_TAIL(&rec_def->rec_metric_list, mdef, entry);

	tsz = mdef->meta_sz + sizeof(int) /* dict[i] */;

	/* metric metadata is a part of record type data which is also located
	 * in the set metadata section. */
	rec_def->mdef.data_sz += tsz;
	if (rec_def->schema)
		rec_def->schema->meta_sz += tsz;
	rec_def->type_sz += tsz;
	rec_def->inst_sz += mdef->data_sz;

	/* size of the record instance will be calculated in ldms_set_new(). */
	return idx;
 err_2:
	free(mdef->name);
 err_1:
	free(mdef);
	return -errno;
}

size_t ldms_record_heap_size_get(ldms_record_t rec_def)
{
	size_t sz = rec_def->inst_sz + sizeof(struct ldms_list_entry);
	sz = ldms_heap_alloc_size(LDMS_LIST_GRAIN, sz);
	return sz;
}

size_t ldms_record_value_size_get(ldms_record_t rec_def)
{
	return rec_def->inst_sz - sizeof(struct ldms_record_inst);
}

int ldms_record_card(ldms_mval_t rec)
{
	struct ldms_record_hdr *hdr = (struct ldms_record_hdr *)&rec->v_rec_inst;
	ldms_record_type_t rec_type;
	if (hdr->flags & LDMS_RECORD_F_INST) {
		rec_type = __rec_type((void*)rec, NULL, NULL, NULL);
		if (!rec_type)
			return -EINVAL;
	} else {
		rec_type = &rec->v_rec_type;
	}
	return __le32_to_cpu(rec_type->n);
}

static inline
ldms_mval_t __record_metric_get(ldms_mval_t rec_inst, int i,
				ldms_record_type_t *type_out,
				ldms_mdesc_t *vd_out,
				struct ldms_set_hdr **meta_out,
				struct ldms_data_hdr **data_out)
{
	ldms_mval_t mval;
	ldms_mdesc_t vd;
	ldms_record_type_t rec_type;
	ldms_record_hdr_t hdr = (ldms_record_hdr_t)&rec_inst->v_rec_inst;
	if (hdr->flags != LDMS_RECORD_F_INST) {
		errno = EINVAL;
		return NULL;
	}
	rec_type = __rec_type((void*)rec_inst, NULL, meta_out, data_out);
	if (!rec_type)
		return NULL;
	if (type_out)
		*type_out = rec_type;
	vd = __record_mdesc_get(rec_inst, i);
	if (vd_out)
		*vd_out = vd;
	mval = ldms_ptr_(void, rec_inst, __le32_to_cpu(vd->vd_data_offset));
	return mval;
}

ldms_mval_t ldms_record_metric_get(ldms_mval_t rec_inst, int metric_id)
{
	return __record_metric_get(rec_inst, metric_id, NULL, NULL, NULL, NULL);
}

const char *ldms_record_metric_name_get(ldms_mval_t rec, int metric_id)
{
	ldms_mdesc_t vd;
	vd = __record_mdesc_get(rec, metric_id);
	if (!vd)
		return NULL;
	return vd->vd_name_unit;
}

const char *ldms_record_metric_unit_get(ldms_mval_t rec, int metric_id)
{
	ldms_mdesc_t vd;
	int len;
	vd = __record_mdesc_get(rec, metric_id);
	if (!vd)
		return NULL;
	len = strlen(vd->vd_name_unit);
	if (len + 1 < vd->vd_name_unit_len) {
		return &vd->vd_name_unit[len + 1];
	}
	return NULL;
}

enum ldms_value_type ldms_record_metric_type_get(ldms_mval_t rec,
						 int metric_id, size_t *count)
{
	ldms_mdesc_t vd;
	vd = __record_mdesc_get(rec, metric_id);
	if (!vd)
		return LDMS_V_NONE;

	if (count) {
		*count = __le32_to_cpu(vd->vd_array_count);
	}
	return vd->vd_type;
}

void ldms_record_metric_set(ldms_mval_t rec_inst, int metric_id,
			    ldms_mval_t val)
{
	ldms_mdesc_t vd;
	struct ldms_data_hdr *data;
	ldms_record_type_t rec_type;
	ldms_mval_t mval = __record_metric_get(rec_inst, metric_id, &rec_type,
						&vd, NULL, &data);
	if (!mval)
		return;
	__metric_set(vd, mval, val);
	LDMS_GN_INCREMENT(data->gn);
}

void ldms_record_metric_array_set(ldms_mval_t rec_inst, int metric_id,
				  ldms_mval_t val, int start,
				  int count)
{
	ldms_mdesc_t vd;
	struct ldms_data_hdr *data;
	ldms_record_type_t rec_type;
	int i;
	ldms_mval_t mval = __record_metric_get(rec_inst, metric_id, &rec_type,
						&vd, NULL, &data);
	if (!mval)
		return;
	for (i = start; i < start+count; i++) {
		__metric_array_set(vd, mval, i, val);
	}
	LDMS_GN_INCREMENT(data->gn);
}

int ldms_list_append_record(ldms_set_t set, ldms_mval_t lh, ldms_mval_t rec_inst)
{
	ldms_record_hdr_t hdr = (ldms_record_hdr_t)&rec_inst->v_rec_inst;
	if (hdr->flags != LDMS_RECORD_F_INST)
		return EINVAL;
	ldms_list_entry_t le = container_of(rec_inst, struct ldms_list_entry, value);
	struct ldms_data_hdr *data;
	if (le->prev || le->next)
		return EBUSY;
	data = (void*)rec_inst - __le32_to_cpu(rec_inst->v_rec_inst.set_data_off);
	__list_append(set->heap, lh, (ldms_mval_t)le);
	/* bump heap gn so that the change in the linked list will be propagated
	 * to the next set buffer. */
	data->heap.gn = __cpu_to_le32(__le32_to_cpu(data->heap.gn)+1);
	return 0;
}

char ldms_record_get_char(ldms_mval_t rec_inst, int i)
{
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, NULL, NULL, NULL);
	assert(mv);
	if (mv)
		return mv->v_char;
	return 0;
}

uint8_t ldms_record_get_u8(ldms_mval_t rec_inst, int i)
{
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, NULL, NULL, NULL);
	assert(mv);
	if (mv)
		return mv->v_u8;
	return 0;
}

uint16_t ldms_record_get_u16(ldms_mval_t rec_inst, int i)
{
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, NULL, NULL, NULL);
	assert(mv);
	if (mv)
		return __le16_to_cpu(mv->v_u16);
	return 0;
}

uint32_t ldms_record_get_u32(ldms_mval_t rec_inst, int i)
{
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, NULL, NULL, NULL);
	assert(mv);
	if (mv)
		return __le32_to_cpu(mv->v_u32);
	return 0;
}

uint64_t ldms_record_get_u64(ldms_mval_t rec_inst, int i)
{
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, NULL, NULL, NULL);
	assert(mv);
	if (mv)
		return __le64_to_cpu(mv->v_u64);
	return 0;
}

int8_t ldms_record_get_s8(ldms_mval_t rec_inst, int i)
{
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, NULL, NULL, NULL);
	assert(mv);
	if (mv)
		return mv->v_s8;
	return 0;
}

int16_t ldms_record_get_s16(ldms_mval_t rec_inst, int i)
{
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, NULL, NULL, NULL);
	assert(mv);
	if (mv)
		return __le16_to_cpu(mv->v_s16);
	return 0;
}

int32_t ldms_record_get_s32(ldms_mval_t rec_inst, int i)
{
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, NULL, NULL, NULL);
	assert(mv);
	if (mv)
		return __le32_to_cpu(mv->v_s32);
	return 0;
}

int64_t ldms_record_get_s64(ldms_mval_t rec_inst, int i)
{
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, NULL, NULL, NULL);
	assert(mv);
	if (mv)
		return __le64_to_cpu(mv->v_s64);
	return 0;
}

float ldms_record_get_float(ldms_mval_t rec_inst, int i)
{
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, NULL, NULL, NULL);
	assert(mv);
	if (mv) {
#if LDMS_SETH_F_LCLBYTEORDER == LDMS_SETH_F_LE
		return mv->v_f;
#else
		uint32_t tmp = __le32_to_cpu(*(uint32_t*)&mv->v_f);
		return *(float *)&tmp;
#endif
	}
	return 0;
}

double ldms_record_get_double(ldms_mval_t rec_inst, int i)
{
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, NULL, NULL, NULL);
	if (mv) {
#if LDMS_SETH_F_LCLBYTEORDER == LDMS_SETH_F_LE
		return mv->v_d;
#else
		uint64_t tmp = __le64_to_cpu(*(uint64_t*)&mv->v_d);
		return *(double *)&tmp;
#endif
	} else
		assert(0 == "Invalid metric index");
	return 0;
}

const char *ldms_record_array_get_str(ldms_mval_t rec_inst, int i)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, &desc, NULL, NULL);
	if (mv)
		return mv->a_char;
	else
		assert(0 == "Invalid metric index");
	return 0;
}

char ldms_record_array_get_char(ldms_mval_t rec_inst, int i, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, &desc, NULL, NULL);
	if (mv && 0 <= idx && idx < __le32_to_cpu(desc->vd_array_count))
		return mv->a_char[idx];
	else
		assert(0 == "Invalid metric or array index");
	return 0;
}

uint8_t ldms_record_array_get_u8(ldms_mval_t rec_inst, int i, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, &desc, NULL, NULL);
	if (mv && 0 <= idx && idx < __le32_to_cpu(desc->vd_array_count))
		return mv->a_u8[idx];
	else
		assert(0 == "Invalid metric or array index");
	return 0;
}

uint16_t ldms_record_array_get_u16(ldms_mval_t rec_inst, int i, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, &desc, NULL, NULL);
	if (mv && 0 <= idx && idx < __le32_to_cpu(desc->vd_array_count))
		return mv->a_u16[idx];
	else
		assert(0 == "Invalid metric or array index");
	return 0;
}

uint32_t ldms_record_array_get_u32(ldms_mval_t rec_inst, int i, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, &desc, NULL, NULL);
	if (mv && 0 <= idx && idx < __le32_to_cpu(desc->vd_array_count))
		return mv->a_u32[idx];
	else
		assert(0 == "Invalid metric or array index");
	return 0;
}

uint64_t ldms_record_array_get_u64(ldms_mval_t rec_inst, int i, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, &desc, NULL, NULL);
	if (mv && 0 <= idx && idx < __le32_to_cpu(desc->vd_array_count))
		return mv->a_u64[idx];
	else
		assert(0 == "Invalid metric or array index");
	return 0;
}

int8_t ldms_record_array_get_s8(ldms_mval_t rec_inst, int i, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, &desc, NULL, NULL);
	if (mv && 0 <= idx && idx < __le32_to_cpu(desc->vd_array_count))
		return mv->a_s8[idx];
	else
		assert(0 == "Invalid metric or array index");
	return 0;
}

int16_t ldms_record_array_get_s16(ldms_mval_t rec_inst, int i, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, &desc, NULL, NULL);
	if (mv && 0 <= idx && idx < __le32_to_cpu(desc->vd_array_count))
		return mv->a_s16[idx];
	else
		assert(0 == "Invalid metric or array index");
	return 0;
}

int32_t ldms_record_array_get_s32(ldms_mval_t rec_inst, int i, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, &desc, NULL, NULL);
	if (mv && 0 <= idx && idx < __le32_to_cpu(desc->vd_array_count))
		return mv->a_s32[idx];
	else
		assert(0 == "Invalid metric or array index");
	return 0;
}

int64_t ldms_record_array_get_s64(ldms_mval_t rec_inst, int i, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, &desc, NULL, NULL);
	if (mv && 0 <= idx && idx < __le32_to_cpu(desc->vd_array_count))
		return mv->a_s64[idx];
	else
		assert(0 == "Invalid metric or array index");
	return 0;
}

float ldms_record_array_get_float(ldms_mval_t rec_inst, int i, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, &desc, NULL, NULL);
	if (mv && 0 <= idx && idx < __le32_to_cpu(desc->vd_array_count)) {
#if LDMS_SETH_F_LCLBYTEORDER == LDMS_SETH_F_LE
		return mv->a_f[idx];
#else
		uint32_t tmp = __le32_to_cpu(*(uint32_t*)&mv->a_f[idx]);
		return *(float *)&tmp;
#endif
	} else
		assert(0 == "Invalid metric or array index");
	return 0;
}

double ldms_record_array_get_double(ldms_mval_t rec_inst, int i, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, &desc, NULL, NULL);
	if (mv && 0 <= idx && idx < __le32_to_cpu(desc->vd_array_count)) {
#if LDMS_SETH_F_LCLBYTEORDER == LDMS_SETH_F_LE
		return mv->a_d[idx];
#else
		uint64_t tmp = __le64_to_cpu(*(uint64_t*)&mv->a_d[idx]);
		return *(double *)&tmp;
#endif
	} else
		assert(0 == "Invalid metric or array index");
	return 0;
}

void ldms_record_set_char(ldms_mval_t rec_inst, int i, char v)
{
	ldms_mdesc_t desc;
	struct ldms_data_hdr *data;
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, &desc, NULL, &data);
	if (mv) {
		mv->v_char = v;
		LDMS_GN_INCREMENT(data->gn);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_record_set_u8(ldms_mval_t rec_inst, int i, uint8_t v)
{
	ldms_mdesc_t desc;
	struct ldms_data_hdr *data;
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, &desc, NULL, &data);
	if (mv) {
		mv->v_u8 = v;
		LDMS_GN_INCREMENT(data->gn);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_record_set_u16(ldms_mval_t rec_inst, int i, uint16_t v)
{
	ldms_mdesc_t desc;
	struct ldms_data_hdr *data;
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, &desc, NULL, &data);
	if (mv) {
		mv->v_u16 = v;
		LDMS_GN_INCREMENT(data->gn);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_record_set_u32(ldms_mval_t rec_inst, int i, uint32_t v)
{
	ldms_mdesc_t desc;
	struct ldms_data_hdr *data;
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, &desc, NULL, &data);
	if (mv) {
		mv->v_u32 = v;
		LDMS_GN_INCREMENT(data->gn);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_record_set_u64(ldms_mval_t rec_inst, int i, uint64_t v)
{
	ldms_mdesc_t desc;
	struct ldms_data_hdr *data;
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, &desc, NULL, &data);
	if (mv) {
		mv->v_u64 = v;
		LDMS_GN_INCREMENT(data->gn);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_record_set_s8(ldms_mval_t rec_inst, int i, int8_t v)
{
	ldms_mdesc_t desc;
	struct ldms_data_hdr *data;
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, &desc, NULL, &data);
	if (mv) {
		mv->v_s8 = v;
		LDMS_GN_INCREMENT(data->gn);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_record_set_s16(ldms_mval_t rec_inst, int i, int16_t v)
{
	ldms_mdesc_t desc;
	struct ldms_data_hdr *data;
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, &desc, NULL, &data);
	if (mv) {
		mv->v_s16 = v;
		LDMS_GN_INCREMENT(data->gn);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_record_set_s32(ldms_mval_t rec_inst, int i, int32_t v)
{
	ldms_mdesc_t desc;
	struct ldms_data_hdr *data;
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, &desc, NULL, &data);
	if (mv) {
		mv->v_s32 = v;
		LDMS_GN_INCREMENT(data->gn);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_record_set_s64(ldms_mval_t rec_inst, int i, int64_t v)
{
	ldms_mdesc_t desc;
	struct ldms_data_hdr *data;
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, &desc, NULL, &data);
	if (mv) {
		mv->v_s64 = v;
		LDMS_GN_INCREMENT(data->gn);
	} else
		assert(0 == "Invalid metric index");
}


void ldms_record_set_float(ldms_mval_t rec_inst, int i, float v)
{
	ldms_mdesc_t desc;
	struct ldms_data_hdr *data;
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, &desc, NULL, &data);
	if (mv) {
		mv->v_f = v;
		LDMS_GN_INCREMENT(data->gn);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_record_set_double(ldms_mval_t rec_inst, int i, double v)
{
	ldms_mdesc_t desc;
	struct ldms_data_hdr *data;
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, &desc, NULL, &data);
	if (mv) {
		mv->v_d = v;
		LDMS_GN_INCREMENT(data->gn);
	} else
		assert(0 == "Invalid metric index");
}


void ldms_record_array_set_str(ldms_mval_t rec_inst, int i, const char *v)
{
	ldms_mdesc_t desc;
	struct ldms_data_hdr *data;
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, &desc, NULL, &data);
	if (mv) {
		strncpy(mv->a_char, v, desc->vd_array_count);
		LDMS_GN_INCREMENT(data->gn);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_record_array_set_char(ldms_mval_t rec_inst, int i, int idx, char v)
{
	ldms_mdesc_t desc;
	struct ldms_data_hdr *data;
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, &desc, NULL, &data);
	if (mv && 0 <= idx && idx < desc->vd_array_count) {
		mv->a_char[idx] = v;
		LDMS_GN_INCREMENT(data->gn);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_record_array_set_u8(ldms_mval_t rec_inst, int i, int idx, uint8_t v)
{
	ldms_mdesc_t desc;
	struct ldms_data_hdr *data;
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, &desc, NULL, &data);
	if (mv && 0 <= idx && idx < desc->vd_array_count) {
		mv->a_u8[idx] = v;
		LDMS_GN_INCREMENT(data->gn);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_record_array_set_u16(ldms_mval_t rec_inst, int i, int idx, uint16_t v)
{
	ldms_mdesc_t desc;
	struct ldms_data_hdr *data;
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, &desc, NULL, &data);
	if (mv && 0 <= idx && idx < desc->vd_array_count) {
		mv->a_u16[idx] = v;
		LDMS_GN_INCREMENT(data->gn);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_record_array_set_u32(ldms_mval_t rec_inst, int i, int idx, uint32_t v)
{
	ldms_mdesc_t desc;
	struct ldms_data_hdr *data;
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, &desc, NULL, &data);
	if (mv && 0 <= idx && idx < desc->vd_array_count) {
		mv->a_u32[idx] = v;
		LDMS_GN_INCREMENT(data->gn);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_record_array_set_u64(ldms_mval_t rec_inst, int i, int idx, uint64_t v)
{
	ldms_mdesc_t desc;
	struct ldms_data_hdr *data;
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, &desc, NULL, &data);
	if (mv && 0 <= idx && idx < desc->vd_array_count) {
		mv->a_u64[idx] = v;
		LDMS_GN_INCREMENT(data->gn);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_record_array_set_s8(ldms_mval_t rec_inst, int i, int idx, int8_t v)
{
	ldms_mdesc_t desc;
	struct ldms_data_hdr *data;
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, &desc, NULL, &data);
	if (mv && 0 <= idx && idx < desc->vd_array_count) {
		mv->a_s8[idx] = v;
		LDMS_GN_INCREMENT(data->gn);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_record_array_set_s16(ldms_mval_t rec_inst, int i, int idx, int16_t v)
{
	ldms_mdesc_t desc;
	struct ldms_data_hdr *data;
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, &desc, NULL, &data);
	if (mv && 0 <= idx && idx < desc->vd_array_count) {
		mv->a_s16[idx] = v;
		LDMS_GN_INCREMENT(data->gn);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_record_array_set_s32(ldms_mval_t rec_inst, int i, int idx, int32_t v)
{
	ldms_mdesc_t desc;
	struct ldms_data_hdr *data;
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, &desc, NULL, &data);
	if (mv && 0 <= idx && idx < desc->vd_array_count) {
		mv->a_s32[idx] = v;
		LDMS_GN_INCREMENT(data->gn);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_record_array_set_s64(ldms_mval_t rec_inst, int i, int idx, int64_t v)
{
	ldms_mdesc_t desc;
	struct ldms_data_hdr *data;
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, &desc, NULL, &data);
	if (mv && 0 <= idx && idx < desc->vd_array_count) {
		mv->a_s64[idx] = v;
		LDMS_GN_INCREMENT(data->gn);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_record_array_set_float(ldms_mval_t rec_inst, int i, int idx, float v)
{
	ldms_mdesc_t desc;
	struct ldms_data_hdr *data;
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, &desc, NULL, &data);
	if (mv && 0 <= idx && idx < desc->vd_array_count) {
		mv->a_f[idx] = v;
		LDMS_GN_INCREMENT(data->gn);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_record_array_set_double(ldms_mval_t rec_inst, int i, int idx, double v)
{
	ldms_mdesc_t desc;
	struct ldms_data_hdr *data;
	ldms_mval_t mv = __record_metric_get(rec_inst, i, NULL, &desc, NULL, &data);
	if (mv && 0 <= idx && idx < desc->vd_array_count) {
		mv->a_d[idx] = v;
		LDMS_GN_INCREMENT(data->gn);
	} else
		assert(0 == "Invalid metric or array index");
}

ldms_mval_t ldms_record_array_get_inst(ldms_mval_t _rec_array, int idx)
{
	ldms_record_inst_t rec_inst = NULL;
	ldms_record_array_t rec_array = &_rec_array->v_rec_array;

	if (idx < 0 || __le32_to_cpu(rec_array->array_len)<=idx) {
		errno = ENOENT;
		goto out;
	}

	rec_inst = ldms_ptr_(void, rec_array->data, idx * __le32_to_cpu(rec_array->inst_sz));

 out:
	return (void*)rec_inst;
}

int ldms_record_array_len(ldms_mval_t rec_array)
{
	return __le32_to_cpu(rec_array->v_rec_array.array_len);
}

int ldms_record_metric_add_template(ldms_record_t rec_def,
			struct ldms_metric_template_s tmp[], int mid[])
{
	ldms_metric_template_t ent;
	int i, ret;
	for (i=0, ent = tmp; ent->name; ent++, i++) {
		ret = ldms_record_metric_add(rec_def,
					ent->name, ent->unit,
					ent->type, ent->len);
		if (ret < 0)
			return ret; /* errno is already set */
		if (mid)
			mid[i] = ret;
	}
	return 0;
}

ldms_record_t ldms_record_from_template(const char *name,
			struct ldms_metric_template_s tmp[],
			int mid[])
{
	ldms_record_t rec_def;
	int ret;

	rec_def = ldms_record_create(name);
	if (!rec_def)
		return NULL;
	ret = ldms_record_metric_add_template(rec_def, tmp, mid);
	if (ret)
		goto err;
	return rec_def;
 err:
	ldms_record_delete(rec_def);
	return NULL;
}

int ldms_schema_metric_add_template(ldms_schema_t s,
				    struct ldms_metric_template_s tmp[],
				    int mid[])
{
	int i, ret;
	ldms_metric_template_t ent;

	for (i=0, ent=tmp; ent->name || ent->rec_def; i++,ent++) {
		switch (ent->type) {
		case LDMS_V_RECORD_TYPE:
			if (ent->flags & LDMS_MDESC_F_META)
				return -EINVAL;
			ret = ldms_schema_record_add(s, ent->rec_def);
			break;
		case LDMS_V_RECORD_ARRAY:
			if (ent->flags & LDMS_MDESC_F_META)
				return -EINVAL;
			ret = ldms_schema_record_array_add(
					s, ent->name, ent->rec_def,
					ent->len
				 );
			break;
		case LDMS_V_LIST:
			if (ent->flags & LDMS_MDESC_F_META)
				return -EINVAL;
			ret = ldms_schema_metric_list_add(
					s, ent->name, ent->unit, ent->len
				 );
			break;
		case LDMS_V_CHAR:
		case LDMS_V_U8:
		case LDMS_V_S8:
		case LDMS_V_U16:
		case LDMS_V_S16:
		case LDMS_V_U32:
		case LDMS_V_S32:
		case LDMS_V_U64:
		case LDMS_V_S64:
		case LDMS_V_F32:
		case LDMS_V_D64:
			if (ent->flags & LDMS_MDESC_F_META) {
				ret = ldms_schema_meta_add_with_unit(
						s, ent->name, ent->unit, ent->type
						);
			} else {
				ret = ldms_schema_metric_add_with_unit(
						s, ent->name, ent->unit, ent->type
						);
			}
			break;
		case LDMS_V_CHAR_ARRAY:
		case LDMS_V_U8_ARRAY:
		case LDMS_V_S8_ARRAY:
		case LDMS_V_U16_ARRAY:
		case LDMS_V_S16_ARRAY:
		case LDMS_V_U32_ARRAY:
		case LDMS_V_S32_ARRAY:
		case LDMS_V_U64_ARRAY:
		case LDMS_V_S64_ARRAY:
		case LDMS_V_F32_ARRAY:
		case LDMS_V_D64_ARRAY:
			if (ent->flags & LDMS_MDESC_F_META) {
				ret = ldms_schema_meta_array_add_with_unit(
						s, ent->name, ent->unit,
						ent->type, ent->len
					 );
			} else {
				ret = ldms_schema_metric_array_add_with_unit(
						s, ent->name, ent->unit,
						ent->type, ent->len
					 );
			}
			break;
		default:
			return -EINVAL;
		}
		if (ret < 0) /* error */
			return ret;
		if (mid)
			mid[i] = ret;
	}
	return 0;
}

ldms_schema_t ldms_schema_from_template(const char *name,
				struct ldms_metric_template_s tmp[], int mid[])
{
	ldms_schema_t sch;
	int ret;
	sch = ldms_schema_new(name);
	if (!sch)
		goto err;
	ret = ldms_schema_metric_add_template(sch, tmp, mid);
	if (ret)
		goto err;
	return sch;
 err:
	if (sch)
		ldms_schema_delete(sch);
	return NULL;
}
