

#ifndef PARSE_QC_FILE_H_
#define PARSE_QC_FILE_H_

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include "test_diff_limits.h"
#include "metric_set.h"




void open_qc_file(char *filename);

void get_metric_set_from_qc_file(struct Time_And_Metrics *data);

void extract_comp_id_from_qc_data_filename();

void close_qc_file();

void save_qc_file_position();

void restore_saved_qc_file_position();

char* get_comp_id_from_qc_file();



#endif /* PARSE_QC_FILE_H_ */
