#include <stdio.h>
#include <stdlib.h>
#include "bassoc.h"

void usage()
{
	printf("Usage: bassocimg_dump INPUT_FILE\n");
}

int main(int argc, char **argv)
{
	int i, n, rc;
	uint64_t idx;
	struct bassocimg *v;
	struct bassocimg_pixel *first_pxl;
	struct bassocimg_pixel *pxl;
	struct bassocimg_hdr *imghdr;
	if (argc != 2) {
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

	n = bassocimg_get_pixel_len(v);
	imghdr = bassocimg_get_hdr(v);
	printf("Total count: %lu\n", imghdr->count);
	printf("# of pixels: %d\n", n);
	first_pxl = bassocimg_get_pixel(v, 0);
	for (i = 0; i < n; i++) {
		pxl = bassocimg_get_pixel(v, i);
		printf("[%d,%d,%d]\n", pxl->sec, pxl->comp_id, pxl->count);
	}
	return 0;
}
