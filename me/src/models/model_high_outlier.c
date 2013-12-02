/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013 Sandia Corporation. All rights reserved.
 * Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
 * license for use of this work by or on behalf of the U.S. Government.
 * Export of this program may require a license from the United States
 * Government.
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
 * model_high_outlier.c
 * Model Type: univariate
 * Thresholds: number in standard deviation unit, e.g., 2.5 = 2.5sd
 * Detect: too high values
 */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <stddef.h>
#include "me.h"

#define HIGH_OUTLIER_NUM_REQ_SAMPLES 100

static me_log_fn msglog;

typedef struct high_outlier_param {
	int num_req_samples;
} *high_outlier_param_t;

typedef struct high_outlier_ctxt {
	double mean;
	double avg_sum_square;
	int num_samples;
} *high_outlier_ctxt_t;

static int __update(high_outlier_ctxt_t ctxt, double val)
{
	ctxt->num_samples++;
	int num = ctxt->num_samples;
	double mean = ctxt->mean;
	double avg_ss = ctxt->avg_sum_square;
	if (num == 1) {
		mean = val;
		avg_ss = pow(val, 2);
	} else if (num == 2) {
		mean = (mean + val) / 2;
		avg_ss += pow(val, 2);
	} else {
		mean = ((mean * (num - 1)) + val) / num;
		avg_ss = ((avg_ss * (num - 2)) + pow(val, 2)) / (num - 1);
	}
	ctxt->mean = mean;
	ctxt->avg_sum_square = avg_ss;
	return 0;
}

static double get_sd(double mean, double avg_sum_square, int n)
{
	return sqrt(avg_sum_square - ((n / (n -1 )) * pow(mean, 2)));

}

static double __evaluate(high_outlier_ctxt_t ctxt, double val)
{
	double sd = get_sd(ctxt->mean, ctxt->avg_sum_square,
					ctxt->num_samples);
	return (val - ctxt->mean) / sd;
}

static int evaluate(me_model_engine_t m, me_model_cfg_t cfg,
				me_input_t input_val, me_output_t output)
{
	uint64_t value = me_get_input_value(input_val, msglog);
	high_outlier_param_t param = (high_outlier_param_t)me_get_params(cfg);
	struct high_outlier_ctxt *ctxt = m->mcontext;

	double z;
	if (ctxt->num_samples < param->num_req_samples) {
		output->level = ME_ONLY_UPDATE;
		__update(ctxt, value);
	} else {
		z = __evaluate(ctxt, value);

		double *thrs = me_get_thresholds(cfg);

		if (z >= thrs[ME_SEV_INFO])
			if (z >= thrs[ME_SEV_WARNING])
				if (z >= thrs[ME_SEV_CRITICAL])
					output->level = ME_SEV_CRITICAL;
				else
					output->level = ME_SEV_WARNING;
			else
				output->level = ME_SEV_INFO;
		else
			output->level = ME_SEV_NOMINAL;

		output->ts = me_get_timestamp(input_val);

		if (output->level <= ME_SEV_INFO)
			__update(ctxt, value);
	}
	return 0;
}

static int reset(me_model_engine_t m)
{
	return 0;
}

static void term()
{
}

static const char *usage()
{
	return  "   A univariate model detects too high values according to \n"
		"   the sample Mean and sample Standard Deviation\n"
		"   A too high value is greater than {mean + s.d. * a given threshold}.\n"
		"	create name=model_high_outlier model_id=<model_id> threshlds=<multipliers of s.d.>\n"
		"		params=<number>\n"
		"	   -  number	The number of required samples. If none is given,\n"
		"			the default is 100\n";
}

static void *parse_param(char *params)
{
	struct high_outlier_param *param = malloc(sizeof(*param));
	if (!params)
		param->num_req_samples = HIGH_OUTLIER_NUM_REQ_SAMPLES;
	else
		param->num_req_samples = atoi((char *)params);
	return param;
}

me_model_engine_t new_model_engine(me_model_cfg_t cfg)
{
	struct high_outlier_ctxt *ctxt = calloc(1, sizeof(*ctxt));
	if (!ctxt)
		return NULL;
	struct me_model_engine *eng = calloc(1, sizeof(*eng));
	if (!eng)
		return NULL;
	eng->evaluate = evaluate;
	eng->reset_state = reset;
	eng->mcontext = (void *)ctxt;
	return eng;
}

struct me_model_plugin model_high_outlier = {
	.base = {
			.name = "model_high_outlier",
			.term = term,
			.type = ME_MODEL_PLUGIN,
			.usage = usage
		},
	.new_model_engine = new_model_engine,
	.parse_param = parse_param
};

struct me_plugin *get_plugin(me_log_fn log_fn)
{
	msglog = log_fn;
	model_high_outlier.base.model_pi = &model_high_outlier;
	return &model_high_outlier.base;
}
