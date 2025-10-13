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

static char *json_encode_str(char *dst, size_t dst_len, char *src)
{
	char *d, *s;
	size_t buf_len = dst_len;

	d = dst;
	s = src;

	while (*s != '\0' || !buf_len) {
		switch (*s) {
		case '\\':
			/* Value is already escaped */
			*d++ = *s++;
			buf_len--;
			if (*s == '\0' || !buf_len)
				continue;
			*d++ = *s++;
			buf_len --;
			break;
		case '"':
			/* Value needs to be escaped */
			*d++ = '\\';
			buf_len --;
			if (*s == '\0' || !buf_len)
				continue;
			*d++ = *s++;
			buf_len --;
			break;
		default:
			*d++ = *s++;
			buf_len --;
		}
	}
	*d = '\0';
	return dst;
}

static char *prim_value_as_json(char *buf, size_t buf_len,
				enum ldms_value_type type,
				ldms_mval_t val, size_t n)
{
	int i;
	char *s, *fmt;
	char vbuf[1024];
	int cnt = 0;

	switch (type) {
	case LDMS_V_CHAR_ARRAY:
		s = json_encode_str(vbuf, sizeof(vbuf), val->a_char);
		if (*s == '\"')
			fmt = "%s";
		else
			fmt = "\"%s\"";
		cnt = snprintf(buf, buf_len, fmt, s);
		break;
	case LDMS_V_CHAR:
		cnt = snprintf(buf, buf_len, "\"%c\"", val->v_char);
		break;
	case LDMS_V_U8:
		cnt = snprintf(buf, buf_len, "%hhu", val->v_u8);
		break;
	case LDMS_V_U8_ARRAY:
		cnt = snprintf(&buf[cnt], buf_len - cnt, "[");
		for (i = 0; i < n; i++) {
			if (i)
				cnt += snprintf(&buf[cnt], buf_len - cnt, ",");
			cnt += snprintf(&buf[cnt], buf_len - cnt, "%hhu", val->a_u8[i]);
		}
		cnt += snprintf(&buf[cnt], buf_len - cnt, "]");
		break;
	case LDMS_V_S8:
		cnt = snprintf(buf, buf_len, "%hhd", val->v_s8);
		break;
	case LDMS_V_S8_ARRAY:
		cnt = snprintf(&buf[cnt], buf_len - cnt, "[");
		for (i = 0; i < n; i++) {
			if (i)
				cnt += snprintf(&buf[cnt], buf_len - cnt, ",");
			cnt += snprintf(&buf[cnt], buf_len - cnt, "%hhd", val->a_s8[i]);
		}
		cnt += snprintf(&buf[cnt], buf_len - cnt, "]");
		break;
	case LDMS_V_U16:
		cnt = snprintf(buf, buf_len, "%hu", val->v_u16);
		break;
	case LDMS_V_U16_ARRAY:
		cnt = snprintf(&buf[cnt], buf_len - cnt, "[");
		for (i = 0; i < n; i++) {
			if (i)
				cnt += snprintf(&buf[cnt], buf_len, ",");
			cnt += snprintf(buf, buf_len - cnt, "%hu", val->a_u16[i]);
		}
		cnt += snprintf(&buf[cnt], buf_len - cnt, "]");
		break;
	case LDMS_V_S16:
		cnt = snprintf(buf, buf_len, "%hd", val->v_s16);
		break;
	case LDMS_V_S16_ARRAY:
		cnt = snprintf(&buf[cnt], buf_len - cnt, "[");
		for (i = 0; i < n; i++) {
			if (i)
				cnt += snprintf(&buf[cnt], buf_len - cnt, ",");
			cnt += snprintf(&buf[cnt], buf_len - cnt, "%hd", val->a_s16[i]);
		}
		cnt += snprintf(&buf[cnt], buf_len - cnt, "]");
		break;
	case LDMS_V_U32:
		cnt = snprintf(buf, buf_len, "%u", val->v_u32);
		break;
	case LDMS_V_U32_ARRAY:
		cnt = snprintf(&buf[cnt], buf_len - cnt, "[");
		for (i = 0; i < n; i++) {
			if (i)
				cnt += snprintf(&buf[cnt], buf_len - cnt, ",");
			cnt += snprintf(&buf[cnt], buf_len - cnt, "%u", val->a_u32[i]);
		}
		cnt += snprintf(&buf[cnt], buf_len - cnt, "]");
		break;
	case LDMS_V_S32:
		cnt = snprintf(buf, buf_len, "%d", val->v_s32);
		break;
	case LDMS_V_S32_ARRAY:
		cnt = snprintf(&buf[cnt], buf_len - cnt, "[");
		for (i = 0; i < n; i++) {
			if (i)
				cnt += snprintf(&buf[cnt], buf_len - cnt, ",");
			cnt += snprintf(&buf[cnt], buf_len - cnt, "%d", val->a_s32[i]);
		}
		cnt += snprintf(&buf[cnt], buf_len - cnt, "]");
		break;
	case LDMS_V_U64:
		cnt = snprintf(buf, buf_len, "%"PRIu64, val->v_u64);
		break;
	case LDMS_V_U64_ARRAY:
		cnt = snprintf(&buf[cnt], buf_len - cnt, "[");
		for (i = 0; i < n; i++) {
			if (i)
				cnt += snprintf(&buf[cnt], buf_len - cnt, ",");
			cnt += snprintf(&buf[cnt], buf_len - cnt, "%"PRIu64, val->a_u64[i]);
		}
		cnt += snprintf(&buf[cnt], buf_len - cnt, "]");
		break;
	case LDMS_V_S64:
		cnt = snprintf(buf, buf_len, "%"PRId64, val->v_s64);
		break;
	case LDMS_V_S64_ARRAY:
		cnt = snprintf(&buf[cnt], buf_len - cnt, "[");
		for (i = 0; i < n; i++) {
			if (i)
				cnt += snprintf(&buf[cnt], buf_len - cnt, ",");
			cnt += snprintf(&buf[cnt], buf_len - cnt, "%"PRId64, val->a_s64[i]);
		}
		cnt += snprintf(&buf[cnt], buf_len - cnt, "]");
		break;
	case LDMS_V_F32:
		cnt = snprintf(buf, buf_len, "%f", val->v_f);
		break;
	case LDMS_V_F32_ARRAY:
		cnt = snprintf(&buf[cnt], buf_len - cnt, "[");
		for (i = 0; i < n; i++) {
			if (i)
				cnt += snprintf(&buf[cnt], buf_len - cnt, ",");
			cnt += snprintf(&buf[cnt], buf_len - cnt, "%f", val->a_f[i]);
		}
		cnt += snprintf(&buf[cnt], buf_len - cnt, "]");
		break;
	case LDMS_V_D64:
		cnt += snprintf(&buf[cnt], buf_len - cnt, "%f", val->v_d);
		break;
	case LDMS_V_D64_ARRAY:
		cnt = snprintf(&buf[cnt], buf_len - cnt, "[");
		for (i = 0; i < n; i++) {
			if (i)
				cnt += snprintf(&buf[cnt], buf_len - cnt, ",");
			cnt += snprintf(&buf[cnt], buf_len - cnt, "%f", val->a_d[i]);
		}
		cnt += snprintf(&buf[cnt], buf_len - cnt, "]");
		break;
	case LDMS_V_TIMESTAMP:
		cnt = snprintf(buf, buf_len, "%u.%06u", val->v_ts.sec, val->v_ts.usec);
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
	return buf;
}

static void fprint_record_json(FILE *fp, int indent, ldms_mval_t rec_inst)
{
	int i;
	size_t array_len;
	enum ldms_value_type type;
	ldms_mval_t mval;
	char buf[1024];

	for (i = 0; i < ldms_record_card(rec_inst); i++) {
		fprintf(fp, "%*s", indent, "");
		fprintf(fp, "%*s{ \"name\" : \"%s\", ", indent, "",
			ldms_record_metric_name_get(rec_inst, i));
		type = ldms_record_metric_type_get(rec_inst, i, &array_len);
		fprintf(fp, "\"type\" : \"%s\", ", ldms_metric_type_to_str(type));
		mval = ldms_record_metric_get(rec_inst, i);
		fprintf(fp, "\"value\" : %s }",
			prim_value_as_json(buf, sizeof(buf),
					   type, mval, array_len));
		if (i < ldms_record_card(rec_inst)-1)
			fprintf(fp, ",\n");
	}

}

static void fprint_list_json(FILE *fp, int indent, ldms_set_t s, ldms_mval_t list_mval)
{
	enum ldms_value_type item_type;
	ldms_mval_t rec_inst_val;
	size_t array_len;

	fprintf(fp, "%*s[\n", indent, "");
	rec_inst_val = ldms_list_first(s, list_mval,
				       &item_type, &array_len);
	while (rec_inst_val) {
		fprint_record_json(fp, indent+4, rec_inst_val);
		rec_inst_val = ldms_list_next(s, list_mval, &item_type, &array_len);
	} while (rec_inst_val);
	fprintf(fp, "\n%*s]\n", indent, "");
}

size_t ldms_fprint_set_as_json(FILE *fp, ldms_set_t s, char *fmt)
{
	int i;
	struct ldms_timestamp ts;
	enum ldms_value_type type;
	int cnt, tot_cnt = 0;

	cnt = fprintf(fp, "{ \"schema_name\" : \"%s\",\n",
		       ldms_set_schema_name_get(s));
	if (cnt < 0)
		goto err;
	tot_cnt += cnt;
	cnt = fprintf(fp, "  \"inst_name\" : \"%s\",\n",
		       ldms_set_instance_name_get(s));
	if (cnt < 0)
		goto err;
	tot_cnt += cnt;
	ts = ldms_transaction_timestamp_get(s);
	cnt = fprintf(fp, "  \"timestamp\" : %0d.%06d,\n", ts.sec, ts.usec);
	if (cnt < 0)
		goto err;
	tot_cnt += cnt;
	ts = ldms_transaction_duration_get(s);
	cnt = fprintf(fp, "  \"duration\" : %0d.%06d,\n", ts.sec, ts.usec);
	if (cnt < 0)
		goto err;
	tot_cnt += cnt;
	cnt = fprintf(fp, "  \"meta_size\" : %d,\n", ldms_set_meta_sz_get(s));
	if (cnt < 0)
		goto err;
	tot_cnt += cnt;
	cnt = fprintf(fp, "  \"data_size\" : %d,\n", ldms_set_data_sz_get(s));
	if (cnt < 0)
		goto err;
	tot_cnt += cnt;
	cnt = fprintf(fp, "  \"heap_size\" : %ld,\n", ldms_set_heap_size_get(s));
	if (cnt < 0)
		goto err;
	tot_cnt += cnt;
	cnt = fprintf(fp, "  \"uid\" : %d,\n", ldms_set_uid_get(s));
	if (cnt < 0)
		goto err;
	tot_cnt += cnt;
	cnt = fprintf(fp, "  \"gid\" : %d,\n", ldms_set_gid_get(s));
	if (cnt < 0)
		goto err;
	tot_cnt += cnt;
	cnt = fprintf(fp, "  \"perm\" : \"0%o\",\n", ldms_set_perm_get(s));
	if (cnt < 0)
		goto err;
	tot_cnt += cnt;
	cnt = fprintf(fp, "  \"info\" : \"%s\",\n", "");
	if (cnt < 0)
		goto err;
	tot_cnt += cnt;
	cnt = fprintf(fp, "  \"metrics\" : [\n");
	if (cnt < 0)
		goto err;
	tot_cnt += cnt;
	for (i = 0; i < ldms_set_card_get(s); i++) {
		type = ldms_metric_type_get(s, i);
		if (type == LDMS_V_RECORD_TYPE)
			continue;
		if (i && i < ldms_set_card_get(s))
			cnt = fprintf(fp, ",\n"); /* next element */
		if (cnt < 0)
			goto err;
		tot_cnt += cnt;
		ldms_mval_t mv = ldms_metric_get(s, i);
		if (type == LDMS_V_LIST) {
			cnt = fprintf(fp, "  { \"name\" : \"%s\", \"value\" : ",
				       ldms_metric_name_get(s, i));
			if (cnt < 0)
				goto err;
			tot_cnt += cnt;
			fprint_list_json(fp, 0, s, mv);
			cnt = fprintf(fp, "  }"); /* end list-value */
			if (cnt < 0)
				goto err;
			tot_cnt += cnt;
		} else {
			uint32_t count = ldms_metric_array_get_len(s, i);
			char vbuf[1024];
			cnt = fprintf(fp,
				       "%*s{\"name\" : \"%s\", \"value\" : %s, "
				"\"units\" : \"%s\" }",
				0, "",
				ldms_metric_name_get(s, i),
				prim_value_as_json(vbuf, sizeof(vbuf),
						   type, mv, count),
				ldms_metric_unit_get(s, i));
			if (cnt < 0)
				goto err;
			tot_cnt += cnt;
		}
	}
	cnt = fprintf(fp, "]\n");	/* end metrics list */
	if (cnt < 0)
		goto err;
	tot_cnt += cnt;
	cnt = fprintf(fp, "}\n");	/* end set */
	if (cnt < 0)
		goto err;
	tot_cnt += cnt;
 err:
	return cnt;
}

char *ldms_set_as_json_string(ldms_set_t s, char *fmt)
{
	char *buf = NULL;
	size_t buf_len = 0;
	int cnt;
	FILE *fp;

	fp = open_memstream(&buf, &buf_len);
	if (!fp)
		goto err;

	cnt = ldms_fprint_set_as_json(fp, s, fmt);
	fclose(fp);
	if (cnt < 0)
		goto err;

	return buf;
 err:
	free(buf);
	return NULL;
}
