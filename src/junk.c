/*
 * This is the junk data provider
 */
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include "ldms.h"
#include "ldmsd.h"

#define HEADER_FILE "/home/gentile/Work/LDMS/sedcutils/opt/cray/sedc/sedcheader";
#define DATA_FILE "/home/gentile/Work/LDMS/sedcutils/opt/cray/sedc/sedcdatafile"

static char *headerfile = HEADER_FILE;
static char *datafile = DATA_FILE;

ldms_set_t set;
FILE *mf;
ldms_metric_t *metric_table;
ldmsd_msg_log_f msglog;
ldms_metric_t compid_metric_handle;
int minindex = 2; //the min index in the header file

static int config(char *str)
{
	if (!set || !compid_metric_handle ) {
		msglog("junk: plugin not initialized\n");
		return EINVAL;
	}

	//expects "component_id value"
	if (0 == strncmp(str, "component_id", 12)) {
		char junk[128];
		int rc;
		union ldms_value v;

		rc = sscanf(str, "component_id %" PRIu64 "%s\n", &v.v_u64, junk);
		if (rc < 1)
			return EINVAL;

		ldms_set_metric(compid_metric_handle, &v);
	}

	return 0;
}

static ldms_set_t get_set()
{
	return set;
}

static int init(const char *path)
{
	size_t meta_sz, tot_meta_sz;
	size_t data_sz, tot_data_sz;
	int rc, i, metric_count;
	uint64_t metric_value;
	char *s;
	char lbuf[256];
	char metric_name[128];
	char junk[128];

	mf = fopen(headerfile, "r");
	if (!mf) {
		msglog("Could not open the junk file '%s'...exiting\n", headerfile);
		return ENOENT;
	}

	/*
	 * Process the header file to determine the metric set size.
	 */

	rc = ldms_get_metric_size("component_id", LDMS_V_U64, &tot_meta_sz, &tot_data_sz);
	metric_count = 0;
	fseek(mf, 0, SEEK_SET);
	if (fgets(lbuf, sizeof(lbuf), mf) != NULL){
	  int count = 0;
	  char *pch = strtok(lbuf, " \n");
	  while (pch != NULL){
	    if (count >= minindex){
	      rc = ldms_get_metric_size(pch, LDMS_V_U64, &meta_sz, &data_sz);
	      if (rc)
		return rc;
	      
	      tot_meta_sz += meta_sz;
	      tot_data_sz += data_sz;
	      metric_count++;
	    }
	    count++;
	  }
	} 

	/* Create the metric set */
	rc = ldms_create_set(path, tot_meta_sz, tot_data_sz, &set);
	if (rc)
		return rc;

	metric_table = calloc(metric_count, sizeof(ldms_metric_t));
	if (!metric_table)
		goto err;
	/*
	 * Process the file again to define all the metrics.
	 */

	compid_metric_handle = ldms_add_metric(set, "component_id", LDMS_V_U64);
	if (!compid_metric_handle) {
		rc = ENOMEM;
		goto err;
	} //compid set in config

	int metric_no = 0;
	fseek(mf, 0, SEEK_SET);
	if (fgets(lbuf, sizeof(lbuf), mf) != NULL){
	  int count = 0;
	  char *pch = strtok(lbuf, " \n");
	  while (pch != NULL){
	    if (count >= minindex){
	      metric_table[metric_no] = ldms_add_metric(set, pch, LDMS_V_U64);
	      if (!metric_table[metric_no]) {
		rc = ENOMEM;
		goto err;
	      }
	      metric_count++;
	    }
	    count++;
	  }
	} 

	return 0;

 err:
	ldms_set_release(set);
	return rc;
}

static int sample(void)
{

  return 0;

	int rc;
	int metric_no;
	char *s;
	char lbuf[256];
	char metric_name[128];
	char junk[128];
	union ldms_value v;

	metric_no = 0;
	fseek(mf, 0, SEEK_SET);
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;
		rc = sscanf(lbuf, "%s %"PRIu64 " %s\n", metric_name, &v.v_u64, junk);
		if (rc != 2 && rc != 3)
			return EINVAL;

		ldms_set_metric(metric_table[metric_no], &v);
		metric_no++;
	} while (s);
 	return 0;
}

static void term(void)
{
	ldms_destroy_set(set);
}

static const char *usage(void)
{
	return  "    config junk component_id <comp_id>\n"
		"        - Set the component_id value in the metric set.\n"
		"        comp_id     The component id value\n";
}

static struct ldms_plugin junk_plugin = {
	.name = "junk",
	.init = init,
	.term = term,
	.config = config,
	.get_set = get_set,
	.sample = sample,
	.usage = usage,
};

struct ldms_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	msglog = pf;
	return &junk_plugin;
}
