#include "metric_set.h"


/**
 * Do the metrics in these 2 sets match?
 * @param data1 set of metrics
 * @param data2 set of metrics
 * @return true if the 2 sets match
 */
int match(struct Time_And_Metrics *data1, struct Time_And_Metrics *data2) {
	if (data1->eof != data2->eof) return(0);
	if (strcmp(data1->comp_id, data2->comp_id)!=0) return(0);
	if (strcmp(data1->metrics, data2->metrics)!=0) return(0);
	return(1);
}


