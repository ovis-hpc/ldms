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
#include "ogc_rbt.h"
#include <limits.h>
#ifdef ENABLE_MMAP
#include <ftw.h>
#endif
#include <mmalloc/mmalloc.h>
#include "ldms_private.h"

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
static struct ogc_rbt set_tree = {
	.root = NULL,
	.comparator = set_comparator
};

struct ldms_set *ldms_find_local_set(const char *set_name)
{
	struct ogc_rbn *z;
	struct ldms_set *s = NULL;

	z = ogc_rbt_find(&set_tree, (void *)set_name);
	if (z)
		s = ogc_container_of(z, struct ldms_set, rb_node);

	return s;
}

extern ldms_set_t ldms_get_set(const char *set_name)
{
	struct ldms_set_desc *sd = NULL;
	struct ldms_set *set = ldms_find_local_set(set_name);
	if (!set)
		goto out;

	sd = calloc(1, sizeof *sd);
	sd->set = set;

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

static uint32_t _get_size(struct ldms_set *s)
{
	return s->meta->tail_off;
}

uint32_t ldms_get_size(ldms_set_t _set)
{
	struct ldms_set_desc *sd = _set;
	return _get_size(sd->set);
}


static uint32_t _get_max_size(struct ldms_set *s)
{
	return s->meta->data_size;
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
	ogc_rbt_del(&set_tree, &s->rb_node);
}

static void add_local_set(struct ldms_set *s)
{
	s->rb_node.key = s->meta->name;
	ogc_rbt_ins(&set_tree, &s->rb_node);
}

static int visit_subtree(struct ogc_rbn *n,
			 int (*cb)(struct ldms_set *, void *arg),
			 void *arg)
{
	struct ldms_set *set;
	int rc;

	if (!ogc_rbt_is_leaf(n)) {
		if (!n->left || !n->right) {
			printf("Corrupted set tree %p\n", n);
			return -1;
		}
		rc = visit_subtree(n->left, cb, arg);
		if (rc)
			goto err;
		set = ogc_container_of(n, struct ldms_set, rb_node);
		rc = cb(set, arg);
		if (rc)
			goto err;
		rc = visit_subtree(n->right, cb, arg);
		if (rc)
			goto err;
	}
	return 0;

 err:
	return rc;
}

int __ldms_for_all_sets(int (*cb)(struct ldms_set *, void *), void *arg)
{
	if (set_tree.root)
		return visit_subtree(set_tree.root, cb, arg);
	return 0;
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

	len = strlen(set->meta->name) + 1;
	if (len > a->set_list_len)
		return -ENOMEM;

	a->count++;
	strcpy(a->set_list, set->meta->name);
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

	len = strlen(set->meta->name) + 1;
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
static int __record_set(const char *set_name, ldms_set_t *s,
			struct ldms_set_hdr *sh, struct ldms_data_hdr *dh, int flags)
{
	struct ldms_set *set;
	struct ldms_set_desc *sd;

	set = ldms_find_local_set(set_name);
	if (set)
		return -EEXIST;

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
		munmap(set->meta, set->meta->meta_size);
		munmap(set->data, set->meta->data_size);
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

	__ldms_dir_del_set(set->meta->name);

	while (!LIST_EMPTY(&set->rbd_list)) {
		rbd = LIST_FIRST(&set->rbd_list);
		ldms_free_rbd(rbd);
	}

	if (set->flags & LDMS_SET_F_FILEMAP) {
		unlink(_create_path(sd->set->meta->name));
		strcat(__set_path, ".META");
		unlink(__set_path);
	}
#ifdef ENABLE_MMAP
	munmap(set->data, set->meta->data_size);
	munmap(set->meta, set->meta->meta_size);
#else
	mm_free(set->meta);
#endif
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
#ifdef ENABLE_MMAP
	int rc = ENOMEM;
	struct ldms_set_hdr *meta;
	struct ldms_data_hdr *data;
	size_t meta_size, data_size;
	int flags;
#endif
	if (strlen(path) > LDMS_LOOKUP_PATH_MAX)
		return EINVAL;

	if (strcmp(x->name, "local"))
		return __ldms_remote_lookup(_x, path, cb, cb_arg);

	/* See if it's in my process */
	set = ldms_find_local_set(path);
#ifndef ENABLE_MMAP
	if (set) {
		struct ldms_set_desc *sd = malloc(sizeof *sd);
		if (!sd)
			return ENOMEM;
		sd->set = set;
		s = sd;
		return 0;
	}
	return ENODEV;
#else
	if (set) {
		struct ldms_set_desc *sd = malloc(sizeof *sd);
		if (!sd)
			goto err_0;
		sd->set = set;
		s = sd;
		goto out;
	}
	meta = _open_and_map_file(path, META_FILE, 0, &meta_size);
	if (!meta)
		goto err_0;
	data = _open_and_map_file(path, DATA_FILE, 0, &data_size);
	if (!data)
		goto err_1;

	flags = LDMS_SET_F_LOCAL | LDMS_SET_F_FILEMAP;
	rc = __record_set(path, &s, meta, data, flags);
	if (rc)
		goto err_2;
 out:
	cb(_x, 0, s, cb_arg);
	return 0;
 err_2:
#ifdef ENABLE_MMAP
	munmap(data, data_size);
#else
	free(data);
#endif
 err_1:
#ifdef ENABLE_MMAP
	munmap(meta, meta_size);
#else
	free(data);
#endif
 err_0:
	return rc;
#endif
}

#ifdef ENABLE_MMAP
static struct ftw_context {
	char *set_list;
	int set_count;
	size_t set_list_size;
} local_dir_context;

static int local_dir_cb(const char *fpath, const struct stat *sb, int typeflag)
{
	const char *p;
	int c, len;
	if (typeflag != FTW_F)
		return 0;

	p = &fpath[SET_DIR_LEN];
	len = strlen(p);
	if (0 == strcmp(&p[len-5], ".META"))
		return 0;

	if (ldms_find_local_set(p))
		return 0;

	c = strlen(p) + 1;
	if (c > local_dir_context.set_list_size)
		return 1;

	local_dir_context.set_list_size -= c;;
	local_dir_context.set_count ++;
	strcpy(local_dir_context.set_list, p);
	local_dir_context.set_list += c;
	return 0;
}

static int local_dir(int *set_count, char *set_list, size_t *set_list_sz)
{
	int rc;
	int set_list_size;

	__ldms_get_local_set_list_sz(set_count, &set_list_size);
	if (set_list_size > *set_list_sz) {
		*set_list_sz = set_list_size;
		return E2BIG;
	}
	rc = __ldms_get_local_set_list(set_list, *set_list_sz,
				       set_count, &set_list_size);

	local_dir_context.set_list = &set_list[set_list_size];
	local_dir_context.set_count = *set_count;

	local_dir_context.set_list_size = *set_list_sz - set_list_size;

	rc = ftw(__set_dir, local_dir_cb, 100);
	*set_count = local_dir_context.set_count;
	if (rc) {
		perror("ftw: ");
	}
	return 0;
}
#endif

int ldms_dir(ldms_t x, ldms_dir_cb_t cb, void *cb_arg, uint32_t flags)
{
	struct ldms_xprt *_x = (struct ldms_xprt *)x;
#ifdef ENABLE_MMAP
	if (0 == strcmp(_x->name, "local"))
		return local_dir(x, cb, cb_arg, flags);
#endif
	if (0 == strcmp(_x->name, "local"))
		fprintf(stderr,"warning: ldms_dir not implemented "
			"for transport 'local' unless MMAP enabled.\n");
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
	sprintf(__set_path, "%s/", __set_dir);
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

#ifdef ENABLE_MMAP
int _ldms_create_set(const char *set_name, size_t meta_sz, size_t data_sz,
		     ldms_set_t *s, int flags)
{
	struct ldms_data_hdr *data;
	struct ldms_set_hdr *meta;
	struct ldms_value_desc *vd;
	char *path;
	int rc;

	path = _create_path(set_name);
	if (!path)
		return ENOENT;

	data_sz += sizeof(struct ldms_data_hdr);
	meta_sz += sizeof(struct ldms_set_hdr);

	meta = _open_and_map_file(set_name, META_FILE, 1, &meta_sz);
	if (!meta) {
		rc = ENOMEM;
		goto out_0;
	}
	meta->meta_size = meta_sz;

	data = _open_and_map_file(set_name, DATA_FILE, 1, &data_sz);
	if (!data) {
		rc =ENOMEM;
		goto out_1;
	}
	meta->data_size = data_sz;
	data->size = data_sz;

	/* Initialize the metric set header */
	if (flags & LDMS_SET_F_LOCAL)
		meta->meta_gn = 1;
	else
		/* This tells ldms_update that we've never received
		 * the remote meta data */
		meta->meta_gn = 0;
	meta->card = 0;
	meta->head_off = sizeof(*meta);
	meta->tail_off = sizeof(*meta);
	meta->flags = LDMS_SETH_F_LCLBYTEORDER;
	strncpy(meta->name, set_name, LDMS_SET_NAME_MAX-1);
	meta->name[LDMS_SET_NAME_MAX-1] = '\0';

	data->gn = data->meta_gn = meta->meta_gn;
	data->head_off = sizeof(*data);
	data->tail_off = sizeof(*data);

	/* Initialize the first value descriptor so that tail->next_offset
	 * is the head. This normalizes the allocation logic in
	 * ldms_add_metric. */
	vd = ldms_ptr_(struct ldms_value_desc, meta, meta->head_off);
	vd->next_offset = meta->head_off;
	vd->type = LDMS_V_NONE;
	vd->name_len = sizeof("<empty>");
	strcpy(vd->name, "<empty>");

	rc = __record_set(set_name, s, meta, data, flags);
	if (rc)
		goto out_1;
	__ldms_dir_add_set(set_name);
	return 0;
 out_2:
	munmap(data, data_sz);
 out_1:
	munmap(meta, meta_sz);
 out_0:
	return rc;
}
#else
int __ldms_create_set(const char *set_name, size_t meta_sz, size_t data_sz,
		      ldms_set_t *s, uint32_t flags)
{
	struct ldms_data_hdr *data;
	struct ldms_set_hdr *meta;
	struct ldms_value_desc *vd;
	int rc;

	meta_sz = (meta_sz + 7) & ~7;
	data_sz += sizeof(struct ldms_data_hdr);
	meta_sz += sizeof(struct ldms_set_hdr);

	meta = mm_alloc(meta_sz + data_sz);
	if (!meta) {
		rc = ENOMEM;
		goto out_0;
	}
	meta->version = LDMS_VERSION;
	meta->meta_size = meta_sz;

	data = (struct ldms_data_hdr *)((unsigned char*)meta + meta_sz);
	meta->data_size = data_sz;
	data->size = data_sz;

	/* Initialize the metric set header */
	if (flags & LDMS_SET_F_LOCAL)
		meta->meta_gn = 1;
	else
		/* This tells ldms_update that we've never received
		 * the remote meta data */
		meta->meta_gn = 0;
	meta->card = 0;
	meta->head_off = sizeof(*meta);
	meta->tail_off = sizeof(*meta);
	meta->flags = LDMS_SETH_F_LCLBYTEORDER;
	strncpy(meta->name, set_name, LDMS_SET_NAME_MAX-1);
	meta->name[LDMS_SET_NAME_MAX-1] = '\0';

	data->gn = data->meta_gn = meta->meta_gn;
	data->head_off = sizeof(*data);
	data->tail_off = sizeof(*data);

	/* Initialize the first value descriptor so that tail->next_offset
	 * is the head. This normalizes the allocation logic in
	 * ldms_add_metric. */
	vd = ldms_ptr_(struct ldms_value_desc, meta, meta->head_off);
	vd->next_offset = meta->head_off;
	vd->type = LDMS_V_NONE;
	vd->name_len = sizeof("<empty>");
	strcpy(vd->name, "<empty>");

	rc = __record_set(set_name, s, meta, data, flags);
	if (rc)
		goto out_1;
	__ldms_dir_add_set(set_name);
	return 0;

 out_1:
	mm_free(meta);
 out_0:
	return rc;
}
#endif

#define LDMS_GRAIN_MMALLOC 1024

int ldms_init(size_t max_size)
{
	size_t grain = LDMS_GRAIN_MMALLOC;
	if (mm_init(max_size, grain))
		return -1;
	return 0;
}

int ldms_create_set(const char *set_name,
		    size_t meta_sz, size_t data_sz, ldms_set_t *s)
{
	return __ldms_create_set(set_name, meta_sz, data_sz,
				 s, LDMS_SET_F_LOCAL);
}

/** \brief Return the name of the set
 */
const char *ldms_get_set_name(ldms_set_t _set)
{
	struct ldms_set_desc *sd = (struct ldms_set_desc *)_set;
	struct ldms_set_hdr *sh = sd->set->meta;
	return sh->name;
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
	int rc = __record_set(sh->name, s, sh, dh, flags);
	return rc;
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
	[LDMS_V_F] = sizeof(float),
	[LDMS_V_D] = sizeof(double),
};
static char *type_names[] = {
	[LDMS_V_NONE] = "NONE",
	[LDMS_V_U8] = "U8",
	[LDMS_V_S8] = "S8",
	[LDMS_V_U16] = "U16",
	[LDMS_V_S16] = "S16",
	[LDMS_V_U32] = "U32",
	[LDMS_V_S32] = "S32",
	[LDMS_V_U64] = "U64",
	[LDMS_V_S64] = "S64",
	[LDMS_V_F] = "F",
	[LDMS_V_D] = "D",
};

#define ROUNDUP(_v,_s) ((_v + (_s - 1)) & ~(_s - 1))

uint32_t ldms_get_metric_meta_size(const char *name, enum ldms_value_type t)
{
	size_t s = sizeof(struct ldms_value_desc) + strlen(name) + 1;
	/* Value is aligned on value-size boundary */
	s += ROUNDUP(s, value_size[t]);
	/* Add size of value */
	s += value_size[t];
	/* Next value is aligned on 8B boundary */
	s = ROUNDUP(s, 8);
	return s;
}

int ldms_get_metric_size(const char *name, enum ldms_value_type t,
			 size_t *meta_sz, size_t *data_sz)
{
	size_t s = sizeof(struct ldms_value_desc) + strlen(name) + 1;
	/* Desc is aligned on value-size boundary */
	s += ROUNDUP(s, 8);
	*meta_sz = s;

	/* Add size of value */
	s = value_size[t];
	/* Next value is aligned on 8B boundary */
	s = ROUNDUP(s, 8);
	*data_sz = s;

	return 0;
}

static uint32_t __get_value(struct ldms_set *s, uint32_t off,
			    struct ldms_value_desc **pvd, union ldms_value **pv)
{
	*pvd = ldms_ptr_(struct ldms_value_desc, s->meta, off);
	*pv = ldms_ptr_(union ldms_value, s->data, (*pvd)->data_offset);

	return (*pvd)->next_offset;
}

void ldms_print_set_metrics(ldms_set_t _set)
{
	struct ldms_set_desc *sd = _set;
	struct ldms_value_desc *vd;
	union ldms_value *v;
	uint32_t off;

	printf("--------------------------------\n");
	printf("set: '%s'\n", sd->set->meta->name);
	printf("metadata size : %" PRIu32 "\n", sd->set->meta->meta_size);
	printf("    data size : %" PRIu32 "\n", sd->set->meta->data_size);
	printf("  metadata gn : %" PRIu64 "\n", sd->set->meta->meta_gn);
	printf("      data gn : %" PRIu64 "\n", sd->set->data->gn);
	printf("         card : %" PRIu32 "\n", sd->set->meta->card);
	printf("--------------------------------\n");
	for (off = sd->set->meta->head_off; off < sd->set->meta->tail_off; ) {
		off = __get_value(sd->set, off, &vd, &v);
		printf("  %32s[%4s] = ",
		       vd->name, type_names[vd->type]);
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
		case LDMS_V_F:
			printf("%.2f", v->v_f);
			break;
		case LDMS_V_D:
			printf("%.2f", v->v_d);
			break;
		}
	}
}

void ldms_visit_metrics(ldms_set_t _set, ldms_visit_cb_t cb, void *arg)
{
	struct ldms_set_desc *sd = _set;
	struct ldms_value_desc *vd;
	union ldms_value *v;
	uint32_t off;

	for (off = sd->set->meta->head_off; off < sd->set->meta->tail_off; ) {
		off = __get_value(sd->set, off, &vd, &v);
		cb(vd, v, arg);
	}
}

struct ldms_value_desc *ldms_first(struct ldms_iterator *i, ldms_set_t _set)
{
	struct ldms_set_desc *sd = _set;

	if (sd->set->meta->head_off >= sd->set->meta->tail_off)
		return NULL;

	i->set = sd->set;
	i->curr_off = sd->set->meta->head_off;
	return ldms_next(i);
}

struct ldms_value_desc *ldms_next(struct ldms_iterator *i)
{
	if (i->curr_off >= i->set->meta->tail_off)
		return NULL;

	i->curr_off = __get_value(i->set, i->curr_off,
				  &i->curr_desc, &i->curr_value);
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

ldms_metric_t ldms_add_metric(ldms_set_t _set, const char *name,
			      enum ldms_value_type type)
{
	struct ldms_metric *m;
	struct ldms_set_desc *sd = _set;
	struct ldms_set_hdr *sh = sd->set->meta;
	struct ldms_data_hdr *dh = sd->set->data;
	struct ldms_value_desc *vd;
	union ldms_value *v;
	uint64_t meta_gn;
	uint32_t value_offset;
	uint8_t name_len;
	int mysize;	/* the size of the desc + pad + value */

	/* Validate type */
	if (type <= LDMS_V_NONE || type > LDMS_V_D)
		return NULL;

	/* Compute my space */
	name_len = strlen(name)+1;
	mysize = sizeof(struct ldms_value_desc) + name_len;
	mysize = ROUNDUP(mysize, 8);

	if (sh->tail_off + mysize > sh->meta_size) {
		errno = ENOMEM;
		return NULL;
	}
	if (dh->tail_off + value_size[type] > sh->data_size) {
		errno = ENOMEM;
		return NULL;
	}

	/* Allocate a metric */
	m = malloc(sizeof *m);
	if (!m)
		return NULL;

	/* After this, the metric set can be modified. To ensure that
	 * the remote peer detects a meta data update, we need to be
	 * careful about ordering. */

	/* My descriptor starts at meta tail */
	vd = ldms_ptr_(struct ldms_value_desc, sh, sh->tail_off);

	/* Build the descriptor */
	vd->type = type;
	vd->name_len = name_len;
	strcpy(vd->name, name);

	/* Compute offset of next descriptor and update the meta data */
	vd->next_offset = sh->tail_off + mysize;

	/* My value starts at data tail. If the tail isn't aligned for
	 * my data type fix it up */
	value_offset = ROUNDUP(dh->tail_off, value_size[type]);
	vd->data_offset = value_offset;

	v = ldms_ptr_(union ldms_value, dh, value_offset);
	switch (type) {
	case LDMS_V_U8:
	case LDMS_V_S8:
		v->v_u8 = 0;
		break;
	case LDMS_V_U16:
	case LDMS_V_S16:
		v->v_u16 = 0;
		break;
	case LDMS_V_U32:
	case LDMS_V_S32:
		v->v_u32 = 0;
		break;
	case LDMS_V_U64:
	case LDMS_V_S64:
		v->v_u64 = 0;
		break;
	case LDMS_V_F:
		v->v_f = 0.0;
		break;
	case LDMS_V_D:
		v->v_d = 0.0;
		break;
	case LDMS_V_LD:
		v->v_ld = 0.0;
		break;
	case LDMS_V_NONE:
		break;
	}

	m->set = sd->set;
	m->value = v;
	m->desc = vd;

	/*
	 * We implement an opportunistic locking scheme as follows:
	 * - Set the meta_gn to zero. a client who fetches the meta
	 *   data will recognize that it needs an update and fetch
	 *   again.
	 * - A client who fetches the metric data will compare the
	 *   meta_ga in the data to the cached meta_gn and recognize
	 *   it needs an update.
	 * - When the metadata and data copies of the metadata gn
	 *   match, we're consistent.
	 * - The meta_gn can never legitimately be 'zero', or the
	 *   client will retry forever. Zero means 'inconsistent'.
	 */
	meta_gn = next_gn(sh->meta_gn);
	sh->meta_gn = 0;	/* clients will refetch on this. */

	dh->tail_off = value_offset + value_size[type];
	sh->card++;
	sh->tail_off = vd->next_offset;

	dh->meta_gn = meta_gn;	/* clients will refetch when dh != sh meta_gn */
	sh->meta_gn = meta_gn;	/* we're consistent. */

	return m;
}

static struct _ldms_type_name_map {
	const char *name;
	enum ldms_value_type type;
} type_name_map[] = {
	{ "D", LDMS_V_D, },
	{ "F", LDMS_V_F, },
	{ "NONE", LDMS_V_NONE, },
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
	if (t > LDMS_V_D)
		t = LDMS_V_NONE;
	return type_names[t];
}

int ldms_begin_transaction(ldms_set_t s)
{
	struct ldms_set_desc *sd = s;
	struct ldms_data_hdr *dh = sd->set->data;
	dh->trans.flags = LDMS_TRANSACTION_BEGIN;
	return 0;
}

int ldms_end_transaction(ldms_set_t s)
{
	struct ldms_set_desc *sd = s;
	struct ldms_data_hdr *dh = sd->set->data;
	struct timeval tv;
	(void)gettimeofday(&tv, NULL);
	dh->trans.ts.sec = tv.tv_sec;
	dh->trans.ts.usec = tv.tv_usec;
	dh->trans.flags = LDMS_TRANSACTION_END;
	return 0;
}

struct ldms_timestamp const *ldms_get_timestamp(ldms_set_t s)
{
	struct ldms_set_desc *sd = s;
	struct ldms_data_hdr *dh = sd->set->data;
	return &dh->trans.ts;
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

