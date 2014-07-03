#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "assert.h"
#include "big_dstring.h"
BIG_DSTRING_TYPE(65536);

/* simple test until we get unit testing in place. */

struct x {
	char *x1;
	char *x2;
};

struct x createfoo(int lim)
{
	struct x x;
	int i;
	big_dstring_t bs;
	// expect 10000 sheep rescued prints
	bdstr_init(&bs);
	for (i = 0; i < lim; i++) {
		bdstrcat(&bs, "1000 sheep rescued", DSTRING_ALL);
	}
	x.x1 = bdstr_extract(&bs);
	bdstr_set(&bs, "QQ");
	for (i = 0; i < lim; i++) {
		bdstrcat(&bs, "1000 sheep rescued", DSTRING_ALL);
	}
	x.x2 = bdstr_extract(&bs);
	return x;
}

int main(int argc, char **argv)
{
	struct x x, y;
	x = createfoo(10);
	printf("%s\n", x.x1);
	printf("%s\n", x.x2);
	free(x.x1);
	free(x.x2);

	y = createfoo(10000);
	printf("%lu,%lu\n", strlen(y.x1), strlen(y.x2));
	free(y.x1);
	free(y.x2);
	return 0;
}

