/* -*- c-basic-offset: 8 -*- */
/* Copyright 2025 Lawrence Livermore National Security, LLC
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */

#include <stdint.h>
#include <inttypes.h>
#include "lustre_shared.h"
#include "jobid_helper.h"

int lustre_stats_file_sample(const char *stats_path,
			     ldms_set_t metric_set,
			     ovis_log_t log)
{
        FILE *sf;
        char buf[512];
	int rc = 0;

        sf = fopen(stats_path, "r");
        if (sf == NULL) {
                ovis_log(log, OVIS_LWARNING, ": file %s not found\n",
                       stats_path);
                return ENOENT;
        }

        /* The first line should always be "snapshot_time"
           we will ignore it because it always contains the time that we read
           from the file, not any information about when the stats last
           changed */
        if (fgets(buf, sizeof(buf), sf) == NULL) {
                ovis_log(log, OVIS_LWARNING, ": failed on read from %s\n",
                       stats_path);
		rc = ENOMSG;
                goto out1;
        }
        if (strncmp("snapshot_time", buf, sizeof("snapshot_time")-1) != 0) {
                ovis_log(log, OVIS_LWARNING, ": first line in %s is not \"snapshot_time\": %s\n",
                       stats_path, buf);
		rc = ENOMSG;
                goto out1;
        }

        while (fgets(buf, sizeof(buf), sf)) {
                char field_name[MAXNAMESIZE+1];
                uint64_t samples, sum;
                int num_matches;
                int index;

                num_matches = sscanf(buf,
                                     "%64s %"SCNu64" samples [%*[^]]] %*u %*u %"SCNu64" %*u",
                                     field_name, &samples, &sum);
                if (num_matches >= 2) {
                        /* we know at least "samples" is available */
                        index = ldms_metric_by_name(metric_set, field_name);
                        if (index != -1) {
                                ldms_metric_set_u64(metric_set, index, samples);
                        }
                }
                if (num_matches >= 3) {
                        /* we know that "sum" is also avaible */
                        int base_name_len = strlen(field_name);
                        sprintf(field_name+base_name_len, ".sum"); /* append ".sum" */
                        index = ldms_metric_by_name(metric_set, field_name);
                        if (index != -1) {
                                ldms_metric_set_u64(metric_set, index, sum);
                        }
                }
        }
out1:
        fclose(sf);

        return rc;
}
