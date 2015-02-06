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
#include "ldms.h"
#include "ldms_xprt.h"
#include "coll/rbt.h"
#include <limits.h>
#include <assert.h>
#include <mmalloc/mmalloc.h>
#include "ldms_private.h"
#include <pthread.h>

#define SET_DIR_PATH "/var/run/ldms"
static char *__set_dir = SET_DIR_PATH;
#define SET_DIR_LEN sizeof(SET_DIR_PATH)
static char __set_path[PATH_MAX];

char *_create_path(const char *set_name);

static int set_comparator(void *a, void *b)
{
	char *x = a;
	char *y = b;

	return strcmp(x, y);
}
static struct rbt set_tree = {
	.root = NULL,
	.comparator = set_comparator
};
pthread_mutex_t set_tree_lock = PTHREAD_MUTEX_INITIALIZER;

void ldms_release_local_set(struct ldms_set *set)
{
	pthread_mutex_unlock(&set_tree_lock);
}

struct ldms_set *ldms_find_local_set(const char *set_name)
{
	struct rbn *z;
	struct ldms_set *s = NULL;

	pthread_mutex_lock(&set_tree_lock);
	z = rbt_find(&set_tree, (void *)set_name);
	if (z)
		s = container_of(z, struct ldms_set, rb_node);
	else
		pthread_mutex_unlock(&set_tree_lock);

	return s;
}

extern ldms_set_t ldms_get_set(const char *set_name)
{
	struct ldms_set_desc *sd = NULL;
	struct ldms_set *set = ldms_find_local_set(set_name);
	if (!set)
		goto out;

	sd = calloc(1, sizeof *sd);
	if (!sd) {
		ldms_release_local_set(set);
		return NULL;
	}
	sd->set = set;
	ldms_release_local_set(set);
 out:
	return sd;
}

uint64_t ldms_get_meta_gn(ldms_set_t _set)
{
	struct ldms_set_desc *sd = _set;
	return sd->set->meta->meta_gn;
}

uint64_t ldms_get_data_gn(ldms_set_t _set)
{
	struct ldms_set_desc *sd = _set;
	return sd->set->data->gn;
}

uint32_t ldms_get_size(ldms_set_t s)
{
	return s->set->meta->meta_sz + s->set->meta->data_sz;
}

uint32_t __ldms_get_size(struct ldms_set *set)
{
	return set->meta->meta_sz + set->meta->data_sz;
}

uint32_t _get_max_size(struct ldms_set *s)
{
	return s->meta->data_sz;
}

uint32_t ldms_get_max_size(ldms_set_t _set)
{
	struct ldms_set_desc *sd = _set;
	return _get_max_size(sd->set);
}

static uint32_t _get_cardinality(struct ldms_set *s)
{
	return s->meta->card;
}

uint32_t ldms_get_cardinality(ldms_set_t _set)
{
	struct ldms_set_desc *sd = _set;
	return _get_cardinality(sd->set);
}


static void rem_local_set(struct ldms_set *s)
{
	pthread_mutex_lock(&set_tree_lock);
	rbt_del(&set_tree, &s->rb_node);
	pthread_mutex_unlock(&set_tree_lock);
}

static void add_local_set(struct ldms_set *s)
{
	struct rbn *z;
	s->rb_node.key = get_instance_name(s->meta)->name;
	pthread_mutex_lock(&set_tree_lock);
	z = rbt_find(&set_tree, get_instance_name(s->meta)->name);
	assert(NULL == z);
	rbt_ins(&set_tree, &s->rb_node);
	pthread_mutex_unlock(&set_tree_lock);
}

struct cb_arg {
	void *user_arg;
	int (*user_cb)(struct ldms_set *, void *);
};

static int rbn_cb(struct rbn *rbn, void *arg, int level)
{
	struct cb_arg *cb_arg = arg;
	struct ldms_set *set = container_of(rbn, struct ldms_set, rb_node);
	return cb_arg->user_cb(set, cb_arg->user_arg);
}

int __ldms_for_all_sets(int (*cb)(struct ldms_set *, void *), void *arg)
{
	struct cb_arg user_arg = { arg, cb };
	int rc;
	pthread_mutex_lock(&set_tree_lock);
	rc = rbt_traverse(&set_tree, rbn_cb, &user_arg);
	pthread_mutex_unlock(&set_tree_lock);
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

int set_list_sz_cb(struct ldms_set *set, void *arg)
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
	int rc;

	arg.count = 0;
	arg.set_list_len = 0;
	rc = __ldms_for_all_sets(set_list_sz_cb, &arg);
	*set_count = arg.count;
	*set_list_size = arg.set_list_len;
}
static int __record_set(const char *instance_name, ldms_set_t *s,
			struct ldms_set_hdr *sh, struct ldms_data_hdr *dh, int flags)
{
	struct ldms_set *set;
	struct ldms_set_desc *sd;

	set = ldms_find_local_set(instance_name);
	if (set) {
		ldms_release_local_set(set);
		return -EEXIST;
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

	add_local_set(set);

	return 0;

 out_1:
	free(sd);
 out_0:
	return ENOMEM;
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

int ldms_remote_update(ldms_t t, ldms_set_t s, ldms_update_cb_t cb, void *arg);
int ldms_update(ldms_set_t s, ldms_update_cb_t cb, void *arg)
{
	struct ldms_set *set = ((struct ldms_set_desc *)s)->set;
	if (set->flags & LDMS_SET_F_REMOTE)
		return ldms_remote_update(__get_xprt(s), s, cb, arg);
	cb(__get_xprt(s), s, 0, arg);
	return 0;
}

void _release_set(struct ldms_set *set)
{
	rem_local_set(set);

	if (set->flags & (LDMS_SET_F_MEMMAP | LDMS_SET_F_FILEMAP)) {
		munmap(set->meta, set->meta->meta_sz);
		munmap(set->data, set->meta->data_sz);
	}
	free(set);
}

void ldms_set_release(ldms_set_t set_p)
{
	struct ldms_set_desc *sd = (struct ldms_set_desc *)set_p;
	if (sd->rbd)
		ldms_free_rbd(sd->rbd);
	if (LIST_EMPTY(&sd->set->rbd_list))
		_release_set(sd->set);
	free(sd);
}

void ldms_destroy_set(ldms_set_t s)
{
	struct ldms_set_desc *sd = (struct ldms_set_desc *)s;
	struct ldms_set *set = sd->set;
	struct ldms_rbuf_desc *rbd;

	__ldms_dir_del_set(get_instance_name(set->meta)->name);

	while (!LIST_EMPTY(&set->rbd_list)) {
		rbd = LIST_FIRST(&set->rbd_list);
		ldms_free_rbd(rbd);
	}

	if (set->flags & LDMS_SET_F_FILEMAP) {
		unlink(_create_path(get_instance_name(sd->set->meta)->name));
		strcat(__set_path, ".META");
		unlink(__set_path);
	}
	mm_free(set->meta);
	rem_local_set(set);
	free(set);
	free(sd);
}

int ldms_lookup(ldms_t _x, const char *path,
		ldms_lookup_cb_t cb, void *cb_arg)
{
	struct ldms_xprt *x = (struct ldms_xprt *)_x;
	struct ldms_set *set;
	ldms_set_t s;
	if (strlen(path) > LDMS_LOOKUP_PATH_MAX)
		return EINVAL;

	return __ldms_remote_lookup(_x, path, cb, cb_arg);
}

int ldms_dir(ldms_t x, ldms_dir_cb_t cb, void *cb_arg, uint32_t flags)
{
	return __ldms_remote_dir(x, cb, cb_arg, flags);
}

void ldms_dir_cancel(ldms_t x)
{
	__ldms_remote_dir_cancel(x);
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
		if (*p == '\0')
			return NULL;
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

const char *ldms_get_schema_name(ldms_set_t s)
{
	struct ldms_set_desc *sd = (struct ldms_set_desc *)s;
	ldms_name_t name = get_schema_name(sd->set->meta);
	return name->name;
}

const char *ldms_get_instance_name(ldms_set_t s)
{
	struct ldms_set_desc *sd = (struct ldms_set_desc *)s;
	ldms_name_t name = get_instance_name(sd->set->meta);
	return name->name;
}

const char *ldms_get_producer_name(ldms_set_t s)
{
	struct ldms_set_desc *sd = (struct ldms_set_desc *)s;
	return sd->set->meta->producer_name;
}

int ldms_set_producer_name(ldms_set_t s, const char *name)
{
	if (LDMS_PRODUCER_NAME_MAX < strlen(name))
		return EINVAL;

	struct ldms_set_desc *sd = (struct ldms_set_desc *)s;
	snprintf(sd->set->meta->producer_name, LDMS_PRODUCER_NAME_MAX, name);
	return 0;
}

int __ldms_create_set(const char *instance_name,
		      struct ldms_lookup_msg *lm,
		      ldms_set_t *s, uint32_t flags)
{
	struct ldms_data_hdr *data;
	struct ldms_set_hdr *meta;
	struct ldms_value_desc *vd;
	ldms_mdef_t m;
	int rc;

	meta = mm_alloc(lm->meta_len + lm->data_len);
	if (!meta) {
		rc = ENOMEM;
		goto out_0;
	}
	meta->version = LDMS_VERSION;
	meta->meta_sz = lm->meta_len;

	data = (struct ldms_data_hdr *)((unsigned char*)meta + lm->meta_len);
	meta->data_sz = lm->data_len;
	data->size = lm->data_len;

	/* Initialize the metric set header */
	if (flags & LDMS_SET_F_LOCAL)
		meta->meta_gn = 1;
	else
		/* This tells ldms_update that we've never received
		 * the remote meta data */
		meta->meta_gn = 0;
	meta->card = lm->card;
	meta->flags = LDMS_SETH_F_LCLBYTEORDER;

	ldms_name_t lname = get_instance_name(meta);
	lname->len = strlen(instance_name) + 1;
	strcpy(lname->name, instance_name);

	data->gn = data->meta_gn = meta->meta_gn;

	rc = __record_set(instance_name, s, meta, data, flags);
	if (rc)
		goto out_1;
	__ldms_dir_add_set(instance_name);
	return 0;

 out_1:
	mm_free(meta);
 out_0:
	return rc;
}

#define LDMS_GRAIN_MMALLOC 1024

int ldms_init(size_t max_size)
{
	size_t grain = LDMS_GRAIN_MMALLOC;
	if (mm_init(max_size, grain))
		return -1;
	return 0;
}

ldms_schema_t ldms_create_schema(const char *schema_name)
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

void ldms_destroy_schema(ldms_schema_t schema)
{
	int i;
	ldms_mdef_t m;

	while (!STAILQ_EMPTY(&schema->metric_list)) {
		m = STAILQ_FIRST(&schema->metric_list);
		STAILQ_REMOVE_HEAD(&schema->metric_list, entry);
		free(m->name);
		free(m);
	}
	free(schema->name);
	free(schema);
}

int ldms_get_metric_count(ldms_schema_t schema)
{
	return schema->metric_count;
}

static int value_size[] = {
	[LDMS_V_NONE] = 0,
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
	[LDMS_V_LD128] = sizeof(long double),
};

int ldms_create_set(const char *instance_name, ldms_schema_t schema, ldms_set_t *s)
{
	struct ldms_data_hdr *data;
	struct ldms_set_hdr *meta;
	struct ldms_value_desc *vd;
	size_t meta_sz;
	uint64_t value_off;
	ldms_mdef_t md;
	int metric_idx;
	int rc;

	if (!instance_name || !schema)
		return EINVAL;

	meta_sz = schema->meta_sz /* header + metric dict */
		+ strlen(schema->name) + 2 /* schema name + '\0' + len */
		+ strlen(instance_name) + 2; /* instance name + '\0' + len */
	meta_sz = roundup(meta_sz, 8);
	meta = mm_alloc(meta_sz + schema->data_sz);
	if (!meta)
		return ENOMEM;

	meta->version = LDMS_VERSION;
	meta->card = schema->metric_count;
	meta->meta_sz = meta_sz;

	data = (struct ldms_data_hdr *)((unsigned char*)meta + meta_sz);
	meta->data_sz = schema->data_sz;
	data->size = schema->data_sz;

	/* Initialize the metric set header */
	meta->meta_gn = 1;
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
	value_off = roundup(sizeof(*data), 8);
	metric_idx = 0;
	size_t vd_size = 0;
	STAILQ_FOREACH(md, &schema->metric_list, entry) {
		/* Add descriptor to dictionary */
		meta->dict[metric_idx] = ldms_off_(meta, vd);

		/* Build the descriptor */
		vd->type = md->type;
		vd->name_len = strlen(md->name) + 1;
		strncpy(vd->name, md->name, vd->name_len);
		vd->data_offset = value_off;

		/* Advance to next descriptor */
		metric_idx++;
		vd = (struct ldms_value_desc *)((char *)vd + md->meta_sz);
		value_off += roundup(value_size[md->type], 8);
		vd_size += md->meta_sz;
	}
	rc = __record_set(instance_name, s, meta, data, LDMS_SET_F_LOCAL);
	if (rc)
		goto out_1;
	__ldms_dir_add_set(instance_name);
	return 0;

 out_1:
	mm_free(meta);
	return rc;
}

const char *ldms_get_set_name(ldms_set_t _set)
{
	struct ldms_set_desc *sd = (struct ldms_set_desc *)_set;
	struct ldms_set_hdr *sh = sd->set->meta;
	return get_instance_name(sh)->name;
}

const char *ldms_get_set_schema_name(ldms_set_t _set)
{
	struct ldms_set_desc *sd = (struct ldms_set_desc *)_set;
	struct ldms_set_hdr *sh = sd->set->meta;
	return get_schema_name(sh)->name;
}

uint32_t ldms_get_set_card(ldms_set_t _set)
{
	struct ldms_set_desc *sd = (struct ldms_set_desc *)_set;
	return sd->set->meta->card;
}

int ldms_mmap_set(void *meta_addr, void *data_addr, ldms_set_t *s)
{
	struct ldms_set_hdr *sh = meta_addr;
	struct ldms_data_hdr *dh = data_addr;
	int flags;

	flags = LDMS_SET_F_MEMMAP | LDMS_SET_F_LOCAL;
	int rc = __record_set(get_instance_name(sh)->name, s, sh, dh, flags);
	return rc;
}

static char *type_names[] = {
	[LDMS_V_NONE] = "none",
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
};

void ldms_get_metric_size(const char *name, enum ldms_value_type t,
			  size_t *meta_sz, size_t *data_sz)
{
	/* Descriptors are aligned on eight byte boundaries */
	*meta_sz = roundup(sizeof(struct ldms_value_desc) + strlen(name) + 1, 8);

	/* Values are aligned on 8b boundary */
	*data_sz = roundup(value_size[t], 8);
}

static void __get_value(struct ldms_set *s, int idx,
			struct ldms_value_desc **pvd, union ldms_value **pv)
{
	*pvd = ldms_ptr_(struct ldms_value_desc, s->meta, s->meta->dict[idx]);
	*pv = ldms_ptr_(union ldms_value, s->data, (*pvd)->data_offset);
}

void ldms_print_set_metrics(ldms_set_t _set)
{
	struct ldms_set_desc *sd = _set;
	struct ldms_value_desc *vd;
	union ldms_value *v;
	int i;

	printf("--------------------------------\n");
	printf("schema: '%s'\n", get_schema_name(sd->set->meta)->name);
	printf("instance: '%s'\n", get_instance_name(sd->set->meta)->name);
	printf("metadata size : %" PRIu32 "\n", sd->set->meta->meta_sz);
	printf("    data size : %" PRIu32 "\n", sd->set->meta->data_sz);
	printf("  metadata gn : %" PRIu64 "\n", sd->set->meta->meta_gn);
	printf("      data gn : %" PRIu64 "\n", sd->set->data->gn);
	printf("         card : %" PRIu32 "\n", sd->set->meta->card);
	printf("--------------------------------\n");
	for (i = 0; i < sd->set->meta->card; i++) {
		__get_value(sd->set, i, &vd, &v);
		printf("  %32s[%4s] = ", vd->name, type_names[vd->type]);
		switch (vd->type) {
		case LDMS_V_U8:
			printf("%2hhu\n", v->v_u8);
			break;
		case LDMS_V_S8:
			printf("%hhd\n", v->v_s8);
			break;
		case LDMS_V_U16:
			printf("%4hu\n", v->v_u16);
			break;
		case LDMS_V_S16:
			printf("%hd\n", v->v_s16);
			break;
		case LDMS_V_U32:
			printf("%8u\n", v->v_u32);
			break;
		case LDMS_V_S32:
			printf("%d\n", v->v_s32);
			break;
		case LDMS_V_U64:
			printf("%" PRIu64 "\n", v->v_u64);
			break;
		case LDMS_V_S64:
			printf("%" PRId64 "\n", v->v_s64);
			break;
		case LDMS_V_F32:
			printf("%.2f", v->v_f);
			break;
		case LDMS_V_D64:
			printf("%.2f", v->v_d);
			break;
		case LDMS_V_LD128:
			printf("%.2Lf", v->v_ld);
			break;
		}
	}
}

void ldms_visit_metrics(ldms_set_t _set, ldms_visit_cb_t cb, void *arg)
{
	struct ldms_set_desc *sd = _set;
	struct ldms_value_desc *vd;
	union ldms_value *v;
	int i;

	for (i = 0; i < sd->set->meta->card; i++) {
		__get_value(sd->set, i, &vd, &v);
		cb(vd, v, arg);
	}
}

struct ldms_value_desc *ldms_first(struct ldms_iterator *i, ldms_set_t _set)
{
	struct ldms_set_desc *sd = _set;

	if (!sd->set->meta->card)
		return NULL;

	i->set = sd->set;
	i->curr_idx = 0;
	__get_value(i->set, i->curr_idx, &i->curr_desc, &i->curr_value);
	return i->curr_desc;
}

struct ldms_value_desc *ldms_next(struct ldms_iterator *i)
{
	i->curr_idx ++;
	if (i->curr_idx >= i->set->meta->card)
		return NULL;

	__get_value(i->set, i->curr_idx, &i->curr_desc, &i->curr_value);
	return i->curr_desc;
}

const char *ldms_get_metric_name(ldms_metric_t _m)
{
	struct ldms_metric *m = (struct ldms_metric *)_m;
	return m->desc->name;
}

enum ldms_value_type ldms_get_metric_type(ldms_metric_t _m)
{
	struct ldms_metric *m = (struct ldms_metric *)_m;
	return m->desc->type;
}

ldms_metric_t ldms_get_metric(ldms_set_t _set, const char *name)
{
	struct ldms_value_desc *vd;
	struct ldms_iterator i;

	for (vd = ldms_first(&i, _set); vd; vd = ldms_next(&i)) {
		if (0 == strcmp(vd->name, name)) {
			struct ldms_metric *m = malloc(sizeof *m);
			if (!m)
				return NULL;
			m->desc = ldms_iter_desc(&i);
			m->value = ldms_iter_value(&i);
			m->set = ((struct ldms_set_desc *)_set)->set;
			return (ldms_metric_t)m;
		}
	}
	return NULL;
}

ldms_metric_t ldms_metric_get(ldms_set_t _set, int idx, struct ldms_metric *metric)
{
	struct ldms_value_desc *vd;
	union ldms_value *v;
	struct ldms_set *s = _set->set;

	if (idx >= s->meta->card)
		return NULL;

	vd = ldms_ptr_(struct ldms_value_desc, s->meta, s->meta->dict[idx]);
	v = ldms_ptr_(union ldms_value, s->data, vd->data_offset);

	metric->set = s;
	metric->desc = vd;
	metric->value = v;

	return metric;
}

ldms_metric_t ldms_make_metric(ldms_set_t _set, struct ldms_value_desc *vd)
{
	struct ldms_data_hdr *dh;
	struct ldms_metric *m = malloc(sizeof *m);
	if (!m)
		return NULL;
	m->desc = vd;
	m->set = ((struct ldms_set_desc *)_set)->set;
	dh = m->set->data;
	m->value = ldms_ptr_(union ldms_value, dh, vd->data_offset);
	return m;
}

void ldms_metric_release(ldms_metric_t m)
{
	free(m);
}

static inline uint64_t next_gn(uint64_t _gn)
{
	uint64_t gn = _gn +1;
	if (gn == 0)
		gn ++;
	return gn;
}

int ldms_add_metric(ldms_schema_t s, const char *name, enum ldms_value_type type)
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
	ldms_get_metric_size(name, type, &m->meta_sz, &m->data_sz);
	STAILQ_INSERT_TAIL(&s->metric_list, m, entry);
	s->metric_count++;
	s->meta_sz += m->meta_sz + sizeof(uint32_t) /* + dict entry */;
	s->data_sz += m->data_sz;
	return s->metric_count - 1;
}

static struct _ldms_type_name_map {
	const char *name;
	enum ldms_value_type type;
} type_name_map[] = {
	{ "D64", LDMS_V_D64, },
	{ "F32", LDMS_V_F32, },
	{ "NONE", LDMS_V_NONE, },
	{ "LD128", LDMS_V_LD128, },
	{ "S16", LDMS_V_S16, },
	{ "S32", LDMS_V_S32, },
	{ "S64", LDMS_V_S64, },
	{ "S8", LDMS_V_S8, },
	{ "U16", LDMS_V_U16, },
	{ "U32", LDMS_V_U32, },
	{ "U64", LDMS_V_U64, },
	{ "U8", LDMS_V_U8, },

};

int comparator(const void *a, const void *b)
{
	const char *n1 = a;
	const struct _ldms_type_name_map *el = b;
	return strcasecmp(n1, el->name);
}

enum ldms_value_type ldms_str_to_type(const char *name)
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

const char *ldms_type_to_str(enum ldms_value_type t)
{
	if (t > LDMS_V_LAST)
		t = LDMS_V_NONE;
	return type_names[t];
}

int ldms_begin_transaction(ldms_set_t s)
{
	struct ldms_set_desc *sd = s;
	struct ldms_data_hdr *dh = sd->set->data;
	struct timeval tv;
	dh->trans.flags = LDMS_TRANSACTION_BEGIN;
	(void)gettimeofday(&tv, NULL);
	dh->trans.ts.sec = tv.tv_sec;
	dh->trans.ts.usec = tv.tv_usec;
	return 0;
}

int ldms_end_transaction(ldms_set_t s)
{
	struct ldms_set_desc *sd = s;
	struct ldms_data_hdr *dh = sd->set->data;
	struct timeval tv;
	(void)gettimeofday(&tv, NULL);
	dh->trans.dur.sec = tv.tv_sec - dh->trans.ts.sec;
	dh->trans.dur.usec = tv.tv_usec - dh->trans.ts.usec;
	dh->trans.ts.sec = tv.tv_sec;
	dh->trans.ts.usec = tv.tv_usec;
	dh->trans.flags = LDMS_TRANSACTION_END;
	return 0;
}

struct ldms_timestamp const *ldms_get_transaction_timestamp(ldms_set_t s)
{
	struct ldms_set_desc *sd = s;
	struct ldms_data_hdr *dh = sd->set->data;
	return &dh->trans.ts;
}

struct ldms_timestamp const *ldms_get_transaction_duration(ldms_set_t s)
{
	struct ldms_set_desc *sd = s;
	struct ldms_data_hdr *dh = sd->set->data;
	return &dh->trans.dur;
}

int ldms_is_set_consistent(ldms_set_t s)
{
	struct ldms_set_desc *sd = s;
	struct ldms_data_hdr *dh = sd->set->data;
	return (dh->trans.flags == LDMS_TRANSACTION_END);
}

ldms_mvec_t ldms_mvec_create(int count)
{
	ldms_mvec_t mvec = malloc(sizeof(*mvec) + count *
				  sizeof(ldms_metric_t));
	if (!mvec)
		return NULL;
	mvec->count = count;
	return mvec;
}

void ldms_mvec_destroy(ldms_mvec_t mvec)
{
	free(mvec);
}

