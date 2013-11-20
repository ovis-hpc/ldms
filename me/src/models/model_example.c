/*
 * model_example.c
 *
 *  Created on: Mar 11, 2013
 *      Author: nichamon
 */
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <stddef.h>

#include "me.h"

static me_log_fn msglog;

static int reset()
{
	msglog("Model Example: reset\n");
	return 0;
}

static int evaluate(me_model_engine_t m, me_model_cfg_t cfg,
				me_input_t input_val, me_output_t output)
{
	double value = me_get_input_value(input_val, msglog);
	double *thresholds = me_get_thresholds(cfg);

	if (input_val->type == ME_NO_DATA) {
		msglog("Mexample: no data.\n");
	}

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

	output->ts = me_get_timestamp(input_val);
	return 0;
}

static void term()
{
}

static const char *usage()
{
	return	"   A univariate model for testing.\n"
		"	create name=model_example model_id=<model_id> thresholds=<thresholds>\n"
		"          Don't need any parameters.\n";
}

me_model_engine_t new_model_engine(me_model_cfg_t cfg)
{
	struct me_model_engine *meng = calloc(1, sizeof(*meng));
	meng->evaluate = evaluate;
	meng->reset_state = reset;
	return meng;
}

struct me_model_plugin model_exam = {
	.base = {
			.name = "model_example",
			.term = term,
			.type = ME_MODEL_PLUGIN,
			.usage = usage,
		},
	.new_model_engine = new_model_engine
};

struct me_plugin *get_plugin(me_log_fn _log_fn)
{
	msglog = _log_fn;
	model_exam.base.model_pi = &model_exam;
	return &model_exam.base;
}
