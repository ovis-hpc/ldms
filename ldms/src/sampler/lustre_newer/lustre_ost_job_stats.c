/* -*- c-basic-offset: 8 -*- */
/* Copyright 2021 Lawrence Livermore National Security, LLC
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */
#include "ldms.h"
#include "ldmsd.h"
#include "lustre_ost.h"
#include "lustre_ost_job_stats.h"

/* Defined in lustre_ost.c */
extern ovis_log_t lustre_ost_log;

/* ldms_schema_t is a pointer */
static ldms_schema_t ost_job_stats_schema;

struct ost_job_stats_data {
        char *jobid;
        uint64_t prev_snapshot_time;
        ldms_set_t metric_set; /* a pointer */
        struct rbn job_stats_node;
};

static char *job_stats_uint64_t_entries[] = {
        "snapshot_time",
        "read_bytes_sum",
        "write_bytes_sum",
        "getattr",
        "setattr",
        "punch",
        "sync",
        "destroy",
        "create",
        "statfs",
        "get_info",
        "set_info",
        "quotactl",
        NULL
};

static int string_comparator(void *a, const void *b)
{
        return strcmp((char *)a, (char *)b);
}

int ost_job_stats_schema_is_initialized()
{
        if (ost_job_stats_schema != NULL)
                return 0;
        else
                return -1;
}


int ost_job_stats_schema_init(const char *producer_name)
{
        /* ldms_schema_t is a pointer */
        ldms_schema_t sch;
        int i;
        int rc;

        sch = ldms_schema_new("lustre_ost_job_stats");
        if (sch == NULL)
                goto err1;
        rc = ldms_schema_meta_array_add(sch, "fs_name", LDMS_V_CHAR_ARRAY, 64);
        if (rc < 0)
                goto err2;
        rc = ldms_schema_meta_array_add(sch, "ost", LDMS_V_CHAR_ARRAY, 64);
        if (rc < 0)
                goto err2;
        rc = ldms_schema_meta_array_add(sch, "job_id", LDMS_V_CHAR_ARRAY, 64);
        if (rc < 0)
                goto err2;
        for (i = 0; job_stats_uint64_t_entries[i] != NULL; i++) {
                rc = ldms_schema_metric_add(sch, job_stats_uint64_t_entries[i],
                                            LDMS_V_U64);
                if (rc < 0)
                        goto err2;
        }

        ost_job_stats_schema = sch;

        return 0;
err2:
        ldms_schema_delete(sch);
err1:
        ovis_log(lustre_ost_log, OVIS_LERROR, "lustre_ost_job_stats schema creation failed\n");
        return -1;
}

void ost_job_stats_schema_fini()
{
        ovis_log(lustre_ost_log, OVIS_LDEBUG, "ost_job_stats_schema_fini()\n");
        if (ost_job_stats_schema != NULL) {
                ldms_schema_delete(ost_job_stats_schema);
                ost_job_stats_schema = NULL;
        }
}

static struct ost_job_stats_data *ost_job_stats_data_create(const char *producer_name,
                                                            const char *jobid,
                                                            const char *fs_name,
                                                            const char *ost_name)
{
        struct ost_job_stats_data *job_stats;
        int index;
        char instance_name[256];

        ovis_log(lustre_ost_log, OVIS_LDEBUG, "ost_job_stats_data_create() jobid=%s\n",
               jobid);
        job_stats = calloc(1, sizeof(*job_stats));
        if (job_stats == NULL)
                goto out1;
        job_stats->jobid = strdup(jobid);
        if (job_stats->jobid == NULL)
                goto out2;
        snprintf(instance_name, sizeof(instance_name), "%s/%s/%s",
                 producer_name, ost_name, jobid);
        job_stats->metric_set = ldms_set_new(instance_name,
                                             ost_job_stats_schema);
        if (job_stats->metric_set == NULL)
                goto out3;
        rbn_init(&job_stats->job_stats_node, job_stats->jobid);

        ldms_set_producer_name_set(job_stats->metric_set, producer_name);
        index = ldms_metric_by_name(job_stats->metric_set, "fs_name");
        ldms_metric_array_set_str(job_stats->metric_set, index, fs_name);
        index = ldms_metric_by_name(job_stats->metric_set, "ost");
        ldms_metric_array_set_str(job_stats->metric_set, index, ost_name);
        index = ldms_metric_by_name(job_stats->metric_set, "job_id");
        ldms_metric_array_set_str(job_stats->metric_set, index, jobid);
        ldms_set_publish(job_stats->metric_set);
        ldmsd_set_register(job_stats->metric_set, SAMP);
        return job_stats;
out3:
        free(job_stats->jobid);
out2:
        free(job_stats);
out1:
        ovis_log(lustre_ost_log, OVIS_LERROR, "ost_job_stats_data_create failed\n");
        return NULL;
}

static void ost_job_stats_data_destroy(struct ost_job_stats_data *job_stats)
{
        ovis_log(lustre_ost_log, OVIS_LDEBUG, "ost_job_stats_data_destroy() jobid=%s\n",
               job_stats->jobid);
        ldmsd_set_deregister(ldms_set_instance_name_get(job_stats->metric_set), SAMP);
        ldms_set_unpublish(job_stats->metric_set);
        ldms_set_delete(job_stats->metric_set);
        free(job_stats->jobid);
        free(job_stats);
}

void ost_job_stats_destroy(struct rbt *job_stats_tree)
{
        struct rbn *rbn;
        struct ost_job_stats_data *job_stats;

        while (!rbt_empty(job_stats_tree)) {
                rbn = rbt_min(job_stats_tree);
                job_stats = container_of(rbn, struct ost_job_stats_data,
                                         job_stats_node);
                rbt_del(job_stats_tree, rbn);
                ost_job_stats_data_destroy(job_stats);
        }
}

static void job_stats_sample_stop(struct ost_job_stats_data **job_stats)
{
        if (*job_stats != NULL) {
                if (!ldms_set_is_consistent((*job_stats)->metric_set)) {
                        ldms_transaction_end((*job_stats)->metric_set);
                }
                *job_stats = NULL;
        }
}

/* Read job_stats file, and for each seen jobid:

   1) If we have not seen the jobid before, create the associated
      metric set.
   2) Update the metrics in the metric set.

   We also need to track which jobids are not seen so that we may
   destroy that metric set which is no longer needed.  We assume
   that we can remove the metric set immediately because lustre
   leaves the jobid around for a while.  The amount of time is
   configurable in job_cleanup_interval, and defaults to 600 seconds.
   As long as the job_cleanup_interval in Lustre is sufficently longer
   than the ldms aggregator polling interval, there is no need for
   additional caching in ldms.
*/
void ost_job_stats_sample(const char *producer_name, const char *fs_name,
                          const char *ost_name, const char *job_stats_path,
                          struct rbt *job_stats_tree)
{
        FILE *js; /* job_stats */
        char buf[512];
        struct rbt new_job_stats;
        struct ost_job_stats_data *job_stats = NULL;

        ovis_log(lustre_ost_log, OVIS_LDEBUG, "ost_job_stats_sample() %s\n",
               ost_name);
        js = fopen(job_stats_path, "r");
        if (js == NULL) {
                ovis_log(lustre_ost_log, OVIS_LWARNING, "file %s not found\n",
                       job_stats_path);
                return;
        }

        /* The first line should always be "job_stats:" */
        if (fgets(buf, sizeof(buf), js) == NULL) {
                ovis_log(lustre_ost_log, OVIS_LWARNING, "failed on read from %s\n",
                       job_stats_path);
                goto out1;
        }
        if (strncmp("job_stats:", buf, sizeof("job_stats:")-1) != 0) {
                ovis_log(lustre_ost_log, OVIS_LWARNING, "first line in %s is not \"job_stats:\": %s\n",
                       job_stats_path, buf);
                goto out1;
        }

        rbt_init(&new_job_stats, string_comparator);
        /* start new empty rbt for job stats we see this time */
        while (fgets(buf, sizeof(buf), js)) {
                char str1[64+1];
                uint64_t val1;
                int rc;
                int index;

                rc = sscanf(buf, " - job_id: %64[^\n]", str1);
                if (rc == 1) {
                        struct rbn *rbn;

                        job_stats_sample_stop(&job_stats);
                        rbn = rbt_find(job_stats_tree, str1);
                        if (rbn) {
                                job_stats = container_of(rbn, struct ost_job_stats_data, job_stats_node);
                                rbt_del(job_stats_tree, &job_stats->job_stats_node);
                        } else {
                                job_stats = ost_job_stats_data_create(producer_name, str1, fs_name, ost_name);
                        }
                        if (job_stats != NULL) {
                                rbt_ins(&new_job_stats, &job_stats->job_stats_node);
                        }
                        continue;
                }
                if (job_stats == NULL) {
                        /* We must have failed to find or allocate the job_stats
                           structure, so we have no where to put metrics.  We'll
                           just skip everything until we see the next job_id
                           section in the file. */
                        continue;
                }
                rc = sscanf(buf, " snapshot_time: %lu", &val1);
                if (rc == 1) {
                        /* only update the metric if a change of snapshot time
                           tells us that the data has changed */
                        if (job_stats->prev_snapshot_time == val1)
                                continue;
                        ovis_log(lustre_ost_log, OVIS_LDEBUG, "jobid %s has updated data\n",
                               job_stats->jobid);
                        job_stats->prev_snapshot_time = val1;
                        ldms_transaction_begin(job_stats->metric_set);
                        index = ldms_metric_by_name(job_stats->metric_set, "snapshot_time");
                        if (index == -1) {
                                ovis_log(lustre_ost_log, OVIS_LWARNING, "ost job_stats metric not found: snapshot_time (job id: %s)\n",
                                       str1);
                        } else {
                                ldms_metric_set_u64(job_stats->metric_set, index, val1);
                        }
                        continue;
                }
                if (ldms_set_is_consistent(job_stats->metric_set)) {
                        /* We recognized the jobid, but the data hasn't changed,
                           so just ignore the data lines */
                        continue;
                }
                rc = sscanf(buf, " %64[^:]: { samples: %*u, unit: bytes, min: %*u, max: %*u, sum: %lu }",
                            str1, &val1);
                if (rc == 2) {
                        int base_name_len = strlen(str1);
                        sprintf(str1+base_name_len, "_sum");
                        index = ldms_metric_by_name(job_stats->metric_set, str1);
                        if (index == -1) {
                                ovis_log(lustre_ost_log, OVIS_LWARNING, "ost job_stats metric not found: %s\n",
                                       str1);
                        } else {
                                ldms_metric_set_u64(job_stats->metric_set, index, val1);
                        }
                        continue;
                }
                rc = sscanf(buf, " %64[^:]: { samples: %lu",
                            str1, &val1);
                if (rc == 2) {
                        index = ldms_metric_by_name(job_stats->metric_set, str1);
                        if (index == -1) {
                                ovis_log(lustre_ost_log, OVIS_LWARNING, "ost job_stats metric not found: %s\n",
                                       str1);
                        } else {
                                ldms_metric_set_u64(job_stats->metric_set, index, val1);
                        }
                        continue;
                }
        }
        job_stats_sample_stop(&job_stats);

        /* destroy any remaining job_stats since we didn't see them this
           time (if we had, they would have been moved to new_job_stats) */
        ost_job_stats_destroy(job_stats_tree);

        /* swap new rbt into old rbt's place */
        memcpy(job_stats_tree, &new_job_stats, sizeof(struct rbt));
out1:
        fclose(js);

        return;
}
