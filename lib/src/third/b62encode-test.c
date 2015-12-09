#include "b62encode.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
int main() {
	uint64_t big = UINT64_MAX;
	char *out = malloc(64);
	int k;
	for (k = 0; k<8 ; k++) {
		memset(out,0,64);
		int sz = b62_encode( out, (unsigned char *)&big, sizeof(big)-k);
		printf("%d %s\n",sz, out);
	}
	free(out);
	return 0;
}
