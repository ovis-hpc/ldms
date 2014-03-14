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
 * model_below_mean.c
 * Model Type: univariate
 * Thresholds: number in standard deviation unit, e.g., 2.5 = 2.5 x sd
 * Detect: too high values
 */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <stddef.h>
#include <string.h>
#include "me.h"

static me_log_fn msglog;

typedef struct below_mean_param {
	double mean;
	double sd;
} *below_mean_param_t;

static int evaluate(me_model_engine_t m, me_model_cfg_t cfg,
				me_input_t input_val, me_output_t output)
{
	if (me_get_input_type(input_val) == ME_NO_DATA)
		return 0;

	uint64_t value = me_get_input_value(input_val, msglog);
	below_mean_param_t param = (below_mean_param_t)me_get_params(cfg);

	double *thrs = me_get_thresholds(cfg);
	double z = ( param->mean - value) / param->sd;

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
	return  "   A univariate model detects the values that is equal to the given mean\n"
		"   or exceed by a given threshold x the given stand deviation\n"
		"	create name=model_above_mean model_id=<model_id>\n"
		"              thresholds=<multipliers of s.d.> params=<mean,sd>\n"
		"	   -  mean	The average of metric values\n";
		"	   -  sd	The standard deviation of metric values\n";
}

static void *parse_param(char *params)
{
	struct below_mean_param *param = malloc(sizeof(*param));
	if (!params)
		return NULL;
	char *tmp = strtok(params, ",");
	if (!tmp)
		return NULL;
	param->mean = atof(tmp);
	tmp = strtok(NULL, ",");
	param->sd = atof(tmp);
	return param;
}

me_model_engine_t new_model_engine(me_model_cfg_t cfg)
{
	struct me_model_engine *eng = calloc(1, sizeof(*eng));
	if (!eng)
		return NULL;
	eng->evaluate = evaluate;
	eng->reset_state = reset;
	eng->mcontext = 0; /* no context */
	return eng;
}

struct me_model_plugin model_below_mean = {
	.base = {
			.name = "model_below_mean",
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
	model_below_mean.base.model_pi = &model_below_mean;
	return &model_below_mean.base;
}
