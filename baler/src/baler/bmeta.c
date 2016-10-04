/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2015 Sandia Corporation. All rights reserved.
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
/**
 * \file bmeta.c
 * \author Narate Taerat (narate at ogc dot us)
 *
 * \brief Baler meta clustering library.
 */
#include <errno.h>
#include <assert.h>

#include "bmeta.h"
#include "bmeta_priv.h"
#include "bmlist.h"
#include "bcommon.h"
#include "butils.h"
#include "bhash.h"

#define BMPTN_MEM_PATH "/path"
#define BMPTN_BMC_HANDLE_VEC_PATH "/bmc_handle_vec"
#define BMPTN_DIST_PATH "/dist_map"
#define BMPTN_NODES_PATH "/nodes"

struct bmptn_store_header *bmptn_store_gethdr(struct bmptn_store *store)
{
	return store->mem->ptr;
}

static
void __bmptn_store_set_state(struct bmptn_store *store, bmptn_store_state_e state)
{
	pthread_mutex_lock(&store->mutex);
	bmptn_store_gethdr(store)->state = state;
	pthread_mutex_unlock(&store->mutex);
}

/**
 * Close the internal stores.
 */
static
void __bmptn_store_close(struct bmptn_store *store)
{
	if (store->mem) {
		bmem_close_free(store->mem);
		store->mem = NULL;
	}
	if (store->bmc_handle_vec) {
		bmvec_generic_close_free(store->bmc_handle_vec);
		store->bmc_handle_vec = NULL;
	}
	if (store->nodes) {
		bmvec_generic_close_free(store->nodes);
		store->nodes = NULL;
	}
	if (store->dist) {
		bmhash_close_free(store->dist);
		store->dist = NULL;
	}
	if (store->engsig_array) {
		free(store->engsig_array);
		store->engsig_array = NULL;
	}
	if (store->engsig_hash) {
		bhash_free(store->engsig_hash);
		store->engsig_hash = NULL;
	}
}

void bmptn_store_close_free(struct bmptn_store *store)
{
	__bmptn_store_close(store);
	if (store->tkn_store)
		btkn_store_close_free(store->tkn_store);
	if (store->ptn_store)
		bptn_store_close_free(store->ptn_store);
	free(store);
}

int bmptn_store_initialized(struct bmptn_store *store)
{
	struct bmptn_store_header *hdr = BMPTR(store->mem, sizeof(*store->mem->hdr));
	if (store->mem->hdr->ulen == sizeof(store->mem->hdr)) {
		return 0;
	}
	return 0 == strncmp(hdr->version, BMPTN_STORE_VER, 64);
}

/**
 * Open the internal store.
 *
 * This is the common routine for initilization and open operations.
 *
 * \param store The store.
 *
 * \retval 0 if success.
 * \retval ERRNO if error.
 */
static
int __bmptn_store_open(struct bmptn_store *store)
{
	struct stat st;
	size_t path_len;
	int rc = 0;

	pthread_mutex_init(&store->mutex, NULL);

	path_len = strlen(store->path);

	/* We will use store->path as a temporary path for components in the
	 * store. */
	snprintf(store->path + path_len, PATH_MAX - path_len, BMPTN_MEM_PATH);
	store->mem = bmem_open(store->path);
	if (!store->mem) {
		berr("Cannot open bmptn_store->mem: %s, error: %m", store->path);
		rc = errno;
		goto out;
	}

	snprintf(store->path + path_len, PATH_MAX - path_len, BMPTN_BMC_HANDLE_VEC_PATH);
	store->bmc_handle_vec = bmvec_generic_open(store->path);
	if (!store->bmc_handle_vec) {
		berr("Cannot open bmptn_store->bmc_handle_vec: %s, error: %m",
				store->path);
		rc = errno;
		goto out;
	}

	snprintf(store->path + path_len, PATH_MAX - path_len, BMPTN_NODES_PATH);
	store->nodes = bmvec_generic_open(store->path);
	if (!store->nodes) {
		berr("Cannot open bmptn_store->nodes: %s, error: %m",
				store->path);
		rc = errno;
		goto out;
	}

	snprintf(store->path + path_len, PATH_MAX - path_len, BMPTN_DIST_PATH);
	store->dist = bmhash_open(store->path, O_RDWR|O_CREAT, 65599,
							BMHASH_FN_FNV, 7);
	if (!store->dist) {
		berr("Cannot open bmptn_store->dist: %s, error: %m",
								store->path);
		rc = errno;
		goto out;
	}

out:
	/* Restore the store path */
	store->path[path_len] = 0;

	return rc;
}

int bmptn_store_purge(const char *path)
{
	char *buff = malloc(PATH_MAX);
	int rc;
	int plen;

	if (!buff) {
		rc = ENOMEM;
		goto out;
	}

	plen = snprintf(buff, PATH_MAX, "%s", path);

	snprintf(buff + plen, PATH_MAX - plen, BMPTN_MEM_PATH);
	rc = bmem_unlink(buff);
	if (rc && rc != ENOENT)
		goto out;

	snprintf(buff + plen, PATH_MAX - plen, BMPTN_BMC_HANDLE_VEC_PATH);
	rc = bmvec_unlink(buff);
	if (rc && rc != ENOENT)
		goto out;

	snprintf(buff + plen, PATH_MAX - plen, BMPTN_NODES_PATH);
	rc = bmvec_unlink(buff);
	if (rc && rc != ENOENT)
		goto out;

	snprintf(buff + plen, PATH_MAX - plen, BMPTN_DIST_PATH);
	rc = bmap_unlink(buff);
	if (rc && rc != ENOENT)
		goto out;

out:
	free(buff);
	return rc;
}

/**
 * Initialize internal store components, assuming that \c store is newly
 * created or purged.
 */
static
int __bmptn_store_init(struct bmptn_store *store)
{
	int rc = 0;
	uint64_t off;
	struct bmptn_store_header *hdr;
	off = bmem_alloc(store->mem, sizeof(struct bmptn_store_header));
	if (!off) {
		rc = ENOMEM;
		goto out;
	}
	hdr = BMPTR(store->mem, off);
	strncpy(hdr->version, BMPTN_STORE_VER, 64);
	hdr->refinement_speed = 0;
	hdr->looseness = 0;
	hdr->diff_ratio = 0;
	hdr->last_ptn_id = BMAP_ID_NONE;

	hdr->state = BMPTN_STORE_STATE_INITIALIZED;
	hdr->working_cls_id = 0;
out:
	return rc;
}

int bmptn_store_reinit(struct bmptn_store *store)
{
	int rc;
	int plen = strlen(store->path);
	struct bmptn_store_header *hdr;

	pthread_mutex_lock(&store->mutex);
	hdr = bmptn_store_gethdr(store);
	switch (hdr->state) {
	case BMPTN_STORE_STATE_ERROR:
	case BMPTN_STORE_STATE_DONE:
	case BMPTN_STORE_STATE_NA:
	case BMPTN_STORE_STATE_INITIALIZED:
		/* These are allowed states to reinit. */
		break;
	default:
		rc = EINVAL;
		goto out;
	}

	__bmptn_store_close(store);

	rc = bmptn_store_purge(store->path);
	if (rc)
		goto out;

	rc = __bmptn_store_open(store);
	if (rc)
		goto out;

	rc = __bmptn_store_init(store);

out:
	pthread_mutex_unlock(&store->mutex);
	return rc;
}

struct bmptn_store *bmptn_store_open(const char *path, const char *bstore_path,
					int create)
{
	struct bmptn_store *store;
	struct stat st;
	int path_len = 0;
	int plen, psize;
	int rc;

	if (!bis_dir(path)) {
		if (!create) {
			berr("Cannot open bmptn_store: %s, error: %m", path);
			return NULL;
		}
		rc = bmkdir_p(path, 0755);
		if (rc) {
			berr("Cannot create directory: %s, error: %m", path);
			return NULL;
		}
	}

	store = calloc(1, sizeof(*store));
	if (!store)
		goto err0;

	store->engsig_hash = NULL;
	store->engsig_array = NULL;

	path_len = snprintf(store->path, sizeof(store->path), "%s", bstore_path);
	if (path_len >= sizeof(store->path)) {
		errno = EINVAL;
		goto err1;
	}

	psize = sizeof(store->path) - path_len;

	plen = snprintf(store->path + path_len, psize, "/ptn_store");
	if (plen >= psize) {
		errno = EINVAL;
		goto err1;
	}
	store->ptn_store = bptn_store_open(store->path, O_RDONLY);
	if (!store->ptn_store)
		goto err1;

	plen = snprintf(store->path + path_len, psize, "/tkn_store");
	if (plen >= psize) {
		errno = EINVAL;
		goto err1;
	}
	store->tkn_store = btkn_store_open(store->path, O_RDONLY);
	if (!store->tkn_store)
		goto err1;

	/* restore store path */
	path_len = snprintf(store->path, sizeof(store->path), "%s", path);
	if (path_len >= sizeof(store->path)) {
		errno = EINVAL;
		goto err1;
	}

	rc = __bmptn_store_open(store);
	if (rc)
		goto err1;

	if (!bmptn_store_initialized(store)) {
		rc = __bmptn_store_init(store);
		if (rc)
			goto err1;
	}

	return store;

err1:
	bmptn_store_close_free(store);
err0:
	return NULL;
}

int bmptn_get_eng_signature(struct bmptn_store *store, const struct bstr *s,
				struct bstr *out)
{
	int i;
	int len = s->blen / sizeof(s->u32str[0]);
	int olen = 0;
	struct btkn_attr tkn_attr;
	for (i = 0; i < len; i++) {
		tkn_attr = btkn_store_get_attr(store->tkn_store, s->u32str[i]);
		if (tkn_attr.type != BTKN_TYPE_ENG)
			continue;
		out->u32str[olen++] = s->u32str[i];
	}
	out->blen = olen * sizeof(out->u32str[0]);
	return 0;
}

struct bmptn_id_list {
	uint32_t id;
	LIST_ENTRY(bmptn_id_list) link;
};

LIST_HEAD(bmptn_id_head, bmptn_id_list);

/**
 * Generate English Signature graph, and label ptn nodes accordingly.
 */
static
int __bmptn_cluster_1(struct bmptn_store *store)
{
	int rc = 0;
	uint32_t i;
	uint32_t n = bmptn_store_gethdr(store)->last_ptn_id;
	void *buff = NULL;
	struct bstr *bstr = NULL;
	struct bhash_entry **eng_sig_array = NULL;
	struct bhash *bhash = NULL;
	struct bhash_entry *ent = NULL;
	struct bmptn_node *node = NULL;
	uint32_t label = 0;
	struct bmptn_node NODE = {0};
	uint32_t metric_lead = 0;

	metric_lead = btkn_store_get_id(store->tkn_store, BMETRIC_LEAD_TKN_BSTR);

	store->engsig_hash = NULL;
	store->engsig_array = NULL;

	/* Initial the nodes array */
	rc = bmvec_generic_set(store->nodes, n+1, &NODE, sizeof(NODE));
	if (rc)
		goto cleanup;

	bhash = bhash_new(65539, 0, NULL);
	if (!bhash) {
		rc = ENOMEM;
		goto cleanup;
	}

	buff = malloc(65536);

	if (!buff) {
		rc = ENOMEM;
		goto cleanup;
	}

	eng_sig_array = calloc(n, sizeof(eng_sig_array[0]));
	if (!eng_sig_array) {
		rc = ENOMEM;
		goto cleanup;
	}

	for (i = BMAP_ID_BEGIN; i <= n; i++) {
		const struct bstr *ptn = bptn_store_get_ptn(store->ptn_store, i);
		if (ptn->u32str[0] == metric_lead)
			continue;
		rc = bmptn_get_eng_signature(store, ptn, buff);
		if (rc)
			goto cleanup;
		bstr = buff;
		ent = bhash_entry_get(bhash, (void*)bstr, bstr_len(bstr));
		if (!ent) {
			label++;
			ent = bhash_entry_set(bhash, (void*)bstr, bstr_len(bstr), label);
			if (!ent) {
				rc = ENOMEM;
				goto cleanup;
			}
			eng_sig_array[label] = ent;
		}
		node = bmvec_generic_get(store->nodes, i, sizeof(*node));
		node->ptn_id = i;
		node->label = ent->value;
		node->link.next = 0;
		bmptn_store_gethdr(store)->percent = ((float)i / n)*100;
	}

	goto out;
cleanup:
	if (rc) {
		__bmptn_store_set_state(store, BMPTN_STORE_STATE_ERROR);
	}
	if (eng_sig_array) {
		free(eng_sig_array);
	}

	if (bhash) {
		bhash_free(bhash);
	}
out:
	if (!rc) {
		store->engsig_array = eng_sig_array;
		store->engsig_hash = bhash;
	}
	if (buff)
		free(buff);
	return rc;
}

#define DIST_BUFF_SIZE (16*1024*1024)

/**
 * Perform simple clustering on English Signature graph, and relabel
 * pattern graph accordingly.
 */
static
int __bmptn_cluster_2(struct bmptn_store *store)
{
	int rc = 0;
	int i, c, j, l, idx;
	struct bhash_entry **cls1_entries = store->engsig_array;
	size_t n = store->engsig_hash->count;
	int N = bmptn_store_gethdr(store)->last_ptn_id;
	float *dist = NULL;
	void *buff = NULL;
	int *stack = NULL;
	int tos = -1;
	struct bmc_handle BMC = {0};
	float thr = (bmptn_store_gethdr(store))->diff_ratio;
	int labelled = 0;

	buff = malloc(DIST_BUFF_SIZE);
	if (!buff) {
		rc = ENOMEM;
		goto cleanup;
	}

	dist = calloc((n+1)*(n+1), sizeof(dist[0]));
	if (!dist) {
		rc = ENOMEM;
		goto cleanup;
	}

	stack = malloc(n * sizeof(stack[0]));
	if (!stack) {
		rc = ENOMEM;
		goto cleanup;
	}

	/* nodes in the graph are eng sig */
	/* label starts from 1 */
	/* cls1_entires[old_label] */

	/* Clear existing labels */
	for (i = 1; i <= n; i++) {
		cls1_entries[i]->value = 0;
	}

	/* Re-label according to eng-sig distance */
	i = 0; /* cls node starts from 1 */
	l = 0; /* label starts from 1 */
loop:
	if (tos < 0) {
		i++;
		while (i <= n) {
			if (!cls1_entries[i]->value)
				break;
			i++;
		}
		if (i > n)
			goto relabel;
		cls1_entries[i]->value = ++l;
		stack[++tos] = i;
		bmptn_store_gethdr(store)->last_cls_id = l;
		bmptn_store_gethdr(store)->percent = ((float)(++labelled)/n)*100;
	}
	c = stack[tos--];
	/* dfs labeling */
	for (j = 1; j <= n; j++) {
		if (cls1_entries[j]->value)
			continue;
		idx = i*n + j;
		if (!dist[idx]) {
			struct bstr *bi, *bj;
			bi = (void*)cls1_entries[i]->key;
			bj = (void*)cls1_entries[j]->key;
			dist[idx] = bstr_lev_dist_u32(bi, bj, buff,
							DIST_BUFF_SIZE);
			if (bi->blen < bj->blen)
				dist[idx] /= bi->blen;
			else
				dist[idx] /= bj->blen;
			dist[j*n+i] = dist[idx];
		}

		if (dist[idx] < thr && !cls1_entries[j]->value) {
			cls1_entries[j]->value = l;
			bmptn_store_gethdr(store)->percent = ((float)(++labelled)/n)*100;
			stack[++tos] = j;
		}
	}

	goto loop;

relabel:
	rc = bmvec_generic_set(store->bmc_handle_vec, l+1, &BMC, sizeof(BMC));
	if (rc)
		goto cleanup;

	for (i = BMAP_ID_BEGIN; i <= N; i++) {
		struct bmptn_node *node = bmvec_generic_get(store->nodes, i,
								sizeof(*node));
		if (!cls1_entries[node->label])
			/* skip mertic stuff */
			continue;
		node->label = cls1_entries[node->label]->value;
		struct bmc_handle *bmc = bmvec_generic_get(
					store->bmc_handle_vec, node->label,
					sizeof(*bmc));
		BMLIST_INSERT_HEAD(bmc->bmlist_off, node, link,
						store->nodes->mem);
	}

cleanup:
out:
	if (rc)
		__bmptn_store_set_state(store, BMPTN_STORE_STATE_ERROR);
	if (buff)
		free(buff);
	if (dist)
		free(dist);
	if (stack)
		free(stack);
	/* Free unused Eng Sig map */
	bhash_free(store->engsig_hash);
	store->engsig_hash = NULL;
	free(store->engsig_array);
	store->engsig_array = NULL;
	return rc;
}

static char distbuff[65536];

static
float __get_dist(struct bmptn_store *store, uint32_t ptnid0, uint32_t ptnid1)
{
	char key_buff[sizeof(struct bstr) + 2*sizeof(uint32_t)];
	struct bstr *key = (void*)key_buff;
	key->blen = 2*sizeof(uint32_t);
	struct bmhash_entry *ent;
	const struct bstr *b0, *b1;

	if (ptnid0 < ptnid1) {
		key->u32str[0] = ptnid0;
		key->u32str[1] = ptnid1;
	} else {
		key->u32str[0] = ptnid1;
		key->u32str[1] = ptnid0;
	}

	ent = bmhash_entry_get(store->dist, key);
	if (ent)
		return *(float*)&ent->value;

	b0 = bptn_store_get_ptn(store->ptn_store, ptnid0);
	if (!b0)
		return -1;
	b1 = bptn_store_get_ptn(store->ptn_store, ptnid1);
	if (!b1)
		return -1;
	return bstr_lev_dist_u32(b0, b1, distbuff, sizeof(distbuff));
}

static
float __avg_dist(struct bmptn_store *store, uint32_t cls_id)
{
	float avg_dist;
	float dist;
	struct barray *array = NULL;
	int i, j, n, rc;
	const uint32_t *x;
	struct bmc_handle *bmc;
	struct bmptn_node *node, *nodej;

	rc = 0;

	array = barray_alloc(sizeof(uint32_t), 1024);
	if (!array) {
		rc = ENOMEM;
		goto out;
	}

	bmc = bmvec_generic_get(store->bmc_handle_vec, cls_id, sizeof(*bmc));
	n = 0;
	BMLIST_FOREACH(node, bmc->bmlist_off, link, store->nodes->mem) {
		node->label = 0;
		rc = barray_set(array, n, &node->ptn_id);
		if (rc) {
			goto out;
		}
		n++;
	}
	avg_dist = 0;
	if (n == 1)
		goto out;
	for (i = 0; i < n; i++) {
		x = barray_get(array, i, NULL);
		node = bmvec_generic_get(store->nodes, *x, sizeof(*node));
		for (j = i+1; j < n; j++) {
			x = barray_get(array, j, NULL);
			nodej = bmvec_generic_get(store->nodes, *x, sizeof(*node));
			dist = __get_dist(store, node->ptn_id, nodej->ptn_id);
			if (dist < 0) {
				rc = errno;
				goto out;
			}
			avg_dist += dist;
		}
	}
	avg_dist /= (n-1)*n/2;
out:
	if (rc) {
		errno = rc;
		avg_dist = -1;
	}

	if (array)
		barray_free(array);
	return avg_dist;
}

int bmptn_refine_cluster(struct bmptn_store *store, uint32_t cls_id, float thr)
{
	int rc = 0;
	struct barray *array = NULL;
	const uint32_t *x;
	uint32_t l;
	int i, j, n;
	float dist;
	struct bmc_handle *bmc, BMC;
	struct bmptn_node *node, *nodej;
	uint32_t *stack = NULL;
	int tos;
	struct bmhash_entry *ent;

	array = barray_alloc(sizeof(uint32_t), 1024);
	if (!array) {
		rc = ENOMEM;
		goto out;
	}
	bmc = bmvec_generic_get(store->bmc_handle_vec, cls_id, sizeof(*bmc));
	n = 0;
	BMLIST_FOREACH(node, bmc->bmlist_off, link, store->nodes->mem) {
		node->label = 0;
		rc = barray_set(array, n, &node->ptn_id);
		if (rc)
			goto out;
		n++;
	}

	if (n == 1) {
		/* Cluster of 1, no need to continue */
		rc = 0;
		goto out;
	}

	stack = malloc(n * sizeof(*stack));
	if (!stack)
		goto out;
	l = 0;
	tos = -1;
	i = -1;
loop:
	if (tos < 0) {
		i++;
		while (i < n) {
			x = barray_get(array, i, NULL);
			node = bmvec_generic_get(store->nodes, *x, sizeof(*node));
			if (!node->label) {
				/* obtain next label */
				if (!l) {
					l = cls_id;
				} else {
					struct bmptn_store_header *hdr =
						bmptn_store_gethdr(store);
					l = ++hdr->last_cls_id;
					rc = bmvec_generic_set(
							store->bmc_handle_vec,
							l, &BMC, sizeof(BMC));
				}

				bmc = bmvec_generic_get(store->bmc_handle_vec,
							l, sizeof(*bmc));
				bmc->dist_thr = thr;
				bmc->bmlist_off = 0;
				bmc->avg_dist = -1;
				node->label = l;
				break;
			}
			i++;
		}
		if (i >= n)
			goto out;
		stack[++tos] = i;
	}

	x = barray_get(array, stack[tos--], NULL);
	node = bmvec_generic_get(store->nodes, *x, sizeof(*node));
	for (j = 0; j < n; j++) {
		x = barray_get(array, j, NULL);
		nodej = bmvec_generic_get(store->nodes, *x, sizeof(*node));
		if (nodej->label)
			continue;
		dist = __get_dist(store, node->ptn_id, nodej->ptn_id);
		if (dist < thr && !nodej->label) {
			BMLIST_INSERT_HEAD(bmc->bmlist_off, nodej, link,
							store->nodes->mem);
			nodej->label = node->label;
			stack[++tos] = j;
		}
	}

	goto loop;
out:
	if (rc)
		__bmptn_store_set_state(store, BMPTN_STORE_STATE_ERROR);
	if (array)
		barray_free(array);
	if (stack)
		free(stack);
	return rc;
}

bmptn_store_state_e bmptn_store_get_state(struct bmptn_store *store)
{
	bmptn_store_state_e ret;
	pthread_mutex_lock(&store->mutex);
	struct bmptn_store_header *hdr = bmptn_store_gethdr(store);
	ret = hdr->state;
	pthread_mutex_unlock(&store->mutex);
	return ret;
}

const char *bmptn_store_get_state_str(struct bmptn_store *store)
{
	static char *_str[] = {
		"BMPTN_STORE_STATE_NA",
		"BMPTN_STORE_STATE_ERROR",
		"BMPTN_STORE_STATE_INITIALIZED",
		"BMPTN_STORE_STATE_META_1",
		"BMPTN_STORE_STATE_META_2",
		"BMPTN_STORE_STATE_REFINING",
		"BMPTN_STORE_STATE_NAMING",
		"BMPTN_STORE_STATE_DONE",
		"BMPTN_STORE_STATE_LAST",
	};
	bmptn_store_state_e e = bmptn_store_get_state(store);
	if (e <= BMPTN_STORE_STATE_LAST) {
		return _str[e];
	}
	return "Unknown state";
}

uint32_t bmptn_store_get_working_cls_id(struct bmptn_store *store)
{
	uint32_t ret;
	pthread_mutex_lock(&store->mutex);
	ret = (bmptn_store_gethdr(store))->working_cls_id;
	pthread_mutex_unlock(&store->mutex);
	return ret;
}

uint32_t bmptn_store_get_last_cls_id(struct bmptn_store *store)
{
	uint32_t ret;
	pthread_mutex_lock(&store->mutex);
	ret = (bmptn_store_gethdr(store))->last_cls_id;
	pthread_mutex_unlock(&store->mutex);
	return ret;
}

uint32_t bmptn_store_get_percent(struct bmptn_store *store)
{
	uint32_t ret;
	pthread_mutex_lock(&store->mutex);
	ret = (bmptn_store_gethdr(store))->percent;
	pthread_mutex_unlock(&store->mutex);
	return ret;
}

int bmptn_set_cluster_name(struct bmptn_store *store,
			   uint32_t cluster_id, const char *name, int name_len)
{
	int rc = 0;
	uint64_t off;
	struct bmptn_cluster_name *cname;
	struct bmc_handle *bmc = bmvec_generic_get(
			store->bmc_handle_vec, cluster_id, sizeof(*bmc));
	if (!name_len)
		name_len = strlen(name);
	if (!bmc) {
		return ENOENT;
	}
	cname = BMPTR(store->mem, bmc->bmstr_off);
	if (!cname || cname->alloc_len < name_len) {
		/* need new mem */
		off = bmem_alloc(store->mem, sizeof(*cname) + name_len);
		if (!off) {
			return ENOMEM;
		}
		bmc->bmstr_off = off;
		cname = BMPTR(store->mem, off);
		cname->alloc_len = name_len;
	}
	memcpy(cname->bstr.cstr, name, name_len);
	cname->bstr.blen = name_len;
	return 0;
}

int bmptn_compute_cluster_name(struct bmptn_store *store, uint32_t cluster_id)
{
	int rc = 0;
	struct bdstr *bdstr = NULL;
	struct bmc_handle *bmc = bmvec_generic_get(store->bmc_handle_vec,
						cluster_id, sizeof(*bmc));

	const struct bstr *bstr;
	const struct bstr *ptn;
	struct bstr *ptnx = NULL;
	struct bstr *ptny = NULL;
	struct bstr *ptntmp;
	struct bstr *ptn_buff = NULL;
	struct bmptn_node *node;
	int idx_len;
	int __idx_len;
	int *idx = NULL;
	int max_blen = 0;

	int i, j, m, n;

	void *buff = NULL;
	int buffsz;

	int first = 1;

	if (!bmc) {
		rc = ENOENT;
		goto cleanup;
	}

	bdstr = bdstr_new(1024);
	if (!bdstr) {
		rc = ENOMEM;
		goto cleanup;
	}

	BMLIST_FOREACH(node, bmc->bmlist_off, link, store->nodes->mem) {
		ptn = bptn_store_get_ptn(store->ptn_store, node->ptn_id);
		if (ptn->blen > max_blen)
			max_blen = ptn->blen;
	}

	buffsz = max_blen / sizeof(uint32_t);
	buffsz *= buffsz;
	buffsz *= sizeof(uint32_t);

	buff = malloc(buffsz);
	if (!buff) {
		rc = ENOMEM;
		goto cleanup;
	}

	__idx_len = max_blen / sizeof(uint32_t);
	idx = malloc(__idx_len * sizeof(*idx));
	if (!idx) {
		rc = ENOMEM;
		goto cleanup;
	}

	ptn_buff = malloc(2*(sizeof(*ptn) + max_blen));
	if (!ptn_buff) {
		rc = ENOMEM;
		goto cleanup;
	}

	ptnx = ptn_buff;
	ptny = ((void*)ptnx) + (sizeof(*ptn) + max_blen);

	BMLIST_FOREACH(node, bmc->bmlist_off, link, store->nodes->mem) {
		ptn = bptn_store_get_ptn(store->ptn_store, node->ptn_id);

		if (first) {
			memcpy(ptnx, ptn, bstr_len(ptn));
			first = 0;
			idx_len = ptn->blen / sizeof(uint32_t);
			for (i = 0; i < idx_len; i++) {
				idx[i] = i;
			}
			continue;
		}

		/* lcs ... */
		idx_len = __idx_len;
		rc = bstr_lcsX_u32(ptn, ptnx, idx, &idx_len, buff, buffsz);
		assert(rc == 0);
		ptny->blen = idx_len * sizeof(uint32_t);
		for (i = 0; i < idx_len; i++) {
			ptny->u32str[i] = ptn->u32str[idx[i]];
		}

		ptntmp = ptnx;
		ptnx = ptny;
		ptny = ptntmp;
	}

	i = j = 0;
	n = ptn->blen / sizeof(uint32_t);
	while (i < n) {
		if (j < idx_len && i == idx[j]) {
			bstr = btkn_store_get_bstr(store->tkn_store,
							ptn->u32str[i]);
			assert(bstr);
			rc = bdstr_append_printf(bdstr, "%.*s",
						bstr->blen, bstr->cstr);
			assert(rc == 0);
			j++;
		} else {
			rc = bdstr_append_printf(bdstr, "*");
			assert(rc == 0);
		}
		i++;
	}

	rc = bmptn_set_cluster_name(store, cluster_id,
					bdstr->str, bdstr->str_len);

cleanup:
	if (ptn_buff) {
		free(ptn_buff);
	}

	if (bdstr) {
		bdstr_free(bdstr);
	}

	if (idx) {
		free(idx);
	}

	if (buff) {
		free(buff);
	}

	return rc;
}

const struct bstr *bmptn_get_cluster_name(struct bmptn_store *store, int cid)
{
	struct bmptn_store_header *hdr = bmptn_store_gethdr(store);
	int n = hdr->last_cls_id;
	if (cid > n) {
		errno = ENOENT;
		return NULL;
	}
	struct bmc_handle *bmc = bmvec_generic_get(store->bmc_handle_vec,
							cid, sizeof(*bmc));
	struct bmptn_cluster_name *cname = BMPTR(store->mem, bmc->bmstr_off);

	if (cname)
		return &cname->bstr;

	return NULL;
}

int bmptn_cluster(struct bmptn_store *store, struct bmeta_cluster_param *param)
{
	int rc = 0;
	char *dist_buff;
	int i;
	struct bhash *cls1;
	struct bhash_entry **eng_sig_array;
	struct bmptn_store_header *hdr = bmptn_store_gethdr(store);
	pthread_mutex_lock(&store->mutex);
	if (hdr->state != BMPTN_STORE_STATE_INITIALIZED) {
		pthread_mutex_unlock(&store->mutex);
		return EINVAL;
	}
	rc = btkn_store_refresh(store->tkn_store);
	if (rc) {
		pthread_mutex_unlock(&store->mutex);
		return rc;
	}
	rc = bptn_store_refresh(store->ptn_store);
	if (rc) {
		pthread_mutex_unlock(&store->mutex);
		return rc;
	}
	hdr->state = BMPTN_STORE_STATE_META_1;
	hdr->diff_ratio = param->diff_ratio;
	hdr->looseness = param->looseness;
	hdr->refinement_speed = param->refinement_speed;
	pthread_mutex_unlock(&store->mutex);
	hdr->last_ptn_id = bptn_store_last_id(store->ptn_store);
	rc = __bmptn_cluster_1(store);
	if (rc)
		return rc;
	__bmptn_store_set_state(store, BMPTN_STORE_STATE_META_2);
	rc = __bmptn_cluster_2(store);
	if (rc)
		return rc;
	__bmptn_store_set_state(store, BMPTN_STORE_STATE_REFINING);
	i = 1;
	while (i <= bmptn_store_gethdr(store)->last_cls_id) {
		struct bmc_handle *bmc = bmvec_generic_get(
					store->bmc_handle_vec, i, sizeof(*bmc));
		bmc->bmstr_off = 0;
		if (bmc->avg_dist < 0) {
			bmc->avg_dist = __avg_dist(store, i);
		}
		if (bmc->avg_dist > bmptn_store_gethdr(store)->looseness) {
			float thr;
			if (bmc->dist_thr < 0) {
				thr = bmptn_store_gethdr(store)->diff_ratio;
			} else {
				thr = bmc->dist_thr / bmptn_store_gethdr(store)->refinement_speed;
			}
			rc = bmptn_refine_cluster(store, i, thr);
			if (rc)
				return rc;
			/* Continue refining the the same cluster until
			 * we have a good avg_dist */
			continue;
		}
		hdr = bmptn_store_gethdr(store);
		hdr->percent = ((float)i/hdr->last_cls_id) * 100;
		i++;
	}
	i = 1;
	while (i <= bmptn_store_gethdr(store)->last_cls_id) {
		rc = bmptn_compute_cluster_name(store, i);
		if (rc) {
			__bmptn_store_set_state(store, BMPTN_STORE_STATE_ERROR);
			return rc;
		}
		i++;
	}
	__bmptn_store_set_state(store, BMPTN_STORE_STATE_DONE);
	return rc;
}

int bmptn_store_get_class_id(struct bmptn_store *store, uint32_t ptn_id)
{
	int id = 0;
	struct bmptn_store_header *hdr;
	struct bmptn_node *node;
	pthread_mutex_lock(&store->mutex);
	hdr = bmptn_store_gethdr(store);
	node = bmvec_generic_get(store->nodes, ptn_id, sizeof(*node));
	if (node)
		id = node->label;
	pthread_mutex_unlock(&store->mutex);
	return id;
}

int bmptn_store_get_class_id_array(struct bmptn_store *store, struct barray *array)
{
	int rc = 0;
	uint32_t n = bptn_store_last_id(store->ptn_store);
	uint32_t i;
	int label;
	struct bmptn_store_header *hdr;
	struct bmptn_node *node;

	pthread_mutex_lock(&store->mutex);
	hdr = bmptn_store_gethdr(store);
	for (i = BMAP_ID_BEGIN; i <= n; i++) {
		node = bmvec_generic_get(store->nodes, i, sizeof(*node));
		if (node)
			label = node->label;
		else
			label = 0;
		rc = barray_set(array, i, &label);
		if (rc)
			goto out;
	}
out:
	pthread_mutex_unlock(&store->mutex);
	return rc;
}
