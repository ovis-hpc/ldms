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

#include <openssl/sha.h>

#include "ovis_json/ovis_json.h"
#include "coll/rbt.h"

#include "ldmsd.h"
#include "ldmsd_request.h"

static ldmsd_decomp_t __decomp_as_is_config(ldmsd_strgp_t strgp,
			json_entity_t cfg, ldmsd_req_ctxt_t reqc);
static int __decomp_as_is_decompose(ldmsd_strgp_t strgp, ldms_set_t set,
				     ldmsd_row_list_t row_list, int *row_count);
static void __decomp_as_is_release_rows(ldmsd_strgp_t strgp,
					 ldmsd_row_list_t row_list);
static void __decomp_as_is_release_decomp(ldmsd_strgp_t strgp);

struct ldmsd_decomp_s __decomp_as_is = {
	.config = __decomp_as_is_config,
	.decompose = __decomp_as_is_decompose,
	.release_rows = __decomp_as_is_release_rows,
	.release_decomp = __decomp_as_is_release_decomp,
};

ldmsd_decomp_t get()
{
	return &__decomp_as_is;
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
	char **col_names; /* names of cols for the index */
	int *col_idx; /* dst columns composing the index */
} *__decomp_index_t;

/* ==== as-is decomposition === */

/* describing a column */
typedef struct __decomp_as_is_col_cfg_s {
	enum ldms_value_type col_type; /* value type of the column */
	char *name; /* column name */
	int set_mid; /* metric ID */
	enum ldms_value_type set_mtype; /* type of set[mid], not to confuse with col_type */
	int set_mlen; /* array length of set[mid] */
	int rec_mid; /* record member ID (if metric is a record) */
	int array_len; /* length of the array, if col_type is array */
} *__decomp_as_is_col_cfg_t;

typedef struct __decomp_as_is_row_cfg_s {
	struct rbn rbn; /* rbn (inserted to cfg->row_cfg_rbt) */
	char *schema_name; /* row schema name */
	int col_count;
	int idx_count;
	__decomp_as_is_col_cfg_t cols; /* array of struct */
	__decomp_index_t idxs; /* array of struct */
	size_t row_sz;
} *__decomp_as_is_row_cfg_t;

int __row_cfg_cmp(void *tree_key, const void *key)
{
	return strcmp(tree_key, key);
}

typedef struct __decomp_as_is_cfg_s {
	struct ldmsd_decomp_s decomp;
	int idx_count;
	struct rbt row_cfg_rbt;
	struct __decomp_index_s idxs[OVIS_FLEX];
} *__decomp_as_is_cfg_t;

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

static void __decomp_as_is_cfg_free(__decomp_as_is_cfg_t dcfg)
{
	int i, j;
	__decomp_index_t didx;
	for (i = 0; i < dcfg->idx_count; i++) {
		didx = &dcfg->idxs[i];
		/* names in idx */
		for (j = 0; j < didx->col_count; j++) {
			free(didx->col_names[j]);
		}
		free(didx->col_names);
		free(didx->col_idx);
		free(didx->name);
	}
	free(dcfg);
}

static void
__decomp_as_is_release_decomp(ldmsd_strgp_t strgp)
{
	if (strgp->decomp) {
		__decomp_as_is_cfg_free((void*)strgp->decomp);
		strgp->decomp = NULL;
	}
}

static ldmsd_decomp_t
__decomp_as_is_config(ldmsd_strgp_t strgp, json_entity_t jcfg,
		      ldmsd_req_ctxt_t reqc)
{
	json_str_t jname;
	json_list_t jidxs, jidx_cols;
	json_entity_t jidx, jidx_col;
	__decomp_as_is_cfg_t dcfg = NULL;
	__decomp_index_t didx;
	int j, k, n_idx;

	/* indices */
	jidxs = __jdict_list(jcfg, "indices");
	if (!jidxs) {
		n_idx = 0;
	} else {
		n_idx = jidxs->item_count;
	}
	dcfg = calloc(1, sizeof(*dcfg) + n_idx * sizeof(dcfg->idxs[0]));
	if (!dcfg)
		goto err_enomem;
	rbt_init(&dcfg->row_cfg_rbt, __row_cfg_cmp);
	dcfg->decomp = __decomp_as_is;
	dcfg->idx_count = n_idx;

	/* foreach index */
	j = 0;
	TAILQ_FOREACH(jidx, &jidxs->item_list, item_entry) {
		didx = &dcfg->idxs[j];
		if (jidx->type != JSON_DICT_VALUE) {
			DECOMP_ERR(reqc, EINVAL, "strgp '%s': index '%d': "
					"an index entry must be a dictionary.\n",
					strgp->obj.name, j);
			goto err_0;
		}
		jname = __jdict_str(jidx, "name");
		if (!jname) {
			DECOMP_ERR(reqc, EINVAL, "strgp '%s': index '%d': "
					"index['name'] is required.\n",
					strgp->obj.name, j);
			goto err_0;
		}
		didx->name = strdup(jname->str);
		if (!didx->name)
			goto err_enomem;
		jidx_cols = __jdict_list(jidx, "cols");
		if (!jidx_cols) {
			DECOMP_ERR(reqc, EINVAL, "strgp '%s': index '%d': "
					"index['cols'] is required.\n",
					strgp->obj.name, j);
			goto err_0;
		}
		didx->col_count = jidx_cols->item_count;
		didx->col_names = calloc(1, didx->col_count*sizeof(didx->col_names[0]));
		if (!didx->col_names)
			goto err_enomem;
		k = 0;
		TAILQ_FOREACH(jidx_col, &jidx_cols->item_list, item_entry) {
			if (jidx_col->type != JSON_STRING_VALUE) {
				DECOMP_ERR(reqc, EINVAL, "strgp '%s': index '%d':"
						"index['cols'][%d] must be a string.\n",
						strgp->obj.name, j, k);
				goto err_0;
			}
			didx->col_names[k] = strdup(jidx_col->value.str_->str);
			if (!didx->col_names[k])
				goto err_enomem;
			k++;
		}
		assert(k == didx->col_count);
		j++;
	}
	assert(j == jidxs->item_count);

	return &dcfg->decomp;

 err_enomem:
	DECOMP_ERR(reqc, errno, "Not enough memory\n");
 err_0:
	if (dcfg)
		__decomp_as_is_cfg_free(dcfg);
	return NULL;
}

/* protected by strgp->lock */
static __decomp_as_is_row_cfg_t
__get_row_cfg(__decomp_as_is_cfg_t dcfg, ldms_set_t set)
{
	char *name = NULL;
	int i, j, k, rec_card, set_card, name_len, col_count;
	__decomp_as_is_row_cfg_t drow = NULL;
	__decomp_as_is_col_cfg_t dcol = NULL;
	enum ldms_value_type mtype;
	ldms_mval_t lh, le, rec_array, rec;
	size_t marray_len, col_array_len;
	const char *mname;
	const char *ss_name = ldms_set_schema_name_get(set);
	ldms_digest_t ldms_digest = ldms_set_digest_get(set);

	assert(ss_name != NULL);
	assert(ldms_digest != NULL);
	name_len = asprintf(&name, "%s_%02hhx%02hhx%02hhx%hhx",
			ldms_set_schema_name_get(set),
			ldms_digest->digest[0],
			ldms_digest->digest[1],
			ldms_digest->digest[2],
			(unsigned char)(ldms_digest->digest[3] >> 4));
	if (name_len < 0)
		return NULL;
	drow = (void*)rbt_find(&dcfg->row_cfg_rbt, name);
	if (drow)
		goto out;

	/* first time seeing this set schema; create decomp row config */

	/* determine number of columns from set schema */
	set_card = ldms_set_card_get(set);
	col_count = 1; /* the first col is `timestamp` (phony metric) */
	for (i = 0; i < set_card; i++) {
		mtype = ldms_metric_type_get(set, i);
		switch (mtype) {
		case LDMS_V_CHAR:
		case LDMS_V_S8:
		case LDMS_V_U8:
		case LDMS_V_S16:
		case LDMS_V_U16:
		case LDMS_V_S32:
		case LDMS_V_U32:
		case LDMS_V_S64:
		case LDMS_V_U64:
		case LDMS_V_F32:
		case LDMS_V_D64:
		case LDMS_V_CHAR_ARRAY:
		case LDMS_V_S8_ARRAY:
		case LDMS_V_U8_ARRAY:
		case LDMS_V_S16_ARRAY:
		case LDMS_V_U16_ARRAY:
		case LDMS_V_S32_ARRAY:
		case LDMS_V_U32_ARRAY:
		case LDMS_V_S64_ARRAY:
		case LDMS_V_U64_ARRAY:
		case LDMS_V_F32_ARRAY:
		case LDMS_V_D64_ARRAY:
			/* primitives and arrays are counted as 1 column */
			col_count += 1;
			break;
		case LDMS_V_RECORD_TYPE:
			/* skip */
			break;
		case LDMS_V_LIST:
			lh = ldms_metric_get(set, i);
			le = ldms_list_first(set, lh, &mtype, &marray_len);
			if (!le) {
				/* List empty .. set is not ready */
				errno = EINVAL;
				goto out;
			}
			if (mtype == LDMS_V_RECORD_INST) {
				rec_card = ldms_record_card(le);
				col_count += rec_card;
			} else {
				col_count += 1;
			}
			break;
		case LDMS_V_RECORD_ARRAY:
			/* treat record_array like list head */
			rec_array = ldms_metric_get(set, i);
			rec = ldms_record_array_get_inst(rec_array, 0);
			rec_card = ldms_record_card(rec);
			col_count += rec_card;
			break;
		default:
			/* unsupported type */
			errno = ENOTSUP;
			goto out;
		}
	}

	drow = calloc(1, sizeof(*drow));
	if (!drow)
		goto out;
	drow->schema_name = name; /* give `name` to decomp row config */
	drow->idx_count = dcfg->idx_count;
	drow->col_count = col_count;
	rbn_init(&drow->rbn, drow->schema_name);
	name = NULL;
	drow->cols = calloc(col_count, sizeof(drow->cols[0]));
	if (!drow->cols)
		goto err;
	drow->idxs = calloc(dcfg->idx_count, sizeof(drow->idxs[0]));
	if (!drow->idxs)
		goto err;

	ldmsd_row_t row = NULL;

	drow->row_sz =  sizeof(*row) +
			sizeof(row->cols[0])*drow->col_count +
			sizeof(row->indices[0])*drow->idx_count +
			sizeof(union ldms_value);

	/* create timestamp column */
	dcol = &drow->cols[0];
	dcol->set_mid = LDMSD_PHONY_METRIC_ID_TIMESTAMP;
	dcol->array_len = 1;
	dcol->rec_mid = -1;
	dcol->col_type = LDMS_V_TIMESTAMP;
	name_len = asprintf(&dcol->name, "timestamp");
	if (!name_len)
		goto err;

	col_count = 1;

	/* create other columns */
	for (i = 0; i < set_card; i++) {
		mtype = ldms_metric_type_get(set, i);
		switch (mtype) {
		case LDMS_V_CHAR:
		case LDMS_V_S8:
		case LDMS_V_U8:
		case LDMS_V_S16:
		case LDMS_V_U16:
		case LDMS_V_S32:
		case LDMS_V_U32:
		case LDMS_V_S64:
		case LDMS_V_U64:
		case LDMS_V_F32:
		case LDMS_V_D64:
		case LDMS_V_CHAR_ARRAY:
		case LDMS_V_S8_ARRAY:
		case LDMS_V_U8_ARRAY:
		case LDMS_V_S16_ARRAY:
		case LDMS_V_U16_ARRAY:
		case LDMS_V_S32_ARRAY:
		case LDMS_V_U32_ARRAY:
		case LDMS_V_S64_ARRAY:
		case LDMS_V_U64_ARRAY:
		case LDMS_V_F32_ARRAY:
		case LDMS_V_D64_ARRAY:
			dcol = &drow->cols[col_count++];
			dcol->set_mid = i;
			dcol->array_len = ldms_metric_array_get_len(set, i);
			dcol->col_type = mtype;
			dcol->set_mtype = mtype;
			dcol->set_mlen = 1;
			name_len = asprintf(&dcol->name, "%s", ldms_metric_name_get(set, i));
			if (name_len < 0)
				goto err;
			dcol->rec_mid = -1; /* not a record */
			break;
		case LDMS_V_RECORD_TYPE:
			/* skip */
			break;
		case LDMS_V_LIST:
			lh = ldms_metric_get(set, i);
			le = ldms_list_first(set, lh, &mtype, &marray_len);
			assert(le);
			if (mtype == LDMS_V_RECORD_INST) {
				mname = ldms_metric_name_get(set, i);
				rec_card = ldms_record_card(le);
				for (j = 0; j < rec_card; j++) {
					dcol = &drow->cols[col_count++];
					dcol->col_type = ldms_record_metric_type_get(le, j, &marray_len);
					dcol->set_mid = i;
					dcol->set_mtype = LDMS_V_LIST;
					dcol->set_mlen = 1;
					dcol->rec_mid = j;
					dcol->array_len = marray_len;
					name_len = asprintf(&dcol->name,
							"%s.%s", mname,
							ldms_record_metric_name_get(le, j));
					if (name_len < 0)
						goto err;
				}
			} else {
				dcol = &drow->cols[col_count++];
				dcol->col_type = mtype;
				dcol->set_mid = i;
				dcol->set_mtype = LDMS_V_LIST;
				dcol->set_mlen = 1;
				dcol->rec_mid = -1;
				dcol->array_len = marray_len;
				name_len = asprintf(&dcol->name, "%s", ldms_metric_name_get(set, i));
				if (name_len < 0)
					goto err;
			}
			break;
		case LDMS_V_RECORD_ARRAY:
			rec_array = ldms_metric_get(set, i);
			marray_len = ldms_record_array_len(rec_array);
			rec = ldms_record_array_get_inst(rec_array, 0);
			mname = ldms_metric_name_get(set, i);
			rec_card = ldms_record_card(rec);
			for (j = 0; j < rec_card; j++) {
				dcol = &drow->cols[col_count++];
				dcol->col_type = ldms_record_metric_type_get(rec, j, &col_array_len);
				dcol->set_mid = i;
				dcol->set_mtype = LDMS_V_RECORD_ARRAY;
				dcol->set_mlen = marray_len;
				dcol->rec_mid = j;
				dcol->array_len = col_array_len;
				name_len = asprintf(&dcol->name,
						"%s.%s", mname,
						ldms_record_metric_name_get(rec, j));
				if (name_len < 0)
					goto err;
			}
			break;
		default:
			/* unsupported type */
			errno = ENOTSUP;
			goto out;
		}
	}

	/* index */
	for (i = 0; i < dcfg->idx_count; i++) {
		__decomp_index_t didx = &dcfg->idxs[i];
		__decomp_index_t ridx = &drow->idxs[i];
		*ridx = *didx;
		ridx->col_idx = calloc(ridx->col_count, sizeof(ridx->col_idx[0]));
		if (!ridx->col_idx)
			goto err;
		drow->row_sz += sizeof(*row->indices[0]) +
				ridx->col_count * sizeof(row->indices[0]->cols[0]);
		/* resolve col id */
		for (j = 0; j < ridx->col_count; j++) {
			for (k = 0; k < drow->col_count; k++) {
				dcol = &drow->cols[k];
				if (0 == strcmp(dcol->name, ridx->col_names[j]))
					break;
			}
			if (k == drow->col_count) {
				/* column not found */
				errno = ENOENT;
				goto err;
			}
			ridx->col_idx[j] = k;
		}
	}

	/* insert into the tree */
	rbt_ins(&dcfg->row_cfg_rbt, &drow->rbn);

 out:
	if (name)
		free(name);
	return drow;

 err:
	if (drow->cols) {
		for (i = 0; i < drow->col_count; i++) {
			free(drow->cols[i].name);
		}
		free(drow->cols);
	}
	if (drow->idxs) {
		for (i = 0; i < drow->idx_count; i++) {
			if (!drow->idxs[i].col_idx)
				continue;
			free(drow->idxs[i].col_idx);
		}
		free(drow->idxs);
	}
	free(drow->schema_name);
	free(drow);
	return NULL;
}

static union ldms_value fill = {0};

static int __decomp_as_is_decompose(ldmsd_strgp_t strgp, ldms_set_t set,
				    ldmsd_row_list_t row_list, int *row_count)
{
	__decomp_as_is_cfg_t dcfg = (void*)strgp->decomp;
	__decomp_as_is_row_cfg_t drow;
	__decomp_as_is_col_cfg_t dcol;
	ldmsd_row_t row;
	ldmsd_col_t col;
	ldmsd_row_index_t idx;
	ldms_mval_t mval, lh, le;
	enum ldms_value_type mtype;
	size_t mlen;
	int j, k, c, mid, rc, rec_mid;
	int col_count;
	struct _col_mval_s {
		__decomp_as_is_col_cfg_t dcol;
		union {
			ldms_mval_t set_mval;
			ldms_mval_t lh;
			ldms_mval_t rec_array;
		};
		union {
			ldms_mval_t le;
			ldms_mval_t rec;
		};
		ldms_mval_t col_mval;
		size_t col_mlen;
		int use_fill;
		int rec_array_idx;
	} *col_mvals = NULL, *mcol;
	ldms_digest_t ldms_digest;
	TAILQ_HEAD(, _list_entry) list_cols;
	int row_more_le;
	struct ldms_timestamp ts;
	const char *set_schema;
	int row_schema_name_len;
	ldms_mval_t phony;
	char *row_schema_name = NULL;

	if (!TAILQ_EMPTY(row_list))
		return EINVAL;

	ts = ldms_transaction_timestamp_get(set);

	set_schema = ldms_set_schema_name_get(set);

	TAILQ_INIT(&list_cols);
	ldms_digest = ldms_set_digest_get(set);

	/*
	 * NOTE Create rows from the set as-is, with list entry expansion.
	 *
	 * The schema format is "<schema_name>_<short_sha>", where the
	 * "<short_sha>" is the first 7 characters of the hex string
	 * representation of the SHA (similar to git short commit ID).
	 *
	 */
	row_schema_name_len = asprintf(&row_schema_name, "%s_%02hhx%02hhx%02hhx%hhx", set_schema,
			ldms_digest->digest[0],
			ldms_digest->digest[1],
			ldms_digest->digest[2],
			(unsigned char)(ldms_digest->digest[3] >> 4));
	if (row_schema_name_len < 0)
		return errno;

	drow = __get_row_cfg(dcfg, set);
	if (!drow)
		return errno;

	col_count = drow->col_count;

	*row_count = 0;

	/* col_mvals is a temporary scratch paper to create rows from
	 * a set with records. col_mvals is freed at the end of
	 * `make_row`. */
	col_mvals = calloc(col_count, sizeof(col_mvals[0]));
	if (!col_mvals) {
		rc = ENOMEM;
		goto err_0;
	}
	for (j = 0; j < col_count; j++) {
		dcol = &drow->cols[j];
		mcol = &col_mvals[j];
		mcol->dcol = dcol;
		mid = dcol->set_mid;
		assert(mid >= 0);
		mcol->le = NULL;

		if (mid == LDMSD_PHONY_METRIC_ID_TIMESTAMP) {
			/* mcol->mval will be assigned in `make_row` */
			continue;
		}

		assert(mid < LDMSD_PHONY_METRIC_ID_FIRST);

		mcol->set_mval = ldms_metric_get(set, mid);
		mtype = ldms_metric_type_get(set, mid);

		assert( mcol->dcol->set_mtype == mtype );

		if (mtype == LDMS_V_LIST)
			goto col_mvals_list;
		if (mtype == LDMS_V_RECORD_ARRAY)
			goto col_mvals_rec_array;
		assert(mtype <= LDMS_V_D64_ARRAY);
		/* primitives & array of primitives */
		mcol->col_mval = mcol->set_mval;
		mcol->col_mlen = ldms_metric_array_get_len(set, dcol->set_mid);
		continue;

	col_mvals_list:
		/* list */
		lh = mcol->set_mval;
		le = mcol->le = ldms_list_first(set, lh, &mtype, &mlen);
		if (!le) /* list end .. use 'fill' */
			goto col_mvals_fill;
		if (mtype == LDMS_V_RECORD_INST)
			goto col_mvals_rec;
		if (mtype != dcol->col_type) /* type mismatched */
			goto col_mvals_fill;
		/* list of primitives */
		mcol->col_mval = le;
		mcol->col_mlen = mlen;
		continue;

	col_mvals_rec:
		/* handling record in list */
		rec_mid = dcol->rec_mid;
		assert(rec_mid >= 0);
		mval = ldms_record_metric_get(le, rec_mid);
		if (!mval) {
			/* no record member */
			goto col_mvals_fill;
		}
		mcol->col_mval = mval;
		mtype = ldms_record_metric_type_get(le, rec_mid, &mcol->col_mlen);
		if (mtype != dcol->col_type) /* type mismatched */
			goto col_mvals_fill;
		continue;

	col_mvals_rec_array:
		/* handling record in array */
		rec_mid = dcol->rec_mid;
		assert(rec_mid >= 0);
		mcol->rec_array_idx = 0;
		mcol->rec = ldms_record_array_get_inst(mcol->rec_array, 0);
		mval = ldms_record_metric_get(mcol->rec, rec_mid);
		if (!mval) {
			/* no record member */
			goto col_mvals_fill;
		}
		mcol->col_mval = mval;
		mtype = ldms_record_metric_type_get(mcol->rec, rec_mid, &mcol->col_mlen);
		if (mtype != dcol->col_type) /* type mismatched */
			goto col_mvals_fill;
		continue;

	col_mvals_fill:
		mcol->use_fill = 1;
		mcol->le = NULL;
		mcol->col_mval = &fill; /* this is { 0 } */
		mcol->col_mlen = 1;
	}

 make_row: /* make/expand rows according to col_mvals */
	row = calloc( 1, drow->row_sz );
	if (!row) {
		rc = errno;
		goto err_0;
	}
	row->schema_name = drow->schema_name;
	row->schema_digest = ldms_digest;
	row->idx_count = drow->idx_count;
	row->col_count = drow->col_count;

	/*
	 * row memory format:
	 * - ldmsd_row_s
	 * - cols[] (array of col structure)
	 * - idx[idx_count] (array of pointers)
	 * - idx[0] structure
	 * - idx[1] structure
	 * - ...
	 * - idx[idx_count-1] structure
	 * - phony[0] (for phony metrics such as timestamp)
	 */

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

		col->metric_id = dcol->set_mid;
		col->rec_metric_id = dcol->rec_mid;

		col->name = dcol->name;
		col->type = dcol->col_type;
		col->array_len = mcol->col_mlen;
		if (dcol->set_mid == LDMSD_PHONY_METRIC_ID_TIMESTAMP) {
			phony->v_ts = ts;
			col->mval = phony;
		} else {
			col->mval = mcol->col_mval;
		}

		if (!mcol->le) /* no more elements */
			continue;

		if (mcol->dcol->set_mtype == LDMS_V_RECORD_ARRAY) {
			/* next record in the record_array */
			mcol->rec_array_idx++;
			mcol->rec = ldms_record_array_get_inst(mcol->rec_array, mcol->rec_array_idx);
			if (!mcol->rec) {
				/* no more entries */
				goto col_fill;
			}
			mcol->col_mval = ldms_record_metric_get(mcol->rec, mcol->dcol->rec_mid);
			mtype = ldms_record_metric_type_get(mcol->le, mcol->dcol->rec_mid, &mcol->col_mlen);
			continue;
		}

		/* step to next element in the list */
		mcol->le = ldms_list_next(set, mcol->le, &mtype, &mcol->col_mlen);
		if (!mcol->le)
			goto col_fill;
		row_more_le = 1;
		if (mcol->dcol->rec_mid >= 0) {
			/* expect record */
			rec_mid = mcol->dcol->rec_mid;
			/* extract the record metric */
			mcol->col_mval = ldms_record_metric_get(mcol->le, rec_mid);
			mtype = ldms_record_metric_type_get(mcol->le, rec_mid, &mcol->col_mlen);
		} else {
			/* expect list of primitives */
			if (dcol->col_type > LDMS_V_D64_ARRAY)
				goto col_fill;
			mcol->col_mval = mcol->le;
		}
		continue;
	col_fill:
		mcol->le = NULL;
		mcol->use_fill = 1;
		mcol->col_mval = &fill;
		mcol->col_mlen = 1;
	}
	TAILQ_INSERT_TAIL(row_list, row, entry);
	(*row_count)++;
	row = NULL;
	if (row_more_le)
		goto make_row;
	free(col_mvals);
	col_mvals = NULL;
	return 0;
 err_0:
	/* clean up stuff here */
	if (col_mvals)
		free(col_mvals);
	__decomp_as_is_release_rows(strgp, row_list);
	return rc;
}

static void __decomp_as_is_release_rows(ldmsd_strgp_t strgp,
					 ldmsd_row_list_t row_list)
{
	ldmsd_row_t row;
	while ((row = TAILQ_FIRST(row_list))) {
		TAILQ_REMOVE(row_list, row, entry);
		free(row);
	}
}
