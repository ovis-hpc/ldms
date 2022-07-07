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

#include <openssl/sha.h>

#include "ovis_json/ovis_json.h"
#include "coll/rbt.h"

#include "ldmsd.h"
#include "ldmsd_request.h"

typedef enum ldmsd_decomp_type_e ldmsd_decomp_type_t;
enum ldmsd_decomp_type_e {
	LDMSD_DECOMP_STATIC,
	LDMSD_DECOMP_AS_IS,
};

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

/* Access dict[key], expecting it to be a str */
static json_str_t __jdict_str(json_entity_t dict, const char *key)
{
	json_entity_t val;

	val = __jdict_ent(dict, key);
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

int __decomp_rbn_cmp(void *tree_key, const void *key)
{
	return strcmp(tree_key, key);
}

static struct rbt __decomp_rbt = RBT_INITIALIZER(__decomp_rbn_cmp);
struct __decomp_rbn_s {
	struct rbn rbn;
	char name[256];
	ldmsd_decomp_t decomp;
};
typedef struct __decomp_rbn_s *__decomp_rbn_t;

static ldmsd_decomp_t __decomp_get(const char *decomp, ldmsd_req_ctxt_t reqc)
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
	__decomp_rbn_t drbn;
	ldmsd_decomp_t (*get)(), dc;

	drbn = (void*)rbt_find(&__decomp_rbt, decomp);
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
		if (!stat(library_name, &st))
			continue;
		dlerr = dlerror();
		errno = ELIBBAD;
		DECOMP_ERR(reqc, ELIBBAD, "Bad decomposer '%s': dlerror %s\n",
			   library_name, dlerr);
		return NULL;
	}

	if (!d) {
		dlerr = dlerror();
		errno = ELIBBAD;
		DECOMP_ERR(reqc, ELIBBAD, "Failed to load decomposer '%s': "
				"dlerror %s\n", decomp, dlerr);
		return NULL;
	}

	get = dlsym(d, "get");
	if (!get) {
		errno = ELIBBAD;
		DECOMP_ERR(reqc, ELIBBAD, "get() not found in %s\n", libpath);
		return NULL;
	}

	dc = get();
	if (!dc) {
		DECOMP_ERR(reqc, errno, "%s:get() error: %d\n", libpath, errno);
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
	rbt_ins(&__decomp_rbt, &drbn->rbn);

	return dc;
}

/* Export so that decomp_flex can call, but don't advertise this in ldmsd.h */
ldmsd_decomp_t ldmsd_decomp_get(const char *decomp, ldmsd_req_ctxt_t reqc)
{
	return __decomp_get(decomp, reqc);
}

/* protected by strgp lock */
int ldmsd_decomp_config(ldmsd_strgp_t strgp, const char *json_path, ldmsd_req_ctxt_t reqc)
{
	json_str_t s;
	json_entity_t cfg;
	int fd, rc;
	off_t off;
	size_t sz;
	json_parser_t jp = NULL;
	char *buff = NULL;
	ldmsd_decomp_t decomp_api;

	if (strgp->decomp) {
		/* already configured */
		rc = EALREADY;
		DECOMP_ERR(reqc, EALREADY, "Already configurd\n");
		goto err_0;
	}

	/* Load JSON from file */
	fd = open(json_path, O_RDONLY);
	if (fd < 0) {
		rc = errno;
		DECOMP_ERR(reqc, errno, "open error %d, file: %s\n",
			   errno, json_path);
		goto err_0;
	}
	off = lseek(fd, 0, SEEK_END);
	if (off == -1) {
		rc = errno;
		DECOMP_ERR(reqc, errno, "seek failed, errno: %d\n", errno);
		goto err_1;
	}
	sz = off;
	buff = malloc(sz + 1);
	if (!buff) {
		rc = errno;
		DECOMP_ERR(reqc, errno, "not enough memory");
		goto err_1;
	}
	off = lseek(fd, 0, SEEK_SET);
	if (off == -1) {
		rc = errno;
		DECOMP_ERR(reqc, errno, "seek failed, errno: %d\n", errno);
		goto err_2;
	}
	jp = json_parser_new(0);
	if (!jp) {
		rc = errno;
		DECOMP_ERR(reqc, errno, "cannot create json parser, error: %d\n", errno);
		goto err_2;
	}
	off = read(fd, buff, sz);
	if (off != sz) {
		rc = errno;
		DECOMP_ERR(reqc, errno, "read failed, error: %d\n", errno);
		goto err_2;
	}
	buff[sz] = '\0';
	rc = json_parse_buffer(jp, buff, sz, &cfg);
	if (rc) {
		DECOMP_ERR(reqc, rc, "json parse error: %d\n", rc);
		errno = rc;
		goto err_3;
	}

	/* Configure decomposer */
	s = __jdict_str(cfg, "type");
	if (!s) {
		rc = errno = EINVAL;
		DECOMP_ERR(reqc, EINVAL, "decomposer: 'type' attribute is missing.\n");
		goto err_4;
	}
	decomp_api = __decomp_get(s->str, reqc);
	if (!decomp_api) {
		rc = errno;
		goto err_4;
	}
	strgp->decomp = decomp_api->config(strgp, cfg, reqc);
	if (!strgp->decomp) {
		rc = errno;
		goto err_4;
	}

	/* decomp config success! */
	rc = 0;

	/* let-through, clean-up */
 err_4:
	json_entity_free(cfg);
 err_3:
	json_parser_free(jp);
 err_2:
	free(buff);
 err_1:
	close(fd);
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
