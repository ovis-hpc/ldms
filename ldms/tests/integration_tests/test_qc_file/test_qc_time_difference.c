#include "test_qc_time_difference.h"



/**
 * Examine a sampler csv file to determine if the time between samples is ok.
 * The test passes if all samples were acquired within
 * 90% of the time period that was specified in the ldmsctl command.
 * @param filename The name of the csv sampler file
 * @param comp id specified in ldmsctl command
 * @param time_period time period specified in ldmsctl command
 * @return true if all samples were acquired with 90% of time_period
 */
int test_qc_time_difference(char *filename, double time_period) {

	/* create bin counters: */
	/*    time difference is within 1%  */
	/*    time difference is within 10% */
	/*    time difference is within 60% */
	/*    time difference is within 90% */
	/*    time difference is over 90%   */
        struct Counter {
		char label[32];
		int within;  //true or false
		double threshold_left;
		double threshold_right;
		unsigned long long counter;
	};
        struct Counter counters[5];
        const int number_of_counters = 5;

        /* one line from the file */
        const int BUFSIZE = 4096;
        struct File {
        	unsigned long long line_number;
        	char one_line[BUFSIZE];
        	int comp_id;
        	double timestamp;
        	double previous_timestamp;
                double time_difference;
        };
        struct File file;

#ifdef DEBUG
        /* has a counter has been incremented */
        int bin_counter_incremented;
#endif
        int i;



	/* open the file */
	FILE *fp = fopen(filename,"r");

        strcpy(counters[0].label, "within 1% of time period");
        counters[0].within = 1;
        counters[0].threshold_left = time_period - 0.01 * time_period;
        counters[0].threshold_right = time_period + 0.01 * time_period;
        counters[0].counter = 0;

        strcpy(counters[1].label, "within 10% of time period");
        counters[1].within = 1;
        counters[1].threshold_left = time_period - 0.10 * time_period;
        counters[1].threshold_right = time_period + 0.10 * time_period;
        counters[1].counter = 0;

        strcpy(counters[2].label, "within 60% of time period");
        counters[2].within = 1;
        counters[2].threshold_left = time_period - 0.60 * time_period;
        counters[2].threshold_right = time_period + 0.60 * time_period;
        counters[2].counter = 0;

        strcpy(counters[3].label, "within 90% of time period");
        counters[3].within = 1;
        counters[3].threshold_left = time_period - 0.90 * time_period;
        counters[3].threshold_right = time_period + 0.90 * time_period;
        counters[3].counter = 0;

        strcpy(counters[4].label, "outside 90% of time period");
        counters[4].within = 0;
        counters[4].threshold_left = time_period - 0.90 * time_period;
        counters[4].threshold_right = time_period + 0.90 * time_period;
        counters[4].counter = 0;

        file.comp_id = -1;
        file.line_number = 0;
        strcpy(file.one_line,"");
        file.previous_timestamp = 0.0;
        file.time_difference = 0.0;
        file.timestamp = 0.0;

	while(1) {

		/* save the old timestamp     */
		/* increment the line counter */
		file.previous_timestamp = file.timestamp;

		/* read one line from the file */
		if (fgets(file.one_line,BUFSIZE,fp) == NULL) break;
                file.one_line[BUFSIZE-1] = '\0';
                assert(strlen(file.one_line)<BUFSIZE-1);

		/* skip over lines that don't start with #time */
                if (strncmp(file.one_line, "#time", 5)!=0)
                	continue;

		/* get the timestamp that is in the first column */
		file.timestamp = atof(file.one_line+7);

		/* increment the line counter */
		file.line_number++;

		/* skip over the first line of data */
		if (file.line_number==1) continue;

		/* how much has elapsed since the last data acquisition? */
		file.time_difference = file.timestamp - file.previous_timestamp;

		/* increment one of the bin counters */
#ifdef DEBUG
		bin_counter_incremented = 0;
#endif
		for (i=0; i<number_of_counters; i++) {
			if (counters[i].within) {
				if (file.time_difference <
						counters[i].threshold_left) {
					continue;
				}
				if (file.time_difference >
					counters[i].threshold_right) {
					continue;
				}
				counters[i].counter++;
				#ifdef DEBUG
					printf("%lf, %lf, %s\n",
						file.timestamp,
						file.time_difference,
						counters[i].label);
					bin_counter_incremented=1;
				#endif
				break;
			} else {
				if (file.time_difference
						<= counters[i].threshold_left) {
					counters[i].counter++;
					#ifdef DEBUG
						bin_counter_incremented=1;
						printf("%lf, %lf, %s\n",
							file.timestamp,
							file.time_difference,
							counters[i].label);
					#endif
					break;
				}
				if (file.time_difference >=
						counters[i].threshold_right) {
					counters[i].counter++;
					#ifdef DEBUG
						bin_counter_incremented=1;
						printf("%lf, %lf, %s\n",
							file.timestamp,
							file.time_difference,
							counters[i].label);
					#endif
					break;
				}
				continue;
			}
			printf("FOUND A BUG\n");
		}
#ifdef DEBUG
		if (bin_counter_incremented==0)
			printf("FOUND A BUG\n");
#endif

	}//while

#ifdef DEBUG
	/* output bin counters */
	for (i=0; i<number_of_counters; i++) {
		printf("%s is %llu\n",
			counters[i].label,
			counters[i].counter);
	}
#endif

	/* close the file */
	fclose(fp);

	/* if there are no counts in the "over 90%" bin, then the test passes */
	return(counters[number_of_counters-1].counter==0?1:0);
}



