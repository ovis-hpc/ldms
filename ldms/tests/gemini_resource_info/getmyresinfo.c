#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <gni_pub.h>


int main() {
	gni_dev_res_desc_t info;
	gni_return_t rc;

	rc = GNI_GetDevResInfo(0, GNI_DEV_RES_FMA, &info);
	if (rc) {
		printf("Error: GNI_GetDevResInfo, rc = %d\n", rc);
	} else {
		printf("Available = %llu, Reserved = %llu, Held = %llu, Total = %llu\n",
		       (unsigned long long) info.available, (unsigned long long) info.reserved,
		       (unsigned long long) info.held, (unsigned long long) info.total);
	}

	return 0;

}
