/**
 * model_high_exact_bound.c
 * Model Type: univariate
 * Thresholds: exact values
 * Detect: too high values
 * parameters: The least and the highest possible values
 */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <stddef.h>
#include <string.h>
#include "me.h"

static me_log_fn msglog;

typedef struct high_exact_bound_param {
	double min;
	double max;
} *high_exact_bound_param_t;

static int evaluate(me_model_engine_t m, me_model_cfg_t cfg,
				me_input_t input_val, me_output_t output)
{
	if (me_get_input_type(input_val) == ME_NO_DATA)
		return 0;

	double value = me_get_input_value(input_val, msglog);

	double *thresholds = me_get_thresholds(cfg);

	high_exact_bound_param_t param = (high_exact_bound_param_t)me_get_params(cfg);
	if ((value < param->min) || (value > param->max)) {
		msglog("high_exact_bound: input value out of range\n");
		return 0;
	}

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

static void *parse_param(char *params)
{
	if (!params) {
		msglog("model_high_percentage: no given parameters\n");
		return NULL;
	}

	char *tmp = strdup(params);
	if (!tmp) {
		msglog("model_high_exact_bound: Out of memory in parse_param\n");
		return NULL;
	}
	char *tok, *ptr;
	tok = strtok_r(tmp, ",", &ptr);
	if (!tok) {
		msglog("model_high_exact_bound: Invalid parameters '%s'\n", params);
		goto err1;
	}

	high_exact_bound_param_t param = malloc(sizeof(*param));
	if (!param) {
		msglog("model_high_exact_bound: Out of memory in parse_param\n");
		goto err2;
	}
	param->min = atof((char *)tok);

	tok = strtok_r(NULL, ",", &ptr);
	if (!tok) {
		msglog("model_high_exact_bound: Invalid parameters '%s'\n", params);
		goto err2;
	}
	param->max = atof((char *)tok);

	if (param->min > param->max) {
		int tmp_min = param->max;
		param->max = param->min;
		param->min = tmp_min;
	}

	return param;
err2:
	free(param);
err1:
	free(tmp);
	return NULL;
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
	return  "   A univariate model detects too high values with given bound values.\n"
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
	.new_model_engine = new_model_engine,
	.parse_param = parse_param,
};

struct me_plugin *get_plugin(me_log_fn _log_fn)
{
	msglog = _log_fn;
	model_high_exact.base.model_pi = &model_high_exact;
	return &model_high_exact.base;
}
