#include "test_sampler_timeDiff.h"


/**
 * Examine a sampler csv file to determine if the time between samples is ok.
 * The test passes if all samples were acquired within
 * 90% of the time period that was specified in the ldmsctl command.
 * @param filename The name of the csv sampler file
 * @param comp id specified in ldmsctl command
 * @param timePeriod time period specified in ldmsctl command
 * @return true if all samples were acquired with 90% of timePeriod
 */
int test_sampler_timeDiff(char *filename, int compId, double timePeriod) {

	/* create bin counters: */
	/*    time difference is within 1%  */
	/*    time difference is within 10% */
	/*    time difference is within 60% */
	/*    time difference is within 90% */
	/*    time difference is over 90%   */
        struct Counter {
		char label[32];
		int within;  //true or false
		double thresholdLeft;
		double thresholdRight;
		unsigned long long counter;
	};

        struct Counter counters[5];
        const int numberOfCounters = 5;

        strcpy(counters[0].label, "within 1% of time period");
        counters[0].within = 1;
        counters[0].thresholdLeft = timePeriod - 0.01 * timePeriod;
        counters[0].thresholdRight = timePeriod + 0.01 * timePeriod;
        counters[0].counter = 0;

        strcpy(counters[1].label, "within 10% of time period");
        counters[1].within = 1;
        counters[1].thresholdLeft = timePeriod - 0.10 * timePeriod;
        counters[1].thresholdRight = timePeriod + 0.10 * timePeriod;
        counters[1].counter = 0;

        strcpy(counters[2].label, "within 60% of time period");
        counters[2].within = 1;
        counters[2].thresholdLeft = timePeriod - 0.60 * timePeriod;
        counters[2].thresholdRight = timePeriod + 0.60 * timePeriod;
        counters[2].counter = 0;

        strcpy(counters[3].label, "within 90% of time period");
        counters[3].within = 1;
        counters[3].thresholdLeft = timePeriod - 0.90 * timePeriod;
        counters[3].thresholdRight = timePeriod + 0.90 * timePeriod;
        counters[3].counter = 0;

        strcpy(counters[4].label, "outside 90% of time period");
        counters[4].within = 0;
        counters[4].thresholdLeft = timePeriod - 0.90 * timePeriod;
        counters[4].thresholdRight = timePeriod + 0.90 * timePeriod;
        counters[4].counter = 0;

	/* open the file */
	FILE *fp = fopen(filename,"r");


	/* used to verify that every data point increments one bin counter */
	unsigned long long lineNumber = -1;
	/* the timestamp in the current iteration */
	double timestamp = 0.0;
	/* the timestamp in the previous iteration */
	double previousTimestamp = 0.0;
	/* line of text in the collector file */
	char oneLine[1024];

	/* skip over the first line.  This line contains column headers */
	fgets(oneLine,1024,fp);


	while(1) {

		/* save the old timestamp     */
		/* increment the line counter */
		previousTimestamp = timestamp;
		lineNumber++;

		/* read one line from the file */
		if (fgets(oneLine,1024,fp) == NULL) break;

		/* skip over lines from the wrong comp id */
		char *ptr = strchr(oneLine,',');
		int compIdInOneLine = atoi(ptr+1);
		if (compIdInOneLine != compId) continue;

		/* get the timestamp that is in the first column */
		timestamp = atof(oneLine);

		/* skip over the 2nd line */
		if (lineNumber==1) continue;

		/* how much has elapsed since the last data acquisition? */
		double timeDifference = timestamp - previousTimestamp;

		/* increment one of the bin counters */
		int binCounterIncremented = 0;
		int i;
		for (i=0; i<numberOfCounters; i++) {
			if (counters[i].within) {
				if (timeDifference<counters[i].thresholdLeft) {
					continue;
				}
				if (timeDifference>counters[i].thresholdRight) {
					continue;
				}
				counters[i].counter++;
				//printf("%lf %s\n",timeDifference,counters[i].label);
				binCounterIncremented=1;
				break;
			} else {
				if (timeDifference<counters[i].thresholdLeft) {
					counters[i].counter++;
					binCounterIncremented=1;
					//printf("%lf %s\n",timeDifference,counters[i].label);
					break;
				}
				if (timeDifference>counters[i].thresholdRight) {
					counters[i].counter++;
					binCounterIncremented=1;
					//printf("%lf %s\n",timeDifference,counters[i].label);
					break;
				}
				continue;
			}
			printf("FOUND A BUG\n");
		}
		if (binCounterIncremented==0) printf("FOUND A BUG\n");

	}//while

	/* output bin counters */
	//int i;
	//for (i=0; i<numberOfCounters; i++) {
	//	printf("%s is %llu\n",counters[i].label, counters[i].counter);
	//}

	/* close the file */
	fclose(fp);

	/* if there are no counts in the "over 90%" bin, then the test passes */
	return(counters[numberOfCounters-1].counter==0?1:0);
}



