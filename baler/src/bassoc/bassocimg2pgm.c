#include <stdio.h>
#include <stdlib.h>

#include "bassoc.h"
#include "bassocimg.h"

void usage()
{
	printf("Usage: bassocimg INPUT_FILE OUTPUT_FILE\n");
}

struct rgba {
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;
};

int main(int argc, char **argv)
{
	int i, n, rc;
	uint64_t x, y;
	uint64_t maxx = 0, maxy = 0;
	uint64_t idx;
	struct bassocimg *v;
	struct bassocimg_pixel *first_pxl;
	struct bassocimg_pixel *pxl;
	if (argc != 3) {
		usage();
		exit(-1);
	}
	if (!bfile_exists(argv[1])) {
		berr("File not found: %s", argv[1]);
		exit(-1);
	}
	v = bassocimg_open(argv[1], 0);
	if (!v) {
		berr("Cannot open file %s, error(%d): %m", argv[1], errno);
		exit(-1);
	}
	FILE *out = fopen(argv[2], "w");
	if (!out) {
		berr("Cannot open file %s, error(%d): %m", argv[2], errno);
		exit(-1);
	}

	n = bassocimg_get_pixel_len(v);
	first_pxl = bassocimg_get_pixel(v, 0);
	for (i = 0; i < n; i++) {
		pxl = bassocimg_get_pixel(v, i);
		x = (pxl->sec - first_pxl->sec)/3600;
		y = (pxl->comp_id);
		if (x > maxx)
			maxx = x;
		if (y > maxy)
			maxy = y;
	}
	uint8_t *buff = calloc((maxx+1)*(maxy+1), 1);
	for (i = 0; i < n; i++) {
		pxl = bassocimg_get_pixel(v, i);
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
	}
	fprintf(out, "P5\n %d\n %d\n %d\n", (int)(maxx+1),(int)(maxy+1), 255);
	fwrite(buff, 1, (maxx+1)*(maxy+1), out);
	return 0;
}
