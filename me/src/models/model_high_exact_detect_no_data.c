/**
 * model_high_exact_detect_no_data.c
 * Model Type: univariate
 * Thresholds: exact values
 * Detect: too high value. The model is configurable what severity info to be
 * reported when the NO_DATA flag is detected.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <stddef.h>
#include <string.h>
#include "me.h"

static me_log_fn msglog;

typedef struct high_exact_detect_no_data_param {
	enum me_severity_level no_data_level;
} *high_exact_detect_no_data_param_t;

static int evaluate(me_model_engine_t m, me_model_cfg_t cfg,
				me_input_t input_val, me_output_t output)
{
	if (me_get_input_type(input_val) == ME_NO_DATA) {
		/*
		 * Set the model state to INFO and notify Komondor that we lost
		 * connection
		 */
		high_exact_detect_no_data_param_t param =
			(high_exact_detect_no_data_param_t)me_get_params(cfg);
		output->level = param->no_data_level;
		goto out;
	}

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
out:
	output->ts = me_get_timestamp(input_val);
	return 0;
}

static int reset()
{
	return 0;
}

static void *parse_param(char *param_s) {
	high_exact_detect_no_data_param_t param = malloc(sizeof(*param));
	if (!param) {
		msglog("high_exact_detect_no_data: Out of memory in parse_param\n");
		return NULL;
	}

	if (0 == strcasecmp(param_s, "info")) {
		param->no_data_level = ME_SEV_INFO;
	} else if (0 == strcasecmp(param_s, "nominal")) {
		param->no_data_level = ME_SEV_NOMINAL;
	} else if (0 == strcasecmp(param_s, "warning")) {
		param->no_data_level = ME_SEV_WARNING;
	} else if (0 == strcasecmp(param_s, "critical")) {
		param->no_data_level = ME_SEV_CRITICAL;
	} else {
		msglog("high_exact_detect_no_data: Invalid parameter '%s'", param_s);
		free(param);
		return NULL;
	}
	return param;
}

static void term()
{
}

static const char *usage()
{
	return  "   A univariate model detects too high values and set the severity level to the configured level if the NO_DATA flag is detected.\n"
		"   A too high value is greater than one of the given thresholds.\n"
		"	create name=model_high_exact_detect_no_data model_id=<model_id> thresholds=<values>\n"
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

struct me_model_plugin model_high_exact_detect_no_data = {
	.base = {
			.name = "model_high_exact_detect_no_data",
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
	model_high_exact_detect_no_data.base.model_pi = &model_high_exact_detect_no_data;
	return &model_high_exact_detect_no_data.base;
}


