/*
 * me_util.c
 *
 *  Created on: Jul 11, 2013
 *      Author: nichamon
 */

#include "me_priv.h"

uint64_t me_get_input_value(me_input_t _input, me_log_fn msglog)
{
	struct me_input *input = (struct me_input *)_input;
	return input->value;
}

struct timeval me_get_timestamp(me_input_t input)
{
	return ((struct me_input *)input)->ts;
}

double *me_get_thresholds(me_model_cfg_t cfg)
{
	return ((struct me_model_cfg *)cfg)->thresholds;
}

void *me_get_params(me_model_cfg_t cfg)
{
	return ((struct me_model_cfg *)cfg)->params;
}

void me_set_input_value_type(me_input_t input, enum me_input_type type)
{
	((struct me_input *)input)->type = type;
}

void me_set_input_id(me_input_t input, uint64_t id)
{
	((struct me_input *)input)->metric_id = id;
}

void me_set_input_timestamp(me_input_t input, struct timeval ts)
{
	((struct me_input *)input)->ts = ts;
}
