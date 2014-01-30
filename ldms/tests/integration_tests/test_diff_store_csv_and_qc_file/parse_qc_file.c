#include "parse_qc_file.h"

FILE *file_qc = NULL;
char filename_qc[BUFSIZE_FILENAME];
char comp_id[BUFSIZE_COMP_ID];
long saved_file_position = 0;

/**
 * open the qc data file
 * @param filename name of the qc data file
 */
void open_qc_file(char *filename) {
	file_qc = fopen(filename, "r");
	assert(file_qc != NULL);
	assert(strlen(filename) < BUFSIZE_FILENAME);
	strcpy(filename_qc, filename);
	extract_comp_id_from_qc_data_filename();
}

/**
 * close the qc data file
 */
void close_qc_file() {
	fclose(file_qc);
}


void save_qc_file_position() {
	saved_file_position = ftell(file_qc);
}

void restore_saved_qc_file_position() {
	fseek(file_qc, saved_file_position, SEEK_SET);
}

char *get_comp_id_from_qc_file() {
	return(comp_id);
}

/**
 * Retrieve one metric set from the qc data file.
 * @param Return to the caller, one metric set.  Also return the
 * timestamp when the metric set was acquired.
 */
void get_metric_set_from_qc_file(struct Time_And_Metrics *data) {

	strcpy(data->comp_id, comp_id);
	data->time = 0;
	strcpy(data->metrics,"");
	data->eof = 0;
	data->err = 0;
	data->file_offset = ftell(file_qc);

	/* create a temporary buffer so that we can reverse the order of the metrics */
	const int BUFSIZE = BUFSIZE_METRICS;
        char buf[BUFSIZE];
	char *ptr_buf = buf + BUFSIZE - 1; //pt to end of metrics line

        /* we are going to read the file, line by line */
	char line[BUFSIZE_LINE];
	char *ptr;

	/* count lines */
	int line_number = 0;

	/* terminate buf */
	/* we are going to populate buf from the end and move toward the front */
	*ptr_buf = '\0';


	/* for each line the sampler csv file */
	while (1) {

		/* save the file position */
		long file_position = ftell(file_qc);

		/* read one line from the file */
		if (fgets(line,BUFSIZE_LINE,file_qc)==NULL) {
			data->eof = 1;
			data->err = 1;
			break;
		}
		assert(strlen(line)<BUFSIZE_LINE-1);
		line[BUFSIZE_LINE-1] = '\0';


		/* skip lines until we find #time */
		if ((line_number==0) && (strncmp(line, "#time", 5)!=0)) {
			continue;
		}
		data->file_offset = ftell(file_qc);


                /* stop reading lines when we find another #time line */
	        if ((strncmp(line,"#time",5)==0) && (line_number!=0)) {
	        	fseek(file_qc, file_position, SEEK_SET);
	        	break;
	        }

	        /* count the number of line */
	        line_number++;

	        /* get the contents of the last column */
	        ptr = line + strlen(line) - 1;
	        while (isspace(*ptr))
	        	*ptr-- = '\0';
	        while (*ptr!=',')
	        	ptr--;
	        ptr++;
	        while (isspace(*ptr))
	        	ptr++;

	        /* The first number is the timestamp */
	        if (line_number==1) {
	        	sscanf(ptr,"%lf",&data->time);
	        /* The remaining numbers are metrics */
	        } else {

	        	/* prepend the metric onto buf */
	        	ptr_buf = ptr_buf - strlen(ptr);
	        	assert(ptr_buf > buf);
	        	memcpy(ptr_buf, ptr, strlen(ptr));

	        	/* prepend ", " onto buf */
	        	ptr_buf = ptr_buf - 2;
			assert(ptr_buf > buf);
			memcpy(ptr_buf, ", ", 2);

			/* prepend the comp-id onto buf */
			ptr_buf = ptr_buf - strlen(comp_id);
			assert(ptr_buf > buf);
			memcpy(ptr_buf, comp_id, strlen(comp_id));

			/* prepend ", " onto buf */
			ptr_buf = ptr_buf - 2;
			assert(ptr_buf > buf);
			memcpy(ptr_buf, ", ", 2);
	        }

	}//while

	/* the beginning of buf has ", "; remove it */
	if (*ptr_buf==',')
		ptr_buf = ptr_buf+2;

	/* copy buf to metrics */
	strcpy(data->metrics,ptr_buf);
}


/**
 * Extract the comp_id from the name of the QC data file.
 * The QC data file has this format:
 * QC_[hostname]_[comp id]_[name of sampler]_[6 random chars].txt
 */
void extract_comp_id_from_qc_data_filename() {

	/* start at the end of the filename                        */
	/* jump past the filename extension and the 6 random chars */
	char *ptr = filename_qc + strlen(filename_qc) - 12;
	char *ptrEnd;

	/* find the _ that is to right of comp_id */
	while (*ptr != '_')
		ptr--;
	ptrEnd = ptr;

	/* find the _ that is to the left of comp_id */
	ptr--;
	while (*ptr != '_')
		ptr--;

	/* everything between these 2 _ is the comp_id */
	ptr++;
	strncpy(comp_id, ptr, ptrEnd-ptr);
	comp_id[ptrEnd-ptr] = '\0';
}


