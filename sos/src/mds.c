/*
 * Copyright (c) 2013 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013 Sandia Corporation. All rights reserved.
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

/*
 * Author: Tom Tucker tom at ogc dot us
 */
 /*
 * Narate: This is unused
 */

#include <sys/queue.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <limits.h>
#include <pthread.h>
#include <errno.h>
#include "mds.h"
#include "mds_priv.h"

struct mds_walk_s {
	mds_t mds;
	void *user_context;
	mds_foreach_fn user_fn;
};

static int walk_fn(oidx_t oidx, oidx_key_t key, size_t keylen,
		   uint64_t obj, void *context)
{
	struct mds_walk_s *w = context;
	mds_index_t c;
	struct mds_key_s _key;
	mds_iter_t i;
	int rc;
	c = ods_obj_offset_to_ptr(w->mds->ods, obj);
	if (!c)
		/* Stop the walk */
		return 1;

	/* No need to lookup, we know where we're starting */
	memset(&_key, 0, sizeof(_key));
	_key.obj = c->first;
	i = mds_iter(w->mds, &_key);

	/* Call the user's callback */
	rc = w->user_fn(w->mds, i, w->user_context);
	free(i);

	return rc;
}

void mds_foreach(mds_t mds, mds_next_e e, mds_foreach_fn fn, void *context)
{
	struct mds_walk_s walk;
	walk.mds = mds;
	walk.user_context = context;
	walk.user_fn = fn;
	switch (e) {
	case MDS_NEXT_COMPONENT:
		oidx_walk(mds->comp_idx, walk_fn, &walk);
		break;
	default:
		oidx_walk(mds->time_idx, walk_fn, &walk);
	}
}

/* Create a new MDS iterator */
mds_iter_t mds_iter(mds_t mds, mds_key_t key)
{
	mds_iter_t i = calloc(1, sizeof *i);
	if (!i)
		goto out;

	i->mds = mds;
	i->key.comp_id = ntohl(key->comp_id);
	i->key.tv_sec = ntohl(key->tv_sec);
	i->key.obj = key->obj;

	/* key.obj takes precident */
	if (i->key.obj) {
		i->start = i->key.obj;
		i->next = i->key.obj;
		i->oidx = NULL;
	} else if (i->key.tv_sec) {
		i->oidx = mds->time_idx;
		uint64_t to = oidx_find(i->oidx,
					&i->key.tv_sec,
					sizeof(i->key.tv_sec));
		mds_index_t ts = ods_obj_offset_to_ptr(mds->ods, to);
		if (ts) {
			i->start = ts->first;
			i->next = i->start;
		} else
			i->start = i->next = 0;
	} else if (i->key.comp_id) {
		i->oidx = mds->comp_idx;
		uint64_t comp = oidx_find(i->oidx,
					  &i->key.comp_id,
					  sizeof(i->key.comp_id));
		mds_index_t c = ods_obj_offset_to_ptr(mds->ods, comp);
		if (c) {
			i->start = c->first;
			i->next = i->start;
		} else
			i->start = i->next = 0;
	}

 out:
	return i;
}

mds_tuple_t mds_tuple(mds_t mds, uint64_t obj, mds_tuple_t t)
{
	mds_record_t r = ods_obj_offset_to_ptr(mds->ods, obj);
	if (!t)
		t = malloc(sizeof *t);
	t->tv_sec = r->tv_sec;
	t->tv_usec = r->tv_usec;
	t->comp_id = r->comp_id;
	t->value = r->value;
	return t;
}	

/* Retrieve the next tuple from the iterator */
mds_tuple_t mds_next(mds_iter_t i, mds_next_e e, mds_tuple_t t)
{
	mds_record_t r = ods_obj_offset_to_ptr(i->mds->ods, i->next);
	if (!r)
		return NULL;
	if (!t) {
		t = calloc(1, sizeof *t);
		if (!t)
			return NULL;
	}
	t->tv_sec = r->tv_sec;
	t->tv_usec = r->tv_usec;
	t->comp_id = r->comp_id;
	t->value = r->value;
	switch  (e) {
	case MDS_NEXT_TIMESTAMP:
		i->next = r->next_ts;
		break;
	case MDS_NEXT_COMPONENT:
		i->next = r->next_comp;
		break;
	default:
		exit(5);
	}
	return t;
}

#define COMPONENT_SUFFIX	"CMP"
#define TIME_SUFFIX		"TS"
#define VALUE_SUFFIX		"VAL"

static char tmp_path[PATH_MAX];

oidx_t init_idx(mds_t mds, int o_flag, int o_mode, const char *suffix)
{
	sprintf(tmp_path, "%s_%s", mds->path, suffix);
	return oidx_open(tmp_path, o_flag, o_mode);
}

void mds_flush(mds_t mds)
{
	ods_flush(mds->ods);
	oidx_flush(mds->comp_idx);
	oidx_flush(mds->time_idx);
}

void mds_close(mds_t mds)
{
	mds_flush(mds);
	ods_close(mds->ods);
	oidx_close(mds->comp_idx);
	oidx_close(mds->time_idx);
}

/**
 * Create a new tuple store
 */
mds_t mds_open(const char *path, int o_flag, ...)
{
	va_list argp;
	int o_mode;

	struct mds_s *mds = calloc(1, sizeof(*mds));
	if (!mds)
		goto out;

	mds->path = strdup(path);

	if (o_flag & O_CREAT) {
		va_start(argp, o_flag);
		o_mode = va_arg(argp, int);
	} else
		o_mode = 0;

	sprintf(tmp_path, "%s_mds", mds->path);
	mds->ods = ods_open(tmp_path, o_flag, o_mode);
	if (!mds->ods)
		goto err;
	mds->udata = ods_get_user_data(mds->ods, &mds->udata_sz);
	if (memcmp(mds->udata->signature, MDS_SIGNATURE, 8)) {
		memcpy(mds->udata->signature, MDS_SIGNATURE, 8);
		mds->udata->ods_extend_sz = MDS_ODS_EXTEND_SZ;
	}
	mds->ods_extend_sz = mds->udata->ods_extend_sz;
	mds->comp_idx = init_idx(mds, o_flag, o_mode, COMPONENT_SUFFIX);
	if (!mds->comp_idx)
		goto err;
	mds->time_idx = init_idx(mds, o_flag, o_mode, TIME_SUFFIX);
	if (!mds->time_idx)
		goto err;
 out:
	return mds;

 err:
	if (mds->comp_idx)
		oidx_close(mds->comp_idx);

	if (mds->time_idx)
		oidx_close(mds->time_idx);

	if (mds->value_idx)
		oidx_close(mds->value_idx);

	free((void *)mds->path);
	free(mds);

	return NULL;
}

int mds_extend(mds_t m, size_t sz)
{
	int rc = ods_extend(m->ods, m->ods_extend_sz);
	if (rc) {
		perror("ods_extend");
		return rc;
	}
	m->udata = ods_get_user_data(m->ods, &m->udata_sz);
	return 0;
}

uint64_t get_idx(mds_t m, mds_next_e e, oidx_key_t key, size_t keylen)
{
	oidx_t idx;
	uint64_t xo;
	switch (e) {
	case MDS_NEXT_COMPONENT:
		idx = m->comp_idx;
		break;
	case MDS_NEXT_TIMESTAMP:
		idx = m->time_idx;
		break;
	default:
		return 0;
	}
	xo = oidx_find(idx, key, keylen);
	if (!xo) {
		/* Add the index */
		mds_index_t x = ods_alloc(m->ods, sizeof *x);
		if (!x) {
			if (mds_extend(m, m->ods_extend_sz))
				goto err;
			x = ods_alloc(m->ods, sizeof *x);
			if (!x)
				goto err;
		}
		x->first = x->last = 0;
		xo = ods_obj_ptr_to_offset(m->ods, x);
		oidx_add(idx, key, keylen, xo);
	}
	return xo;
 err:
	errno = ENOMEM;
	return 0;
}

/** Write a tuple to a store */
int mds_add(mds_t m, mds_tuple_t t)
{
	mds_index_t c;
	mds_index_t ts;
	uint32_t comp_id_key;
	uint32_t ts_key;

	comp_id_key = ntohl(t->comp_id);
	ts_key = ntohl(t->tv_sec);

	/* Note that any pointers into ods are invalid after extend */
	uint64_t co = get_idx(m, MDS_NEXT_COMPONENT, &comp_id_key, 4);
	if (!co)
		goto err;

	uint64_t tso = get_idx(m, MDS_NEXT_TIMESTAMP, &ts_key, 4);
	if (!ts)
		goto err;

	/* Allocate the new record */
	mds_record_t nr = ods_alloc(m->ods, sizeof *nr);
	if (!nr) {
		if (mds_extend(m, m->ods_extend_sz))
			goto err;
		nr = ods_alloc(m->ods, sizeof *nr);
		if (!nr) {
			errno = ENOMEM;
			goto err;
		}
	}

	/* wait to resolve objects until after we've extended as necessary */
	c = ods_obj_offset_to_ptr(m->ods, co);
	ts = ods_obj_offset_to_ptr(m->ods, tso);

	/* Chain the new record to the component list */
	mds_record_t pr = ods_obj_offset_to_ptr(m->ods, c->last);
	if (pr)
		pr->next_comp = ods_obj_ptr_to_offset(m->ods, nr);
	nr->prev_comp = c->last;
	c->last = ods_obj_ptr_to_offset(m->ods, nr);
	if (!c->first)
		c->first = c->last;

	/* Chain the new record to the timestamp list */
	pr = ods_obj_offset_to_ptr(m->ods, ts->last);
	if (pr)
		pr->next_ts = ods_obj_ptr_to_offset(m->ods, nr);
	nr->prev_ts = ts->last;
	ts->last = ods_obj_ptr_to_offset(m->ods, nr);
	if (!ts->first)
		ts->first = ts->last;

	nr->next_comp = nr->next_ts = 0;
	nr->tv_sec = t->tv_sec;
	nr->tv_usec = t->tv_usec;
	nr->value = t->value;
	nr->comp_id = t->comp_id;
	return 0;
 err:
	return (ssize_t)-1;
}

#ifdef MDS_MAIN
#include "idx.h"

idx_t ct_idx;
idx_t c_idx;
struct metric_store_s {
	mds_t mds;
};

int main(int argc, char *argv[])
{
	char *s;
	static char pfx[32];
	static char buf[128];
	static char c_key[32];
	static char comp_type[32];
	static char metric_name[32];
	struct metric_store_s *m;

	if (argc < 2) {
		printf("usage: ./mds <dir>\n");
		exit(1);
	}
	strcpy(pfx, argv[1]);
	ct_idx = idx_create();
	c_idx = idx_create();

	while ((s = fgets(buf, sizeof(buf), stdin)) != NULL) {
		struct mds_tuple_s tuple;
		sscanf(buf, "%[^,],%[^,],%d,%ld,%d,%d",
		       comp_type, metric_name,
		       &tuple.comp_id,
		       &tuple.value,
		       &tuple.ts_sec,
		       &tuple.ts_msec);

		/* Add a component type directory if one does not
		 * already exist
		 */
		if (!idx_find(ct_idx, &comp_type, 2)) {
			sprintf(tmp_path, "%s/%s", pfx, comp_type);
			mkdir(tmp_path, 0777);
			idx_add(ct_idx, &comp_type, 2, (void *)1UL);
		}
		sprintf(c_key, "%s:%s", comp_type, metric_name);
		m = idx_find(c_idx, c_key, strlen(c_key));
		if (!m) {
			/*
			 * Open a new MDS for this component-type and
			 * metric combination
			 */
			m = malloc(sizeof *m);
			sprintf(tmp_path, "%s/%s/%s", pfx, comp_type, metric_name);
			m->mds = mds_open(tmp_path, O_CREAT | O_RDWR, 0660);
			if (m->mds) {
				idx_add(c_idx, c_key, strlen(c_key), m);
			} else {
				free(m);
				printf("Could not create MDS database '%s'\n",
				       tmp_path);
				exit(1);
			}
		}
		if (mds_add(m->mds, &tuple))
			goto err;
#if 0
		timersub(&tv1, &tv0, &tvres);
		timeradd(&tvsum, &tvres, &tvsum);
#endif
	}
	return 0;
 err:
	return 1;
}
#endif
