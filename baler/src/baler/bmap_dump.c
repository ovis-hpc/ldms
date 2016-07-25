#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "bmapper.h"

#define FMT "?hs:I"

const char *path = NULL;
int inverse = 0;

void usage()
{
	printf("\nUSAGE: bmap_dump [-I] -s MAP_FILE\n\n");
	exit(-1);
}

int main(int argc, char **argv)
{
	char o;
next_arg:
	o = getopt(argc, argv, FMT);
	switch (o) {
	case -1:
		goto no_arg;
	case 's':
		path = optarg;
		break;
	case 'I':
		inverse = 1;
		break;
	case '?':
	case 'h':
	default:
		usage();
	}
	goto next_arg;
no_arg:

	if (path == NULL)
		usage();

	int rc;
	struct stat st;

	rc = stat(path, &st);
	if (rc) {
		printf("Cannot check status of map: %s\n", path);
		exit(-1);
	}

	if (!S_ISDIR(st.st_mode)) {
		printf("Invalid map path: %s\n", path);
		exit(-1);
	}

	struct bmap *bmap = bmap_open(path);

	if (!bmap) {
		printf("Cannot open map: %s\n", path);
		exit(-1);
	}
	if (inverse) {
		bmap_dump_inverse(bmap);
	} else {
		bmap_dump(bmap);
	}
	bmap_close_free(bmap);

	return 0;
}
