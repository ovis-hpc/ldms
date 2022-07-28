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
#define _GNU_SOURCE

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>

#include <openssl/evp.h>

#include "ovis_json/ovis_json.h"
#include "coll/rbt.h"

#include "ldmsd.h"
#include "ldmsd_request.h"

static ldmsd_decomp_t __decomp_static_config(ldmsd_strgp_t strgp,
			json_entity_t cfg, ldmsd_req_ctxt_t reqc);
static int __decomp_static_decompose(ldmsd_strgp_t strgp, ldms_set_t set,
				     ldmsd_row_list_t row_list, int *row_count);
static void __decomp_static_release_rows(ldmsd_strgp_t strgp,
					 ldmsd_row_list_t row_list);
static void __decomp_static_release_decomp(ldmsd_strgp_t strgp);

struct ldmsd_decomp_s __decomp_static = {
	.config = __decomp_static_config,
	.decompose = __decomp_static_decompose,
	.release_rows = __decomp_static_release_rows,
	.release_decomp = __decomp_static_release_decomp,
};

ldmsd_decomp_t get()
{
	return &__decomp_static;
}

/* ==== JSON helpers ==== */

static json_entity_t __jdict_ent(json_entity_t dict, const char *key)
{
	json_entity_t attr;
	json_entity_t val;

	attr = json_attr_find(dict, key);
	if (!attr) {
		errno = ENOKEY;
		return NULL;
	}
	val = json_attr_value(attr);
	return val;
}

/* Access dict[key], expecting it to be a list */
static json_list_t __jdict_list(json_entity_t dict, const char *key)
{
	json_entity_t val;

	val = __jdict_ent(dict, key);
	if (!val)
		return NULL;
	if (val->type != JSON_LIST_VALUE) {
		errno = EINVAL;
		return NULL;
	}
	return val->value.list_;
}

/* Access dict[key], expecting it to be a str */
static json_str_t __jdict_str(json_entity_t dict, const char *key)
{
	json_entity_t val;

	val = __jdict_ent(dict, key);
	if (!val)
		return NULL;
	if (val->type != JSON_STRING_VALUE) {
		errno = EINVAL;
		return NULL;
	}
	return val->value.str_;
}

static enum json_value_e __ldms_json_type(enum ldms_value_type type)
{
	switch (type) {
	case LDMS_V_CHAR: /* maps to json string of 1 character */
	case LDMS_V_CHAR_ARRAY:
		return JSON_STRING_VALUE;
	case LDMS_V_U8:
	case LDMS_V_S8:
	case LDMS_V_U8_ARRAY:
	case LDMS_V_S8_ARRAY:
	case LDMS_V_U16:
	case LDMS_V_S16:
	case LDMS_V_U16_ARRAY:
	case LDMS_V_S16_ARRAY:
	case LDMS_V_U32:
	case LDMS_V_S32:
	case LDMS_V_U32_ARRAY:
	case LDMS_V_S32_ARRAY:
	case LDMS_V_U64:
	case LDMS_V_S64:
	case LDMS_V_U64_ARRAY:
	case LDMS_V_S64_ARRAY:
		return JSON_INT_VALUE;
	case LDMS_V_F32:
	case LDMS_V_F32_ARRAY:
	case LDMS_V_D64:
	case LDMS_V_D64_ARRAY:
		return JSON_FLOAT_VALUE;
	default:
		assert(0 == "Not supported");
		return JSON_NULL_VALUE;
	}
}

static int __array_fill_from_json(ldms_mval_t v, enum ldms_value_type type,
				  json_list_t jlist)
{
	int i;
	json_entity_t ent;
	enum json_value_e jtype;

	jtype = __ldms_json_type(type);
	i = 0;
	TAILQ_FOREACH(ent, &jlist->item_list, item_entry) {
		if (ent->type != jtype)
			return EINVAL;
		switch (type) {
		case LDMS_V_U8_ARRAY:
			v->a_u8[i] = (uint8_t)ent->value.int_;
			break;
		case LDMS_V_S8_ARRAY:
			v->a_s8[i] = (int8_t)ent->value.int_;
			break;
		case LDMS_V_U16_ARRAY:
			v->a_u16[i] = htole16((uint16_t)ent->value.int_);
			break;
		case LDMS_V_S16_ARRAY:
			v->a_s16[i] = htole16((int16_t)ent->value.int_);
			break;
		case LDMS_V_U32_ARRAY:
			v->a_u32[i] = htole32((uint32_t)ent->value.int_);
			break;
		case LDMS_V_S32_ARRAY:
			v->a_s32[i] = htole32((int32_t)ent->value.int_);
			break;
		case LDMS_V_U64_ARRAY:
			v->a_u64[i] = htole64((uint64_t)ent->value.int_);
			break;
		case LDMS_V_S64_ARRAY:
			v->a_s64[i] = htole64((int64_t)ent->value.int_);
			break;
		case LDMS_V_F32_ARRAY:
			v->a_f[i] = htole32((float)ent->value.double_);
			break;
		case LDMS_V_D64_ARRAY:
			v->a_d[i] = htole64((double)ent->value.double_);
			break;
		default:
			return EINVAL;
		}
		i++;
	}
	return 0;
}

static int __prim_fill_from_json(ldms_mval_t v, enum ldms_value_type type,
				      json_entity_t jent)
{
	enum json_value_e jtype;

	jtype = __ldms_json_type(type);
	if (jent->type != jtype)
		return EINVAL;
	switch (type) {
	case LDMS_V_U8:
		v->v_u8 = (uint8_t)jent->value.int_;
		break;
	case LDMS_V_S8:
		v->v_s8 = (int8_t)jent->value.int_;
		break;
	case LDMS_V_U16:
		v->v_u16 = htole16((uint16_t)jent->value.int_);
		break;
	case LDMS_V_S16:
		v->v_s16 = htole16((int16_t)jent->value.int_);
		break;
	case LDMS_V_U32:
		v->v_u32 = htole32((uint32_t)jent->value.int_);
		break;
	case LDMS_V_S32:
		v->v_s32 = htole32((int32_t)jent->value.int_);
		break;
	case LDMS_V_U64:
		v->v_u64 = htole64((uint64_t)jent->value.int_);
		break;
	case LDMS_V_S64:
		v->v_s64 = htole64((int64_t)jent->value.int_);
		break;
	case LDMS_V_F32:
		v->v_f = htole32((float)jent->value.double_);
		break;
	case LDMS_V_D64:
		v->v_d = htole64((double)jent->value.double_);
		break;
	default:
		return EINVAL;
	}
	return 0;
}

/* ==== Helpers ==== */

static inline int __ldms_vsz(enum ldms_value_type t)
{
	switch (t) {
	case LDMS_V_CHAR:
	case LDMS_V_U8:
	case LDMS_V_S8:
	case LDMS_V_CHAR_ARRAY:
	case LDMS_V_U8_ARRAY:
	case LDMS_V_S8_ARRAY:
		return sizeof(char);
	case LDMS_V_U16:
	case LDMS_V_S16:
	case LDMS_V_U16_ARRAY:
	case LDMS_V_S16_ARRAY:
		return sizeof(int16_t);
	case LDMS_V_U32:
	case LDMS_V_S32:
	case LDMS_V_U32_ARRAY:
	case LDMS_V_S32_ARRAY:
		return sizeof(int32_t);
	case LDMS_V_U64:
	case LDMS_V_S64:
	case LDMS_V_U64_ARRAY:
	case LDMS_V_S64_ARRAY:
		return sizeof(int64_t);
	case LDMS_V_F32:
	case LDMS_V_F32_ARRAY:
		return sizeof(float);
	case LDMS_V_D64:
	case LDMS_V_D64_ARRAY:
		return sizeof(double);
	default:
		assert(0 == "Unsupported type");
	}
	return -1;
}


/* ==== generic decomp ==== */
/* convenient macro to put error message in both ldmsd log and `reqc` */
#define DECOMP_ERR(reqc, rc, fmt, ...) do { \
		ldmsd_lerror("decomposer: " fmt, ##__VA_ARGS__); \
		if (reqc) { \
			(reqc)->errcode = rc; \
			Snprintf(&(reqc)->line_buf, &(reqc)->line_len, "decomposer: " fmt, ##__VA_ARGS__); \
		} \
	} while (0)

/* common index config descriptor */
typedef struct __decomp_index_s {
	char *name;
	int col_count;
	int *col_idx; /* dst columns composing the index */
} *__decomp_index_t;

/* ==== static decomposition === */

/* describing a src-dst column pair */
typedef struct __decomp_static_col_cfg_s {
	enum ldms_value_type type; /* value type */
	int array_len; /* length of the array, if type is array */
	char *src; /* name of the metric */
	char *rec_member; /* name of the record metric (if applicable) */
	char *dst; /* destination (storage side) column name */
	ldms_mval_t fill; /* fill value */
	int fill_len; /* if fill is an array */
	union ldms_value __fill; /* storing a non-array primitive fill value */
} *__decomp_static_col_cfg_t;

typedef struct __decomp_static_row_cfg_s {
	char *schema_name; /* row schema name */
	int col_count;
	int idx_count;
	__decomp_static_col_cfg_t cols; /* array of struct */
	__decomp_index_t idxs; /* array of struct */
	struct ldms_digest_s schema_digest;
	size_t row_sz;
	struct rbt mid_rbt; /* collection of metric IDs mapping */
} *__decomp_static_row_cfg_t;

typedef struct __decomp_static_cfg_s {
	struct ldmsd_decomp_s decomp;
	int row_count;
	struct __decomp_static_row_cfg_s rows[OVIS_FLEX];
} *__decomp_static_cfg_t;

typedef struct __decomp_static_mid_rbn_s {
	struct rbn rbn;
	struct ldms_digest_s ldms_digest;
	int col_count;
	struct {
		int mid;
		int rec_mid;
		enum ldms_value_type mtype;
		enum ldms_value_type rec_mtype;
	} col_mids[OVIS_FLEX];
} *__decomp_static_mid_rbn_t;

int __mid_rbn_cmp(void *tree_key, const void *key)
{
	return memcmp(tree_key, key, sizeof(struct ldms_digest_s));
}

/* str - int pair */
struct str_int_s {
	const char *str;
	int i;
};

struct str_int_tbl_s {
	int len;
	struct str_int_s ent[OVIS_FLEX];
};

int str_int_cmp(const void *a, const void *b)
{
	const struct str_int_s *sa = a, *sb = b;
	return strcmp(sa->str, sb->str);
}

static void __decomp_static_cfg_free(__decomp_static_cfg_t dcfg)
{
	int i, j;
	struct __decomp_static_row_cfg_s *drow;
	struct __decomp_static_col_cfg_s *dcol;
	for (i = 0; i < dcfg->row_count; i++) {
		drow = &dcfg->rows[i];
		/* cols */
		for (j = 0; j < drow->col_count; j++) {
			dcol = &drow->cols[j];
			free(dcol->src);
			free(dcol->dst);
			free(dcol->rec_member);
			if (dcol->fill != &dcol->__fill)
				free(dcol->fill);
		}
		free(drow->cols);
		/* idxs */
		for (j = 0; j < drow->idx_count; j++) {
			free(drow->idxs[j].name);
			free(drow->idxs[j].col_idx);
		}
		free(drow->idxs);
		/* schema */
		free(drow->schema_name);
	}
	free(dcfg);
}

static void
__decomp_static_release_decomp(ldmsd_strgp_t strgp)
{
	if (strgp->decomp) {
		__decomp_static_cfg_free((void*)strgp->decomp);
		strgp->decomp = NULL;
	}
}

static ldmsd_decomp_t
__decomp_static_config( ldmsd_strgp_t strgp, json_entity_t jcfg,
			ldmsd_req_ctxt_t reqc )
{
	json_str_t jsch, jsrc, jdst, jrec_member, jname, jtype;
	json_list_t jrows, jcols, jidxs, jidx_cols;
	json_entity_t jrow, jcol, jidx, jfill, jarray_len;
	__decomp_static_cfg_t dcfg = NULL;
	__decomp_static_row_cfg_t drow;
	__decomp_static_col_cfg_t dcol;
	__decomp_index_t didx;
	int i, j, k, rc;
	struct str_int_tbl_s *col_id_tbl = NULL;
	struct str_int_s key, *tbl_ent;
	EVP_MD_CTX *evp_ctx = NULL;

	jrows = __jdict_list(jcfg, "rows");
	if (!jrows) {
		DECOMP_ERR(reqc, errno, "strgp '%s': The 'rows' attribute is missing, "
						     "or its value is not a list.\n",
						     strgp->obj.name);
		goto err_0;
	}
	evp_ctx = EVP_MD_CTX_create();
	if (!evp_ctx) {
		DECOMP_ERR(reqc, errno, "out of memory\n");
		goto err_0;
	}
	dcfg = calloc(1, sizeof(*dcfg) + jrows->item_count*sizeof(dcfg->rows[0]));
	if (!dcfg) {
		DECOMP_ERR(reqc, errno, "out of memory\n");
		goto err_0;
	}
	dcfg->decomp = __decomp_static;

	/* for each row schema */
	i = 0;
	TAILQ_FOREACH(jrow, &jrows->item_list, item_entry) {
		if (jrow->type != JSON_DICT_VALUE) {
			DECOMP_ERR(reqc, EINVAL, "strgp '%s': row '%d': "
					"The list item must be a dictionary.\n",
					strgp->obj.name, i);
			goto err_0;
		}

		drow = &dcfg->rows[i];
		drow->row_sz = sizeof(struct ldmsd_row_s);
		EVP_DigestInit_ex(evp_ctx, EVP_sha256(), NULL);
		rbt_init(&drow->mid_rbt, __mid_rbn_cmp);

		/* schema name */
		jsch = __jdict_str(jrow, "schema");
		if (!jsch) {
			DECOMP_ERR(reqc, EINVAL, "strgp '%s': row '%d': "
					"row['schema'] attribute is required\n",
					strgp->obj.name, i);
			goto err_0;
		}
		drow->schema_name = strdup(jsch->str);
		if (!drow->schema_name)
			goto err_enomem;

		/* columns */
		jcols = __jdict_list(jrow, "cols");
		if (!jcols) {
			DECOMP_ERR(reqc, EINVAL, "strgp '%s': row '%d': "
					"row['cols'] list is required\n",
					strgp->obj.name, i);
			goto err_0;
		}
		drow->col_count = jcols->item_count;
		drow->cols = calloc(1, drow->col_count*sizeof(drow->cols[0]));
		if (!drow->cols)
			goto err_enomem;
		drow->row_sz += drow->col_count * sizeof(struct ldmsd_col_s);
		/* for each column */
		j = 0;
		TAILQ_FOREACH(jcol, &jcols->item_list, item_entry) {
			dcol = &drow->cols[j];
			if (jcol->type != JSON_DICT_VALUE) {
				DECOMP_ERR(reqc, EINVAL, "strgp '%s': row '%d': col '%d': "
						"a column must be a dictionary.\n",
						strgp->obj.name, i, j);
				goto err_0;
			}
			jfill = __jdict_ent(jcol, "fill");
			jsrc = __jdict_str(jcol, "src");
			if (!jsrc) {
				DECOMP_ERR(reqc, EINVAL, "strgp '%s': row '%d'--col '%d': "
						"column['src'] is required\n",
						strgp->obj.name, i, j);
				goto err_0;
			}
			dcol->src = strdup(jsrc->str);
			if (!dcol->src)
				goto err_enomem;
			if (0 == strcmp("timestamp", dcol->src)) {
				/* add space for the phony timestamp metric */
				drow->row_sz += sizeof(union ldms_value);
			}
			jdst = __jdict_str(jcol, "dst");
			if (!jdst) {
				DECOMP_ERR(reqc, EINVAL, "strgp '%s': row '%d'--col '%d': "
						"column['dst'] is required\n",
						strgp->obj.name, i, j);
				goto err_0;
			}
			dcol->dst = strdup(jdst->str);
			if (!dcol->dst)
				goto err_enomem;
			jrec_member = __jdict_str(jcol, "rec_member");
			if (jrec_member) {
				dcol->rec_member = strdup(jrec_member->str);
				if (!dcol->rec_member)
					goto err_enomem;
			}
			jtype = __jdict_str(jcol, "type");
			if (!jtype) {
				DECOMP_ERR(reqc, EINVAL, "strgp '%s': row '%d': col[dst] '%s': "
						"column['type'] is required\n",
						strgp->obj.name, i, dcol->dst);
				goto err_0;
			}
			dcol->type = ldms_metric_str_to_type(jtype->str);
			if (dcol->type == LDMS_V_NONE) {
				DECOMP_ERR(reqc, EINVAL, "strgp '%s': row '%d': col[dst] '%s',"
						"column['type'] value is an unknown type: %s\n",
						strgp->obj.name, i, dcol->dst, jtype->str);
				goto err_0;
			}
			/* update row schema digest */
			EVP_DigestUpdate(evp_ctx, dcol->dst, strlen(dcol->dst));
			EVP_DigestUpdate(evp_ctx, &dcol->type, sizeof(dcol->type));
			if (!ldms_type_is_array(dcol->type))
				goto not_array_fill;
			/* array routine */
			jarray_len = __jdict_ent(jcol, "array_len");
			if (!jarray_len) {
				DECOMP_ERR(reqc, EINVAL, "strgp '%s': row '%d': col[dst] '%s': "
						"column['array_len'] is required.\n",
						strgp->obj.name, i, dcol->dst);
				goto err_0;
			}
			if (jarray_len->type != JSON_INT_VALUE) {
				DECOMP_ERR(reqc, EINVAL, "strgp '%s': row '%d': col[dst] '%s': "
						"column['array_len'] value must be an integer.\n",
						strgp->obj.name, i, dcol->dst);
				goto err_0;
			}
			dcol->array_len = jarray_len->value.int_;

			/* fill */
			dcol->fill = calloc(dcol->array_len, __ldms_vsz(dcol->type));
			if (!dcol->fill)
				goto err_enomem;
			if (dcol->type == LDMS_V_CHAR_ARRAY)
				goto str_fill;
			/* array fill */
			if (!jfill) /* fill values are already 0 */
				goto next_col;
			if (jfill->type != JSON_LIST_VALUE) {
				DECOMP_ERR(reqc, EINVAL, "strgp '%s': row '%d': col[dst] '%s': "
						"'fill' type mismatch: expecting a LIST\n",
						strgp->obj.name, i, dcol->dst);
				goto err_0;
			}
			dcol->fill_len = jfill->value.list_->item_count;
			if (dcol->fill_len > dcol->array_len) {
				DECOMP_ERR(reqc, EINVAL, "strgp '%s': row '%d': col[dst] '%s': "
						"'fill' array length too long\n",
						strgp->obj.name, i, dcol->dst);
				goto err_0;
			}
			rc = __array_fill_from_json(dcol->fill, dcol->type, jfill->value.list_);
			if (rc) {
				DECOMP_ERR(reqc, EINVAL, "strgp '%s': row '%d': col[dst] '%s': "
						"'fill' error: type mismatch\n",
						strgp->obj.name, i, dcol->dst);
				goto err_0;
			}
			goto next_col;
		str_fill:
			/* str fill routine */
			if (!jfill) /* fill values are already 0 */
				goto next_col;
			if (jfill->type != JSON_STRING_VALUE) {
				DECOMP_ERR(reqc, EINVAL, "strgp '%s': row '%d': col[dst] '%s': "
						"'fill' type mismatch: expecting a STRING\n",
						strgp->obj.name, i, dcol->dst);
				goto err_0;
			}
			dcol->fill_len = jfill->value.str_->str_len + 1;
			if (dcol->fill_len > dcol->array_len) {
				DECOMP_ERR(reqc, EINVAL, "strgp '%s': row '%d': col[dst] '%s': "
						"'fill' array length too long\n",
						strgp->obj.name, i, dcol->dst);
				goto err_0;
			}
			memcpy(dcol->fill, jfill->value.str_->str, dcol->fill_len);
			goto next_col;
		not_array_fill:
			/* non-array fill routine */
			dcol->fill = &dcol->__fill;
			if (!jfill) /* fill value is already 0 */
				goto next_col;
			rc = __prim_fill_from_json(dcol->fill, dcol->type, jfill);
			if (rc) {
				DECOMP_ERR(reqc, EINVAL, "strgp '%s': row '%d': col[dst] '%s',"
						"'fill' error: type mismatch\n",
						strgp->obj.name, i, dcol->dst);
				goto err_0;
			}
		next_col:
			j++;
		}
		assert(j == jcols->item_count);

		/* Finalize row schema digest */
		unsigned int len = LDMS_DIGEST_LENGTH;
		EVP_DigestFinal(evp_ctx, drow->schema_digest.digest, &len);

		/* indices */
		jidxs = __jdict_list(jrow, "indices");
		if (!jidxs)
			goto next_row;
		drow->idx_count = jidxs->item_count;
		drow->idxs = calloc(1, drow->idx_count*sizeof(drow->idxs[0]));
		if (!drow->idxs)
			goto err_enomem;
		drow->row_sz += drow->idx_count * sizeof(ldmsd_row_index_t);
		/* prep temporary col-id table */
		col_id_tbl = calloc(1,  sizeof(*col_id_tbl) +
					drow->col_count*sizeof(col_id_tbl->ent[0]));
		if (!col_id_tbl)
			goto err_enomem;
		col_id_tbl->len = drow->col_count;
		for (j = 0; j < drow->col_count; j++) {
			col_id_tbl->ent[j].str = drow->cols[j].dst;
			col_id_tbl->ent[j].i = j;
		}
		qsort(col_id_tbl->ent, col_id_tbl->len, sizeof(col_id_tbl->ent[0]), str_int_cmp);
		/* foreach index */
		j = 0;
		TAILQ_FOREACH(jidx, &jidxs->item_list, item_entry) {
			didx = &drow->idxs[j];
			if (jidx->type != JSON_DICT_VALUE) {
				DECOMP_ERR(reqc, EINVAL, "strgp '%s': row '%d': "
						"an index must be a dictionary.\n",
						strgp->obj.name, i);
				goto err_0;
			}
			jname = __jdict_str(jidx, "name");
			if (!jname) {
				DECOMP_ERR(reqc, EINVAL, "strgp '%s': row '%d': index '%d': "
						"index['name'] is required.\n",
						strgp->obj.name, i, j);
				goto err_0;
			}
			didx->name = strdup(jname->str);
			if (!didx->name)
				goto err_enomem;
			jidx_cols = __jdict_list(jidx, "cols");
			if (!jidx_cols) {
				DECOMP_ERR(reqc, EINVAL, "strgp '%s': row '%d': index '%d':"
						"index['cols'] is required.\n",
						strgp->obj.name, i, j);
				goto err_0;
			}
			didx->col_count = jidx_cols->item_count;
			didx->col_idx = calloc(1, didx->col_count*sizeof(didx->col_idx[0]));
			if (!didx->col_idx)
				goto err_enomem;
			drow->row_sz += sizeof(struct ldmsd_row_index_s) +
					didx->col_count * sizeof(ldmsd_col_t);
			/* resolve col name to col id */
			k = 0;
			TAILQ_FOREACH(jcol, &jidx_cols->item_list, item_entry) {
				if (jcol->type != JSON_STRING_VALUE) {
					DECOMP_ERR(reqc, EINVAL, "strgp '%s': row '%d': index '%d': col '%d': "
							"index['cols'][x] value must be a string.\n",
							strgp->obj.name, i, j, k);
					goto err_0;
				}
				key.str = jcol->value.str_->str;
				tbl_ent = bsearch(&key, col_id_tbl->ent,
						  col_id_tbl->len,
						  sizeof(col_id_tbl->ent[0]),
						  str_int_cmp);
				if (!tbl_ent) {
					DECOMP_ERR(reqc, ENOENT, "strgp '%s': row '%d': index '%d': "
							"column '%s' not found.\n",
							strgp->obj.name, i, j, key.str);
					goto err_0;
				}
				didx->col_idx[k] = tbl_ent->i;
				k++;
			}
			assert(k == jidx_cols->item_count);
			j++;
		}
		assert(j == jidxs->item_count);
		/* clean up temporary col-id table */
		free(col_id_tbl);
		col_id_tbl = NULL;
	next_row:
		dcfg->row_count++;
		i++;
	}
	assert(i == jrows->item_count);
	if (evp_ctx)
		EVP_MD_CTX_destroy(evp_ctx);
	return &dcfg->decomp;

 err_enomem:
	DECOMP_ERR(reqc, errno, "Not enough memory\n");
 err_0:
	__decomp_static_cfg_free(dcfg);
	free(col_id_tbl);
	if (evp_ctx)
		EVP_MD_CTX_destroy(evp_ctx);
	return NULL;
}

static int __decomp_static_resolve_mid(__decomp_static_mid_rbn_t mid_rbn,
				       __decomp_static_row_cfg_t drow,
				       ldms_set_t set)
{
	int i, mid;
	ldms_mval_t lh, le, rec_array, rec;
	size_t mlen;
	const char *src;
	enum ldms_value_type mtype;
	for (i = 0; i < mid_rbn->col_count; i++) {
		mid_rbn->col_mids[i].mid = -1;
		mid_rbn->col_mids[i].rec_mid = -1;

		src = drow->cols[i].src;

		if (0 == strcmp(src, "timestamp")) {
			mid_rbn->col_mids[i].mid = LDMSD_PHONY_METRIC_ID_TIMESTAMP;
			mid_rbn->col_mids[i].rec_mtype = LDMS_V_TIMESTAMP;
			continue;
		}

		if (0 == strcmp(src, "producer")) {
			mid_rbn->col_mids[i].mid = LDMSD_PHONY_METRIC_ID_PRODUCER;
			mid_rbn->col_mids[i].rec_mtype = LDMS_V_CHAR_ARRAY;
			continue;
		}

		if (0 == strcmp(src, "instance")) {
			mid_rbn->col_mids[i].mid = LDMSD_PHONY_METRIC_ID_INSTANCE;
			mid_rbn->col_mids[i].rec_mtype = LDMS_V_CHAR_ARRAY;
			continue;
		}

		mid = ldms_metric_by_name(set, drow->cols[i].src);
		mid_rbn->col_mids[i].mid = mid;
		if (mid < 0) /* OK to not exist */
			continue;
		mtype = ldms_metric_type_get(set, mid);
		mid_rbn->col_mids[i].mtype = mtype;
		if (mtype == LDMS_V_LIST)
			goto list_routine;
		if (mtype == LDMS_V_RECORD_ARRAY)
			goto rec_array_routine;

		/* primitives & array of primitives */
		if (mtype > LDMS_V_D64_ARRAY) {
			mid_rbn->col_mids[i].mid = -EINVAL;
			continue;
		}
		mid_rbn->col_mids[i].rec_mid = -EINVAL;
		mid_rbn->col_mids[i].rec_mtype = LDMS_V_NONE;
		continue;

	list_routine:
		/* handling LIST */
		lh = ldms_metric_get(set, mid);
		le = ldms_list_first(set, lh, &mtype, &mlen);
		if (!le) {
			/* list empty. can't init yet */
			mid_rbn->col_mids[i].rec_mid = -1;
			continue;
		}
		if (mtype == LDMS_V_LIST) {
			/* LIST of LIST is not supported */
			mid_rbn->col_mids[i].rec_mid = -EINVAL;
			mid_rbn->col_mids[i].rec_mtype = LDMS_V_NONE;
			continue;
		}
		if (!drow->cols[i].rec_member) {
			/* expect LIST of non-record elements */
			mid_rbn->col_mids[i].rec_mid = -EINVAL;
			mid_rbn->col_mids[i].rec_mtype = LDMS_V_NONE;
			continue;
		}
		/* handling LIST of records */
		mid = ldms_record_metric_find(le, drow->cols[i].rec_member);
		mid_rbn->col_mids[i].rec_mid = mid;
		if (mid >= 0) {
			mtype = ldms_record_metric_type_get(le, mid, &mlen);
			mid_rbn->col_mids[i].rec_mtype = mtype;
		}
		continue;

	rec_array_routine:
		rec_array = ldms_metric_get(set, mid);
		rec = ldms_record_array_get_inst(rec_array, 0);
		if (!drow->cols[i].rec_member) {
			mid_rbn->col_mids[i].rec_mid = -EINVAL;
			mid_rbn->col_mids[i].rec_mtype = LDMS_V_NONE;
			continue;
		}
		mid = ldms_record_metric_find(rec, drow->cols[i].rec_member);
		if (mid < 0) {
			mid_rbn->col_mids[i].rec_mid = -ENOENT;
			mid_rbn->col_mids[i].rec_mtype = LDMS_V_NONE;
			continue;
		}
		mtype = ldms_record_metric_type_get(rec, mid, &mlen);
		mid_rbn->col_mids[i].rec_mid = mid;
		mid_rbn->col_mids[i].rec_mtype = mtype;
		continue;
	}
	return 0;
}

static int __decomp_static_decompose(ldmsd_strgp_t strgp, ldms_set_t set,
				     ldmsd_row_list_t row_list, int *row_count)
{
	__decomp_static_cfg_t dcfg = (void*)strgp->decomp;
	__decomp_static_row_cfg_t drow;
	__decomp_static_col_cfg_t dcol;
	ldmsd_row_t row;
	ldmsd_col_t col;
	ldmsd_row_index_t idx;
	ldms_mval_t mval, lh, le, rec_array;
	enum ldms_value_type mtype;
	size_t mlen;
	int i, j, k, c, mid, rc, rec_mid;
	struct _col_mval_s {
		ldms_mval_t mval;
		ldms_mval_t rec_array;
		union {
			ldms_mval_t le;
			ldms_mval_t rec;
		};
		enum ldms_value_type mtype;
		size_t array_len;
		int metric_id;
		int rec_metric_id;
		int rec_array_len;
		int rec_array_idx;
	} *col_mvals = NULL, *mcol;
	__decomp_static_mid_rbn_t mid_rbn;
	ldms_digest_t ldms_digest;
	TAILQ_HEAD(, _list_entry) list_cols;
	int row_more_le;
	struct ldms_timestamp ts;
	const char *producer;
	const char *instance;
	int producer_len, instance_len;
	ldms_mval_t phony;

	if (!TAILQ_EMPTY(row_list))
		return EINVAL;

	ts = ldms_transaction_timestamp_get(set);

	producer = ldms_set_producer_name_get(set);
	producer_len = strlen(producer) + 1;
	instance = ldms_set_instance_name_get(set);
	instance_len = strlen(instance) + 1;

	TAILQ_INIT(&list_cols);
	ldms_digest = ldms_set_digest_get(set);

	*row_count = 0;
	for (i = 0; i < dcfg->row_count; i++) {
		drow = &dcfg->rows[i];

		/* mid resolve */
		mid_rbn = (void*)rbt_find(&drow->mid_rbt, ldms_digest);
		if (mid_rbn) /* metric IDs have already been resolved for this LDMS schema */
			goto make_col_mvals;
		/* Resolving `src` -> metric ID */
		mid_rbn = calloc(1, sizeof(*mid_rbn) +
				    drow->col_count * sizeof(mid_rbn->col_mids[0]));
		if (!mid_rbn) {
			rc = ENOMEM;
			goto err_0;
		}
		memcpy(&mid_rbn->ldms_digest, ldms_digest, sizeof(*ldms_digest));
		rbn_init(&mid_rbn->rbn, &mid_rbn->ldms_digest);
		mid_rbn->col_count = drow->col_count;
		rbt_ins(&drow->mid_rbt, &mid_rbn->rbn);
		rc = __decomp_static_resolve_mid(mid_rbn, drow, set);
		if (rc)
			goto err_0;
	make_col_mvals:
		/* col_mvals is a temporary scratch paper to create rows from
		 * a set with records. col_mvals is freed at the end of
		 * `make_row`. */
		col_mvals = calloc(1, drow->col_count * sizeof(*col_mvals));
		if (!col_mvals) {
			rc = ENOMEM;
			goto err_0;
		}
		for (j = 0; j < drow->col_count; j++) {
			mid = mid_rbn->col_mids[j].mid;
			mcol = &col_mvals[j];
			if (mid < 0) /* metric not existed in the set */
				goto col_mvals_fill;
			mcol->metric_id = mid;
			mcol->rec_metric_id = -1;
			mcol->rec_array_idx = -1;
			mcol->rec_array_len = -1;
			switch (mid) {
			case LDMSD_PHONY_METRIC_ID_TIMESTAMP:
				/* mcol->mval will be assigned in `make_row` */
				mcol->mtype = LDMS_V_TIMESTAMP;
				mcol->array_len = 1;
				mcol->le = NULL;
				continue;
			case LDMSD_PHONY_METRIC_ID_PRODUCER:
				mcol->mval = (ldms_mval_t)producer;
				/* mcol->mval->a_char is producer */
				mcol->mtype = LDMS_V_CHAR_ARRAY;
				mcol->array_len = producer_len;
				mcol->le = NULL;
				continue;
			case LDMSD_PHONY_METRIC_ID_INSTANCE:
				mcol->mval = (ldms_mval_t)instance;
				/* mcol->mval->a_char is instance */
				mcol->mtype = LDMS_V_CHAR_ARRAY;
				mcol->array_len = instance_len;
				mcol->le = NULL;
				continue;
			}
			mval = ldms_metric_get(set, mid);
			mtype = ldms_metric_type_get(set, mid);
			if (mtype != mid_rbn->col_mids[j].mtype) {
				ldmsd_lerror("strgp '%s': the metric type (%s) of "
					     "row %d:col %d is different from the type (%s) of "
					     "LDMS metric '%s'.\n", strgp->obj.name,
					     ldms_metric_type_to_str(mid_rbn->col_mids[j].mtype),
					     i, j, ldms_metric_type_to_str(mtype),
					     ldms_metric_name_get(set, mid));
				rc = EINVAL;
				goto err_0;
			}

			if (mtype == LDMS_V_LIST)
				goto col_mvals_list;
			if (mtype == LDMS_V_RECORD_ARRAY)
				goto col_mvals_rec_array;
			if (mtype > LDMS_V_D64_ARRAY)
				goto col_mvals_fill;
			/* primitives */
			if (ldms_type_is_array(mtype)) {
				mlen = ldms_metric_array_get_len(set, mid);
			} else {
				mlen = 1;
			}
			mcol->mval = mval;
			mcol->mtype = mtype;
			mcol->array_len = mlen;
			mcol->le = NULL;
			continue;

		col_mvals_list:
			/* list */
			lh = mval;
			le = mcol->le = ldms_list_first(set, lh, &mtype, &mlen);
			if (!le) /* list end .. use 'fill' */
				goto col_mvals_fill;
			if (mtype == LDMS_V_RECORD_INST)
				goto col_mvals_rec_mid;
			if (mtype > LDMS_V_D64_ARRAY) /* bad type */
				goto col_mvals_fill;
			/* list of primitives */
			mcol->mval = le;
			mcol->mtype = mtype;
			mcol->array_len = mlen;
			continue;
		col_mvals_rec_mid:
			/* handling record */
			rec_mid = mid_rbn->col_mids[j].rec_mid;
			if (rec_mid >= 0) {
				mval = ldms_record_metric_get(le, rec_mid);
				mcol->mval = mval;
				mcol->mtype = ldms_record_metric_type_get(mcol->le, rec_mid, &mcol->array_len);
				mcol->rec_metric_id = rec_mid;
				continue;
			}
			if (rec_mid == -1) {
				/* has not been resolved yet ..
				 * try resolving it here */
				rec_mid = ldms_record_metric_find(le, drow->cols[j].rec_member);
				mid_rbn->col_mids[j].rec_mid = rec_mid;
				goto col_mvals_rec_mid;
			}
			/* member not exist, use fill */
			mcol->rec_metric_id = -ENOENT;
			goto col_mvals_fill;

		col_mvals_rec_array:
			rec_mid = mid_rbn->col_mids[j].rec_mid;
			if (rec_mid < 0) {
				mcol->rec_metric_id = rec_mid;
				goto col_mvals_fill;
			}
			rec_array = mval;
			mcol->rec_array = rec_array;
			mcol->rec_array_len = ldms_record_array_len(rec_array);
			mcol->rec_array_idx = 0;
			mcol->rec = ldms_record_array_get_inst(rec_array, 0);
			mcol->mval = ldms_record_metric_get(mcol->rec, rec_mid);
			mcol->mtype = ldms_record_metric_type_get(mcol->rec, rec_mid, &mcol->array_len);
			mcol->rec_metric_id = rec_mid;
			continue;

		col_mvals_fill:
			mcol->le = NULL;
			mcol->mval = drow->cols[j].fill;
			mcol->mtype = drow->cols[j].type;
			mcol->array_len = drow->cols[j].fill_len;
		}

	make_row: /* make/expand rows according to col_mvals */
		row = calloc(1, drow->row_sz);
		if (!row) {
			rc = errno;
			goto err_0;
		}
		row->schema_name = drow->schema_name;
		row->schema_digest = &drow->schema_digest;
		row->idx_count = drow->idx_count;
		row->col_count = drow->col_count;

		/* indices */
		row->indices = (void*)&row->cols[row->col_count];
		idx = (void*)&row->indices[row->idx_count];
		for (j = 0; j < row->idx_count; j++) {
			row->indices[j] = idx;
			idx->col_count = drow->idxs[j].col_count;
			idx->name = drow->idxs[j].name;
			for (k = 0; k < idx->col_count; k++) {
				c = drow->idxs[j].col_idx[k];
				idx->cols[k] = &row->cols[c];
			}
			idx = (void*)&idx->cols[idx->col_count];
		}

		/* phony mvals are next to the idx data */
		phony = (void*)idx;

		row_more_le = 0;
		/* cols */
		for (j = 0; j < row->col_count; j++) {
			col = &row->cols[j];
			dcol = &drow->cols[j];
			mcol = &col_mvals[j];

			if (dcol->type != mcol->mtype) {
				ldmsd_lerror("strgp '%s': row '%d' col[dst] '%s': "
					     "the value type (%s) is not "
					     "compatible with the source metric type (%s). "
					     "Please check the decomposition configuration.\n",
					     strgp->obj.name, i, dcol->dst,
					     ldms_metric_type_to_str(dcol->type),
					     ldms_metric_type_to_str(mcol->mtype));
				rc = EINVAL;
				goto err_0;
			}

			col->metric_id = mcol->metric_id;
			col->rec_metric_id = mcol->rec_metric_id;

			col->name = dcol->dst;
			col->type = mcol->mtype;
			col->array_len = mcol->array_len;
			if (mid_rbn->col_mids[j].mid == LDMSD_PHONY_METRIC_ID_TIMESTAMP) {
				phony->v_ts = ts;
				col->mval = phony;
				phony++;
			} else {
				/* The other phony types are fine */
				col->mval = mcol->mval;
			}

			if (!mcol->le) /* no more elements */
				continue;

			if (mcol->rec_array_idx >= 0) /* array of records */
				goto col_rec_array;

			/* list */
			/* step to next element in the list */
			mcol->le = ldms_list_next(set, mcol->le, &mcol->mtype, &mcol->array_len);
			if (!mcol->le)
				goto col_fill;
			row_more_le = 1;
			if (drow->cols[j].rec_member) {
				/* expect record */
				rec_mid = mid_rbn->col_mids[j].rec_mid;
				if (rec_mid < 0) {
					goto col_fill;
				}
				/* extract the record metric */
				mcol->mval = ldms_record_metric_get(mcol->le, rec_mid);
				mcol->mtype = ldms_record_metric_type_get(mcol->le, rec_mid, &mcol->array_len);
			} else {
				/* expect list of primitives */
				if (mcol->mtype > LDMS_V_D64_ARRAY)
					goto col_fill;
				mcol->mval = mcol->le;
			}
			continue;

		col_rec_array:
			rec_mid = mid_rbn->col_mids[j].rec_mid;
			if (rec_mid < 0 || mcol->rec_array_idx < 0)
				goto col_fill;
			/* step */
			mcol->rec_array_idx++;
			mcol->rec = ldms_record_array_get_inst(mcol->rec_array, mcol->rec_array_idx);
			if (!mcol->rec)
				goto col_fill;
			/* extract the record metric */
			mcol->mval = ldms_record_metric_get(mcol->rec, rec_mid);
			mcol->mtype = ldms_record_metric_type_get(mcol->le, rec_mid, &mcol->array_len);
			continue;

		col_fill:
			mcol->mval = drow->cols[j].fill;
			mcol->array_len = drow->cols[j].array_len;
			mcol->mtype = drow->cols[j].type;
		}
		TAILQ_INSERT_TAIL(row_list, row, entry);
		(*row_count)++;
		row = NULL;
		if (row_more_le)
			goto make_row;
		free(col_mvals);
		col_mvals = NULL;
	}
	return 0;
 err_0:
	/* clean up stuff here */
	if (col_mvals)
		free(col_mvals);
	__decomp_static_release_rows(strgp, row_list);
	return rc;
}

static void __decomp_static_release_rows(ldmsd_strgp_t strgp,
					 ldmsd_row_list_t row_list)
{
	ldmsd_row_t row;
	while ((row = TAILQ_FIRST(row_list))) {
		TAILQ_REMOVE(row_list, row, entry);
		free(row);
	}
}
