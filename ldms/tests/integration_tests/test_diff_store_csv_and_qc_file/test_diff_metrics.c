#include "test_diff_metrics.h"




/**
 * Determine if the values in a store csv file
 * matches values found in a QC data file.
 * <pre>
 * ALGORITHM:
 * A comp_id is chosen
 * For each metric set, in the store csv file, with the chosen comp_id
 *     search the QC data file for a matching metric set
 *     if no match is found, return false.
 * return true
 * </pre>
 * @param filename_store_csv The name of the store csv file.  The store csv
 * file contains metric sets from one or more different comp_ids.
 * @param filename_qc The name of the QC data file.  The QC data file
 * contains metric sets from one, and only one, comp_id.
 * @param time_period time period specified in ldmsctl command
 * @return 1 if 100% of the values, that are from the same comp_id as the
 * QC data file, matches with the values in the QC data file.
 */
int test_diff_metrics(
	char *filename_store_csv,
	char *filename_qc,
	double time_period) {


	struct Time_And_Metrics data_store_csv;
	struct Time_And_Metrics data_qc;
	unsigned long long number_of_matching_lines = 0;
	unsigned long long number_of_unmatched_lines = 0;
	char comp_id[BUFSIZE_COMP_ID];

	/* open the sample csv file and the qc data file */
	open_store_csv_file(filename_store_csv);
	open_qc_file(filename_qc);
	strcpy(comp_id, get_comp_id_from_qc_file());
	save_qc_file_position();

	while (1) {

		/* get next metric set from store csv file */
		get_metric_set_from_store_csv_file(&data_store_csv);
		if (data_store_csv.eof==1)
			break;

		/* if this is the wrong comp_id, then skip over this line */
		if (strcmp(data_store_csv.comp_id,comp_id)!=0)
			continue;

		/* find matching qc metric set */
		if (find_matching_qc_data(&data_store_csv, &data_qc)==NULL) {
			restore_saved_qc_file_position();
			if (data_qc.eof==0) {
				number_of_unmatched_lines++;
#ifdef DEBUG
				printf("NO MATCH: time=%lf comp_id=%s\n",
						data_store_csv.time,
						data_store_csv.comp_id);
#endif
			}

			continue;
		}
#ifdef DEBUG
		printf("MATCH  : time=%lf comp_id=%s\n",
			data_store_csv.time, data_store_csv.comp_id);
#endif
		number_of_matching_lines++;
		save_qc_file_position();
	} /*while*/


	/* close the sample csv file and the qc data file */
	close_qc_file();
	close_store_csv_file();

#ifdef DEBUG
	printf("number of matching lines is %llu\n",number_of_matching_lines);
	printf("number of unmatched lines is %llu\n",number_of_unmatched_lines);
#endif

	/* the test passes if there are no unmatched lines */
	if (number_of_unmatched_lines==0) return(1);
	return(0);
}


/**
 * Searches qc data file for a match with a metric from a store csv file.
 * @param data_store_csv the metric set we are are searching for
 * @param data_qc the matching qc metric set is passed back to the caller.
 * If no match is found, then data_qc->err is set to 1.
 * @return matching qc metric set.  If no match is found, then NULL is returned.
 */
struct Time_And_Metrics *find_matching_qc_data
	(struct Time_And_Metrics *data_store_csv,
	 struct Time_And_Metrics *data_qc) {


	while(1) {

		/* read the next qc record */
		get_metric_set_from_qc_file(data_qc);

		/* if we hit EOF, then no match */
		if (data_qc->eof) {
			data_qc->err = 1;
			return(NULL);
		}

		/* if the qc time > store csv time, then no match */
		if (data_qc->time > data_store_csv->time) {
			data_qc->err = 1;
			return(NULL);
		}

		/* if the comp_id don't match, then no match */
		if (strcmp(data_qc->comp_id, data_store_csv->comp_id)!=0) {
			data_qc->err = 1;
			return(NULL);
		}

		/* if the metrics match, then success */
		if (strcmp(data_qc->metrics, data_store_csv->metrics)==0)
			return(data_qc);

	}
}




