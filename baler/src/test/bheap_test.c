#include <stdio.h>
#include "baler/bheap.h"
#include "baler/butils.h"

int uint_cmp(void *a, void *b)
{
	if ((uint64_t)a < (uint64_t)b)
		return -1;
	if ((uint64_t)a > (uint64_t)b)
		return 1;
	return 0;
}

int uint_cmp2(void *a, void *b)
{
	if ((uint64_t)a < (uint64_t)b)
		return 1;
	if ((uint64_t)a > (uint64_t)b)
		return -1;
	return 0;
}

void print_heap(struct bheap *h)
{
	int i;
	for (i = 0; i < h->len; i++) {
		printf("%ld, ", (uint64_t)h->array[i]);
	}
	printf("\n");
}

void test_heap(void *fn)
{
	uint64_t data[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
	int len = 10;
	int i;
	int rc;
	struct bheap *h = bheap_new(5, fn);
	if (!h) {
		berror("bheap_new()");
		exit(-1);
	}
	for (i = 0; i < len; i++) {
		rc = bheap_insert(h, (void*)data[i]);
		if (rc) {
			berror("bheap_insert()");
			exit(-1);
		}
		rc = bheap_verify(h);
		if (rc) {
			berror("bheap_verify()");
			exit(-1);
		}
		print_heap(h);
	}

	h->array[0] = (void*)11;
	bheap_percolate_top(h);
	rc = bheap_verify(h);
	if (rc) {
		berror("bheap_verify()");
		exit(-1);
	}
	print_heap(h);

	bheap_free(h);
}

int main(int argc, char **argv)
{
	test_heap(uint_cmp);
	test_heap(uint_cmp2);
	return 0;
}
