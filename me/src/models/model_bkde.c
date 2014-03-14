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
 * model_bkde.c
 * Model Type: univariate
 * Thresholds: probabilities
 * Detect: too erratic in the sense of probability
 */
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <sys/queue.h>

#include "me.h"

#define BKDE_NUM_REQ_SAMPLE 100

static me_log_fn msglog;

struct data_point {
	double value;
	struct data_point *next;	/* The next data point */
	TAILQ_ENTRY(data_point) entry;
};

TAILQ_HEAD(sample_list, data_point);

struct bkde_param {
	int num_req_samples; /* The minimum number of samples before evaluating */
};

struct bkde_coef {
	int num_samples;	/* The number of samples from the producer */
	struct sample_list samples;	/* the sorted list of samples */
	struct data_point *last_sample;		/* the last sample/data point */
	struct data_point *oldest; /* the oldest sample/data point in the list */
	double max_value;	/* the maximum value of the samples */
	double min_value;	/* the minimum value of the samples */
};

static double transform_data(double min, double max, double value,
				struct sample_list *list)
{
	double dist_tail = (max - min) * 0.05;
	min = min - dist_tail;
	max = max + dist_tail;
	return log((value - min)/(max - value));
}

static double find_IQR(struct bkde_coef *coef, int num_req_samples)
{
	static double tmp = 0;
	static uint q1_int, q3_int;
	static double q1_frac, q3_frac;

	if (tmp == 0) {
		tmp = (num_req_samples + 1) * 0.25;
		q1_int = floor(tmp);
		q1_frac = tmp - q1_int;
		tmp = (num_req_samples + 1) * 0.75;
		q3_int = floor(tmp);
		q3_frac = tmp - q3_int;
	}

	int count = 0;
	struct data_point *var;
	float q1, q3;

	double min = coef->min_value;
	double max = coef->max_value;

	double t_var, t_next;

	struct sample_list *head = &(coef->samples);
	TAILQ_FOREACH(var, head, entry) {
		count++;
		if (count == q1_int) {
			t_var = transform_data(min, max, var->value, head);
			t_next = transform_data(min, max,
					TAILQ_NEXT(var, entry)->value, head);
			q1 = (1 - q1_frac) * t_var + q1_frac * t_next;
			break;
		}
	}
	count = num_req_samples;
	TAILQ_FOREACH_REVERSE(var, head, sample_list, entry) {
		if (count == q3_int) {
			t_var = transform_data(min, max, var->value, head);
			t_next = transform_data(min, max,
					TAILQ_NEXT(var, entry)->value, head);
			q3 = (1 - q3_frac) * t_var + q3_frac * t_next;
			break;
		}
		count--;
	}

	return q3 - q1;
}

static double Silverman_bandwidth(struct bkde_coef *coef, double sd,
							int num_req_samples)
{
	double iqr = find_IQR(coef, num_req_samples);
	return 0.9 * fmin(sd, iqr/1.34) / pow(coef->num_samples, 0.2);
}

static int add_sample(struct bkde_coef *coef, double value,
							int num_req_samples)
{
	struct data_point *var, *new_point, *tmp;

	new_point = malloc(sizeof(*new_point));
	if (!new_point) {
		printf("model_bkde: failed to allocate new data point.\n");
		return -1;
	}
	new_point->value = value;

	if (coef->num_samples == 0) {
		coef->oldest = new_point;
		coef->last_sample = new_point;
	} else {
		coef->last_sample->next = new_point;
		coef->last_sample = new_point;
	}

	struct sample_list *head = &(coef->samples);
	/**
	 * Removing and freeing the oldest data point
	 * in the list when the list is filled.
	 */
	if (coef->num_samples >= num_req_samples) {
		tmp = coef->oldest->next;
		TAILQ_REMOVE(head, coef->oldest, entry);
		free(coef->oldest);
		coef->oldest = tmp;
	} else {
		coef->num_samples++;
	}

	int is_inserted = 0;
	TAILQ_FOREACH(var, head, entry) {
		if (var->value >= value) {
			is_inserted = 1;
			TAILQ_INSERT_BEFORE(var, new_point, entry);
			break;
		}
	}

	if (!is_inserted)
		TAILQ_INSERT_TAIL(head, new_point, entry);

	return 0;
}

static double find_sd(struct bkde_coef *coef)
{
	struct sample_list *head = &(coef->samples);
	double mean, avg_ssq;
	mean = avg_ssq = 0;
	struct data_point *var;
	double t_value;
	TAILQ_FOREACH(var, head, entry) {
		t_value = transform_data(coef->min_value, coef->max_value,
				var->value, head);
		mean += t_value;
		avg_ssq += pow(t_value, 2);
	}
	mean = mean / coef->num_samples;
	avg_ssq = avg_ssq / (coef->num_samples - 1);
	return sqrt(avg_ssq - (coef->num_samples / (coef->num_samples - 1)
							* pow(mean, 2)));
}

static double Gaussian_kernal_CDF(struct bkde_coef *coef, double h,
								double value)
{
	struct sample_list *head = &(coef->samples);
	double tmp = 0;
	double t_value = transform_data(coef->min_value, coef->max_value,
								value, head);
	struct data_point *var;

	TAILQ_FOREACH(var, head, entry) {
		double t_var = transform_data(coef->min_value,
				coef->max_value, var->value, head);
		tmp += erf((t_value - t_var) / (h * (double)M_SQRT2));
	}

	tmp += coef->num_samples;
	return tmp / (2 * coef->num_samples);
}

int __update(struct bkde_coef *coef, double value, int num_req_samples)
{
	int rc = add_sample(coef, value, num_req_samples);
	coef->min_value = TAILQ_FIRST(&coef->samples)->value;
	coef->max_value = TAILQ_LAST(&(coef->samples), sample_list)->value;

	return rc;
}

static int evaluate(me_model_engine_t m, me_model_cfg_t cfg,
				me_input_t input, me_output_t output)
{
	if (me_get_input_type(input) == ME_NO_DATA)
		return 0;

	struct bkde_coef *coef = (struct bkde_coef *)m->mcontext;
	struct bkde_param *param = (struct bkde_param *)me_get_params(cfg);
	double sd, h, prob, value;

	value = me_get_input_value(input, msglog);

	if (coef->num_samples < param->num_req_samples) {
		/* Update the sample list*/
		__update(coef, value, param->num_req_samples);
		if (coef->num_samples < 3)
			sd = 1;
		else
			sd = find_sd(coef);
		output->level = ME_ONLY_UPDATE;
	} else {
		/* do evaluate */
		if (value < coef->min_value) {
			coef->min_value = value;
		} else {
			if (value > coef->max_value)
				coef->max_value = value;
		}

		sd = find_sd(coef);

		if (sd == 0)
			return 0;

		double delta = (coef->max_value - coef->min_value) * 0.05;

		double *thres = me_get_thresholds(cfg);

		if (delta > 0) {
			h = Silverman_bandwidth(coef, sd,
					param->num_req_samples);
			prob = Gaussian_kernal_CDF(coef, h, value + delta) -
				Gaussian_kernal_CDF(coef, h, value - delta);

			if (prob >= thres[ME_SEV_INFO])
				if (prob >= thres[ME_SEV_WARNING])
					if (prob >= thres[ME_SEV_CRITICAL])
						output->level = ME_SEV_CRITICAL;
					else
						output->level = ME_SEV_WARNING;
				else
					output->level = ME_SEV_INFO;
			else
				output->level = ME_SEV_NOMINAL;

			output->ts = me_get_timestamp(input);
		}
		__update(coef, value, param->num_req_samples);
	}
	return 0;
}

static int reset(me_model_engine_t m)
{
	struct bkde_coef *coef = (struct bkde_coef *)m->mcontext;
	coef->num_samples = 0;
	coef->last_sample = 0;
	coef->max_value = 0;
	coef->min_value = 0;
	coef->oldest = 0;
	struct sample_list *head = &(coef->samples);

	struct data_point *var;
	TAILQ_FOREACH(var, head, entry) {
		TAILQ_REMOVE(head, var, entry);
		free(var);
	}

	return 0;
}

static void term()
{
}

static const char *usage()
{
	return  "  A univariate model detects outliers of a metric based on its empirical distribution\n"
		"	create name=model_bkde model_id=<model_id> thresholds=<thresholds>\n"
		"		[params=<num_req_samples>]\n"
		"	   -  num_req_samples   The number of samples before the evaluate starts.\n"
		"				The default is 100\n";
}

static void *parse_param(char *_param)
{
	struct bkde_param *param = malloc(sizeof(*param));

	if (!_param)
		param->num_req_samples = BKDE_NUM_REQ_SAMPLE;
	else
		param->num_req_samples = atoi((char *)param);

	return param;
}

me_model_engine_t new_model_engine(me_model_cfg_t cfg)
{
	struct bkde_coef *coef = calloc(1, sizeof(*coef));
	TAILQ_INIT(&(coef->samples));
	struct me_model_engine *meg = calloc(1, sizeof(*meg));
	meg->evaluate = evaluate;
	meg->reset_state = reset;
	meg->mcontext = (void *)coef;
	return meg;
}

struct me_model_plugin model_bkde = {
	.base = {
			.name = "model_bkde",
			.term = term,
			.type = ME_MODEL_PLUGIN,
			.usage = usage,
		},
	.parse_param = parse_param,
	.new_model_engine = new_model_engine
};

struct me_plugin *get_plugin(me_log_fn log_fn)
{
	msglog = log_fn;
	model_bkde.base.model_pi = &model_bkde;
	return &model_bkde.base;
}
