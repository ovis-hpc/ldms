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

#include <jansson.h>
#include "coll/rbt.h"

#include "ldmsd.h"
#include "ldmsd_request.h"

/* convenient macro to put error message in both ldmsd log and `reqc` */
#define THISLOG(reqc, rc, fmt, ...) do { \
	ldmsd_log(LDMSD_LERROR, fmt, ##__VA_ARGS__); \
	if (reqc) { \
		(reqc)->errcode = rc; \
		Snprintf(&(reqc)->line_buf, &(reqc)->line_len, fmt, ##__VA_ARGS__); \
	} \
} while (0)

static ldmsd_decomp_t decomp_static_config(ldmsd_strgp_t strgp,
			json_t *cfg, ldmsd_req_ctxt_t reqc);
static int decomp_static_decompose(ldmsd_strgp_t strgp, ldms_set_t set,
				     ldmsd_row_list_t row_list, int *row_count);
static void decomp_static_release_rows(ldmsd_strgp_t strgp,
					 ldmsd_row_list_t row_list);
static void decomp_static_release_decomp(ldmsd_strgp_t strgp);

static struct ldmsd_decomp_s decomp_static = {
	.config = decomp_static_config,
	.decompose = decomp_static_decompose,
	.release_rows = decomp_static_release_rows,
	.release_decomp = decomp_static_release_decomp,
};

ldmsd_decomp_t get()
{
	return &decomp_static;
}

static int
mval_from_json(ldms_mval_t *v, enum ldms_value_type *mtype, int *mlen, json_t *jent)
{
	json_t *item;
	json_type type;
	ldms_mval_t mval = NULL;
	int i;

	switch (json_typeof(jent)) {
	case JSON_REAL:
		*mtype = LDMS_V_D64;
		mval = calloc(1, sizeof(mval->v_d));
		mval->v_d = json_real_value(jent);
		break;
	case JSON_STRING:
		*mtype = LDMS_V_CHAR_ARRAY;
		*mlen = json_string_length(jent) + 1;
		mval = malloc(*mlen);
		if (!mval)
			goto err;
		memcpy(mval->a_char, json_string_value(jent), *mlen);
		break;
	case JSON_INTEGER:
		mval = calloc(1, sizeof(mval->v_s64));
		mval->v_s64 = json_integer_value(jent);
		break;
	case JSON_ARRAY:
		*mlen = json_array_size(jent);
		item = json_array_get(jent, 0);
		if (!item)
			goto err;
		type = json_typeof(item);
		switch (type) {
		case JSON_INTEGER:
			*mtype = LDMS_V_S64_ARRAY;
			mval = calloc(*mlen, sizeof(mval->v_s64));
			if (!mval)
				goto err;
			break;
		case JSON_REAL:
			*mtype = LDMS_V_D64_ARRAY;
			mval = calloc(*mlen, sizeof(mval->v_d));
			if (!mval)
				goto err;
			break;
		case JSON_OBJECT:
		case JSON_ARRAY:
		case JSON_STRING:
		case JSON_NULL:
		case JSON_TRUE:
		case JSON_FALSE:
		default:
			goto err;
		}
		json_array_foreach(jent, i, item) {
			if (type == JSON_INTEGER)
				ldms_mval_array_set_s64(mval, i, json_integer_value(item));
			else
				ldms_mval_array_set_double(mval, i, json_real_value(item));
		}
	default:
		goto err;
	}
	*v = mval;
	return 0;
err:
	return EINVAL;
}

/* common index config descriptor */
typedef struct decomp_index_s {
	char *name;
	int col_count;
	int *col_idx; /* dst columns composing the index */
} *decomp_index_t;

/* describing a src-dst column pair */
typedef struct decomp_static_col_cfg_s {
	enum ldms_value_type type; /* value type */
	int array_len; /* length of the array, if type is array */
	char *src; /* name of the metric */
	char *rec_member; /* name of the record metric (if applicable) */
	char *dst; /* destination (storage side) column name */
	ldms_mval_t fill; /* fill value */
	int fill_len; /* if fill is an array */
	union ldms_value __fill; /* storing a non-array primitive fill value */
	enum ldmsd_decomp_op op;
} *decomp_static_col_cfg_t;

typedef struct decomp_static_row_cfg_s {
	char *schema_name; /* row schema name */
	int col_count;
	int idx_count;
	int row_limit;		/* Limit of rows to cache per group */
	decomp_static_col_cfg_t cols; /* array of struct */
	decomp_index_t idxs; /* array of struct */
	struct ldms_digest_s schema_digest;
	size_t row_sz;	    /* The memory size of the row in bytes */
	size_t mval_size;   /* Size of the memory in mvals */
	struct rbt mid_rbt; /* collection of metric ID mappings */
	int op_present;	    /* !0 if any column contains an operator */
	int group_count;    /* The number of columns in the
			       group index key */
	int *group_cols;    /* Array of column indexes for each
			       column in the key */
	int row_order_count;/* The number of columns in the
			       row group index key */
	int *row_order_cols;/* Array of column indexes for each
			       column in the row key */
} *decomp_static_row_cfg_t;

typedef struct decomp_static_cfg_s {
	struct ldmsd_decomp_s decomp;
	int row_count;
	struct decomp_static_row_cfg_s rows[OVIS_FLEX];
} *decomp_static_cfg_t;

typedef struct decomp_static_mid_rbn_s {
	struct rbn rbn;
	struct ldms_digest_s ldms_digest;
	int col_count;
	struct {
		int mid;
		int rec_mid;
		enum ldms_value_type mtype;
		size_t array_len;
		enum ldms_value_type rec_mtype;
		size_t mval_size;	/* Size of this mval */
		off_t mval_offset;	/* Offset into mval memory for this column */
	} col_mids[OVIS_FLEX];
} *decomp_static_mid_rbn_t;

static int __mid_rbn_cmp(void *tree_key, const void *key)
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

static int str_int_cmp(const void *a, const void *b)
{
	const struct str_int_s *sa = a, *sb = b;
	return strcmp(sa->str, sb->str);
}

static void decomp_static_cfg_free(decomp_static_cfg_t dcfg)
{
	int i, j;
	struct decomp_static_row_cfg_s *cfg_row;
	struct decomp_static_col_cfg_s *cfg_col;
	for (i = 0; i < dcfg->row_count; i++) {
		cfg_row = &dcfg->rows[i];
		/* cols */
		for (j = 0; j < cfg_row->col_count; j++) {
			cfg_col = &cfg_row->cols[j];
			free(cfg_col->src);
			free(cfg_col->dst);
			free(cfg_col->rec_member);
			if (cfg_col->fill != &cfg_col->__fill)
				free(cfg_col->fill);
		}
		free(cfg_row->cols);
		/* idxs */
		for (j = 0; j < cfg_row->idx_count; j++) {
			free(cfg_row->idxs[j].name);
			free(cfg_row->idxs[j].col_idx);
		}
		free(cfg_row->idxs);
		/* schema */
		free(cfg_row->schema_name);
	}
	free(dcfg);
}

static void
decomp_static_release_decomp(ldmsd_strgp_t strgp)
{
	if (strgp->decomp) {
		decomp_static_cfg_free((void*)strgp->decomp);
		strgp->decomp = NULL;
	}
}

static int get_col_no(decomp_static_row_cfg_t cfg_row, const char *name)
{
	int i;
	for (i = 0; i < cfg_row->col_count; i++) {
		if (0 == strcmp(name, cfg_row->cols[i].dst))
			return i;
	}
	return -1;
}

static int handle_indices(
		ldmsd_strgp_t strgp, json_t *jidxs,
		decomp_static_row_cfg_t cfg_row, int row_no,
		ldmsd_req_ctxt_t reqc)
{
	struct str_int_tbl_s *col_id_tbl = NULL;
	struct str_int_s key, *tbl_ent;
	decomp_index_t didx;
	json_t *jidx, *jidx_cols, *jcol, *jname;
	int rc, j, k;

	if (!jidxs)
		return 0;

	if (!json_is_array(jidxs)) {
		THISLOG(reqc, EINVAL, "strgp '%s': row '%d': "
			"the 'indices' value must be an array.\n",
			strgp->obj.name, row_no);
		return EINVAL;
	}

	cfg_row->idx_count = json_array_size(jidxs);
	cfg_row->idxs = calloc(1, cfg_row->idx_count * sizeof(cfg_row->idxs[0]));
	if (!cfg_row->idxs)
		goto enomem;
	cfg_row->row_sz += cfg_row->idx_count * sizeof(ldmsd_row_index_t);
	/* prep temporary col-id table */
	col_id_tbl = calloc(1, sizeof(*col_id_tbl) +
				(cfg_row->col_count * sizeof(col_id_tbl->ent[0])));
	if (!col_id_tbl)
		goto enomem;
	col_id_tbl->len = cfg_row->col_count;
	for (j = 0; j < cfg_row->col_count; j++) {
		col_id_tbl->ent[j].str = cfg_row->cols[j].dst;
		col_id_tbl->ent[j].i = j;
	}

	qsort(col_id_tbl->ent, col_id_tbl->len, sizeof(col_id_tbl->ent[0]), str_int_cmp);
	/* foreach index */
	json_array_foreach(jidxs, j, jidx) {
		didx = &cfg_row->idxs[j];
		if (!json_is_object(jidx)) {
			THISLOG(reqc, EINVAL, "strgp '%s': row '%d': "
				"an index must be a dictionary.\n",
				strgp->obj.name, row_no);
			rc = EINVAL;
			goto err_0;
		}
		jname = json_object_get(jidx, "name");
		if (!jname) {
			THISLOG(reqc, EINVAL, "strgp '%s': row '%d': index '%d': "
				"index['name'] is required.\n",
				strgp->obj.name, row_no, j);
			rc = EINVAL;
			goto err_0;
		}
		didx->name = strdup(json_string_value(jname));
		if (!didx->name)
			goto enomem;
		jidx_cols = json_object_get(jidx, "cols");
		if (!jidx_cols) {
			THISLOG(reqc, EINVAL, "strgp '%s': row '%d': index '%d':"
				"index['cols'] is required.\n",
				strgp->obj.name, row_no, j);
			rc = EINVAL;
			goto err_0;
		}
		didx->col_count = json_array_size(jidx_cols);
		didx->col_idx = calloc(1, didx->col_count * sizeof(didx->col_idx[0]));
		if (!didx->col_idx)
			goto enomem;
		cfg_row->row_sz += sizeof(struct ldmsd_row_index_s) +
				(didx->col_count * sizeof(ldmsd_col_t));
		/* resolve col name to col id */
		json_array_foreach(jidx_cols, k, jcol) {
			if (!json_is_string(jcol)) {
				THISLOG(reqc, EINVAL, "strgp '%s': row '%d': index '%d': col '%d': "
					"index['cols'][x] value must be a string.\n",
					strgp->obj.name, row_no, j, k);
				rc = EINVAL;
				goto err_0;
			}
			key.str = json_string_value(jcol);
			tbl_ent = bsearch(&key, col_id_tbl->ent,
						col_id_tbl->len,
						sizeof(col_id_tbl->ent[0]),
						str_int_cmp);
			if (!tbl_ent) {
				THISLOG(reqc, ENOENT, "strgp '%s': row '%d': index '%d': "
					"column '%s' not found.\n",
					strgp->obj.name, row_no, j, key.str);
				rc = ENOENT;
				goto err_0;
			}
			didx->col_idx[k] = tbl_ent->i;
		}
	}
	free(col_id_tbl);
	return 0;
enomem:
	rc = ENOMEM;
	THISLOG(reqc, ENOMEM, "%s: Insufficent memory.\n", strgp->obj.name);
err_0:
	return rc;
}

static int handle_group(
		ldmsd_strgp_t strgp, json_t *jgroup,
		decomp_static_row_cfg_t cfg_row, int row_no,
		ldmsd_req_ctxt_t reqc)
{
	json_t *jidxs, *jidx;
	json_t *jlimit;
	int col_no;
	int rc;

	jlimit  = json_object_get(jgroup, "limit");
	if (jlimit) {
		if (json_is_integer(jlimit)) {
			cfg_row->row_limit = json_integer_value(jlimit);
		} else if (json_is_string(jlimit)) {
			cfg_row->row_limit = strtoul(json_string_value(jlimit), NULL, 0);
		} else {
			THISLOG(reqc, EINVAL, "strgp '%s': row '%d': "
				"group['index'] is a required and must be an array.\n",
				strgp->obj.name, row_no);
			return EINVAL;
		}
	} else {
		cfg_row->row_limit = 2;
	}

	/* Loop through all of the entries in the
	 * "index" list and set the associated
	 * column index in the group_cols array
	 */
	jidxs = json_object_get(jgroup, "index");
	if (!jidxs || !json_is_array(jidxs)) {
		THISLOG(reqc, EINVAL, "strgp '%s': row '%d': "
			"group['index'] is a required and must be an array.\n",
			strgp->obj.name, row_no);
		return EINVAL;
	}

	cfg_row->group_count = json_array_size(jidxs);
	cfg_row->group_cols = calloc(cfg_row->group_count,
					sizeof(*cfg_row->group_cols));
	if (!cfg_row->group_cols)
		goto enomem;

	json_array_foreach(jidxs, col_no, jidx) {
		if (!json_is_string(jidx)) {
			THISLOG(reqc, EINVAL, "strgp '%s': row '%d': "
				"group['index'] entries must be column "
				"name strings.\n",
				strgp->obj.name, row_no);
			rc = EINVAL;
			goto err_0;
		}
		cfg_row->group_cols[col_no] = get_col_no(cfg_row, json_string_value(jidx));
		if (cfg_row->group_cols[col_no] < 0) {
			THISLOG(reqc, EINVAL, "strgp '%s': row '%d': "
				"group['index'] the specified column '%s'"
				"is not present in the column list.\n",
				strgp->obj.name, row_no, json_string_value(jidx));
			rc = EINVAL;
			goto err_0;
		}
	}

	jidxs = json_object_get(jgroup, "order");
	if (!jidxs || !json_is_array(jidxs)) {
		THISLOG(reqc, EINVAL, "strgp '%s': row '%d': "
			"group['order'] is a required and must be an array.\n",
			strgp->obj.name, row_no);
		rc = EINVAL;
		goto err_0;
	}
	cfg_row->row_order_count = json_array_size(jidxs);
	cfg_row->row_order_cols = calloc(cfg_row->row_order_count,
					sizeof(*cfg_row->row_order_cols));
	if (!cfg_row->row_order_cols)
		goto enomem;

	json_array_foreach(jidxs, col_no, jidx) {
		if (!json_is_string(jidx)) {
			THISLOG(reqc, EINVAL, "strgp '%s': row '%d': "
				"group['order'] entries must be column "
				"name strings.\n",
				strgp->obj.name, row_no);
			rc = EINVAL;
			goto err_0;
		}
		cfg_row->row_order_cols[col_no] =
				get_col_no(cfg_row, json_string_value(jidx));
		if (cfg_row->row_order_cols[col_no] < 0) {
			THISLOG(reqc, EINVAL, "strgp '%s': row '%d': "
				"group['order'] the specified column '%s'"
				"is not present in the column list.\n",
				strgp->obj.name, row_no, json_string_value(jidx));
			rc = ENOENT;
			goto err_0;
		}
	}

	strgp->row_cache = ldmsd_row_cache_create(strgp, cfg_row->row_limit);
	if (!strgp->row_cache)
		goto enomem;
	return 0;
enomem:
	rc = ENOMEM;
	THISLOG(reqc, ENOMEM, "%s: Insufficient memory.\n", strgp->obj.name);
err_0:
	return rc;
}

static enum ldmsd_decomp_op
string_to_ldmsd_decomp_op(const char *operation)
{
        if (0 == strcmp(operation, "diff")) {
                return LDMSD_DECOMP_OP_DIFF;
        } else if (0 == strcmp(operation, "mean")) {
                return LDMSD_DECOMP_OP_MEAN;
        } else if (0 == strcmp(operation, "min")) {
                return LDMSD_DECOMP_OP_MIN;
        } else if (0 == strcmp(operation, "max")) {
                return LDMSD_DECOMP_OP_MAX;
        } else {
                return LDMSD_DECOMP_OP_NONE;
        }
}

static const char *
ldmsd_decomp_op_to_string(enum ldmsd_decomp_op operation)
{
        switch (operation) {
        case LDMSD_DECOMP_OP_DIFF:
                return "diff";
        case LDMSD_DECOMP_OP_MEAN:
                return "mean";
        case LDMSD_DECOMP_OP_MIN:
                return "min";
        case LDMSD_DECOMP_OP_MAX:
                return "max";
        default:
                return "none";
        }
}

static ldmsd_decomp_t
decomp_static_config(ldmsd_strgp_t strgp, json_t *jcfg,
		     ldmsd_req_ctxt_t reqc)
{
	json_t *jsch, *jsrc, *jdst, *jrec_member;
	json_t *jrows, *jcols, *jidxs;
	json_t *jrow, *jcol, *jfill, *jop;
	decomp_static_row_cfg_t cfg_row;
	decomp_static_col_cfg_t cfg_col;
	decomp_static_cfg_t dcfg = NULL;
	int row_no, col_no, rc;

	jrows = json_object_get(jcfg, "rows");
	if (!jrows || !json_is_array(jrows)) {
		THISLOG(reqc, errno,
			"strgp '%s': The 'rows' attribute is missing, "
			"or its value is not a list.\n",
			strgp->obj.name);
		return NULL;
	}
	dcfg = calloc(1, sizeof(*dcfg) + json_array_size(jrows) * sizeof(dcfg->rows[0]));
	if (!dcfg)
		goto enomem;
	dcfg->decomp = decomp_static;

	/* for each row schema */
	json_array_foreach(jrows, row_no, jrow) {
		if (!json_is_object(jrow)) {
			THISLOG(reqc, EINVAL,
				"strgp '%s': row '%d': "
				"The list item must be a dictionary.\n",
				strgp->obj.name, row_no);
			goto err_0;
		}

		cfg_row = &dcfg->rows[row_no];
		cfg_row->row_sz = sizeof(struct ldmsd_row_s);
		rbt_init(&cfg_row->mid_rbt, __mid_rbn_cmp);

		/* schema name */
		jsch = json_object_get(jrow, "schema");
		if (!jsch || !json_is_string(jsch)) {
			THISLOG(reqc, EINVAL, "strgp '%s': row '%d': "
				"row['schema'] attribute is required and must be a string\n",
				strgp->obj.name, row_no);
			goto err_0;
		}
		cfg_row->schema_name = strdup(json_string_value(jsch));
		if (!cfg_row->schema_name)
			goto enomem;

		/* columns */
		jcols = json_object_get(jrow, "cols");
		if (!jcols || !json_is_array(jcols)) {
			THISLOG(reqc, EINVAL, "strgp '%s': row '%d': "
				"row['cols'] is required and must be an array.\n",
				strgp->obj.name, row_no);
			goto err_0;
		}

		cfg_row->col_count = json_array_size(jcols);
		if (!cfg_row->col_count) {
			THISLOG(reqc, EINVAL, "strgp '%s': row '%d': "
				"row['cols'] list is empty\n",
				strgp->obj.name, row_no);
			goto err_0;
		}
		/* Add a configuration for the 'timestamp' column that goes in every row */
		cfg_row->cols = calloc(1, (cfg_row->col_count * sizeof(cfg_row->cols[0])));
		if (!cfg_row->cols)
			goto enomem;
		cfg_row->row_sz += cfg_row->col_count * sizeof(struct ldmsd_col_s);

		/* for each column */
		json_array_foreach(jcols, col_no, jcol) {
			if (!json_is_object(jcol)) {
				THISLOG(reqc, EINVAL, "strgp '%s': row '%d'--col '%d': "
					"a column entry must be a dictionary.\n",
					strgp->obj.name, row_no, col_no);
				goto err_0;
			}
			cfg_col = &cfg_row->cols[col_no];
			jsrc = json_object_get(jcol, "src");
			if (!jsrc || !json_is_string(jsrc)) {
				THISLOG(reqc, EINVAL, "strgp '%s': row '%d'--col '%d': "
					"column['src'] is required and must be a string.\n",
					strgp->obj.name, row_no, col_no);
				goto err_0;
			}
			cfg_col->src = strdup(json_string_value(jsrc));
			if (!cfg_col->src)
				goto enomem;
			jdst = json_object_get(jcol, "dst");
			if (jdst && json_is_string(jdst)) {
				cfg_col->dst = strdup(json_string_value(jdst));
			} else {
				/* Inherit the destination name from the source */
				cfg_col->dst = strdup(cfg_col->src);
			}
			if (!cfg_col->dst)
				goto enomem;

			jrec_member = json_object_get(jcol, "rec_member");
			if (jrec_member) {
				if (!json_is_string(jrec_member)) {
					THISLOG(reqc, EINVAL, "strgp '%s': row '%d'--col '%d': "
						"rec_member must be a string.\n",
						strgp->obj.name, row_no, col_no);
					goto err_0;
				}
				cfg_col->rec_member = strdup(json_string_value(jrec_member));
				if (!cfg_col->rec_member)
					goto enomem;
			}

			jop = json_object_get(jcol, "op");
			if (jop) {
                                cfg_col->op = string_to_ldmsd_decomp_op(json_string_value(jop));
                                if (cfg_col->op == LDMSD_DECOMP_OP_NONE) {
					THISLOG(reqc, EINVAL, "strgp '%s': row '%d'--col %d : "
						"unrecognized functional operator '%s'\n",
						strgp->obj.name, row_no, col_no, json_string_value(jop));
					goto err_0;
                                }
                                cfg_row->op_present = 1; /* true */
			}
			jfill = json_object_get(jcol, "fill");
			if (jfill) {
				/* The remaining code handles 'fill' */
				rc = mval_from_json(&cfg_col->fill, &cfg_col->type,
						&cfg_col->fill_len, jfill);
				if (rc) {
					THISLOG(reqc, EINVAL,
						"strgp '%s': row '%d': col[dst] '%s',"
						"'fill' error %d preparing metric value.\n",
						strgp->obj.name, row_no, cfg_col->dst, rc);
					goto err_0;
				}
			}
		}

		/* indices */
		jidxs = json_object_get(jrow, "indices");
		if (jidxs) {
			rc = handle_indices(strgp, jidxs, cfg_row, row_no, reqc);
			if (rc)
				goto err_0;
		}

		/* group clause */
		json_t *jgroup = json_object_get(jrow, "group");
		if (jgroup) {
			rc = handle_group(strgp, jgroup, cfg_row, row_no, reqc);
			if (rc)
				goto err_0;
		}

		dcfg->row_count++;
	}
	return &dcfg->decomp;
enomem:
	THISLOG(reqc, errno, "%s: Insufficient memory.\n", strgp->obj.name);
err_0:
	decomp_static_cfg_free(dcfg);
	return NULL;
}

static int resolve_metrics(ldmsd_strgp_t strgp,
			decomp_static_mid_rbn_t mid_rbn,
			decomp_static_row_cfg_t cfg_row,
			ldms_set_t set)
{
	EVP_MD_CTX *evp_ctx = NULL;
	int col_no, mid;
	size_t mval_offset = 0;
	ldms_mval_t lh, le, rec_array, rec;
	size_t mlen = -1;
	const char *src;
	enum ldms_value_type mtype;

	evp_ctx = EVP_MD_CTX_create();
	if (!evp_ctx) {
		ldmsd_log(LDMSD_LERROR, "out of memory\n");
		goto err;
	}
	EVP_DigestInit_ex(evp_ctx, EVP_sha256(), NULL);

	mid_rbn->col_count = cfg_row->col_count;
	for (col_no = 0; col_no < mid_rbn->col_count; col_no++) {
		mid_rbn->col_mids[col_no].mid = -1;
		mid_rbn->col_mids[col_no].rec_mid = -1;
		mid_rbn->col_mids[col_no].array_len = -1;
		mid_rbn->col_mids[col_no].rec_mid = -EINVAL;
		mid_rbn->col_mids[col_no].rec_mtype = LDMS_V_NONE;

		src = cfg_row->cols[col_no].src;

		if (0 == strcmp(src, "timestamp")) {
			mid_rbn->col_mids[col_no].mid = LDMSD_PHONY_METRIC_ID_TIMESTAMP;
			mid_rbn->col_mids[col_no].mtype = LDMS_V_TIMESTAMP;
			mid_rbn->col_mids[col_no].mval_offset = mval_offset;
			mid_rbn->col_mids[col_no].mval_size =
				ldms_metric_value_size_get(LDMS_V_TIMESTAMP, 0);
			mval_offset += LDMS_ROUNDUP(mid_rbn->col_mids[col_no].mval_size,
						sizeof(uint64_t));
			goto next;
		}

		if (0 == strcmp(src, "producer")) {
			mid_rbn->col_mids[col_no].mid = LDMSD_PHONY_METRIC_ID_PRODUCER;
			cfg_row->cols[col_no].type = LDMS_V_CHAR_ARRAY;
			/* Doesn't consume mval space, data comes from
			 * instance name in set meta-data */
			goto next;
		}

		if (0 == strcmp(src, "instance")) {
			mid_rbn->col_mids[col_no].mid = LDMSD_PHONY_METRIC_ID_INSTANCE;
			cfg_row->cols[col_no].type = LDMS_V_CHAR_ARRAY;
			/* Doesn't consume mval space, data comes from
			 * instance name in set meta-data */
			goto next;
		}

		mid = ldms_metric_by_name(set, cfg_row->cols[col_no].src);
		mid_rbn->col_mids[col_no].mid = mid;
		if (mid < 0) {
			/* This is a fill candidate, must have a type and if
			 * an array a length */
			if (cfg_row->cols[col_no].type) {
				if (ldms_type_is_array(cfg_row->cols[col_no].type)) {
					if (cfg_row->cols[col_no].array_len < 0) {
						ldmsd_log(LDMSD_LERROR,
							"strgp '%s': col[dst] '%s' "
							"array must have a len specified if it "
							"does not exist in the set.\n",
							strgp->obj.name,
							cfg_row->cols[col_no].dst);
						goto err;
					}
				}
				/* Metric does not exist, but it has a type and if needed
				 * an array length */
				mid_rbn->col_mids[col_no].mid = LDMSD_PHONY_METRIC_ID_FILL;
				mtype  = cfg_row->cols[col_no].type;
				mlen = cfg_row->cols[col_no].array_len;
				mid_rbn->col_mids[col_no].mtype = mtype;
				mid_rbn->col_mids[col_no].array_len = mlen;
				goto next;
			}
			ldmsd_log(LDMSD_LERROR,
				"strgp '%s': col[src] '%s' "
				"does not exist in the set and the 'type' is not "
				"specified.\n",
				strgp->obj.name, cfg_row->cols[col_no].src);
			goto err;
		}

		mtype = ldms_metric_type_get(set, mid);
		mid_rbn->col_mids[col_no].mtype = mtype;
		mlen = ldms_metric_array_get_len(set, mid);
		mid_rbn->col_mids[col_no].array_len = mlen;

		if (mtype == LDMS_V_LIST)
			goto list_routine;
		if (mtype == LDMS_V_RECORD_ARRAY)
			goto rec_array_routine;

		/* primitives & array of primitives */
		mid_rbn->col_mids[col_no].mval_offset = mval_offset;
		mid_rbn->col_mids[col_no].mval_size = ldms_metric_value_size_get(mtype, mlen);
		mval_offset += LDMS_ROUNDUP(mid_rbn->col_mids[col_no].mval_size, sizeof(uint64_t));

		if (mtype > LDMS_V_D64_ARRAY) {
			/* Invalid type */
			ldmsd_log(LDMSD_LERROR,
				"strgp '%s': col[src] '%s' "
				"the metric type %d is not supported.\n",
				strgp->obj.name, cfg_row->cols[col_no].src,
				mtype
				);
			goto err;
		}
		goto next;

	list_routine:
		/* handling LIST */
		lh = ldms_metric_get(set, mid);
		le = ldms_list_first(set, lh, &mtype, &mlen);
		if (!le) {
			/* list empty. can't init yet */
			ldmsd_log(LDMSD_LERROR,
				"strgp '%s': row '%d': col[dst] '%s' "
				"LIST is empty, skipping set metric resolution.\n",
				strgp->obj.name, col_no, cfg_row->cols[col_no].dst
				);
			errno = ENOENT;
			goto err;
		}
		if (mtype == LDMS_V_LIST) {
			/* LIST of LIST is not supported */
			/* Invalid type */
			ldmsd_log(LDMSD_LERROR,
				"strgp '%s': row '%d': col[dst] '%s' "
				"LIST of LIST is not supported.\n",
				strgp->obj.name, col_no, cfg_row->cols[col_no].dst
				);
			goto err;
		}
		if (!cfg_row->cols[col_no].rec_member) {
			/* LIST of non-record elements */
			goto next;
		}
		/* LIST of records */
		mid = ldms_record_metric_find(le, cfg_row->cols[col_no].rec_member);
		mid_rbn->col_mids[col_no].rec_mid = mid;
		if (mid >= 0) {
			mtype = ldms_record_metric_type_get(le, mid, &mlen);
			mid_rbn->col_mids[col_no].rec_mtype = mtype;
			mid_rbn->col_mids[col_no].mval_offset = mval_offset;
			mid_rbn->col_mids[col_no].mval_size =
				ldms_metric_value_size_get(mtype, mlen);
			mval_offset +=
				LDMS_ROUNDUP(mid_rbn->col_mids[col_no].mval_size,
						sizeof(uint64_t));
		}
		goto next;

	rec_array_routine:
		rec_array = ldms_metric_get(set, mid);
		rec = ldms_record_array_get_inst(rec_array, 0);
		if (!cfg_row->cols[col_no].rec_member) {
			ldmsd_log(LDMSD_LERROR,
				"strgp '%s': row '%d': the record array '%s' "
				"is emptyd.\n",
				strgp->obj.name, col_no, cfg_row->cols[col_no].src);
			goto err;
		}
		mid = ldms_record_metric_find(rec, cfg_row->cols[col_no].rec_member);
		if (mid < 0) {
			ldmsd_log(LDMSD_LERROR,
				"strgp '%s': row '%d': col[dst] '%s' "
				"Missing record member definition.n",
				strgp->obj.name, col_no, cfg_row->cols[col_no].dst);
			goto next;
		}
		mtype = ldms_record_metric_type_get(rec, mid, &mlen);
		mid_rbn->col_mids[col_no].rec_mid = mid;
		mid_rbn->col_mids[col_no].rec_mtype = mtype;
		mtype = ldms_record_metric_type_get(le, mid, &mlen);
		mid_rbn->col_mids[col_no].rec_mtype = mtype;
		mid_rbn->col_mids[col_no].mval_offset = mval_offset;
		mid_rbn->col_mids[col_no].mval_size =
			ldms_metric_value_size_get(mtype,
				ldms_metric_array_get_len(set, mid));
		mval_offset += LDMS_ROUNDUP(mid_rbn->col_mids[col_no].mval_size,
					sizeof(uint64_t));
next:
		/* update row schema digest */
		EVP_DigestUpdate(evp_ctx, cfg_row->cols[col_no].dst,
			strlen(cfg_row->cols[col_no].dst));
		EVP_DigestUpdate(evp_ctx, &cfg_row->cols[col_no].type,
					sizeof(cfg_row->cols[col_no].type));
	}
	cfg_row->mval_size = mval_offset;
	/* Finalize row schema digest */
	unsigned int len = LDMS_DIGEST_LENGTH;
	EVP_DigestFinal(evp_ctx, cfg_row->schema_digest.digest, &len);
	EVP_MD_CTX_destroy(evp_ctx);
	return 0;
err:
	EVP_MD_CTX_destroy(evp_ctx);
	if (errno)
		return errno;
	return EINVAL;
}

static ldmsd_row_t
row_cache_dup(decomp_static_row_cfg_t cfg_row,
		decomp_static_mid_rbn_t mid_rbn,
		ldmsd_row_t row)
{
	int i, j;
	ldmsd_row_index_t idx;
	ldmsd_row_t dup_row = NULL;

	dup_row = calloc(1, cfg_row->row_sz + cfg_row->mval_size);
	if (!dup_row)
		goto out;

	dup_row->schema = row->schema;
	dup_row->schema_name = row->schema_name;
	dup_row->schema_digest = row->schema_digest;
	dup_row->idx_count = row->idx_count;
	dup_row->col_count = row->col_count;

	/* indices */
	dup_row->indices = (void*)&dup_row->cols[row->col_count];
	idx = (void*)&dup_row->indices[dup_row->idx_count];
	for (i = 0; i < dup_row->idx_count; i++) {
		dup_row->indices[i] = idx;
		idx->col_count = cfg_row->idxs[i].col_count;
		idx->name = cfg_row->idxs[i].name;
		for (j = 0; j < idx->col_count; j++) {
			int c = cfg_row->idxs[i].col_idx[j];
			idx->cols[j] = &row->cols[c];
		}
		idx = (void*)&idx->cols[idx->col_count];
	}
	dup_row->mvals = (uint8_t *)idx;
	memcpy(dup_row->mvals, row->mvals, cfg_row->mval_size);
	for (i = 0; i < row->col_count; i++) {
		dup_row->cols[i].name = row->cols[i].name;
		dup_row->cols[i].column = row->cols[i].column;
		dup_row->cols[i].type = row->cols[i].type;
		dup_row->cols[i].array_len = row->cols[i].array_len;
		dup_row->cols[i].metric_id = row->cols[i].metric_id;
		dup_row->cols[i].rec_metric_id = row->cols[i].rec_metric_id;
		switch (dup_row->cols[i].metric_id) {
		case LDMSD_PHONY_METRIC_ID_PRODUCER:
		case LDMSD_PHONY_METRIC_ID_INSTANCE:
		case LDMSD_PHONY_METRIC_ID_FILL:
			dup_row->cols[i].mval = row->cols[i].mval;
			break;
		case LDMSD_PHONY_METRIC_ID_TIMESTAMP:
		default:
			dup_row->cols[i].mval = (ldms_mval_t)&dup_row->mvals[mid_rbn->col_mids[i].mval_offset];
		}
	}
 out:
	return dup_row;
}

static void assign_value(ldms_mval_t dst, ldms_mval_t src,
			enum ldms_value_type type, size_t count)
{
	switch (type) {
	case LDMS_V_U8:
		dst->v_u8 = src->v_u8;
		break;
	case LDMS_V_S8:
		dst->v_s8 = src->v_s8;
		break;
	case LDMS_V_U16:
		dst->v_u16 = src->v_u16;
		break;
	case LDMS_V_S16:
		dst->v_s16 = src->v_s16;
		break;
	case LDMS_V_U32:
		dst->v_u32 = src->v_u32;
		break;
	case LDMS_V_S32:
		dst->v_s32 = src->v_s32;
		break;
	case LDMS_V_U64:
		dst->v_u64 = src->v_u64;
		break;
	case LDMS_V_S64:
		dst->v_s64 = src->v_s64;
		break;
	case LDMS_V_F32:
		dst->v_f = src->v_f;
		break;
	case LDMS_V_D64:
		dst->v_d = src->v_d;
		break;
	case LDMS_V_TIMESTAMP:
		dst->v_ts = src->v_ts;
		break;
	case LDMS_V_CHAR_ARRAY:
		memcpy(dst->a_char, src->a_char, count);
		break;
	default:
		break;
	}
}

typedef int (*ldmsd_functional_op_t)(ldmsd_row_list_t row_list, ldmsd_row_t dest_row, int col_id);
static int none_op(ldmsd_row_list_t row_list, ldmsd_row_t dest_row, int col_id)
{
	ldmsd_row_t src_row = TAILQ_FIRST(row_list);
	ldmsd_col_t src_col = &src_row->cols[col_id];
	ldmsd_col_t dst_col = &dest_row->cols[col_id];
	assign_value(dst_col->mval, src_col->mval,
			dst_col->type, dst_col->array_len);
	return 0;
}

static int diff_op(ldmsd_row_list_t row_list, ldmsd_row_t dest_row, int col_id)
{
	ldmsd_row_t src_row = TAILQ_FIRST(row_list);
	ldmsd_row_t prev_row = TAILQ_NEXT(src_row, entry);
	ldmsd_col_t dst_col = &dest_row->cols[col_id];
	union ldms_value zero;
	memset(&zero, 0, sizeof(zero));
	/* dst_col->mval = src_row->cols[col_id].mval - prev_row->cols[col_id].mval; */
	if (!prev_row) {
		assign_value(dst_col->mval, &zero, dst_col->type, dst_col->array_len);
		return 0;
	}
	switch (dst_col->type) {
	case LDMS_V_U8:
		dst_col->mval->v_u8 =
			src_row->cols[col_id].mval->v_u8 -
			prev_row->cols[col_id].mval->v_u8;
		break;
	case LDMS_V_S8:
		dst_col->mval->v_s8 =
			src_row->cols[col_id].mval->v_s8 -
			prev_row->cols[col_id].mval->v_s8;
		break;
	case LDMS_V_U16:
		dst_col->mval->v_u16 =
			src_row->cols[col_id].mval->v_u16 -
			prev_row->cols[col_id].mval->v_u16;
		break;
	case LDMS_V_S16:
		dst_col->mval->v_s16 =
			src_row->cols[col_id].mval->v_s16 -
			prev_row->cols[col_id].mval->v_s16;
		break;
	case LDMS_V_U32:
		dst_col->mval->v_u32 =
			src_row->cols[col_id].mval->v_u32 -
			prev_row->cols[col_id].mval->v_u32;
		break;
	case LDMS_V_S32:
		dst_col->mval->v_s32 =
			src_row->cols[col_id].mval->v_s32 -
			prev_row->cols[col_id].mval->v_s32;
		break;
	case LDMS_V_U64:
		dst_col->mval->v_u64 =
			src_row->cols[col_id].mval->v_u64 -
			prev_row->cols[col_id].mval->v_u64;
		break;
	case LDMS_V_S64:
		dst_col->mval->v_s64 =
			src_row->cols[col_id].mval->v_s64 -
			prev_row->cols[col_id].mval->v_s64;
		break;
	case LDMS_V_F32:
		dst_col->mval->v_f =
			src_row->cols[col_id].mval->v_f -
			prev_row->cols[col_id].mval->v_f;
		break;
	case LDMS_V_D64:
		dst_col->mval->v_d =
			src_row->cols[col_id].mval->v_d -
			prev_row->cols[col_id].mval->v_d;
		break;
	case LDMS_V_TIMESTAMP:
		dst_col->mval->v_ts.sec = src_row->cols[col_id].mval->v_ts.sec -
			prev_row->cols[col_id].mval->v_ts.sec;
		dst_col->mval->v_ts.usec = src_row->cols[col_id].mval->v_ts.usec -
			prev_row->cols[col_id].mval->v_ts.usec;
		break;
	default:
		return EINVAL;
	}
	return 0;
}

static int mean_op(ldmsd_row_list_t row_list, ldmsd_row_t dest_row, int col_id)
{
	int count = 0;
	ldmsd_row_t src_row = TAILQ_FIRST(row_list);
	ldmsd_col_t dst_col = &dest_row->cols[col_id];
	union ldms_value zero;
	memset(&zero, 0, sizeof(zero));

	assign_value(dst_col->mval, &zero, dst_col->type, dst_col->array_len);
	while (src_row)
	{
		switch (dst_col->type) {
		case LDMS_V_U8:
			dst_col->mval->v_u8 += src_row->cols[col_id].mval->v_u8;
			break;
		case LDMS_V_S8:
			dst_col->mval->v_s8 += src_row->cols[col_id].mval->v_s8;
			break;
		case LDMS_V_U16:
			dst_col->mval->v_u16 += src_row->cols[col_id].mval->v_u16;
			break;
		case LDMS_V_S16:
			dst_col->mval->v_s16 += src_row->cols[col_id].mval->v_s16;
			break;
		case LDMS_V_U32:
			dst_col->mval->v_u32 += src_row->cols[col_id].mval->v_u32;
			break;
		case LDMS_V_S32:
			dst_col->mval->v_s32 += src_row->cols[col_id].mval->v_s32;
			break;
		case LDMS_V_U64:
			dst_col->mval->v_u64 += src_row->cols[col_id].mval->v_u64;
			break;
		case LDMS_V_S64:
			dst_col->mval->v_s64 += src_row->cols[col_id].mval->v_s64;
			break;
		case LDMS_V_F32:
			dst_col->mval->v_f += src_row->cols[col_id].mval->v_f;
			break;
		case LDMS_V_D64:
			dst_col->mval->v_d += src_row->cols[col_id].mval->v_d;
			break;
		case LDMS_V_TIMESTAMP:
			dst_col->mval->v_ts.sec += src_row->cols[col_id].mval->v_ts.sec;
			dst_col->mval->v_ts.usec += src_row->cols[col_id].mval->v_ts.usec;
			break;
		default:
			return EINVAL;
		}
		count += 1;
		src_row = TAILQ_NEXT(src_row, entry);
	}
	switch (dst_col->type) {
	case LDMS_V_U8:
		dst_col->mval->v_u8 /= count;
		break;
	case LDMS_V_S8:
		dst_col->mval->v_s8 /= count;
		break;
	case LDMS_V_U16:
		dst_col->mval->v_u16 /= count;
		break;
	case LDMS_V_S16:
		dst_col->mval->v_s16 /= count;
		break;
	case LDMS_V_U32:
		dst_col->mval->v_u32 /= count;
		break;
	case LDMS_V_S32:
		dst_col->mval->v_s32 /= count;
		break;
	case LDMS_V_U64:
		dst_col->mval->v_u64 /= count;
		break;
	case LDMS_V_S64:
		dst_col->mval->v_s64 /= count;
		break;
	case LDMS_V_F32:
		dst_col->mval->v_f /= count;
		break;
	case LDMS_V_D64:
		dst_col->mval->v_d /= count;
		break;
	case LDMS_V_TIMESTAMP:
		dst_col->mval->v_ts.sec /= count;
		dst_col->mval->v_ts.usec /= count;
		break;
	default:
		return EINVAL;
	}
	return 0;
}

static int max_op(ldmsd_row_list_t row_list, ldmsd_row_t dest_row, int col_id)
{
	ldmsd_row_t src_row = TAILQ_FIRST(row_list);
	ldmsd_col_t dst_col = &dest_row->cols[col_id];
	union ldms_value max;

	assign_value(&max, src_row->cols[col_id].mval, dst_col->type, dst_col->array_len);
	src_row = TAILQ_NEXT(src_row, entry);
	while (src_row)
	{
		switch (dst_col->type) {
		case LDMS_V_U8:
			if (max.v_u8 < src_row->cols[col_id].mval->v_u8)
				max.v_u8 = src_row->cols[col_id].mval->v_u8;
			break;
		case LDMS_V_S8:
			if (max.v_s8 < src_row->cols[col_id].mval->v_s8)
				max.v_s8 = src_row->cols[col_id].mval->v_s8;
			break;
		case LDMS_V_U16:
			if (max.v_u16 < src_row->cols[col_id].mval->v_u16)
				max.v_u16 = src_row->cols[col_id].mval->v_u16;
			break;
		case LDMS_V_S16:
			if (max.v_s16 < src_row->cols[col_id].mval->v_s16)
				max.v_s16 = src_row->cols[col_id].mval->v_s16;
			break;
		case LDMS_V_U32:
			if (max.v_u32 < src_row->cols[col_id].mval->v_u32)
				max.v_u32 = src_row->cols[col_id].mval->v_u32;
			break;
		case LDMS_V_S32:
			if (max.v_s32 < src_row->cols[col_id].mval->v_s32)
				max.v_s32 = src_row->cols[col_id].mval->v_s32;
			break;
		case LDMS_V_U64:
			if (max.v_u64 < src_row->cols[col_id].mval->v_u64)
				max.v_u64 = src_row->cols[col_id].mval->v_u64;
			break;
		case LDMS_V_S64:
			if (max.v_s64 < src_row->cols[col_id].mval->v_s64)
				max.v_s64 = src_row->cols[col_id].mval->v_s64;
			break;
		case LDMS_V_F32:
			if (max.v_f < src_row->cols[col_id].mval->v_f)
				max.v_f = src_row->cols[col_id].mval->v_f;
			break;
		case LDMS_V_D64:
			if (max.v_d < src_row->cols[col_id].mval->v_d)
				max.v_d = src_row->cols[col_id].mval->v_d;
			break;
		case LDMS_V_TIMESTAMP:
			if (max.v_ts.sec < src_row->cols[col_id].mval->v_ts.sec) {
				max.v_ts = src_row->cols[col_id].mval->v_ts;
			} else if (max.v_ts.sec == src_row->cols[col_id].mval->v_ts.sec) {
				if (max.v_ts.usec < src_row->cols[col_id].mval->v_ts.usec) {
					max.v_ts = src_row->cols[col_id].mval->v_ts;
				}
			}
			break;
		default:
			return EINVAL;
		}
		src_row = TAILQ_NEXT(src_row, entry);
	}
	switch (dst_col->type) {
	case LDMS_V_U8:
		dst_col->mval->v_u8 = max.v_u8;
		break;
	case LDMS_V_S8:
		dst_col->mval->v_s8 = max.v_s8;
		break;
	case LDMS_V_U16:
		dst_col->mval->v_u16 = max.v_u16;
		break;
	case LDMS_V_S16:
		dst_col->mval->v_s16 = max.v_s16;
		break;
	case LDMS_V_U32:
		dst_col->mval->v_u32 = max.v_u32;
		break;
	case LDMS_V_S32:
		dst_col->mval->v_s32 = max.v_s32;
		break;
	case LDMS_V_U64:
		dst_col->mval->v_u64 = max.v_u64;
		break;
	case LDMS_V_S64:
		dst_col->mval->v_s64 = max.v_s64;
		break;
	case LDMS_V_F32:
		dst_col->mval->v_f = max.v_f;
		break;
	case LDMS_V_D64:
		dst_col->mval->v_d = max.v_d;
		break;
	case LDMS_V_TIMESTAMP:
		dst_col->mval->v_ts = max.v_ts;
		break;
	default:
		return EINVAL;
	}
	return 0;
}

static int min_op(ldmsd_row_list_t row_list, ldmsd_row_t dest_row, int col_id)
{
	ldmsd_row_t src_row = TAILQ_FIRST(row_list);
	ldmsd_col_t dst_col = &dest_row->cols[col_id];
	union ldms_value min;

	assign_value(&min, src_row->cols[col_id].mval, dst_col->type, dst_col->array_len);
	src_row = TAILQ_NEXT(src_row, entry);
	while (src_row)
	{
		switch (dst_col->type) {
		case LDMS_V_U8:
			if (min.v_u8 > src_row->cols[col_id].mval->v_u8)
				min.v_u8 = src_row->cols[col_id].mval->v_u8;
			break;
		case LDMS_V_S8:
			if (min.v_s8 > src_row->cols[col_id].mval->v_s8)
				min.v_s8 = src_row->cols[col_id].mval->v_s8;
			break;
		case LDMS_V_U16:
			if (min.v_u16 > src_row->cols[col_id].mval->v_u16)
				min.v_u16 = src_row->cols[col_id].mval->v_u16;
			break;
		case LDMS_V_S16:
			if (min.v_s16 > src_row->cols[col_id].mval->v_s16)
				min.v_s16 = src_row->cols[col_id].mval->v_s16;
			break;
		case LDMS_V_U32:
			if (min.v_u32 > src_row->cols[col_id].mval->v_u32)
				min.v_u32 = src_row->cols[col_id].mval->v_u32;
			break;
		case LDMS_V_S32:
			if (min.v_s32 > src_row->cols[col_id].mval->v_s32)
				min.v_s32 = src_row->cols[col_id].mval->v_s32;
			break;
		case LDMS_V_U64:
			if (min.v_u64 > src_row->cols[col_id].mval->v_u64)
				min.v_u64 = src_row->cols[col_id].mval->v_u64;
			break;
		case LDMS_V_S64:
			if (min.v_s64 > src_row->cols[col_id].mval->v_s64)
				min.v_s64 = src_row->cols[col_id].mval->v_s64;
			break;
		case LDMS_V_F32:
			if (min.v_f > src_row->cols[col_id].mval->v_f)
				min.v_f = src_row->cols[col_id].mval->v_f;
			break;
		case LDMS_V_D64:
			if (min.v_d > src_row->cols[col_id].mval->v_d)
				min.v_d = src_row->cols[col_id].mval->v_d;
			break;
		case LDMS_V_TIMESTAMP:
			if (min.v_ts.sec > src_row->cols[col_id].mval->v_ts.sec) {
				min.v_ts = src_row->cols[col_id].mval->v_ts;
			} else if (min.v_ts.sec == src_row->cols[col_id].mval->v_ts.sec) {
				if (min.v_ts.usec > src_row->cols[col_id].mval->v_ts.usec) {
					min.v_ts = src_row->cols[col_id].mval->v_ts;
				}
			}
			break;
		default:
			return EINVAL;
		}
		src_row = TAILQ_NEXT(src_row, entry);
	}
	switch (dst_col->type) {
	case LDMS_V_U8:
		dst_col->mval->v_u8 = min.v_u8;
		break;
	case LDMS_V_S8:
		dst_col->mval->v_s8 = min.v_s8;
		break;
	case LDMS_V_U16:
		dst_col->mval->v_u16 = min.v_u16;
		break;
	case LDMS_V_S16:
		dst_col->mval->v_s16 = min.v_s16;
		break;
	case LDMS_V_U32:
		dst_col->mval->v_u32 = min.v_u32;
		break;
	case LDMS_V_S32:
		dst_col->mval->v_s32 = min.v_s32;
		break;
	case LDMS_V_U64:
		dst_col->mval->v_u64 = min.v_u64;
		break;
	case LDMS_V_S64:
		dst_col->mval->v_s64 = min.v_s64;
		break;
	case LDMS_V_F32:
		dst_col->mval->v_f = min.v_f;
		break;
	case LDMS_V_D64:
		dst_col->mval->v_d = min.v_d;
		break;
	case LDMS_V_TIMESTAMP:
		dst_col->mval->v_ts = min.v_ts;
		break;
	default:
		return EINVAL;
	}
	return 0;
}

static ldmsd_functional_op_t op_table[] = {
	[LDMSD_DECOMP_OP_NONE] = none_op,
	[LDMSD_DECOMP_OP_DIFF] = diff_op,
	[LDMSD_DECOMP_OP_MEAN] = mean_op,
	[LDMSD_DECOMP_OP_MIN] = min_op,
	[LDMSD_DECOMP_OP_MAX] = max_op,
};

static int decomp_static_decompose(ldmsd_strgp_t strgp, ldms_set_t set,
				   ldmsd_row_list_t row_list, int *row_count)
{
	decomp_static_cfg_t dcfg = (void*)strgp->decomp;
	decomp_static_row_cfg_t cfg_row;
	decomp_static_col_cfg_t cfg_col;
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
	decomp_static_mid_rbn_t mid_rbn = NULL;
	ldms_digest_t ldms_digest;
	TAILQ_HEAD(, _list_entry) list_cols;
	int row_more_le;
	struct ldms_timestamp ts;
	const char *producer;
	const char *instance;
	int producer_len, instance_len;

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
		cfg_row = &dcfg->rows[i];

		/* At the time configuration is performed we have not yet
		 * seen the metric set, therefore we don't know the metric-id
		 * for the metrics specified in the configuration. The logic
		 * here is to look up a metric by name and store the metric-id
		 * in the col_cfg for the schema. */

		/* Check if we have already resolved the metric-id for this
		 * schema. The schema is identified by the LDMS schema digest.
		 */
		mid_rbn = (void*)rbt_find(&cfg_row->mid_rbt, ldms_digest);
		if (!mid_rbn) {
			/* Resove the metric data for this new schema */
			/* Resolving `src` -> metric ID */
			mid_rbn = calloc(1, sizeof(*mid_rbn) +
					cfg_row->col_count * sizeof(mid_rbn->col_mids[0]));
			if (!mid_rbn) {
				rc = ENOMEM;
				goto err_0;
			}
			rc = resolve_metrics(strgp, mid_rbn, cfg_row, set);
			if (rc)
				goto err_0;
			memcpy(&mid_rbn->ldms_digest, ldms_digest, sizeof(*ldms_digest));
			rbn_init(&mid_rbn->rbn, &mid_rbn->ldms_digest);
			rbt_ins(&cfg_row->mid_rbt, &mid_rbn->rbn);
		}

		/*
		 * col_mvals is a scratch memory to create rows from
		 * a set with records. col_mvals is freed at the end of
		 * `make_row`.
		 */
		col_mvals = calloc(1, cfg_row->col_count * sizeof(*col_mvals));
		if (!col_mvals) {
			rc = ENOMEM;
			goto err_0;
		}
		for (j = 0; j < cfg_row->col_count; j++) {
			mid = mid_rbn->col_mids[j].mid;
			mcol = &col_mvals[j];
			assert(mid >= 0);
			mcol->metric_id = mid;
			mcol->rec_metric_id = -1;
			mcol->rec_array_idx = -1;
			mcol->rec_array_len = -1;
			switch (mid) {
			case LDMSD_PHONY_METRIC_ID_TIMESTAMP:
				/* mcol->mval will be assigned in `make_row` */
				mcol->mtype = LDMS_V_TIMESTAMP;
				continue;
			case LDMSD_PHONY_METRIC_ID_PRODUCER:
				mcol->mval = (ldms_mval_t)producer;
				/* mcol->mval->a_char is producer */
				mcol->mtype = LDMS_V_CHAR_ARRAY;
				mcol->array_len = producer_len;
				continue;
			case LDMSD_PHONY_METRIC_ID_INSTANCE:
				mcol->mval = (ldms_mval_t)instance;
				/* mcol->mval->a_char is instance */
				mcol->mtype = LDMS_V_CHAR_ARRAY;
				mcol->array_len = instance_len;
				continue;
			case LDMSD_PHONY_METRIC_ID_FILL:
				mcol->mval = (ldms_mval_t)instance;
				/* mcol->mval->a_char is instance */
				mcol->mtype = cfg_row->cols[j].type;
				mcol->array_len = cfg_row->cols[j].array_len;
				mcol->mval = cfg_row->cols[j].fill;
				continue;
			}
			mval = ldms_metric_get(set, mid);
			mtype = ldms_metric_type_get(set, mid);
			if (mtype != mid_rbn->col_mids[j].mtype) {
                                ldmsd_log(LDMSD_LERROR, "strgp '%s': the metric type (%s) of "
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
			mlen = ldms_metric_array_get_len(set, mid);
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
				rec_mid = ldms_record_metric_find(le, cfg_row->cols[j].rec_member);
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
			mcol->mval = cfg_row->cols[j].fill;
			mcol->mtype = cfg_row->cols[j].type;
			mcol->array_len = cfg_row->cols[j].fill_len;
		}

	make_row: /* make/expand rows according to col_mvals */
		row = calloc(1, cfg_row->row_sz + cfg_row->mval_size);
		if (!row) {
			rc = errno;
			goto err_0;
		}
		row->schema_name = cfg_row->schema_name;
		row->schema_digest = &cfg_row->schema_digest;
		row->idx_count = cfg_row->idx_count;
		row->col_count = cfg_row->col_count;

		/* indices */
		row->indices = (void*)&row->cols[row->col_count];
		idx = (void*)&row->indices[row->idx_count];
		for (j = 0; j < row->idx_count; j++) {
			row->indices[j] = idx;
			idx->col_count = cfg_row->idxs[j].col_count;
			idx->name = cfg_row->idxs[j].name;
			for (k = 0; k < idx->col_count; k++) {
				c = cfg_row->idxs[j].col_idx[k];
				idx->cols[k] = &row->cols[c];
			}
			idx = (void*)&idx->cols[idx->col_count];
		}
		row->mvals = (uint8_t *)idx;

		row_more_le = 0;
		/* cols */
		for (j = 0; j < row->col_count; j++) {
			col = &row->cols[j];
			col->mval = (ldms_mval_t)&row->mvals[mid_rbn->col_mids[j].mval_offset];
			cfg_col = &cfg_row->cols[j];
			mcol = &col_mvals[j];

			col->metric_id = mcol->metric_id;
			col->rec_metric_id = mcol->rec_metric_id;
			col->name = cfg_col->dst;
			col->type = mcol->mtype;
			col->array_len = mcol->array_len;

			switch (mid_rbn->col_mids[j].mid) {
			case LDMSD_PHONY_METRIC_ID_TIMESTAMP:
				col->mval->v_ts = ts;
				continue;
			case LDMSD_PHONY_METRIC_ID_PRODUCER:
				col->mval = (ldms_mval_t)producer;
				col->array_len = producer_len;
				continue;
			case LDMSD_PHONY_METRIC_ID_INSTANCE:
				col->mval = (ldms_mval_t)instance;
				col->array_len = instance_len;
				continue;
			case LDMSD_PHONY_METRIC_ID_FILL:
				col->mval = cfg_col->fill;
				col->array_len = cfg_col->fill_len;
				continue;
			default:
				assign_value(col->mval, mcol->mval, mcol->mtype, mcol->array_len);
				break;
			}

			if (mcol->rec_array_idx >= 0)	/* Array of records */
				goto col_rec_array;

			if (!mcol->le)			/* No more list elements */
				continue;

			/* list */
			/* step to next element in the list */
			mcol->le = ldms_list_next(set, mcol->le, &mcol->mtype, &mcol->array_len);
			if (!mcol->le)
				/* Fill remaining list elements with default */
				goto col_fill;

			row_more_le = 1;
			if (cfg_row->cols[j].rec_member) {
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
			mcol->mval = cfg_row->cols[j].fill;
			mcol->array_len = row->cols[j].array_len;
			mcol->mtype = cfg_row->cols[j].type;
		}

		if (cfg_row->op_present) {
			ldmsd_row_cache_idx_t group_idx;
			ldmsd_row_cache_idx_t row_idx;
			ldmsd_row_cache_key_t *keys;
			ldmsd_row_t dup_row;

			/* Build the group key */
			keys = calloc(cfg_row->group_count, sizeof(*keys));
			for (j = 0; j < cfg_row->group_count; j++) {
				keys[j] = ldmsd_row_cache_key_create(
						row->cols[cfg_row->group_cols[j]].type,
						row->cols[cfg_row->group_cols[j]].array_len);
				memcpy(keys[j]->mval,
					row->cols[cfg_row->group_cols[j]].mval,
					keys[j]->mval_size);
			}
			group_idx = ldmsd_row_cache_idx_create(cfg_row->group_count, keys);

			/* Build the row-order key */
			keys = calloc(cfg_row->row_order_count, sizeof(*keys));
			row_idx = ldmsd_row_cache_idx_create(cfg_row->row_order_count, keys);

			/* Initialize the metric types in the key values */
			for (j = 0; j < cfg_row->row_order_count; j++) {
				keys[j] = ldmsd_row_cache_key_create(
						row->cols[cfg_row->row_order_cols[j]].type,
						row->cols[cfg_row->row_order_cols[j]].array_len);
				memcpy(keys[j]->mval,
					row->cols[cfg_row->row_order_cols[j]].mval,
					keys[j]->mval_size);
			}

			/* Cache the current, unmodified row */
			rc = ldmsd_row_cache(strgp->row_cache, group_idx, row_idx, row);
			if (rc)
				goto err_0;

			/* We are about to modify the row. We can't modify the
			 * row we just cached or it will be useless for the next
			 * sample */
			dup_row = row_cache_dup(cfg_row, mid_rbn, row);

			/* Apply functional operators to columns */
			for (j = 0; j < row->col_count; j++) {
				struct ldmsd_row_list_s row_list;
				int count = ldmsd_row_cache_make_list(
						&row_list,
						cfg_row->row_limit,
						strgp->row_cache,
						group_idx);
				if (count != cfg_row->group_count)
					ldmsd_log(LDMSD_LWARNING,
						"strgp '%s': insufficient rows in "
						"cache to satisfy functional operator '%s' "
						"on column '%s'.\n",
                                                strgp->obj.name,
                                                ldmsd_decomp_op_to_string(cfg_col->op),
						cfg_col->dst);
				cfg_col = &cfg_row->cols[j];
				rc = op_table[cfg_col->op](&row_list, dup_row, j);
			}
			ldmsd_row_cache_idx_free(group_idx);
			row = dup_row;
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
	free(col_mvals);
	free(mid_rbn);
	decomp_static_release_rows(strgp, row_list);
	return rc;
}

static void decomp_static_release_rows(ldmsd_strgp_t strgp,
					ldmsd_row_list_t row_list)
{
	ldmsd_row_t row;
	while ((row = TAILQ_FIRST(row_list))) {
		TAILQ_REMOVE(row_list, row, entry);
		free(row);
	}
}
