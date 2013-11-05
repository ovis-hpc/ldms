/*
 * model_high_exact.c
 *
 *  Created on: May 10, 2013
 *      Author: nichamon
 */
/**
 * model_high_exact.c
 * Model Type: univariate
 * Thresholds: exact values
 * Detect: too high values
 */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <stddef.h>
#include "me.h"

static me_log_fn msglog;

static int evaluate(me_model_engine_t m, me_model_cfg_t cfg,
				me_input_t input_val, me_output_t output)
{
	uint64_t value = me_get_input_value(input_val, msglog);

	double *thresholds = me_get_thresholds(cfg);

	if (value >= thresholds[ME_SEV_INFO])
		if (value >= thresholds[ME_SEV_WARNING])
			if (value >= thresholds[ME_SEV_CRITICAL])
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

static int reset()
{
	return 0;
}

static void term()
{
}

static const char *usage()
{
	return  "   A univariate model detects too high values.\n"
		"   A too high value is greater than one of the given thresholds.\n"
		"	create name=model_high_exact model_id=<model_id> thresholds=<values>\n"
		"	   - values	values of metrics used as thresholds"
		"	   Don't need any parameters.\n";
}

me_model_engine_t new_model_engine(me_model_cfg_t cfg)
{
	struct me_model_engine *eng = calloc(1, sizeof(*eng));
	if (!eng)
		return NULL;
	eng->evaluate = evaluate;
	eng->reset_state = reset;
	return eng;
}

struct me_model_plugin model_high_exact = {
	.base = {
			.name = "model_high_exact",
			.term = term,
			.type = ME_MODEL_PLUGIN,
			.usage = usage,
		},
	.new_model_engine = new_model_engine
};

struct me_plugin *get_plugin(me_log_fn _log_fn)
{
	msglog = _log_fn;
	model_high_exact.base.model_pi = &model_high_exact;
	return &model_high_exact.base;
}


