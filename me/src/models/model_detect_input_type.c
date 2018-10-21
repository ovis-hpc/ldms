/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-2014 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013-2014 Sandia Corporation. All rights reserved.
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
 * \file model_detect_input_type.c
 * \brief Detect the specify type in the last number in the thresholds.
 *
 * Model Type: univariate
 * Thresholds: <input_type>,<input type>,<input type>
 * The available input types are:
 * 	INPUT_DATA:	the value sent from the source is new. (default)
 * 	NO_DATA:	the value sent from the source is old or empty.
 *
 * Application:
 * 	The model could be used to detect the event that an ldmsd aggregator lost the connection to an ldmsd sampler.
 */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <stddef.h>
#include "me.h"

static me_log_fn msglog;

static int evaluate(me_model_engine_t m, me_model_cfg_t cfg,
				me_input_t input, me_output_t output)
{
	enum me_input_type type = me_get_input_type(input);
	double *thrs = me_get_thresholds(cfg);

	if (type == (enum me_input_type)thrs[ME_SEV_CRITICAL])
		output->level = ME_SEV_CRITICAL;
	else if (type == (enum me_input_type)thrs[ME_SEV_WARNING])
		output->level = ME_SEV_WARNING;
	else if (type == (enum me_input_type)thrs[ME_SEV_INFO])
		output->level = ME_SEV_INFO;
	else
		output->level = ME_SEV_NOMINAL;

	output->ts = me_get_timestamp(input);

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
	return  "   A univariate model detects the input type. \n"
		"   Two existing input types are\n"
		"        INPUT_DATA	The value sent from the source is new. (default)\n"
		"        NO_DATA	The value sent from the source is old or empty.\n"
		"     create name=model_high_outlier model_id=<model_id> thresholds=<type>,<type>,<type>\n"
		"	   -  type	The input type to be detected.\n";
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

struct me_model_plugin model_detect_input_type = {
	.base = {
			.name = "model_detect_input_type",
			.term = term,
			.type = ME_MODEL_PLUGIN,
			.usage = usage
		},
	.new_model_engine = new_model_engine,
	.parse_param = NULL
};

struct me_plugin *get_plugin(me_log_fn log_fn)
{
	msglog = log_fn;
	model_detect_input_type.base.model_pi = &model_detect_input_type;
	return &model_detect_input_type.base;
}
