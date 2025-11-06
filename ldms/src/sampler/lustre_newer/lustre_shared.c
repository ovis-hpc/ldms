/* -*- c-basic-offset: 8 -*- */
/* Copyright 2025 Lawrence Livermore National Security, LLC
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/types.h>
#include <limits.h>
#include <linux/limits.h>
#include <dirent.h>
#include <stdint.h>
#include <inttypes.h>
#include "lustre_shared.h"
#include "jobid_helper.h"

/*
 * Find the osd directory that should contain simple stats files such
 * as "kbytesfree".
 *
 * Returns strdup'ed string or NULL.  Caller must free.
 */
char *lustre_osd_dir_find(const char * const *paths, const char *component_name, ovis_log_t log)
{
	char *path;
	char *osd_dir = NULL;
	int i;

	for (i = 0, path = (char *)paths[0]; path != NULL; i++, path = (char *)paths[i]) {
		struct dirent *dirent;
		DIR *dir;

		dir = opendir(path);
		if (dir == NULL) {
			ovis_log(log, OVIS_LDEBUG,
				 "osd for %s, base dir %s does not exist\n",
				 component_name, path);
			continue;
		}

		while ((dirent = readdir(dir)) != NULL) {
			if (dirent->d_type == DT_DIR &&
			    strncmp(dirent->d_name, "osd-", strlen("osd-")) == 0) {
				char tmp_path[PATH_MAX];
				snprintf(tmp_path, PATH_MAX, "%s/%s/%s",
					 path, dirent->d_name, component_name);
				if (access(tmp_path, F_OK) == 0) {
					osd_dir = strdup(tmp_path);
					break;
				}
			}
		}

		closedir(dir);

		if (osd_dir != NULL) {
			ovis_log(log, OVIS_LDEBUG,
				 "osd for %s found at path %s\n",
				 component_name, osd_dir);
			break;
		} else {
			ovis_log(log, OVIS_LDEBUG,
				 "osd for %s not found in base path %s\n",
				 component_name, path);
		}

	}

	return osd_dir;
}

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

uint64_t lustre_file_read_uint64_t(const char *dir, const char *file, ovis_log_t log)
{
        uint64_t val;
        char filepath[PATH_MAX];
        char valbuf[64];
        FILE *fp;

        snprintf(filepath, PATH_MAX, "%s/%s", dir, file);
        fp = fopen(filepath, "r");
        if (fp == NULL) {
                ovis_log(log, OVIS_LWARNING, "unable to open %s\n", filepath);
                return 0;
        }
        if (fgets(valbuf, sizeof(valbuf), fp) == NULL) {
                ovis_log(log, OVIS_LWARNING, "unable to read %s\n", filepath);
                fclose(fp);
                return 0;
        }
        fclose(fp);

        /* turn string into int */
        sscanf(valbuf, "%lu", &val);

        return val;
}

void sanitize_job_id_str(char name[])
{
	char *src = name;
	char *dest = name;
	bool quoted = false;

	if (*src == '"') {
		/* skip the first double-quote */
		quoted = true;
		src++;
	}
	while (*src != '\0') {
		if (quoted && *(src+1) == '\0' && *src == '"') {
			/* skip the final _matching_ double-quote */
			src++;
			continue;
		}
		if (quoted && *src == '\\') {
		        /* might be the start of a hex representation of a
			   character, for instance a " might be: \x22 */
			char c;
			int num;
			int rc;
			rc = sscanf(src, "\\x%2hhx%n", &c, &num);
			if (rc == 1) {
				/* convert hex representation back to a character */
				src += num-1;
				*src = c;
			}
		}
		if (ldms_set_instance_name_char_allowed(*src)) {
			*dest = *src;
		} else {
			*dest = '_';
		}
		src++;
		dest++;
	}
	*dest = '\0';

	return;
}
