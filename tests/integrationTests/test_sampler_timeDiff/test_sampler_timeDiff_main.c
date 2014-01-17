#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "test_sampler_timeDiff.h"



/**
 * Examine a sampler csv file to determine if the time between samples is ok.
 * The test passes if all samples were acquired within
 * 90% of the time period that was specified in the ldmsctl command.
 */
int main(int argc, char **argv ) {


	if (argc!=4) {
		printf("%s %s %s",
		       "usage:  ",
		       "./QC_timeDiffWithin90Percent",
		       "filename [comp id] timePeriodInSec"
			);
		exit(0);
	}

	int pass =
		test_sampler_timeDiff(argv[1], atoi(argv[2]), atof(argv[3]));
	printf("%s\n", pass?"pass":"fail");

	return(pass);
}
