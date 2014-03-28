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
 * \file model_high_percentage.c
 * \brief Detect a value such the its percent is higher than the given thresholds.
 *
 * Model Type: univariate
 * Thresholds: percentage. For example, if 10 is given, the model interprets as 10% of the maximum value.
 * Parameter: the maximum value of the metric
 */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <stddef.h>
#include "me.h"

static me_log_fn msglog;

typedef struct high_percentage_param {
	double max_value;
} *high_pct_param_t;

static int evaluate(me_model_engine_t m, me_model_cfg_t cfg,
				me_input_t input, me_output_t output)
{
	if (me_get_input_type(input) == ME_NO_DATA)
		return 0;

	high_pct_param_t param = (high_pct_param_t)me_get_params(cfg);

	enum me_input_type type = me_get_input_type(input);
	double *thrs = me_get_thresholds(cfg);
	double value = me_get_input_value(input, msglog);

	double pct = (value / param->max_value) * 100;

	if (pct >= thrs[ME_SEV_INFO])
		if (pct >= thrs[ME_SEV_WARNING])
			if (pct >= thrs[ME_SEV_CRITICAL])
				output->level = ME_SEV_CRITICAL;
			else
				output->level = ME_SEV_WARNING;
		else
			output->level = ME_SEV_INFO;
	else
		output->level = ME_SEV_NOMINAL;

	output->ts = me_get_timestamp(input);

	return 0;
}

static void *parse_param(char *params)
{
	if (!params) {
		msglog("model_high_percentage: no given parameters\n");
		return NULL;
	}

	struct high_percentage_param *param = malloc(sizeof(*param));
		param->max_value = atof((char *)params);

	return param;
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
	return  "   A univariate model detects high percentage values relatively to the maximum value. \n"
		"     create name=model_high_percentage model_id=<model_id> thresholds=<pct>,<pct>,<pct>\n"
		"		params=<max value>\n"
		"	   - pct	Percentage. For example, if 10 is given, the model interprets as 10%.\n"
		"	   - max value	The maximum value of the metric\n";
}

static me_model_engine_t new_model_engine(me_model_cfg_t cfg)
{
	struct me_model_engine *eng = calloc(1, sizeof(*eng));
	if (!eng)
		return NULL;
	eng->evaluate = evaluate;
	eng->reset_state = reset;
	eng->mcontext = NULL;
	return eng;
}

struct me_model_plugin model_high_percentage = {
	.base = {
			.name = "model_high_percentage",
			.term = term,
			.type = ME_MODEL_PLUGIN,
			.usage = usage
		},
	.new_model_engine = new_model_engine,
	.parse_param = parse_param,
};

struct me_plugin *get_plugin(me_log_fn log_fn)
{
	msglog = log_fn;
	model_high_percentage.base.model_pi = &model_high_percentage;
	return &model_high_percentage.base;
}
