/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2023 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2023 Open Grid Computing, Inc. All rights reserved.
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

#define _GNU_SOURCE
#include <ctype.h>
#include <grp.h>
#include <pwd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include "coll/rbt.h"
#include "ovis_json/ovis_json.h"
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_stream.h"
#include "ovis_log.h"

#define SAMP "json_stream"

static ovis_log_t __log = NULL;
#define LOG(_level_, _fmt_, ...) ovis_log(__log, _level_, "[%d] " _fmt_, __LINE__, ##__VA_ARGS__)
#define LCRITICAL(_fmt_, ...) ovis_log(__log, OVIS_LCRIT, "[%d]" _fmt_, __LINE__, ##__VA_ARGS__)
#define LERROR(_fmt_, ...) ovis_log(__log, OVIS_LERROR, "[%d] " _fmt_, __LINE__, ##__VA_ARGS__)
#define LWARN(_fmt_, ...) ovis_log(__log, OVIS_LWARN, "[%d] " _fmt_, __LINE__, ##__VA_ARGS__)
#define LINFO(_fmt_, ...) ovis_log(__log, OVIS_LINFO, "[%d] " _fmt_, __LINE__, ##__VA_ARGS__)
#define LDEBUG(_fmt_, ...) ovis_log(__log, OVIS_LDEBUG, "[%d] " _fmt_, __LINE__, ##__VA_ARGS__)

static int str_cmp(void *tree_key, const void *srch_key)
{
	return strcmp(tree_key, srch_key);
}

#define DEFAULT_CHAR_ARRAY_LEN 255

/*
 * Some attributes of the JSON object have special meaning and/or are
 * required. These include:
 * - "schema" : Defines the unique schema name to use for the
 *              constructed metric set. If two JSON objects advertise
 *              the same schema name, but have different contents, the
 *              resulting object conversion is undefined..
 */

/*
 * [ { }, { }, ... ] encoded as LIST of LDMS_V_RECORD
 * [ int, int, ... ] encoded as list of LDMS_V_S64
 * [ string, string, ... ] encoded as list of ??
 * [ float, float, ... ] encoded as list of LDMS_V_D64
 * { } encoded as LDMS_V_RECORD
 */

#ifndef ARRAY_LEN
#define ARRAY_LEN(a) (sizeof(a)/sizeof(*a))
#endif /* ARRAY_LEN */

struct attr_entry {
	char *name;		   /* Attribute name */
	int midx;		   /* The metric index in the set */
	int ridx;		   /* The record type index */
	enum json_value_e type;	   /* THE LDMS metric value type */
	struct rbn rbn;		   /* schema->attr_tree entry */
};

struct schema_entry {
	ldms_schema_t schema;	/* The LDMS schema */
	char *name;		/* The schema name. This is the key
				   in the schema tree */
	struct rbt attr_tree;	/* This tree maps JSON object
				   attributes to conversion functions */
	struct rbn rbn;
};
static struct rbt schema_tree = RBT_INITIALIZER(str_cmp);
static pthread_mutex_t schema_tree_lock = PTHREAD_MUTEX_INITIALIZER;

static const char *usage(struct ldmsd_plugin *self)
{
	return \
	"config name=json_stream_sampler producer=<producer_name> \n"
	"         heap_sz=<int> stream=<stream_name>\n"
	"         [instance=<instance_name>] [component_id=<component_id>] [perm=<permissions>]\n"
	"         [uid=<user_name>] [gid=<group_name>]\n"
	"     producer      A unique name for the host providing the data\n"
	"     stream        A stream name to subscribe to.\n"
	"     heap_sz       The number of bytes to reserve for the set heap.\n"
	"     instance      A unique name for the metric set. If none is given,"
	"                   the set instance name will be <producer>_<schema name>.\n"
	"     component_id  A unique number for the component being monitored.\n"
	"                   The default is 0\n"
	"     uid           The user-id of the set's owner (defaults to geteuid())\n"
	"     gid           The group id of the set's owner (defaults to getegid())\n"
	"     perm          The set's access permissions (defaults to 0777)\n";
}

static int make_record_array(ldms_record_t record, json_entity_t list_attr)
{
	json_entity_t list;
	json_entity_t item;
	size_t list_len;
	jbuf_t jbuf;
	int rc;

	list = json_attr_value(list_attr);
	item = json_item_first(list);
	if (!item) {
		LERROR("Can't encode an empty list in an LDMS schema.\n");
		return EINVAL;	/* Can't parse empty list */
	}
	list_len = json_list_len(list);
	switch (json_entity_type(item)) {
	case JSON_INT_VALUE:
		rc = ldms_record_metric_add(record,
					    json_attr_name(list_attr)->str, NULL,
					    LDMS_V_S64_ARRAY, list_len);
		break;
	case JSON_BOOL_VALUE:
		rc = ldms_record_metric_add(record,
					    json_attr_name(list_attr)->str, NULL,
					    LDMS_V_S8_ARRAY, list_len);
		break;
	case JSON_FLOAT_VALUE:
		rc = ldms_record_metric_add(record,
					    json_attr_name(list_attr)->str, NULL,
					    LDMS_V_D64_ARRAY, list_len);
		break;
	case JSON_STRING_VALUE:
		jbuf = json_entity_dump(NULL, list);
		if (!jbuf) {
			LCRITICAL("Memory allocation failure.\n");
			rc = ENOMEM;
			break;
		}
		rc = ldms_record_metric_add(record,
					    json_attr_name(list_attr)->str, NULL,
					    LDMS_V_CHAR_ARRAY, jbuf->cursor+1);
		jbuf_free(jbuf);
		break;
	default:
		LERROR("Invalid list entry type (%d) for encoding as array in record\n",
		       json_entity_type(item));
		rc = EINVAL;
	}
	return rc;
}

static int make_record(ldms_schema_t schema, char *name, json_entity_t dict,
		       ldms_record_t *rec)
{
	int rc = -ENOMEM;
	ldms_record_t record;
	json_entity_t json_attr;

	record = ldms_record_create(name);
	if (!record)
		goto err_0;

	for (json_attr = json_attr_first(dict); json_attr;
	     json_attr = json_attr_next(json_attr)) {
		json_entity_t json_value = json_attr_value(json_attr);
		switch (json_entity_type(json_value)) {
		case JSON_INT_VALUE:
			rc = ldms_record_metric_add(record,
						    json_attr_name(json_attr)->str, NULL,
						    LDMS_V_S64, 0);
			break;
		case JSON_BOOL_VALUE:
			rc = ldms_record_metric_add(record,
						    json_attr_name(json_attr)->str, NULL,
						    LDMS_V_S8, 0);
			break;
		case JSON_FLOAT_VALUE:
			rc = ldms_record_metric_add(record,
						    json_attr_name(json_attr)->str, NULL,
						    LDMS_V_D64, 0);
			break;
		case JSON_STRING_VALUE:
			rc = ldms_record_metric_add(record,
						    json_attr_name(json_attr)->str, NULL,
						    LDMS_V_CHAR_ARRAY, 255);
			break;
		case JSON_LIST_VALUE:
			rc = make_record_array(record, json_attr);
			break;
		case JSON_DICT_VALUE:
			LINFO("Encoding unsupported nested dictionary '%s' as a string value.\n",
			       json_attr_name(json_attr)->str);
			rc = ldms_record_metric_add(record,
						    json_attr_name(json_attr)->str, NULL,
						    LDMS_V_CHAR_ARRAY, 255);
			break;
		default:
			LERROR("Ignoring unsupported entity '%s[%s]') "
			       "in JSON dictionary.\n",
			       json_attr_name(json_attr)->str,
			       json_type_name(json_entity_type(json_value)));
		};
	}
	*rec = record;
	return ldms_schema_record_add(schema, record);
 err_0:
	return rc;
}

static int make_list(ldms_schema_t schema, json_entity_t parent, json_entity_t list_attr)
{
	json_entity_t list = json_attr_value(list_attr);
	json_entity_t item, len_attr;
	size_t item_size;
	ldms_record_t record;
	char *record_name;
	enum json_value_e type;
	int rc;

	item = json_item_first(list);
	if (!item)
		return 0;	/* empty list */

	type = json_entity_type(item);
	switch (type) {
	case JSON_FLOAT_VALUE:
		item_size = ldms_list_heap_size_get(LDMS_V_D64, 1, 1);
		break;
	case JSON_INT_VALUE:
		item_size = ldms_list_heap_size_get(LDMS_V_S64, 1, 1);
		break;
	case JSON_BOOL_VALUE:
		item_size = ldms_list_heap_size_get(LDMS_V_S8, 1, 1);
		break;
	case JSON_STRING_VALUE:
		item_size = ldms_list_heap_size_get(LDMS_V_CHAR_ARRAY,
						    1, json_value_str(item)->str_len);
		break;
	case JSON_DICT_VALUE:
		/*
		 * Add a record definition for the dictionary list item
		 */
		rc = asprintf(&record_name, "%s_record", json_attr_name(list_attr)->str);
		rc = make_record(schema, record_name, item, &record);
		free(record_name);
		if (rc < 0)
			return rc;
		item_size = ldms_record_heap_size_get(record);
		break;
	default:
		LERROR("Invalid item type encountered in list\n");
		return -EINVAL;
	}
	/* Check if there is a max specified for the list to override
	 * the current length */
	rc = asprintf(&record_name, "%s_max_len",
		      json_attr_name(list_attr)->str);
	len_attr = json_attr_find(parent, record_name);
	free(record_name);
	size_t list_len = json_list_len(list);
	if (len_attr) {
		if (json_entity_type(json_attr_value(len_attr))
		    != JSON_INT_VALUE) {
			LERROR("The list length override for '%s' must be "
			       "an integer.\n", json_attr_name(list_attr)->str);
		} else {
			list_len = json_value_int(json_attr_value(len_attr));
		}
	}
	LINFO("Adding list '%s' with %zd elements of size %zd\n",
	      json_attr_name(list_attr)->str, list_len, item_size);
	return ldms_schema_metric_list_add(schema,
					   json_attr_name(list_attr)->str, NULL,
					   2 * item_size * list_len);
}

typedef int (*json_setter_t)(ldms_set_t set, ldms_mval_t mval, json_entity_t entity, void *);
static json_setter_t setter_table[];

typedef int (*dict_list_setter_t)(ldms_mval_t rec_inst, int mid, int idx, json_entity_t value, void *);

int JSON_INT_VALUE_setter(ldms_set_t set, ldms_mval_t mval, json_entity_t entity, void *ctxt)
{
	ldms_mval_set_s64(mval, json_value_int(entity));
	return 0;
}

int JSON_BOOL_VALUE_setter(ldms_set_t set, ldms_mval_t mval, json_entity_t entity, void *ctxt)
{
	ldms_mval_set_s8(mval, (int8_t)json_value_bool(entity));
	return 0;
}

int JSON_FLOAT_VALUE_setter(ldms_set_t set, ldms_mval_t mval, json_entity_t entity, void *ctxt)
{
	ldms_mval_set_double(mval, json_value_float(entity));
	return 0;
}

int JSON_STRING_VALUE_setter(ldms_set_t set, ldms_mval_t mval, json_entity_t entity, void *ctxt)
{
	json_str_t v = json_value_str(entity);
	ldms_mval_array_set_str(mval, v->str, v->str_len);
	return 0;
}

int JSON_ATTR_VALUE_setter(ldms_set_t set, ldms_mval_t mval, json_entity_t entity, void *ctxt)
{
	assert(0 == "Invalid JSON type setter");
	return EINVAL;
}

int JSON_LIST_VALUE_setter(ldms_set_t set, ldms_mval_t list_mval,
			   json_entity_t list, void *ctxt)
{
	json_entity_t item;
	enum json_value_e type;
	ldms_mval_t item_mval;
	int rc, i = 0;
	char *rec_type_name = NULL;
	int rec_idx;

	rc = ldms_list_purge(set, list_mval);
	for (item = json_item_first(list); item; item = json_item_next(item)) {
		type = json_entity_type(item);
		LDEBUG("Setting list item %d of type %d\n", i, json_entity_type(item));

		switch (type) {
		case JSON_INT_VALUE:
			item_mval = ldms_list_append_item(set, list_mval, LDMS_V_S64, 1);
			break;
		case JSON_BOOL_VALUE:
			item_mval = ldms_list_append_item(set, list_mval, LDMS_V_S8, 1);
			break;
		case JSON_FLOAT_VALUE:
			item_mval = ldms_list_append_item(set, list_mval, LDMS_V_D64, 1);
			break;
		case JSON_STRING_VALUE:
			item_mval = ldms_list_append_item(set, list_mval,
							  LDMS_V_CHAR_ARRAY, 255);
			break;
		case JSON_DICT_VALUE:
			if (!rec_type_name) {
				rc = asprintf(&rec_type_name, "%s_record", (char *)ctxt);
				rec_idx = ldms_metric_by_name(set, rec_type_name);
				free(rec_type_name);
			}
			item_mval = ldms_record_alloc(set, rec_idx);
			if (!item_mval) {
				rc = ENOMEM;
				goto err;
			}
			rc = ldms_list_append_record(set, list_mval, item_mval);
			break;
		default:
			LERROR("Invalid list entry %d type (%d)\n", i, type);
			rc = EINVAL;
			goto err;
		}
		if (item_mval) {
			rc = setter_table[type](set, item_mval, item, ctxt);
			if (rc)
				LERROR("Error %d setting list item %d\n", rc, i);
		} else {
			LERROR("NULL list item %d mval\n", i);
		}
		i++;
	}
	return 0;
 err:
	return rc;
}

static int dict_list_set(ldms_mval_t rec_inst, int mid, json_entity_t list, void *ctxt);
int JSON_DICT_VALUE_setter(ldms_set_t set, ldms_mval_t rec_inst, json_entity_t dict, void *ctxt)
{
	json_entity_t attr;
	ldms_mval_t mval;
	int rc, idx;
	jbuf_t jbuf;
	size_t array_len;

	for (attr = json_attr_first(dict); attr; attr = json_attr_next(attr)) {
		char *name = json_attr_name(attr)->str;
		json_entity_t value = json_attr_value(attr);
		enum json_value_e type = json_entity_type(value);

		idx = ldms_record_metric_find(rec_inst, name);
		/* Ignore skipped values from json dictionary */
		if (idx < 0) {
			LINFO("Ignoring '%s' attribute in JSON dictionary.\n", name);
			continue;
		}
		mval = ldms_record_metric_get(rec_inst, idx);
		switch (type) {
		case JSON_DICT_VALUE:
			/* This is a dictionary in another
			 * dictionary. LDMS_V_RECORD does not support
			 * nested records, so set the dictionary
			 * value to a JSON string */
			(void) ldms_record_metric_type_get(rec_inst, idx, &array_len);
			jbuf = json_entity_dump(NULL, value);
			if (array_len <= jbuf->cursor) { /* jbuf->cursor doesn't include '\0'. */
				LWARN("Dictionary attribute '%s' (%d) is larger "
					"than the allocated space (%ld). "
					"The string is chunked.\n",
					name, jbuf->cursor, array_len);
				/* Chunk the string */
				jbuf->buf[array_len] = '\0';
			} else {
				 /* Ensure that the string is null-terminated. */
				jbuf->buf[jbuf->cursor] = '\0';
			}
			ldms_record_array_set_str(rec_inst, idx, jbuf->buf);
			jbuf_free(jbuf);
			rc = 0;
			break;
		case JSON_LIST_VALUE:
			/*
			 * Record cannot have lists, so all lists in a dictionary
			 * is mapped to an array. A list of strings is encoded
			 * as char[].
			 */
			rc = dict_list_set(rec_inst, idx, value, NULL);
			break;
		default:
			rc = setter_table[type](set, mval, value, NULL);
			break;
		}
		if (rc)
			LERROR("Error %d setting record attribute '%s'\n", rc, name);
	}
	return 0;
}

int JSON_NULL_VALUE_setter(ldms_set_t set, ldms_mval_t mval, json_entity_t entity, void *ctxt)
{
	return 0;
}


static json_setter_t setter_table[] = {
	[JSON_INT_VALUE] = JSON_INT_VALUE_setter,
	[JSON_BOOL_VALUE] = JSON_BOOL_VALUE_setter,
	[JSON_FLOAT_VALUE] = JSON_FLOAT_VALUE_setter,
	[JSON_STRING_VALUE] = JSON_STRING_VALUE_setter,
	[JSON_ATTR_VALUE] = JSON_ATTR_VALUE_setter,
	[JSON_LIST_VALUE] = JSON_LIST_VALUE_setter,
	[JSON_DICT_VALUE] = JSON_DICT_VALUE_setter,
	[JSON_NULL_VALUE] = JSON_NULL_VALUE_setter
};

static int DICT_LIST_INT_setter(ldms_mval_t rec_inst, int mid, int idx, json_entity_t item, void *ctxt)
{
	enum json_value_e jtype;
	jtype = json_entity_type(item);
	if (jtype != JSON_INT_VALUE) {
		LERROR("List '%s' in a dictionary contains '%s' but expected '%s'.\n",
					ldms_record_metric_name_get(rec_inst, mid),
					json_type_name(jtype), json_type_name(JSON_INT_VALUE));
		return EINVAL;
	}

	ldms_record_array_set_s64(rec_inst, mid, idx, json_value_int(item));
	return 0;
}

static int DICT_LIST_BOOL_setter(ldms_mval_t rec_inst, int mid, int idx, json_entity_t item, void *ctxt)
{
	enum json_value_e jtype;
	jtype = json_entity_type(item);
	if (jtype != JSON_BOOL_VALUE) {
		LERROR("List '%s' in a dictionary contains '%s' but expected '%s'.\n",
					ldms_record_metric_name_get(rec_inst, mid),
					json_type_name(jtype), json_type_name(JSON_BOOL_VALUE));
		return EINVAL;
	}
	ldms_record_array_set_s8(rec_inst, mid, idx, json_value_int(item));
	return 0;
}

static int DICT_LIST_FLOAT_setter(ldms_mval_t rec_inst, int mid, int idx, json_entity_t item, void *ctxt)
{
	enum json_value_e jtype;
	jtype = json_entity_type(item);
	if (jtype != JSON_FLOAT_VALUE) {
		LERROR("List '%s' in a dictionary contains '%s' but expected '%s'.\n",
					ldms_record_metric_name_get(rec_inst, mid),
					json_type_name(jtype), json_type_name(JSON_FLOAT_VALUE));
		return EINVAL;
	}
	ldms_record_array_set_double(rec_inst, mid, idx, json_value_float(item));
	return 0;
}

static int DICT_LIST_STRING_setter(ldms_mval_t rec_inst, int mid, int idx, json_entity_t list, void *ctxt)
{
	size_t array_len;
	jbuf_t jbuf;

	(void)ldms_record_metric_type_get(rec_inst, mid, &array_len);

	jbuf = json_entity_dump(NULL, list);
	if (!jbuf) {
		LCRITICAL("Memory allocation failure.\n");
		return ENOMEM;
	}

	if (array_len <= jbuf->cursor) {
		LWARN("The JSON-formatted of a list of strings (%d) is larger "
			"than the allocated space. (%ld) The received data is chunked.\n",
			jbuf->cursor, array_len);
		jbuf->buf[array_len] = '\0';
	}

	ldms_record_array_set_str(rec_inst, mid, jbuf->buf);
	jbuf_free(jbuf);
	return 0;
}

static dict_list_setter_t dl_setter_table[] = {
	[JSON_INT_VALUE] = DICT_LIST_INT_setter,
	[JSON_BOOL_VALUE] = DICT_LIST_BOOL_setter,
	[JSON_FLOAT_VALUE] = DICT_LIST_FLOAT_setter,
	[JSON_STRING_VALUE] = DICT_LIST_STRING_setter
};

static int dict_list_set(ldms_mval_t rec_inst, int mid, json_entity_t list, void *ctxt)
{
	int idx;
	int rc = 0;
	json_entity_t item;
	enum json_value_e jtype;
	size_t array_len;

	(void)ldms_record_metric_type_get(rec_inst, mid, &array_len);
	if (json_list_len(list) > array_len) {
		LWARN("List '%s' in a dictionary length (%ld) is larger than "
			"the encoded array length (%ld). The extra items will be ignored.\n",
					ldms_record_metric_name_get(rec_inst, mid),
						json_list_len(list), array_len);
	}

	item = json_item_first(list);
	jtype = json_entity_type(item);
	for (idx = 0; idx < array_len && item; idx++ , item = json_item_next(item)) {
		switch (jtype) {
		case JSON_INT_VALUE:
			rc = dl_setter_table[JSON_INT_VALUE](rec_inst, mid, idx, item, ctxt);
			break;
		case JSON_BOOL_VALUE:
			rc = dl_setter_table[JSON_BOOL_VALUE](rec_inst, mid, idx, item, ctxt);
			break;
		case JSON_FLOAT_VALUE:
			rc = dl_setter_table[JSON_FLOAT_VALUE](rec_inst, mid, idx, item, ctxt);
			break;
		case JSON_STRING_VALUE:
			rc = dl_setter_table[JSON_STRING_VALUE](rec_inst, mid, idx, list, ctxt);
			break;
		default:
			LERROR();
			break;
		}
	}

	return rc;
}

static int get_schema_for_json(char *name, json_entity_t e, ldms_schema_t *sch)
{
	int i, rc = 0;
	ldms_schema_t schema;
	struct schema_entry *entry;
	struct rbn *rbn;
	struct attr_entry *ae;
	json_entity_t json_attr;
	json_entity_t json_value;
	ldms_record_t record;
	enum json_value_e type;
	char *record_name;
	int midx, ridx = -1;

	pthread_mutex_lock(&schema_tree_lock);
	rbn = rbt_find(&schema_tree, name);
	pthread_mutex_unlock(&schema_tree_lock);
	if (rbn) {
		entry = container_of(rbn, struct schema_entry, rbn);
		*sch = entry->schema;
		return 0;
	}
	schema = ldms_schema_new(name);
	if (!schema) {
		rc = errno;
		goto err_0;
	}
	entry = calloc(1, sizeof(*entry));
	if (!entry) {
		rc = errno;
		goto err_1;
	}
	entry->schema = schema;
	entry->name = strdup(name);
	if (!entry->name) {
		rc = errno;
		goto err_2;
	}
	rbt_init(&entry->attr_tree, str_cmp);
	rbn_init(&entry->rbn, entry->name);

	/* Add the special JSON stream attributes. These special
	 * attributes will have metric indices of 0 (S_uid),
	 * 1 (S_gid), and 2 (S_perm)
	 */
	const char *stream_meta_attr[] = { "S_uid", "S_gid", "S_perm" };
	for (i = 0; i < sizeof(stream_meta_attr) / sizeof(stream_meta_attr[0]); i++) {
		midx = ldms_schema_metric_add(schema, stream_meta_attr[i], LDMS_V_S32);
		if (midx < 0)
			goto err_3;
		ae = calloc(1, sizeof(*ae));
		if (!ae) {
			rc = errno;
			goto err_3;
		}
		ae->name = strdup(stream_meta_attr[i]);
		if (!ae->name) {
			rc = ENOMEM;
			free(ae);
			goto err_3;
		}
		ae->type = JSON_INT_VALUE;
		ae->ridx = -1;
		ae->midx = midx;
		rbn_init(&ae->rbn, ae->name);
		rbt_ins(&entry->attr_tree, &ae->rbn);
	}

	for (json_attr = json_attr_first(e); json_attr;
	     json_attr = json_attr_next(json_attr)) {

		json_value = json_attr_value(json_attr);
		type = json_entity_type(json_value);
		switch (type) {
		case JSON_INT_VALUE:
			midx = ldms_schema_metric_add(schema,
						      json_attr_name(json_attr)->str,
						      LDMS_V_S64);
			break;
		case JSON_BOOL_VALUE:
			midx = ldms_schema_metric_add(schema,
						      json_attr_name(json_attr)->str,
						      LDMS_V_S8);
			break;
		case JSON_FLOAT_VALUE:
			midx = ldms_schema_metric_add(schema,
						      json_attr_name(json_attr)->str,
						      LDMS_V_D64);
			break;
		case JSON_STRING_VALUE:
			midx = ldms_schema_metric_array_add(schema,
							    json_attr_name(json_attr)->str,
							    LDMS_V_CHAR_ARRAY, DEFAULT_CHAR_ARRAY_LEN);
			break;
		case JSON_LIST_VALUE:
			midx = make_list(schema, e, json_attr);
			break;
		case JSON_DICT_VALUE:
			/* Add the record definition to the schema */
			rc = asprintf(&record_name, "%s_record", json_attr_name(json_attr)->str);
			ridx = make_record(schema, record_name,
					   json_attr_value(json_attr), &record);
			free(record_name);
			/* A record must be a member of an array or list.
			 * Create an array to contain the record */
			midx = ldms_schema_record_array_add(schema,
							    json_attr_name(json_attr)->str,
							    record, 1);
			break;
		default:
			LERROR("Unsupported type, '%s', in JSON dictionary.\n",
			       json_type_name(type));
			// rc = EINVAL;
			// goto err_3;
			continue;
		};
		if (midx < 0) {
			rc = EINVAL;
			goto err_3;
		}
		ae = calloc(1, sizeof(*ae));
		if (!ae) {
			rc = errno;
			goto err_3;
		}
		ae->name = strdup(json_attr_name(json_attr)->str);
		if (!ae->name) {
			rc = ENOMEM;
			free(ae);
			goto err_3;
		}
		ae->type = type;
		ae->ridx = ridx;
		ae->midx = midx;
		rbn_init(&ae->rbn, ae->name);
		rbt_ins(&entry->attr_tree, &ae->rbn);
	}
	pthread_mutex_lock(&schema_tree_lock);
	/* Make certain we didn't lose a race with another stream
	 * thread */
	rbn = rbt_find(&schema_tree, name);
	if (rbn) {
		rc = EBUSY;
		pthread_mutex_unlock(&schema_tree_lock);
		goto err_3;
	}
	rbn_init(&entry->rbn, entry->name);
	rbt_ins(&schema_tree, &entry->rbn);
	pthread_mutex_unlock(&schema_tree_lock);
	*sch = entry->schema;
	return 0;
 err_3:
	while (!rbt_empty(&entry->attr_tree)) {
		rbn = rbt_min(&entry->attr_tree);
		ae = container_of(rbn, struct attr_entry, rbn);
		free(ae->name);
		rbt_del(&entry->attr_tree, rbn);
	}
	free(entry->name);
 err_2:
	free(entry);
 err_1:
	ldms_schema_delete(schema);
 err_0:
	return rc;
}

static int json_recv_cb(ldms_stream_event_t ev, void *arg);

pthread_mutex_t cfg_tree_lock = PTHREAD_MUTEX_INITIALIZER;
static struct rbt cfg_tree = RBT_INITIALIZER(str_cmp);
struct json_cfg_inst {
	struct ldmsd_plugin *plugin;
	char *stream_name;
	size_t heap_sz;
	char *producer_name;
	char *instance_name;
	uint64_t comp_id;
	uid_t uid;
	gid_t gid;
	uint32_t perm;
	pthread_mutex_t lock;
	struct rbn rbn;		/* Key is stream_name */
};

#define DEFAULT_HEAP_SZ 512
/*
 * instance=FMT	The FMT string is a format specifier for generating
 *		the instance name from features from the JSON object
 *		and the transport on which the object was received.
 */
static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl,
		  struct attr_value_list *avl)
{
	struct json_cfg_inst *inst;
	char *value;
	int rc;

	inst = calloc(1, sizeof(*inst));
	if (!inst)
		return ENOMEM;

	pthread_mutex_init(&inst->lock, NULL);

	/* stream name */
	value = av_value(avl, "stream");
	if (!value) {
		rc = EINVAL;
		LERROR("The 'stream' configuration parameter is required.\n");
		goto err_0;
	}
	inst->stream_name = strdup(value);
	if (!inst->stream_name) {
		rc = ENOMEM;
		goto err_0;
	}

	/* producer */
	value = av_value(avl, "producer");
	if (!value) {
		LERROR("The 'producer' configuration parameter is required.\n");
		rc = EINVAL;
		goto err_0;
	}
	inst->producer_name = strdup(value);
	if (!inst->producer_name) {
		rc = ENOMEM;
		goto err_0;
	}

	/* instance name */
	value = av_value(avl, "instance");
	if (value) {
		inst->instance_name = strdup(value);
		if (!inst->instance_name) {
			rc = ENOMEM;
			goto err_0;
		}
	}

	/* component_id */
	value = av_value(avl, "component_id");
	inst->comp_id = 0;
	if (value) {
		/* Skip non isdigit prefix */
		while (*value != '\0' && !isdigit(*value)) value++;
		if (*value != '\0')
			inst->comp_id = (uint64_t)(atoi(value));
	}
	/* heap_sz */
	inst->heap_sz = DEFAULT_HEAP_SZ;
	value = av_value(avl, "heap_sz");
	if (value)
		inst->heap_sz = strtol(value, NULL, 0);

	/* Set uid, gid, perm to the default values */
	ldms_set_default_authz(&inst->uid, &inst->gid, &inst->perm, DEFAULT_AUTHZ_READONLY);
	/* uid */
	value = av_value(avl, "uid");
	if (value) {
		if (isalpha(value[0])) {
			/* Lookup the user name */
			struct passwd *pwd = getpwnam(value);
			if (!pwd) {
				LERROR("The specified user '%s' does not exist\n", value);
				rc = EINVAL;
				goto err_0;
			}
			inst->uid = pwd->pw_uid;
		} else {
			inst->uid = strtol(value, NULL, 0);
		}
	}

	/* gid */
	value = av_value(avl, "gid");
	if (value) {
		if (isalpha(value[0])) {
			/* Try to lookup the group name */
			struct group *grp = getgrnam(value);
			if (!grp) {
				LERROR("The specified group '%s' does not exist\n", value);
				rc = EINVAL;
				goto err_0;
			}
			inst->gid = grp->gr_gid;
		} else {
			inst->gid = strtol(value, NULL, 0);
		}
	}

	/* permission */
	value = av_value(avl, "perm");
	if (value) {
		if (value[0] != '0') {
			LINFO("Warning, the permission bits '%s' are not specified "
			       "as an Octal number.\n",
			       value);
		}
		inst->perm = strtol(value, NULL, 0);
	}

	rbn_init(&inst->rbn, inst->stream_name);
	pthread_mutex_lock(&cfg_tree_lock);
	struct rbn *rbn = rbt_find(&cfg_tree, inst->stream_name);
	if (rbn) {
		rc = EBUSY;
		LERROR("The stream name '%s' has already been registered for "
		       "this sampler.\n", inst->stream_name);
		goto err_1;
	}
	rbt_ins(&cfg_tree, &inst->rbn);
	pthread_mutex_unlock(&cfg_tree_lock);
	ldms_stream_subscribe(inst->stream_name, 0, json_recv_cb, inst, "json_stream_sampler");
	return 0;
 err_1:
	pthread_mutex_unlock(&cfg_tree_lock);
 err_0:
	if (inst) {
		free(inst->stream_name);
		free(inst->producer_name);
		free(inst->instance_name);
		free(inst);
	}
	return rc;
}

static void update_set_data(struct json_cfg_inst *inst,
			    ldms_set_t set,
			    json_entity_t entity)
{
	const char *schema_name = ldms_set_schema_name_get(set);
	struct schema_entry *entry;
	json_entity_t json_attr;
	ldms_mval_t mval;
	struct rbn *rbn;
	int rc;

	/* Find the schema instance for this set */
	rbn = rbt_find(&schema_tree, schema_name);
	if (!rbn) {
		LERROR("There is no parsing entity for schema '%s'\n",
		       schema_name);
		return;
	} else {
		entry = container_of(rbn, struct schema_entry, rbn);
	}

	for (json_attr = json_attr_first(entity); json_attr;
	     json_attr = json_attr_next(json_attr)) {

		char *name = json_attr_name(json_attr)->str;
		json_entity_t value = json_attr_value(json_attr);
		enum json_value_e type = json_entity_type(json_attr_value(json_attr));
		rbn = rbt_find(&entry->attr_tree, name);
		if (!rbn) {
			LERROR("Could not find attribute entry for '%s'\n", name);
			continue;
		}
		struct attr_entry *ae = container_of(rbn, struct attr_entry, rbn);
		LDEBUG("Updating midx %d with json attribute '%s' of type %d\n",
		       ae->midx, name, type);

		mval = ldms_metric_get(set, ae->midx);
		assert(mval);

		switch (type) {
		case JSON_DICT_VALUE:
			/* The associated record the 1st and only
			 * element of the containing array at mval */
			rc = setter_table[JSON_DICT_VALUE](set,
							   ldms_record_array_get_inst(mval, 0),
							   value, name);
			break;
		default:
			rc = setter_table[ae->type](set, mval, value, name);
			break;
		}
		if (rc)
			LERROR("Error %d setting the metric value '%s'\n", rc, ae->name);
	}
}

static int json_recv_cb(ldms_stream_event_t ev, void *arg)
{
	const char *msg;
	json_entity_t entity;
	int rc = EINVAL;
	ldms_schema_t schema;
	struct json_cfg_inst *inst = arg;
	json_entity_t schema_name;

	if (ev->type != LDMS_STREAM_EVENT_RECV)
		return 0;
	LDEBUG("thread: %lu, stream: '%s', msg: '%s'\n", pthread_self(), ev->recv.name, ev->recv.data);
	if (ev->recv.type != LDMS_STREAM_JSON) {
		LERROR("Unexpected stream type data...ignoring\n");
		return 0;
	}

	pthread_mutex_lock(&inst->lock);

	msg = ev->recv.data;
	entity = ev->recv.json;

	/* Find/create the schema for this JSON object */
	if (JSON_DICT_VALUE != json_entity_type(entity)) {
		rc = EINVAL;
		LERROR("%s: Ignoring message that is not a JSON dictionary.\n",
				ev->recv.name);
		goto err_0;
	}

	schema_name = json_value_find(entity, "schema");
	if (!schema_name || (JSON_STRING_VALUE != json_entity_type(schema_name))) {
		rc = EINVAL;
		LERROR("%s: Ignoring message with 'schema' attribute that is "
		       "missing or not a string.\n", ev->recv.name);
		goto err_0;
	}
	rc = get_schema_for_json(json_value_str(schema_name)->str, entity, &schema);
	if (rc) {
		LERROR("%s: Error %d creating an LDMS schema for the JSON object '%s'\n",
		       ev->recv.name, rc, msg);
		goto err_0;
	}
	char *set_name;
	if (inst->instance_name) {
		set_name = strdup(inst->instance_name);
	} else {
		rc = asprintf(&set_name, "%s_%s", inst->producer_name,
			      json_value_str(schema_name)->str);
		if (rc < 0)
			set_name = NULL;
	}
	if (!set_name) {
		LERROR("Memory allocation failure.\n");
		goto err_0;
	}
	ldms_set_t set = ldms_set_by_name(set_name);
	if (!set) {
		set = ldms_set_create(set_name, schema, inst->uid, inst->gid,
						  inst->perm, inst->heap_sz);
		if (set) {
			LINFO("Created the set '%s' with schema '%s'\n",
			      set_name, ldms_schema_name_get(schema));
			ldms_set_publish(set);
		} else {
			LERROR("Error %d creating the set '%s' with schema '%s'\n",
			       errno, set_name, ldms_schema_name_get(schema));
			rc = errno;
			goto err_1;
		}
	}
	free(set_name);

	/* Update the stream meta-data in the set */
	ldms_transaction_begin(set);
	ldms_metric_set_s32(set, 0, ev->recv.cred.uid);
	ldms_metric_set_s32(set, 1, ev->recv.cred.gid);
	ldms_metric_set_s32(set, 2, ev->recv.perm);
	update_set_data(inst, set, entity);
	ldms_transaction_end(set);

	pthread_mutex_unlock(&inst->lock);
	return 0;
 err_1:
	free(set_name);
 err_0:
	pthread_mutex_unlock(&inst->lock);
	return rc;
}

static void term(struct ldmsd_plugin *self)
{
	/* TODO: Cleanup ... maybe ... */
}

static int sample(struct ldmsd_sampler *self)
{
	/* no opt */
	return 0;
}

static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	/* no opt */
	return NULL;
}

static struct ldmsd_sampler json_plugin = {
	.base = {
		.name = SAMP,
		.type = LDMSD_PLUGIN_SAMPLER,
		.term = term,
		.config = config,
		.usage = usage,
	},
	.get_set = get_set,
	.sample = sample,
};

struct ldmsd_plugin *get_plugin()
{
	if (!__log) {
		/* Log initialization errors are quiet and will result in
		 * messages going to the application log instead of our subsystem
		 * specific log */
		__log = ovis_log_register
			("sampler.json_stream",
			 "JSON sampler that creates LDMS metric sets from JSON messages."
			 );
	}
	return &json_plugin.base;
}
