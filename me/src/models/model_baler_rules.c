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
 * Model Baler association rules
 * Model Type: multivariate
 * input: only ptn
 * Thresholds: chance that an event will occur
 * Detect: event
 *
 *
 * Assumption: The rules in the file are sorted by their predecessor sets
 * 		and the predecessor sets are sorted. For example,
 *	1,3,5-100
 *	1,5,6-99
 *	2,11,50-100
 *	10,11,50-100
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/mman.h>
#include <errno.h>
#include <time.h>
#include <malloc.h>

#include "me.h"

#define BALER_MAX_PTN_IDS 65536
#define NOT_OCCURRED 0
#define OCCURRED 1

static me_log_fn msglog;

struct baler_rule_param {
	char *rule_path;
	double window_size; /* Window size in minutes */
};
typedef struct baler_rule_param *baler_rule_param_t;

struct antecedent {
	uint64_t ptn_id;
	char state;	/* 1 occurred, 0 not occurred*/
	TAILQ_ENTRY(antecedent) entry;
};

struct baler_rule {
	uint32_t num_atcds; /* number of predecessors */
	TAILQ_HEAD(antecedent_list, antecedent) atcd_states;	/* the states of all antecedents */
	double conf_level;	/* Confidence level */
	uint32_t num_occurred; /* number of occurred predecessors */
	TAILQ_ENTRY(baler_rule) entry;
};

struct baler_ptn_id {
	struct timeval ts;
	struct antecedent *occurrence; /* Pointer to the antecedent in the occurring queue */
	TAILQ_HEAD(rule_list, baler_rule) rule_list_h;
};

typedef struct baler_rules_ctxt {
	struct baler_ptn_id *ptn_ids; /* array of all pattern ids */
	TAILQ_HEAD(atcd_q, antecedent) occ_q_h; /* Queue of occurred antecedents */
} *baler_rules_ctxt_t;

/**
 * \brief Update a rule for a new antecedent occurrence
 */
int __update_not_occurred_state(struct baler_rule *rule, uint64_t ptn_id)
{
	if (rule->num_occurred >= rule->num_atcds) {
		msglog("model_baler_rules: Internal error "
				"on updating model states\n");
		return -1;
	}
	struct antecedent *atcd;
	TAILQ_FOREACH(atcd, &rule->atcd_states, entry) {
		if (atcd->ptn_id == ptn_id) {
			if (atcd->state == NOT_OCCURRED) {
				atcd->state = OCCURRED;
				rule->num_occurred++;
			}
			break;
		}
	}
	return 0;
}

/**
 * \brief Update a rule for too old antecedent occurrence
 */
int __update_occurred_state(struct baler_rule *rule, uint64_t ptn_id)
{
	if (rule->num_occurred < 0) {
		msglog("model_baler_rules: Internal error "
				"on updating model states\n");
		return -1;
	}
	struct antecedent *atcd;
	TAILQ_FOREACH(atcd, &rule->atcd_states, entry) {
		if (atcd->ptn_id == ptn_id) {
			if (atcd->state == OCCURRED) {
				atcd->state = NOT_OCCURRED;
				rule->num_occurred--;
			}
			break;
		}
	}
	return 0;
}

/**
 * \brief Clear out the antecedents that are out of the window
 * @param[in]	ts	the timestamp of the latest received input
 * @param[in]	ptn_id	the ptn_id of the received input
 */
int __remove_old_antecedent(baler_rules_ctxt_t ctxt, struct timeval *ts,
					double win_size, uint64_t ptn_id)
{
	struct antecedent *occurrence;
	struct baler_ptn_id *b_ptn_id;
	struct timeval res;
	struct baler_rule *rule;

	occurrence = TAILQ_FIRST(&ctxt->occ_q_h);
	while (occurrence) {
		b_ptn_id = &(ctxt->ptn_ids[occurrence->ptn_id]);
		timersub(ts, &(b_ptn_id->ts), &res);
		/*
		 * Ignore the microsecond unit. The error is at most 1 second.
		 */
		if ((res.tv_sec / 60.0) > win_size) {
			TAILQ_REMOVE(&ctxt->occ_q_h, occurrence, entry);
			b_ptn_id->ts.tv_sec = b_ptn_id->ts.tv_usec = 0;
			TAILQ_FOREACH(rule, &b_ptn_id->rule_list_h, entry) {
				if (__update_occurred_state(rule,
							occurrence->ptn_id))
					return -1;
			}
			free(occurrence);
			b_ptn_id->occurrence = NULL;
		} else {
			break;
		}
		occurrence = TAILQ_FIRST(&ctxt->occ_q_h);
	}
	return 0;
}


void __reset_rule(struct baler_rule *rule)
{
	struct antecedent *state;
	if (rule->num_occurred)
		TAILQ_FOREACH(state, &rule->atcd_states, entry)
			state->state = NOT_OCCURRED;
	rule->num_occurred = 0;
}

static int evaluate(me_model_engine_t m, me_model_cfg_t cfg,
				me_input_t input_val, me_output_t output)
{
	int rc = 0;
	baler_rules_ctxt_t ctxt = (baler_rules_ctxt_t)m->mcontext;
	uint64_t ptn_id = me_get_input_value(input_val, msglog);
	baler_rule_param_t param = (baler_rule_param_t)me_get_params(cfg);

	/* The ptn_id of the input is greater than the max number of ptn_ids. */
	if (ptn_id > BALER_MAX_PTN_IDS -1) {
		msglog("model_baler_rules: ptn_id '%d' is out of bound (%d).\n",
				ptn_id, BALER_MAX_PTN_IDS);
		return -1;
	}

	struct timeval ts = me_get_timestamp(input_val);
	/* Clear out all occurrences that spilled out of the window */
	__remove_old_antecedent(ctxt, &ts, param->window_size, ptn_id);

	struct baler_ptn_id *b_ptn_id = &(ctxt->ptn_ids[ptn_id]);

	/* no rules for the input */
	if (TAILQ_EMPTY(&b_ptn_id->rule_list_h))
		goto update_only;

	/*
	 * Update the timestamp and
	 * remove the previous occurrence from the queue
	 * NOTE: no need to update rule states because
	 * the states are still OCCURRED
	 */
	b_ptn_id->ts = ts;
	if (b_ptn_id->occurrence) {
		TAILQ_REMOVE(&ctxt->occ_q_h, b_ptn_id->occurrence, entry);
		free(b_ptn_id->occurrence);
		b_ptn_id->occurrence = NULL;
	}

	/*
	 * Create a new occurrence for the input, and
	 * insert it to the occurrence queue.
	 * The timestamp of the b_ptn_id is updated, so the occurrence
	 *  of the old timestamp is removed from the occurrence queue
	 */
	struct antecedent *occurrence = malloc(sizeof(*occurrence));
	occurrence->ptn_id = ptn_id;
	TAILQ_INSERT_TAIL(&ctxt->occ_q_h, occurrence, entry);

	b_ptn_id->occurrence = occurrence;

	/* evaluate */
	struct baler_rule *rule;
	double conf_level = 0;
	/*
	 * This b_ptn_id definitely has at least one rule.
	 * Otherwise, it will go to update_only.
	 */
	TAILQ_FOREACH(rule, &b_ptn_id->rule_list_h, entry) {
		if (rc = __update_not_occurred_state(rule, ptn_id))
			return rc;
		if (rule->num_occurred == rule->num_atcds) {
			if (conf_level < rule->conf_level)
				conf_level = rule->conf_level;
			__reset_rule(rule);
		}
	}

	double *thresholds = me_get_thresholds(cfg);

	if (conf_level == 0)
		goto update_only;
	else
		if (conf_level >= thresholds[ME_SEV_INFO])
			if (conf_level >= thresholds[ME_SEV_WARNING])
				if (conf_level >= thresholds[ME_SEV_CRITICAL])
					output->level = ME_SEV_CRITICAL;
				else
					output->level = ME_SEV_WARNING;
			else
				output->level = ME_SEV_INFO;
		else
			output->level = ME_SEV_NOMINAL;
	output->ts = ts;
	return 0;

update_only:
 	output->level = ME_ONLY_UPDATE;
	return 0;
}

static int reset(me_model_engine_t eng)
{
	baler_rules_ctxt_t ctxt = (baler_rules_ctxt_t)eng->mcontext;
	struct antecedent *occurrence;
	struct baler_ptn_id *b_ptn_id;
	struct baler_rule *rule;
	while (!TAILQ_EMPTY(&ctxt->occ_q_h)) {
		occurrence = TAILQ_FIRST(&ctxt->occ_q_h);
		b_ptn_id = &(ctxt->ptn_ids[occurrence->ptn_id]);
		TAILQ_FOREACH(rule, &b_ptn_id->rule_list_h, entry) {
			__reset_rule(rule);
		}
		TAILQ_REMOVE(&ctxt->occ_q_h, occurrence, entry);
		free(occurrence);
		b_ptn_id->occurrence = NULL;
		b_ptn_id->ts.tv_sec = b_ptn_id->ts.tv_usec = 0;
	}
	return 0;
}

static void term()
{
}

static const char *usage()
{
	return  "   A univariate model evaluates only Baler Patterns according to Baler Association rules\n"
		"   The metric ID gives to this model must be Baler metric ID.\n"
		"	create model=model_baler_rules model_id=<model_id> thresholds=<thresholds>\n"
		"		params=<path,window_size>\n"
		"	   -  path		path to the rule file.\n"
		"	   -  window_size	the size of window used in rule mining\n"
		"				d=day, h=hour, m=minute(default), s=second\n"
		"	model model_id=<exist model_id> metric_ids=<Baler metric ID>\n"
		"	   -  Baler metric ID	The metric ID of baler patterns\n";
}

static void *parse_param(char *param)
{
	if (!param) {
		msglog("model_baler_rules: rule_file_path,window_size\n");
		return NULL;
	}

	char win_unit;
	char tmp[256];
	char file_path[256];
	double win_size;

	sprintf(tmp, "%s%s", param, "m");
	sscanf(tmp, "%[^,],%lf%c", file_path, &win_size, &win_unit);

	struct baler_rule_param *b_param = malloc(sizeof(*b_param));

	b_param->rule_path = strdup(file_path);
	if (!b_param->rule_path) {
		msglog("Baler rule model: ENOMEM from strdup '%s'\n", file_path);
		free(b_param);
		return NULL;
	}

	/**
	 * TODO: think about overflow problem
	 */
	switch (win_unit) {
	case 'd':
		b_param->window_size = win_size * 1440.0;
		break;
	case 'h':
		b_param->window_size = win_size * 60.0;
		break;
	case 's':
		b_param->window_size = win_size / 60.0;
		break;
	case 'm':
		b_param->window_size = win_size;
		break;
	default:
		msglog("Baler rule model: Invalid window size "
				"unit '%c'\n", &win_unit);
		free(b_param->rule_path);
		free(b_param);
		return NULL;
	}

	return b_param;
}

void __destroy_rules(struct baler_ptn_id *preds)
{
	int ptn_id;
	struct baler_rule *rule;
	for (ptn_id = 0; ptn_id < BALER_MAX_PTN_IDS; ptn_id++)
 		if (!TAILQ_EMPTY(&preds[ptn_id].rule_list_h))
 			TAILQ_FOREACH(rule, &preds[ptn_id].rule_list_h, entry)
 				TAILQ_REMOVE(&preds[ptn_id].rule_list_h,
 							rule, entry);
 				free(rule);
}

struct baler_rule *__alloc_rule()
{
	struct baler_rule *rule = malloc(sizeof(*rule));
	if (!rule) {
		msglog("Baler rule model: failed to allocate a rule. "
				"ERROR '%d'\n", ENOMEM);
		return NULL;
	}
	TAILQ_INIT(&rule->atcd_states);
	rule->num_atcds = 0;
	rule->num_occurred = 0;
	return rule;
}

/**
 * Assumption: no duplicated antecedents are given
 */
void __add_atcd(uint64_t ptn_id, struct baler_rule *rule)
{
	struct antecedent *atcd = malloc(sizeof(*atcd));
	atcd->ptn_id = ptn_id;
	atcd->state = 0;
	TAILQ_INSERT_TAIL(&rule->atcd_states, atcd, entry);
	rule->num_atcds++;
}

int __load_rules(baler_rules_ctxt_t ctxt, baler_rule_param_t param)
{
	TAILQ_INIT(&ctxt->occ_q_h);
	char delim[] = ",";

	char *line, *tmp;
	size_t len = 0;
	ssize_t read;

	char antecedents[128];
	int ptn_id, conq;
	double conf;
	size_t size = BALER_MAX_PTN_IDS * sizeof(struct baler_ptn_id);

	ctxt->ptn_ids = mmap(NULL, size, PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

	struct baler_ptn_id *ptn_ids = ctxt->ptn_ids;
	struct baler_rule *rule;

	FILE *rule_f = fopen(param->rule_path, "r");
	if (!rule_f) {
		msglog("baler_rules: Couldn't open the rule file '%s'.\n",
							param->rule_path);
		return errno;
	}

	/* Read a line from the rule file */
	while ((read = getline(&line, &len, rule_f)) != -1) {

		/*
		 * Scan for the predecessor string, the pattern ID of the consequence,
		 * and the confidence level
		 */
		if (!sscanf(line, "%[^-]-%d(%lf)", antecedents, &conq, &conf)) {
			msglog("baler_rules: failed to load the rules. "
						"ERROR: '%d'\n", errno);
			goto err;
		}

		/* Allocate a rule */
		rule = __alloc_rule();
		if (!rule)
			goto err;
		rule->conf_level = conf;

		/*
		 * Parse the antecedent string to get the pattern IDs of
		 * all antecedents
		 */
		tmp = strtok(antecedents, delim);
		while (tmp) { 		/* For each antecedent */
			ptn_id = atoi(tmp);
			__add_atcd(ptn_id, rule);
			if (TAILQ_EMPTY(&ptn_ids[ptn_id].rule_list_h))
				TAILQ_INIT(&ptn_ids[ptn_id].rule_list_h);
			TAILQ_INSERT_TAIL(&ptn_ids[ptn_id].rule_list_h, rule, entry);

			tmp = strtok(NULL, delim);
		}
	}
	fclose(rule_f);
	return 0;
 err:
 	__destroy_rules(ptn_ids);
 	munmap(ctxt->ptn_ids, size);
 	return -1;
}

me_model_engine_t new_model_engine(me_model_cfg_t cfg)
{
	struct baler_rules_ctxt *ctxt = calloc(1, sizeof(*ctxt));
	if (!ctxt)
		return NULL;
	if (__load_rules(ctxt, (baler_rule_param_t)me_get_params(cfg))) {
		free(ctxt);
		return NULL;
	}
	struct me_model_engine *eng = calloc(1, sizeof(*eng));
	if (!eng)
		return NULL;
	eng->evaluate = evaluate;
	eng->reset_state = reset;
	eng->mcontext = (void *)ctxt;
	return eng;
}

struct me_model_plugin model_baler_rules = {
	.base = {
			.name = "model_baler_rules",
			.term = term,
			.type = ME_MODEL_PLUGIN,
			.usage = usage
		},
	.parse_param = parse_param,
	.new_model_engine = new_model_engine
};

struct me_plugin *get_plugin(me_log_fn log_fn)
{
	msglog = log_fn;
	model_baler_rules.base.model_pi = &model_baler_rules;
	return &model_baler_rules.base;
}
