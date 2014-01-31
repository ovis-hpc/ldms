#include "parse_store_csv_file.h"

FILE *file_store_csv = NULL;
char filename_store_csv[BUFSIZE_FILENAME];


/**
 * open the store csv file and toss out the header line
 * @param filename name of the store csv file
 */
void open_store_csv_file(char *filename) {

	char line[BUFSIZE_LINE];

	file_store_csv = fopen(filename, "r");
	assert(file_store_csv != NULL);
	assert(strlen(filename) < BUFSIZE_FILENAME);
	strcpy(filename_store_csv, filename);

	/* skip past the header line */
	assert(fgets(line,BUFSIZE_LINE,file_store_csv)!=NULL);
	assert(strlen(line)<BUFSIZE_LINE-1);
}


/**
 * close the store csv file
 */
void close_store_csv_file() {
	fclose(file_store_csv);
}

/**
 * Retrieve one metric set from the store csv file.
 * @param Return to the caller, one metric set.  Also return the
 * timestamp when the metric set was acquired.
 */
void get_metric_set_from_store_csv_file(struct Time_And_Metrics *data) {

        /* we are going to read one line from the file */
	char line[BUFSIZE_LINE];
	char *ptr;
	char *ptr_metrics;

	strcpy(data->comp_id,"");
	data->time = 0;
	strcpy(data->metrics,"");
	data->eof = 0;
	data->err = 0;
	data->file_offset = ftell(file_store_csv);

	/* read one line from the file */
	if (fgets(line,BUFSIZE_LINE,file_store_csv)==NULL) {
		data->eof = 1;
		data->err = 1;
		return;
	}
	assert(strlen(line)<BUFSIZE_LINE-1);
	line[BUFSIZE_LINE-1] = '\0';

	/* remove trailing whitespace */
	ptr = line + strlen(line) - 1;
	while (isspace(*ptr))
		*ptr-- = '\0';

	/* The first column contains the timestamp */
	ptr = line;
	while (!isspace(*ptr) && *ptr!=',')
		ptr++;
	*ptr = '\0';
	assert(strlen(line) < BUFSIZE_TIME);
	sscanf(line,"%lf",&data->time);
	ptr++;

	/* The remainder of the line contains the metric set */
	while (isspace(*ptr))
		ptr++;
	assert(strlen(ptr) < BUFSIZE_METRICS);
	strcpy(data->metrics, ptr);
	ptr_metrics = ptr;


	/* The next column contains the comp_id */
	while (!isspace(*ptr) && *ptr!=',')
		ptr++;
	assert(ptr-ptr_metrics+1 < BUFSIZE_COMP_ID);
	memcpy(data->comp_id, ptr_metrics, ptr-ptr_metrics);
	*(data->comp_id + (ptr - ptr_metrics)) = '\0';
}



