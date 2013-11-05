#include <stdio.h>
#include <stdint.h>
#include "baler/bset.h"

#define N 10000

int num[] = {
	[10] = 1,
	[20] = 1,
	[30] = 1,
	[9999] = 1,
	[N] = 0
};

int main(int argc, char **argv)
{
	struct bset_u32 *set = bset_u32_alloc(0);
	int i;
	for (i=0; i<N; i++) {
		if (num[i])
			bset_u32_insert(set, i);
	}
	int rc = 0;
	for (i=0; i<N; i++) {
		int exists = bset_u32_exist(set, i);
		if (num[i] && !exists) {
			printf("ERROR: %d does not exists!!!\n", i);
			rc = -1;
		}
		if (!num[i] && exists) {
			printf("ERROR: %d exists :(\n", i);
			rc = -1;
		}
	}
	if (!rc)
		printf("bset test OK\n");
	return rc;
}
