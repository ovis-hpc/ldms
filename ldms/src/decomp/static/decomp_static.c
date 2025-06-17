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

static ovis_log_t static_log;
/* convenient macro to put error message in both ldmsd log and `reqc` */
#define THISLOG(reqc, rc, fmt, ...) do { \
	ovis_log(static_log, OVIS_LERROR, fmt, ##__VA_ARGS__); \
	if (reqc) { \
		(reqc)->errcode = rc; \
		Snprintf(&(reqc)->line_buf, &(reqc)->line_len, fmt, ##__VA_ARGS__); \
	} \
} while (0)


static ldmsd_decomp_t decomp_static_config(ldmsd_strgp_t strgp,
			json_t *cfg, ldmsd_req_ctxt_t reqc);
static int decomp_static_decompose(ldmsd_strgp_t strgp, ldms_set_t set,
				     ldmsd_row_list_t row_list, int *row_count,
				     void **decomp_ctxt);
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
	static_log = ovis_log_register("store.decomp.static", "Messages for the static decomposition");
	if (!static_log) {
		ovis_log(NULL, OVIS_LWARN,
			"Failed to create decomp_static's "
			"log subsystem. Error %d.\n", errno);
	}
	return &decomp_static;
}

static void assign_value(ldmsd_col_t dst, ldmsd_col_t src);

static int
mval_from_json(ldms_mval_t *v,
	       enum ldms_value_type mtype, int *mlen, json_t *jent,
	       char *err_buf, size_t err_sz)
{
	json_t *item;
	json_type type;
	int i, rc;
	size_t sz;
	struct ldmsd_col_s dst = {}, src = {};
	json_type jtype;
	/* dst is the output, src is the value from json */

	dst.type = mtype;
	jtype = json_typeof(jent);

	if ((mtype == LDMS_V_CHAR_ARRAY && jtype != JSON_STRING)) {
		snprintf(err_buf, err_sz, "string type must have string value");
		return EINVAL;
	}

	/* sanity check */
	switch (jtype) {
	case JSON_REAL:
	case JSON_INTEGER:
		if (ldms_type_is_array(mtype)) {
			snprintf(err_buf, err_sz, "array type must have array value");
			return EINVAL;
		}
		break;
	case JSON_ARRAY:
		if (!ldms_type_is_array(mtype)) {
			snprintf(err_buf, err_sz, "array value must have array type");
			return EINVAL;
		}
		break;
	case JSON_STRING:
		if (mtype != LDMS_V_CHAR && mtype != LDMS_V_CHAR_ARRAY) {
			snprintf(err_buf, err_sz,
				 "string value must have 'char' or 'char_array' type");
			return EINVAL;
		}
		break;
	default:
		snprintf(err_buf, err_sz, "Unsupported JSON value");
		return EINVAL;
	}

	/* Let's just coerce the value for other cases */

	/* resolve src (from json) */
	switch (jtype) {
	case JSON_REAL:
		src.type = LDMS_V_D64;
		src.mval = calloc(1, sizeof(src.mval->v_d));
		src.mval->v_d = json_real_value(jent);
		src.array_len = 1;
		break;
	case JSON_STRING:
		src.type = LDMS_V_CHAR_ARRAY;
		src.array_len = json_string_length(jent) + 1;
		src.mval = malloc(src.array_len);
		if (!src.mval)
			goto enomem;
		memcpy(src.mval->a_char, json_string_value(jent), src.array_len);
		break;
	case JSON_INTEGER:
		src.type = LDMS_V_S64;
		src.mval = calloc(1, sizeof(src.mval->v_s64));
		src.mval->v_s64 = json_integer_value(jent);
		src.array_len = 1;
		break;
	case JSON_ARRAY:
		src.array_len = json_array_size(jent);
		item = json_array_get(jent, 0);
		if (!item) {
			rc = ENOENT;
			snprintf(err_buf, err_sz, "Empty array");
			goto err;
		}
		type = json_typeof(item);
		switch (type) {
		case JSON_INTEGER:
			src.type = LDMS_V_S64_ARRAY;
			src.mval = calloc(src.array_len, sizeof(src.mval->v_s64));
			if (!src.mval)
				goto enomem;
			break;
		case JSON_REAL:
			src.type = LDMS_V_D64_ARRAY;
			src.mval = calloc(src.array_len, sizeof(src.mval->v_d));
			if (!src.mval)
				goto enomem;
			break;
		case JSON_OBJECT:
		case JSON_ARRAY:
		case JSON_STRING:
		case JSON_NULL:
		case JSON_TRUE:
		case JSON_FALSE:
		default:
			rc = EINVAL;
			snprintf(err_buf, err_sz, "Unsupported JSON array type");
			goto err;
		}
		json_array_foreach(jent, i, item) {
			if (type == JSON_INTEGER)
				ldms_mval_array_set_s64(src.mval, i, json_integer_value(item));
			else
				ldms_mval_array_set_double(src.mval, i, json_real_value(item));
		}
		break;
	default:
		rc = EINVAL;
		snprintf(err_buf, err_sz, "Unsupported JSON type");
		goto err;
	}

	dst.type = mtype;
	dst.array_len = src.array_len;
	sz = ldms_metric_value_size_get(dst.type, dst.array_len);
	dst.mval = malloc(sz);
	if (!dst.mval)
		goto enomem;

	assign_value(&dst, &src);
	*v = dst.mval;
	*mlen = dst.array_len;
	free(src.mval);

	return 0;
enomem:
	rc = ENOMEM;
	snprintf(err_buf, err_sz, "Not enough memory");
err:
	if (src.mval)
		free(src.mval);
	return rc;
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
	size_t mval_offset;
	size_t mval_size;
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
	char digest_str[LDMS_DIGEST_STR_LENGTH];
	int col_count;
	struct decomp_static_col_mid_s {
		int mid;
		int rec_mid;
		enum ldms_value_type mtype;
		size_t array_len;
		enum ldms_value_type rec_mtype;
		enum ldms_value_type le_mtype;
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
	json_t *jlimit, *jtimeout;
	int col_no;
	int rc;
	struct timespec *timeout, _timeout;

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

	jtimeout = json_object_get(jgroup, "timeout");
	if (jtimeout) {
		if (json_typeof(jtimeout) != JSON_STRING) {
			THISLOG(reqc, EINVAL, "strgp '%s': row '%d': "
				"group['timeout'] must be a STRING describing "
				"time (e.g. \"10s\").\n",
				strgp->obj.name, row_no);
			rc = ENOENT;
			goto err_0;

		}
		ldmsd_timespec_from_str(&_timeout, json_string_value(jtimeout));
		timeout = &_timeout;
	} else {
		timeout = NULL;
	}

	strgp->row_cache = ldmsd_row_cache_create(strgp, cfg_row->row_limit, timeout);
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
	json_t *jrow, *jcol, *jfill, *jop, *jtype;
	char *lb, *rb;
	decomp_static_row_cfg_t cfg_row;
	decomp_static_col_cfg_t cfg_col;
	decomp_static_cfg_t dcfg = NULL;
	int row_no, col_no, rc;
	char err_buf[256];

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

			cfg_col->fill_len = -1;

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

			/* record member encoded in the src */
			lb = strchr(cfg_col->src, '[');
			rb = strchr(cfg_col->src, ']');
			if (lb && rb) {
				rb[0] = 0;
				cfg_col->rec_member = strdup(lb+1);
				if (!cfg_col->rec_member)
					goto enomem;
				lb[0] = 0;
			} else if (lb || rb) {
				/* incomplete bracket */
				THISLOG(reqc, EINVAL,
					"strgp '%s': row '%d'--col '%d': "
					"incomplete brackets in column['src'].\n",
					strgp->obj.name, row_no, col_no);
				goto err_0;
			}

			jrec_member = json_object_get(jcol, "rec_member");
			if (jrec_member) {
				if (!json_is_string(jrec_member)) {
					THISLOG(reqc, EINVAL, "strgp '%s': row '%d'--col '%d': "
						"rec_member must be a string.\n",
						strgp->obj.name, row_no, col_no);
					goto err_0;
				}
				if (cfg_col->rec_member) {
					THISLOG(reqc, EINVAL,
						"Conflicting configuration: both"
						" 'rec_member' and '->' (in src)"
						" are specified.");
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
			jtype = json_object_get(jcol, "type");
			if (jtype) {
				if (jtype->type != JSON_STRING) {
					THISLOG(reqc, EINVAL, "strgp '%s': row '%d'--col %d : "
						"bad 'type'\n",
						strgp->obj.name, row_no, col_no);
					goto err_0;
				}
				cfg_col->type = ldms_metric_str_to_type(json_string_value(jtype));
			} else {
				cfg_col->type = LDMS_V_NONE;
			}
			jfill = json_object_get(jcol, "fill");
			if (jfill) {
				if (!jtype) {
					THISLOG(reqc, EINVAL,
						"strgp '%s': row '%d': col[dst] '%s',"
						"'fill' error, 'type' is required.\n",
						strgp->obj.name, row_no, cfg_col->dst);
					goto err_0;
				}
				/* The remaining code handles 'fill' */
				rc = mval_from_json(&cfg_col->fill, cfg_col->type,
						&cfg_col->fill_len, jfill,
						err_buf, sizeof(err_buf));
				if (rc) {
					THISLOG(reqc, rc,
						"strgp '%s': row '%d': col[dst] '%s',"
						"'fill' error %d: %s.\n",
						strgp->obj.name, row_no,
						cfg_col->dst, rc, err_buf);
					goto err_0;
				}
				cfg_col->array_len = cfg_col->fill_len;
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
		} else if (cfg_row->op_present) {
			THISLOG(reqc, EINVAL,
				"%s: row: %d, The 'group' clause is required if "
				"functional operators are in use.\n",
				strgp->obj.name, row_no);
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

#define ASSERT_RETURN(COND) do { \
		assert(COND); \
		if (!(COND)) \
			return EINVAL; \
	} while (0)

struct resolve_ctxt_s {
	ldmsd_strgp_t strgp;
	ldms_set_t set;
	EVP_MD_CTX *evp_ctx;
	int col_no;
	size_t mval_offset;
};

static int resolve_col(struct resolve_ctxt_s *ctxt,
		       struct decomp_static_col_mid_s *col_mid,
		       decomp_static_col_cfg_t cfg_col)
{
	int rc = EINVAL;
	const char *src;
	enum ldms_value_type mtype, le_mtype, rec_mtype;
	ldms_mval_t lh, le, rec_array, rec;
	int special;
	int mid, rec_mid;
	size_t mlen;

	mid = -1;
	rec_mid = -1;
	mlen = -1;
	mtype = LDMS_V_NONE;
	rec_mtype = LDMS_V_NONE;
	le_mtype = LDMS_V_NONE;

	col_mid->mtype = LDMS_V_NONE;
	col_mid->mid = -1;
	col_mid->rec_mid = -1;
	col_mid->array_len = -1;
	col_mid->rec_mid = -EINVAL;
	col_mid->rec_mtype = LDMS_V_NONE;
	col_mid->le_mtype = LDMS_V_NONE;

	src = cfg_col->src;

	/* resolve phony metric names first */
	special = 1;
	mid = ldmsd_phony_metric_resolve(src);
	switch (mid) {
	case LDMSD_PHONY_METRIC_ID_TIMESTAMP:
	case LDMSD_PHONY_METRIC_ID_DURATION:
		assert( cfg_col->type == LDMS_V_NONE ||
			cfg_col->type == LDMS_V_TIMESTAMP );
		mtype = LDMS_V_TIMESTAMP;
		mlen = 1;
		goto commit;
	case LDMSD_PHONY_METRIC_ID_UID:
	case LDMSD_PHONY_METRIC_ID_GID:
	case LDMSD_PHONY_METRIC_ID_PERM:
	case LDMSD_PHONY_METRIC_ID_CARD:
		assert( cfg_col->type == LDMS_V_NONE ||
			cfg_col->type == LDMS_V_U32 );
		mtype = LDMS_V_U32;
		mlen = 1;
		goto commit;
	case LDMSD_PHONY_METRIC_ID_DIGEST:
	case LDMSD_PHONY_METRIC_ID_SCHEMA:
	case LDMSD_PHONY_METRIC_ID_PRODUCER:
	case LDMSD_PHONY_METRIC_ID_INSTANCE:
		assert( cfg_col->type == LDMS_V_NONE ||
			cfg_col->type == LDMS_V_CHAR_ARRAY );
		col_mid->mid = mid;
		cfg_col->type = LDMS_V_CHAR_ARRAY;
		/* Doesn't consume mval space, data comes from
		 * instance name in set meta-data */
		goto next;
	case LDMSD_PHONY_METRIC_ID_UNKNOWN:
		mid = -1;
		special = 0;
		/* no-op */
		break;
	}

	/* Not a phony metric name; try getting the metric from the set. */

	mid = ldms_metric_by_name(ctxt->set, src);
	if (mid < 0) {
		/* This is a fill candidate, must have a type and if
		 * an array a length */
		if (!cfg_col->fill) {
			ovis_log(static_log, OVIS_LERROR,
				"strgp '%s': col[src] '%s' does not exist in "
				"the set '%s' and the 'fill' is not specified.\n",
				ctxt->strgp->obj.name, cfg_col->src,
				ldms_set_name_get(ctxt->set));
			rc = EINVAL;
			goto err;
		}
		if (ldms_type_is_array(cfg_col->type) && cfg_col->fill_len < 0) {
			ovis_log(static_log, OVIS_LERROR,
				"strgp '%s': col[dst] '%s' array must have a "
				"len specified if it does not exist in the set.\n",
				ctxt->strgp->obj.name, cfg_col->dst);
			rc = EINVAL;
			goto err;
		}
		/* use fill */
		mid = LDMSD_PHONY_METRIC_ID_FILL;
		mtype  = cfg_col->type;
		mlen = cfg_col->fill_len;
		goto commit;
	}

	mtype = ldms_metric_type_get(ctxt->set, mid);
	mlen = ldms_metric_array_get_len(ctxt->set, mid);
	ASSERT_RETURN((!ldms_type_is_array(mtype)) || mlen);

	if (LDMS_V_NONE < mtype && mtype <= LDMS_V_D64_ARRAY) {
		/* Primitives & array of primitives. We have mlen, mtype and
		 * everything figured out */
		goto commit;
	}

	if (mtype == LDMS_V_LIST)
		goto list_routine;
	if (mtype == LDMS_V_RECORD_ARRAY)
		goto rec_array_routine;

	/* Invalid type */
	ovis_log(static_log, OVIS_LERROR,
		"strgp '%s': col[src] '%s' the metric type %s(%d) "
		"is not supported.\n",
		ctxt->strgp->obj.name, cfg_col->src,
		ldms_metric_type_to_str(mtype), mtype);
	rc = EINVAL;
	goto err;

list_routine:
	/* handling LIST */
	lh = ldms_metric_get(ctxt->set, mid);
	le = ldms_list_first(ctxt->set, lh, &le_mtype, &mlen);
	ASSERT_RETURN((!ldms_type_is_array(le_mtype)) || mlen);
	if (!le) {
		/* list empty. can't init yet */
		ovis_log(static_log, OVIS_LDEBUG,
			"strgp '%s': row '%d': col[dst] '%s' "
			"LIST is empty, skipping set metric resolution.\n",
			ctxt->strgp->obj.name, ctxt->col_no, cfg_col->dst
			);
		/* The caller resolve_metrics() treates ENOEXEC as a non-error. */
		rc = ENOEXEC;
		goto err;
	}
	if (le_mtype == LDMS_V_LIST) {
		/* LIST of LIST is not supported */
		/* Invalid type */
		ovis_log(static_log, OVIS_LERROR,
			"strgp '%s': row '%d': col[dst] '%s' "
			"LIST of LIST is not supported.\n",
			ctxt->strgp->obj.name, ctxt->col_no, cfg_col->dst
			);
		rc = EINVAL;
		goto err;
	}
	if (!cfg_col->rec_member) {
		/* LIST of non-record elements */
		goto commit;
	}
	/* LIST of records */
	rec_mid = ldms_record_metric_find(le, cfg_col->rec_member);
	if (rec_mid < 0) {
		ovis_log(static_log, OVIS_LERROR,
			"strgp '%s': row '%d': col[dst] '%s' "
			"Record member '%s' not found.\n",
			ctxt->strgp->obj.name, ctxt->col_no, cfg_col->dst,
			cfg_col->rec_member);
		rc = ENOENT;
		goto err;
	}
	rec_mtype = ldms_record_metric_type_get(le, rec_mid, &mlen);
	ASSERT_RETURN((!ldms_type_is_array(rec_mtype)) || mlen);
	goto commit;

rec_array_routine:
	rec_array = ldms_metric_get(ctxt->set, mid);
	rec = ldms_record_array_get_inst(rec_array, 0);
	if (!cfg_col->rec_member) {
		ovis_log(static_log, OVIS_LERROR,
			"strgp '%s': row '%d': the record array '%s' "
			"is empty.\n",
			ctxt->strgp->obj.name, ctxt->col_no, cfg_col->src);
		rc = EINVAL;
		goto err;
	}
	rec_mid = ldms_record_metric_find(rec, cfg_col->rec_member);
	if (rec_mid < 0) {
		ovis_log(static_log, OVIS_LERROR,
			"strgp '%s': row '%d': col[dst] '%s' "
			"Missing record member definition.n",
			ctxt->strgp->obj.name, ctxt->col_no, cfg_col->dst);
		rc = EINVAL;
		goto err;
	}
	rec_mtype = ldms_record_metric_type_get(rec, rec_mid, &mlen);
	ASSERT_RETURN((!ldms_type_is_array(rec_mtype)) || mlen);
	/* fall through to commit */

commit:
	/* commit to col_mid */
	col_mid->mid = mid;
	col_mid->mtype = mtype;
	col_mid->array_len = mlen;
	col_mid->le_mtype = le_mtype;
	if (rec_mid >= 0) {
		col_mid->rec_mid = rec_mid;
		col_mid->rec_mtype = rec_mtype;
		ASSERT_RETURN((!ldms_type_is_array(rec_mtype)) || mlen);
	} else {
	}

	/* commit to cfg_col */

	ASSERT_RETURN(mtype != LDMS_V_NONE);
	if (cfg_col->type == LDMS_V_NONE) {
		if (rec_mid >= 0)
			cfg_col->type = rec_mtype;
		else if (mtype == LDMS_V_LIST)
			cfg_col->type = le_mtype;
		else
			cfg_col->type = mtype;
	}
	if (0 == cfg_col->array_len) {
		cfg_col->array_len = mlen;
		ASSERT_RETURN((!ldms_type_is_array(cfg_col->type)) ||
				cfg_col->array_len);
	}
	if (0 == cfg_col->mval_size) {
		cfg_col->mval_offset = ctxt->mval_offset;
		cfg_col->mval_size = ldms_metric_value_size_get(cfg_col->type, mlen);
	} else {
		ASSERT_RETURN(cfg_col->type == mtype);
		ASSERT_RETURN(cfg_col->mval_offset == ctxt->mval_offset);
	}

	/* update mval_offset */
	ctxt->mval_offset += LDMS_ROUNDUP(cfg_col->mval_size, sizeof(uint64_t));

next:

	ASSERT_RETURN(special || cfg_col->mval_size);
	/* update row schema digest */
	EVP_DigestUpdate(ctxt->evp_ctx, cfg_col->dst, strlen(cfg_col->dst));
	EVP_DigestUpdate(ctxt->evp_ctx, &cfg_col->type, sizeof(cfg_col->type));
	rc = 0;
err:
	return rc;
}

/*
 * ENOEXEC is used to indicate that the plugin cannot parse
 * the input and the further logic shouldn't be executed. However, it is not an error.
 *
 * resolve_metrics() treats ENOEXEC as a non-error return code.
 */
static int resolve_metrics(ldmsd_strgp_t strgp,
			decomp_static_mid_rbn_t mid_rbn,
			decomp_static_row_cfg_t cfg_row,
			ldms_set_t set)
{
	EVP_MD_CTX *evp_ctx = NULL;
	int col_no;
	decomp_static_col_cfg_t cfg_col;
	struct decomp_static_col_mid_s *col_mid;
	int rc;

	struct resolve_ctxt_s ctxt = {
		.strgp = strgp,
		.set = set,
		.mval_offset = 0,
	};

	evp_ctx = EVP_MD_CTX_create();
	if (!evp_ctx) {
		ovis_log(static_log, OVIS_LERROR, "out of memory\n");
		rc = errno;
		goto err;
	}
	EVP_DigestInit_ex(evp_ctx, EVP_sha256(), NULL);
	ctxt.evp_ctx = evp_ctx;
	mid_rbn->col_count = cfg_row->col_count;
	for (col_no = 0; col_no < mid_rbn->col_count; col_no++) {
		col_mid = &mid_rbn->col_mids[col_no];
		cfg_col = &cfg_row->cols[col_no];
		ctxt.col_no = col_no;
		rc = resolve_col(&ctxt, col_mid, cfg_col);
		/* Treat ENOEXEC as a non-error */
		if (rc)
			goto err;
	}
	cfg_row->mval_size = ctxt.mval_offset;
	/* Finalize row schema digest */
	unsigned int len = LDMS_DIGEST_LENGTH;
	EVP_DigestFinal(evp_ctx, cfg_row->schema_digest.digest, &len);
	EVP_MD_CTX_destroy(evp_ctx);
	return 0;
err:
	EVP_MD_CTX_destroy(evp_ctx);
	return rc;
}

static ldmsd_row_t
row_cache_dup(decomp_static_row_cfg_t cfg_row,
		decomp_static_mid_rbn_t mid_rbn,
		ldmsd_row_t row)
{
	int i, j;
	ldmsd_row_index_t idx;
	ldmsd_row_t dup_row = NULL;
	decomp_static_col_cfg_t cfg_col;

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
		cfg_col = &cfg_row->cols[i];
		dup_row->cols[i].name = row->cols[i].name;
		dup_row->cols[i].column = row->cols[i].column;
		dup_row->cols[i].type = row->cols[i].type;
		dup_row->cols[i].array_len = row->cols[i].array_len;
		dup_row->cols[i].metric_id = row->cols[i].metric_id;
		dup_row->cols[i].rec_metric_id = row->cols[i].rec_metric_id;
		switch (dup_row->cols[i].metric_id) {
		case LDMSD_PHONY_METRIC_ID_PRODUCER:
		case LDMSD_PHONY_METRIC_ID_INSTANCE:
		case LDMSD_PHONY_METRIC_ID_SCHEMA:
		case LDMSD_PHONY_METRIC_ID_DIGEST:
		case LDMSD_PHONY_METRIC_ID_FILL:
			dup_row->cols[i].mval = row->cols[i].mval;
			break;
		case LDMSD_PHONY_METRIC_ID_TIMESTAMP:
		default:
			dup_row->cols[i].mval = (ldms_mval_t)&dup_row->mvals[cfg_col->mval_offset];
		}
	}
 out:
	return dup_row;
}

static void assign_value(ldmsd_col_t dst, ldmsd_col_t src)
{
	size_t sz;
	int i, len;
	if (ldms_type_is_array(dst->type) && ldms_type_is_array(src->type)) {
		len = (dst->array_len < src->array_len)?dst->array_len:src->array_len;
	} else {
		len = 1;
	}
	if (dst->type == src->type) {
		sz = ldms_metric_value_size_get(dst->type, len);
		memcpy(dst->mval, src->mval, sz);
		return;
	}
	switch (dst->type) {
	case LDMS_V_CHAR:
		dst->mval->v_char = ldms_mval_as_char(src->mval, src->type, 0);
		break;
	case LDMS_V_U8:
		dst->mval->v_u8 = ldms_mval_as_u8(src->mval, src->type, 0);
		break;
	case LDMS_V_S8:
		dst->mval->v_s8 = ldms_mval_as_s8(src->mval, src->type, 0);
		break;
	case LDMS_V_U16:
		dst->mval->v_u16 = ldms_mval_as_u16(src->mval, src->type, 0);
		break;
	case LDMS_V_S16:
		dst->mval->v_s16 = ldms_mval_as_s16(src->mval, src->type, 0);
		break;
	case LDMS_V_U32:
		dst->mval->v_u32 = ldms_mval_as_u32(src->mval, src->type, 0);
		break;
	case LDMS_V_S32:
		dst->mval->v_s32 = ldms_mval_as_s32(src->mval, src->type, 0);
		break;
	case LDMS_V_U64:
		dst->mval->v_u64 = ldms_mval_as_u64(src->mval, src->type, 0);
		break;
	case LDMS_V_S64:
		dst->mval->v_s64 = ldms_mval_as_s64(src->mval, src->type, 0);
		break;
	case LDMS_V_F32:
		dst->mval->v_f = ldms_mval_as_float(src->mval, src->type, 0);
		break;
	case LDMS_V_D64:
		dst->mval->v_d = ldms_mval_as_double(src->mval, src->type, 0);
		break;
	case LDMS_V_CHAR_ARRAY:
		for (i = 0; i < len; i++) {
			dst->mval->a_char[i] = ldms_mval_as_char(src->mval, src->type, i);
		}
		break;
	case LDMS_V_U8_ARRAY:
		for (i = 0; i < len; i++) {
			dst->mval->a_u8[i] = ldms_mval_as_u8(src->mval, src->type, i);
		}
		break;
	case LDMS_V_S8_ARRAY:
		for (i = 0; i < len; i++) {
			dst->mval->a_s8[i] = ldms_mval_as_s8(src->mval, src->type, i);
		}
		break;
	case LDMS_V_U16_ARRAY:
		for (i = 0; i < len; i++) {
			dst->mval->a_u16[i] = ldms_mval_as_u16(src->mval, src->type, i);
		}
		break;
	case LDMS_V_S16_ARRAY:
		for (i = 0; i < len; i++) {
			dst->mval->a_s16[i] = ldms_mval_as_s16(src->mval, src->type, i);
		}
		break;
	case LDMS_V_U32_ARRAY:
		for (i = 0; i < len; i++) {
			dst->mval->a_u32[i] = ldms_mval_as_u32(src->mval, src->type, i);
		}
		break;
	case LDMS_V_S32_ARRAY:
		for (i = 0; i < len; i++) {
			dst->mval->a_s32[i] = ldms_mval_as_s32(src->mval, src->type, i);
		}
		break;
	case LDMS_V_U64_ARRAY:
		for (i = 0; i < len; i++) {
			dst->mval->a_u64[i] = ldms_mval_as_u64(src->mval, src->type, i);
		}
		break;
	case LDMS_V_S64_ARRAY:
		for (i = 0; i < len; i++) {
			dst->mval->a_s64[i] = ldms_mval_as_s64(src->mval, src->type, i);
		}
		break;
	case LDMS_V_F32_ARRAY:
		for (i = 0; i < len; i++) {
			dst->mval->a_f[i] = ldms_mval_as_float(src->mval, src->type, i);
		}
		break;
	case LDMS_V_D64_ARRAY:
		for (i = 0; i < len; i++) {
			dst->mval->a_d[i] = ldms_mval_as_double(src->mval, src->type, i);
		}
		break;
	case LDMS_V_TIMESTAMP:
		dst->mval->v_ts = ldms_mval_as_timestamp(src->mval, src->type, 0);
		break;
	default:
		/* no-op */
		break;
	}
}

typedef int (*ldmsd_functional_op_t)(ldmsd_row_list_t row_list, ldmsd_row_t dest_row, int col_id);
static int none_op(ldmsd_row_list_t row_list, ldmsd_row_t dest_row, int col_id)
{
	ldmsd_row_t src_row = TAILQ_FIRST(row_list);
	ldmsd_col_t src_col = &src_row->cols[col_id];
	ldmsd_col_t dst_col = &dest_row->cols[col_id];
	assign_value(dst_col, src_col);
	return 0;
}

static int diff_op(ldmsd_row_list_t row_list, ldmsd_row_t dest_row, int col_id)
{
	ldmsd_row_t src_row = TAILQ_FIRST(row_list);
	ldmsd_row_t prev_row = TAILQ_NEXT(src_row, entry);
	ldmsd_col_t dst_col = &dest_row->cols[col_id];
	struct ldmsd_col_s zero_col;
	union ldms_value zero;
        uint64_t src_time;
        uint64_t prev_time;
        uint64_t diff_time;

	memset(&zero, 0, sizeof(zero));
	zero_col.mval = &zero;
	zero_col.array_len = 1;
	zero_col.type = LDMS_V_U64;
	/* dst_col->mval = src_row->cols[col_id].mval - prev_row->cols[col_id].mval; */
	if (!prev_row) {
		assign_value(dst_col, &zero_col);
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
                src_time = (src_row->cols[col_id].mval->v_ts.sec * 1000000) + src_row->cols[col_id].mval->v_ts.usec;
                prev_time = (prev_row->cols[col_id].mval->v_ts.sec * 1000000) + prev_row->cols[col_id].mval->v_ts.usec;
                diff_time = src_time - prev_time;
                dst_col->mval->v_ts.sec = (uint32_t)(diff_time / 1000000);
                dst_col->mval->v_ts.usec = (uint32_t)(diff_time % 1000000);
		break;
	default:
		return EINVAL;
	}
	return 0;
}

static int mean_op(ldmsd_row_list_t row_list, ldmsd_row_t dest_row, int col_id)
{
	ldmsd_row_t src_row = TAILQ_FIRST(row_list);
	ldmsd_col_t dst_col = &dest_row->cols[col_id];
	union ldms_value *x;
	int64_t bi, r, g;
	double bd;
        uint64_t tm;
	int n;

	switch (dst_col->type) {
	case LDMS_V_CHAR:
	case LDMS_V_U8: case LDMS_V_S8:
	case LDMS_V_U16: case LDMS_V_S16:
	case LDMS_V_U32: case LDMS_V_S32:
	case LDMS_V_U64: case LDMS_V_S64:
	case LDMS_V_F32: case LDMS_V_D64:
	case LDMS_V_TIMESTAMP:
		/* OK */
		break;
	default:
		return EINVAL;
	}

	/* calculate average iteratively (similar to simple moving average)
	 * to reduce the chance of value overflow or underflow.
	 *
	 * NOTE:
	 *  A is an average series (real value)
	 *  B is the integer part of A
	 *  R is the residual part
	 *  Namely: A[n] = B[n] + (R[n])/n
	 *
	 *   A[n+1] = (n*A[n] + x[n+1]) / (n+1)
	 *          = ( (n+1)A[n] - A[n] + x[n+1] ) / (n+1)
	 *          = (n+1)A[n]/(n+1) - A[n]/(n+1) + x[n+1]/(n+1)
	 *          = A[n] + (x[n+1] - A[n])/(n+1)
	 *          = B[n] + (R[n])/n + (x[n+1] - B[n] - (R[n])/n)/(n+1)
	 *          = B[n] + ( (n+1)R[n]/n + x[n+1] - B[n] - R[n]/n )/(n+1)
	 *          = B[n] + ( R[n] + x[n+1] - B[n] )/(n+1)
	 *
	 *   In the calculation below
	 *   `bi` is integer-type B[n] on LHS, and is B[n+1] on RHS
	 *   `bd` is double-type  B[n] on LHS, and is B[n+1] on RHS
	 *   `r` is R[n] on LHS, and is R[n+1] on RHS
	 *   `g` refers to the term R[n]+x[n+1]-B[n]
	 */
	r = 0;
	n = 0;
	bi = 0;
	bd = 0;
	while (src_row)
	{
		x = src_row->cols[col_id].mval;
		switch (dst_col->type) {
		case LDMS_V_U8:
			g = r + x->v_u8 - bi;
			bi = bi + g/(n+1);
			r = g % (n+1);
			break;
		case LDMS_V_S8:
			g = r + x->v_s8 - bi;
			bi = bi + g/(n+1);
			r = g % (n+1);
			break;
		case LDMS_V_U16:
			g = r + x->v_u16 - bi;
			bi = bi + g/(n+1);
			r = g % (n+1);
			break;
		case LDMS_V_S16:
			g = r + x->v_s16 - bi;
			bi = bi + g/(n+1);
			r = g % (n+1);
			break;
		case LDMS_V_U32:
			g = r + x->v_u32 - bi;
			bi = bi + g/(n+1);
			r = g % (n+1);
			break;
		case LDMS_V_S32:
			g = r + x->v_s32 - bi;
			bi = bi + g/(n+1);
			r = g % (n+1);
			break;
		case LDMS_V_U64:
			/* make small terms to prevent over/under flow */
			g = x->v_u64/(n+1) - bi/(n+1);
			r = r + (x->v_u64 % (n+1)) - (bi%(n+1));
			bi = bi + g + r/(n+1);
			r = r % (n+1);
			break;
		case LDMS_V_S64:
			/* make small terms to prevent over/under flow */
			g = x->v_s64/(n+1) - bi/(n+1);
			r = r + (x->v_s64 % (n+1)) - (bi%(n+1));
			bi = bi + g + r/(n+1);
			r = r % (n+1);
			break;
		case LDMS_V_F32:
			bd = bd + (x->v_f - bd)/(n+1);
			break;
		case LDMS_V_D64:
			bd = bd + (x->v_d - bd)/(n+1);
			break;
		case LDMS_V_TIMESTAMP:
			/* do as u64 usecs and convert back later */
			tm = (x->v_ts.sec * 1000000) + x->v_ts.usec;
			g = r + tm - bi;
			bi = bi + g/(n+1);
			r = g % (n+1);
			break;
		default:
			return EINVAL;
		}
		n += 1;
		src_row = TAILQ_NEXT(src_row, entry);
	}
	/* residue with opposite sign of the result */
	if (r < 0 && bi > 0) {
		bi -= 1;
	} else if (r > 0 && bi < 0) {
		bi += 1;
	}
	switch (dst_col->type) {
	case LDMS_V_U8:
		dst_col->mval->v_u8 = bi;
		break;
	case LDMS_V_S8:
		dst_col->mval->v_s8 = bi;
		break;
	case LDMS_V_U16:
		dst_col->mval->v_u16 = bi;
		break;
	case LDMS_V_S16:
		dst_col->mval->v_s16 = bi;
		break;
	case LDMS_V_U32:
		dst_col->mval->v_u32 = bi;
		break;
	case LDMS_V_S32:
		dst_col->mval->v_s32 = bi;
		break;
	case LDMS_V_U64:
		dst_col->mval->v_u64 = bi;
		break;
	case LDMS_V_S64:
		dst_col->mval->v_s64 = bi;
		break;
	case LDMS_V_F32:
		dst_col->mval->v_f = bd;
		break;
	case LDMS_V_D64:
		dst_col->mval->v_d = bd;
		break;
	case LDMS_V_TIMESTAMP:
		dst_col->mval->v_ts.sec  = bi / 1000000;
		dst_col->mval->v_ts.usec = bi % 1000000;
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
	struct ldmsd_col_s max_col;

	max_col.mval = &max;
	max_col.type = dst_col->type;
	max_col.array_len = 1;

	assign_value(&max_col, &src_row->cols[col_id]);
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
	struct ldmsd_col_s min_col;

	min_col.mval = &min;
	min_col.type = dst_col->type;
	min_col.array_len = 1;

	assign_value(&min_col, &src_row->cols[col_id]);
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
				   ldmsd_row_list_t row_list, int *row_count,
				   void **decomp_ctxt)
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
	struct ldmsd_col_s _col;
	decomp_static_mid_rbn_t mid_rbn = NULL;
	ldms_digest_t ldms_digest;
	TAILQ_HEAD(, _list_entry) list_cols;
	int row_more_le;
	struct ldms_timestamp ts;
	const char *producer;
	const char *instance;
	const char *schema;
	int producer_len, instance_len, schema_len;
	union ldms_value zfill = {0}; /* zero value as default "fill" */

	if (!TAILQ_EMPTY(row_list))
		return EINVAL;

	ts = ldms_transaction_timestamp_get(set);
	producer = ldms_set_producer_name_get(set);
	producer_len = strlen(producer) + 1;
	instance = ldms_set_instance_name_get(set);
	instance_len = strlen(instance) + 1;
	schema = ldms_set_schema_name_get(set);
	schema_len = strlen(schema) + 1;

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
				free(mid_rbn);
				goto err_0;
			}
			rc = resolve_metrics(strgp, mid_rbn, cfg_row, set);
			if (rc) {
				/* decomp_static cannot decompose
				 * the row but it is not an error,
				 * e.g., empty list.
				 */
				if (rc == ENOEXEC) {
					rc = 0;
				}
				free(mid_rbn);
				goto err_0;
			}
			memcpy(&mid_rbn->ldms_digest, ldms_digest, sizeof(*ldms_digest));
			rbn_init(&mid_rbn->rbn, &mid_rbn->ldms_digest);
			rbt_ins(&cfg_row->mid_rbt, &mid_rbn->rbn);
			ldms_digest_str(ldms_digest, mid_rbn->digest_str,
					sizeof(mid_rbn->digest_str));
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
			case LDMSD_PHONY_METRIC_ID_DURATION:
				/* mcol->mval will be assigned in `make_row` */
				mcol->mtype = LDMS_V_TIMESTAMP;
				continue;
			case LDMSD_PHONY_METRIC_ID_UID:
			case LDMSD_PHONY_METRIC_ID_GID:
			case LDMSD_PHONY_METRIC_ID_PERM:
			case LDMSD_PHONY_METRIC_ID_CARD:
				/* mcol->mval will be assigned in `make_row` */
				mcol->mtype = LDMS_V_U32;
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
			case LDMSD_PHONY_METRIC_ID_DIGEST:
				/* mcol->mval->a_char is digest str */
				mcol->mtype = LDMS_V_CHAR_ARRAY;
				mcol->array_len = LDMS_DIGEST_STR_LENGTH;
				continue;
			case LDMSD_PHONY_METRIC_ID_SCHEMA:
				/* mcol->mval->a_char is schema */
				mcol->mtype = LDMS_V_CHAR_ARRAY;
				mcol->array_len = schema_len;
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
				ovis_log(static_log, OVIS_LERROR, "strgp '%s': the metric type (%s) of "
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
			if (mcol->mval) {
				mcol->mtype = cfg_row->cols[j].type;
				mcol->array_len = cfg_row->cols[j].fill_len;
			} else {
				/* no "fill" specified, use default "fill" */
				mcol->mval = &zfill;
				mcol->array_len = 1;
				mcol->mtype = cfg_row->cols[j].type;
			}
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
			cfg_col = &cfg_row->cols[j];
			col = &row->cols[j];
			col->mval = (ldms_mval_t)&row->mvals[cfg_col->mval_offset];
			mcol = &col_mvals[j];

			col->metric_id = mcol->metric_id;
			col->rec_metric_id = mcol->rec_metric_id;
			col->name = cfg_col->dst;
			col->type = cfg_col->type;
			col->array_len = cfg_col->array_len;

			switch (mid_rbn->col_mids[j].mid) {
			case LDMSD_PHONY_METRIC_ID_TIMESTAMP:
				col->mval->v_ts = ts;
				continue;
			case LDMSD_PHONY_METRIC_ID_DURATION:
				col->mval->v_ts = ldms_transaction_duration_get(set);
				continue;
			case LDMSD_PHONY_METRIC_ID_UID:
				col->mval->v_u32 = ldms_set_uid_get(set);
				continue;
			case LDMSD_PHONY_METRIC_ID_GID:
				col->mval->v_u32 = ldms_set_gid_get(set);
				continue;
			case LDMSD_PHONY_METRIC_ID_PERM:
				col->mval->v_u32 = ldms_set_perm_get(set);
				continue;
			case LDMSD_PHONY_METRIC_ID_CARD:
				col->mval->v_u32 = ldms_set_card_get(set);
				continue;
			case LDMSD_PHONY_METRIC_ID_PRODUCER:
				col->mval = (ldms_mval_t)producer;
				col->array_len = producer_len;
				continue;
			case LDMSD_PHONY_METRIC_ID_INSTANCE:
				col->mval = (ldms_mval_t)instance;
				col->array_len = instance_len;
				continue;
			case LDMSD_PHONY_METRIC_ID_DIGEST:
				col->mval = (ldms_mval_t)mid_rbn->digest_str;
				col->array_len = LDMS_DIGEST_STR_LENGTH;
				continue;
			case LDMSD_PHONY_METRIC_ID_SCHEMA:
				col->mval = (ldms_mval_t)schema;
				col->array_len = schema_len;
				continue;
			case LDMSD_PHONY_METRIC_ID_FILL:
				col->mval = cfg_col->fill;
				col->array_len = cfg_col->fill_len;
				continue;
			default:
				_col.array_len = mcol->array_len;
				_col.type = mcol->mtype;
				_col.mval = mcol->mval;
				assign_value(col, &_col);
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
			if (mcol->mval) {
				mcol->array_len = row->cols[j].array_len;
				mcol->mtype = cfg_row->cols[j].type;
			} else {
				mcol->mval = &zfill;
				mcol->array_len = 1;
				mcol->mtype = cfg_row->cols[j].type;
			}
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
                        struct ldmsd_row_list_s tmp_row_list;
                        int count;
                        count = ldmsd_row_cache_make_list(&tmp_row_list,
                                                          cfg_row->row_limit,
                                                          strgp->row_cache,
                                                          group_idx);
			for (j = 0; j < row->col_count; j++) {
				cfg_col = &cfg_row->cols[j];
				if (cfg_col->op != LDMSD_DECOMP_OP_NONE
                                    && count < cfg_row->row_limit)
					ovis_log(static_log, OVIS_LDEBUG,
                                                  "strgp '%s': insufficient rows (%d of %d) in "
                                                  "cache to satisfy functional operator '%s' "
                                                  "on column '%s'.\n",
                                                  strgp->obj.name, count, cfg_row->row_limit,
                                                  ldmsd_decomp_op_to_string(cfg_col->op),
                                                  cfg_col->dst);
				rc = op_table[cfg_col->op](&tmp_row_list, dup_row, j);
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
