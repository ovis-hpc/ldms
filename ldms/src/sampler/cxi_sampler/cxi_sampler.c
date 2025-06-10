/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2025 Lawrence Berkley National Lab, All rights reserved.
 * Copyright (c) 2025 Open Grid Computing, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the BSD-type
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *      Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *      Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 *      Neither the name of Sandia nor the names of any contributors may
 *      be used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 *      Neither the name of Open Grid Computing nor the names of any
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *      Modified source versions must be plainly marked as such, and
 *      must not be misrepresented as being the original software.
 *
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define _GNU_SOURCE

#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>
#include <regex.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <pthread.h>
#include <dirent.h>
#include <fnmatch.h>

#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_plug_api.h"
#include "sampler_base.h"

#define MAX_CXI_IFACES 256
#define MAX_FILES 2000
#define MAX_FILENAME_LENGTH 256
#define TEL_PATH_CXI "/sys/class/cxi"
#define RH_PATH_CXI "/var/run/cxi"

struct files_s {
	int count;
        /* The +1 is to leave room for the extra derived:link_restarts metric */
	int midx[MAX_FILES+1];
	char *names[MAX_FILES+1];
};

#define LINK_RESTARTS_TIMESTAMPS 10
struct link_restarts {
        char *path_template;
        uint64_t timestamps[LINK_RESTARTS_TIMESTAMPS];
        uint64_t value;
};

typedef struct cxi_s {
	struct files_s rh_files;
	struct files_s tel_files;
	int iface_count;
	char **iface_names;
	char *tel_path;
	char *rh_path;
        struct link_restarts *restarts; /* array of length iface_count */
        int restarts_mid;

	ovis_log_t log;
	ldms_schema_t schema;
	int iface_mid;
	base_data_t base;
	int metric_offset;
	ldms_set_t set;
	int rh_list_mid;
	int tel_list_mid;
	ldms_mval_t rh_list_mval;
	ldms_mval_t tel_list_mval;
} *cxi_t;

/* Skip special files and directories */
static int skip_entry(DIR *dir, struct dirent *entry)
{
        if (entry->d_type == DT_UNKNOWN) {
                struct stat statbuf;
                if (fstatat(dirfd(dir), entry->d_name, &statbuf, 0) == 0) {
                        if (!S_ISREG(statbuf.st_mode)) {
                                return 1;
                        }
                }
        } else if (entry->d_type != DT_REG ) {
                return 1;
        }
        if (strcmp(entry->d_name, "ALL-in-binary") == 0 ||
	    strcmp(entry->d_name, "reset_counters") == 0) {
		return 1;
	}
        return 0;
}

static int qsort_strcmp(const void *a, const void *b, void *log_var)
{
        /* ovis_log_t log = log_var; */
        return strcmp(*(char * const *)a, *(char * const *)b);
}

static void compact_remaining_available(char *available[], int remaining)
{
        int i;
        for (i = 0; i < remaining; i++) {
                available[i] = available[i+1];
        }
        available[i] = NULL;
}

/*
 * The counter files take two forms as of shs-12.0.0.
 *
 * For cxi files (in /sys/class/cxi), the form is:
 *   <counter>@<timestamp seconds>.<timestamp nanoseconds>
 * e.g.
 *   103060429480@1743569213.088677609
 *
 * For retry handler files, the format is just:
 *
 *  <counter>
 * e.g.
 *  103060429480
 *
 * We do not currently care about the timestamp, so this function
 * suffices to read the initial unsigned integer in each.
 */
static uint64_t read_integer_from_file(const char *filename, ovis_log_t log)
{
	uint64_t value = 0;
	FILE *file = fopen(filename, "r");

	if (!file) {
		ovis_log(log, OVIS_LERROR,
			 "Error %d opening file '%s'\n", errno, filename);
		return 0;
	}

	if (fscanf(file, "%"SCNu64, &value) != 1) {
		ovis_log(log, OVIS_LERROR,
			 "Error %d reading unsigned integer from file '%s'\n",
			 errno, filename);
		fclose(file);
		return 0;
	}

	fclose(file);
	return value;
}

static uint64_t link_restarts_sample(struct link_restarts *lr, ovis_log_t log)
{
        char filepath[PATH_MAX];
        uint64_t ts;
        int i;

        for (i = 0; i < LINK_RESTARTS_TIMESTAMPS; i++) {
                snprintf(filepath, sizeof(filepath), lr->path_template, i);
                ts = read_integer_from_file(filepath, log);
                if (lr->timestamps[i] != ts) {
                        lr->timestamps[i] = ts;
                        lr->value += 1;
                }
        }

        return lr->value;
}

static struct link_restarts *link_restarts_init(char *ifaces[], int num_ifaces,
                                                char *tel_path, ovis_log_t log)
{
        struct link_restarts *lr;
        char path_template[PATH_MAX];
        int i;

        lr = calloc(num_ifaces, sizeof(*lr));
        if (lr == NULL) {
                return NULL;
        }
        for (i = 0; i < num_ifaces; i++) {
                snprintf(path_template, PATH_MAX,
                         "%s/%s/device/link_restarts/time_%%d",
                         tel_path, ifaces[i]);
                lr[i].path_template = strdup(path_template);
                /* initializing the cache with the existing time stamp values,
                   so we intentionally ignore the return value */
                link_restarts_sample(&lr[i], log);
        }

        return lr;
}

static void link_restarts_fini(struct link_restarts *lr, int num_ifaces)
{
        int i;

        for (i = 0; i < num_ifaces; i++) {
                free(lr[i].path_template);
        }
        free(lr);
}

/*
 * IN directory - directory in which to look for files
 * IN patterns - string of comma-separated regular expressions
 * IN log - the log
 * OUT selected - Array of size MAX_FILES, used to return pointers to malloc'ed
 *                file name strings. Individual name strings must be freed by
 *                the caller.
 * OUT num_selected - the number of malloced strings pointed to by selected
 */
static int select_files(char *directory, char *patterns, ovis_log_t log,
                        char *selected[], int *num_selected)
{
        int num_available = 0;
        char *available[MAX_FILES];
        DIR *dir;
	struct dirent *ent;
        char *patterns_copy;
        char *pattern;
        int i;

        /* If NULL, skip nothing */
	if (patterns == NULL) {
		return 0;
	}

	dir = opendir(directory);
	if (!dir) {
		ovis_log(log, OVIS_LERROR,
			 "Error %d opening '%s'\n",
			 errno, directory);
		return 1;
	}
        ovis_log(log, OVIS_LDEBUG, "Selecting files from directory '%s'\n", directory);

	while ((ent = readdir(dir)) != NULL && num_available < MAX_FILES) {
                if (skip_entry(dir, ent)) {
                        continue;
                }
                available[num_available] = strdup(ent->d_name);
                num_available++;
        }
        closedir(dir);
        qsort_r(available, num_available, sizeof(char *), qsort_strcmp, log);

	/* Copy of original list */
	patterns_copy = strdup(patterns);
	if (!patterns_copy) {
		ovis_log(log, OVIS_LERROR, "Memory allocation failed for pattern list\n");
		return 1;
	}

	/* Splits comma-separated string into individual words. */
	pattern = strtok(patterns_copy, ",");

        *num_selected = 0;
	/* Step through each match */
	while (pattern != NULL) {
		/* Trim leading spaces */
		while (*pattern == ' ')
			pattern++;

		/* Trim trailing spaces */
		char *end = pattern + strlen(pattern) - 1;
		while (end > pattern && *end == ' ') {
			*end = '\0';
			end--;
		}

                if (strcmp(pattern, "derived:link_restarts") == 0) {
                        /* Special case */
                        selected[*num_selected] = strdup("derived:link_restarts");
                        *num_selected +=1;
                        pattern = strtok(NULL, ",");
                        continue;
                }

		/* Compile and execute the regex */
		regex_t regex;
		int reti = regcomp(&regex, pattern, REG_EXTENDED);
		if (reti != 0) {
			ovis_log(log, OVIS_LERROR, "Could not compile regex: %s\n", pattern);
			pattern = strtok(NULL, ",");
			continue;
		}

                for (i = 0; i < num_available; ) {
                        regmatch_t pmatch;
                        reti = regexec(&regex, available[i], 1, &pmatch, 0);
                        /* Test for complete match */
                        if (reti == 0 && pmatch.rm_so == 0 && pmatch.rm_eo == strlen(available[i])) {
                                ovis_log(log, OVIS_LDEBUG,
                                         "Found match: '%s', pattern '%s'\n",
                                         available[i], pattern);
                                selected[*num_selected] = available[i];
                                compact_remaining_available(&available[i], num_available - i - 1);
                                num_available--;
                                *num_selected += 1;
                        } else {
                                i++;
                        }
                }

                regfree(&regex);

		pattern = strtok(NULL, ",");
	}

        for (i = 0; i < num_available; i++) {
                free(available[i]);
        }
	free(patterns_copy);

	return 0;
}

static int get_cxi_metric_names(ldmsd_plug_handle_t handle, struct files_s *tel_files,
				const char *cxi_name, char *root_path, char *patterns)
{
	ovis_log_t log = ldmsd_plug_log_get(handle);
	const char *end_path_tel = "device/telemetry";

	/* root_path "/" cxi_name "/" null terminator */
	size_t path_len = strlen(root_path) + 1 + strlen(cxi_name) + 1 + strlen(end_path_tel) + 2;
	char *s_path = malloc(path_len);
	if (!s_path) {
		ovis_log(log, OVIS_LERROR,
			 "Memory allocation failed for path '%s'\n",
			 root_path);
		return 1;
	}

	snprintf(s_path, path_len, "%s/%s/%s", root_path, cxi_name, end_path_tel);

        select_files(s_path, patterns, log, tel_files->names, &tel_files->count);

	free(s_path);
	return 0;
}


static char *get_counter_names_from_file(ldmsd_plug_handle_t handle, const char *filename)
{
    ovis_log_t log = ldmsd_plug_log_get(handle);

    if (!filename || filename[0] == '\0') {
        ovis_log(log, OVIS_LERROR, "Filename is NULL or empty\n");
        return NULL;
    }

    FILE *file = fopen(filename, "r");
    if (!file) {
        ovis_log(log, OVIS_LERROR, "Failed to open counter filter file '%s'\n", filename);
        return NULL;
    }

    char line[MAX_FILENAME_LENGTH];
    size_t total_len = 0;
    char *result = NULL;
    int first = 1;

    while (fgets(line, sizeof(line), file)) {
        // Strip newline characters
        line[strcspn(line, "\r\n")] = 0;

        // Skip empty lines
        if (line[0] == '\0')
            continue;

        ovis_log(log, OVIS_LDEBUG, "LINE: '%s'\n", line);

        size_t line_len = strlen(line);
        size_t new_len = total_len + line_len + (first ? 0 : 1) + 1; // +1 for comma or null terminator

        // Reallocate result buffer
        char *new_result = realloc(result, new_len);
        if (!new_result) {
            ovis_log(log, OVIS_LERROR,"Memory allocation failed!\n");
            free(result);
            fclose(file);
            return NULL;
        }
        result = new_result;

        // Append comma if not the first item
        if (!first) {
            result[total_len] = ',';
            total_len += 1;
        } else {
            first = 0;
        }

        // Copy the line
        memcpy(result + total_len, line, line_len);
        total_len += line_len;

        // Null-terminate
        result[total_len] = '\0';
    }

    fclose(file);
    ovis_log(log, OVIS_LDEBUG, "RESULT: '%s'\n", result);
    return result;
}

static int get_cxi_metric_values(cxi_t cxi)
{
	int iface, file;
	static char path[PATH_MAX];
	enum ldms_value_type typ;
	size_t len;
	ldms_mval_t tel_rec = ldms_list_first(cxi->set, cxi->tel_list_mval, &typ, &len);

	for (iface = 0; iface < cxi->iface_count; iface++) {
		ldms_record_array_set_str(tel_rec,
					  cxi->iface_mid,
					  cxi->iface_names[iface]);

		for (file = 0; file < cxi->tel_files.count; file++) {
                        uint64_t value;

                        if (cxi->restarts &&
                            cxi->restarts_mid == cxi->tel_files.midx[file]) {
                                /* Special case sampling for derived:link_restarts metric */
                                value = link_restarts_sample(&cxi->restarts[iface],
                                                             cxi->log);
                        } else {
                                snprintf(path, sizeof(path), "%s/%s/device/telemetry/%s",
                                         cxi->tel_path, cxi->iface_names[iface],
                                         cxi->tel_files.names[file]);

                                value = read_integer_from_file(path, cxi->log);
                        }

			ldms_record_set_u64(tel_rec,
                                            cxi->tel_files.midx[file],
                                            value);
		}
		tel_rec = ldms_list_next(cxi->set, tel_rec, &typ, &len);
	}
	return 0;
}

static int get_rh_metric_values(cxi_t cxi)
{
	int iface, file;
	static char path[PATH_MAX];
	enum ldms_value_type typ;
	size_t len;
	ldms_mval_t rh_rec = ldms_list_first(cxi->set, cxi->rh_list_mval, &typ, &len);

	for (iface = 0; iface < cxi->iface_count; iface++) {
		ldms_record_array_set_str(rh_rec,
					  cxi->iface_mid,
					  cxi->iface_names[iface]);

		for (file = 0; file < cxi->rh_files.count; file++) {
			snprintf(path, sizeof(path), "%s/%s/%s",
				 cxi->rh_path, cxi->iface_names[iface],
				 cxi->rh_files.names[file]);

                        uint64_t value = read_integer_from_file(path, cxi->log);
                        ldms_record_set_s64(rh_rec,
					    cxi->rh_files.midx[file],
					    value);
		}
		rh_rec = ldms_list_next(cxi->set, rh_rec, &typ, &len);
	}
	return 0;
}

static int get_rh_names(ldmsd_plug_handle_t handle, struct files_s *rh_files,
			const char *cxi_name, char *root_path, char *patterns)
{
	ovis_log_t log = ldmsd_plug_log_get(handle);

	/* +1 for "/" +2 for "/" and null terminator */
	size_t path_len = strlen(root_path) + 1 + strlen(cxi_name) + 2;
	char *s_path = malloc(path_len);
	if (!s_path) {
		ovis_log(log, OVIS_LERROR,
			 "Memory allocation failed for path in '%s'\n",
			 root_path);
		return 1;
	}

	snprintf(s_path, path_len, "%s/%s", root_path, cxi_name);

        select_files(s_path, patterns, log, rh_files->names, &rh_files->count);

	free(s_path);
	return 0;
}

static int create_metric_set(cxi_t cxi)
{
	int rc, i;
	ldms_record_t tel_rec;
	ldms_record_t rh_rec;
	int tel_rec_mid;
	int rh_rec_mid;

	cxi->schema = base_schema_new(cxi->base);
	if (!cxi->schema) {
		ovis_log(cxi->log, OVIS_LERROR,
			 "%s: The schema '%s' could not be created, errno=%d.\n",
			 __FILE__, cxi->base->schema_name, errno);
		rc = errno;
		goto err1;
	}
	cxi->metric_offset = ldms_schema_metric_count_get(cxi->schema);

	/* Per interface telemetry record */
	tel_rec = ldms_record_create("tel_record");
	cxi->iface_mid = ldms_record_metric_add(tel_rec,
						"iface_name", "",
						LDMS_V_CHAR_ARRAY, 32);

	for (i = 0; i < cxi->tel_files.count; i++) {
                int midx;
		midx = ldms_record_metric_add(tel_rec, cxi->tel_files.names[i],
					    "", LDMS_V_U64, 0);
		if (midx < 0) {
			ovis_log(cxi->log, OVIS_LERROR,
				 "Error %d creating metric '%s'\n",
				 -midx, cxi->tel_files.names[i]);
                        /* Not owned by the schema yet */
                        ldms_record_delete(tel_rec);
                        rc = errno;
                        goto err1;
		}
		/* Cache the record metric index in tel_files */
                cxi->tel_files.midx[i] = midx;

                /* Special case initialization for derived:link_restarts metric */
                if (strcmp(cxi->tel_files.names[i], "derived:link_restarts") == 0) {
                        cxi->restarts = link_restarts_init(cxi->iface_names,
                                                           cxi->iface_count,
                                                           cxi->tel_path,
                                                           cxi->log);
                        cxi->restarts_mid = midx;
                }
	}
	tel_rec_mid = ldms_schema_record_add(cxi->schema, tel_rec);
	if (!tel_rec_mid){
		ldms_record_delete(tel_rec);
		rc = errno;
		goto err1;
	}

	/* Per interface retry handler record */
	rh_rec = ldms_record_create("rh_record");
	rc = ldms_record_metric_add(rh_rec,
				    "iface_name", "",
				    LDMS_V_CHAR_ARRAY, 32);

	for (i = 0; i < cxi->rh_files.count; i++) {
		rc = ldms_record_metric_add(rh_rec, cxi->rh_files.names[i],
					    "", LDMS_V_U64, 0);
		if (rc < 0) {
                        /* Not owned by the schema yet */
                        ldms_record_delete(rh_rec);
			rc = errno;
			goto err1;
		}
		/* Cache the record metric index in rh_files */
                cxi->rh_files.midx[i] = rc;
	}

	/* List of per interface telemetry records */
	rc = ldms_schema_metric_list_add(cxi->schema, "tel_list", "tel_record",
					 ldms_record_heap_size_get(tel_rec) * cxi->iface_count);
	cxi->tel_list_mid = rc;
	if (rc < 0) {
		ovis_log(cxi->log, OVIS_LERROR,
			 "Error %d creating tel_list.\n", errno);
		goto err1;
	}

	/* List of per interface retry handler records */
	rh_rec_mid = ldms_schema_record_add(cxi->schema, rh_rec);
	if (!rh_rec_mid){
		ldms_record_delete(rh_rec);
		rc = errno;
		goto err1;
	}
	rc = ldms_schema_metric_list_add(cxi->schema, "rh_list", "rh_record",
					 ldms_record_heap_size_get(rh_rec) * cxi->iface_count);
	cxi->rh_list_mid = rc;
	if (rc < 0) {
		ovis_log(cxi->log, OVIS_LERROR,
			 "Error %d creating rh_list.\n", errno);
		goto err1;
	}

	size_t size = ldms_record_heap_size_get(tel_rec) +
		      ldms_record_heap_size_get(rh_rec);
	size = size * cxi->iface_count;
	cxi->set = base_set_new_heap(cxi->base, size);
	if (!cxi->set) {
		rc = errno;
		goto err1;
	}

	ldms_mval_t rh_rec_mval;
	ldms_mval_t tel_rec_mval;

	for (i = 0; i < cxi->iface_count; i++) {
		cxi->rh_list_mval = ldms_metric_get(cxi->set, cxi->rh_list_mid);
		cxi->tel_list_mval = ldms_metric_get(cxi->set, cxi->tel_list_mid);
		rh_rec_mval = ldms_record_alloc(cxi->set, rh_rec_mid);
		tel_rec_mval = ldms_record_alloc(cxi->set, tel_rec_mid);

		rc = ldms_list_append_record(cxi->set, cxi->rh_list_mval, rh_rec_mval);
		if (rc) {
			ovis_log(cxi->log, OVIS_LERROR,
				 "Error %d appending record to rh_list.\n",
				 rc);
			goto err2;
		}

		rc = ldms_list_append_record(cxi->set, cxi->tel_list_mval, tel_rec_mval);
		if (rc) {
			ovis_log(cxi->log, OVIS_LERROR,
				 "Error %d appending record to tel_list.\n",
				 rc);
			goto err2;
		}
	}
	return 0;
err2:
	if (cxi->set) {
		base_set_delete(cxi->base);
		cxi->set = NULL;
	}
err1:
	if (cxi->schema) {
		base_schema_delete(cxi->base);
		cxi->schema = NULL;
	}
	return rc;
}

static const char *usage(ldmsd_plug_handle_t handle)
{
	static char help_str[PATH_MAX];
	snprintf(help_str, sizeof(help_str), "config name=%s %s [tel_path=PATH] [rh_path=PATH]",
		 ldmsd_plug_name_get(handle), BASE_CONFIG_USAGE);
	return help_str;
}

static int get_cxi_interfaces(ldmsd_plug_handle_t handle, char *tel_path)
{
	cxi_t cxi = ldmsd_plug_ctxt_get(handle);
	const char *pattern = "cxi*";
	DIR *dir = opendir(tel_path);
	int count;

	if (!dir) {
		ovis_log(cxi->log, OVIS_LERROR,
			 "Error %d opening directory '%s'\n",
			 errno, tel_path);
		return 1;
	}

	struct dirent *ent;
	count = 0;
	while ((ent = readdir(dir)) != NULL) {
		if (fnmatch(pattern, ent->d_name, 0) == 0) {
			count++;
		}
	}
	closedir(dir);

	dir = opendir(tel_path);
	if (!dir) {
		ovis_log(cxi->log, OVIS_LERROR,
			 "Error %d reopening directory '%s'\n",
			 errno, tel_path);
		return 1;
	}

	cxi->iface_count = count;
	cxi->iface_names = calloc(count, sizeof(char *));
	if (!cxi->iface_names) {
		ovis_log(cxi->log, OVIS_LERROR, "Memory allocation failed");
		closedir(dir);
		return 1;
	}

	count = 0;
	while ((ent = readdir(dir)) != NULL) {
		if (fnmatch(pattern, ent->d_name, 0) == 0) {
			cxi->iface_names[count] = strdup(ent->d_name);
			if (!cxi->iface_names[count]) {
				ovis_log(cxi->log, OVIS_LERROR, "Memory allocation failed");
				closedir(dir);
				return 1;
			}
			count++;
		}
	}
        qsort_r(cxi->iface_names, count, sizeof(char *), qsort_strcmp, cxi->log);

	closedir(dir);
	return 0;
}

static int config(ldmsd_plug_handle_t handle,
		  struct attr_value_list *kwl, struct attr_value_list *avl)
{
	cxi_t cxi = ldmsd_plug_ctxt_get(handle);
	ovis_log_t log = ldmsd_plug_log_get(handle);
	int rc;
	char *rh_path;
	char *rh_counters;
	char *rh_counters_file;
	char *tel_path;
	char *tel_counters;
	char *tel_counters_file;

	if (cxi->set) {
		ovis_log(log, OVIS_LERROR, "Set already created.\n");
		return EBUSY;
	}

	cxi->base = base_config(avl,
				ldmsd_plug_cfg_name_get(handle),
				ldmsd_plug_name_get(handle), log);
	if (!cxi->base) {
		rc = errno;
		goto err;
	}

	/* Metric filter */
	tel_counters = av_value(avl, "tel_counters");
	if (tel_counters != NULL) {
		ovis_log(log, OVIS_LDEBUG, "We have telemetry counter filters: %s\n", tel_counters);
	}
	rh_counters = av_value(avl, "rh_counters");
	if (rh_counters != NULL) {
		ovis_log(log, OVIS_LDEBUG, "We have retry handler counter filters: %s\n", rh_counters);
	}

	/* Metric filter from files */
	if (!tel_counters) {
		tel_counters_file = av_value(avl, "tel_counters_file");
		if (tel_counters_file != NULL) {
			ovis_log(log, OVIS_LDEBUG, "We have telemetry counter file: %s\n", tel_counters_file);
			tel_counters = get_counter_names_from_file(handle, tel_counters_file);
			if (tel_counters == NULL) {
				rc = EINVAL;
				goto err;
			}
			ovis_log(log, OVIS_LDEBUG, "We have telemetry counter filters: %s\n", tel_counters);
		}
	}
	if (!rh_counters) {
		rh_counters_file = av_value(avl, "rh_counters_file");
		if (rh_counters_file != NULL) {
			ovis_log(log, OVIS_LDEBUG, "We have retry handler counter file: %s\n", rh_counters_file);
			rh_counters = get_counter_names_from_file(handle, rh_counters_file);
			if (rh_counters == NULL) {
				rc = EINVAL;
				goto err;
			}
			ovis_log(log, OVIS_LDEBUG, "We have retry handler counter filters: %s\n", rh_counters);
		}
	}

	tel_path = av_value(avl, "tel_path");
	if (!tel_path) {
		tel_path = TEL_PATH_CXI;
	}
	cxi->tel_path = strdup(tel_path);
	if (!cxi->tel_path) {
		rc = ENOMEM;
		goto err;
	}

	rh_path = av_value(avl, "rh_path");
	if (!rh_path) {
		rh_path = RH_PATH_CXI;
	}
	cxi->rh_path = strdup(rh_path);
	if (!cxi->rh_path) {
		rc = ENOMEM;
		goto err;
	}

	/* Count CXI interfaces and allocate file name arrays */
	if (get_cxi_interfaces(handle, tel_path) != 0) {
		rc = 1;
		goto err;
	}

	/* Process telemetry and retry handler files for each CXI interface */
        if (get_cxi_metric_names(handle, &cxi->tel_files, cxi->iface_names[0],
                                 tel_path, tel_counters) != 0) {
                rc = 1;
                goto err;
        }
        if (get_rh_names(handle, &cxi->rh_files, cxi->iface_names[0],
                         rh_path, rh_counters) != 0) {
                rc = 1;
                goto err;
        }

	rc = create_metric_set(cxi);
	if (rc != 0) {
		goto err;
	}

	return 0;

err:
	return rc;
}

static int sample(ldmsd_plug_handle_t handle)
{
	cxi_t cxi = ldmsd_plug_ctxt_get(handle);

	base_sample_begin(cxi->base);

	/* Process telemetry and retry handler files for each CXI interface */
	if (get_cxi_metric_values(cxi) != 0) {
		return 1;
	}
	if (get_rh_metric_values(cxi) != 0) {
		return 1;
	}

	base_sample_end(cxi->base);
	return 0;
}

static int constructor(ldmsd_plug_handle_t handle)
{
	cxi_t cxi = calloc(1, sizeof(*cxi));
	if (cxi) {
		ldmsd_plug_ctxt_set(handle, cxi);
		return 0;
	}
	return ENOMEM;
}

static void destructor(ldmsd_plug_handle_t handle)
{
	cxi_t cxi = ldmsd_plug_ctxt_get(handle);
	int i;
	int j;

	if (!cxi)
		return;

	/* Handle ldms_set_t deletion */
	if (cxi->set)
		base_set_delete(cxi->base);

	if (cxi->schema)
		base_schema_delete(cxi->base);

        if (cxi->restarts)
                link_restarts_fini(cxi->restarts, cxi->iface_count);

	/* Free interface names */
	if (cxi->iface_names) {
		for ( i = 0; i < cxi->iface_count; i++) {
			free(cxi->iface_names[i]);
		}
		free(cxi->iface_names);
	}

	/* Free tel_path and rh_path */
	free(cxi->tel_path);
	free(cxi->rh_path);

	/* Free telemetry files */
	if (cxi->tel_files.count > 0) {
                for ( j = 0; j < cxi->tel_files.count; j++) {
                        free(cxi->tel_files.names[j]);
		}
	}

	/* Free retry handler files */
	if (cxi->rh_files.count > 0) {
                for ( j = 0; j < cxi->rh_files.count; j++) {
                        free(cxi->rh_files.names[j]);
		}
	}

	/* Free base data */
	base_del(cxi->base);

	/* Finally, free the main structure */
	free(cxi);
}

struct ldmsd_sampler ldmsd_plugin_interface = {
	.base.type = LDMSD_PLUGIN_SAMPLER,
	.base.flags = LDMSD_PLUGIN_MULTI_INSTANCE,
	.base.config = config,
	.base.usage = usage,
	.base.constructor = constructor,
	.base.destructor = destructor,
	.sample = sample,
};
