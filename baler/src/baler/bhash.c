/**
 * \file bhash.c
 * \author Narate Taerat (narate at ogc dot us)
 */

#include "bhash.h"
#include "fnv_hash.h"

struct bhash *bhash_new(size_t hash_size, uint32_t seed, bhash_fn_t hash_fn)
{
	struct bhash *h;

	h = malloc(sizeof(*h));
	if (!h)
		return NULL;

	if (hash_fn)
		h->hash_fn = hash_fn;
	else
		h->hash_fn = fnv_hash_a1_32;

	h->count = 0;
	h->hash_size = hash_size;
	h->seed = seed;
	h->hash_table = calloc(h->hash_size, sizeof(h->hash_table[0]));

	if (!h->hash_table) {
		free(h);
		return NULL;
	}

	return h;
}

void bhash_free(struct bhash *h)
{
	struct bhash_entry *ent;
	int i;
	for (i = 0; i < h->hash_size; i++) {
		while ((ent = LIST_FIRST(&h->hash_table[i]))) {
			LIST_REMOVE(ent, link);
			free(ent);
		}
	}
	free(h->hash_table);
	free(h);
}

static
struct bhash_entry *bhash_entry_new(const char *key, size_t keylen)
{
	struct bhash_entry *ent = malloc(sizeof(*ent) + keylen);
	if (!ent)
		return NULL;
	memcpy(ent->key, key, keylen);
	ent->keylen = keylen;
	return ent;
}

static inline
int __bhash_key_cmp(const char *k0, size_t len0, const char *k1, size_t len1)
{
	if (len0 == len1)
		return memcmp(k0, k1, len0);
	return len0 - len1;
}

static
struct bhash_entry * __bhash_entry_find(struct bhash *h, const char *key, size_t keylen,
			uint32_t *hvalue)
{
	struct bhash_entry *ent = NULL;
	uint32_t hv = h->hash_fn(key, keylen, h->seed) % h->hash_size;
	ent = LIST_FIRST(&h->hash_table[hv]);
	while (ent) {
		if (0 == __bhash_key_cmp(key, keylen, ent->key, ent->keylen)) {
			break;
		}
		ent = LIST_NEXT(ent, link);
	}
out:
	if (hvalue)
		*hvalue = hv;
	return ent;
}

struct bhash_entry *bhash_entry_set(struct bhash *h, const char *key,
						size_t keylen, uint64_t value)
{
	uint32_t hv;
	struct bhash_entry *ent = __bhash_entry_find(h, key, keylen, &hv);
	if (!ent) {
		ent = bhash_entry_new(key, keylen);
		if (!ent)
			return NULL;
		LIST_INSERT_HEAD(&h->hash_table[hv], ent, link);
		h->count++;
	}
	ent->value = value;
	return ent;
}

int bhash_entry_del(struct bhash *h, const char *key, size_t keylen)
{
	struct bhash_entry *ent = __bhash_entry_find(h, key, keylen, NULL);
	if (!ent)
		return ENOENT;
	bhash_entry_remove_free(h, ent);
	return 0;
}

int bhash_entry_remove_free(struct bhash *h, struct bhash_entry *ent)
{
	bhash_entry_remove(h, ent);
	bhash_entry_free(ent);
	return 0;
}

int bhash_entry_remove(struct bhash *h, struct bhash_entry *ent)
{
	LIST_REMOVE(ent, link);
	h->count--;
	return 0;
}

void bhash_entry_free(struct bhash_entry *ent)
{
	free(ent);
}

struct bhash_entry *bhash_entry_get(struct bhash *h, const char *key,
								size_t keylen)
{
	return __bhash_entry_find(h, key, keylen, NULL);
}

struct bhash_iter *bhash_iter_new(struct bhash *h)
{
	struct bhash_iter *iter = calloc(1, sizeof(*iter));
	if (!iter)
		return NULL;
	iter->h = h;
	return iter;
}

int bhash_iter_begin(struct bhash_iter *iter)
{
	iter->idx = 0;
	while (iter->idx < iter->h->hash_size) {
		iter->entry = LIST_FIRST(&iter->h->hash_table[iter->idx]);
		if (iter->entry)
			return 0;
		iter->idx++;
	}
	return ENOENT;
}

int bhash_iter_next(struct bhash_iter *iter)
{
	if (!iter->entry)
		return ENOENT;
	iter->entry = LIST_NEXT(iter->entry, link);
	if (iter->entry)
		return 0;
	iter->idx++;
	while (iter->idx < iter->h->hash_size) {
		iter->entry = LIST_FIRST(&iter->h->hash_table[iter->idx]);
		if (iter->entry)
			return 0;
		iter->idx++;
	}
	return ENOENT;
}

struct bhash_entry *bhash_iter_entry(struct bhash_iter *iter)
{
	return iter->entry;
}

void bhash_iter_free(struct bhash_iter *iter)
{
	free(iter);
}
/* EOF */
