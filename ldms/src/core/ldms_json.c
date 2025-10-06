/* -*- c-basic-offset: 8 -*-
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
 *      Neither the name of Open Grid Computing nor the names of any
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *      Modified source versions must be plainly marked as such, and
 *      must not be misrepresented as being the original software.
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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include "ldms.h"
#include "ldms_json.h"

/*
 * Escape the quote character '"' --> '\"'
 */
static int json_encode_str(FILE *fp, char *src)
{
	int cnt = 0;
	int quote = *src != '"';
	if (quote) {
		cnt = fprintf(fp, "\"");
		if (cnt <= 0)
			return cnt;
	}
	for (; *src != '\0'; src++) {
		switch (*src) {
		case '\\':
			/* Value is already escaped */
			cnt = fprintf(fp, "\\%c", *(++src));
			break;
		case '"':
			/* Value needs to be escaped */
			cnt = fprintf(fp, "\\\"");
			break;
		default:
			cnt = fprintf(fp, "%c", *src);
			break;
		}
		if (cnt <= 0)
			return cnt;
	}
	if (quote)
		cnt = fprintf(fp, "\"");
	return cnt;
}

/*
 * An LDMS Metric is formatted as follows:
 *
 * {
 *   "name" : "<name>",
 *   "type" : "<type>",
 *   "units" : "<units>",
 *   "value" : <value>
 * }
 */
static int prim_value_as_json(FILE *fp,
			enum ldms_value_type type,
			ldms_mval_t val, size_t n)
{
	int i;
	int cnt;

	switch (type) {
	case LDMS_V_CHAR_ARRAY:
		cnt = json_encode_str(fp, val->a_char);
		break;
	case LDMS_V_CHAR:
		cnt = fprintf(fp, "\"%c\"", val->v_char);
		break;
	case LDMS_V_U8:
		cnt = fprintf(fp, "%hhu", val->v_u8);
		break;
	case LDMS_V_U8_ARRAY:
		cnt = fprintf(fp, "[");
		if (cnt <= 0)
			break;
		for (i = 0; i < n; i++) {
			if (i) {
				cnt = fprintf(fp, ",");
				if (cnt <= 0)
					return cnt;
			}
			cnt = fprintf(fp, "%hhu", val->a_u8[i]);
			if (cnt <= 0)
				return cnt;
		}
		cnt = fprintf(fp, "]");
		break;
	case LDMS_V_S8:
		cnt = fprintf(fp, "%hhd", val->v_s8);
		break;
	case LDMS_V_S8_ARRAY:
		cnt = fprintf(fp, "[");
		if (cnt <= 0)
			break;
		for (i = 0; i < n; i++) {
			if (i) {
				cnt = fprintf(fp, ",");
				if (cnt <= 0)
					return cnt;
			}
			cnt = fprintf(fp, "%hhd", val->a_s8[i]);
			if (cnt <= 0)
				return cnt;
		}
		cnt = fprintf(fp, "]");
		break;
	case LDMS_V_U16:
		cnt = fprintf(fp, "%hu", val->v_u16);
		break;
	case LDMS_V_U16_ARRAY:
		cnt = fprintf(fp, "[");
		if (cnt <= 0)
			break;
		for (i = 0; i < n; i++) {
			if (i) {
				cnt = fprintf(fp, ",");
				if (cnt <= 0)
					return cnt;
			}
			cnt = fprintf(fp, "%hu", val->a_u16[i]);
			if (cnt <= 0)
				return cnt;
		}
		cnt += fprintf(fp, "]");
		break;
	case LDMS_V_S16:
		cnt = fprintf(fp, "%hd", val->v_s16);
		break;
	case LDMS_V_S16_ARRAY:
		cnt = fprintf(fp, "[");
		if (cnt <= 0)
			break;
		for (i = 0; i < n; i++) {
			if (i) {
				cnt = fprintf(fp, ",");
				if (cnt <= 0)
					return cnt;
			}
			cnt = fprintf(fp, "%hd", val->a_s16[i]);
			if (cnt <= 0)
				return cnt;
		}
		cnt = fprintf(fp, "]");
		break;
	case LDMS_V_U32:
		cnt = fprintf(fp, "%u", val->v_u32);
		break;
	case LDMS_V_U32_ARRAY:
		cnt = fprintf(fp, "[");
		if (cnt <= 0)
			break;
		for (i = 0; i < n; i++) {
			if (i) {
				cnt = fprintf(fp, ",");
				if (cnt <= 0)
					return cnt;
			}
			cnt = fprintf(fp, "%u", val->a_u32[i]);
			if (cnt <= 0)
				return cnt;
		}
		cnt = fprintf(fp, "]");
		break;
	case LDMS_V_S32:
		cnt = fprintf(fp, "%d", val->v_s32);
		break;
	case LDMS_V_S32_ARRAY:
		cnt = fprintf(fp, "[");
		if (cnt <= 0)
			break;
		for (i = 0; i < n; i++) {
			if (i) {
				cnt = fprintf(fp, ",");
				if (cnt <= 0)
					return cnt;
			}
			cnt = fprintf(fp, "%d", val->a_s32[i]);
			if (cnt <= 0)
				return cnt;
		}
		cnt = fprintf(fp, "]");
		break;
	case LDMS_V_U64:
		cnt = fprintf(fp, "%"PRIu64, val->v_u64);
		break;
	case LDMS_V_U64_ARRAY:
		cnt = fprintf(fp, "[");
		if (cnt <= 0)
			break;
		for (i = 0; i < n; i++) {
			if (i) {
				cnt = fprintf(fp, ",");
				if (cnt <= 0)
					return cnt;
			}
			cnt = fprintf(fp, "%"PRIu64, val->a_u64[i]);
			if (cnt <= 0)
				return cnt;
		}
		cnt = fprintf(fp, "]");
		break;
	case LDMS_V_S64:
		cnt = fprintf(fp, "%"PRId64, val->v_s64);
		break;
	case LDMS_V_S64_ARRAY:
		cnt = fprintf(fp, "[");
		if (cnt <= 0)
			break;
		for (i = 0; i < n; i++) {
			if (i) {
				cnt = fprintf(fp, ",");
				if (cnt <= 0)
					return cnt;
			}
			cnt = fprintf(fp, "%"PRId64, val->a_s64[i]);
			if (cnt <= 0)
				return cnt;
		}
		cnt = fprintf(fp, "]");
		break;
	case LDMS_V_F32:
		cnt = fprintf(fp, "%f", val->v_f);
		break;
	case LDMS_V_F32_ARRAY:
		cnt = fprintf(fp, "[");
		for (i = 0; i < n; i++) {
			if (i) {
				cnt = fprintf(fp, ",");
				if (cnt <= 0)
					return cnt;
			}
			cnt = fprintf(fp, "%f", val->a_f[i]);
			if (cnt <= 0)
				return cnt;
		}
		cnt = fprintf(fp, "]");
		break;
	case LDMS_V_D64:
		cnt = fprintf(fp, "%f", val->v_d);
		break;
	case LDMS_V_D64_ARRAY:
		cnt = fprintf(fp, "[");
		for (i = 0; i < n; i++) {
			if (i) {
				cnt = fprintf(fp, ",");
				if (cnt <= 0)
					return cnt;
			}
			cnt = fprintf(fp, "%f", val->a_d[i]);
			if (cnt <= 0)
				return cnt;
		}
		cnt = fprintf(fp, "]");
		break;
	case LDMS_V_TIMESTAMP:
		cnt = fprintf(fp, "%u.%06u", val->v_ts.sec, val->v_ts.usec);
		break;
	case LDMS_V_RECORD_TYPE:
		assert(0 == "RECORD_TYPE is not a primitive type");
		break;
	case LDMS_V_RECORD_INST:
		assert(0 == "RECORD_INST is not a primitive type");
		break;
	case LDMS_V_RECORD_ARRAY:
		assert(0 == "RECORD_ARRAY is not a primitive type");
		break;
	case LDMS_V_LIST:
		assert(0 == "LIST is not a primitive type");
		break;
	default:
		assert(0 ==  "Unknown metric type");
	}

	return cnt;
}

/*
* An LDMS Record is represented as follows:
*
* {
*   "<metric-name-1>" : { "type" : "<metric-type-str>", "units" : <units>, "value" : <value> },
*   "<metric-name-2>" : { "type" : "<metric-type-str>", "units" : <units>, "value" : <value> },
*  . . .
*   "<metric-name-N>" : { "type" : "<metric-type-str>", "units" : <units>, "value" : <value> }
* }
*/
static int fprint_record_json(FILE *fp, ldms_mval_t rec_inst)
{
	int i;
	size_t array_len;
	enum ldms_value_type type;
	ldms_mval_t mval;
	int cnt;
	const char *units;

	cnt = fprintf(fp, "{");
	if(cnt <= 0)
		return cnt;
	for (i = 0; i < ldms_record_card(rec_inst); i++) {
		cnt = fprintf(fp, "\"%s\" : { ",
				ldms_record_metric_name_get(rec_inst, i));
		if(cnt <= 0)
			return cnt;
		type = ldms_record_metric_type_get(rec_inst, i, &array_len);
		cnt = fprintf(fp, "\"type\" : \"%s\", \"units\" : \"%s\", ",
				ldms_metric_type_to_str(type),
				(units = ldms_record_metric_unit_get(rec_inst, i)) == NULL ? "" : units);
		if(cnt <= 0)
			return cnt;
		mval = ldms_record_metric_get(rec_inst, i);
		cnt = fprintf(fp, "\"value\" : ");
		if(cnt <= 0)
			return cnt;
		cnt = prim_value_as_json(fp, type, mval, array_len);
		if(cnt <= 0)
			return cnt;
		cnt = fprintf(fp, "}");
		if(cnt <= 0)
			return cnt;
		if (i < ldms_record_card(rec_inst)-1) {
			cnt = fprintf(fp, ",");
			if(cnt <= 0)
				return cnt;
		}
	}
	return fprintf(fp, "}");
}

/*
 * An LDMS List is formatted as follows:
 *
 * [
 *   <value>, <value>, ... <value>
 * ]
 */
static int fprint_list_json(FILE *fp, ldms_set_t s, ldms_mval_t list_mval)
{
	enum ldms_value_type item_type;
	ldms_mval_t list_entry;
	size_t array_len;
	int item_count = 0;
	int cnt;

	cnt = fprintf(fp, "[");
	if (cnt <= 0)
		return cnt;

	for (list_entry = ldms_list_first(s, list_mval, &item_type, &array_len);
	     list_entry != NULL;
	     list_entry = ldms_list_next(s, list_entry, &item_type, &array_len))
	{
		if (item_count) {
			cnt = fprintf(fp, ", ");
			if (cnt <= 0)
				return cnt;
		}
		switch (item_type) {
		case LDMS_V_RECORD_INST:
			cnt = fprint_record_json(fp, list_entry);
			if (cnt <= 0)
				return cnt;
			break;
		case LDMS_V_LIST:
			cnt = fprint_list_json(fp, s, list_entry);
			if (cnt <= 0)
				return cnt;
			break;
		default:
			cnt = prim_value_as_json(fp,
				 item_type, list_entry, array_len);
			if (cnt <= 0)
				return cnt;
			break;
		}
		item_count++;
	}

	return fprintf(fp, "]");
}

size_t ldms_fprint_set_as_json(FILE *fp, ldms_set_t s)
{
	int i;
	struct ldms_timestamp ts;
	enum ldms_value_type type;
	int cnt;
	const char *units;

	cnt = fprintf(fp, "{ \"schema_name\" : \"%s\",\n",
		       ldms_set_schema_name_get(s));
	if (cnt <= 0)
		return cnt;
	cnt = fprintf(fp, "  \"inst_name\" : \"%s\",\n",
		       ldms_set_instance_name_get(s));
	if (cnt <= 0)
		return cnt;
	ts = ldms_transaction_timestamp_get(s);
	cnt = fprintf(fp, "  \"timestamp\" : %0d.%06d,\n", ts.sec, ts.usec);
	if (cnt <= 0)
		return cnt;
	ts = ldms_transaction_duration_get(s);
	cnt = fprintf(fp, "  \"duration\" : %0d.%06d,\n", ts.sec, ts.usec);
	if (cnt <= 0)
		return cnt;
	cnt = fprintf(fp, "  \"meta_size\" : %d,\n", ldms_set_meta_sz_get(s));
	if (cnt <= 0)
		return cnt;
	cnt = fprintf(fp, "  \"data_size\" : %d,\n", ldms_set_data_sz_get(s));
	if (cnt <= 0)
		return cnt;
	cnt = fprintf(fp, "  \"heap_size\" : %ld,\n", ldms_set_heap_size_get(s));
	if (cnt <= 0)
		return cnt;
	cnt = fprintf(fp, "  \"uid\" : %d,\n", ldms_set_uid_get(s));
	if (cnt <= 0)
		return cnt;
	cnt = fprintf(fp, "  \"gid\" : %d,\n", ldms_set_gid_get(s));
	if (cnt <= 0)
		return cnt;
	cnt = fprintf(fp, "  \"perm\" : \"0%o\",\n", ldms_set_perm_get(s));
	if (cnt <= 0)
		return cnt;
	cnt = fprintf(fp, "  \"info\" : \"%s\",\n", "");
	if (cnt <= 0)
		return cnt;
	cnt = fprintf(fp, "  \"metrics\" : [\n");
	if (cnt <= 0)
		return cnt;
	for (i = 0; i < ldms_set_card_get(s); i++) {
		type = ldms_metric_type_get(s, i);
		if (type == LDMS_V_RECORD_TYPE)
			continue;
		if (i && i < ldms_set_card_get(s)) {
			cnt = fprintf(fp, ", "); /* next element */
			if (cnt <= 0)
				return cnt;
		}
		ldms_mval_t mv = ldms_metric_get(s, i);
		if (type == LDMS_V_LIST) {
			type = ldms_metric_type_get(s, i);
			cnt = fprintf(fp,
					"{ \"name\" : \"%s\", \"type\" : \"%s\", "
					"\"units\" : \"%s\", \"value\" : ",
					ldms_metric_name_get(s, i),
					ldms_metric_type_to_str(type),
					(units = ldms_metric_unit_get(s, i)) == NULL
						 ? "" : units);
			if (cnt <= 0)
				return cnt;
			cnt = fprint_list_json(fp, s, mv);
			if (cnt <= 0)
				return cnt;
			cnt = fprintf(fp, " }");
			if (cnt <= 0)
				return cnt;
		} else {
			uint32_t count = ldms_metric_array_get_len(s, i);
			type = ldms_metric_type_get(s, i);
			cnt = fprintf(fp,
				"{\"name\" : \"%s\", \"type\" : \"%s\", \"units\" : \"%s\", \"value\" : ",
				ldms_metric_name_get(s, i),
				ldms_metric_type_to_str(type),
				(units = ldms_metric_unit_get(s, i)) == NULL
					 ? "" : units);
			cnt = prim_value_as_json(fp, type, mv, count);
			if (cnt <= 0)
				return cnt;
			cnt = fprintf(fp, "}");
			if (cnt < 0)
				return cnt;
		}
	}
	cnt = fprintf(fp, "]}");	/* end metrics list */
	return cnt;
}

char *ldms_set_as_json_string(ldms_set_t s, size_t *buf_len)
{
	char *buf = NULL;
	int cnt;
	FILE *fp;

	*buf_len = 0;
	fp = open_memstream(&buf, buf_len);
	if (!fp)
		goto err;

	cnt = ldms_fprint_set_as_json(fp, s);
	fclose(fp);
	if (cnt < 0)
		goto err;

	return buf;
 err:
	errno = ENOMEM;
	free(buf);
	return NULL;
}
