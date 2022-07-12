/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2018,2020 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2018,2020 Open Grid Computing, Inc. All rights reserved.
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
#ifndef _OVIS_JSON_H_
#define _OVIS_JSON_H_
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

typedef void *yyscan_t;
typedef struct json_parser_s {
	yyscan_t scanner;
	struct yy_buffer_state *buffer_state;
} *json_parser_t;

typedef struct jbuf_s {
	size_t buf_len;
	int cursor;
	char buf[0];
} *jbuf_t;

extern json_parser_t json_parser_new(size_t user_data);
extern void json_parser_free(json_parser_t p);
extern int json_verify_string(char *s);
extern void json_entity_free(json_entity_t e);
extern const char *json_type_name(enum json_value_e type);
extern enum json_value_e json_entity_type(json_entity_t e);

extern int json_parse_buffer(json_parser_t p, char *buf, size_t buf_len, json_entity_t *e);

extern json_entity_t json_entity_new(enum json_value_e type, ...);

/**
 * \brief Dump the json entity into a json buffer (\c jbuf_t)
 *
 * This function can be used to appended json string to an existing
 * json buffer.
 *
 * \param jb	\c jbuf_t to hold the string.
 *		If NULL is given, a new \c jbuf_t is allocated.
 *		If it is not empty, the json string of \c e will
 *		will appended to the buffer.
 * \param e	a json entity
 *
 * \return a json buffer -- \c jbuf_t -- is returned.
 * \see jbuf_free
 */
extern jbuf_t json_entity_dump(jbuf_t jb, json_entity_t e);

/**
 * \brief Create a new JSON entity identical to the given entity \c e.
 *
 * \param e     The original JSON entity
 *
 * \return a new JSON entity. NULL is returned if an out-of-memory error occurs.
 */
extern json_entity_t json_entity_copy(json_entity_t e);

/*
 * \brief Build or append a dictionary with the given list of its attribute value pairs.
 *
 * If \c d is NULL, a new dictionary with the given attribute value list.
 * If \c d is not NULL, the given attribute value list will be added to \c d.
 *
 * The format of the attribute value pair in the list is
 * <JSON value type>, <attribute name>, <attribute value>.
 *
 * The last value must be -1 to end the attribute value list.
 *
 * If the value type is JSON_LIST_VALUE, it must end with -2.
 *
 * \example
 *
 * attr = json_entity_new(JSON_ATTR_NAME, "attr", json_entity_new(JSON_STRING_VALUE, "my attribute"))
 * d = json_dict_build(NULL,
 * 	JSON_INT_VALUE,    "int",    1,
 * 	JSON_BOOL_VALUE,   "bool",   1,
 * 	JSON_FLOAT_VALUE,  "float",  1.1,
 * 	JSON_STRING_VALUE, "string", "str",
 * 	JSON_LIST_VALUE,   "list",   JSON_INT_VALUE, 1,
 * 				     JSON_STRING_VALUE, "last",
 * 				     -2,
 * 	JSON_DICT_VALUE,   "dict",   JSON_INT_VALUE, "attr1", 2,
 * 				     JSON_BOOL_VALUE, "attr2", 0,
 * 				     JSON_STRING_VALUE, "attr3", "last attribute",
 * 				     -2,
 * 	JSON_ATTR_VALUE, attr,
 * 	-1
 * 	);
 *
 * Note: attr is "attr": "my attribute".
 *
 * The result dictionary is
 * { "int":    1,
 *   "bool":   true,
 *   "float":  1.1,
 *   "string": "str",
 *   "list":   [1, "last" ],
 *   "dict":   { "attr1": 2,
 *               "attr2": 0,
 *               "attr3": "last attribute"
 *             },
 *   "attr":   "my attribute"
 * }
 *
 */
extern json_entity_t json_dict_build(json_entity_t d, ...);

/**
 * \brief Add the attributes of dictionary \c src into dictionary \c dst
 *
 * The attribute value in \c src will replace the value in \c dst,
 * if the attribute key presents in both dictionaries. \c dst will be modified
 * and \c src is left unchanged.
 *
 * \return 0 on success. ENOMEM if the function fails to add a new attribute to \c src
 */
extern int json_dict_merge(json_entity_t dst, json_entity_t src);

/**
 * \brief Return the ATTR entity of the given name \c name
 *
 * \param name    The attribute name
 *
 * \return a JSON entity. NULL is returned if no attributes of the given name exist.
 */
extern json_entity_t json_attr_find(json_entity_t d, const char *name);

/*
 * \brief Return the JSON entity of the attribute value
 *
 * \param name   The attribute name
 *
 * \return A json entity. NULL is returned if no attributes of the given name exist.
 */
extern json_entity_t json_value_find(json_entity_t d, const char *name);
extern json_entity_t json_attr_first(json_entity_t d);
extern json_entity_t json_attr_next(json_entity_t a);

/**
 * \brief Return the number of attributes in the dictionary \c d
 */
extern int json_attr_count(json_entity_t d);

/**
 * \brief Add an attribute of key \c name and value \c v to the JSON dict \c d
 *
 * If the attribute name already exists, its value is replaced with the given
 * attribute value.
 *
 * \param d      JSON dictionary entity
 * \param name   attribute name
 * \param v      attribute value entity
 *
 * \return 0 on success. Otherwise, an errno is returned.
 *
 * \see json_entity_new, json_attr_rem
 */
extern int json_attr_add(json_entity_t d, const char *name, json_entity_t v);
extern json_str_t json_attr_name(json_entity_t a);
extern json_entity_t json_attr_value(json_entity_t a);

/**
 * \brief Remove the attribute of \c name from the dictionary \c d and free it
 *
 * \param d      JSON dictionary entity
 * \param name   attribute name
 *
 * \return 0 on success. ENOENT is returned if no attributes of \c name exist.
 */
extern int json_attr_rem(json_entity_t d, char *name);

/**
 * \brief Return the number of elements in the list \c l
 */
extern size_t json_list_len(json_entity_t l);
extern void json_item_add(json_entity_t a, json_entity_t e);
extern json_entity_t json_item_first(json_entity_t a);
extern json_entity_t json_item_next(json_entity_t i);

/**
 * \brief Remove and return the element at index \c idx from the list \c a
 *
 * \param a      JSON list entity
 * \param idx    List index
 *
 * \param a JSON entity. NULL if \c idx is out of range.
 */
extern json_entity_t json_item_pop(json_entity_t a, int idx);

/**
 * \brief Remove \c item from list \c l
 *
 * The address of \c item, rather than the content of \c item, is used to
 * compare with the elements in the list.
 *
 * If there are no elements in the list \c l that have the same address as \c item,
 * ENOENT is returned.
 *
 * The caller is responsible for freeing \c item.
 *
 * The memory of \c item is not freed.
 *
 * \param l       JSON list entity
 * \param item    A list element
 *
 * \return 0 on success. ENOENT if \c item is not found.
 */
extern int json_item_rem(json_entity_t l, json_entity_t item);

extern int64_t json_value_int(json_entity_t e);
extern int json_value_bool(json_entity_t e);
extern double json_value_float(json_entity_t e);

/* get the string and length object. */
extern json_str_t json_value_str(json_entity_t e);

/* \return the underlying null terminated string pointer
 * if e is a string type, or NULL if it is not.  If NULL,
 * errno will also be set. */
extern const char *json_value_cstr(json_entity_t e);

extern json_dict_t json_value_dict(json_entity_t e);
extern json_list_t json_value_list(json_entity_t e);

extern jbuf_t jbuf_new(void);
/**
 * \brief Add the named attribute (as formatted with fmt) to jb.
 * The JSON formatting "name":<formatted_data> is automatically applied.
 * The fmt argument must include escaped double-quotes if it is a string.
 * Note: Repeated calls to this are slower than a single call to
 * jbuf_append_str with a complex argument list, but more flexible.
 * \return updated pointer for jb, or NULL if realloc fails.
 */
extern jbuf_t jbuf_append_attr(jbuf_t jb, const char *name, const char *fmt, ...);

/** \brief Extend jbuf by the output of formatting with fmt.
 * The fmt argument must include all required JSON elements, which
 * may be {} [] : , and ". No validation is applied.
 * Note: Automatically extends jbuf space as needed with realloc.
 * \return updated pointer for jb, or NULL if realloc fails.
 */
extern jbuf_t jbuf_append_str(jbuf_t jb, const char *fmt, ...);

/**
 * \brief Var args version of jbuf_append_str.
 */
extern jbuf_t jbuf_append_va(jbuf_t jb, const char *fmt, va_list ap);
/**
 * Destroy result of jbuf_new.
 */
extern void jbuf_free(jbuf_t jb);
/**
 * Change jb to contain 0-length string data, without modifying
 * the currently allocated buffer memory.
 */
extern void jbuf_reset(jbuf_t jb);

#endif
