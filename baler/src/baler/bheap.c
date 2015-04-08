/**
 * \file bheap.c
 * \author Narate Taerat (narate at ogc dot us)
 */
#include "bheap.h"
#include "butils.h"

struct bheap *bheap_new(int alloc_len, int (*cmp)(void*,void*))
{
	struct bheap *h = NULL;
	h = calloc(1, sizeof(*h));
	if (!h)
		goto out;
	h->cmp = cmp;
	h->alloc_len = alloc_len;
	h->array = malloc(alloc_len * sizeof(*h->array));
	if (!h)
		goto err0;
	goto out;
err0:
	free(h);
	h = NULL;
out:
	return h;
}

void bheap_free(struct bheap *h)
{
	free(h->array);
	free(h);
}

int bheap_insert(struct bheap *h, void *elm)
{
	int i, p, cmp;
	if (h->len == h->alloc_len) {
		/* try expanding ... */
		int new_len = h->alloc_len + 4096;
		void **tmp = realloc(h->array, new_len * sizeof(*h->array));
		if (!tmp)
			return ENOMEM;
		h->alloc_len = new_len;
		h->array = tmp;
	}
	i = h->len;
	h->len++;
	while (i) {
		p = (i-1)/2;
		cmp = h->cmp(elm, h->array[p]);
		if (cmp >= 0) {
			break;
		}
		h->array[i] = h->array[p];
		i = p;
	}
	h->array[i] = elm;
	return 0;
}

void* bheap_remove_top(struct bheap *h)
{
	void *tmp = bheap_get_top(h);
	if (!tmp)
		return NULL;
	h->len--;
	h->array[0] = h->array[h->len];
	bheap_percolate_top(h);
	return tmp;
}

void* bheap_get_top(struct bheap *h)
{
	if (h->len)
		return h->array[0];
	return NULL;
}

void bheap_percolate_top(struct bheap *h)
{
	int i, l, r, d;
	int cmp;
	void *tmp = h->array[0];
	i = 0;
	l = 1;
	r = 2;
	while (r < h->len) {
		cmp = h->cmp(h->array[l], h->array[r]);
		if (cmp < 0) {
			cmp = h->cmp(tmp, h->array[l]);
			d = l;
		} else {
			cmp = h->cmp(tmp, h->array[r]);
			d = r;
		}

		if (cmp <= 0)
			break;

		h->array[i] = h->array[d];

		i = d;
		l = i*2+1;
		r = l+1;
	}

	if (l < h->len) {
		cmp = h->cmp(tmp, h->array[l]);
		if (cmp > 0) {
			h->array[i] = h->array[l];
			i = l;
		}
	}

	h->array[i] = tmp;
}

int bheap_verify(struct bheap *h)
{
	int i, l, r;
	for (i = 0; i < h->len; i++) {
		l = i*2+1;
		r = l+1;
		if (l < h->len) {
			if (h->cmp(h->array[i], h->array[l]) > 0)
				return EINVAL;
		}
		if (r < h->len) {
			if (h->cmp(h->array[i], h->array[r]) > 0)
				return EINVAL;
		}
	}
	return 0;
}
