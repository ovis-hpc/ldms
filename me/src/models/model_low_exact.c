/*
 * too_low_exact_m.c
 *
 *  Created on: May 9, 2013
 *      Author: nichamon
 */
/**
 * model_low_exact.c
 * Model Type: univariate
 * Thresholds: exact values
 * Detect: too low values
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
	uint64_t value = me_get_input_value(input, msglog);

	double *thresholds = me_get_thresholds(cfg);

	if (value <= thresholds[ME_SEV_INFO])
		if (value <= thresholds[ME_SEV_WARNING])
			if (value <= thresholds[ME_SEV_CRITICAL])
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

static int reset(me_model_engine_t m)
{
	return 0;
}

static void term()
{
}

static const char *usage()
{
	return  "   A univariate model detects too low values.\n"
		"   A too low value is smaller than one of the given thresholds.\n"
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

struct me_model_plugin model_low_exact = {
	.base = {
			.name = "model_low_exact",
			.term = term,
			.type = ME_MODEL_PLUGIN,
			.usage = usage,
		},
	.new_model_engine = new_model_engine
};

struct me_plugin *get_plugin(me_log_fn log_fn)
{
	msglog = log_fn;
	model_low_exact.base.model_pi = &model_low_exact;
	return &model_low_exact.base;
}
