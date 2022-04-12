/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2022 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2022 Open Grid Computing, Inc. All rights reserved.
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

#include <assert.h>
#include <stdio.h>
#include "ldms.h"

#define ARRAY_LEN 5
#define LIST_LEN 1
#define VALUE 123
#define VALUE_CHAR 'a'
#define SCHEMA_NAME "my_schema"
#define SET_NAME "my_set"
#define RECORD_DEF_NAME "my_record"
#define LIST_NAME "list"

static void mval_set(ldms_mval_t mv, enum ldms_value_type type)
{
	int i;
	switch (type) {
	case LDMS_V_CHAR:
		ldms_mval_set_char(mv, VALUE_CHAR);
		break;
	case LDMS_V_U8:
		ldms_mval_set_u8(mv, VALUE);
		break;
	case LDMS_V_S8:
		ldms_mval_set_s8(mv, VALUE);
		break;
	case LDMS_V_U16:
		ldms_mval_set_u16(mv, VALUE);
		break;
	case LDMS_V_S16:
		ldms_mval_set_s16(mv, VALUE);
		break;
	case LDMS_V_U32:
		ldms_mval_set_u32(mv, VALUE);
		break;
	case LDMS_V_S32:
		ldms_mval_set_s32(mv, VALUE);
		break;
	case LDMS_V_U64:
		ldms_mval_set_u64(mv, VALUE);
		break;
	case LDMS_V_S64:
		ldms_mval_set_s64(mv, VALUE);
		break;
	case LDMS_V_F32:
		ldms_mval_set_float(mv, VALUE);
		break;
	case LDMS_V_D64:
		ldms_mval_set_double(mv, VALUE);
		break;
	case LDMS_V_CHAR_ARRAY:
		for (i = 0; i < ARRAY_LEN; i++)
			ldms_mval_array_set_char(mv, i, VALUE_CHAR);
		break;
	case LDMS_V_U8_ARRAY:
		for (i = 0; i < ARRAY_LEN; i++)
			ldms_mval_array_set_u8(mv, i, VALUE);
		break;
	case LDMS_V_S8_ARRAY:
		for (i = 0; i < ARRAY_LEN; i++)
			ldms_mval_array_set_s8(mv, i, VALUE);
		break;
	case LDMS_V_U16_ARRAY:
		for (i = 0; i < ARRAY_LEN; i++)
			ldms_mval_array_set_u16(mv, i, VALUE);
		break;
	case LDMS_V_S16_ARRAY:
		for (i = 0; i < ARRAY_LEN; i++)
			ldms_mval_array_set_s16(mv, i, VALUE);
		break;
	case LDMS_V_U32_ARRAY:
		for (i = 0; i < ARRAY_LEN; i++)
			ldms_mval_array_set_u32(mv, i, VALUE);
		break;
	case LDMS_V_S32_ARRAY:
		for (i = 0; i < ARRAY_LEN; i++)
			ldms_mval_array_set_s32(mv, i, VALUE);
		break;
	case LDMS_V_U64_ARRAY:
		for (i = 0; i < ARRAY_LEN; i++)
			ldms_mval_array_set_u64(mv, i, VALUE);
		break;
	case LDMS_V_S64_ARRAY:
		for (i = 0; i < ARRAY_LEN; i++)
			ldms_mval_array_set_s64(mv, i, VALUE);
		break;
	case LDMS_V_F32_ARRAY:
		for (i = 0; i < ARRAY_LEN; i++)
			ldms_mval_array_set_float(mv, i, VALUE);
		break;
	case LDMS_V_D64_ARRAY:
		for (i = 0; i < ARRAY_LEN; i++)
			ldms_mval_array_set_double(mv, i, VALUE);
		break;
	default:
		assert(0);
	}
}

static int mval_verify(ldms_mval_t mv, enum ldms_value_type type)
{
	int i, cnt = 0;
	switch (type) {
	case LDMS_V_CHAR:
		return (VALUE_CHAR == ldms_mval_get_char(mv));
	case LDMS_V_U8:
		return (VALUE == ldms_mval_get_u8(mv));
	case LDMS_V_S8:
		return (VALUE == ldms_mval_get_s8(mv));
	case LDMS_V_U16:
		return (VALUE == ldms_mval_get_u16(mv));
	case LDMS_V_S16:
		return (VALUE == ldms_mval_get_s16(mv));
	case LDMS_V_U32:
		return (VALUE == ldms_mval_get_u32(mv));
	case LDMS_V_S32:
		return (VALUE == ldms_mval_get_s32(mv));
	case LDMS_V_U64:
		return (VALUE == ldms_mval_get_u64(mv));
	case LDMS_V_S64:
		return (VALUE == ldms_mval_get_s64(mv));
	case LDMS_V_F32:
		return (VALUE == ldms_mval_get_float(mv));
	case LDMS_V_D64:
		return (VALUE == ldms_mval_get_double(mv));
	case LDMS_V_CHAR_ARRAY:
		for (i = 0; i < ARRAY_LEN; i++)
			cnt += (VALUE_CHAR == ldms_mval_array_get_char(mv, i));
		return (cnt == ARRAY_LEN);
	case LDMS_V_U8_ARRAY:
		for (i = 0; i < ARRAY_LEN; i++)
			cnt += (VALUE == ldms_mval_array_get_u8(mv, i));
		return (cnt == ARRAY_LEN);
	case LDMS_V_S8_ARRAY:
		for (i = 0; i < ARRAY_LEN; i++)
			cnt += (VALUE == ldms_mval_array_get_s8(mv, i));
		return (cnt == ARRAY_LEN);
	case LDMS_V_U16_ARRAY:
		for (i = 0; i < ARRAY_LEN; i++)
			cnt += (VALUE == ldms_mval_array_get_u16(mv, i));
		return (cnt == ARRAY_LEN);
	case LDMS_V_S16_ARRAY:
		for (i = 0; i < ARRAY_LEN; i++)
			cnt += (VALUE == ldms_mval_array_get_s16(mv, i));
		return (cnt == ARRAY_LEN);
	case LDMS_V_U32_ARRAY:
		for (i = 0; i < ARRAY_LEN; i++)
			cnt += (VALUE == ldms_mval_array_get_u32(mv, i));
		return (cnt == ARRAY_LEN);
	case LDMS_V_S32_ARRAY:
		for (i = 0; i < ARRAY_LEN; i++)
			cnt += (VALUE == ldms_mval_array_get_s32(mv, i));
		return (cnt == ARRAY_LEN);
	case LDMS_V_U64_ARRAY:
		for (i = 0; i < ARRAY_LEN; i++)
			cnt += (VALUE == ldms_mval_array_get_u64(mv, i));
		return (cnt == ARRAY_LEN);
	case LDMS_V_S64_ARRAY:
		for (i = 0; i < ARRAY_LEN; i++)
			cnt += (VALUE == ldms_mval_array_get_s64(mv, i));
		return (cnt == ARRAY_LEN);
	case LDMS_V_F32_ARRAY:
		for (i = 0; i < ARRAY_LEN; i++)
			cnt += (VALUE == ldms_mval_array_get_float(mv, i));
		return (cnt == ARRAY_LEN);
	case LDMS_V_D64_ARRAY:
		for (i = 0; i < ARRAY_LEN; i++)
			cnt += (VALUE == ldms_mval_array_get_double(mv, i));
		return (cnt == ARRAY_LEN);
	default:
		assert(0);
	}
	return 0;
}

int main(int argc, char **argv) {
	int i;
	enum ldms_value_type type;
	ldms_schema_t schema;
	ldms_set_t set;
	ldms_record_t rec_def;
	char *s;
	int list_mid;
	int rec_def_mid;
	size_t card;
	ldms_mval_t mval, lh, rec;
	size_t cnt;

	ldms_init(1024);

	schema = ldms_schema_new(SCHEMA_NAME);
	assert(schema);

	rec_def = ldms_record_create(RECORD_DEF_NAME);
	assert(rec_def);
	for (type = LDMS_V_FIRST; type < LDMS_V_LAST; type++) {
		s = (char *)ldms_metric_type_to_str(type);
		if ((LDMS_V_RECORD_TYPE == type) ||
			(LDMS_V_RECORD_INST == type) ||
			(LDMS_V_LIST == type) ||
			(LDMS_V_LIST_ENTRY == type)) {
			continue;
		} else {
			ldms_record_metric_add(rec_def, s, "", type, ARRAY_LEN);
		}
	}
	rec_def_mid = ldms_schema_record_add(schema, rec_def);
	list_mid = ldms_schema_metric_list_add(schema, LIST_NAME, "",
						ldms_record_heap_size_get(rec_def));

	set = ldms_set_new(SET_NAME, schema);
	assert(set);

	ldms_transaction_begin(set);
	lh = ldms_metric_get(set, list_mid);
	rec = ldms_record_alloc(set, rec_def_mid);
	assert(rec);
	card = ldms_record_card(rec);
	for (i = 0; i < card; i++) {
		type = ldms_record_metric_type_get(rec, i, &cnt);
		mval = ldms_record_metric_get(rec, i);
		mval_set(mval, type);
	}
	ldms_list_append_record(set, lh, rec);
	ldms_transaction_end(set);

	rec = ldms_list_first(set, lh, &type, &cnt);
	for (i = 0; i < card; i++) {
		mval = ldms_record_metric_get(rec, i);
		type = ldms_record_metric_type_get(rec, i, &cnt);
		printf("%s: ", ldms_metric_type_to_str(type));
		if (!mval_verify(mval, type)) {
			printf("failed\n");
		} else {
			printf("passed\n");
		}
	}
	printf("done\n");
	return 0;
}
