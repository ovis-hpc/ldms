#include <stdio.h>
#include <stdlib.h>
#include "bassoc.h"

void usage()
{
	printf("Usage: bassocimg_dump CACHE IMG_NAME\n");
}

int main(int argc, char **argv)
{
	int i, n, rc;
	uint64_t idx;
	struct bassocimg_cache *cache;
	struct bassocimg *img;
	struct bassocimg_pixel *first_pxl;
	struct bassocimg_pixel *pxl;
	struct bassocimg_hdr *imghdr;
	struct bassocimg_iter itr;
	struct bstr *name;
	if (argc != 3) {
		usage();
		exit(-1);
	}

	if (!bis_dir(argv[1])) {
		berr("'%s' not found or not a cache dir.", argv[1]);
		exit(-1);
	}

	cache = bassocimg_cache_open(argv[1], 0);
	if (!cache) {
		berr("Cannot open cache %s, error(%d): %m", argv[1], errno);
		exit(-1);
	}

	name = bstr_alloc_init_cstr(argv[2]);
	img = bassocimg_cache_get_img(cache, name, 0);
	if (!img) {
		berr("Image not found: %s", argv[2]);
		exit(-1);
	}

	printf("Total count: %lu\n", BASSOCIMG_HDR(img)->count);
	printf("# of pixels: %lu\n", BASSOCIMG_HDR(img)->len);
	bassocimg_iter_init(&itr, img);
	for (pxl = bassocimg_iter_first(&itr);
			pxl; pxl = bassocimg_iter_next(&itr)) {
		printf("[%lu,%lu,%lu]\n", pxl->sec, pxl->comp_id, pxl->count);
	}
	return 0;
}
