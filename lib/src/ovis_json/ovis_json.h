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
#include <sys/queue.h>
#include <coll/rbt.h>
#include <stdarg.h>
#include <regex.h>

typedef struct json_str_s *json_str_t;
typedef struct json_attr_s *json_attr_t;
typedef struct json_list_s *json_list_t;
typedef struct json_dict_s *json_dict_t;
typedef struct json_entity_s *json_entity_t;
typedef struct json_doc_s *json_doc_t;

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
#define JSON_EOL_VALUE	-2

typedef struct jbuf_s {
	size_t buf_len;
	int cursor;
	char buf[0];
} *jbuf_t;

/**
 * Max attribute name length not including the terminating '\0'
 */
// #define JSON_ATTR_NAME_MAX	253 /* (256 - 112)*/
// #define JSON_ATTR_NAME_MAX	(512 - 112)
// #define JSON_ATTR_NAME_MAX	(256 - 112)
#define JSON_ATTR_NAME_MAX	(192 - 112)
// #define JSON_ATTR_NAME_MAX	(128 - 112)

/**
 * \brief Verify that \c s is valid JSON.
 *
 * This convenience function checks if the specified input string is
 * valid JSON.
 *
 * \return 0 If the string is valid JSON
 * \return -1 if the string is not valid JSON
 * \return ENOMEM If there is insufficient memory
 */
int json_verify_string(char *s);

/**
 * \brief Free resources associated with \c e.
 *
 * The function protects against a NULL \c e parameter.
 */
void json_entity_free(json_entity_t e);

/**
 * \brief Return a string representation of \c type
 */
const char *json_type_name(enum json_value_e type);

/**
 * \brief Return true (1) if the input \c type is valid.
 */
enum json_value_e json_entity_type(json_entity_t e);

/**
 * \brief Parse a string into a JSON document
 *
 * The function returns 0 on a successful parse, and the \c pdoc
 * parameter contains the JSON document. The input buffer \c buf is
 * not modified.
 *
 * Parsing of the input buffer completes when either a '\0' is found
 * in the input buffer or \c buf_len bytes are consumed.
 *
 * The caller must free the returned JSON document with json_doc_free()
 * when the document is no longer needed.
 *
 * If a parsing error occurs, the return value will be non-zero. The
 * json_doc_errstr() function will return a string describing the
 * error.
 *
 * \param buf Pointer to the buffer to parse
 * \param buf_len The length of the \c buf
 * \param pdoc Pointer the json_doc_t where the JSON document is returned
 *
 * \return 0 The buffer was successfully parsed
 * \return !0 An error was encountered.
 */
int json_parse_buffer(char *buf, size_t buf_len, json_doc_t *pdoc);

/**
 * Create an empty dictionary
 */
json_entity_t json_dict_new(json_doc_t);

/**
 * Create an empty list
 */
json_entity_t json_list_new(json_doc_t);

/**
 * \brief Create a string entity
 *
 * Create a JSON string entity. The initialization string does not
 * need to be null terminated. The \c str parameter is NULL, the
 * string will be initialized to an empty string with a length of
 * zero. If the \c len parameter is 0, the \c str parameter is
 * ignored.
 *
 * \param str The input string
 * \param len The number of the bytes from the input string to consume
 */
json_entity_t json_string_new(json_doc_t doc, const char *str, size_t len);

/**
 * \brief Create a float entity
 *
 * \param d The initialization value
 */
json_entity_t json_float_new(json_doc_t doc, double d);

/**
 * \brief Create an integer entity
 *
 * \param i The initialization value
 */
json_entity_t json_int_new(json_doc_t doc, int64_t i);

/**
 * \brief Create a boolean entity
 *
 * \param tnf True (!0) or false (0)
 */
json_entity_t json_bool_new(json_doc_t doc, int tnf);

/**
 * \brief Create a NULL entity
 */
json_entity_t json_null_new(json_doc_t doc);

/**
 * \brief Create a new JSON entity of the specified type
 *
 * \param type The JSON entity type to create
 * \param ...  Type-specific initialization arguments:
 *             - JSON_INT_VALUE: int64_t value
 *             - JSON_BOOL_VALUE: int32_t value (0 or 1)
 *             - JSON_FLOAT_VALUE: double value
 *             - JSON_STRING_VALUE: char* string
 *             - JSON_ATTR_VALUE: char* name, json_entity_t value
 *             - JSON_LIST_VALUE: (no additional arguments)
 *             - JSON_DICT_VALUE: (no additional arguments)
 *             - JSON_NULL_VALUE: (no additional arguments)
 *
 * \return A new JSON entity, or NULL on error
 *
 * \note JSON integers are stored as signed 64-bit values (int64_t).
 *       Always pass int64_t for JSON_INT_VALUE to avoid truncation.
 */
extern json_entity_t json_entity_new(json_doc_t doc, enum json_value_e type, ...);

/**
 * \brief Dump the JSON entity into a json buffer (\c jbuf_t)
 *
 * This function can be used to append json string to an existing
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
 * \brief Format the JSON document into a JSON buffer (\c jbuf_t)
 *
 * \param jdoc  The JSON document handle.
 *
 * \return a json buffer -- \c jbuf_t -- is returned.
 * \see jbuf_free
 */
extern jbuf_t json_doc_dump(json_doc_t doc);

/**
 * \brief Create a new JSON entity identical to the given entity \c e.
 *
 * \param e     The original JSON entity
 *
 * \return a new JSON entity. NULL is returned if an out-of-memory error occurs.
 */
extern json_entity_t json_entity_copy(json_doc_t doc, json_entity_t e);

json_doc_t json_doc_new(void);
void json_doc_free(json_doc_t doc);
json_entity_t json_doc_root(json_doc_t doc);
char *json_doc_errstr(json_doc_t doc);
typedef struct json_stats_s {
	int fit_count;		/* strings that fit in the entity */
	int overflow_count;	/* strings that required malloc */
	int int_count;		/* integers */
	int float_count;	/* floating point numbers */
	int bool_count;		/* booleans */
	int null_count;		/* NULL count */
} *json_stats_t;

json_stats_t json_doc_stats(json_doc_t doc);

/**
 * \brief Build or append a dictionary with the given list of its attribute value pairs.
 *
 * \param d	The dictionary handle to modify, or NULL if starting a new dictionary.
 *
 * If \c d is NULL, a new dictionary with the given attribute value list.
 * If \c d is not NULL, the given attribute value list will be added to \c d.
 *
 * The format of the argument list is as follows:
 *
 *      <attr-name>, <value-type>, <attr-value>
 *
 * A NULL <attr-name> indicates the end of the argument list.
 *
 * A valid <value-type> and the expected arguments are as follows::
 *
 * - JSON_INT_VALUE: int64_t value
 * - JSON_BOOL_VALUE: int32_t value (0 or 1)
 * - JSON_FLOAT_VALUE: double value
 * - JSON_STRING_VALUE: char* string, size_t len
 * - JSON_DICT_VALUE: A nested dictionary, ends with a NULL attribute name
 * - JSON_LIST_VALUE: A list of <values>, the list ends with JSON_EOL_VALUE type.
 * - JSON_ATTR_VALUE: json_entity_t attribute. The <attr-name> is
 *                    ignored in this case and would be specified as "".
 *
 * Note that the type of the argument is very important, int, for
 * example cannot be used for the JSON_STRING_VALUE length field
 * because sizeof(int) != sizeof(size_t) on some architectures
 * (e.g. arm).  Because strlen() returns size_t, size_t is used for
 * the length argument. The reason for requiring the length is to
 * allow string values to be initialized from unterminated strings.
 *
 * \example
 *
 * char *str = "this is a string, but only 'string' is being used.";
 * json_entity_t *e = json_dict_build(
 *                        "my-string", JSON_STRING_VALUE, &str[27], (size_t)8,
 *                        NULL);
 *
 * This avoids modifying and/or copying \c str in order to use it to
 * specify the string value.
 *
 * An element type < 0 (JSON_EOL_VALUE) is used to 'terminate' the
 * argument value list for list types. A NULL attribute name
 * terminates a dictionary,
 *
 * \example
 *
 * json_entity_t d =
 *     json_dict_build(
 *        "int",    JSON_INT_VALUE,    (int64_t)1,
 *        "bool",   JSON_BOOL_VALUE,   (int32_t)1,
 *        "float",  JSON_FLOAT_VALUE,  1.1,
 *        "string", JSON_STRING_VALUE, "str", strlen("str"),
 *        "list",   JSON_LIST_VALUE,
 *                      // List elements
 *                      JSON_INT_VALUE, 1L,
 *                      JSON_STRING_VALUE, "last", strlen(last),
 *                      JSON_EOL_VALUE, // end LIST_VALUE argument
 *        "dict",   JSON_DICT_VALUE,
 *                      // Dictionary attributes
 *                      "attr1", JSON_INT_VALUE, 2L,
 *                      "attr2", JSON_BOOL_VALUE, (int32_t)0,
 *                      "attr3", JSON_STRING_VALUE, "last attribute", (size_t)14,
 *                      NULL,  // NULL terminates sub-dictionary argument list
 *        NULL  // NULL attribute name terminates dictionary
 *      );
 *
 * The resulting dictionary is
 * { "int":    1,
 *   "bool":   true,
 *   "float":  1.1,
 *   "string": "str",
 *   "list":   [1, "last" ],
 *   "dict":   { "attr1": 2,
 *               "attr2": 0,
 *               "attr3": "last attribute"
 *             }
 * }
 *
 */
json_entity_t json_dict_build(json_doc_t doc, ...);

/**
 * \brief Return the ATTR entity of the given name \c name
 *
 * \param name    The attribute name
 *
 * \return A JSON entity or NULL if no attributes of the given name exist.
 */
extern json_entity_t json_attr_find(json_entity_t d, const char *name);

/**
 * \brief Return the JSON entity of the attribute value
 *
 * \param name   The attribute name
 *
 * \return A json entity. NULL is returned if no attributes of the given name exist.
 */
extern json_entity_t json_value_find(json_entity_t d, const char *name);

/**
 * \brief Return the first attribute in the dictionary
 *
 * \param d The dictionary attribute
 *
 * \returns An entity handle or NULL if the dictionary is empty
 */
extern json_entity_t json_attr_first(json_entity_t d);

/**
 * \brief Return the next attribute in the dictionary
 *
 * \param a The previous attribute handle
 *
 * \returns An entity handle or NULL if there are no more attributes.
 */
extern json_entity_t json_attr_next(json_entity_t a);

/**
 * \brief Return the number of attributes in the dictionary \c d
 */
extern int json_attr_count(json_entity_t d);

/**
 * \brief Add an attribute with name \c name and value \c v to the JSON dict \c d
 *
 * If the attribute \c name already exists, it is added with value \c v.
 * All attributes with duplicate names are returned when iterating
 * through the list of atttributes in the dictionary, however, the
 * entity returned by json_attr_find() may be any of the attributes
 * with this name.
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

/**
 * \brief Return the attribute name of the entity \c a
 */
extern char *json_attr_name(json_entity_t a);

/**
 * \brief Return the value of the attribute entity \c a
 */
extern json_entity_t json_attr_value(json_entity_t a);

/**
 * \brief Remove the attribute \c name from the dictionary \c d and free it
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

/**
 * \brief Add the entity \c e to the list \c l
 */
extern void json_item_add(json_entity_t l, json_entity_t e);
/**
 * \brief Return the first entity in the list \c a
 */
extern json_entity_t json_item_first(json_entity_t a);

/**
 * \brief Return the element following \c i in the list
 */
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

/**
 * \brief Return the JSON integer value of the entity \c e
 *
 * If the specified entity is not a JSON integer, this interface will assert.
 */
extern int64_t json_value_int(json_entity_t e);
/**
 * \brief Return the JSON bool value of the entity \c e
 *
 * If the specified entity is not a JSON boolean, this interface will assert.
 */
extern int json_value_bool(json_entity_t e);
/**
 * \brief Return the JSON float value of the entity \c e
 *
 * If the specified entity is not a JSON float, this interface will assert.
 */
extern double json_value_float(json_entity_t e);

/**
 * \brief Return the NULL terminated string value of the entity \c e
 *
 * If the entity \c e is not a string, this function will assert.
 */
extern char *json_value_cstr(json_entity_t e);
/**
 * \brief Return the length of the string \c e
 *
 * Returns the length of the string \c e, not including the terminating '\0'
 *
 * If the entity \c e is not a string, this function will assert.
 */
extern size_t json_value_strlen(json_entity_t e);

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
 *
 * It is often an error (use-after-free or memory leak) unless
 * calling this function as jb = jbuf_append_str(jb,...).
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
