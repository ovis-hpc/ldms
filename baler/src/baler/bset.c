#include "bset.h"
#include "butils.h"
#include <stdlib.h>

struct bset_u32* bset_u32_alloc(int hsize)
{
	struct bset_u32 *set = (typeof(set)) calloc(1, sizeof(*set));
	if (!set)
		goto err0;
	if (bset_u32_init(set, hsize) == -1)
		goto err1;
	return set;
err1:
	free(set);
err0:
	return NULL;
}

int bset_u32_init(struct bset_u32 *set, int hsize)
{
	if (!set)
		return -1;
	if (!hsize)
		hsize = BSET_DEFAULT_HSIZE;
	set->hash = (typeof(set->hash)) calloc(hsize, sizeof(*set->hash));
	if (!set->hash)
		return -1;
	set->hsize = hsize;
	set->count = 0;
	return 0;
}

void bset_u32_clear(struct bset_u32 *set)
{
	if (!set)
		return;
	int i;
	for (i=0; i<set->hsize; i++) {
		struct blist_u32 *elm;
		while (elm = LIST_FIRST(&set->hash[i])) {
			LIST_REMOVE(elm, link);
			free(elm);
		}
	}
	free(set->hash);
}

void bset_u32_free(struct bset_u32 *set)
{
	bset_u32_clear(set);
	free(set);
}

int bset_u32_exist(struct bset_u32 *set, uint32_t val)
{
	uint32_t idx = val % set->hsize;
	struct blist_u32_head *lh = &set->hash[idx];
	struct blist_u32 *elm;
	LIST_FOREACH(elm, lh, link) {
		if (elm->data == val)
			return 1;
	}
	return 0;
}

/**
 * The same as ::bset_u32_exist, but also export index \a idx of the \a val.
 * \param set The pointer to ::bset_u32.
 * \param val The value to be inserted.
 * \param[out] idx The (output) index of \a val.
 */
int bset_u32_exist_idx(struct bset_u32 *set, uint32_t val, uint32_t *idx)
{
	*idx = val % set->hsize;
	struct blist_u32_head *lh = &set->hash[*idx];
	struct blist_u32 *elm;
	LIST_FOREACH(elm, lh, link) {
		if (elm->data == val)
			return 1;
	}
	return 0;
}

int bset_u32_insert(struct bset_u32 *set, uint32_t val)
{
	int idx;
	if (bset_u32_exist_idx(set, val, &idx))
		return EEXIST;
	struct blist_u32 *elm = (typeof(elm)) malloc(sizeof(*elm));
	if (!elm)
		return ENOMEM;
	elm->data = val;
	LIST_INSERT_HEAD(&set->hash[idx], elm, link);
	set->count++;
	return 0;
}

int bset_u32_remove(struct bset_u32 *set, uint32_t val)
{
	int idx = val % set->hsize;
	struct blist_u32 *elm;
	LIST_FOREACH(elm, &set->hash[idx], link) {
		if (elm->data != val)
			continue;
		LIST_REMOVE(elm, link);
		free(elm);
		return 0;
	}
	return ENOENT;
}

struct bset_u32_iter* bset_u32_iter_new(struct bset_u32 *set)
{
	struct bset_u32_iter *iter = calloc(1, sizeof(*iter));
	if (!iter)
		return NULL;
	iter->set = set;
	return iter;
}

int bset_u32_iter_next(struct bset_u32_iter *iter, uint32_t *out)
{
	while (!iter->elem && iter->next_idx < iter->set->hsize) {
		iter->elem = LIST_FIRST(&iter->set->hash[iter->next_idx++]);
	}

	if (!iter->elem)
		return ENOENT;

	*out = iter->elem->data;
	iter->elem = LIST_NEXT(iter->elem, link);
	return 0;
}

int bset_u32_iter_reset(struct bset_u32_iter *iter)
{
	iter->next_idx = 0;
	iter->elem = NULL;
}

int uint32_t_cmp(const void *p1, const void *p2)
{
	/* 0 - 4294967295 = 1, so *p1 - *p2 is unsafe */
	if (*(uint32_t*)p1 < *(uint32_t*)p2)
		return -1;
	if (*(uint32_t*)p1 > *(uint32_t*)p2)
		return 1;
	return 0;
}

void* bset_u32_to_brange_u32(struct bset_u32 *set)
{
	LIST_HEAD(, brange_u32) *head = calloc(1, sizeof(*head));
	if (!head)
		goto err0;
	uint32_t *data = malloc(sizeof(*data) * set->count);
	if (!data)
		goto err1;
	struct bset_u32_iter *iter = bset_u32_iter_new(set);
	if (!iter)
		goto err2;
	uint32_t v;
	int i = 0;
	while (bset_u32_iter_next(iter, &v) == 0) {
		data[i++] = v;
	}
	free(iter); /* iter is not needed anymore */
	iter = NULL;
	qsort(data, set->count, sizeof(data[0]), uint32_t_cmp);
	struct brange_u32 *pr; /* previous range */
	struct brange_u32 *r; /* current range */
	/* init the first range */
	r = calloc(1, sizeof(*r));
	if (!r)
		goto err3;
	r->a = r->b = data[0];
	LIST_INSERT_HEAD(head, r, link);

	for (i=1; i<set->count; i++) {
		if (data[i] - r->b == 1) {
			r->b = data[i];
			continue;
		}
		/* else create new range */
		pr = r;
		r = calloc(1, sizeof(*r));
		if (!r)
			goto err4;
		r->a = r->b = data[i];
		LIST_INSERT_AFTER(pr, r, link);
	}
	free(data);
	data = NULL;

	return head;

err4:
	while (r = LIST_FIRST(head)) {
		LIST_REMOVE(r, link);
		free(r);
	}
err3:
	free(iter);
err2:
	free(data);
err1:
	free(head);
err0:
	return NULL;
}

