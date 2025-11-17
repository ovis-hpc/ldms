/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2025 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2025 Open Grid Computing, Inc. All rights reserved.
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
#ifndef __OVIS_JSON_PRIV_H__
#define __OVIS_JSON_PRIV_H__

#include <ovis_json.h>

struct json_entity_s;
TAILQ_HEAD(json_item_list, json_entity_s);
struct json_entity_s {
	enum json_value_e type;
	json_parser_t parser;
	union {
		int32_t bool_;
		int64_t int_;
		double double_;
		struct json_str_s {
			char str_[JSON_ATTR_NAME_MAX+1];
			char *str;
			size_t str_len;
		} str_;
		struct json_attr_s {
			char name[JSON_ATTR_NAME_MAX+1];
			json_entity_t value;
			struct rbn attr_rbn;
		} attr_;
		struct json_list_s {
			int item_count;
			struct json_item_list item_list;
		} list_;
		struct json_dict_s {
			struct rbt attr_tree;
		} dict_;
	} value;
	TAILQ_ENTRY(json_entity_s) item_entry;
};

typedef struct json_lexer_state_s {
	int cur_line;		/* line in buffer */
	int cur_loc;		/* offset in line */
	int cur_pos;		/* offset in input */
} *json_lexer_state_t;

struct json_entity_entry_s
{
	struct json_entity_s e;
	LIST_ENTRY(json_entity_entry_s) entry;
};

struct json_parser_s {
	struct json_lexer_state_s state;
	regex_t f_regex;	/* for floating point */
	pthread_mutex_t entity_lock;
	size_t entity_cache;
	LIST_HEAD(json_entity_list, json_entity_entry_s) entity_list;
};

#endif
