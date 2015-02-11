
#ifndef METRICS_SET_H_
#define METRICS_SET_H_

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "test_diff_limits.h"

struct Time_And_Metrics {
	double time;                   //date & time of acquisition
	char metrics[BUFSIZE_METRICS]; //string representation of metric set
	char comp_id[BUFSIZE_COMP_ID]; //comp_id
	int eof;                       //at end of the file?
	int err;                       //0 if no errors
	long file_offset;              //file position of metric set
};


int match(struct Time_And_Metrics *data1, struct Time_And_Metrics *data2);


#endif /* METRICS_SET_H_ */
