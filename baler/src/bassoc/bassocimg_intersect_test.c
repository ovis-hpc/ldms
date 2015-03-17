#include <stdio.h>
#include <stdlib.h>
#include "bassocimg.h"
#include "baler/butils.h"

void usage()
{
	printf("bassocimg_intersect_test IMG1 IMG2 OUTPUT_IMG\n");
}

int main(int argc, char **argv)
{
	if (argc != 4) {
		usage();
		exit(-1);
	}

	struct bassocimg *img0 = bassocimg_open(argv[1], 0);
	if (!img0) {
		berr("Cannot open image %s", argv[1]);
		exit(-1);
	}

	struct bassocimg *img1 = bassocimg_open(argv[2], 0);
	if (!img1) {
		berr("Cannot open image %s", argv[2]);
		exit(-1);
	}

	if (bfile_exists(argv[3])) {
		berr("Output file existed: %s\n", argv[3]);
		exit(-1);
	}

	struct bassocimg *out = bassocimg_open(argv[3], 1);
	bassocimg_intersect(img0, img1, out);

	bassocimg_close_free(img0);
	bassocimg_close_free(img1);
	bassocimg_close_free(out);

	return 0;
}
