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
 * \file ldms_geopm_sampler_test.c
 * \brief Command line tool to enable unit testing of the GEOPM LDMS plugin
 */

#include "ldms_geopm_sampler.h"

#include <stdarg.h>
#include <stdio.h>
#include <limits.h>
#include <float.h>
#include <stdint.h>
#include <sys/errno.h>
#include <geopm_field.h>
#include "ldmsd.h"

void ldmsd_set_deregister(const char *inst_name, const char *plugin_name)
{

}


int ldmsd_set_register(ldms_set_t set, const char *plugin_name)
{
	return 0;
}

static void test_logger(int ldms_enum, const char *fmt, ...)
{
	va_list fmt_args;
	va_start(fmt_args, fmt);
	FILE *stream = stdout;
	if (ldms_enum == LDMSD_LERROR) {
		stream = stderr;
	}
	vfprintf(stream, fmt, fmt_args);
}

static int check_convert(void)
{
	double sample;
	enum ldms_geopm_sampler_metric_type_e metric_type;
	enum ldms_value_type value_type;
	union ldms_value value;

	sample = DBL_MAX;
	metric_type = LDMS_GEOPM_SAMPLER_METRIC_TYPE_D64;
	value_type = ldms_geopm_sampler_value_type(metric_type);
	if (value_type != LDMS_V_D64) {
		fprintf(stderr, "Error: ldms_geopm_sampler_value_type(LDMS_GEOPM_SAMPLER_METRIC_TYPE_D64) returns wrong value.\n");
		return -1;
	}
	ldms_geopm_sampler_convert_sample(metric_type, sample, &value);
	if (value.v_d != DBL_MAX) {
		fprintf(stderr, "Error: ldms_geopm_sampler_convert_sampler() for double failed.\n");
		return -1;
	}

	sample = INT32_MAX;
	metric_type = LDMS_GEOPM_SAMPLER_METRIC_TYPE_S32;
	value_type = ldms_geopm_sampler_value_type(metric_type);
	if (value_type != LDMS_V_S32) {
		fprintf(stderr, "Error: ldms_geopm_sampler_value_type(LDMS_GEOPM_SAMPLER_METRIC_TYPE_S32) returns wrong value.\n");
		return -1;
	}
	ldms_geopm_sampler_convert_sample(metric_type, sample, &value);
	if (value.v_s32 != INT32_MAX) {
		fprintf(stderr, "Error: ldms_geopm_sampler_convert_sampler() for int32_t failed.\n");
		return -1;
	}

	int64_t expect = INT32_MAX * 0xFFFFULL;
	sample = expect;
	metric_type = LDMS_GEOPM_SAMPLER_METRIC_TYPE_S64;
	value_type = ldms_geopm_sampler_value_type(metric_type);
	if (value_type != LDMS_V_S64) {
		fprintf(stderr, "Error: ldms_geopm_sampler_value_type(LDMS_GEOPM_SAMPLER_METRIC_TYPE_S64) returns wrong value.\n");
		return -1;
	}
	ldms_geopm_sampler_convert_sample(metric_type, sample, &value);
	if (value.v_s64 != expect) {
		fprintf(stderr, "Error: ldms_geopm_sampler_convert_sampler() for int64_t failed.\n");
		return -1;
	}

	sample = geopm_field_to_signal(UINT64_MAX);
	metric_type = LDMS_GEOPM_SAMPLER_METRIC_TYPE_U64;
	value_type = ldms_geopm_sampler_value_type(metric_type);
	if (value_type != LDMS_V_U64) {
		fprintf(stderr, "Error: ldms_geopm_sampler_value_type(LDMS_GEOPM_SAMPLER_METRIC_TYPE_U64) returns wrong value.\n");
		return -1;
	}
	ldms_geopm_sampler_convert_sample(metric_type, sample, &value);
	if (value.v_s64 != UINT64_MAX) {
		fprintf(stderr, "Error: ldms_geopm_sampler_convert_sampler() for uint64_t failed.\n");
		return -1;
	}
	return 0;
}

static int run(const char *path)
{
	FILE *fid = NULL;
	int err = ldms_geopm_sampler_open_config(path, &fid);
	char signal_name[NAME_MAX] = {};
	char metric_name[NAME_MAX] = {};
	int domain_type = -1;
	int domain_idx = -1;
	enum ldms_geopm_sampler_metric_type_e metric_type = LDMS_GEOPM_SAMPLER_METRIC_TYPE_INVALID;
	int parse_err = 0;

	if (check_convert() != 0) {
		return -1;
	}
	while(err == 0 &&
	      (parse_err = ldms_geopm_sampler_parse_line(fid,
							 signal_name,
							 &domain_type,
							 &domain_idx,
							 metric_name,
							 &metric_type)) == 0) {
		err = ldms_geopm_sampler_push_signal(signal_name,
						     domain_type,
						     domain_idx,
						     metric_type);
	}
	if (err == 0) {
		err = ldms_geopm_sampler_parse_check(parse_err);
	}
	if (fid != NULL) {
		fclose(fid);
	}
	if (!err) {
		err = ldms_geopm_sampler_read_batch();
	}
	if (!err) {
		err = ldms_geopm_sampler_sample_batch();
	}
	if (!err) {
		ldms_geopm_sampler_reset();
	}
	return err;
}

int main(int argc, char **argv)
{
	int err = 0;
	if (argc == 1 || strncmp("--help", argv[1], strlen("--help")) == 0) {
		printf("Usage: %s <geopm_request_path>\n", argv[0]);
		return 0;
	}

	ldms_geopm_sampler_set_log(test_logger);

	err = run(argv[1]);
	if (err == 0) {
		// Run twice to validate reset
		err = run(argv[1]);
	}

	if (!err) {
		printf("\nSUCCESS\n");
	} else {
		fprintf(stderr, "\nFAIL\n");
	}
	return err;
}
