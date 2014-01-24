#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "assert.h"
#include "big_dstring.h"
BIG_DSTRING_TYPE(65536);

/* simple test until we get unit testing in place. */

char *createfoo (int lim) 
{
	int i;
	big_dstring_t bs;
	bdstr_init(&bs);
	for (i=0; i < lim; i++) {
		bdstrcat(&bs,"1000 years luck",DSTRING_ALL);
	}
	return bdstr_extract(&bs);
}

int main(int argc, char **argv) {
char * x, *y;
x = createfoo(10);
printf("%s\n",x);
free(x);

y=createfoo(10000);
free(y);
return 0;
}

