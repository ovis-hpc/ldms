/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010-2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2010-2016 Sandia Corporation. All rights reserved.
 *
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

char *_create_path(const char *set_name);

static int set_comparator(void *a, const void *b)
{
	char *x = a;
	char *y = (char *)b;

	return strcmp(x, y);
}
static struct rbt set_tree = {
	.root = NULL,
	.comparator = set_comparator
};
pthread_mutex_t set_tree_lock = PTHREAD_MUTEX_INITIALIZER;

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

	z = rbt_find(&set_tree, (void *)set_name);
	if (z)
		s = container_of(z, struct ldms_set, rb_node);
	return s;
}

/* Caller must hold the set tree lock */
struct ldms_set *__ldms_local_set_first()
{
	struct rbn *z;
	struct ldms_set *s = NULL;

	z = rbt_min(&set_tree);
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
	pthread_mutex_lock(&set_tree_lock);
}

void __ldms_set_tree_unlock()
{
	pthread_mutex_unlock(&set_tree_lock);
}

extern ldms_set_t ldms_set_by_name(const char *set_name)
{
	struct ldms_set_desc *sd = NULL;
	__ldms_set_tree_lock();
	struct ldms_set *set = __ldms_find_local_set(set_name);
	struct ldms_rbuf_desc *rbd;
	if (!set)
		goto out;

	sd = calloc(1, sizeof *sd);
	if (!sd)
		goto out;

	/* Create fake rbd */
	rbd = calloc(1, sizeof(*rbd));
	if (!rbd) {
		free(sd);
		sd = NULL;
		goto out;
	}
	/*
	 * Insert the rbd to the remote list
	 * to take the reference on the set.
	 */
	rbd->set = set;
	LIST_INSERT_HEAD(&set->remote_rbd_list, rbd, set_link);

	sd->set = set;
	sd->rbd = rbd;

 out:
	__ldms_set_tree_unlock();
	return sd;
}

uint64_t ldms_set_meta_gn_get(ldms_set_t _set)
{
	struct ldms_set_desc *sd = _set;
	return __le64_to_cpu(sd->set->meta->meta_gn);
}

uint64_t ldms_set_data_gn_get(ldms_set_t _set)
{
	struct ldms_set_desc *sd = _set;
	return __le64_to_cpu(sd->set->data->gn);
}

static void rem_local_set(struct ldms_set *s)
{
	rbt_del(&set_tree, &s->rb_node);
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
	rc = rbt_traverse(&set_tree, rbn_cb, &user_arg);
	return rc;
}

struct set_list_arg {
	char *set_list;
	ssize_t set_list_len;
	int count;
};

int set_list_cb(struct ldms_set *set, void *arg)
{
	struct set_list_arg *a = arg;
	int len;

	len = get_instance_name(set->meta)->len;
	if (len > a->set_list_len)
		return -ENOMEM;

	a->count++;
	strcpy(a->set_list, get_instance_name(set->meta)->name);
	a->set_list += len;
	a->set_list_len -= len;

	return 0;
}

int __ldms_get_local_set_list(char *set_list, size_t set_list_len,
			      int *set_count, int *set_list_size)
{
	struct set_list_arg arg;
	int rc;

	arg.set_list = set_list;
	arg.set_list_len = set_list_len;
	arg.count = 0;
	rc = __ldms_for_all_sets(set_list_cb, &arg);
	if (!rc) {
		*set_count = arg.count;
		/* Original len - remainder */
		*set_list_size = set_list_len - arg.set_list_len;
	}
	return rc;
}

static int set_list_sz_cb(struct ldms_set *set, void *arg)
{
	struct set_list_arg *a = arg;
	int len;

	len = get_instance_name(set->meta)->len;
	a->set_list_len += len;
	a->count++;

	return 0;
}

 void __ldms_get_local_set_list_sz(int *set_count, int *set_list_size)
{
	struct set_list_arg arg;

	arg.count = 0;
	arg.set_list_len = 0;
	(void)__ldms_for_all_sets(set_list_sz_cb, &arg);
	*set_count = arg.count;
	*set_list_size = arg.set_list_len;
}

 /* The caller must hold the set tree lock. */
static int __record_set(const char *instance_name, ldms_set_t *s,
			struct ldms_set_hdr *sh, struct ldms_data_hdr *dh, int flags)
{
	struct ldms_set *set;
	struct ldms_set_desc *sd;

	set = __ldms_find_local_set(instance_name);
	if (set) {
		return EEXIST;
	}

	sd = calloc(1, sizeof *sd);
	if (!sd)
		goto out_0;

	set = calloc(1, sizeof *set);
	if (!set)
		goto out_1;

	set->meta = sh;
	set->data = dh;
	set->flags = flags;

	*s = sd;
	sd->set = set;
	sd->rbd = NULL;

	set->rb_node.key = get_instance_name(set->meta)->name;
	rbt_ins(&set_tree, &set->rb_node);
	return 0;

 out_1:
	free(sd);
 out_0:
	return ENOMEM;
}

/* Caller must hold the set tree lock. */
static
int __ldms_set_publish(struct ldms_set *set)
{
	struct ldms_set *_set;
	if (set->flags & LDMS_SET_F_PUBLISHED)
		return EEXIST;

	set->flags |= LDMS_SET_F_PUBLISHED;
	__ldms_dir_add_set(get_instance_name(set->meta)->name);
	return 0;
}

int ldms_set_publish(ldms_set_t sd)
{
	int rc;
	__ldms_set_tree_lock();
	rc = __ldms_set_publish(sd->set);
	__ldms_set_tree_unlock();
	return rc;
}

/* Caller must hold the set tree lock */
static
int __ldms_set_unpublish(struct ldms_set *set)
{
	struct ldms_set *_set;
	if (!(set->flags & LDMS_SET_F_PUBLISHED))
		return ENOENT;

	set->flags &= ~LDMS_SET_F_PUBLISHED;
	__ldms_dir_del_set(get_instance_name(set->meta)->name);
	return 0;
}

int ldms_set_unpublish(ldms_set_t sd)
{
	int rc;
	__ldms_set_tree_lock();
	rc = __ldms_set_unpublish(sd->set);
	__ldms_set_tree_unlock();
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

static ldms_t __get_xprt(ldms_set_t s)
{
	struct ldms_set_desc *sd = (struct ldms_set_desc *)s;
	return (ldms_t)(sd->rbd?sd->rbd->xprt:0);
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
	struct ldms_set *set = ((struct ldms_set_desc *)s)->set;
	if (set->flags & LDMS_SET_F_REMOTE){
		ldms_t x = __get_xprt(s);
		if (!cb) {
			int rc = __ldms_remote_update(x, s, sync_update_cb, arg);
			if (rc)
				return rc;
			sem_wait(&x->sem);
			return x->sem_rc;
		}
		return __ldms_remote_update(x, s, cb, arg);
	}
	if (cb)
		cb(__get_xprt(s), s, 0, arg);
	return 0;
}

void ldms_set_delete(ldms_set_t s)
{
	if (!s) {
		assert(NULL == "The metric set passed in is NULL");
	}

	__ldms_set_tree_lock();
	struct ldms_set_desc *sd = (struct ldms_set_desc *)s;
	if (sd->rbd)
		__ldms_free_rbd(sd->rbd);

	struct ldms_set *set = sd->set;
	struct ldms_rbuf_desc *rbd;
	if (LIST_EMPTY(&set->remote_rbd_list)) {
		__ldms_set_unpublish(set);
		while (!LIST_EMPTY(&set->local_rbd_list)) {
			rbd = LIST_FIRST(&set->local_rbd_list);
			LIST_REMOVE(rbd, set_link);
			__ldms_free_rbd(rbd);
		}
	}
	__ldms_set_tree_unlock();
	free(sd);
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
	char *dirc = strdup(set_name);
	char *basec = strdup(set_name);
	char *dname = dirname(dirc);
	char *bname = basename(basec);
	char *p;
	int tail, rc = 0;

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
	struct ldms_set_desc *sd = (struct ldms_set_desc *)s;
	ldms_name_t name = get_instance_name(sd->set->meta);
	return name->name;
}

const char *ldms_set_producer_name_get(ldms_set_t s)
{
	struct ldms_set_desc *sd = (struct ldms_set_desc *)s;
	return sd->set->meta->producer_name;
}

int ldms_set_producer_name_set(ldms_set_t s, const char *name)
{
	if (LDMS_PRODUCER_NAME_MAX < strlen(name))
		return EINVAL;

	struct ldms_set_desc *sd = (struct ldms_set_desc *)s;
	strncpy(sd->set->meta->producer_name, name, LDMS_PRODUCER_NAME_MAX);
	return 0;
}

/* Caller must hold the set tree lock. */
int __ldms_create_set(const char *instance_name, const char *schema_name,
		      size_t meta_len, size_t data_len, size_t card,
		      ldms_set_t *s, uint32_t flags)
{
	struct ldms_data_hdr *data;
	struct ldms_set_hdr *meta;
	int rc;

	meta = mm_alloc(meta_len + data_len);
	if (!meta) {
		rc = ENOMEM;
		goto out_0;
	}
	memset(meta, 0, meta_len + data_len);
	LDMS_VERSION_SET(meta->version);
	meta->meta_sz = __cpu_to_le32(meta_len);

	data = (struct ldms_data_hdr *)((unsigned char*)meta + meta_len);
	meta->data_sz = __cpu_to_le32(data_len);
	data->size = __cpu_to_le64(data_len);

	/* Initialize the metric set header */
	meta->meta_gn = __cpu_to_le64(1);
	meta->card = __cpu_to_le32(card);
	meta->flags = LDMS_SETH_F_LCLBYTEORDER;

	ldms_name_t lname = get_instance_name(meta);
	lname->len = strlen(instance_name) + 1;
	strcpy(lname->name, instance_name);

	lname = get_schema_name(meta);
	lname->len = strlen(schema_name);
	strcpy(lname->name, schema_name);

	data->gn = data->meta_gn = meta->meta_gn;

	rc = __record_set(instance_name, s, meta, data, flags);
	if (rc)
		goto out_1;
	return 0;

 out_1:
	mm_free(meta);
 out_0:
	return rc;
}

uint32_t __ldms_set_size_get(struct ldms_set *s)
{
	return __le32_to_cpu(s->meta->meta_sz) + __le32_to_cpu(s->meta->data_sz);
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

ldms_set_t ldms_set_new(const char *instance_name, ldms_schema_t schema)
{
	struct ldms_data_hdr *data;
	struct ldms_set_hdr *meta;
	struct ldms_value_desc *vd;
	size_t meta_sz;
	uint64_t value_off;
	ldms_mdef_t md;
	int metric_idx;
	int rc, i;
	ldms_set_t s;

	if (!instance_name || !schema) {
		errno = EINVAL;
		return NULL;
	}

	meta_sz = schema->meta_sz /* header + metric dict */
		+ strlen(schema->name) + 2 /* schema name + '\0' + len */
		+ strlen(instance_name) + 2; /* instance name + '\0' + len */
	meta_sz = roundup(meta_sz, 8);
	meta = mm_alloc(meta_sz + schema->data_sz);
	if (!meta) {
		errno = ENOMEM;
		return NULL;
	}
	memset(meta, 0, meta_sz + schema->data_sz);
	LDMS_VERSION_SET(meta->version);
	meta->card = __cpu_to_le32(schema->card);
	meta->meta_sz = __cpu_to_le32(meta_sz);

	data = (struct ldms_data_hdr *)((unsigned char*)meta + meta_sz);
	meta->data_sz = __cpu_to_le32(schema->data_sz);
	data->size = __cpu_to_le64(schema->data_sz);

	/* Initialize the metric set header */
	meta->meta_gn = __cpu_to_le64(1);
	meta->flags = LDMS_SETH_F_LCLBYTEORDER;

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

	data->gn = data->meta_gn = meta->meta_gn;

	/* Add the metrics from the schema */
	vd = get_first_metric_desc(meta);
	value_off = (uint8_t *)data - (uint8_t *)meta;
	value_off = roundup(value_off + sizeof(*data), 8);
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
	 * meta-attributes from the meta data area
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
	__ldms_set_tree_lock();
	rc = __record_set(instance_name, &s, meta, data, LDMS_SET_F_LOCAL);
	if (rc)
		goto err_1;
	rc = __ldms_set_publish(s->set);
	if (rc)
		goto err_2;
	__ldms_set_tree_unlock();
	return s;
 err_2:
	free(s->set);
	free(s);
 err_1:
	__ldms_set_tree_unlock();
	mm_free(meta);
	errno = rc;
	return NULL;
}

const char *ldms_set_name_get(ldms_set_t _set)
{
	struct ldms_set_desc *sd = (struct ldms_set_desc *)_set;
	struct ldms_set_hdr *sh = sd->set->meta;
	return get_instance_name(sh)->name;
}

const char *ldms_set_schema_name_get(ldms_set_t _set)
{
	struct ldms_set_desc *sd = (struct ldms_set_desc *)_set;
	struct ldms_set_hdr *sh = sd->set->meta;
	return get_schema_name(sh)->name;
}

uint32_t ldms_set_card_get(ldms_set_t _set)
{
	struct ldms_set_desc *sd = (struct ldms_set_desc *)_set;
	return __le32_to_cpu(sd->set->meta->card);
}

extern uint32_t ldms_set_meta_sz_get(ldms_set_t _set)
{
	struct ldms_set_desc *sd = (struct ldms_set_desc *)_set;
	return __le32_to_cpu(sd->set->meta->meta_sz);
}

extern uint32_t ldms_set_data_sz_get(ldms_set_t _set)
{
	struct ldms_set_desc *sd = (struct ldms_set_desc *)_set;
	return __le32_to_cpu(sd->set->meta->data_sz);
}

int ldms_mmap_set(void *meta_addr, void *data_addr, ldms_set_t *s)
{
	struct ldms_set_hdr *sh = meta_addr;
	struct ldms_data_hdr *dh = data_addr;
	int flags;

	flags = LDMS_SET_F_MEMMAP | LDMS_SET_F_LOCAL;
	__ldms_set_tree_lock();
	int rc = __record_set(get_instance_name(sh)->name, s, sh, dh, flags);
	if (rc) {
		__ldms_set_tree_unlock();
		return rc;
	}

	rc = __ldms_set_publish((*s)->set);
	__ldms_set_tree_unlock();
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
};

static inline ldms_mdesc_t __desc_get(ldms_set_t s, int idx)
{
	if (idx >= 0 && idx < __le32_to_cpu(s->set->meta->card))
		return ldms_ptr_(struct ldms_value_desc, s->set->meta,
				__le32_to_cpu(s->set->meta->dict[idx]));
	return NULL;
}

static ldms_mval_t __value_get(struct ldms_set *s, int idx, ldms_mdesc_t *pd)
{
	ldms_mdesc_t desc = ldms_ptr_(struct ldms_value_desc, s->meta,
			__le32_to_cpu(s->meta->dict[idx]));
	if (pd)
		*pd = desc;
	return ldms_ptr_(union ldms_value, s->meta,
			__le32_to_cpu(desc->vd_data_offset));
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

ldms_mval_t ldms_metric_get(ldms_set_t s, int i)
{
	ldms_mdesc_t desc;

	if (i < 0 || i >= __le32_to_cpu(s->set->meta->card))
		return NULL;

	desc = ldms_ptr_(struct ldms_value_desc, s->set->meta,
			 __le32_to_cpu(s->set->meta->dict[i]));

	return ldms_ptr_(union ldms_value, s->set->meta,
			 __le32_to_cpu(desc->vd_data_offset));
}

ldms_mval_t ldms_metric_get_addr(ldms_set_t s, int i)
{
	ldms_mdesc_t desc = ldms_ptr_(struct ldms_value_desc, s->set->meta,
			__le32_to_cpu(s->set->meta->dict[i]));
	return ldms_ptr_(union ldms_value, s->set->meta,
			 __le32_to_cpu(desc->vd_data_offset));
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
	ldms_mdesc_t desc = ldms_ptr_(struct ldms_value_desc, s->set->meta,
			__le32_to_cpu(s->set->meta->dict[i]));
	if (metric_is_array(desc))
		return ldms_ptr_(union ldms_value, s->set->meta,
				 __le32_to_cpu(desc->vd_data_offset));
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
		*(uint32_t*)&mv->v_f = __cpu_to_le32(*(uint32_t*)&v->v_f);
		break;
	case LDMS_V_D64:
		*(uint64_t*)&mv->v_d = __cpu_to_le64(*(uint64_t*)&v->v_d);
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

	desc = ldms_ptr_(struct ldms_value_desc, s->set->meta,
			 __le32_to_cpu(s->set->meta->dict[i]));
	mv = ldms_ptr_(union ldms_value, s->set->meta,
		       __le32_to_cpu(desc->vd_data_offset));

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

	desc = ldms_ptr_(struct ldms_value_desc, s->set->meta,
			__le32_to_cpu(s->set->meta->dict[metric_idx]));
	dst = ldms_ptr_(union ldms_value, s->set->meta,
			__le32_to_cpu(desc->vd_data_offset));

	__metric_array_set(s, desc, dst, array_idx, src);
	__ldms_gn_inc(s->set, desc);
}

void ldms_metric_set_char(ldms_set_t s, int i, char v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __value_get(s->set, i, &desc);
	if (mv) {
		mv->v_char = v;
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_metric_set_u8(ldms_set_t s, int i, uint8_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __value_get(s->set, i, &desc);
	if (mv) {
		mv->v_u8 = v;
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_metric_set_s8(ldms_set_t s, int i, int8_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __value_get(s->set, i, &desc);
	if (mv) {
		mv->v_s8 = v;
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_metric_set_u16(ldms_set_t s, int i, uint16_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __value_get(s->set, i, &desc);
	if (mv) {
		mv->v_u16 = __cpu_to_le16(v);
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_metric_set_s16(ldms_set_t s, int i, int16_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __value_get(s->set, i, &desc);
	if (mv) {
		mv->v_s16 = __cpu_to_le16(v);
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_metric_set_u32(ldms_set_t s, int i, uint32_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __value_get(s->set, i, &desc);
	if (mv) {
		mv->v_u32 = __cpu_to_le32(v);
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_metric_set_s32(ldms_set_t s, int i, int32_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __value_get(s->set, i, &desc);
	if (mv) {
		mv->v_s32 = __cpu_to_le32(v);
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_metric_set_u64(ldms_set_t s, int i, uint64_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __value_get(s->set, i, &desc);
	if (mv) {
		mv->v_u64 = __cpu_to_le64(v);
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_metric_set_s64(ldms_set_t s, int i, int64_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __value_get(s->set, i, &desc);
	if (mv) {
		mv->v_s64 = __cpu_to_le64(v);
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_metric_set_float(ldms_set_t s, int i, float v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __value_get(s->set, i, &desc);
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
	ldms_mval_t mv = __value_get(s->set, i, &desc);
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
	ldms_mval_t mv = __value_get(s->set, mid, &desc);
	if (mv) {
		strncpy(mv->a_char, str, desc->vd_array_count);
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric index");
}

void ldms_metric_array_set_char(ldms_set_t s, int mid, int idx, char v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __value_get(s->set, mid, &desc);
	if (mv && (idx < desc->vd_array_count)) {
		mv->a_char[idx] = v;
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_metric_array_set_u8(ldms_set_t s, int mid, int idx, uint8_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __value_get(s->set, mid, &desc);
	if (mv && (idx < desc->vd_array_count)) {
		mv->a_u8[idx] = v;
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_metric_array_set_s8(ldms_set_t s, int mid, int idx, int8_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __value_get(s->set, mid, &desc);
	if (mv && (idx < desc->vd_array_count)) {
		mv->a_s8[idx] = v;
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_metric_array_set_u16(ldms_set_t s, int mid, int idx, uint16_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __value_get(s->set, mid, &desc);
	if (mv && (idx < desc->vd_array_count)) {
		mv->a_u16[idx] = __cpu_to_le16(v);
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_metric_array_set_s16(ldms_set_t s, int mid, int idx, int16_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __value_get(s->set, mid, &desc);
	if (mv && (idx < desc->vd_array_count)) {
		mv->a_s16[idx] = __cpu_to_le16(v);
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_metric_array_set_u32(ldms_set_t s, int mid, int idx, uint32_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __value_get(s->set, mid, &desc);
	if (mv && (idx < desc->vd_array_count)) {
		mv->a_u32[idx] = __cpu_to_le32(v);
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_metric_array_set_s32(ldms_set_t s, int mid, int idx, int32_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __value_get(s->set, mid, &desc);
	if (mv && (idx < desc->vd_array_count)) {
		mv->a_s32[idx] = __cpu_to_le32(v);
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_metric_array_set_u64(ldms_set_t s, int mid, int idx, uint64_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __value_get(s->set, mid, &desc);
	if (mv && (idx < desc->vd_array_count)) {
		mv->a_u64[idx] = __cpu_to_le64(v);
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_metric_array_set_s64(ldms_set_t s, int mid, int idx, int64_t v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __value_get(s->set, mid, &desc);
	if (mv && (idx < desc->vd_array_count)) {
		mv->a_s64[idx] = __cpu_to_le64(v);
		__ldms_gn_inc(s->set, desc);
	} else
		assert(0 == "Invalid metric or array index");
}

void ldms_metric_array_set_float(ldms_set_t s, int mid, int idx, float v)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __value_get(s->set, mid, &desc);
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
	ldms_mval_t mv = __value_get(s->set, mid, &desc);
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
	ldms_mval_t val = ldms_ptr_(union ldms_value, s->set->meta,
				    __le32_to_cpu(desc->vd_data_offset));
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
			val->a_u32[i] = __cpu_to_le16(mval->a_u32[i]);
		break;
	case LDMS_V_U64_ARRAY:
	case LDMS_V_S64_ARRAY:
	case LDMS_V_D64_ARRAY:
		for (i = start; i < start + count && i < desc->vd_array_count; i++)
			val->a_u64[i] = __cpu_to_le16(mval->a_u64[i]);
		break;
	default:
		assert(0 == "Invalid array element type");
	}
	__ldms_gn_inc(s->set, desc);
}

char ldms_metric_get_char(ldms_set_t s, int i)
{
	ldms_mval_t mv = __value_get(s->set, i, NULL);
	if (mv)
		return mv->v_char;
	else
		assert(0 == "Invalid metric index");
	return 0;
}

uint8_t ldms_metric_get_u8(ldms_set_t s, int i)
{
	ldms_mval_t mv = __value_get(s->set, i, NULL);
	if (mv)
		return mv->v_u8;
	else
		assert(0 == "Invalid metric index");
	return 0;
}

int8_t ldms_metric_get_s8(ldms_set_t s, int i)
{
	ldms_mval_t mv = __value_get(s->set, i, NULL);
	if (mv)
		return mv->v_s8;
	else
		assert(0 == "Invalid metric index");
	return 0;
}

uint16_t ldms_metric_get_u16(ldms_set_t s, int i)
{
	ldms_mval_t mv = __value_get(s->set, i, NULL);
	if (mv)
		return __le16_to_cpu(mv->v_u16);
	else
		assert(0 == "Invalid metric index");
	return 0;
}

int16_t ldms_metric_get_s16(ldms_set_t s, int i)
{
	ldms_mval_t mv = __value_get(s->set, i, NULL);
	if (mv)
		return __le16_to_cpu(mv->v_s16);
	else
		assert(0 == "Invalid metric index");
	return 0;
}

uint32_t ldms_metric_get_u32(ldms_set_t s, int i)
{
	ldms_mval_t mv = __value_get(s->set, i, NULL);
	if (mv)
		return __le32_to_cpu(mv->v_u32);
	else
		assert(0 == "Invalid metric index");
	return 0;
}

int32_t ldms_metric_get_s32(ldms_set_t s, int i)
{
	ldms_mval_t mv = __value_get(s->set, i, NULL);
	if (mv)
		return __le32_to_cpu(mv->v_s32);
	else
		assert(0 == "Invalid metric index");
	return 0;
}

uint64_t ldms_metric_get_u64(ldms_set_t s, int i)
{
	ldms_mval_t mv = __value_get(s->set, i, NULL);
	if (mv)
		return __le64_to_cpu(mv->v_u64);
	else
		assert(0 == "Invalid metric index");
	return 0;
}

int64_t ldms_metric_get_s64(ldms_set_t s, int i)
{
	ldms_mval_t mv = __value_get(s->set, i, NULL);
	if (mv)
		return __le64_to_cpu(mv->v_s64);
	else
		assert(0 == "Invalid metric index");
	return 0;
}

float ldms_metric_get_float(ldms_set_t s, int i)
{
	ldms_mval_t mv = __value_get(s->set, i, NULL);
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
	ldms_mval_t mv = __value_get(s->set, i, NULL);
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
	ldms_mval_t mv = __value_get(s->set, mid, &desc);
	if (mv)
		return mv->a_char;
	else
		assert(0 == "Invalid metric index");
	return 0;
}

char ldms_metric_array_get_char(ldms_set_t s, int mid, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __value_get(s->set, mid, &desc);
	if (mv && (idx < desc->vd_array_count))
		return mv->a_char[idx];
	else
		assert(0 == "Invalid metric or array index");
	return 0;
}

uint8_t ldms_metric_array_get_u8(ldms_set_t s, int mid, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __value_get(s->set, mid, &desc);
	if (mv && (idx < desc->vd_array_count))
		return mv->a_u8[idx];
	else
		assert(0 == "Invalid metric or array index");
	return 0;
}

int8_t ldms_metric_array_get_s8(ldms_set_t s, int mid, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __value_get(s->set, mid, &desc);
	if (mv && (idx < desc->vd_array_count))
		return mv->a_s8[idx];
	else
		assert(0 == "Invalid metric or array index");
	return 0;
}

uint16_t ldms_metric_array_get_u16(ldms_set_t s, int mid, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __value_get(s->set, mid, &desc);
	if (mv && (idx < desc->vd_array_count))
		return mv->a_u16[idx];
	else
		assert(0 == "Invalid metric or array index");
	return 0;
}

int16_t ldms_metric_array_get_s16(ldms_set_t s, int mid, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __value_get(s->set, mid, &desc);
	if (mv && (idx < desc->vd_array_count))
		return mv->a_s16[idx];
	else
		assert(0 == "Invalid metric or array index");
	return 0;
}

uint32_t ldms_metric_array_get_u32(ldms_set_t s, int mid, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __value_get(s->set, mid, &desc);
	if (mv && (idx < desc->vd_array_count))
		return mv->a_u32[idx];
	else
		assert(0 == "Invalid metric or array index");
	return 0;
}

int32_t ldms_metric_array_get_s32(ldms_set_t s, int mid, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __value_get(s->set, mid, &desc);
	if (mv && (idx < desc->vd_array_count))
		return mv->a_s32[idx];
	else
		assert(0 == "Invalid metric or array index");
	return 0;
}

uint64_t ldms_metric_array_get_u64(ldms_set_t s, int mid, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __value_get(s->set, mid, &desc);
	if (mv && (idx < desc->vd_array_count))
		return mv->a_u64[idx];
	else
		assert(0 == "Invalid metric or array index");
	return 0;
}

int64_t ldms_metric_array_get_s64(ldms_set_t s, int mid, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __value_get(s->set, mid, &desc);
	if (mv && (idx < desc->vd_array_count))
		return mv->a_s64[idx];
	else
		assert(0 == "Invalid metric or array index");
	return 0;
}

float ldms_metric_array_get_float(ldms_set_t s, int mid, int idx)
{
	ldms_mdesc_t desc;
	ldms_mval_t mv = __value_get(s->set, mid, &desc);
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
	ldms_mval_t mv = __value_get(s->set, mid, &desc);
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
	struct ldms_set_desc *sd = s;
	struct ldms_data_hdr *dh = sd->set->data;
	struct timeval tv;
	dh->trans.flags = LDMS_TRANSACTION_BEGIN;
	(void)gettimeofday(&tv, NULL);
	dh->trans.ts.sec = __cpu_to_le32(tv.tv_sec);
	dh->trans.ts.usec = __cpu_to_le32(tv.tv_usec);
	return 0;
}

int ldms_transaction_end(ldms_set_t s)
{
	struct ldms_set_desc *sd = s;
	struct ldms_data_hdr *dh = sd->set->data;
	struct timeval tv;
	(void)gettimeofday(&tv, NULL);
	dh->trans.dur.sec = __cpu_to_le32(tv.tv_sec - __le32_to_cpu(dh->trans.ts.sec));
	dh->trans.dur.usec = __cpu_to_le32(tv.tv_usec - __le32_to_cpu(dh->trans.ts.usec));
	dh->trans.ts.sec = __cpu_to_le32(tv.tv_sec);
	dh->trans.ts.usec = __cpu_to_le32(tv.tv_usec);
	dh->trans.flags = LDMS_TRANSACTION_END;
	return 0;
}

struct ldms_timestamp ldms_transaction_timestamp_get(ldms_set_t s)
{
	struct ldms_set_desc *sd = s;
	struct ldms_data_hdr *dh = sd->set->data;
	struct ldms_timestamp ts = {
		.sec = __le32_to_cpu(dh->trans.ts.sec),
		.usec = __le32_to_cpu(dh->trans.ts.usec),
	};
	return ts;
}

struct ldms_timestamp ldms_transaction_duration_get(ldms_set_t s)
{
	struct ldms_set_desc *sd = s;
	struct ldms_data_hdr *dh = sd->set->data;
	struct ldms_timestamp ts = {
		.sec = __le32_to_cpu(dh->trans.dur.sec),
		.usec = __le32_to_cpu(dh->trans.dur.usec),
	};
	return ts;
}

int ldms_set_is_consistent(ldms_set_t s)
{
	struct ldms_set_desc *sd = s;
	struct ldms_data_hdr *dh = sd->set->data;
	return (dh->trans.flags == LDMS_TRANSACTION_END);
}

void ldms_version_get(struct ldms_version *v)
{
	LDMS_VERSION_SET(*v);
}

int ldms_version_check(struct ldms_version *v)
{
	return LDMS_VERSION_EQUAL(*v);
}
