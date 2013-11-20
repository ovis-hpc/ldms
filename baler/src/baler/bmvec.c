#include "bmvec.h"

struct bmvec_u64* bmvec_u64_open(char *path)
{
	struct bmvec_u64 *vec = (typeof(vec)) malloc(sizeof(*vec));
	if (!vec) {
		berror("malloc");
		goto err0;
	}
	pthread_mutex_init(&vec->mutex, NULL);
	vec->mem = bmem_open(path);
	if (!vec->mem) {
		berror("bmem_open");
		goto err1;
	}
	vec->bvec = (typeof(vec->bvec)) vec->mem->ptr;
	return vec;
err2:
	bmem_close_free(vec->mem);
err1:
	free(vec);
err0:
	return NULL;
}

void* bmvec_generic_open(char *path)
{
	struct bmvec_char *vec = (typeof(vec)) malloc(sizeof(*vec));
	if (!vec) {
		berror("malloc");
		goto err0;
	}
	pthread_mutex_init(&vec->mutex, NULL);
	vec->mem = bmem_open(path);
	if (!vec->mem) {
		berror("bmem_open");
		goto err1;
	}
	vec->bvec = (typeof(vec->bvec)) vec->mem->ptr;
	return vec;
err2:
	bmem_close_free(vec->mem);
err1:
	free(vec);
err0:
	return NULL;
}

int bmvec_u64_init(struct bmvec_u64 *vec, uint32_t size, uint64_t value)
{
	int __size = (size|(BMVEC_INC-1))+1;
	int init_size = sizeof(vec->bvec)+__size*sizeof(uint64_t);
	int64_t off = bmem_alloc(vec->mem, init_size);
	if (off == -1) {
		berror("bmem_alloc");
		return -1;
	}
	vec->bvec = (typeof(vec->bvec)) vec->mem->ptr;
	vec->bvec->alloc_len = __size;
	vec->bvec->len = size;
	int i;
	uint64_t *data = vec->bvec->data;
	for (i=0; i<size; i++) {
		data[i] = value;
	}
	return 0;
}

int bmvec_generic_init(void *_vec, uint32_t size, void *elm, uint32_t elm_size)
{
	struct bmvec_char *vec = (typeof(vec)) _vec;
	int __size = (size|(BMVEC_INC-1))+1;
	int init_size = sizeof(vec->bvec)+__size*elm_size;
	int64_t off = bmem_alloc(vec->mem, init_size);
	if (off == -1) {
		berror("bmem_alloc");
		return -1;
	}
	vec->bvec = (typeof(vec->bvec)) vec->mem->ptr;
	vec->bvec->alloc_len = __size;
	vec->bvec->len = size;
	int i;
	char *data = vec->bvec->data;
	for (i=0; i<size; i++) {
		memcpy(data+elm_size*i, elm, elm_size);
	}
	return 0;
}

void bmvec_u64_close_free(struct bmvec_u64 *vec)
{
	bmem_close_free(vec->mem);
	free(vec);
}

void bmvec_generic_close_free(void *_vec)
{
	struct bmvec_char *vec = (typeof(vec)) _vec;
	bmem_close_free(vec->mem);
	free(vec);
}

