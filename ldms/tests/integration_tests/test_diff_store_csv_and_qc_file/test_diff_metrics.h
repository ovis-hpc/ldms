#ifndef TEST_DIFF_METRIC_H_
#define TEST_DIFF_METRIC_H_

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "test_diff_limits.h"
#include "parse_qc_file.h"
#include "parse_store_csv_file.h"
#include "metric_set.h"

struct QC_Time {
	int at_EOF;
	double time;
};


int test_diff_metrics(
	char *filename_store_csv,
	char *filename_qc);

struct Time_And_Metrics *find_matching_qc_data
	(struct Time_And_Metrics *data_store_csv,
	 struct Time_And_Metrics *data_qc);

#endif /*TEST_DIFF_METRIC_H_*/
