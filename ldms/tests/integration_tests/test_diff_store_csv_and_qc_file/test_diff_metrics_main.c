
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "test_diff_metrics.h"
#include "metric_set.h"



/**
 * Examine a sampler csv file to determine if the time between samples is ok.
 * The test passes if all samples were acquired within
 * 90% of the time period that was specified in the ldmsctl command.
 */
int main(int argc, char **argv ) {


	if (argc!=3) {
		printf("%s %s %s %s",
		       "usage:  ",
		       "./test_diff_metrics.exe",
		       "[filename of sampler csv]",
		       "[filename of qc data]"
			);
		exit(0);
	}

	int pass = test_diff_metrics(argv[1], argv[2]);
	printf("%s\n", pass?"pass":"fail");

	return(pass);
}
