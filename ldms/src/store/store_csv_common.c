/**
 * Copyright (c) 2016-2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2016-2019 Open Grid Computing, Inc. All rights reserved.
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


#define PLUGNAME 0
#define _GNU_SOURCE
#define store_csv_common_lib
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <assert.h>
#include "config.h"
#include "store_csv_common.h"
#define DSTRING_USE_SHORT
#include "ovis_util/dstring.h"


static char *bad_replacement = "/malloc/failed";

int replace_string(char **strp, const char *val)
{
	if (!strp)
		return EINVAL;
	if (!val) {
		if (*strp != bad_replacement)
			free(*strp);
		*strp = NULL;
		return 0;
	}
	if (*strp != bad_replacement)
		free(*strp);
	char *new = strdup(val);
	if (new) {
		*strp = new;
		return 0;
	}
	*strp = bad_replacement;
	return ENOMEM;
}

/* Disallow odd characters and space in environment variables
 * for template assembly.
 * Allow A-z0-9%@()+-_./:=
 */
static int validate_env(const char *var, const char *val, struct csv_plugin_static *cps) {
	int rc = 0;
	if (!val)
		return 0;
	const char *c = val;
	const char *b = NULL;
	for ( ; *c != '\0'; c++) {
		switch (*c) {
		case '%':
		case '(':
		case ')':
		case '+':
		case '-':
		case '=':
		case '_':
		case '.':
		case '/':
		case ':':
		case '@':
			break;
		default:
			if (!rc && !isalnum(*c)) {
				rc = ENOTSUP;
				b = c;
			}
		}
	}
	if (rc)
		ovis_log(cps->mylog, OVIS_LERROR, "%s: rename_output: unsupported character %c in template use of env(%s): %s\n",
			cps->pname, *b, var, val);
	return rc;
}

int create_outdir(const char *path, struct csv_store_handle_common *s_handle,
	struct csv_plugin_static *cps) {
	if (!cps) {
		return EINVAL;
	}
	if (!s_handle) {
		ovis_log(cps->mylog, OVIS_LERROR,"create_outdir: NULL store handle received.\n");
		return EINVAL;
	}
	mode_t mode = (mode_t) s_handle->create_perm;

	int err = 0;
	if (mode > 0) {
		/* derive directory mode from perm */
		mode |= S_IWUSR;
		if (mode & S_IROTH)
			mode |= S_IXOTH;
		if (mode & S_IRGRP)
			mode |= S_IXGRP;
		if (mode & S_IRUSR)
			mode |= S_IXUSR;
	} else {
		/* default 750 */
		mode = S_IXGRP | S_IXUSR | S_IRGRP | S_IRUSR |S_IWUSR;
	}
	/* ovis_log(cps->mylog, OVIS_LDEBUG,"f_mkdir_p %o %s\n", (int)mode, path); */
	err = f_mkdir_p(path, mode);
	if (err) {
		err = errno;
		switch (err) {
		case EEXIST:
			break;
		default:
			ovis_log(cps->mylog, OVIS_LERROR,"create_outdir: failed to create directory for %s: %s\n",
				path, STRERROR(err));
			return err;
		}
	}

	/* ovis_log(cps->mylog, OVIS_LDEBUG,"create_outdir: f_mkdir+p(%s, %o)\n", path, mode); */
	return 0;
}

void rename_output(const char *name,
	const char *ftype, struct csv_store_handle_common *s_handle,
	struct csv_plugin_static *cps) {
	if (!cps) {
		return;
	}
	if (!s_handle) {
		ovis_log(cps->mylog, OVIS_LERROR,"rename_output: NULL store handle received.\n");
		return;
	}
	const char *container = s_handle->container;
	const char *schema = s_handle->schema;
	if (s_handle && !s_handle->rename_template)
		return;
	char *rt = s_handle->rename_template;
	if (!rt || !name || !ftype || !container || !schema) {
		ovis_log(cps->mylog, OVIS_LDEBUG,"Invalid argument in rename_output"
				"(%s, %s, %s, %s, %s, %p, %p)\n",
				rt ? rt : "missing rename_template ",
				name ? name : "missing name",
				ftype ? ftype : "missing ftype",
				container ? container : "missing container",
				schema ? schema : "missing schema",
				s_handle, cps);
		return;
	}
	mode_t mode = (mode_t) s_handle->rename_perm;
	if (mode > 0) {
		errno = 0;
		int merr = chmod(name, mode);
		int rc = errno;
		if (merr) {
			ovis_log(cps->mylog, OVIS_LERROR,"%s: rename_output: unable to chmod(%s,%o): %s.\n",
				cps->pname, name, s_handle->rename_perm,
				STRERROR(rc));
		}
	}

	gid_t newgid = s_handle->rename_gid;
	uid_t newuid = s_handle->rename_uid;
	if (newuid != (uid_t)-1 || newgid != (gid_t)-1)
	{
		errno = 0;
		int merr = chown(name, newuid, newgid);
		int rc = errno;
		if (merr) {
			ovis_log(cps->mylog, OVIS_LERROR,"%s: rename_output: unable to chown(%s, %u, %u): %s.\n",
				cps->pname, name, newuid, newgid, STRERROR(rc));
		}
	}

	dsinit(ds);
	char *head = rt;
	char *end = strchr(head,'%');
	char *namedup = NULL;
	while (end != NULL) {
		dstrcat(&ds, head, (end - head));
		switch (end[1]) {
		case 'P':
			head = end + 2;
			dscat(ds, cps->pname);
			break;
		case 'S':
			head = end + 2;
			dscat(ds, s_handle->schema);
			break;
		case 'C':
			head = end + 2;
			dscat(ds, s_handle->container);
			break;
		case 'T':
			head = end + 2;
			dscat(ds, ftype);
			break;
		case 'B':
			head = end + 2;
			namedup = strdup(name);
			if (namedup) {
				char *bname = basename(namedup);
				dscat(ds, bname);
				free(namedup);
			} else {
				ovis_log(cps->mylog, OVIS_LERROR,"%s: rename_output: ENOMEM\n", cps->pname);
				dstr_free(&ds);
				return;
			}
			break;
		case 'D':
			head = end + 2;
			namedup = strdup(name);
			if (namedup) {
				char *dname = dirname(namedup);
				dscat(ds, dname);
				free(namedup);
			} else {
				ovis_log(cps->mylog, OVIS_LERROR,"%s: rename_output: ENOMEM\n", cps->pname);
				dstr_free(&ds);
				return;
			}
			break;
		case '{':
			head = end + 2;
			char *vend = strchr(head,'}');
			if (!vend) {
				ovis_log(cps->mylog, OVIS_LERROR,
					"%s: rename_output: unterminated %%{ in template at %s\n",
					cps->pname, head);
				dstr_free(&ds);
				return;
			} else {
				size_t vlen = vend - head + 1;
				char var[vlen];
				memset(var, 0, vlen);
				strncpy(var, head, vlen-1);
				var[vlen] = '\0';
				head = vend + 1;
				char *val = getenv(var);
				if (val) {
					/*
					ovis_log(cps->mylog, OVIS_LDEBUG,
						"%s: rename_output: getenv(%s) = %s\n", cps->pname, var, val);
					*/
					if (validate_env(var, val, cps)) {
						dstr_free(&ds);
						ovis_log(cps->mylog, OVIS_LERROR,
							"%s: rename_output: rename cancelled\n",
							cps->pname);
						return;
					}
					dscat(ds, val);
				} else {
					ovis_log(cps->mylog, OVIS_LDEBUG,
						"%s: rename_output: empty %%{%s}\n",
						cps->pname, var);
				}
			}
			break;
		case 's':
			head = end + 2;
			char *dot = strrchr(name,'.');
			if (!dot) {
				ovis_log(cps->mylog, OVIS_LERROR,"%s: rename_output: no timestamp\n", cps->pname);
				dstr_free(&ds);
				return;
			}
			dot = dot + 1;
			char *num = dot;
			while (isdigit(*num)) {
				num++;
			}
			if (*num != '\0') {
				ovis_log(cps->mylog, OVIS_LERROR,"%s: rename_output: no timestamp at end\n", cps->pname);
				dstr_free(&ds);
				return;
			}
			dscat(ds,dot);
			break;
		default:
			/* unknown subst */
			dstrcat(&ds, "%", 1);
			head = end + 1;
		}
		end = strchr(head, '%');
	}
	dscat(ds, head);
	char *newname = dsdone(ds);
	dstr_free(&ds);
	if (!newname) {
		ovis_log(cps->mylog, OVIS_LERROR,"%s: rename_output: failed to create new filename for %s\n",
			cps->pname, name);
		return;
	}

	namedup = strdup(newname);
	char *ndname = dirname(namedup);
	int err = 0;
	if (mode) {
		/* derive directory mode from perm */
		mode |= S_IWUSR;
		if (mode & S_IROTH)
			mode |= S_IXOTH;
		if (mode & S_IRGRP)
			mode |= S_IXGRP;
		if (mode & S_IRUSR)
			mode |= S_IXUSR;
	} else {
		/* default 750 */
		mode = S_IXGRP | S_IXUSR | S_IRGRP | S_IRUSR |S_IWUSR;
	}
	/* ovis_log(cps->mylog, OVIS_LDEBUG,"f_mkdir_p %o %s\n", (int)mode, ndname); */
	err = f_mkdir_p(ndname, mode);
	free(namedup);
	if (err) {
		err = errno;
		switch (err) {
		case EEXIST:
			break;
		default:
			ovis_log(cps->mylog, OVIS_LERROR, "%s: rename_output: failed to create directory for %s: %s\n",
				cps->pname, newname, STRERROR(err));
			free(newname);
			return;
		}

	}

	ovis_log(cps->mylog, OVIS_LDEBUG, "%s: rename_output: rename(%s, %s)\n",
		cps->pname, name, newname);
	err = rename(name, newname);
	if (err) {
		int ec = errno;
		ovis_log(cps->mylog, OVIS_LERROR,"%s: rename_output: failed rename(%s, %s): %s\n",
			    cps->pname, name, newname, STRERROR(ec));
	}
	free(newname);
}

void ch_output(FILE *f, const char *name,
	struct csv_store_handle_common *s_handle,
	struct csv_plugin_static *cps) {
	if (!cps) {
		return;
	}
	if (!s_handle) {
		ovis_log(cps->mylog, OVIS_LERROR,"ch_output: NULL store handle received.\n");
		return;
	}
	if (!f) {
		ovis_log(cps->mylog, OVIS_LERROR,"ch_output: NULL FILE pointer received.\n");
		return;
	}
	int fd = fileno(f);
	const mode_t ex = S_IXUSR | S_IXGRP | S_IXOTH;
	mode_t mode = (mode_t) s_handle->create_perm;
	mode &= 0777;
	mode &= ~ex;
	if (mode > 0) {
		errno = 0;
		int merr = fchmod(fd, mode);
		int rc = errno;
		if (merr) {
			ovis_log(cps->mylog, OVIS_LERROR,"ch_output: unable to chmod(%s,%o): %s.\n",
				name, s_handle->create_perm, STRERROR(rc));
		}
	}

	gid_t newgid = s_handle->create_gid;
	uid_t newuid = s_handle->create_uid;
	if (newuid != (uid_t)-1 || newgid != (gid_t)-1)
	{
		errno = 0;
		int merr = fchown(fd, newuid, newgid);
		int rc = errno;
		if (merr) {
			ovis_log(cps->mylog, OVIS_LERROR,"ch_output: unable to fchown(%d, (%s),%u, %u): %s.\n",
				fd, name, newuid, newgid, STRERROR(rc));
		}
		ovis_log(cps->mylog, OVIS_LDEBUG,"ch_output: fchown(%d, (%s),%u, %u): %s.\n",
			fd, name, newuid, newgid, STRERROR(rc));
	}
}

#if 0 /* swap_data def */
struct swap_data {
	size_t nstorekeys;
	size_t usedkeys;
	time_t appx;
	struct old_file *old;
	struct csv_plugin_static *cps;
};
#endif

static int config_buffer(const char *bs, const char *bt, int *rbs, int *rbt, const char *k) {
	int tempbs;
	int tempbt;
	if (!rbs || !rbt) {
		ovis_log(PG.mylog, OVIS_LERROR,
		       "%s: config_buffer: bad arguments\n", __FILE__);
		return EINVAL;
	}

	if (!bs && !bt){
		*rbs = 1;
		*rbt = 0;
		return 0;
	}

	if (!bs && bt){
		ovis_log(PG.mylog, OVIS_LERROR,
		       "%s: Cannot have buffer type without buffer for %s\n",
		       __FILE__, k);
		return EINVAL;
	}

	tempbs = atoi(bs);
	if (tempbs < 0){
		ovis_log(PG.mylog, OVIS_LERROR,
		       "%s: Bad val for buffer %d of %s\n",
		       __FILE__, tempbs, k);
		return EINVAL;
	}
	if ((tempbs == 0) || (tempbs == 1)){
		if (bt){
			ovis_log(PG.mylog, OVIS_LERROR,
			       "%s: Cannot have no/autobuffer with buffer type for %s\n",
			       __FILE__, k);
			return EINVAL;
		} else {
			*rbs = tempbs;
			*rbt = 0;
			return 0;
		}
	}

	if (!bt){
		ovis_log(PG.mylog, OVIS_LERROR,
		       "%s: Cannot have buffer size with no buffer type for %s\n",
		       __FILE__,  k);
		return EINVAL;
	}

	tempbt = atoi(bt);
	if ((tempbt != 3) && (tempbt != 4)){
		ovis_log(PG.mylog, OVIS_LERROR, "%s: Invalid buffer type %d for %s\n",
		       __FILE__, tempbt, k);
		return EINVAL;
	}

	if (tempbt == 4){
		//adjust bs for kb
		tempbs *= 1024;
	}

	*rbs = tempbs;
	*rbt = tempbt;

	return 0;
}


int csv_row_format_types_common(int typeformat, FILE* file, const char *fpath,
		const struct csv_store_handle_common *sh, int doudata,
		struct csv_plugin_static *cps, ldms_set_t set,
		struct ldmsd_row_s *row)
{
	if (typeformat < 1)
		return 0;
	if (!sh || !file || !fpath || !cps || !set)
		return EINVAL;
	const char *pn = cps->pname;
	size_t i;
	int j;
	int rc;
	const char *u64str = ldms_metric_type_to_str(LDMS_V_U64);
	const char *castr = ldms_metric_type_to_str(LDMS_V_CHAR_ARRAY);
	ldmsd_col_t col;

#define CHECKERR(fprintfresult) \
	if (fprintfresult < 0) { \
		ovis_log(cps->mylog, OVIS_LERROR, "%s: Error %d writing to type header '%s'\n", pn, \
		       fprintfresult, fpath); \
		goto error; \
	}

#define PRINT_UDATA \
	if (doudata) { \
		rc = fprintf(file, "%s%s", sep, u64str); \
		sep = ","; \
		CHECKERR(rc); \
	}

	const char *ud = "-udata";
	const char *arr;
	if (!doudata) {
		ud = "";
	}
	switch (typeformat) {
	case TH_UNROLL:
		arr = "";
		break;
	case TH_ARRAY:
		arr = "-array";
		break;
	default:
		rc = EINVAL;
		CHECKERR(rc);
	}
	rc = fprintf(file, "#!ldms%s-kinds%s!", arr, ud);
	CHECKERR(rc);

	const char *mt;
	const char *sep = "";
	for (i = 0; i < row->col_count; i++, sep=",") {
		col = &row->cols[i];
		enum ldms_value_type met_type = col->type;

		/* Handle phony metrics. They don't have udata. */
		switch (col->metric_id) {
		case LDMSD_PHONY_METRIC_ID_TIMESTAMP:
			/* timestamp (sec or msec) and remaining usec */
			rc = fprintf(file,"%stimestamp,%s", sep, u64str);
			CHECKERR(rc);
			continue;
		case LDMSD_PHONY_METRIC_ID_PRODUCER:
			rc = fprintf(file,"%s%s%d", sep, castr, LDMS_PRODUCER_NAME_MAX);
			CHECKERR(rc);
			continue;
		case LDMSD_PHONY_METRIC_ID_INSTANCE:
			rc = fprintf(file,"%s%s%d", sep, castr, LDMS_SET_NAME_MAX);
			CHECKERR(rc);
			continue;
		}

		/* otherwise, this is a normal metric */
		enum ldms_value_type base_type =
			ldms_metric_type_to_scalar_type(met_type);
		/* unroll arrays, except strings */
		if (TH_UNROLL == typeformat) {
			if ( met_type != LDMS_V_CHAR_ARRAY) {
				for (j = 0; j < col->array_len; j++) {
					PRINT_UDATA;
					mt = ldms_metric_type_to_str(base_type);
					rc = fprintf(file,"%s%s", sep, mt);
					CHECKERR(rc);
				}
			} else {
				PRINT_UDATA;
				mt = ldms_metric_type_to_str(met_type);
				rc = fprintf(file,"%s%s", sep, mt);
				CHECKERR(rc);
			}
		}
		/*  S32[]$len */
		if (TH_ARRAY == typeformat) {
			PRINT_UDATA;
			mt = ldms_metric_type_to_str(met_type);
			if (met_type != base_type) /* array */
				rc = fprintf(file,"%s%s%d", sep, mt, col->array_len);
			else
				rc = fprintf(file,"%s%s", sep, mt);
			CHECKERR(rc);
			continue;
		}
	}
	rc = fprintf(file,"\n");
	CHECKERR(rc);

	return 0;
 error:
	return 1;
#undef CHECKERR
#undef PRINT_UDATA
}

int __primitive_metric_header(FILE *fp, const struct csv_store_handle_common *sh,
		    int doudata, char *wsqt, const char *mname,
		    enum ldms_value_type mtype, size_t len, const char *sep)
{
	int j, ec;

	/* use same formats as ldms_ls */
	switch (mtype) {
	case LDMS_V_LIST:
	case LDMS_V_RECORD_INST:
	case LDMS_V_RECORD_TYPE:
		return EINTR;
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
		if (!sh->expand_array)
			goto store;
		if (doudata) {
			for (j = 0; j < len; j++) { // only 1 name for all of them.
				ec = fprintf(fp, "%s%s%s%d.userdata%s,%s%s%d.value%s",
					sep, wsqt, mname, j, wsqt, wsqt, mname, j, wsqt);
				if (ec < 0)
					return ec;
			}
		} else {
			for (j = 0; j < len; j++) { // only 1 name for all of them.
				ec = fprintf(fp, "%s%s%s%d%s", sep, wsqt, mname, j, wsqt);
				if (ec < 0)
					return ec;
			}
		}
		return 0;
	default:
		goto store;
	}

store:
	if (doudata) {
		ec = fprintf(fp, ",%s%s.userdata%s,%s%s.value%s",
			wsqt, mname, wsqt, wsqt, mname, wsqt);
		if (ec < 0)
			return ec;
	} else {
		ec = fprintf(fp, ",%s%s%s", wsqt, mname, wsqt);
		if (ec < 0)
			return ec;
	}
	return 0;
}

int csv_format_types_common(int typeformat, FILE* file, const char *fpath, const struct csv_store_handle_common *sh, int doudata, struct csv_plugin_static *cps, ldms_set_t set, int *metric_array, size_t metric_count)
{
	if (typeformat < 1)
		return 0;
	if (!sh || !file || !fpath || !cps || !set || !metric_array)
		return EINVAL;
	const char *pn = cps->pname;
	size_t i;
	int rc;

	/* make row */
	struct ldmsd_row_s *row;
	struct ldmsd_col_s *col;

#define CHECKERR(fprintfresult) \
	if (fprintfresult < 0) { \
		ovis_log(cps->mylog, OVIS_LERROR, "%s: Error %d writing to type header '%s'\n", pn, \
		       fprintfresult, fpath); \
		goto error; \
	}

#define PRINT_UDATA \
	if (doudata) { \
		rc = fprintf(file, ",%s", u64str); \
		CHECKERR(rc); \
	}

	row = calloc(1, sizeof(*row) + (2+metric_count) * sizeof(row->cols[0]));
	if (!row)
		return ENOMEM;
	row->col_count = 2 + metric_count;
	row->cols[0].name = "timestamp";
	row->cols[0].type = LDMS_V_U64;
	row->cols[0].metric_id = LDMSD_PHONY_METRIC_ID_TIMESTAMP;
	row->cols[0].array_len = 1;

	row->cols[1].name = "ProducerName";
	row->cols[1].type = LDMS_V_CHAR_ARRAY;
	row->cols[1].metric_id = LDMSD_PHONY_METRIC_ID_PRODUCER;
	row->cols[1].array_len = 16; /* can be anything, unused in this case */

	for (i = 0; i < metric_count; i++) {
		col = &row->cols[2+i];
		col->name = ldms_metric_name_get(set, metric_array[i]);
		col->type = ldms_metric_type_get(set, metric_array[i]);
		col->metric_id = metric_array[i];
		col->array_len = ldms_metric_array_get_len(set, metric_array[i]);
	}

	rc = csv_row_format_types_common(typeformat, file, fpath, sh, doudata,cps, set, row);
	free(row);
	CHECKERR(rc);

	return 0;
 error:
	return 1;
#undef CHECKERR
#undef PRINT_UDATA
}

int csv_row_format_header(FILE *file, const char *fpath,
		const struct csv_store_handle_common *sh, int doudata,
		struct csv_plugin_static *cps, ldms_set_t set,
		struct ldmsd_row_s *row,
		int time_format)
{
	if (!sh || !file || !fpath || !cps || !set)
		return EINVAL;
	FILE *fp = file;
	const char *pn = cps->pname;
	size_t i;
	int j;
	int ec;

#define CHECKERR(fprintfresult) \
	if (fprintfresult < 0) { \
		ovis_log(cps->mylog, OVIS_LERROR, "%s: Error %d writing to header '%s'\n", pn, \
		       fprintfresult, fpath); \
		ec = errno; \
		goto err; \
	}

	char *wsqt = ""; /* stub for ietfcsv */
	char *sep = "";
	if (sh->ietfcsv) {
		wsqt = "\"";
	}

	ec = fprintf(fp, "#");

	for (i = 0; i < row->col_count; i++, sep = ",") {
		struct ldmsd_col_s *col = &row->cols[i];
		const char* name = col->name;
		enum ldms_value_type metric_type = col->type;

		/* timestamp is a special case */
		switch (col->metric_id) {
		case LDMSD_PHONY_METRIC_ID_TIMESTAMP:
			/* Print timestamp header fields */
			if (time_format == TF_MILLISEC) {
				/* Alternate time format. First field is milliseconds-since-epoch,
				   and the second field is the left-over microseconds */
				ec = fprintf(fp, "%s%sTime_msec%s,%sTime_usec%s",sep,wsqt,wsqt,wsqt,wsqt);
			} else {
				/* Traditional time format, where the first field is
				   <seconds>.<microseconds>, second is microseconds repeated */
				ec = fprintf(fp, "%s%sTime%s,%sTime_usec%s",sep,wsqt,wsqt,wsqt,wsqt);
			}
			CHECKERR(ec);
			continue;
		case LDMSD_PHONY_METRIC_ID_PRODUCER:
		case LDMSD_PHONY_METRIC_ID_INSTANCE:
			/* phony metrics don't have udata */
			ec = fprintf(fp, "%s%s%s%s", sep, wsqt, name, wsqt);
			CHECKERR(ec);
			continue;
		}

		if (LDMS_V_RECORD_TYPE == metric_type)
			continue;
		if (LDMS_V_LIST == metric_type) {
			ldms_mval_t lh, lent;
			enum ldms_value_type mtype, prev_mtype;
			size_t cnt, prev_cnt;
			char n[100];

			lh = col->mval;
			lent = ldms_list_first(set, lh, &mtype, &cnt);
			if (!lent) {
				/*
				 * No list entry.
				 * We cannot determine the list entry's type.
				 */
				ec = EINTR;
				return ec;
			}
			/* Print list entry index header */
			snprintf(n, 100, "%s_entry_idx", name);
			ec = __primitive_metric_header(fp, sh, doudata, wsqt, n, LDMS_V_U64, 1, sep);
			CHECKERR(ec);

			/* Print list entries' column headers */
			if (LDMS_V_LIST == mtype) {
				/* We don't support list of lists */
				ec = ENOTSUP;
				return ec;
			} else if (LDMS_V_RECORD_INST == mtype) {
				enum ldms_value_type mt;
				size_t c;
				size_t rsz = ldms_record_card(lent);
				int rec_def = -1;
				for (j = 0; j < rsz; j++) {
					if (rec_def >= 0) {
						if (rec_def != ldms_record_type_get(lent)) {
							/* Different record types */
							ec = ENOTSUP;
							return ec;
						}
					}
					rec_def = ldms_record_type_get(lent);
					snprintf(n, 100, "%s_%s", name,
						ldms_record_metric_name_get(lent, j));
					mt = ldms_record_metric_type_get(lent, j, &c);
					ec = __primitive_metric_header(fp, sh, doudata,
								  wsqt, n, mt, c, sep);
					CHECKERR(ec);
				}
			} else {
				ec = __primitive_metric_header(fp, sh, doudata, wsqt,
							  name, mtype, cnt, sep);
				CHECKERR(ec);
			}
			do {
				/*
				 * Verify whether the list is supported or not.
				 */
				prev_mtype = mtype;
				prev_cnt = cnt;
				lent = ldms_list_next(set, lent, &mtype, &cnt);
				if (!lent)
					break;
				if ((prev_mtype != mtype) || (prev_cnt != cnt)) {
					ec = ENOTSUP;
					return ec;
				}
			} while (lent);
		} else {
			size_t cnt = 1;
			if (ldms_type_is_array(metric_type))
				cnt = col->array_len;
			ec = __primitive_metric_header(fp, sh, doudata, wsqt, name,
						  metric_type, cnt, sep);
			CHECKERR(ec);
		}
	}

	fprintf(fp, "\n");
	return 0;
 err:
	return ec;
}

int csv_format_header_common(FILE *file, const char *fpath, const struct csv_store_handle_common *sh, int doudata, struct csv_plugin_static *cps, ldms_set_t set, int *metric_array, size_t metric_count, int time_format)
{
	if (!sh || !file || !fpath || !cps || !set || !metric_array)
		return EINVAL;
	size_t i;
	int ec;

	struct ldmsd_row_s *row;
	struct ldmsd_col_s *col;

	row = calloc(1, sizeof(*row) + (2+metric_count) * sizeof(row->cols[0]));
	if (!row)
		return ENOMEM;
	row->col_count = 2 + metric_count;
	row->cols[0].name = "timestamp";
	row->cols[0].type = LDMS_V_U64;
	row->cols[0].metric_id = LDMSD_PHONY_METRIC_ID_TIMESTAMP;
	row->cols[0].array_len = 1;

	row->cols[1].name = "ProducerName";
	row->cols[1].type = LDMS_V_CHAR_ARRAY;
	row->cols[1].metric_id = LDMSD_PHONY_METRIC_ID_PRODUCER;
	row->cols[1].array_len = 16; /* can be anything, unused in this case */

	for (i = 0; i < metric_count; i++) {
		col = &row->cols[2+i];
		col->name = ldms_metric_name_get(set, metric_array[i]);
		col->type = ldms_metric_type_get(set, metric_array[i]);
		col->metric_id = metric_array[i];
		col->array_len = ldms_metric_array_get_len(set, metric_array[i]);
		col->mval = ldms_metric_get(set, metric_array[i]);
	}

	ec = csv_row_format_header(file, fpath, sh, doudata, cps, set, row, time_format);
	free(row);
	return ec;
}

/**
 * configurations for default (plugin name) and instances.
 */
int open_store_common(struct plugattr *pa, struct csv_store_handle_common *s_handle, struct csv_plugin_static *cps)
{
	if (!cps || !pa)
		return EINVAL;

	int rc = 0;
	const char *k = s_handle->store_key;
	int32_t cvt;
	bool r;

	/* -1 means do not change */
	s_handle->create_uid = (uid_t)-1;
	s_handle->create_gid = (gid_t)-1;
	s_handle->rename_uid = (uid_t)-1;
	s_handle->rename_gid = (gid_t)-1;

	r = false;
	cvt = ldmsd_plugattr_bool(pa, "ietfcsv", k, &r);
	if (cvt == -1) {
		ovis_log(cps->mylog, OVIS_LERROR, "%s:%s: ietfcsv cannot be parsed.\n", cps->pname, k);
		return EINVAL;
	}
	s_handle->ietfcsv = r;

	r = false;
	cvt = ldmsd_plugattr_bool(pa, "altheader", k, &r);
	if (cvt == -1) {
		ovis_log(cps->mylog, OVIS_LERROR,"open_store_common altheader= cannot be parsed\n");
		return EINVAL;
	}
	s_handle->altheader = r;

	uint32_t th = 0;
	cvt = ldmsd_plugattr_u32(pa, "typeheader", k, &th);
	if (cvt && cvt != ENOKEY) {
		ovis_log(cps->mylog, OVIS_LERROR,"open_store_common typeheader= cannot be parsed\n");
		return EINVAL;
	}
	if (th > TH_MAX) {
		ovis_log(cps->mylog, OVIS_LERROR,"open_store_common typeheader=%u too large\n", th);
		return EINVAL;
	}
	s_handle->typeheader = th;

	uint32_t time_format = 0;
	cvt = ldmsd_plugattr_u32(pa, "time_format", k, &time_format);
	if (cvt && cvt != ENOKEY) {
		ovis_log(cps->mylog, OVIS_LERROR,"open_store_common time_format= cannot be parsed\n");
		return EINVAL;
	}
	if (time_format > TF_MAX) {
		ovis_log(cps->mylog, OVIS_LERROR,"open_store_common time_format=%u too large\n", time_format);
		return EINVAL;
	}
	s_handle->time_format = time_format;

	const char *rename_template =
		ldmsd_plugattr_value(pa, "rename_template", k);
	if (rename_template && strlen(rename_template) >= 2 ) {
		char *tmp1 = strdup(rename_template);
		if (!tmp1) {
			rc = ENOMEM;
			return rc;
		} else {
			s_handle->rename_template = tmp1;
		}
	} else {
		if (rename_template) {
			ovis_log(cps->mylog, OVIS_LERROR, "%s: rename_template "
				"must be specificed correctly. "
				"got instead %s\n", cps->pname,
				rename_template ) ;
			rc = EINVAL;
			return rc;
		}
	}

	int32_t uid, gid;
	cvt = ldmsd_plugattr_s32(pa, "rename_uid", k, &uid);
	if (!cvt) {
		if (uid >= 0)
			s_handle->rename_uid = (uid_t)uid;
		else
			cvt = ERANGE;
	}
	if (cvt == ERANGE || cvt ==  ENOTSUP) {
		rc = cvt;
		s_handle->rename_uid = (uid_t)-1;
		ovis_log(cps->mylog, OVIS_LERROR,
			"%s %s: open_store_common rename_uid= out of range\n",
			cps->pname, k);
		return rc;
	}

	cvt = ldmsd_plugattr_s32(pa, "rename_gid", k, &gid);
	if (!cvt) {
	       	if (gid >= 0)
			s_handle->rename_gid = (uid_t)gid;
		else
			cvt = ERANGE;
	}
	if (cvt == ERANGE || cvt ==  ENOTSUP) {
		rc = cvt;
		s_handle->rename_gid = (gid_t)-1;
		ovis_log(cps->mylog, OVIS_LERROR,
			"%s %s: open_store_common rename_gid= out of range\n",
			cps->pname, k);
		return rc;
	}

	const char * rename_pval = ldmsd_plugattr_value(pa, "rename_perm", k);
	if (rename_pval) {
		int perm = strtol(rename_pval, NULL, 8);
		if (perm < 1 || perm > 04777) {
			rc = EINVAL;
			s_handle->rename_perm = 0;
			ovis_log(cps->mylog, OVIS_LERROR,
				"%s %s: open_store_common ignoring bad rename_perm=%s\n",
				cps->pname, k, rename_pval);
			return rc;
		} else {
			s_handle->rename_perm = perm;
		}
	}

	cvt = ldmsd_plugattr_s32(pa, "create_uid", k, &uid);
	if (!cvt) {
		if (uid >= 0)
			s_handle->create_uid = (uid_t)uid;
		else
			cvt = ERANGE;
	}
	if (cvt == ERANGE || cvt ==  ENOTSUP) {
		rc = cvt;
		s_handle->create_uid = (uid_t)-1;
		ovis_log(cps->mylog, OVIS_LERROR,
			"%s %s: open_store_common create_uid= out of range\n",
			cps->pname, k);
		return rc;
	}

	cvt = ldmsd_plugattr_s32(pa, "create_gid", k, &gid);
	if (!cvt) {
		if (gid >= 0)
			s_handle->create_gid = (uid_t)gid;
		else
			cvt = ERANGE;
	}
	if (cvt == ERANGE || cvt ==  ENOTSUP) {
		rc = cvt;
		s_handle->create_gid = (gid_t)-1;
		ovis_log(cps->mylog, OVIS_LERROR,
			"%s %s: open_store_common create_gid= out of range\n",
			cps->pname, k);
		return rc;
	}

	const char * create_pval = ldmsd_plugattr_value(pa, "create_perm", k);
	if (create_pval) {
		int perm = strtol(create_pval, NULL, 8);
		if (perm < 1 || perm > 04777) {
			rc = EINVAL;
			s_handle->create_perm = 0;
			ovis_log(cps->mylog, OVIS_LERROR,
				"%s %s: open_store_common ignoring bad create_perm=%s\n",
				cps->pname, k, create_pval);
			return rc;
		} else {
			s_handle->create_perm = perm;
		}
	}

	const char *value = ldmsd_plugattr_value(pa, "buffer", k);
	const char *bvalue = ldmsd_plugattr_value(pa, "buffertype", k);
	int buf = 1, buft = 0;
	rc = config_buffer(value, bvalue, &buf, &buft, k);
	if (rc) {
		return rc;
	}
	s_handle->buffer_sz = buf;
	s_handle->buffer_type = buft;
	s_handle->otime = time(NULL);

	return rc;
}

void close_store_common(struct csv_store_handle_common *s_handle, struct csv_plugin_static *cps) {
	if (!s_handle || !cps) {
		ovis_log(cps->mylog, OVIS_LERROR,
			"%s: close_store_common with null argument\n",
			cps->pname);
		return;
	}

	rename_output(s_handle->filename, FTYPE_DATA, s_handle, cps);
	if (s_handle->altheader)
		rename_output(s_handle->headerfilename, FTYPE_HDR, s_handle, cps);
	if (s_handle->typeheader)
		rename_output(s_handle->typefilename, FTYPE_KIND, s_handle, cps);
	replace_string(&(s_handle->filename), NULL);
	replace_string(&(s_handle->headerfilename),  NULL);
	replace_string(&(s_handle->typefilename),  NULL);
	free(s_handle->rename_template);
	s_handle->rename_template = NULL;
}

void print_csv_plugin_common(struct csv_plugin_static *cps)
{
	ovis_log(cps->mylog, OVIS_LALWAYS, "%s:\n", cps->pname);
}

void print_csv_store_handle_common(struct csv_store_handle_common *h, struct csv_plugin_static *p)
{
	if (!p)
		return;
	if (!h) {
		ovis_log(p->mylog, OVIS_LALWAYS, "csv store handle dump: NULL handle.\n");
		return;
	}
	ovis_log(p->mylog, OVIS_LALWAYS, "%s handle dump:\n", p->pname);
	ovis_log(p->mylog, OVIS_LALWAYS, "%s: filename: %s\n", p->pname, h->filename);
	ovis_log(p->mylog, OVIS_LALWAYS, "%s: headerfilename: %s\n", p->pname, h->headerfilename);
	ovis_log(p->mylog, OVIS_LALWAYS, "%s: typefilename: %s\n", p->pname, h->typefilename);
	ovis_log(p->mylog, OVIS_LALWAYS, "%s: altheader:%s\n", p->pname, h->altheader ?
			                "true" : "false");
	ovis_log(p->mylog, OVIS_LALWAYS, "%s: typeheader:%d\n", p->pname, h->typeheader);
	ovis_log(p->mylog, OVIS_LALWAYS, "%s: time_format:%d\n", p->pname, h->time_format);
	ovis_log(p->mylog, OVIS_LALWAYS, "%s: buffertype: %d\n", p->pname, h->buffer_type);
	ovis_log(p->mylog, OVIS_LALWAYS, "%s: buffer: %d\n", p->pname, h->buffer_sz);
	ovis_log(p->mylog, OVIS_LALWAYS, "%s: rename_template:%s\n", p->pname, h->rename_template);
	ovis_log(p->mylog, OVIS_LALWAYS, "%s: rename_uid: %" PRIu32 "\n", p->pname, h->rename_uid);
	ovis_log(p->mylog, OVIS_LALWAYS, "%s: rename_gid: %" PRIu32 "\n", p->pname, h->rename_gid);
	ovis_log(p->mylog, OVIS_LALWAYS, "%s: rename_perm: %o\n", p->pname, h->rename_perm);
	ovis_log(p->mylog, OVIS_LALWAYS, "%s: create_uid: %" PRIu32 "\n", p->pname, h->create_uid);
	ovis_log(p->mylog, OVIS_LALWAYS, "%s: create_gid: %" PRIu32 "\n", p->pname, h->create_gid);
	ovis_log(p->mylog, OVIS_LALWAYS, "%s: create_perm: %o\n", p->pname, h->create_perm);
}
