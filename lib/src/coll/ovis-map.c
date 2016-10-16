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
#include "ovis-map.h"
#include "third/city.h"
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <assert.h>
#include "rbt.h"

#define HASH64(key,size) CityHash64(key,size)

/** approximately "OVISMAP" on the right endianness cast to char. */
#define OVISMAP_MAGIC 0xa50414d5349564f
#define MAP_OK(m) ((m)->magic == OVISMAP_MAGIC)
#ifdef NOISYMAGIC
#undef MAP_OK
#define MAP_OK(m) ((m)->magic == OVISMAP_MAGIC || 0 == printf("bad magic at %d\n",__LINE__))
#endif

/* Compare map element to user-defined element */
typedef int (*ovis_map_comparator)(struct ovis_map_element *e, struct ovis_map_element *user);

struct ovis_map {
	uint64_t magic;
	struct rbt mets; /* sorted by hash, then strcmp if collision */
	pthread_mutex_t mets_lock; /**< write lock at tree level */
	size_t mets_size; /* count of items in mets */
};

struct map_node {
	struct rbn n;
	struct ovis_map_element ome;
};

static int element_cmp(struct ovis_map_element *e, struct ovis_map_element *f)
{
	/* don't allow bad comparisons or input into the map. duplicates rejected. */
	if (!e || !f || !e->key || !f->key) {
		return 0;
	}
	if (! e->keyhash) {
		e->keyhash = HASH64(e->key,strlen(e->key));
	}
	if (! f->keyhash) {
		f->keyhash = HASH64(f->key,strlen(f->key));
	}
	if (e->keyhash == f->keyhash) {
		return strcmp(e->key,f->key);
	}
	if (e->keyhash < f->keyhash) {
		return -1;
	}
	return 1;
}

struct ovis_map *ovis_map_create()
{
	errno = 0;
	struct ovis_map *map = calloc(1,sizeof(struct ovis_map));
	if (!map) {
		return NULL;
	}
	int err = pthread_mutex_init(&(map->mets_lock), NULL);
	if (err) {
		free(map);
		errno = err;
		return NULL;
	}
	rbt_init(&(map->mets),(rbn_comparator_t)element_cmp);
	map->mets_size = 0;
	map->magic = OVISMAP_MAGIC; /* OVISMAP, or backward, as string */
	return map;
}

struct visit_data {
	ovis_map_visitor v;
	void *u;
};

static int visitor(struct rbn *n, void *udata, int level)
{
	(void)level;
	struct map_node *mn = (struct map_node *)n;
	struct visit_data *v = (struct visit_data *)udata;
	v->v(&(mn->ome),v->u);
	return 0;
}

void ovis_map_visit(struct ovis_map *m, ovis_map_visitor v, void *userdata)
{
	if (!m || !MAP_OK(m) || !v) {
		return;
	}
	struct visit_data vd = { v, userdata };
	pthread_mutex_lock(&m->mets_lock);
	(void)rbt_traverse(&(m->mets),visitor,&vd);
	pthread_mutex_unlock(&m->mets_lock);
}


struct gather {
	struct rbn **buf;
	int64_t count;
};

static
int gathernodes(struct rbn *n, void *data, int level)
{
	(void) level;
	struct gather *g = data;
	g->buf[g->count] = n;
	g->count++;
	return 0;
}

void ovis_map_destroy(struct ovis_map *map, ovis_map_visitor destroy, void *udata)
{

	if (!MAP_OK(map)) {
		return;
	}
	pthread_mutex_lock(&map->mets_lock);
	struct gather all;
	struct rbn *nodeptrs[map->mets_size+1];
	nodeptrs[map->mets_size] = NULL;
	all.buf = nodeptrs;
	all.count = 0;
	rbt_traverse(&(map->mets), gathernodes, &all);
	size_t i = 0;
	for ( ; i < map->mets_size; i++) {
		struct rbn *node;
		node = rbt_find(&(map->mets),
				&(((struct map_node*)(nodeptrs[i]))->ome));
		if (node) {
			rbt_del(&(map->mets), node);
		} else {
			assert("YIKES! can't delete what we just found." == NULL);
		}
	}
	if (destroy) {
		i = 0;
		for ( ; i < map->mets_size; i++) {
			destroy(&(((struct map_node*)(nodeptrs[i]))->ome),udata);
		}
	}
	i = 0;
	for ( ; i < map->mets_size; i++) {
		free(nodeptrs[i]);
		nodeptrs[i] = NULL;
	}
	map->mets_size = 0;
	map->magic = 0xdeadbeef;
	pthread_mutex_unlock(&map->mets_lock);
	pthread_mutex_destroy(&map->mets_lock);
	free(map);
}

size_t ovis_map_size(struct ovis_map *m)
{
	if (m && MAP_OK(m)) {
		return m->mets_size;
	}
	return 0;
}

uint64_t ovis_map_keyhash(const char *key, size_t keylen)
{
	if (key) {
		return HASH64(key,keylen);
	}
	return 0;
}

struct ovis_map_element ovis_map_findhash(struct ovis_map *map, struct ovis_map_element e)
{
	if (map && MAP_OK(map)) {
		struct rbn *node = rbt_find(&(map->mets), &e);
		if (node) {
			return ((struct map_node*)node)->ome;
		}
	}
	return e;
}

struct ovis_map_element ovis_map_find(struct ovis_map *map, const char *s)
{
	struct ovis_map_element e;
	e.key = s;
	e.keyhash = HASH64(s, strlen(s));
	e.value = NULL;
	return ovis_map_findhash(map, e);
}

static struct rbn *create_node(struct ovis_map_element *user)
{
	if (!user) {
		return NULL;
	}
	struct map_node *node = malloc(sizeof(struct map_node));
	if (!node) {
		return NULL;
	}
	void *key = &(node->ome);
	rbn_init(&(node->n),key);
	node->ome = *user;
	return &(node->n);

}

int ovis_map_insert_fast(struct ovis_map *m, struct ovis_map_element el)
{
	if (!m || !MAP_OK(m) || !el.key || !el.value || !el.keyhash) {
		return EINVAL;
	}
	int rc = 0;
	pthread_mutex_lock(&(m->mets_lock));
	struct rbn *node = create_node(&el);
	if (node) {
		rbt_ins(&(m->mets),node);
		m->mets_size++;
	} else {
		rc = ENOMEM;
	}
	pthread_mutex_unlock(&(m->mets_lock));
	return rc;
}

int ovis_map_insert(struct ovis_map *map, const char *key, void *value)
{
	if (!map || !MAP_OK(map) || !key || !value) {
		return EINVAL;
	}

	pthread_mutex_lock(&(map->mets_lock));
	struct ovis_map_element ome = { key, HASH64(key,strlen(key)), value};
	struct rbn *node;
	node = rbt_find(&(map->mets),&ome);
	int rc = 0;
	if (node) {
		rc = ENOTUNIQ;
		goto out;
	}
	node = create_node(&ome);
	if (node) {
		rbt_ins(&(map->mets),node);
		map->mets_size++;
	} else {
		rc = ENOMEM;
	}
out:
	pthread_mutex_unlock(&(map->mets_lock));
	return rc;
}

int ovis_map_insert_new(struct ovis_map *map, const char *key, void *value)
{
	if (!map || !MAP_OK(map) || !key || !value) {
		return EINVAL;
	}

	int rc = 0;
	pthread_mutex_lock(&(map->mets_lock));
	struct ovis_map_element ome = { key, HASH64(key,strlen(key)), value};
	struct rbn *node = create_node(&ome);
	if (node) {
		rbt_ins(&(map->mets),node);
		map->mets_size++;
	} else {
		rc = ENOMEM;
	}
	pthread_mutex_unlock(&(map->mets_lock));
	return rc;
}

int64_t ovis_map_snapshot(struct ovis_map *map, struct ovis_map_element **snap, size_t snap_len)
{
	int64_t rc = 0;
	if (!map || !MAP_OK(map)) {
		return -1;
	}
	if (!snap) {
		return map->mets_size + 1;
	}

	pthread_mutex_lock(&map->mets_lock);
	struct rbn *nodeptrs[map->mets_size + 1];
	nodeptrs[map->mets_size] = NULL;
	if ( map->mets_size > snap_len - 1) {
		rc = map->mets_size + 1;
		goto out;
	}

	struct gather all;
	all.buf = nodeptrs;
	all.count = 0;
	rbt_traverse(&(map->mets), gathernodes, &all);
	snap[all.count] = NULL;
	for ( ; all.count > 0; all.count--) {
		snap[all.count - 1] =
			&(((struct map_node*)(nodeptrs[all.count - 1]))->ome);
	}

out:
	pthread_mutex_unlock(&map->mets_lock);
	return rc;
}

