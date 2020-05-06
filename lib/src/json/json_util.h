/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2018 Open Grid Computing, Inc. All rights reserved.
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
#ifndef _JSON_UTIL_H_
#define _JSON_UTIL_H_
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <coll/htbl.h>
#include <stdarg.h>

typedef struct json_str_s *json_str_t;
typedef struct json_attr_s *json_attr_t;
typedef struct json_list_s *json_list_t;
typedef struct json_dict_s *json_dict_t;
typedef struct json_entity_s *json_entity_t;

enum json_value_e {
	JSON_INT_VALUE,
	JSON_BOOL_VALUE,
	JSON_FLOAT_VALUE,
	JSON_STRING_VALUE,
	JSON_ATTR_VALUE,
	JSON_LIST_VALUE,
	JSON_DICT_VALUE,
	JSON_NULL_VALUE
};

struct json_entity_s {
	enum json_value_e type;
	union {
		int bool_;
		int64_t int_;
		double double_;
		json_str_t str_;
		json_attr_t attr_;
		json_list_t list_;
		json_dict_t dict_;
	} value;
	TAILQ_ENTRY(json_entity_s) item_entry;
};

struct json_str_s {
	struct json_entity_s base;
	char *str;
	size_t str_len;
};

struct json_list_s {
	struct json_entity_s base;
	int item_count;
	TAILQ_HEAD(json_item_list, json_entity_s) item_list;
};

struct json_attr_s {
	struct json_entity_s base;
	json_entity_t name;
	json_entity_t value;
	struct hent attr_ent;
};

struct json_dict_s {
	struct json_entity_s base;
	htbl_t attr_table;
};

struct json_loc_s {
	int first_line;
	int first_column;
	int last_line;
	int last_column;
	char *filename;
};

#ifndef YY_TYPEDEF_YY_SCANNER_T
typedef void *yyscan_t;
#endif
typedef struct json_parser_s {
	yyscan_t scanner;
	struct yy_buffer_state *buffer_state;
} *json_parser_t;

extern json_parser_t json_parser_new(size_t user_data);
extern void json_parser_free(json_parser_t p);
extern int json_verify_string(char *s);
extern void json_entity_free(json_entity_t e);
extern const char *json_type_name(enum json_value_e typ);
extern enum json_value_e json_entity_type(json_entity_t e);
extern json_entity_t json_attr_find(json_entity_t d, char *name);
extern json_entity_t json_attr_first(json_entity_t d);
extern json_entity_t json_attr_next(json_entity_t a);
extern int json_parse_buffer(json_parser_t p, char *buf, size_t buf_len, json_entity_t *e);

extern json_entity_t json_entity_new(enum json_value_e type, ...);

extern size_t json_list_len(json_entity_t l);
extern void json_item_add(json_entity_t a, json_entity_t e);
extern void json_attr_add(json_entity_t d, json_entity_t a);
extern json_entity_t json_item_first(json_entity_t a);
extern json_entity_t json_item_next(json_entity_t i);
extern json_str_t json_attr_name(json_entity_t a);
extern json_entity_t json_attr_value(json_entity_t a);

extern json_entity_t json_value_find(json_entity_t d, char *name);
extern int64_t json_value_int(json_entity_t e);
extern int json_value_bool(json_entity_t e);
extern double json_value_float(json_entity_t e);
extern json_str_t json_value_str(json_entity_t e);
extern json_dict_t json_value_dict(json_entity_t e);
extern json_list_t json_value_list(json_entity_t e);

typedef struct jbuf_s {
	size_t buf_len;
	int cursor;
	char buf[0];
} *jbuf_t;

extern jbuf_t jbuf_new(void);
extern jbuf_t jbuf_append_attr(jbuf_t jb, const char *name, const char *fmt, ...);
extern jbuf_t jbuf_append_str(jbuf_t jb, const char *fmt, ...);
extern jbuf_t jbuf_append_va(jbuf_t jb, const char *fmt, va_list ap);
extern void jbuf_free(jbuf_t jb);

#define YYSTYPE json_entity_t
#endif
