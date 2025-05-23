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

typedef struct files_s {
	int count;
	int midx[MAX_FILES];
	char *names[MAX_FILES];
} *files_t;

typedef struct ifaces_s {
	int count;
	char *names[MAX_CXI_IFACES];
} *cxi_iface_t;

typedef struct cxi_s {
	struct files_s *rh_files;
	struct files_s *tel_files;
	int iface_count;
	char **iface_names;
	char *tel_path;
	char *rh_path;

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

static int skip_name(char *name, char *counters, ovis_log_t log)
{
	/* Skip special files and directories */
	if (strcmp(name, ".") == 0 ||
	    strcmp(name, "..") == 0 ||
	    strcmp(name, "ALL-in-binary") == 0 ||
	    strcmp(name, "config") == 0 ||
	    strcmp(name, "reset_counters") == 0) {
		return 1;
	}

	/* If NULL, skip nothing */
	if (counters == NULL) {
		return 0;
	}

	/* Copy of original list */
	char *list_copy = strdup(counters);
	if (!list_copy) {
		ovis_log(log, OVIS_LERROR, "Memory allocation failed for counters list\n");
		return 0;
	}

	/* Splits comma-separated string into individual words. */
	char *token = strtok(list_copy, ",");
	int result = 1; /* Default to skip */

	/* Step through each match */
	while (token != NULL) {
		/* Trim leading spaces */
		while (*token == ' ')
			token++;

		/* Trim trailing spaces */
		char *end = token + strlen(token) - 1;
		while (end > token && *end == ' ') {
			*end = '\0';
			end--;
		}

		/* Compile and execute the regex */
		regex_t regex;
		int reti = regcomp(&regex, token, REG_NOSUB | REG_EXTENDED);
		if (reti != 0) {
			ovis_log(log, OVIS_LERROR, "Could not compile regex: %s\n", token);
			token = strtok(NULL, ",");
			continue;
		}

		reti = regexec(&regex, name, 0, NULL, 0);
		regfree(&regex);

		/* Test for match */
		if (reti == 0) {
			ovis_log(log, OVIS_LDEBUG, "Found Match: '%s'\n", token);
			result = 0; /* Don't skip */
			break;
		}
		token = strtok(NULL, ",");
	}

	free(list_copy);

	if (result) {
		ovis_log(log, OVIS_LDEBUG, "Skip it '%s'\n", name);
	}

	return result;
}

static int get_cxi_metric_names(ldmsd_plug_handle_t handle,
				files_t tel_files,
				const char *cxi_name, char *root_path, char *counters)
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

	snprintf(s_path, path_len, "/%s/%s/%s", root_path, cxi_name, end_path_tel);

	DIR *dir = opendir(s_path);
	if (!dir) {
		ovis_log(log, OVIS_LERROR,
			 "Error %d opening '%s'\n",
			 errno, s_path);
		free(s_path);
		return 1;
	}

	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL && tel_files->count < MAX_FILES) {
		/* Skip special files and directories */
		if (skip_name(ent->d_name, counters, log))
			continue;

		/* Allocate and copy the file name */
		tel_files->names[tel_files->count] = strdup(ent->d_name);
		if (!tel_files->names[tel_files->count]) {
			ovis_log(log, OVIS_LERROR,
				 "Memory allocation failed for file name '%s'\n",
				 ent->d_name);
			closedir(dir);
			free(s_path);
			return 1;
		}
		tel_files->count++;
	}

	closedir(dir);
	free(s_path);
	return 0;
}

static double parse_value_before_at(cxi_t cxi, const char *filename)
{
	char buffer[1024];
	FILE *file = fopen(filename, "r");
	if (!file) {
		ovis_log(cxi->log, OVIS_LERROR,
			 "Error opening file '%s'\n", filename);
		return 0.0;
	}

	if (!fgets(buffer, sizeof(buffer), file)) {
		ovis_log(cxi->log, OVIS_LERROR,
			 "Error reading from file '%s'\n", filename);
		fclose(file);
		return 0.0;
	}
	fclose(file);

	return atof(buffer);
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

		for (file = 0; file < cxi->tel_files[iface].count; file++) {
			snprintf(path, sizeof(path), "%s/%s/device/telemetry/%s",
				 cxi->tel_path, cxi->iface_names[iface],
				 cxi->tel_files[iface].names[file]);

			double value = parse_value_before_at(cxi, path);
			ldms_record_set_double(tel_rec,
					       cxi->tel_files[iface].midx[file],
					       value);
		}
		tel_rec = ldms_list_next(cxi->set, tel_rec, &typ, &len);
	}
	return 0;
}

static int read_integer_from_file(cxi_t cxi, const char *filename)
{
	FILE *file = fopen(filename, "r");
	if (!file) {
		ovis_log(cxi->log, OVIS_LERROR,
			 "Error %d opening file '%s'\n", errno, filename);
		return 0;
	}

	int value;
	if (fscanf(file, "%d", &value) != 1) {
		ovis_log(cxi->log, OVIS_LERROR,
			 "Error %d reading integer from file '%s'\n",
			 errno, filename);
		fclose(file);
		return 0;
	}

	fclose(file);
	return value;
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

		for (file = 0; file < cxi->rh_files[iface].count; file++) {
			snprintf(path, sizeof(path), "%s/%s/%s",
				 cxi->rh_path, cxi->iface_names[iface],
				 cxi->rh_files[iface].names[file]);

			int64_t value = read_integer_from_file(cxi, path);
			ldms_record_set_s64(rh_rec,
					    cxi->rh_files[iface].midx[file],
					    value);
		}
		rh_rec = ldms_list_next(cxi->set, rh_rec, &typ, &len);
	}
	return 0;
}

static int get_rh_names(ldmsd_plug_handle_t handle, files_t rh_files,
			const char *cxi_name, char *root_path, char *counters)
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

	snprintf(s_path, path_len, "/%s/%s", root_path, cxi_name);

	DIR *dir = opendir(s_path);
	if (!dir) {
		ovis_log(log, OVIS_LERROR,
			 "Error %d opening directory '%s'\n",
			 errno, s_path);
		free(s_path);
		return 1;
	}

	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL) {
		/* Skip some files and directories */
		if (skip_name(ent->d_name, counters, log)) {
			continue;
		}

		/* Allocate and copy the file name */
		rh_files->names[rh_files->count] = strdup(ent->d_name);
		if (!rh_files->names[rh_files->count]) {
			ovis_log(log, OVIS_LERROR,
				 "Memory allocation failed for file name '%s'\n",
				 ent->d_name);
			closedir(dir);
			free(s_path);
			return 1;
		}
		rh_files->count++;
	}

	closedir(dir);
	free(s_path);
	return 0;
}

static int create_metric_set(cxi_t cxi)
{
	int rc, i, j;
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
		goto err;
	}
	cxi->metric_offset = ldms_schema_metric_count_get(cxi->schema);

	/* Per interface telemetry record */
	tel_rec = ldms_record_create("tel_record");
	cxi->iface_mid = ldms_record_metric_add(tel_rec,
						"iface_name", "",
						LDMS_V_CHAR_ARRAY, 32);

	for (i = 0; i < cxi->tel_files[0].count; i++) {
		rc = ldms_record_metric_add(tel_rec, cxi->tel_files[0].names[i],
					    "", LDMS_V_D64, 0);
		if (rc < 0) {
			ovis_log(cxi->log, OVIS_LERROR,
				 "Error %d creating metric '%s'\n",
				 -rc, cxi->tel_files[0].names[i]);
			rc = errno;
			goto err;
		}
		/* Cache the record metric index in tel_files */
		for (j = 0; j < cxi->iface_count; j++) {
			cxi->tel_files[j].midx[i] = rc;
		}
	}
	tel_rec_mid = ldms_schema_record_add(cxi->schema, tel_rec);

	/* Per interface retry handler record */
	rh_rec = ldms_record_create("rh_record");
	rc = ldms_record_metric_add(rh_rec,
				    "iface_name", "",
				    LDMS_V_CHAR_ARRAY, 32);

	for (i = 0; i < cxi->rh_files[0].count; i++) {
		rc = ldms_record_metric_add(rh_rec, cxi->rh_files[0].names[i],
					    "", LDMS_V_D64, 0);
		if (rc < 0) {
			rc = errno;
			goto err;
		}
		/* Cache the record metric index in rh_files */
		for (j = 0; j < cxi->iface_count; j++) {
			cxi->rh_files[j].midx[i] = rc;
		}
	}

	/* List of per interface telemetry records */
	rc = ldms_schema_metric_list_add(cxi->schema, "tel_list", "tel_record",
					 ldms_record_heap_size_get(tel_rec) * cxi->iface_count);
	cxi->tel_list_mid = rc;
	if (rc < 0) {
		ovis_log(cxi->log, OVIS_LERROR,
			 "Error %d creating tel_list.\n", errno);
		goto err;
	}

	/* List of per interface retry handler records */
	rh_rec_mid = ldms_schema_record_add(cxi->schema, rh_rec);
	rc = ldms_schema_metric_list_add(cxi->schema, "rh_list", "rh_record",
					 ldms_record_heap_size_get(rh_rec) * cxi->iface_count);
	cxi->rh_list_mid = rc;
	if (rc < 0) {
		ovis_log(cxi->log, OVIS_LERROR,
			 "Error %d creating rh_list.\n", errno);
		goto err;
	}

	size_t size = ldms_record_heap_size_get(tel_rec) +
		      ldms_record_heap_size_get(rh_rec);
	size = size * cxi->iface_count;
	cxi->set = base_set_new_heap(cxi->base, size);
	if (!cxi->set) {
		rc = errno;
		goto err;
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
			goto err;
		}

		rc = ldms_list_append_record(cxi->set, cxi->tel_list_mval, tel_rec_mval);
		if (rc) {
			ovis_log(cxi->log, OVIS_LERROR,
				 "Error %d appending record to tel_list.\n",
				 rc);
			goto err;
		}
	}
	return 0;

err:
	if (cxi->base)
		base_schema_delete(cxi->base);
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

	cxi->tel_files = calloc(count, sizeof(struct files_s));
	if (!cxi->tel_files) {
		ovis_log(cxi->log, OVIS_LERROR, "Memory allocation failed");
		closedir(dir);
		return 1;
	}

	cxi->rh_files = calloc(count, sizeof(struct files_s));
	if (!cxi->rh_files) {
		ovis_log(cxi->log, OVIS_LERROR, "Memory allocation failed");
		closedir(dir);
		return 1;
	}

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
	char *tel_path;
	char *tel_counters;

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
	int i;
	for (i = 0; i < cxi->iface_count; i++) {
		if (get_cxi_metric_names(handle, &cxi->tel_files[i], cxi->iface_names[i],
					 tel_path, tel_counters) != 0) {
			rc = 1;
			goto err;
		}
		if (get_rh_names(handle, &cxi->rh_files[i], cxi->iface_names[i], rh_path, rh_counters) != 0) {
			rc = 1;
			goto err;
		}
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
	if (!cxi)
		return;

	/* Handle ldms_set_t deletion */
	if (cxi->set)
		base_set_delete(cxi->base);

	if (cxi->schema)
		base_schema_delete(cxi->base);

	/* Free interface names */
	if (cxi->iface_names) {
		for (int i = 0; i < cxi->iface_count; i++) {
			free(cxi->iface_names[i]);
		}
		free(cxi->iface_names);
	}

	/* Free tel_path and rh_path */
	free(cxi->tel_path);
	free(cxi->rh_path);

	/* Free telemetry files */
	if (cxi->tel_files) {
		for (int i = 0; i < cxi->iface_count; i++) {
			for (int j = 0; j < cxi->tel_files[i].count; j++) {
				free(cxi->tel_files[i].names[j]);
			}
		}
		free(cxi->tel_files);
	}

	/* Free retry handler files */
	if (cxi->rh_files) {
		for (int i = 0; i < cxi->iface_count; i++) {
			for (int j = 0; j < cxi->rh_files[i].count; j++) {
				free(cxi->rh_files[i].names[j]);
			}
		}
		free(cxi->rh_files);
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
