/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013,2014 Open Grid Computing, Inc. All rights reserved.
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
 * \file model_severity_level.c
 * \brief This model is to evaluate the severity level metric ME received as inputs.
 *
 * When the model receives a value that matches one of the given thresholds,
 * it will report an event of the severity level associated
 * with the match threshold. Otherwise, the model reports a nominal event.
 *
 * Model Type: univariate
 * Thresholds: <INFO>, <WARNING>, <CRITICAL>
 * 	INFO		The metric value indicating the INFO level.
 *	WARNING		The metric value indicating the WARNING level.
 *	CRITICAL	The metric value indicating the CRITICAL level.
 *
 * Created on: Apr 25, 2014
 * Author: Nichamon Naksinehaboon
 */

#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include "me.h"

static me_log_fn msglog;

static int evaluate(me_model_engine_t m, me_model_cfg_t cfg,
				me_input_t input, me_output_t output)
{
	if (me_get_input_type(input) == ME_NO_DATA)
		return 0;

	enum me_input_type type = me_get_input_type(input);
	double *thrs = me_get_thresholds(cfg);
	double value = me_get_input_value(input, msglog);

	if (value == thrs[ME_SEV_CRITICAL])
		output->level = ME_SEV_CRITICAL;
	else if (value == thrs[ME_SEV_WARNING])
		output->level = ME_SEV_WARNING;
	else if (value == thrs[ME_SEV_INFO])
		output->level = ME_SEV_INFO;
	else if (value == thrs[ME_SEV_NOMINAL])
		output->level = ME_SEV_NOMINAL;
	else
		output->level = ME_ONLY_UPDATE;

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
	return 	"   create name=model_severity_level model_id=<model_id> thresholds=<INFO>,<WARNING>,<CRITICAL>\n"
		"	   - INFO	The metric value indicating the INFO level.\n"
		"	   - WARNING	The metric value indicating the WARNING level.\n"
		"	   - CRITICAL	The metric value indicating the CRITICAL level.\n"
		"   \n"
		"   A univariate model evaluates the severity level metric ME received as inputs.\n"
		"   When the model receives a value equal to a given threshold, the model generates an event\n"
		"   of the severity level. If the value does not match any given thresholds, the model generates\n"
		"   a nominal event.\n";
}

static me_model_engine_t new_model_engine(me_model_cfg_t cfg)
{
	struct me_model_engine *engine = calloc(1, sizeof(*engine));
	if (!engine)
		return NULL;
	engine->evaluate = evaluate;
	engine->reset_state = reset;
	engine->mcontext = NULL;
	return engine;
}

struct me_model_plugin model_severity_level = {
	.base = {
			.name = "model_severity_level",
			.term = term,
			.type = ME_MODEL_PLUGIN,
			.usage = usage
		},
	.new_model_engine = new_model_engine,
	.parse_param = NULL,
};

struct me_plugin *get_plugin(me_log_fn log_fn)
{
	msglog = log_fn;
	model_severity_level.base.model_pi = &model_severity_level;
	return &model_severity_level.base;
}
