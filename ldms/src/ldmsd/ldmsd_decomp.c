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
#include <dlfcn.h>
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <ctype.h>

#include <openssl/sha.h>

#include <jansson.h>

#ifdef HAVE_YYJSON
#include <yyjson.h>
#endif

#include "coll/rbt.h"

#include "ldmsd.h"
#include "ldmsd_request.h"

/* Defined in ldmsd.c */
extern ovis_log_t store_log;

typedef enum ldmsd_decomp_type_e ldmsd_decomp_type_t;
enum ldmsd_decomp_type_e {
	LDMSD_DECOMP_STATIC,
	LDMSD_DECOMP_AS_IS,
};

/* Macro to put error message in both ldmsd log and `reqc` */
#define DECOMP_ERR(reqc, rc, fmt, ...) do { \
		ovis_log(store_log, OVIS_LERROR, "decomposer: " fmt, ##__VA_ARGS__); \
		if (reqc) { \
			(reqc)->errcode = rc; \
			linebuf_printf(reqc, "decomposer: " fmt, ##__VA_ARGS__); \
		} \
	} while (0)

static int decomp_rbn_cmp(void *tree_key, const void *key)
{
	return strcmp(tree_key, key);
}

static struct rbt decomp_rbt = RBT_INITIALIZER(decomp_rbn_cmp);
struct decomp_rbn_s {
	struct rbn rbn;
	char name[256];
	ldmsd_decomp_t decomp;
};
typedef struct decomp_rbn_s *decomp_rbn_t;

static ldmsd_decomp_t decomp_get(const char *decomp, ldmsd_req_ctxt_t reqc)
{
	char library_name[PATH_MAX];
	char library_path[PATH_MAX];
	char *pathdir = library_path;
	char *libpath;
	char *saveptr = NULL;
	char *path = getenv("LDMSD_PLUGIN_LIBPATH");
	void *d = NULL;
	char *dlerr;
	struct stat st;
	int rc;
	decomp_rbn_t drbn;
	ldmsd_decomp_t (*get)(), dc;

	drbn = (void*)rbt_find(&decomp_rbt, decomp);
	if (drbn)
		return drbn->decomp;

	if (!path)
		path = LDMSD_PLUGIN_LIBPATH_DEFAULT;

	strncpy(library_path, path, sizeof(library_path) - 1);

	while ((libpath = strtok_r(pathdir, ":", &saveptr)) != NULL) {
		pathdir = NULL;
		snprintf(library_name, sizeof(library_name),
			 "%s/libdecomp_%s.so", libpath, decomp);
		d = dlopen(library_name, RTLD_NOW);
		if (d)
			break;
		errno = 0;
		if ((rc = stat(library_name, &st))) {
			rc = errno;
			if (rc == ENOENT) {
				/*
				 * We will print a not-found log message
				 * after we search all paths in pathdir.
				 */
			} else {
				DECOMP_ERR(reqc, rc, "Failed to load %s. %s\n",
							library_name, strerror(rc));
			}
			continue;
		} else {
			dlerr = dlerror();
			errno = ELIBBAD;
			DECOMP_ERR(reqc, ELIBBAD, "Failed to load '%s': dlerror %s\n",
								library_name, dlerr);
			return NULL;
		}
	}
	if (!d) {
		errno = rc;
		DECOMP_ERR(reqc, rc, "Cannot load the decomposer %s library.\n", decomp);
		return NULL;
	}

	get = dlsym(d, "get");
	if (!get) {
		errno = ELIBBAD;
		DECOMP_ERR(reqc, ELIBBAD, "The library %s is not a valid "
					"decomposer plugin.\n", libpath);
		return NULL;
	}

	dc = get();
	if (!dc) {
		rc = errno;
		DECOMP_ERR(reqc, rc, "Failed to get a decomposer '%s' object. %s\n",
							libpath, strerror(rc));
		return NULL;
	}

	drbn = malloc(sizeof(*drbn));
	if (!drbn) {
		DECOMP_ERR(reqc, ENOMEM, "Not enough memory.\n");
		return NULL;
	}

	rbn_init(&drbn->rbn, drbn->name);
	snprintf(drbn->name, sizeof(drbn->name), "%s", decomp);
	drbn->decomp = dc;
	rbt_ins(&decomp_rbt, &drbn->rbn);

	return dc;
}

/* Export so that decomp_flex can call, but don't advertise this in ldmsd.h */
ldmsd_decomp_t ldmsd_decomp_get(const char *decomp, ldmsd_req_ctxt_t reqc)
{
	return decomp_get(decomp, reqc);
}

#if JANSSON_VERSION_HEX >= 0x020b00
const char *err_text[] = {
	[json_error_unknown] = "unknown error",
	[json_error_out_of_memory] = "out of memory",
	[json_error_stack_overflow] = "stack overflow",
	[json_error_cannot_open_file] = "cannot open file",
	[json_error_invalid_argument] = "invalid argument",
	[json_error_invalid_utf8] = "invalid UTF8",
	[json_error_premature_end_of_input] = "unexpected end of file",
	[json_error_end_of_input_expected] = "unexpected data at end of object",
	[json_error_invalid_syntax] = "invalid syntax",
	[json_error_invalid_format] = "invalid format",
	[json_error_wrong_type] = "wrong type",
	[json_error_null_character] = "null character",
	[json_error_null_value] = "null value",
	[json_error_null_byte_in_key] = "null byte in key",
	[json_error_duplicate_key] = "duplicate key",
	[json_error_numeric_overflow] = "numeric overflow",
	[json_error_item_not_found] = "item not found",
	[json_error_index_out_of_range] = "index out of range"
};
#endif

/* protected by strgp lock */
int ldmsd_decomp_config(ldmsd_strgp_t strgp, const char *json_path, ldmsd_req_ctxt_t reqc)
{
	int rc;
	json_t *root, *type;
	json_error_t jerr;
	ldmsd_decomp_t decomp_api;

	if (strgp->decomp) {
		/* already configured */
		rc = EALREADY;
		DECOMP_ERR(reqc, EALREADY, "Already configured.\n");
		goto err_0;
	}

	root = json_load_file(json_path, 0, &jerr);
	if (!root) {
		rc = errno = EINVAL;
		DECOMP_ERR(reqc, rc,
			"json parser error: line %d column: %d %s: %s\n",
			jerr.line, jerr.column,
			#if JANSSON_VERSION_HEX >= 0x020b00
			err_text[json_error_code(&jerr)],
			#else
			"",
			#endif
			jerr.text);
		goto err_1;
	}

	/* Configure decomposer */
	type = json_object_get(root, "type");
	if (!type) {
		rc = errno = EINVAL;
		DECOMP_ERR(reqc, rc,
			"json parser error: JSON configuration is missing the "
			"'type' attribute.\n");
		goto err_1;
	}
	decomp_api = decomp_get(json_string_value(type), reqc);
	if (!decomp_api) {
		rc = errno;
		goto err_1;
	}
	strgp->decomp = decomp_api->config(strgp, root, reqc);
	if (!strgp->decomp) {
		rc = errno;
		goto err_1;
	}

	/* decomp config success! */
	rc = 0;
err_1:
	json_decref(root);
err_0:
	return rc;
}

typedef struct strbuf_s {
	TAILQ_ENTRY(strbuf_s) entry;
	int off; /* current write offset */
	int remain; /* remaining bytes */
	char buf[];
} *strbuf_t;
TAILQ_HEAD(strbuf_tailq_s, strbuf_s);
typedef struct strbuf_tailq_s *strbuf_tailq_t;

void strbuf_purge(strbuf_tailq_t h)
{
	strbuf_t b;
	while ((b = TAILQ_FIRST(h))) {
		TAILQ_REMOVE(h, b, entry);
		free(b);
	}
}

__attribute__(( format(printf, 2, 3) ))
int strbuf_printf(strbuf_tailq_t h, const char *fmt, ...)
{
	va_list ap;
	int len;
	strbuf_t b = TAILQ_LAST(h, strbuf_tailq_s);

	if (b)
		goto print;

	/* no buffer, allocate one */
	len = BUFSIZ;

 alloc:
	len = (len + BUFSIZ - 1) & ~(BUFSIZ-1);
	b = malloc(sizeof(*b) + len);
	if (!b)
		return ENOMEM;
	b->remain = len;
	b->off = 0;
	TAILQ_INSERT_TAIL(h, b, entry);

 print:
	va_start(ap, fmt);
	len = vsnprintf(b->buf + b->off, b->remain, fmt, ap);
	va_end(ap);
	if (len >= b->remain) {
		b->buf[b->off] = '\0'; /* recover */
		goto alloc;
	}
	b->remain -= len;
	b->off += len;
	return 0;
}

static int strbuf_str(strbuf_tailq_t h, char **out_str, int *out_len)
{
	int len = 0, off = 0;
	char *str;
	strbuf_t b;
	TAILQ_FOREACH(b, h, entry) {
		len += b->off;
	}
	str = malloc(len + 1);
	if (!str)
		return ENOMEM;
	TAILQ_FOREACH(b, h, entry) {
		memcpy(str + off, b->buf, b->off);
		off += b->off;
	}
	str[len] = '\0';
	*out_str = str;
	*out_len = len;
	return 0;
}

static int strbuf_printcol_s8(strbuf_tailq_t h, ldmsd_col_t col)
{
	return strbuf_printf(h, "%hhd", col->mval->v_s8);
}

static int strbuf_printcol_u8(strbuf_tailq_t h, ldmsd_col_t col)
{
	return strbuf_printf(h, "%hhu", col->mval->v_u8);
}

static int strbuf_printcol_s16(strbuf_tailq_t h, ldmsd_col_t col)
{
	return strbuf_printf(h, "%hd", col->mval->v_s16);
}

static int strbuf_printcol_u16(strbuf_tailq_t h, ldmsd_col_t col)
{
	return strbuf_printf(h, "%hu", col->mval->v_u16);
}

static int strbuf_printcol_s32(strbuf_tailq_t h, ldmsd_col_t col)
{
	return strbuf_printf(h, "%d", col->mval->v_s32);
}

static int strbuf_printcol_u32(strbuf_tailq_t h, ldmsd_col_t col)
{
	return strbuf_printf(h, "%u", col->mval->v_u32);
}

static int strbuf_printcol_s64(strbuf_tailq_t h, ldmsd_col_t col)
{
	return strbuf_printf(h, "%ld", col->mval->v_s64);
}

static int strbuf_printcol_u64(strbuf_tailq_t h, ldmsd_col_t col)
{
	return strbuf_printf(h, "%lu", col->mval->v_u64);
}

static int strbuf_printcol_f(strbuf_tailq_t h, ldmsd_col_t col)
{
	return strbuf_printf(h, "%.9g", col->mval->v_f);
}

static int strbuf_printcol_d(strbuf_tailq_t h, ldmsd_col_t col)
{
	return strbuf_printf(h, "%.17g", col->mval->v_d);
}

static int strbuf_printcol_char(strbuf_tailq_t h, ldmsd_col_t col)
{
	return strbuf_printf(h, "\"%c\"", col->mval->v_char);
}

static int strbuf_printcol_str(strbuf_tailq_t h, ldmsd_col_t col)
{
	return strbuf_printf(h, "\"%s\"", col->mval->a_char);
}

static int strbuf_printcol_ts(strbuf_tailq_t h, ldmsd_col_t col)
{
	/* print TS as float */
	return strbuf_printf(h, "%u.%06u", col->mval->v_ts.sec,
					   col->mval->v_ts.usec);
}

static int strbuf_printcol_s8_array(strbuf_tailq_t h, ldmsd_col_t col)
{
	int rc;
	int i;
	rc = strbuf_printf(h, "[");
	if (rc)
		return rc;
	for (i = 0; i < col->array_len; i++) {
		if (i)
			rc = strbuf_printf(h, ",%hhd", col->mval->a_s8[i]);
		else
			rc = strbuf_printf(h, "%hhd", col->mval->a_s8[i]);
		if (rc)
			return rc;
	}
	rc = strbuf_printf(h, "]");
	return rc;
}

static int strbuf_printcol_u8_array(strbuf_tailq_t h, ldmsd_col_t col)
{
	int rc;
	int i;
	rc = strbuf_printf(h, "[");
	if (rc)
		return rc;
	for (i = 0; i < col->array_len; i++) {
		if (i)
			rc = strbuf_printf(h, ",%hhu", col->mval->a_u8[i]);
		else
			rc = strbuf_printf(h, "%hhu", col->mval->a_u8[i]);
		if (rc)
			return rc;
	}
	rc = strbuf_printf(h, "]");
	return rc;
}

static int strbuf_printcol_s16_array(strbuf_tailq_t h, ldmsd_col_t col)
{
	int rc;
	int i;
	rc = strbuf_printf(h, "[");
	if (rc)
		return rc;
	for (i = 0; i < col->array_len; i++) {
		if (i)
			rc = strbuf_printf(h, ",%hd", col->mval->a_s16[i]);
		else
			rc = strbuf_printf(h, "%hd", col->mval->a_s16[i]);
		if (rc)
			return rc;
	}
	rc = strbuf_printf(h, "]");
	return rc;
}

static int strbuf_printcol_u16_array(strbuf_tailq_t h, ldmsd_col_t col)
{
	int rc;
	int i;
	rc = strbuf_printf(h, "[");
	if (rc)
		return rc;
	for (i = 0; i < col->array_len; i++) {
		if (i)
			rc = strbuf_printf(h, ",%hu", col->mval->a_u16[i]);
		else
			rc = strbuf_printf(h, "%hu", col->mval->a_u16[i]);
		if (rc)
			return rc;
	}
	rc = strbuf_printf(h, "]");
	return rc;
}

static int strbuf_printcol_s32_array(strbuf_tailq_t h, ldmsd_col_t col)
{
	int rc;
	int i;
	rc = strbuf_printf(h, "[");
	if (rc)
		return rc;
	for (i = 0; i < col->array_len; i++) {
		if (i)
			rc = strbuf_printf(h, ",%d", col->mval->a_s32[i]);
		else
			rc = strbuf_printf(h, "%d", col->mval->a_s32[i]);
		if (rc)
			return rc;
	}
	rc = strbuf_printf(h, "]");
	return rc;
}

static int strbuf_printcol_u32_array(strbuf_tailq_t h, ldmsd_col_t col)
{
	int rc;
	int i;
	rc = strbuf_printf(h, "[");
	if (rc)
		return rc;
	for (i = 0; i < col->array_len; i++) {
		if (i)
			rc = strbuf_printf(h, ",%u", col->mval->a_u32[i]);
		else
			rc = strbuf_printf(h, "%u", col->mval->a_u32[i]);
		if (rc)
			return rc;
	}
	rc = strbuf_printf(h, "]");
	return rc;
}

static int strbuf_printcol_s64_array(strbuf_tailq_t h, ldmsd_col_t col)
{
	int rc;
	int i;
	rc = strbuf_printf(h, "[");
	if (rc)
		return rc;
	for (i = 0; i < col->array_len; i++) {
		if (i)
			rc = strbuf_printf(h, ",%ld", col->mval->a_s64[i]);
		else
			rc = strbuf_printf(h, "%ld", col->mval->a_s64[i]);
		if (rc)
			return rc;
	}
	rc = strbuf_printf(h, "]");
	return rc;
}

static int strbuf_printcol_u64_array(strbuf_tailq_t h, ldmsd_col_t col)
{
	int rc;
	int i;
	rc = strbuf_printf(h, "[");
	if (rc)
		return rc;
	for (i = 0; i < col->array_len; i++) {
		if (i)
			rc = strbuf_printf(h, ",%lu", col->mval->a_u64[i]);
		else
			rc = strbuf_printf(h, "%lu", col->mval->a_u64[i]);
		if (rc)
			return rc;
	}
	rc = strbuf_printf(h, "]");
	return rc;
}

static int strbuf_printcol_f_array(strbuf_tailq_t h, ldmsd_col_t col)
{
	int rc;
	int i;
	rc = strbuf_printf(h, "[");
	if (rc)
		return rc;
	for (i = 0; i < col->array_len; i++) {
		if (i)
			rc = strbuf_printf(h, ",%.9g", col->mval->a_f[i]);
		else
			rc = strbuf_printf(h, "%.9g", col->mval->a_f[i]);
		if (rc)
			return rc;
	}
	rc = strbuf_printf(h, "]");
	return rc;
}

static int strbuf_printcol_d_array(strbuf_tailq_t h, ldmsd_col_t col)
{
	int rc;
	int i;
	rc = strbuf_printf(h, "[");
	if (rc)
		return rc;
	for (i = 0; i < col->array_len; i++) {
		if (i)
			rc = strbuf_printf(h, ",%.17g", col->mval->a_d[i]);
		else
			rc = strbuf_printf(h, "%.17g", col->mval->a_d[i]);
		if (rc)
			return rc;
	}
	rc = strbuf_printf(h, "]");
	return rc;
}

typedef int (*printcol_fn)(strbuf_tailq_t h, ldmsd_col_t col);
printcol_fn printcol_tbl[] = {
	[LDMS_V_S8] = strbuf_printcol_s8,
	[LDMS_V_U8] = strbuf_printcol_u8,
	[LDMS_V_S16] = strbuf_printcol_s16,
	[LDMS_V_U16] = strbuf_printcol_u16,
	[LDMS_V_S32] = strbuf_printcol_s32,
	[LDMS_V_U32] = strbuf_printcol_u32,
	[LDMS_V_S64] = strbuf_printcol_s64,
	[LDMS_V_U64] = strbuf_printcol_u64,
	[LDMS_V_F32] = strbuf_printcol_f,
	[LDMS_V_D64] = strbuf_printcol_d,
	[LDMS_V_CHAR] = strbuf_printcol_char,
	[LDMS_V_CHAR_ARRAY] = strbuf_printcol_str,
	[LDMS_V_S8_ARRAY] = strbuf_printcol_s8_array,
	[LDMS_V_U8_ARRAY] = strbuf_printcol_u8_array,
	[LDMS_V_S16_ARRAY] = strbuf_printcol_s16_array,
	[LDMS_V_U16_ARRAY] = strbuf_printcol_u16_array,
	[LDMS_V_S32_ARRAY] = strbuf_printcol_s32_array,
	[LDMS_V_U32_ARRAY] = strbuf_printcol_u32_array,
	[LDMS_V_S64_ARRAY] = strbuf_printcol_s64_array,
	[LDMS_V_U64_ARRAY] = strbuf_printcol_u64_array,
	[LDMS_V_F32_ARRAY] = strbuf_printcol_f_array,
	[LDMS_V_D64_ARRAY] = strbuf_printcol_d_array,
	[LDMS_V_TIMESTAMP] = strbuf_printcol_ts,
	[LDMS_V_LAST+1] = NULL,
};

static int strbuf_printcol(strbuf_tailq_t h, ldmsd_col_t col)
{
	printcol_fn fn;
	if (col->type > LDMS_V_LAST)
		return EINVAL;
	fn = printcol_tbl[col->type];
	if (!fn)
		return EINVAL;
	return fn(h, col);
}

int ldmsd_row_to_json_array(ldmsd_row_t row, char **str, int *len)
{
	struct strbuf_tailq_s h = TAILQ_HEAD_INITIALIZER(h);
	ldmsd_col_t col;
	int i, rc;

	rc = strbuf_printf(&h, "[");
	if (rc)
		goto err_0;
	for (i = 0; i < row->col_count; i++) {
		col = &row->cols[i];
		if (i) { /* comma */
			rc = strbuf_printf(&h, ",");
			if (rc)
				goto err_0;
		}
		rc = strbuf_printcol(&h, col);
		if (rc)
			goto err_0;
	}
	rc = strbuf_printf(&h, "]");
	if (rc)
		goto err_0;

	rc = strbuf_str(&h, str, len);
	strbuf_purge(&h);
	return rc;

 err_0:
	strbuf_purge(&h);
	return rc;
}

int ldmsd_row_to_json_object(ldmsd_row_t row, char **str, int *len)
{
	struct strbuf_tailq_s h = TAILQ_HEAD_INITIALIZER(h);
	ldmsd_col_t col;
	int i, rc;

	rc = strbuf_printf(&h, "{");
	if (rc)
		goto err_0;
	for (i = 0; i < row->col_count; i++) {
		col = &row->cols[i];
		if (i) { /* comma */
			rc = strbuf_printf(&h, ",");
			if (rc)
				goto err_0;
		}
		rc = strbuf_printf(&h, "\"%s\":", col->name);
		if (rc)
			goto err_0;
		rc = strbuf_printcol(&h, col);
		if (rc)
			goto err_0;
	}
	rc = strbuf_printf(&h, "}");
	if (rc)
		goto err_0;

	rc = strbuf_str(&h, str, len);
	strbuf_purge(&h);
	return rc;

 err_0:
	strbuf_purge(&h);
	return rc;
}

#ifdef HAVE_YYJSON
bool yyjson_add_by_type(yyjson_mut_doc *doc, yyjson_mut_val *root, ldmsd_col_t col)
{
	int rc, i;
	char buf[2];
	yyjson_mut_val *arr = NULL;

	switch (col->type) {
	case LDMS_V_S8:
		return yyjson_mut_obj_add_int(doc, root, col->name, col->mval->v_s8);
		break;
	case LDMS_V_U8:
		return yyjson_mut_obj_add_int(doc, root, col->name, col->mval->v_u8);
		break;
	case LDMS_V_S16:
		return yyjson_mut_obj_add_int(doc, root, col->name, col->mval->v_s16);
		break;
	case LDMS_V_U16:
		return yyjson_mut_obj_add_int(doc, root, col->name, col->mval->v_u16);
		break;
	case LDMS_V_S32:
		return yyjson_mut_obj_add_int(doc, root, col->name, col->mval->v_s32);
		break;
	case LDMS_V_U32:
		return yyjson_mut_obj_add_int(doc, root, col->name, col->mval->v_u32);
		break;
	case LDMS_V_S64:
		return yyjson_mut_obj_add_int(doc, root, col->name, col->mval->v_u64);
		break;
	case LDMS_V_U64:
		return yyjson_mut_obj_add_int(doc, root, col->name, col->mval->v_u64);
		break;
	case LDMS_V_F32:
		return yyjson_mut_obj_add_real(doc, root, col->name, col->mval->v_f);
		break;
	case LDMS_V_D64:
		return yyjson_mut_obj_add_real(doc, root, col->name, col->mval->v_d);
		break;
	case LDMS_V_CHAR:
		buf[0] = col->mval->v_char;
		buf[1] = '\0';
		return yyjson_mut_obj_add_strcpy(doc, root, col->name, buf);
		break;
	case LDMS_V_CHAR_ARRAY:
		return yyjson_mut_obj_add_strcpy(doc, root, col->name, (char *)col->mval->a_char);
		break;
	case LDMS_V_S8_ARRAY:
		arr = yyjson_mut_arr_with_sint8(doc, col->mval->a_s8, col->array_len);
		return yyjson_mut_obj_add_val(doc, root, col->name, arr);
		break;
	case LDMS_V_U8_ARRAY:
		arr = yyjson_mut_arr_with_sint16(doc, col->mval->a_s16, col->array_len);
		return yyjson_mut_obj_add_val(doc, root, col->name, arr);
		break;
	case LDMS_V_S16_ARRAY:
		arr = yyjson_mut_arr_with_sint32(doc, col->mval->a_s32, col->array_len);
		return yyjson_mut_obj_add_val(doc, root, col->name, arr);
		break;
	case LDMS_V_U16_ARRAY:
		arr = yyjson_mut_arr_with_sint64(doc, col->mval->a_s64, col->array_len);
		return yyjson_mut_obj_add_val(doc, root, col->name, arr);
		break;
	case LDMS_V_S32_ARRAY:
		arr = yyjson_mut_arr_with_uint8(doc, col->mval->a_u8, col->array_len);
		return yyjson_mut_obj_add_val(doc, root, col->name, arr);
		break;
	case LDMS_V_U32_ARRAY:
		arr = yyjson_mut_arr_with_uint16(doc, col->mval->a_u16, col->array_len);
		return yyjson_mut_obj_add_val(doc, root, col->name, arr);
		break;
	case LDMS_V_S64_ARRAY:
		arr = yyjson_mut_arr_with_uint32(doc, col->mval->a_u32, col->array_len);
		return yyjson_mut_obj_add_val(doc, root, col->name, arr);
		break;
	case LDMS_V_U64_ARRAY:
		arr = yyjson_mut_arr_with_uint64(doc, col->mval->a_u64, col->array_len);
		return yyjson_mut_obj_add_val(doc, root, col->name, arr);
		break;
	case LDMS_V_F32_ARRAY:
		arr = yyjson_mut_arr_with_float(doc, col->mval->a_f, col->array_len);
		return yyjson_mut_obj_add_val(doc, root, col->name, arr);
		break;
	case LDMS_V_D64_ARRAY:
		arr = yyjson_mut_arr_with_double(doc, col->mval->a_d, col->array_len);
		return yyjson_mut_obj_add_val(doc, root, col->name, arr);
		break;
	case LDMS_V_TIMESTAMP:
		char *ts_buf = alloca(128);
		snprintf(ts_buf, 128, "%u.%06u", col->mval->v_ts.sec,
						 col->mval->v_ts.usec);
		return yyjson_mut_obj_add_strcpy(doc, root, col->name, ts_buf);
		break;
	case LDMS_V_LAST+1:
		goto err;
		break;
	}

 err:
	return NULL;
}

int ldmsd_row_to_json_object_yyjson(ldmsd_row_t row, char **out_str, int *out_len)
{
	ldmsd_col_t col;
	int i, rc = -2;
	size_t size = 0;

	yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
	yyjson_mut_val *root = yyjson_mut_obj(doc);
	yyjson_mut_doc_set_root(doc, root);
	yyjson_mut_val * val = NULL;

	if(doc == NULL || root == NULL) {
		goto err_0;
	}
	for (i = 0; i < row->col_count; i++) {
		col = &row->cols[i];
		bool good = yyjson_add_by_type(doc, root, col);
		if (!good) {
			rc = -5;
			goto err_0;
		}
	}
	char *json = yyjson_mut_write(doc, 0, &size);
	if (size == 0) {
		return -3;
	}
	*out_str = json;
	*out_len = size;
	yyjson_mut_doc_free(doc);
	return 0;
 err_0:
	return rc;
}
#endif

static const char *col_type_str(enum ldms_value_type type)
{
	static char *type_str[] = {
	    [LDMS_V_CHAR] = "{\"type\":\"string\",\"logicalType\":\"single-character\"}",
	    [LDMS_V_U8] = "{\"type\":\"int\",\"logicalType\":\"uint8\"}",
	    [LDMS_V_S8] = "{\"type\":\"int\",\"logicalType\":\"int8\"}",
	    [LDMS_V_U16] = "{\"type\":\"int\",\"logicalType\":\"unsigned-short\"}",
	    [LDMS_V_S16] = "{\"type\":\"int\",\"logicalType\":\"signed-short\"}",
	    [LDMS_V_U32] = "{\"type\":\"long\",\"logicalType\":\"unsigned-int\"}",
	    [LDMS_V_S32] = "\"int\"",
	    [LDMS_V_U64] = "{\"type\":\"long\",\"logicalType\":\"unsigned-long\"}",
	    [LDMS_V_S64] = "\"long\"",
	    [LDMS_V_F32] = "\"float\"",
	    [LDMS_V_D64] = "\"double\"",
	    [LDMS_V_CHAR_ARRAY] = "\"string\"",
	    [LDMS_V_U8_ARRAY] = "{\"type\":\"int\",\"logicalType\":\"uint8\"}",
	    [LDMS_V_S8_ARRAY] = "{\"type\":\"int\",\"logicalType\":\"int8\"}",
	    [LDMS_V_U16_ARRAY] = "{\"type\":\"int\",\"logicalType\":\"unsigned-short\"}",
	    [LDMS_V_S16_ARRAY] = "{\"type\":\"int\",\"logicalType\":\"signed-short\"}",
	    [LDMS_V_U32_ARRAY] = "{\"type\":\"long\",\"logicalType\":\"unsigned-int\"}",
	    [LDMS_V_S32_ARRAY] = "\"int\"",
	    [LDMS_V_U64_ARRAY] = "{\"type\":\"long\",\"logicalType\":\"unsigned-long\"}",
	    [LDMS_V_S64_ARRAY] = "\"long\"",
	    [LDMS_V_F32_ARRAY] = "\"float\"",
	    [LDMS_V_D64_ARRAY] = "\"double\"",
	    [LDMS_V_LIST] = "nosup",
	    [LDMS_V_LIST_ENTRY] = "nosup",
	    [LDMS_V_RECORD_TYPE] = "nosup",
	    [LDMS_V_RECORD_INST] = "nosup",
	    [LDMS_V_RECORD_ARRAY] = "nosup",
	    [LDMS_V_TIMESTAMP] = "{\"type\":\"long\",\"logicalType\":\"timestamp-millis\"}"
	};
	return type_str[type];
}

static const char *col_default(enum ldms_value_type type)
{
	static char *default_str[] = {
	    [LDMS_V_CHAR] = "\" \"",
	    [LDMS_V_U8] = "0",
	    [LDMS_V_S8] = "0",
	    [LDMS_V_U16] = "0",
	    [LDMS_V_S16] = "0",
	    [LDMS_V_U32] = "0",
	    [LDMS_V_S32] = "0",
	    [LDMS_V_U64] = "0",
	    [LDMS_V_S64] = "0",
	    [LDMS_V_F32] = "0.0",
	    [LDMS_V_D64] = "0.0",
	    [LDMS_V_CHAR_ARRAY] = "\"\"",
	    [LDMS_V_U8_ARRAY] = "[]",
	    [LDMS_V_S8_ARRAY] = "[]",
	    [LDMS_V_U16_ARRAY] = "[]",
	    [LDMS_V_S16_ARRAY] = "[]",
	    [LDMS_V_U32_ARRAY] = "[]",
	    [LDMS_V_S32_ARRAY] = "[]",
	    [LDMS_V_U64_ARRAY] = "[]",
	    [LDMS_V_S64_ARRAY] = "[]",
	    [LDMS_V_F32_ARRAY] = "[]",
	    [LDMS_V_D64_ARRAY] = "[]",
	    [LDMS_V_LIST] = "nosup",
	    [LDMS_V_LIST_ENTRY] = "nosup",
	    [LDMS_V_RECORD_TYPE] = "nosup",
	    [LDMS_V_RECORD_INST] = "nosup",
	    [LDMS_V_RECORD_ARRAY] = "nosup",
	    [LDMS_V_TIMESTAMP] = "0"
	};
	return default_str[type];
}

static char get_avro_char(char c)
{
	if (isalnum(c))
		return c;
	if (c == '-')
		return c;
	return '_';
}

char *ldmsd_avro_name_get(const char *ldms_name)
{
	char *avro_name_buf = calloc(1, strlen(ldms_name) + 1);
	char *avro_name = avro_name_buf;
	while (*ldms_name != '\0') {
		*avro_name++ = get_avro_char(*ldms_name++);
	}
	if (avro_name)
		*avro_name = '\0';
	return avro_name_buf;
}

int ldmsd_row_to_json_avro_schema(ldmsd_row_t row, char **str, size_t *len)
{
	char *avro_name = NULL;
	struct strbuf_tailq_s h = TAILQ_HEAD_INITIALIZER(h);
	ldmsd_col_t col;
	int i, rc;

	rc = strbuf_printf(&h, "{");
	if (rc) goto err_0;
	rc = strbuf_printf(&h, "\"name\":\"%s\",", row->schema_name);
	if (rc) goto err_0;
	rc = strbuf_printf(&h, "\"type\":\"record\",");
	if (rc) goto err_0;
	rc = strbuf_printf(&h, "\"fields\":[");
	if (rc) goto err_0;

	for (i = 0; i < row->col_count; i++) {
		col = &row->cols[i];
		avro_name = ldmsd_avro_name_get(col->name);
		if (i) { /* comma */
			rc = strbuf_printf(&h, ",");
			if (rc)
				goto err_0;
		}
		switch (col->type) {
		case LDMS_V_TIMESTAMP:
			rc = strbuf_printf(&h,
                                           "{\"name\":\"%s\",\"type\":%s}",
					   avro_name, col_type_str(col->type));
			if (rc)
				goto err_0;
			break;
		case LDMS_V_CHAR:
		case LDMS_V_U8:
		case LDMS_V_S8:
		case LDMS_V_U16:
		case LDMS_V_S16:
		case LDMS_V_U32:
		case LDMS_V_S32:
		case LDMS_V_U64:
		case LDMS_V_S64:
		case LDMS_V_F32:
		case LDMS_V_D64:
		case LDMS_V_CHAR_ARRAY:
			rc = strbuf_printf(&h,
                                           "{\"name\":\"%s\",\"type\":%s,"
                                           "\"default\":%s}",
					   avro_name, col_type_str(col->type),
                                           col_default(col->type));
			if (rc)
				goto err_0;
			break;
		case LDMS_V_U8_ARRAY:
		case LDMS_V_S8_ARRAY:
		case LDMS_V_U16_ARRAY:
		case LDMS_V_S16_ARRAY:
		case LDMS_V_U32_ARRAY:
		case LDMS_V_S32_ARRAY:
		case LDMS_V_U64_ARRAY:
		case LDMS_V_S64_ARRAY:
		case LDMS_V_F32_ARRAY:
		case LDMS_V_D64_ARRAY:
			rc = strbuf_printf(&h,
					   "{\"name\":\"%s\","
					   "\"type\":{ \"type\" : \"array\", \"items\": %s },"
                                           "\"default\":%s}",
					   avro_name, col_type_str(col->type),
                                           col_default(col->type));
			if (rc)
				goto err_0;
			break;
		case LDMS_V_LIST:
		case LDMS_V_LIST_ENTRY:
		case LDMS_V_RECORD_TYPE:
		case LDMS_V_RECORD_INST:
		case LDMS_V_RECORD_ARRAY:
		default:
			rc = EINVAL;
			goto err_0;
		}
	}
	rc = strbuf_printf(&h, "]}");
	if (rc)
		goto err_0;

	rc = strbuf_str(&h, str, (int *)len);
	strbuf_purge(&h);
	free(avro_name);
	return rc;

 err_0:
	if (avro_name)
		free(avro_name);
	strbuf_purge(&h);
	return rc;
}

typedef struct ldmsd_phony_metric_tbl_entry_s {
	const char *name;
	ldmsd_phony_metric_id_t id;
} *ldmsd_phony_metric_tbl_entry_t;

struct ldmsd_phony_metric_tbl_entry_s __phony_metric_tbl[] = {
	/* alphabetically ordered for bsearch */
	{ "M_card",      LDMSD_PHONY_METRIC_ID_CARD      },
	{ "M_digest",    LDMSD_PHONY_METRIC_ID_DIGEST    },
	{ "M_duration",  LDMSD_PHONY_METRIC_ID_DURATION  },
	{ "M_gid",       LDMSD_PHONY_METRIC_ID_GID       },
	{ "M_instance",  LDMSD_PHONY_METRIC_ID_INSTANCE  },
	{ "M_perm",      LDMSD_PHONY_METRIC_ID_PERM      },
	{ "M_producer",  LDMSD_PHONY_METRIC_ID_PRODUCER  },
	{ "M_schema",    LDMSD_PHONY_METRIC_ID_SCHEMA    },
	{ "M_timestamp", LDMSD_PHONY_METRIC_ID_TIMESTAMP },
	{ "M_uid",       LDMSD_PHONY_METRIC_ID_UID       },
	{ "instance",    LDMSD_PHONY_METRIC_ID_INSTANCE  },
	{ "producer",    LDMSD_PHONY_METRIC_ID_PRODUCER  },
	{ "timestamp",   LDMSD_PHONY_METRIC_ID_TIMESTAMP },
};

static int ldmsd_phony_metric_tbl_entry_cmp(const void *_k, const void *_e)
{
	const struct ldmsd_phony_metric_tbl_entry_s *e = _e;
	return strcmp(_k, e->name);
}

ldmsd_phony_metric_id_t ldmsd_phony_metric_resolve(const char *str)
{
	static const int N = sizeof(__phony_metric_tbl)/sizeof(__phony_metric_tbl[0]);
	const struct ldmsd_phony_metric_tbl_entry_s *ent;
	ent = bsearch(str, __phony_metric_tbl, N, sizeof(__phony_metric_tbl[0]),
			ldmsd_phony_metric_tbl_entry_cmp);
	if (!ent)
		return LDMSD_PHONY_METRIC_ID_UNKNOWN;
	return ent->id;
}
