#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "test_qc_time_difference.h"



/**
 * Examine a sampler csv file to determine if the time between samples is ok.
 * The test passes if all samples were acquired within
 * 90% of the time period that was specified in the ldmsctl command.
 */
int main(int argc, char **argv ) {


	if (argc!=3) {
		printf("%s %s %s",
		       "usage:  ",
		       "./test_time_difference_main.exe",
		       "filename [time period in sec]\n"
			);
		exit(0);
	}

	int pass = test_qc_time_difference(argv[1], atoi(argv[2]));
	printf("%s\n", pass?"pass":"fail");

	return(pass);
}
