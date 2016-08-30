#include <stdio.h>
#include <stdlib.h>

#include "bassoc.h"
#include "bassocimg.h"

void usage()
{
	printf("Usage: bassocimg2pgm IMG_CACHE IMG_NAME OUTPUT_FILE\n");
}

struct rgba {
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;
};

int main(int argc, char **argv)
{
	const char *cache_path;
	const char *img_name;
	const char *out_path;
	int i, n, rc;
	uint64_t x, y;
	uint64_t maxx = 0, maxy = 0;
	uint64_t idx;
	bassocimg_cache_t cache;
	struct bassocimg_iter itr;
	struct bassocimg *v;
	struct bassocimg_pixel *first_pxl;
	struct bassocimg_pixel *pxl;
	struct bdbstr *bdbstr = bdbstr_new(256);
	if (!bdbstr) {
		berror("bdbstr_new()");
		exit(-1);
	}
	if (argc != 4) {
		usage();
		exit(-1);
	}
	cache_path = argv[1];
	img_name = argv[2];
	out_path = argv[3];
	cache = bassocimg_cache_open(cache_path, 0);
	if (!cache) {
		berr("Cannot open cache: %s", cache_path);
		exit(-1);
	}
	rc = bdbstr_append_printf(bdbstr, "%s", img_name);
	if (rc) {
		berr("bdbstr_append_printf() error, rc: %d", rc);
		exit(-1);
	}
	v = bassocimg_cache_get_img(cache, bdbstr->bstr, 0);
	if (!v) {
		berror("bassocimg_cache_get_img()");
		exit(-1);
	}

	FILE *out = fopen(argv[2], "w");
	if (!out) {
		berr("Cannot open file %s, error(%d): %m", argv[2], errno);
		exit(-1);
	}

	bassocimg_iter_init(&itr, v);
	pxl = first_pxl = bassocimg_iter_first(&itr);

	while (pxl) {
		x = (pxl->sec - first_pxl->sec)/3600;
		y = (pxl->comp_id);
		if (x > maxx)
			maxx = x;
		if (y > maxy)
			maxy = y;
		pxl = bassocimg_iter_next(&itr);
	}

	uint8_t *buff = calloc((maxx+1)*(maxy+1), 1);
	if (!buff) {
		berror("calloc()");
		exit(-1);
	}

	pxl = bassocimg_iter_first(&itr);
	while (pxl) {
		x = (pxl->sec - first_pxl->sec)/3600;
		y = (pxl->comp_id);
		idx = y * (maxx+1) + x;
		uint8_t c;
		if (pxl->count > 255) {
			c = 255;
		} else {
			c = pxl->count;
		}
		buff[idx] = c;
		pxl = bassocimg_iter_next(&itr);
	}
	fprintf(out, "P5\n %d\n %d\n %d\n", (int)(maxx+1),(int)(maxy+1), 255);
	fwrite(buff, 1, (maxx+1)*(maxy+1), out);
	return 0;
}
