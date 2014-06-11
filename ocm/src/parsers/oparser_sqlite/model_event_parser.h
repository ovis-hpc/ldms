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
/*
 * model_event_parser.h
 *
 *  Created on: Oct 22, 2013
 *      Author: nichamon
 */

#ifndef MODEL_EVENT_PARSER_H_
#define MODEL_EVENT_PARSER_H_

#include <sys/queue.h>

enum mae_me_sevl {
	MAE_NOMINAL = 0,
	MAE_INFO,
	MAE_WARNING,
	MAE_CRITICAL,
	MAE_NUM_LEVELS
};

struct oparser_model {
	char *name;
	uint16_t model_id;
	char *thresholds;
	char *params;
	char report_flags[MAE_NUM_LEVELS + 1];
	TAILQ_ENTRY(oparser_model) entry;
};
TAILQ_HEAD(oparser_model_q, oparser_model);

struct oparser_action {
	char *name;
	char *execute;
	TAILQ_ENTRY(oparser_action) entry;
};
TAILQ_HEAD(oparser_action_q, oparser_action);

struct oparser_sev_level {
	char *msg;
	char action_name[512];
};

struct metric_id_s {
	uint64_t *metric_ids;
	int num_metric_ids;
	LIST_ENTRY(metric_id_s) entry;
};
LIST_HEAD(metric_id_list, metric_id_s);

struct oparser_event {
	int event_id;
	char *name;
	char *ctype;
	uint16_t model_id;
	struct metric_id_list mid_list;
	int num_metric_id_set;
	struct oparser_sev_level msg_level[MAE_NUM_LEVELS];
	uint32_t fake_mtype_id;	/* fake metric type id for a user event */
	TAILQ_ENTRY(oparser_event) entry;
	TAILQ_ENTRY(oparser_event) uevent_entry;
};
TAILQ_HEAD(oparser_event_q, oparser_event);

void oparser_mae_parser_init(sqlite3 *_db, char *read_buf, char *value_buf);

void oparser_models_to_sqlite();
void oparser_actions_to_sqlite();
void oparser_events_to_sqlite();

#endif /* MODEL_EVENT_PARSER_H_ */
