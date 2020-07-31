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
#include "ldms_private.h"
#include "ldms.h"
#include "ldms_xprt.h"
#include "coll/rbt.h"

#define SET_DIR_PATH "/var/run/ldms"
static char *__set_dir = SET_DIR_PATH;
#define SET_DIR_LEN sizeof(SET_DIR_PATH)
static char __set_path[PATH_MAX];
static void __destroy_set(void *v);

const char *ldms_xprt_op_names[] = {
	"LOOKUP",
	"UPDATE",
	"PUBLISH",
	"SET_DELETE",
	"DIR",
	"SEND"
};

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

static struct rbt del_tree = {
	.root = NULL,
	.comparator = id_comparator
};

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

/* Caller must hold the set tree lock. */
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

/* Caller must hold the set tree lock */
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

extern ldms_set_t ldms_set_by_name(const char *set_name)
{
        struct ldms_set *set;
        struct ldms_rbuf_desc *rbd;

	__ldms_set_tree_lock();
	set = __ldms_find_local_set(set_name);
	__ldms_set_tree_unlock();
        if (!set)
                return NULL;
        rbd = __ldms_alloc_rbd(NULL, set, LDMS_RBD_LOCAL, __func__);
        if (rbd)
                ref_get(&set->ref, __func__);
        ref_put(&set->ref, "__ldms_find_local_set");
        return rbd;
}

uint64_t ldms_set_meta_gn_get(ldms_set_t s)
{
	return __le64_to_cpu(s->set->meta->meta_gn);
}

uint64_t ldms_set_data_gn_get(ldms_set_t s)
{
	return __le64_to_cpu(s->set->data->gn);
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

/* Caller must hold the set tree lock */
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

static const char *perm_string(uint32_t perm)
{
	static char str[16];
	char *s = str;
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
	return str;
}

char *set_state(struct ldms_set *set)
{
	static char str[8];
	if (set->data->trans.flags == LDMS_TRANSACTION_END)
		str[0] = 'C';
	else
		str[0] = ' ';
	if (set->flags & LDMS_SET_F_LOCAL)
		str[1] = 'L';
	else if (set->flags & LDMS_SET_F_REMOTE)
		str[1] = 'R';
	else
		str[1] = ' ';
	if (set->flags & LDMS_SET_F_PUSH_CHANGE)
		str[2] = 'P';
	else
		str[2] = ' ';
	str[3] = '\0';
	return str;
}

size_t __ldms_format_set_meta_as_json(struct ldms_set *set,
				      int need_comma,
				      char *buf, size_t buf_size)
{
	size_t cnt;
	cnt = snprintf(buf, buf_size,
		       "%c{"
		       "\"name\":\"%s\","
		       "\"schema\":\"%s\","
		       "\"flags\":\"%s\","
		       "\"meta_size\":%d,"
		       "\"data_size\":%d,"
		       "\"uid\":%d,"
		       "\"gid\":%d,"
		       "\"perm\":\"%s\","
		       "\"card\":%d,"
		       "\"array_card\":%d,"
		       "\"meta_gn\":%lld,"
		       "\"data_gn\":%lld,"
		       "\"timestamp\":{\"sec\":%d,\"usec\":%d},"
		       "\"duration\":{\"sec\":%d,\"usec\":%d},"
		       "\"info\":[",
		       need_comma ? ',' : ' ',
		       get_instance_name(set->meta)->name,
		       get_schema_name(set->meta)->name,
		       set_state(set),
		       __le32_to_cpu(set->meta->meta_sz),
		       __le32_to_cpu(set->meta->data_sz),
		       __le32_to_cpu(set->meta->uid),
		       __le32_to_cpu(set->meta->gid),
		       perm_string(__le32_to_cpu(set->meta->perm)),
		       __le32_to_cpu(set->meta->card),
		       __le32_to_cpu(set->meta->array_card),
		       __le64_to_cpu(set->meta->meta_gn),
		       __le64_to_cpu(set->data->gn),
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

 /* The caller must hold the set tree lock. */
static struct ldms_set *
__record_set(const char *instance_name,
	     struct ldms_set_hdr *sh, struct ldms_data_hdr *dh, int flags)
{
	struct ldms_set *set;

	set = calloc(1, sizeof *set);
	if (!set) {
		errno = ENOMEM;
		goto out_0;
	}

	LIST_INIT(&set->local_info);
	LIST_INIT(&set->remote_info);
	LIST_INIT(&set->remote_rbd_list);
	LIST_INIT(&set->local_rbd_list);
	pthread_mutex_init(&set->lock, NULL);
	set->curr_idx = __le32_to_cpu(sh->array_card) - 1;
	set->set_id = __sync_fetch_and_add(&__next_set_id, 1);
	set->meta = sh;
	set->data_array = dh;
	set->data = __set_array_get(set, set->curr_idx);
	set->flags = flags;

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
		goto out_1;
	}
	rbt_ins(&__set_tree, &set->rb_node);
	rbt_ins(&__id_tree, &set->id_node);
 out_1:
	__ldms_set_tree_unlock();
 out_0:
	return set;
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

uint64_t ldms_set_id(ldms_set_t set)
{
	if (set)
		return set->set->set_id;
	return 0;
}

int ldms_set_publish(ldms_set_t sd)
{
	int rc;
	ref_get(&sd->set->ref, "publish");
	rc = __ldms_set_publish(sd->set);
	ref_put(&sd->set->ref, "publish");
	return rc;
}

/* Caller must hold the set tree lock */
static
int __ldms_set_unpublish(struct ldms_set *set)
{
	if (!(set->flags & LDMS_SET_F_PUBLISHED))
		return ENOENT;

	set->flags &= ~LDMS_SET_F_PUBLISHED;
	__ldms_dir_del_set(set);
	return 0;
}

int ldms_set_unpublish(ldms_set_t sd)
{
	int rc;
	ref_get(&sd->set->ref, "unpublish");
	rc = __ldms_set_unpublish(sd->set);
	ref_put(&sd->set->ref, "unpublish");
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

int ldms_xprt_update(ldms_set_t s, ldms_update_cb_t cb, void *arg)
{
	struct ldms_set *set = s->set;
	ldms_t xprt = ldms_xprt_get(s->xprt);
	int rc;

	assert(set);

	if (0 == (set->flags & LDMS_SET_F_REMOTE)) {
		if (cb)
			cb(s->xprt, s, 0, arg);
		return 0;
	}

	if (!xprt)
		return EINVAL;

	pthread_mutex_lock(&xprt->lock);
	if (!cb) {
		int rc = __ldms_remote_update(xprt, s, sync_update_cb, arg);
		pthread_mutex_unlock(&xprt->lock);
		if (rc) {
			ldms_xprt_put(xprt);
			return rc;
		}
		sem_wait(&xprt->sem);
		rc = xprt->sem_rc;
		ldms_xprt_put(xprt);
	}
	rc = __ldms_remote_update(xprt, s, cb, arg);
	pthread_mutex_unlock(&xprt->lock);
	ldms_xprt_put(xprt);
	return rc;
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
	rbt_del(&del_tree, &set->del_node);
        mm_free(set->meta);
        __ldms_set_info_delete(&set->local_info);
        __ldms_set_info_delete(&set->remote_info);
        free(set);
}

static void __destroy_set(void *v)
{
	pthread_mutex_lock(&__del_tree_lock);
	__destroy_set_no_lock(v);
	pthread_mutex_unlock(&__del_tree_lock);
}

static void __set_delete_cb(ldms_t xprt, int status, ldms_set_t rbd, void *cb_arg)
{
	struct ldms_set *set = cb_arg;
	ref_put(&set->ref, "share_lookup");
	ref_put(&rbd->ref, "share_lookup");
}

void ldms_set_delete(ldms_set_t s)
{
	extern void __ldms_rbd_xprt_release(struct ldms_rbuf_desc *rbd);
	struct ldms_rbuf_desc *rbd;
	struct ldms_set *set = s->set;
	struct ldms_set *__set;
	ldms_t xprt;

	__ldms_set_tree_lock();
	__set = __ldms_set_by_id(set->set_id);
	if (!__set) {
		__ldms_set_tree_unlock();
		goto set_new_put;
	}
	rbt_del(&__set_tree, &set->rb_node);
	rbt_del(&__id_tree, &set->id_node);
	__ldms_set_tree_unlock();

	ldms_xprt_set_delete(s, __set_delete_cb, set);

	pthread_mutex_lock(&set->lock);
	while (!LIST_EMPTY(&set->remote_rbd_list)) {
		rbd = LIST_FIRST(&set->remote_rbd_list);
		LIST_REMOVE(rbd, set_link);
		ref_put(&rbd->ref, "set_rbd_list");
		switch (rbd->type) {
		case LDMS_RBD_INITIATOR:
			ref_put(&rbd->ref, "rendezvous_lookup");
			ref_put(&set->ref, "rendezvous_lookup");
			break;
		case LDMS_RBD_TARGET:
			ref_put(&rbd->ref, "rendezvous_push");
			ref_put(&set->ref, "rendezvous_push");
		case LDMS_RBD_LOCAL:
			/* cleaned up in __set_delete_cb() */
			break;
		}
		xprt = rbd->xprt;
		if (xprt) {
			pthread_mutex_lock(&xprt->lock);
			if (rbd->xprt)
				/* Make certain we didn't lose a disconnect race */
				__ldms_rbd_xprt_release(rbd);
			pthread_mutex_unlock(&xprt->lock);
		}
	}
	pthread_mutex_unlock(&set->lock);

	/* Add the set to the delete tree with the current timestamp */
	set->del_time = time(NULL);
	rbn_init(&set->del_node, &set->del_time);
	pthread_mutex_lock(&__del_tree_lock);
	rbt_ins(&del_tree, &set->del_node);
	pthread_mutex_unlock(&__del_tree_lock);

	/* Drop the create references on the RBD and the set */
	ref_put(&set->ref, "__record_set");
set_new_put:
	ref_put(&set->ref, "set_new");
	ref_put(&s->ref, "set_new");
}

void ldms_set_put(ldms_set_t s)
{
	struct ldms_set *set;
	if (!s)
		return;
	set = s->set;
	pthread_mutex_lock(&set->lock);
	__ldms_free_rbd(s, "ldms_set_by_name"); /* removes the RBD from the local/remote rbd list */
	pthread_mutex_unlock(&set->lock);
	ref_put(&set->ref, "ldms_set_by_name");
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
	ldms_name_t name = get_instance_name(s->set->meta);
	return name->name;
}

const char *ldms_set_producer_name_get(ldms_set_t s)
{
	return s->set->meta->producer_name;
}

int ldms_set_producer_name_set(ldms_set_t s, const char *name)
{
	if (LDMS_PRODUCER_NAME_MAX < strlen(name))
		return EINVAL;

	strncpy(s->set->meta->producer_name, name, LDMS_PRODUCER_NAME_MAX);
	return 0;
}

/* Caller must hold the set tree lock. */
struct ldms_set *__ldms_create_set(const char *instance_name,
				   const char *schema_name,
				   size_t meta_len, size_t data_len,
				   size_t card,
				   size_t array_card,
				   uint32_t flags)
{
	int i;
	struct ldms_data_hdr *data, *data_base;
	struct ldms_set_hdr *meta;
	struct ldms_set *set = NULL;

	meta = mm_alloc(meta_len + array_card * data_len);
	if (!meta) {
		errno = ENOMEM;
		return NULL;
	}

	memset(meta, 0, meta_len + array_card * data_len);
	LDMS_VERSION_SET(meta->version);
	meta->meta_sz = __cpu_to_le32(meta_len);

	meta->data_sz = __cpu_to_le32(data_len);

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

	data_base = (void*)meta + meta_len;

	for (i = 0; i < array_card; i++) {
		data = (void*)data_base + i*data_len;
		data->size = __cpu_to_le64(data_len);
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
		 __le32_to_cpu(s->meta->data_sz));
}

#define LDMS_GRAIN_MMALLOC 1024

int ldms_init(size_t max_size)
{
	size_t grain = LDMS_GRAIN_MMALLOC;
	if (mm_init(max_size, grain))
		return -1;
	return 0;
}

ldms_schema_t ldms_schema_new(const char *schema_name)
{
	ldms_schema_t s = calloc(1, sizeof *s);
	if (s) {
		s->name = strdup(schema_name);
		s->meta_sz = sizeof(struct ldms_set_hdr);
		s->data_sz = sizeof(struct ldms_data_hdr);
		s->array_card = 1;
		STAILQ_INIT(&s->metric_list);
	}
	return s;
}

void ldms_schema_delete(ldms_schema_t schema)
{
	ldms_mdef_t m;

	if (!schema)
		return;

	while (!STAILQ_EMPTY(&schema->metric_list)) {
		m = STAILQ_FIRST(&schema->metric_list);
		STAILQ_REMOVE_HEAD(&schema->metric_list, entry);
		free(m->name);
		free(m);
	}
	free(schema->name);
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

static int value_size[] = {
	[LDMS_V_NONE] = 0,
	[LDMS_V_CHAR] = sizeof(uint8_t),
	[LDMS_V_U8] = sizeof(uint8_t),
	[LDMS_V_S8] = sizeof(uint8_t),
	[LDMS_V_U16] = sizeof(uint16_t),
	[LDMS_V_S16] = sizeof(uint16_t),
	[LDMS_V_U32] = sizeof(uint32_t),
	[LDMS_V_S32] = sizeof(uint32_t),
	[LDMS_V_U64] = sizeof(uint64_t),
	[LDMS_V_S64] = sizeof(uint64_t),
	[LDMS_V_F32] = sizeof(float),
	[LDMS_V_D64] = sizeof(double),
	/* size of array type in this table is the size of an element */
	[LDMS_V_CHAR_ARRAY] = sizeof(uint8_t),
	[LDMS_V_U8_ARRAY] = sizeof(uint8_t),
	[LDMS_V_S8_ARRAY] = sizeof(uint8_t),
	[LDMS_V_U16_ARRAY] = sizeof(uint16_t),
	[LDMS_V_S16_ARRAY] = sizeof(uint16_t),
	[LDMS_V_U32_ARRAY] = sizeof(uint32_t),
	[LDMS_V_S32_ARRAY] = sizeof(uint32_t),
	[LDMS_V_U64_ARRAY] = sizeof(uint64_t),
	[LDMS_V_S64_ARRAY] = sizeof(uint64_t),
	[LDMS_V_F32_ARRAY] = sizeof(float),
	[LDMS_V_D64_ARRAY] = sizeof(double),
};

size_t __ldms_value_size_get(enum ldms_value_type t, uint32_t count)
{
	size_t value_sz = 0;
	value_sz = value_size[t] * count;
	/* Values are aligned on 8b boundary */
	return roundup(value_sz, 8);
}

void __ldms_metric_size_get(const char *name, enum ldms_value_type t,
		uint32_t count, size_t *meta_sz, size_t *data_sz)
{
	/* Descriptors are aligned on eight byte boundaries */
	*meta_sz = roundup(sizeof(struct ldms_value_desc) + strlen(name) + 1, 8);

	*data_sz = __ldms_value_size_get(t, count);
}

ldms_set_t ldms_set_new_with_auth(const char *instance_name,
				  ldms_schema_t schema,
				  uid_t uid, gid_t gid, mode_t perm)
{
	struct ldms_data_hdr *data, *data_base;
	struct ldms_set_hdr *meta;
	struct ldms_value_desc *vd;
	size_t meta_sz, array_data_sz;
	uint64_t value_off;
	ldms_mdef_t md;
	int metric_idx;
	int i;
	int set_array_card;

	if (!instance_name || !schema) {
		errno = EINVAL;
		return NULL;
	}

	set_array_card = schema->array_card;

	if (set_array_card < 0) {
		errno = EINVAL;
		return NULL;
	}

	if (!set_array_card) {
		set_array_card = 1;
	}

	meta_sz = schema->meta_sz /* header + metric dict */
		+ strlen(schema->name) + 2 /* schema name + '\0' + len */
		+ strlen(instance_name) + 2; /* instance name + '\0' + len */
	meta_sz = roundup(meta_sz, 8);
	assert(schema->data_sz == roundup(schema->data_sz, 8));
	array_data_sz = schema->data_sz * set_array_card;
	meta = mm_alloc(meta_sz + array_data_sz);
	if (!meta) {
		errno = ENOMEM;
		return NULL;
	}
	memset(meta, 0, meta_sz + array_data_sz);
	LDMS_VERSION_SET(meta->version);
	meta->card = __cpu_to_le32(schema->card);
	meta->meta_sz = __cpu_to_le32(meta_sz);

	meta->data_sz = __cpu_to_le32(schema->data_sz);

	/* Initialize the metric set header (metadata part) */
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
	lname->len = strlen(schema->name) + 1;
	strcpy(lname->name, schema->name);

	/* set array element data hdr initialization */
	data_base = (void*)meta + meta_sz;
	for (i = 0; i < set_array_card; i++) {
		data = (void*)data_base + i*schema->data_sz;
		data->size = __cpu_to_le64(schema->data_sz);
		data->curr_idx = __cpu_to_le32(set_array_card - 1);
		data->gn = data->meta_gn = meta->meta_gn;
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
		vd->vd_type = md->type;
		vd->vd_flags = md->flags;
		vd->vd_array_count = __cpu_to_le32(md->count);
		vd->vd_name_len = strlen(md->name) + 1;
		strncpy(vd->vd_name, md->name, vd->vd_name_len);
		if (md->flags & LDMS_MDESC_F_DATA) {
			vd->vd_data_offset = __cpu_to_le32(value_off);
			value_off += __ldms_value_size_get(md->type, md->count);
		} else
			vd->vd_data_offset = 0; /* set after all metrics defined */

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
	for (i = 0; i < schema->card; i++) {
		vd = ldms_ptr_(struct ldms_value_desc, meta, __le32_to_cpu(meta->dict[i]));
		if (vd->vd_flags & LDMS_MDESC_F_DATA)
			continue;
		vd->vd_data_offset = value_off;
		value_off += __ldms_value_size_get(vd->vd_type,
						   __le32_to_cpu(vd->vd_array_count));
	}
	struct ldms_set *set = __record_set(instance_name, meta, data_base, LDMS_SET_F_LOCAL);
	if (!set)
		goto err_1;
	ldms_set_t rbd = __ldms_alloc_rbd(NULL, set, LDMS_RBD_LOCAL, "set_new");
	if (!rbd)
		goto err_2;

	return rbd;
 err_2:
	rbt_del(&__set_tree, &set->rb_node);
	rbt_del(&__id_tree, &set->id_node);
	free(set);
 err_1:
	mm_free(meta);
	return NULL;
}

ldms_set_t ldms_set_new(const char *instance_name, ldms_schema_t schema)
{
	return ldms_set_new_with_auth(instance_name, schema, geteuid(), getegid(), 0777);
}

int ldms_set_config_auth(ldms_set_t set, uid_t uid, gid_t gid, mode_t perm)
{
	set->set->meta->uid = __cpu_to_le32(uid);
	set->set->meta->gid = __cpu_to_le32(gid);
	set->set->meta->perm = __cpu_to_le32(perm);
	return 0;
}

const char *ldms_set_name_get(ldms_set_t s)
{
	struct ldms_set_hdr *sh = s->set->meta;
	return get_instance_name(sh)->name;
}

const char *ldms_set_schema_name_get(ldms_set_t s)
{
	struct ldms_set_hdr *sh = s->set->meta;
	return get_schema_name(sh)->name;
}

uint32_t ldms_set_card_get(ldms_set_t s)
{
	return __le32_to_cpu(s->set->meta->card);
}

uint32_t ldms_set_uid_get(ldms_set_t s)
{
	return __le32_to_cpu(s->set->meta->uid);
}

uint32_t ldms_set_gid_get(ldms_set_t s)
{
	return __le32_to_cpu(s->set->meta->gid);
}

uint32_t ldms_set_perm_get(ldms_set_t s)
{
	return __le32_to_cpu(s->set->meta->perm);
}

extern uint32_t ldms_set_meta_sz_get(ldms_set_t s)
{
	return __le32_to_cpu(s->set->meta->meta_sz);
}

extern uint32_t ldms_set_data_sz_get(ldms_set_t s)
{
	return __le32_to_cpu(s->set->meta->data_sz);
}

int ldms_mmap_set(void *meta_addr, void *data_addr, ldms_set_t *ps)
{
	struct ldms_set_hdr *sh = meta_addr;
	struct ldms_data_hdr *dh = data_addr;
	int flags;

	flags = LDMS_SET_F_MEMMAP | LDMS_SET_F_LOCAL;
	struct ldms_set *set = __record_set(get_instance_name(sh)->name, sh, dh, flags);
	if (!set)
		goto err;
	int rc = __ldms_set_publish(set);
	if (!rc) {
		struct ldms_rbuf_desc *rbd;
		rbd = __ldms_alloc_rbd(NULL, set, LDMS_RBD_LOCAL, "set_new");
		if (!rbd)
			goto err;
		*ps = rbd;
	} else {
		errno = rc;
		goto err;
	}
	return 0;
 err:
	return errno;
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
	if (idx >= 0 && idx < __le32_to_cpu(s->set->meta->card))
		return ldms_ptr_(struct ldms_value_desc, s->set->meta,
				__le32_to_cpu(s->set->meta->dict[idx]));
	return NULL;
}

const char *ldms_metric_name_get(ldms_set_t set, int i)
{
	ldms_mdesc_t desc = __desc_get(set, i);
	if (desc)
		return desc->vd_name;
	return NULL;
}

enum ldms_value_type ldms_metric_type_get(ldms_set_t set, int i)
{
	ldms_mdesc_t desc = __desc_get(set, i);
	if (desc && LDMS_V_LAST >= desc->vd_type &&
		LDMS_V_FIRST <= desc->vd_type)
		return desc->vd_type;
	return LDMS_V_NONE;
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
		if (0 == strcmp(desc->vd_name, name))
			return i;
	}
	return -1;
}

int __schema_metric_add(ldms_schema_t s, const char *name, int flags,
			enum ldms_value_type type, uint32_t array_count)
{
	ldms_mdef_t m;

	if (!s || !name)
		return -EINVAL;

	/* check if the name is a duplicate */
	STAILQ_FOREACH(m, &s->metric_list, entry) {
		if (!strcmp(m->name, name))
			return -EEXIST;
	}
	m = calloc(1, sizeof *m);
	if (!m)
		return -ENOMEM;

	m->name = strdup(name);
	m->type = type;
	m->flags = flags;
	m->count = array_count;
	__ldms_metric_size_get(name, type, m->count, &m->meta_sz, &m->data_sz);
	STAILQ_INSERT_TAIL(&s->metric_list, m, entry);
	s->card++;
	s->meta_sz += m->meta_sz + sizeof(uint32_t) /* + dict entry */;
	if (flags & LDMS_MDESC_F_DATA)
		s->data_sz += m->data_sz;
	else
		s->meta_sz += m->data_sz;
	return s->card - 1;
}

int ldms_schema_metric_add(ldms_schema_t s, const char *name, enum ldms_value_type type)
{
	if (type > LDMS_V_D64)
		return EINVAL;
	return __schema_metric_add(s, name, LDMS_MDESC_F_DATA, type, 1);
}

int ldms_schema_meta_add(ldms_schema_t s, const char *name, enum ldms_value_type type)
{
	if (type > LDMS_V_D64)
		return EINVAL;
	return __schema_metric_add(s, name, LDMS_MDESC_F_META, type, 1);
}

int ldms_schema_metric_array_add(ldms_schema_t s, const char *name,
				 enum ldms_value_type type, uint32_t count)
{
	if (!ldms_type_is_array(type))
		return EINVAL;
	return __schema_metric_add(s, name, LDMS_MDESC_F_DATA, type, count);
}

int ldms_schema_meta_array_add(ldms_schema_t s, const char *name,
			       enum ldms_value_type type, uint32_t count)
{
	return __schema_metric_add(s, name, LDMS_MDESC_F_META, type, count);
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
	{ "NONE", LDMS_V_NONE, },
	{ "S16", LDMS_V_S16, },
	{ "S16_ARRAY", LDMS_V_S16_ARRAY},
	{ "S32", LDMS_V_S32, },
	{ "S32_ARRAY", LDMS_V_S32_ARRAY},
	{ "S64", LDMS_V_S64, },
	{ "S64_ARRAY", LDMS_V_S64_ARRAY},
	{ "S8", LDMS_V_S8, },
	{ "S8_ARRAY", LDMS_V_S8_ARRAY},
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
		__ldms_gn_inc(s->set, desc);
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
	return !(t < LDMS_V_CHAR_ARRAY || LDMS_V_D64_ARRAY < t);
}

static int metric_is_array(ldms_mdesc_t desc)
{
	return ldms_type_is_array(desc->vd_type);
}

int ldms_metric_is_array(ldms_set_t s, int i)
{
	ldms_mdesc_t desc = ldms_ptr_(struct ldms_value_desc, s->set->meta,
			__le32_to_cpu(s->set->meta->dict[i]));
	return metric_is_array(desc);
}

void ldms_metric_modify(ldms_set_t s, int i)
{
	ldms_mdesc_t desc = ldms_ptr_(struct ldms_value_desc, s->set->meta,
			__le32_to_cpu(s->set->meta->dict[i]));
	if (desc)
		__ldms_gn_inc(s->set, desc);
	else
		assert(0 == "Invalid metric index");
}

static ldms_mval_t __mval_to_set(struct ldms_set *s, int idx, ldms_mdesc_t *pd)
{
	ldms_mdesc_t desc = ldms_ptr_(struct ldms_value_desc, s->meta,
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
	ldms_mdesc_t desc = ldms_ptr_(struct ldms_value_desc, s->meta,
				__le32_to_cpu(s->meta->dict[idx]));
	if (pd)
		*pd = desc;
	if (desc->vd_flags & LDMS_MDESC_F_DATA) {
		/* Check if it is being called inside a transaction */
		if (s->data->trans.flags != LDMS_TRANSACTION_END) {
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
	if (i < 0 || i >= __le32_to_cpu(s->set->meta->card))
		return NULL;

	return __mval_to_get(s->set, i, NULL);
}

ldms_mval_t ldms_metric_get_addr(ldms_set_t s, int i)
{
	if (i < 0 || i >= __le32_to_cpu(s->set->meta->card))
		return NULL;

	return __mval_to_get(s->set, i, NULL);
}

uint32_t ldms_metric_array_get_len(ldms_set_t s, int i)
{
	ldms_mdesc_t desc = ldms_ptr_(struct ldms_value_desc, s->set->meta,
				      __le32_to_cpu(s->set->meta->dict[i]));
	if (metric_is_array(desc))
		return __le32_to_cpu(desc->vd_array_count);
	return 1;
}
ldms_mval_t ldms_metric_array_get(ldms_set_t s, int i)
{
	ldms_mdesc_t desc;
	ldms_mval_t ret = __mval_to_get(s->set, i, &desc);
	if (metric_is_array(desc))
		return ret;
	return NULL;
}

static void __metric_set(ldms_set_t s, ldms_mdesc_t desc, ldms_mval_t mv, ldms_mval_t v)
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

	if (i < 0 || i >= __le32_to_cpu(s->set->meta->card))
		assert(0 == "Invalid metric index");

	mv = __mval_to_set(s->set, i, &desc);

	__metric_set(s, desc, mv, v);
	__ldms_gn_inc(s->set, desc);
}

static void __metric_array_set(ldms_set_t s, ldms_mdesc_t desc, ldms_mval_t dst,
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

	if (metric_idx >= __le32_to_cpu(s->set->meta->card))
		assert(0 == "Invalid metric index");

	dst = __mval_to_set(s->set, metric_idx, &desc);

	__metric_array_set(s, desc, dst, array_idx, src);
	__ldms_gn_inc(s->set, desc);
}

void ldms_metric_set_char(ldms_set_t s, int i, char v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s->set, i, &desc);
	if (mv) {
		mv->v_char = v;
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_metric_set_u8(ldms_set_t s, int i, uint8_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s->set, i, &desc);
	if (mv) {
		mv->v_u8 = v;
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_metric_set_s8(ldms_set_t s, int i, int8_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s->set, i, &desc);
	if (mv) {
		mv->v_s8 = v;
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_metric_set_u16(ldms_set_t s, int i, uint16_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s->set, i, &desc);
	if (mv) {
		mv->v_u16 = __cpu_to_le16(v);
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_metric_set_s16(ldms_set_t s, int i, int16_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s->set, i, &desc);
	if (mv) {
		mv->v_s16 = __cpu_to_le16(v);
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_metric_set_u32(ldms_set_t s, int i, uint32_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s->set, i, &desc);
	if (mv) {
		mv->v_u32 = __cpu_to_le32(v);
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_metric_set_s32(ldms_set_t s, int i, int32_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s->set, i, &desc);
	if (mv) {
		mv->v_s32 = __cpu_to_le32(v);
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_metric_set_u64(ldms_set_t s, int i, uint64_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s->set, i, &desc);
	if (mv) {
		mv->v_u64 = __cpu_to_le64(v);
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_metric_set_s64(ldms_set_t s, int i, int64_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s->set, i, &desc);
	if (mv) {
		mv->v_s64 = __cpu_to_le64(v);
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_metric_set_float(ldms_set_t s, int i, float v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s->set, i, &desc);
	if (mv) {
#if LDMS_SETH_F_LCLBYTEORDER == LDMS_SETH_F_LE
		mv->v_f = v;
#else
		*(uint32_t *)&mv->v_f = __cpu_to_le32(*(uint32_t *)&v);
#endif
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_metric_set_double(ldms_set_t s, int i, double v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s->set, i, &desc);
	if (mv) {
#if LDMS_SETH_F_LCLBYTEORDER == LDMS_SETH_F_LE
		mv->v_d = v;
#else
		*(uint64_t *)&mv->v_d = __cpu_to_le64(*(uint64_t *)&v);
#endif
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_metric_array_set_str(ldms_set_t s, int mid, const char *str)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s->set, mid, &desc);
	if (mv) {
		strncpy(mv->a_char, str, desc->vd_array_count);
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_metric_array_set_char(ldms_set_t s, int mid, int idx, char v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s->set, mid, &desc);
	if (mv && (idx < desc->vd_array_count)) {
		mv->a_char[idx] = v;
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_metric_array_set_u8(ldms_set_t s, int mid, int idx, uint8_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s->set, mid, &desc);
	if (mv && (idx < desc->vd_array_count)) {
		mv->a_u8[idx] = v;
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_metric_array_set_s8(ldms_set_t s, int mid, int idx, int8_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s->set, mid, &desc);
	if (mv && (idx < desc->vd_array_count)) {
		mv->a_s8[idx] = v;
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_metric_array_set_u16(ldms_set_t s, int mid, int idx, uint16_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s->set, mid, &desc);
	if (mv && (idx < desc->vd_array_count)) {
		mv->a_u16[idx] = __cpu_to_le16(v);
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_metric_array_set_s16(ldms_set_t s, int mid, int idx, int16_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s->set, mid, &desc);
	if (mv && (idx < desc->vd_array_count)) {
		mv->a_s16[idx] = __cpu_to_le16(v);
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_metric_array_set_u32(ldms_set_t s, int mid, int idx, uint32_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s->set, mid, &desc);
	if (mv && (idx < desc->vd_array_count)) {
		mv->a_u32[idx] = __cpu_to_le32(v);
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_metric_array_set_s32(ldms_set_t s, int mid, int idx, int32_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s->set, mid, &desc);
	if (mv && (idx < desc->vd_array_count)) {
		mv->a_s32[idx] = __cpu_to_le32(v);
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_metric_array_set_u64(ldms_set_t s, int mid, int idx, uint64_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s->set, mid, &desc);
	if (mv && (idx < desc->vd_array_count)) {
		mv->a_u64[idx] = __cpu_to_le64(v);
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_metric_array_set_s64(ldms_set_t s, int mid, int idx, int64_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s->set, mid, &desc);
	if (mv && (idx < desc->vd_array_count)) {
		mv->a_s64[idx] = __cpu_to_le64(v);
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_metric_array_set_float(ldms_set_t s, int mid, int idx, float v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s->set, mid, &desc);
	if (mv && (idx < desc->vd_array_count)) {
#if LDMS_SETH_F_LCLBYTEORDER == LDMS_SETH_F_LE
		mv->a_f[idx] = v;
#else
		/* This type abuse is necessary to avoid the integer cast
		 * that will strip the factional portion of the float value
		 */
		*(uint32_t *)&mv->a_f[idx] = __cpu_to_le32(*(uint32_t *)&v);
#endif
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_metric_array_set_double(ldms_set_t s, int mid, int idx, double v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_set(s->set, mid, &desc);
	if (mv && (idx < desc->vd_array_count)) {
#if LDMS_SETH_F_LCLBYTEORDER == LDMS_SETH_F_LE
		mv->a_d[idx] = v;
#else
		/* See xxx_set_float for type abuse note */
		*(uint64_t *)&mv->a_d[idx] = __cpu_to_le64(*(uint64_t *)&v);
#endif
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_metric_array_set(ldms_set_t s, int mid, ldms_mval_t mval,
			   size_t start, size_t count)
{
	ldms_mdesc_t desc = ldms_ptr_(struct ldms_value_desc, s->set->meta,
				      __le32_to_cpu(s->set->meta->dict[mid]));
	int i;
	ldms_mval_t val = __mval_to_set(s->set, mid, &desc);
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
	__ldms_gn_inc(s->set, desc);
}

char ldms_metric_get_char(ldms_set_t s, int i)
{
	ldms_mval_t mv = __mval_to_get(s->set, i, NULL);
	if (mv)
		return mv->v_char;
	else
		assert(0 == "Invalid metric index");
	return 0;
}

uint8_t ldms_metric_get_u8(ldms_set_t s, int i)
{
	ldms_mval_t mv = __mval_to_get(s->set, i, NULL);
	if (mv)
		return mv->v_u8;
	else
		assert(0 == "Invalid metric index");
	return 0;
}

int8_t ldms_metric_get_s8(ldms_set_t s, int i)
{
	ldms_mval_t mv = __mval_to_get(s->set, i, NULL);
	if (mv)
		return mv->v_s8;
	else
		assert(0 == "Invalid metric index");
	return 0;
}

uint16_t ldms_metric_get_u16(ldms_set_t s, int i)
{
	ldms_mval_t mv = __mval_to_get(s->set, i, NULL);
	if (mv)
		return __le16_to_cpu(mv->v_u16);
	else
		assert(0 == "Invalid metric index");
	return 0;
}

int16_t ldms_metric_get_s16(ldms_set_t s, int i)
{
	ldms_mval_t mv = __mval_to_get(s->set, i, NULL);
	if (mv)
		return __le16_to_cpu(mv->v_s16);
	else
		assert(0 == "Invalid metric index");
	return 0;
}

uint32_t ldms_metric_get_u32(ldms_set_t s, int i)
{
	ldms_mval_t mv = __mval_to_get(s->set, i, NULL);
	if (mv)
		return __le32_to_cpu(mv->v_u32);
	else
		assert(0 == "Invalid metric index");
	return 0;
}

int32_t ldms_metric_get_s32(ldms_set_t s, int i)
{
	ldms_mval_t mv = __mval_to_get(s->set, i, NULL);
	if (mv)
		return __le32_to_cpu(mv->v_s32);
	else
		assert(0 == "Invalid metric index");
	return 0;
}

uint64_t ldms_metric_get_u64(ldms_set_t s, int i)
{
	ldms_mval_t mv = __mval_to_get(s->set, i, NULL);
	if (mv)
		return __le64_to_cpu(mv->v_u64);
	else
		assert(0 == "Invalid metric index");
	return 0;
}

int64_t ldms_metric_get_s64(ldms_set_t s, int i)
{
	ldms_mval_t mv = __mval_to_get(s->set, i, NULL);
	if (mv)
		return __le64_to_cpu(mv->v_s64);
	else
		assert(0 == "Invalid metric index");
	return 0;
}

float ldms_metric_get_float(ldms_set_t s, int i)
{
	ldms_mval_t mv = __mval_to_get(s->set, i, NULL);
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
	ldms_mval_t mv = __mval_to_get(s->set, i, NULL);
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
	ldms_mval_t mv = __mval_to_get(s->set, mid, &desc);
	if (mv)
		return mv->a_char;
	else
		assert(0 == "Invalid metric index");
	return 0;
}

char ldms_metric_array_get_char(ldms_set_t s, int mid, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_get(s->set, mid, &desc);
	if (mv && (idx < desc->vd_array_count))
		return mv->a_char[idx];
	else
		assert(0 == "Invalid metric or array index");
	return 0;
}

uint8_t ldms_metric_array_get_u8(ldms_set_t s, int mid, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_get(s->set, mid, &desc);
	if (mv && (idx < desc->vd_array_count))
		return mv->a_u8[idx];
	else
		assert(0 == "Invalid metric or array index");
	return 0;
}

int8_t ldms_metric_array_get_s8(ldms_set_t s, int mid, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_get(s->set, mid, &desc);
	if (mv && (idx < desc->vd_array_count))
		return mv->a_s8[idx];
	else
		assert(0 == "Invalid metric or array index");
	return 0;
}

uint16_t ldms_metric_array_get_u16(ldms_set_t s, int mid, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_get(s->set, mid, &desc);
	if (mv && (idx < desc->vd_array_count))
		return mv->a_u16[idx];
	else
		assert(0 == "Invalid metric or array index");
	return 0;
}

int16_t ldms_metric_array_get_s16(ldms_set_t s, int mid, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_get(s->set, mid, &desc);
	if (mv && (idx < desc->vd_array_count))
		return mv->a_s16[idx];
	else
		assert(0 == "Invalid metric or array index");
	return 0;
}

uint32_t ldms_metric_array_get_u32(ldms_set_t s, int mid, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_get(s->set, mid, &desc);
	if (mv && (idx < desc->vd_array_count))
		return mv->a_u32[idx];
	else
		assert(0 == "Invalid metric or array index");
	return 0;
}

int32_t ldms_metric_array_get_s32(ldms_set_t s, int mid, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_get(s->set, mid, &desc);
	if (mv && (idx < desc->vd_array_count))
		return mv->a_s32[idx];
	else
		assert(0 == "Invalid metric or array index");
	return 0;
}

uint64_t ldms_metric_array_get_u64(ldms_set_t s, int mid, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_get(s->set, mid, &desc);
	if (mv && (idx < desc->vd_array_count))
		return mv->a_u64[idx];
	else
		assert(0 == "Invalid metric or array index");
	return 0;
}

int64_t ldms_metric_array_get_s64(ldms_set_t s, int mid, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_get(s->set, mid, &desc);
	if (mv && (idx < desc->vd_array_count))
		return mv->a_s64[idx];
	else
		assert(0 == "Invalid metric or array index");
	return 0;
}

float ldms_metric_array_get_float(ldms_set_t s, int mid, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __mval_to_get(s->set, mid, &desc);
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
	ldms_mval_t mv = __mval_to_get(s->set, mid, &desc);
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

int ldms_transaction_begin(ldms_set_t s)
{
	struct ldms_data_hdr *dh, *dh_prev;
	struct timeval tv;
	int i, n;

	pthread_mutex_lock(&s->set->lock);
	dh_prev = __ldms_set_array_get(s, s->set->curr_idx);
	n = __le32_to_cpu(s->set->meta->array_card);
	s->set->curr_idx = (s->set->curr_idx + 1) % n;
	dh = __ldms_set_array_get(s, s->set->curr_idx);
	*dh = *dh_prev; /* copy over the header contents */
	dh->trans.flags = LDMS_TRANSACTION_BEGIN;
	(void)gettimeofday(&tv, NULL);
	dh->trans.ts.sec = __cpu_to_le32(tv.tv_sec);
	dh->trans.ts.usec = __cpu_to_le32(tv.tv_usec);
	/* update curr_idx in all headers */
	for (i = 0; i < n; i++) {
		dh = __ldms_set_array_get(s, i);
		dh->curr_idx = __cpu_to_le32(s->set->curr_idx);
	}
	s->set->data = __ldms_set_array_get(s, s->set->curr_idx);
	pthread_mutex_unlock(&s->set->lock);
	return 0;
}

int ldms_transaction_end(ldms_set_t s)
{
	struct ldms_data_hdr *dh;
	struct timeval tv;

	pthread_mutex_lock(&s->set->lock);
	(void)gettimeofday(&tv, NULL);
	dh = __ldms_set_array_get(s, s->set->curr_idx);
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
	pthread_mutex_unlock(&s->set->lock);
	__ldms_xprt_push(s, LDMS_RBD_F_PUSH_CHANGE);
	return 0;
}

struct ldms_timestamp ldms_transaction_timestamp_get(ldms_set_t s)
{
	struct ldms_data_hdr *dh = s->set->data;
	struct ldms_timestamp ts = {
		.sec = __le32_to_cpu(dh->trans.ts.sec),
		.usec = __le32_to_cpu(dh->trans.ts.usec),
	};
	return ts;
}

struct ldms_timestamp ldms_transaction_duration_get(ldms_set_t s)
{
	struct ldms_data_hdr *dh = s->set->data;
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
	struct ldms_data_hdr *dh = s->set->data;
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

	pthread_mutex_lock(&s->set->lock);
	rc = __ldms_set_info_set(&s->set->local_info, key, value);
	if (rc > 0) {
		/* error */
		goto out;
	}
	if (rc == -1) {
		/* no changes .. nothing else to do */
		rc = 0;
		goto out;
	}
	if (s->set->flags & LDMS_SET_F_PUBLISHED)
		__ldms_dir_upd_set(s->set);
out:
	pthread_mutex_unlock(&s->set->lock);
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
	pthread_mutex_lock(&s->set->lock);
	pair = __ldms_set_info_find(&s->set->local_info, key);
	if (!pair) {
		pthread_mutex_unlock(&s->set->lock);
		return;
	}
	__ldms_set_info_unset(pair);
	if (s->set->flags & LDMS_SET_F_PUBLISHED)
		__ldms_dir_upd_set(s->set);
	pthread_mutex_unlock(&s->set->lock);
}

char *ldms_set_info_get(ldms_set_t s, const char *key)
{
	struct ldms_set_info_pair *pair;
	char *value = NULL;

	pthread_mutex_lock(&s->set->lock);
	pair = __ldms_set_info_find(&s->set->local_info, key);
	if (pair) {
		value = strdup(pair->value);
		goto out;
	}

	pair = __ldms_set_info_find(&s->set->remote_info, key);
	if (!pair)
		goto out;
	value = strdup(pair->value);
out:
	pthread_mutex_unlock(&s->set->lock);
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
	pthread_mutex_lock(&s->set->lock);
	if (flag == LDMS_SET_INFO_F_LOCAL) {
		list = &s->set->local_info;
	} else if (flag == LDMS_SET_INFO_F_REMOTE) {
		list = &s->set->remote_info;
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
	pthread_mutex_unlock(&s->set->lock);
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

#define DELETE_TIMEOUT	(600)	/* 10 minutes */
#define DELETE_CHECK	(60)
#define REPORT_MIN	(10)

static void *delete_proc(void *arg)
{
	struct rbn *rbn;
	struct ldms_set *set;
	ldms_name_t name;
	time_t dur;
	char *to = getenv("LDMS_DELETE_TIMEOUT");
	int timeout = (to ? atoi(to) : DELETE_TIMEOUT);
	if (timeout <= DELETE_CHECK)
		timeout = DELETE_CHECK;
	do {
		/*
		 * Iterate through the tree from oldest to
		 * newest. Delete any set older than the threshold
		 */
		pthread_mutex_lock(&__del_tree_lock);
		rbn = rbt_max(&del_tree);
		while (rbn) {
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
			fprintf(stderr,
				"Deleting dangling set %s with reference "
				"count %d, waited %jd seconds\n",
				name->name, set->ref.ref_count, dur);
			ref_dump(&set->ref, __func__, stderr);
			__destroy_set_no_lock(set);
			rbn = rbt_max(&del_tree);
			fflush(stderr);
		}
		pthread_mutex_unlock(&__del_tree_lock);
		sleep(DELETE_CHECK);
	} while (1);
	return NULL;
}

static pthread_t delete_thread;
static void __attribute__ ((constructor)) cs_init(void)
{
	int rc = pthread_create(&delete_thread, NULL, delete_proc, NULL);
	if (!rc) {
		pthread_setname_np(delete_thread, "delete_thread");
	}
}

static void __attribute__ ((destructor)) cs_term(void)
{
}

