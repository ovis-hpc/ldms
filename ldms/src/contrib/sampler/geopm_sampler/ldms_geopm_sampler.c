/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2022 Intel Corporation
 * Copyright (c) 2011-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2011-2018 Open Grid Computing, Inc. All rights reserved.
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


/**
 * \file ldms_geopm_sampler.c
 * \brief Provider of signals accessible via PlatformIO from libgeopmd
 */

#define _GNU_SOURCE

#include "ldms_geopm_sampler.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/errno.h>

#include "ldms.h"
#include "ldmsd.h"
#include "sampler_base.h"

#include "geopm_pio.h"
#include "geopm_topo.h"
#include "geopm_error.h"
#include "geopm_field.h"

#define SAMP "ldms_geopm_sampler"
#define GEOPM_METRIC_MAX 4096

static ldmsd_msg_log_f msglog = NULL;

/* Global state other than the logging function */
static ldms_set_t g_set = NULL;
static base_data_t g_base = NULL;
static int g_metric_offset = 0;
static int g_metric_cnt = 0;
static int g_metric_pio_idx[GEOPM_METRIC_MAX] = {};
static double g_metric[GEOPM_METRIC_MAX] = {};
static enum ldms_geopm_sampler_metric_type_e g_metric_type[GEOPM_METRIC_MAX] = {};
static char g_geopm_request_path[NAME_MAX] = "";


enum ldms_value_type ldms_geopm_sampler_value_type(enum ldms_geopm_sampler_metric_type_e metric_type)
{
	enum ldms_value_type result;
	switch(metric_type) {
		case LDMS_GEOPM_SAMPLER_METRIC_TYPE_D64:
			result = LDMS_V_D64;
			break;
		case LDMS_GEOPM_SAMPLER_METRIC_TYPE_S32:
			result = LDMS_V_S32;
			break;
		case LDMS_GEOPM_SAMPLER_METRIC_TYPE_S64:
			result = LDMS_V_S64;
			break;
		case LDMS_GEOPM_SAMPLER_METRIC_TYPE_U64:
			result = LDMS_V_U64;
			break;
		default:
			result = LDMS_V_NONE;
			break;
	}
	return result;
}

int ldms_geopm_sampler_convert_sample(enum ldms_geopm_sampler_metric_type_e metric_type,
				      double sample,
				      union ldms_value *value)
{
	int rc = 0;
	switch(metric_type) {
		case LDMS_GEOPM_SAMPLER_METRIC_TYPE_D64:
			value->v_d = sample;
			break;
		case LDMS_GEOPM_SAMPLER_METRIC_TYPE_S32:
			value->v_s32 = (int32_t)sample;
			break;
		case LDMS_GEOPM_SAMPLER_METRIC_TYPE_S64:
			value->v_s64 = (int64_t)sample;
			break;
		case LDMS_GEOPM_SAMPLER_METRIC_TYPE_U64:
			value->v_u64 = geopm_signal_to_field(sample);
			break;
		default:
			msglog(LDMSD_LERROR,
			       SAMP": %s:%d: Error converting signal, unknown metric_type: %d\n",
			       __FILE__, __LINE__, metric_type);
			rc = EINVAL;
			break;
	}
	return rc;
}

void ldms_geopm_sampler_set_log(void (*pf)(int, const char *fms, ...))
{
	msglog = pf;
}

int ldms_geopm_sampler_open_config(const char *config_path, FILE **result)
{
	int rc = 0;
	errno = 0;
	*result = fopen(config_path, "r");
	if (*result == NULL) {
		msglog(LDMSD_LERROR,
		       SAMP": %s:%d: Error in opening file '%s' containing list of samples, errno=%d\n",
		       __FILE__, __LINE__, config_path, errno);
		rc = ENOENT;
	} else {
		msglog(LDMSD_LDEBUG,
		       SAMP": Success opening configuration file: '%s'\n",
		       config_path);
	}
	return rc;
}

int ldms_geopm_sampler_parse_line(FILE *fid,
				  char *signal_name,
				  enum geopm_domain_e *domain_type,
				  int *domain_idx,
				  char *metric_name,
				  enum ldms_geopm_sampler_metric_type_e *metric_type)
{
	char format[NAME_MAX];
	int format_size = snprintf(format, sizeof(format),
				   "%%%ds %%%ds %%d %%%ds ",
				   NAME_MAX - 1, NAME_MAX - 1, NAME_MAX - 1);
	assert(format_size < NAME_MAX);
	char domain_type_str[NAME_MAX];
	char *line = NULL;
	size_t line_len = 0;
	int num_scan = 0;
	errno = 0;
	while (num_scan == 0) {
		if (getline(&line, &line_len, fid) == -1) {
			if (errno) {
				num_scan = errno;
			}
			else if (feof(fid)) {
				num_scan = EOF;
			}
			continue;
		}
		char unparsed[NAME_MAX];
		char *line_ptr = line;
		while (isspace(*line_ptr)) {
			++line_ptr;
		}
		if (line_ptr[0] != '\0') {
			signal_name[NAME_MAX - 1] = '\0';
			domain_type_str[NAME_MAX - 1] = '\0';
			unparsed[NAME_MAX - 1] = '\0';
			num_scan = sscanf(line_ptr, format, signal_name,
					  domain_type_str, domain_idx, unparsed);
			if (num_scan >= 1 && signal_name[0] == '#') {
				num_scan = 0;
			}
		}
	}
	if (line != NULL) {
		free(line);
	}
	if (num_scan == EOF) {
		return EOF;
	}
	else if (num_scan == ENOMEM) {
		msglog(LDMSD_LERROR,
		       SAMP": %s:%d: Unable to allocate memory while parsing GEOPM Sampler configuration file\n",
		       __FILE__, __LINE__);
		return ENOMEM;
	}
	else if (num_scan != 3) {
		msglog(LDMSD_LERROR,
		       SAMP": %s:%d: File format error for GEOPM Sampler configuration file\n",
		       __FILE__, __LINE__);
		return EINVAL;
	}
	*domain_type = geopm_topo_domain_type(domain_type_str);
	if (*domain_type < 0) {
		char err_msg[NAME_MAX];
		geopm_error_message(*domain_type, err_msg, NAME_MAX);
		msglog(LDMSD_LERROR,
		       SAMP": %s:%d: Invalid domain name '%s' provided in GEOPM configuration file\n%s\n",
		       __FILE__, __LINE__, domain_type_str, err_msg);
		return EINVAL;

	}
	int agg_type = -1;
	int behavior_type = -1;
	int format_type;
	int info_rc = geopm_pio_signal_info(signal_name,
					    &agg_type,
					    &format_type,
					    &behavior_type);
	if (info_rc != 0) {
		char err_msg[NAME_MAX];
		geopm_error_message(info_rc, err_msg, NAME_MAX);
		msglog(LDMSD_LERROR,
		       SAMP": %s:%d: Unable to determine signal format\n%s\n",
		       __FILE__, __LINE__, err_msg);
		return info_rc;

	}
	*metric_type = format_type;
	int print_rc = snprintf(metric_name, NAME_MAX, "%s_%s_%d",
				signal_name, domain_type_str, *domain_idx);
	if (print_rc >= NAME_MAX || print_rc < 0) {
		msglog(LDMSD_LERROR,
		       SAMP": %s:%d: Unable to create metric name with snprintf() ",
		       __FILE__, __LINE__);
		return -1;
	}
	msglog(LDMSD_LDEBUG, SAMP": Adding metric: %s\n", metric_name);
	return 0;
}

int ldms_geopm_sampler_parse_check(int parse_rc)
{
	if (parse_rc != EOF) {
		msglog(LDMSD_LERROR,
		       SAMP": %s:%d: Unable to parse GEOPM Sampler configuration file\n",
		       __FILE__, __LINE__);
		return parse_rc;
	}
	if (g_metric_cnt == 0) {
		msglog(LDMSD_LERROR,
		       SAMP": %s:%d: No metrics added after parsing configuration file\n",
		       __FILE__, __LINE__);
		return errno ? errno : -1;
	}
	return 0;
}

int ldms_geopm_sampler_push_signal(const char *signal_name,
				   enum geopm_domain_e domain_type,
				   int domain_idx,
				   enum ldms_geopm_sampler_metric_type_e metric_type)
{
	if (g_metric_cnt >= GEOPM_METRIC_MAX) {
		msglog(LDMSD_LERROR,
		       SAMP": %s:%d: Configuration file contains too many requests\n",
		       __FILE__, __LINE__);
		return E2BIG;
	}

	int rc = 0;
	int sig_idx = geopm_pio_push_signal(signal_name, domain_type, domain_idx);
	if (sig_idx < 0) {
		rc = EINVAL;
		char err_msg[NAME_MAX];
		geopm_error_message(sig_idx, err_msg, NAME_MAX);
		msglog(LDMSD_LERROR,
		       SAMP": %s:%d: Failed call to geopm_pio_push_signal(%s, %d, %d)\n%s\n",
		       __FILE__, __LINE__, signal_name, domain_type,
		       domain_idx, err_msg);
	}
	g_metric_pio_idx[g_metric_cnt] = sig_idx;
	g_metric_type[g_metric_cnt] = metric_type;
	++g_metric_cnt;
	return rc;
}

int ldms_geopm_sampler_read_batch(void)
{
	int rc = 0;
	char err_msg[NAME_MAX];

	/* read metric set once */
	rc = geopm_pio_read_batch();
	if (rc != 0) {
		geopm_error_message(rc, err_msg, NAME_MAX);
		msglog(LDMSD_LERROR,
		       SAMP": %s:%d\n%s ",
		       __FILE__, __LINE__, err_msg);
	}
	return rc;
}

int ldms_geopm_sampler_sample_batch(void)
{
	int rc = 0;
	char err_msg[NAME_MAX];

	/* extract vals of individual metrics */
	for (int metric_idx = 0; rc == 0 && metric_idx < g_metric_cnt; metric_idx++) {
		rc = geopm_pio_sample(g_metric_pio_idx[metric_idx], g_metric + metric_idx);
		if (rc != 0) {
			geopm_error_message(rc, err_msg, NAME_MAX);
			msglog(LDMSD_LERROR,
			       SAMP": %s:%d\n%s\n",
			       __FILE__, __LINE__, err_msg);
		}
		msglog(LDMSD_LDEBUG,
		       SAMP": metric_%d = %f\n",
		       g_metric_offset + metric_idx,
		       g_metric[metric_idx]);
	}
	return rc;
}

void ldms_geopm_sampler_reset(void)
{
	g_metric_offset = 0;
	g_metric_cnt = 0;
	geopm_pio_reset();
}

static int create_metric_set(void)
{
	ldms_schema_t schema;
	int rc = 0;
	char metric_name[NAME_MAX];
	char signal_name[NAME_MAX];
	enum ldms_geopm_sampler_metric_type_e metric_type = LDMS_GEOPM_SAMPLER_METRIC_TYPE_INVALID;
	int domain_idx = -1;
	enum geopm_domain_e domain_type = GEOPM_DOMAIN_INVALID;
	FILE *signal_fileptr = NULL;

	errno = 0;
	schema = base_schema_new(g_base);
	if (schema == NULL) {
		msglog(LDMSD_LERROR,
		       SAMP": %s:%d: The schema '%s' could not be created, errno=%d.\n",
		       __FILE__, __LINE__, g_base->schema_name, errno);
		rc = errno ? errno : -1;
		goto exit;
	}

	/* Location of first metric */
	g_metric_offset = ldms_schema_metric_count_get(schema);
	msglog(LDMSD_LDEBUG,
	       SAMP": Schema created: starts at offset %d\n",
	       g_metric_offset);

	/*
	 * Process the file to define all the metrics.
	 */
	rc = ldms_geopm_sampler_open_config(g_geopm_request_path, &signal_fileptr);
	if (rc != 0) {
		goto exit;
	}
	int parse_rc = 0;
	while ((parse_rc = ldms_geopm_sampler_parse_line(signal_fileptr,
							 signal_name,
							 &domain_type,
							 &domain_idx,
							 metric_name,
							 &metric_type)) == 0) {
		rc = ldms_geopm_sampler_push_signal(signal_name,
						    domain_type,
						    domain_idx,
						    metric_type);
		if (rc != 0) {
			goto exit;
		}
		rc = ldms_schema_metric_add(schema, metric_name,
					    ldms_geopm_sampler_value_type(metric_type));
		if (rc < 0) {
			msglog(LDMSD_LERROR,
			       SAMP": %s:%d: Unable to add metric using ldms_schma_metric_add(), returned: %d.\n",
			       __FILE__, __LINE__, rc);
			goto exit;
		}
	}
	rc = ldms_geopm_sampler_parse_check(parse_rc);
	if (rc != 0) {
		goto exit;
	}
	errno = 0;
	g_set = base_set_new(g_base);
	if (g_set == NULL) {
		rc = errno ? errno : -1;
		msglog(LDMSD_LERROR,
		       SAMP": %s:%d: Failed to call base_set_new(), returned NULL: errno: %d.\n",
		       __FILE__, __LINE__, errno);
		goto exit;
	}

exit:
	if (signal_fileptr) {
		fclose(signal_fileptr);
	}
	return rc;
}


/**
 * check for invalid flags, with particular emphasis on warning the user about
 */
static int config_check(struct attr_value_list *avl)
{
	msglog(LDMSD_LDEBUG, SAMP": config_check() called\n");
	char *tmp_ptr = av_value(avl, "geopm_request_path");
	strncpy(g_geopm_request_path, tmp_ptr, NAME_MAX);
	if (g_geopm_request_path[NAME_MAX - 1] != '\0') {
		msglog(LDMSD_LERROR,
		       SAMP": %s:%d: Long file name: '%s'\n",
		       __FILE__, __LINE__, tmp_ptr);
		return ENAMETOOLONG;
	}
	return access(g_geopm_request_path, R_OK);
}

static const char *usage(struct ldmsd_plugin *self)
{
	return  "config geopm_request_path=<absolute-path-to-file> name=" SAMP " " BASE_CONFIG_USAGE;
}

static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	int rc = 0;

	/* Check if 'config' has already been called before.
	 * Note: The current implementation of the GEOPM sampler does not permit multiple
	 * calls to 'config'
	 */
	if (g_set) {
		msglog(LDMSD_LERROR,
		       SAMP ": %s:%d: Set already created.\n",
		       __FILE__, __LINE__);
		return EINVAL;
	}

	/* Check the validity of the config script params
	 * For the GEOPM sampler, this function checks the availability of
	 * the file containing the list of signals to be monitored while sampling
	 */
	rc = config_check(avl);
	if (rc != 0) {
		return rc;
	}

	/* Configure sampler base
	 * This makes call into core LDMS functions for initializing the sampler
	 */
	errno = 0;
	g_base = base_config(avl, SAMP, SAMP, msglog);
	msglog(LDMSD_LDEBUG, SAMP": Base config() called.\n");
	if (g_base == NULL) {
		rc = errno ? errno : -1;
		msglog(LDMSD_LERROR,
		       SAMP ": %s:%d: Failed to call base_config(), returned NULL: errno: %d.\n",
		       __FILE__, __LINE__, errno);
		goto exit;
	}

	/* Parse GEOPM sampler configuration file and add all metrics */
	rc = create_metric_set();
	if (rc) {
		msglog(LDMSD_LERROR,
		       SAMP ": %s:%d: Failed to create a metric set.\n",
		       __FILE__, __LINE__);
		goto exit;
	}
	return 0;

exit:
	base_del(g_base);
	return rc;
}


static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	return g_set;
}

static int sample(struct ldmsd_sampler *self)
{
	int rc = 0;
	union ldms_value value;
	char err_msg[NAME_MAX];
	double sample;

	if (g_set == NULL) {
		msglog(LDMSD_LDEBUG, SAMP": plugin not initialized\n");
		return EINVAL;
	}

	base_sample_begin(g_base);

	/* read metric set once */
	rc = ldms_geopm_sampler_read_batch();
	if (rc != 0) {
		goto exit;
	}

	rc = ldms_geopm_sampler_sample_batch();
	if (rc != 0) {
		goto exit;
	}

	for (int metric_idx = 0; rc == 0 && metric_idx < g_metric_cnt; metric_idx++) {
		msglog(LDMSD_LDEBUG, SAMP": metric %d = %f\n",
		       g_metric_offset + metric_idx, g_metric[metric_idx]);
		rc = ldms_geopm_sampler_convert_sample(g_metric_type[metric_idx],
						       g_metric[metric_idx],
						       &value);
		if (rc == 0) {
			ldms_metric_set(g_set, g_metric_offset + metric_idx, &value);
		}
	}
exit:
	base_sample_end(g_base);
	return rc;
}


static void term(struct ldmsd_plugin *self)
{
	if (g_base) {
		base_del(g_base);
	}
	if (g_set) {
		ldms_set_delete(g_set);
	}
	if (g_metric_cnt != 0) {
		ldms_geopm_sampler_reset();
	}
	g_set = NULL;
}


static struct ldmsd_sampler g_geopm_sampler_plugin = {
	.base = {
		.name = SAMP,
		.type = LDMSD_PLUGIN_SAMPLER,
		.term = term,
		.config = config,
		.usage = usage,
	},
	.get_set = get_set,
	.sample = sample,
};


struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	ldms_geopm_sampler_set_log(pf);
	g_set = NULL;
	return &g_geopm_sampler_plugin.base;
}
