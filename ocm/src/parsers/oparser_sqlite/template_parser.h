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
 * template_parser.h
 *
 *  Created on: Oct 17, 2013
 *      Author: nichamon
 */

#ifndef TEMPLATE_PARSER_H_
#define TEMPLATE_PARSER_H_

#include <sys/queue.h>
#include <stdio.h>

#include "oparser_util.h"

struct set {
	char *sampler_pi; /** ldmsd sampler plugin name */
	char cfg[1024];
	struct metric_list mlist;
	uint8_t is_synchronous;
	LIST_ENTRY(set) entry;
};
LIST_HEAD(set_list, set);

struct template {
	struct oparser_component *comp;
	struct set_list slist;
};

struct template_def {
	char *name;
	struct oparser_component_type *comp_type;
	struct template *templates;
	int num_tmpls;
	LIST_ENTRY(template_def) entry;
};
LIST_HEAD(template_def_list, template_def);

struct prod_comps {
	struct oparser_component_type *comp_type;
	struct oparser_component_list clist;
};

void oparser_template_parser_init(FILE *_log_fp);

struct template_def_list *oparser_parse_template(FILE *conf,
				struct oparser_scaffold *scaffold);

void oparser_metrics_to_sqlite(struct template_def_list *tmpl_def_list,
								sqlite3 *db);

void oparser_templates_to_sqlite(struct template_def_list *tmpl_def_list,
								sqlite3 *db);

#endif /* TEMPLATE_PARSER_H_ */
