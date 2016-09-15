#include <stdio.h>
#include <stdlib.h>
#include "bassocimg.h"
#include "baler/butils.h"

void usage()
{
	printf("bassocimg_intersect_test CACHE NAME1 NAME2 OUT_CACHE NAME_OUT\n");
}

int main(int argc, char **argv)
{
	if (argc != 6) {
		usage();
		exit(-1);
	}

	const char *in_cache_path = argv[1];
	const char *out_cache_path = argv[4];
	struct bstr *in_name_1 = bstr_alloc_init_cstr(argv[2]);
	struct bstr *in_name_2 = bstr_alloc_init_cstr(argv[3]);
	struct bstr *out_name = bstr_alloc_init_cstr(argv[5]);

	struct bassocimg_cache *in_cache, *out_cache;

	in_cache = bassocimg_cache_open(in_cache_path, 0);
	if (!in_cache) {
		berr("Cannot open image cache %s", in_cache_path);
		exit(-1);
	}
	out_cache = bassocimg_cache_open(argv[4], 1);
	if (!out_cache) {
		berr("Cannot open image cache %s", out_cache_path);
		exit(-1);
	}

	bassocimg_t img1, img2;
	img1 = bassocimg_cache_get_img(in_cache, in_name_1, 0);
	if (!img1) {
		berr("Input image '%.*s' not found",  in_name_1->blen, in_name_1->cstr);
		exit(-1);
	}
	img2 = bassocimg_cache_get_img(in_cache, in_name_2, 0);
	if (!img1) {
		berr("Input image '%.*s' not found",  in_name_2->blen, in_name_2->cstr);
		exit(-1);
	}

	bassocimg_t out;
	out = bassocimg_cache_get_img(out_cache, out_name, 1);
	bassocimg_reset(out);

	bassocimg_intersect(img1, img2, out);

	return 0;
}
