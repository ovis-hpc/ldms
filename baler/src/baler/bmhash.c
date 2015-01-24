/**
 * \file bmhash.c
 * \author Narate Taerat (narate at ogc dot us)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "bmhash.h"
#include "butils.h"

#include "fnv_hash.h"
#include "murmur_hash.h"

#define BMHASH_VER "BMHASH_0.1"

static inline
struct bmhash_header *bmhash_get_header(struct bmhash *bmh)
{
	return bmh->mem->ptr;
}

static inline
struct bvec_u64 *bmhash_get_table(struct bmhash *bmh)
{
	return bmhash_get_header(bmh)->table;
}

static inline
int __bmhash_initialized(struct bmhash *bmh)
{
	return (bmh->mem->hdr->ulen >= sizeof(struct bmhash_header)) &&
		(memcmp((bmhash_get_header(bmh)->ver), BMHASH_VER, sizeof(BMHASH_VER))==0);
}

static
int __bmhash_init(struct bmhash *bmh, va_list ap)
{
	int rc = 0;
	uint64_t offset;
	uint32_t i;
	uint32_t hash_size = va_arg(ap, typeof(hash_size));
	uint32_t fn_enum = va_arg(ap, typeof(fn_enum));
	uint32_t seed = va_arg(ap, typeof(seed));
	struct bmhash_header *hdr;

	offset = bmem_alloc(bmh->mem, sizeof(*hdr) + sizeof(struct bvec_u64) +
					hash_size*sizeof(uint64_t));
	if (!offset)
		return ENOMEM;
	hdr = BMPTR(bmh->mem, offset);
	memcpy(hdr->ver, BMHASH_VER, sizeof(BMHASH_VER));
	hdr->count = 0;
	hdr->hash_size = hash_size;
	hdr->fn_enum = fn_enum;
	hdr->seed = seed;
	hdr->table->alloc_len = hdr->hash_size;
	hdr->table->len = hdr->hash_size;

	for (i = 0; i < hdr->hash_size; i++) {
		hdr->table->data[i] = 0;
	}
	return 0;
}

struct bmhash *bmhash_open(const char *path, int flag, ...)
{
	struct bmhash *bmh = NULL;
	va_list ap;
	struct stat st;
	struct bmhash_header *hdr;
	int rc;

	rc = stat(path, &st);

	if (rc && errno == ENOENT && !(flag & O_CREAT)) {
		return NULL;
	}

	bmh = malloc(sizeof(*bmh));
	if (!bmh)
		return NULL;

	snprintf(bmh->path, sizeof(bmh->path), "%s", path);
	bmh->mem = bmem_open(path);
	if (!bmh->mem) {
		free(bmh);
		return NULL;
	}

	if (!__bmhash_initialized(bmh)) {
		if (!(flag & O_CREAT)) {
			bmhash_close_free(bmh);
			errno = EINVAL;
			return NULL;
		}
		va_start(ap, flag);
		rc = __bmhash_init(bmh, ap);
		va_end(ap);
		if (rc) {
			bmhash_close_free(bmh);
			errno = rc;
			return NULL;
		}
	}

	hdr = bmhash_get_header(bmh);

	switch (hdr->fn_enum) {
	case BMHASH_FN_FNV:
		bmh->hash_fn = fnv_hash_a1_32;
		break;
	case BMHASH_FN_MURMUR:
		bmh->hash_fn = (void*)MurmurHash3_x86_32;
		break;

	default:
		bmhash_close_free(bmh);
		bmh = NULL;
		errno = EINVAL;
	}

	return bmh;
}

int bmhash_unlink(const char *path)
{
	return bmem_unlink(path);
}

void bmhash_close_free(struct bmhash *bmh)
{
	bmem_close_free(bmh->mem);
	bmh->mem = NULL;
	free(bmh);
}

static
struct bmhash_entry *__bmhash_entry_find(struct bmhash *bmh,
				const struct bstr *key, uint32_t *hv)
{
	uint32_t __hv;
	struct bmhash_header *hdr = bmhash_get_header(bmh);
	struct bmhash_entry *ent = NULL;
	__hv = bmh->hash_fn(key->cstr, key->blen, hdr->seed);
	__hv %= hdr->table->len;

	if (hv)
		*hv = __hv;

	BMLIST_FOREACH(ent, hdr->table->data[__hv], link, bmh->mem) {
		if (0 == bstr_cmp(key, &ent->key)) {
			/* Found it */
			return ent;
		}
	}

	return NULL; /* Not found */
}

struct bmhash_entry *bmhash_entry_set(struct bmhash *bmh,
				      const struct bstr *key, uint64_t value)
{
	uint32_t hv;
	struct bmhash_entry *ent = __bmhash_entry_find(bmh, key, &hv);
	if (!ent) {
		struct bmhash_header *hdr = bmhash_get_header(bmh);
		uint64_t off = bmem_alloc(bmh->mem, sizeof(*ent) + key->blen);
		if (!off) {
			errno = ENOMEM;
			return NULL;
		}
		ent = BMPTR(bmh->mem, off);
		memcpy(ent->key.cstr, key->cstr, key->blen);
		ent->key.blen = key->blen;
		BMLIST_INSERT_HEAD(hdr->table->data[hv], ent, link, bmh->mem);
	}
	ent->value = value;
	return ent;
}

struct bmhash_entry *bmhash_entry_get(struct bmhash *bmh,
				      const struct bstr *key)
{
	return __bmhash_entry_find(bmh, key, NULL);
}

struct bmhash_iter *bmhash_iter_new(struct bmhash *bmh)
{
	struct bmhash_iter *iter = malloc(sizeof(*iter));
	if (!iter)
		return NULL;
	iter->bmh = bmh;
	iter->ent = NULL;
	iter->idx = 0;
	return iter;
}

void bmhash_iter_free(struct bmhash_iter *iter)
{
	free(iter);
}

struct bmhash_entry *bmhash_iter_entry(struct bmhash_iter *iter)
{
	return iter->ent;
}

int bmhash_iter_begin(struct bmhash_iter *iter)
{
	struct bmhash_header *hdr = bmhash_get_header(iter->bmh);
	struct bvec_u64 *table = hdr->table;
	int i;
	for (i = 0; i < table->len; i++) {
		if (table->data[i]) {
			iter->idx = i;
			iter->ent = BMPTR(iter->bmh->mem, table->data[i]);
			return 0;
		}
	}
	return ENOENT;
}

int bmhash_iter_next(struct bmhash_iter *iter)
{
	if (!iter->ent)
		return ENOENT;

	iter->ent = BMLIST_NEXT(iter->ent, link, iter->bmh->mem);
	if (iter->ent)
		return 0;

	/* Moving iter to the next list */
	struct bmhash_header *hdr = bmhash_get_header(iter->bmh);
	struct bvec_u64 *table = hdr->table;
	iter->idx++;
	while (iter->idx < table->len) {
		if (table->data[iter->idx]) {
			iter->ent = BMPTR(iter->bmh->mem,
						table->data[iter->idx]);
			return 0;
		}
		iter->idx++;
	}

	/* Reaching here == no more list */
	return ENOENT;
}
