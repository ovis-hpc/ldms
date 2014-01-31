/*
 * parse_store_csv_file.h
 *
 *  Created on: Jan 30, 2014
 *      Author: ejwalsh
 */

#ifndef PARSE_STORE_CSV_FILE_H_
#define PARSE_STORE_CSV_FILE_H_


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include "test_diff_limits.h"
#include "metric_set.h"




void open_store_csv_file(char *filename);

void get_metric_set_from_store_csv_file(struct Time_And_Metrics *data);

void close_store_csv_file();


#endif /* PARSE_STORE_CSV_FILE_H_ */
