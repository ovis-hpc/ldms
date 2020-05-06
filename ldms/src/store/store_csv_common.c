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


/** parse an option of the form name=bool where bool is
 * lead with tfyn01 or uppercase versions of same or value
 * string is "", so assume user meant name=true and absence of name
 * in options defaults to false.
 */
int parse_bool2(ldmsd_msg_log_f mlg, struct attr_value_list *avl,
		const char *name, bool *bval, const char *src)
{
	if (!avl || !name || !bval)
		return EINVAL;
	const char *val = av_value(avl, name);
	if (val) {
		switch (val[0]) {
		case '1':
		case 't':
		case 'T':
		case 'y':
		case 'Y':
		case '\0':
			*bval = true;
			break;
		case '0':
		case 'f':
		case 'F':
		case 'n':
		case 'N':
			*bval = false;
			break;
		default:
			if (mlg)
				mlg(LDMSD_LERROR, "%s: bad %s=%s\n",
					(src ? src : ""), name, val);
			return EINVAL;
		}
	}
	return 0;
}

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

/* get a handle to the onp we should use. return NULL only if no notify configured anywhere. */
static struct ovis_notification **get_onph(struct csv_store_handle_common *s_handle,
	struct csv_plugin_static *cps)
{
	if (!s_handle->notify)
		return NULL;
	uint32_t wto = 6000;
	unsigned mqs = 1000;
	unsigned retry = 10;
	mode_t perm = 0700;
	/* init if unopened */
	if (s_handle->notify && s_handle->onp == NULL) {
		bool fifo = s_handle->notify_isfifo;
		s_handle->onp = ovis_notification_open(s_handle->notify,
				wto, mqs, retry,
				(ovis_notification_log_fn)cps->msglog,
				perm, fifo);
		if (s_handle->onp)
			cps->msglog(LDMSD_LDEBUG,"Created onp %s\n",
				s_handle->notify);
		else
			cps->msglog(LDMSD_LDEBUG,"Create fail for sh.onp %s\n",
				s_handle->notify);
	}
	if (s_handle->onp)
		return &(s_handle->onp);
	else
		return NULL;
}

void notify_output(const char *event, const char *name, const char *ftype,
	struct csv_store_handle_common *s_handle, struct csv_plugin_static *cps) {
	if (!cps)
		return;
	if (s_handle && !s_handle->notify)
		return;
	const char *container = s_handle->container;
	const char *schema = s_handle->schema;
	if (!event || !name || !ftype || !s_handle) {
		cps->msglog(LDMSD_LDEBUG,"Invalid argument in notify_output"
				"(%s, %s, %s, %p, %p)\n",
				event ? event : "missing event",
				name ? name : "missing name",
				ftype ? ftype : "missing ftype",
				container ? container : "missing container",
				schema ? schema : "missing schema",
				s_handle, cps);
		return;
	}
	struct ovis_notification **onph = get_onph(s_handle, cps);
	if (! onph || ! *onph) {
		cps->msglog(LDMSD_LDEBUG,"onp not set in handle or cps\n");
		return;
	}

	int *hcp = (NULL != s_handle->onp) ? &(s_handle->hooks_closed) :
						&(cps->hooks_closed);
	char *msg;
	if (*hcp) {
		cps->msglog(LDMSD_LINFO, "Request by storecsv with output closed: %s\n",
			s_handle->notify);
		return;
	}
	dsinit(ds);
	dscat(ds,event);
	dscat(ds," ");
	dscat(ds,cps->pname);
	dscat(ds," ");
	dscat(ds,container);
	dscat(ds," ");
	dscat(ds,schema);
	dscat(ds," ");
	dscat(ds,ftype);
	dscat(ds," ");
	dscat(ds,name);
	msg = dsdone(ds);
	if (!msg) {
		cps->msglog(LDMSD_LERROR,
			"Out of memory in notify_output for %s\n",name);
		return;
	}
	int rc = ovis_notification_add(*onph, msg);
	switch (rc) {
	case 0:
		cps->msglog(LDMSD_LDEBUG,"Notification of %s\n", msg);
		break;
	case EINVAL:
		cps->msglog(LDMSD_LERROR,"Notification error by %s for %s: %s\n",
			cps->pname, name, msg);
		break;
	case ESHUTDOWN:
		cps->msglog(LDMSD_LERROR,"Disconnected output detected. Closing.\n");
		ovis_notification_close(*onph);
		*hcp = 1;
		*onph = NULL;
		break;
	default:
		cps->msglog(LDMSD_LERROR,"Unexpected error type %d in notify_spool\n",rc);
	}
	free(msg);
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
		cps->msglog(LDMSD_LERROR, "%s: rename_output: unsupported character %c in template use of env(%s): %s\n",
			cps->pname, *b, var, val);
	return rc;
}

int create_outdir(const char *path, struct csv_store_handle_common *s_handle,
	struct csv_plugin_static *cps) {
#define EBSIZE 512
	char errbuf[EBSIZE];
	if (!cps) {
		return EINVAL;
	}
	if (!s_handle) {
		cps->msglog(LDMSD_LERROR,"create_outdir: NULL store handle received.\n");
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
	/* cps->msglog(LDMSD_LDEBUG,"f_mkdir_p %o %s\n", (int)mode, path); */
	err = f_mkdir_p(path, mode);
	if (err) {
		err = errno;
		switch (err) {
		case EEXIST:
			break;
		default:
			strerror_r(err, errbuf, EBSIZE);
			cps->msglog(LDMSD_LERROR,"create_outdir: failed to create directory for %s: %s\n",
				path, errbuf);
			return err;
		}
	}

	/* cps->msglog(LDMSD_LDEBUG,"create_outdir: f_mkdir+p(%s, %o)\n", path, mode); */
	return 0;
#undef EBSIZE
}

void rename_output(const char *name,
	const char *ftype, struct csv_store_handle_common *s_handle,
	struct csv_plugin_static *cps) {
#define EBSIZE 512
	char errbuf[EBSIZE];
	if (!cps) {
		return;
	}
	if (!s_handle) {
		cps->msglog(LDMSD_LERROR,"rename_output: NULL store handle received.\n");
		return;
	}
	const char *container = s_handle->container;
	const char *schema = s_handle->schema;
	if (s_handle && !s_handle->rename_template)
		return;
	char *rt = s_handle->rename_template;
	if (!rt || !name || !ftype || !container || !schema) {
		cps->msglog(LDMSD_LDEBUG,"Invalid argument in rename_output"
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
			strerror_r(rc, errbuf, EBSIZE);
			cps->msglog(LDMSD_LERROR,"%s: rename_output: unable to chmod(%s,%o): %s.\n",
				cps->pname, name, s_handle->rename_perm, errbuf);
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
			strerror_r(rc, errbuf, EBSIZE);
			cps->msglog(LDMSD_LERROR,"%s: rename_output: unable to chown(%s, %u, %u): %s.\n",
				cps->pname, name, newuid, newgid, errbuf);
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
				cps->msglog(LDMSD_LERROR,"%s: rename_output: ENOMEM\n", cps->pname);
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
				cps->msglog(LDMSD_LERROR,"%s: rename_output: ENOMEM\n", cps->pname);
				dstr_free(&ds);
				return;
			}
			break;
		case '{':
			head = end + 2;
			char *vend = strchr(head,'}');
			if (!vend) {
				cps->msglog(LDMSD_LERROR,
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
					cps->msglog(LDMSD_LDEBUG,
						"%s: rename_output: getenv(%s) = %s\n", cps->pname, var, val);
					*/
					if (validate_env(var, val, cps)) {
						dstr_free(&ds);
						cps->msglog(LDMSD_LERROR,
							"%s: rename_output: rename cancelled\n",
							cps->pname);
						return;
					}
					dscat(ds, val);
				} else {
					cps->msglog(LDMSD_LDEBUG,
						"%s: rename_output: empty %%{%s}\n",
						cps->pname, var);
				}
			}
			break;
		case 's':
			head = end + 2;
			char *dot = strrchr(name,'.');
			if (!dot) {
				cps->msglog(LDMSD_LERROR,"%s: rename_output: no timestamp\n", cps->pname);
				dstr_free(&ds);
				return;
			}
			dot = dot + 1;
			char *num = dot;
			while (isdigit(*num)) {
				num++;
			}
			if (*num != '\0') {
				cps->msglog(LDMSD_LERROR,"%s: rename_output: no timestamp at end\n", cps->pname);
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
		cps->msglog(LDMSD_LERROR,"%s: rename_output: failed to create new filename for %s\n",
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
	/* cps->msglog(LDMSD_LDEBUG,"f_mkdir_p %o %s\n", (int)mode, ndname); */
	err = f_mkdir_p(ndname, mode);
	free(namedup);
	if (err) {
		err = errno;
		switch (err) {
		case EEXIST:
			break;
		default:
			strerror_r(err, errbuf, EBSIZE);
			cps->msglog(LDMSD_LERROR, "%s: rename_output: failed to create directory for %s: %s\n",
				cps->pname, newname, errbuf);
			free(newname);
			return;
		}

	}

	cps->msglog(LDMSD_LDEBUG, "%s: rename_output: rename(%s, %s)\n",
		cps->pname, name, newname);
	err = rename(name, newname);
	if (err) {
		int ec = errno;
		if (ec != ENOENT) {
			strerror_r(ec, errbuf, EBSIZE);
			cps->msglog(LDMSD_LERROR,"%s: rename_output: failed rename(%s, %s): %s\n",
				cps->pname, name, newname, errbuf);
		}
		/* enoent happens if altheader = 0 or typeheader = 0 */
	}
	free(newname);
#undef EBSIZE
}

void ch_output(FILE *f, const char *name,
	struct csv_store_handle_common *s_handle,
	struct csv_plugin_static *cps) {
#define EBSIZE 512
	char errbuf[EBSIZE];
	if (!cps) {
		return;
	}
	if (!s_handle) {
		cps->msglog(LDMSD_LERROR,"ch_output: NULL store handle received.\n");
		return;
	}
	if (!f) {
		cps->msglog(LDMSD_LERROR,"ch_output: NULL FILE pointer received.\n");
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
			strerror_r(rc, errbuf, EBSIZE);
			cps->msglog(LDMSD_LERROR,"ch_output: unable to chmod(%s,%o): %s.\n",
				name, s_handle->create_perm, errbuf);
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
			strerror_r(rc, errbuf, EBSIZE);
			cps->msglog(LDMSD_LERROR,"ch_output: unable to fchown(%d, (%s),%lu, %lu): %s.\n",
				fd, name, newuid, newgid, errbuf);
		}
		cps->msglog(LDMSD_LDEBUG,"ch_output: fchown(%d, (%s),%lu, %lu): %s.\n",
			fd, name, newuid, newgid, errbuf);
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
		ldmsd_log(LDMSD_LERROR,
		       "%s: config_buffer: bad arguments\n", __FILE__);
		return EINVAL;
	}

	if (!bs && !bt){
		*rbs = 1;
		*rbt = 0;
		return 0;
	}

	if (!bs && bt){
		ldmsd_log(LDMSD_LERROR,
		       "%s: Cannot have buffer type without buffer for %s\n",
		       __FILE__, k);
		return EINVAL;
	}

	tempbs = atoi(bs);
	if (tempbs < 0){
		ldmsd_log(LDMSD_LERROR,
		       "%s: Bad val for buffer %d of %s\n",
		       __FILE__, tempbs, k);
		return EINVAL;
	}
	if ((tempbs == 0) || (tempbs == 1)){
		if (bt){
			ldmsd_log(LDMSD_LERROR,
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
		ldmsd_log(LDMSD_LERROR,
		       "%s: Cannot have buffer size with no buffer type for %s\n",
		       __FILE__,  k);
		return EINVAL;
	}

	tempbt = atoi(bt);
	if ((tempbt != 3) && (tempbt != 4)){
		ldmsd_log(LDMSD_LERROR, "%s: Invalid buffer type %d for %s\n",
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


int csv_format_types_common(int typeformat, FILE* file, const char *fpath, const struct csv_store_handle_common *sh, int doudata, struct csv_plugin_static *cps, ldms_set_t set, int *metric_array, size_t metric_count)
{
	if (typeformat < 1)
		return 0;
	if (!sh || !file || !fpath || !cps || !set || !metric_array)
		return EINVAL;
	const char *pn = cps->pname;
	uint32_t len;
	size_t i;
	int j;
	int rc;
	const char *u64str = ldms_metric_type_to_str(LDMS_V_U64);
	const char *castr = ldms_metric_type_to_str(LDMS_V_CHAR_ARRAY);

#define CHECKERR(fprintfresult) \
	if (fprintfresult < 0) { \
		cps->msglog(LDMSD_LERROR, "%s: Error %d writing to type header '%s'\n", pn, \
		       fprintfresult, fpath); \
		goto error; \
	}

#define PRINT_UDATA \
	if (doudata) { \
		rc = fprintf(file, ",%s", u64str); \
		CHECKERR(rc); \
	}

	const char *ud = "-udata";
	if (!doudata) {
		ud = "";
	}
	switch (typeformat) {
	case TH_UNROLL:
		rc = fprintf(file, "#!ldms-kinds%s!%s,%s,%s%d", ud, "timestamp",
				u64str, castr, LDMS_PRODUCER_NAME_MAX);
		break;
	case TH_ARRAY:
		rc = fprintf(file, "#!ldms-array-kinds%s!%s,%s,%s%d",
				ud, "timestamp", u64str, castr,
				LDMS_PRODUCER_NAME_MAX);
		break;
	default:
		rc = EINVAL;
		break;
	}
	CHECKERR(rc);

	const char *mt;
	for (i = 0; i < metric_count; i++) {
		int m = metric_array[i];
		enum ldms_value_type met_type = ldms_metric_type_get(set, m);
		enum ldms_value_type base_type =
			ldms_metric_type_to_scalar_type(met_type);
		/* unroll arrays, except strings */
		if (TH_UNROLL == typeformat) {
			if ( met_type != LDMS_V_CHAR_ARRAY) {
				len = ldms_metric_array_get_len(set, m);
				for (j = 0; j < len; j++) {
					PRINT_UDATA;
					mt = ldms_metric_type_to_str(base_type);
					rc = fprintf(file,",%s", mt);
					CHECKERR(rc);
				}
			} else {
				PRINT_UDATA;
				mt = ldms_metric_type_to_str(met_type);
				rc = fprintf(file,",%s", mt);
				CHECKERR(rc);
			}
		}
		/*  S32[]$len */
		if (TH_ARRAY == typeformat) {
			len = ldms_metric_array_get_len(set, m);
			PRINT_UDATA;
			mt = ldms_metric_type_to_str(met_type);
			if (met_type != base_type)
				rc = fprintf(file,",%s%d", mt, len);
			else
				rc = fprintf(file,",%s", mt);
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

int csv_format_header_common(FILE *file, const char *fpath, const struct csv_store_handle_common *sh, int doudata, struct csv_plugin_static *cps, ldms_set_t set, int *metric_array, size_t metric_count)
{
	if (!sh || !file || !fpath || !cps || !set || !metric_array)
		return EINVAL;
	FILE *fp = file;
	const char *pn = cps->pname;
	size_t i;
	int j;
	uint32_t len;
	int ec;

#define CHECKERR(fprintfresult) \
	if (fprintfresult < 0) { \
		cps->msglog(LDMSD_LERROR, "%s: Error %d writing to header '%s'\n", pn, \
		       fprintfresult, fpath); \
		ec = errno; \
		goto err; \
	}

	char *wsqt = ""; /* stub for ietfcsv */
	if (sh->ietfcsv) {
		wsqt = "\"";
	}

	/* This allows optional loading a float (Time) into an int field and
	   retaining usec as a separate field */
	ec = fprintf(fp, "#%sTime%s,%sTime_usec%s,%sProducerName%s",wsqt,wsqt,wsqt,wsqt,wsqt,wsqt);
	CHECKERR(ec);

	for (i = 0; i < metric_count; i++) {
		const char* name = ldms_metric_name_get(set, metric_array[i]);
		enum ldms_value_type metric_type = ldms_metric_type_get(set, metric_array[i]);

		/* use same formats as ldms_ls */
		switch (metric_type) {
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
			len = ldms_metric_array_get_len(set, metric_array[i]);
			if (doudata) {
				for (j = 0; j < len; j++) { // only 1 name for all of them.
					ec = fprintf(fp, ",%s%s%d.userdata%s,%s%s%d.value%s",
						wsqt, name, j, wsqt, wsqt, name, j, wsqt);
					CHECKERR(ec);
				}
			} else {
				for (j = 0; j < len; j++) { // only 1 name for all of them.
					ec = fprintf(fp, ",%s%s%d%s", wsqt, name, j, wsqt);
					CHECKERR(ec);
				}
			}
			break;
		default:
			if (doudata) {
				ec = fprintf(fp, ",%s%s.userdata%s,%s%s.value%s",
					wsqt, name, wsqt, wsqt, name, wsqt);
				CHECKERR(ec);
			} else {
				ec = fprintf(fp, ",%s%s%s", wsqt, name, wsqt);
				CHECKERR(ec);
			}
			break;
		}
	}

	fprintf(fp, "\n");
	return 0;
 err:
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
	const char *notify;
	notify = ldmsd_plugattr_value(pa, "notify", k);
	if (notify && strlen(notify) >= 2 ) {
		char *tmp1 = strdup(notify);
		if (!tmp1) {
			rc = ENOMEM;
			return rc;
		} else {
			s_handle->notify = tmp1;
		}
	} else {
		if (notify) {
			cps->msglog(LDMSD_LERROR, "%s %s: notify "
				"must be specificed correctly. "
				"got instead %s\n", cps->pname, k, notify);
			rc = EINVAL;
			return rc;
		}
	}
	int32_t cvt;
	bool r;
	if (!rc) {
		r = false;
		cvt = ldmsd_plugattr_bool(pa, "notify_isfifo", k, &r);
		if (cvt == -1) {
			cps->msglog(LDMSD_LERROR, "%s:%s: notify_isfifo cannot be parsed.\n", cps->pname, k);
			return EINVAL;
		}
		s_handle->notify_isfifo = r;
	}

	/* -1 means do not change */
	s_handle->create_uid = (uid_t)-1;
	s_handle->create_gid = (gid_t)-1;
	s_handle->rename_uid = (uid_t)-1;
	s_handle->rename_gid = (gid_t)-1;

	r = false;
	cvt = ldmsd_plugattr_bool(pa, "ietfcsv", k, &r);
	if (cvt == -1) {
		cps->msglog(LDMSD_LERROR, "%s:%s: ietfcsv cannot be parsed.\n", cps->pname, k);
		return EINVAL;
	}
	s_handle->ietfcsv = r;

	r = false;
	cvt = ldmsd_plugattr_bool(pa, "altheader", k, &r);
	if (cvt == -1) {
		cps->msglog(LDMSD_LERROR,"open_store_common altheader= cannot be parsed\n");
		return EINVAL;
	}
	s_handle->altheader = r;

	uint32_t th = 0;
	cvt = ldmsd_plugattr_u32(pa, "typeheader", k, &th);
	if (cvt && cvt != ENOKEY) {
		cps->msglog(LDMSD_LERROR,"open_store_common typeheader= cannot be parsed\n");
		return EINVAL;
	}
	if (th > TH_MAX) {
		cps->msglog(LDMSD_LERROR,"open_store_common typeheader=%u too large\n", th);
		return EINVAL;
	}
	s_handle->typeheader = th;

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
			cps->msglog(LDMSD_LERROR, "%s: rename_template "
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
		cps->msglog(LDMSD_LERROR,
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
		cps->msglog(LDMSD_LERROR,
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
			cps->msglog(LDMSD_LERROR,
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
		cps->msglog(LDMSD_LERROR,
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
		cps->msglog(LDMSD_LERROR,
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
			cps->msglog(LDMSD_LERROR,
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
		cps->msglog(LDMSD_LERROR,
			"%s: close_store_common with null argument\n",
			cps->pname);
		return;
	}

	notify_output(NOTE_CLOSE, s_handle->filename, NOTE_DAT, s_handle, cps);
	notify_output(NOTE_CLOSE, s_handle->headerfilename, NOTE_HDR, s_handle,
		cps);
	rename_output(s_handle->filename, NOTE_DAT, s_handle, cps);
	rename_output(s_handle->headerfilename, NOTE_HDR, s_handle, cps);
	rename_output(s_handle->typefilename, NOTE_KIND, s_handle, cps);
	replace_string(&(s_handle->filename), NULL);
	replace_string(&(s_handle->headerfilename),  NULL);
	replace_string(&(s_handle->typefilename),  NULL);
	/* free(s_handle->notify); skip. handle notify is always copy of pg or sk notify or null */
	s_handle->notify = NULL;
	free(s_handle->rename_template);
	s_handle->rename_template = NULL;
}

void print_csv_plugin_common(struct csv_plugin_static *cps)
{
	cps->msglog(LDMSD_LALL, "%s: onp: %p\n", cps->pname, cps->onp);
}

void print_csv_store_handle_common(struct csv_store_handle_common *h, struct csv_plugin_static *p)
{
	if (!p)
		return;
	if (!h) {
		p->msglog(LDMSD_LALL, "csv store handle dump: NULL handle.\n");
		return;
	}
	p->msglog(LDMSD_LALL, "%s handle dump:\n", p->pname);
	p->msglog(LDMSD_LALL, "%s: filename: %s\n", p->pname, h->filename);
	p->msglog(LDMSD_LALL, "%s: headerfilename: %s\n", p->pname, h->headerfilename);
	p->msglog(LDMSD_LALL, "%s: typefilename: %s\n", p->pname, h->typefilename);
	p->msglog(LDMSD_LALL, "%s: notify:%s\n", p->pname, h->notify);
	p->msglog(LDMSD_LALL, "%s: notify_isfifo:%s\n", p->pname, h->notify_isfifo ?
			                "true" : "false");
	p->msglog(LDMSD_LALL, "%s: altheader:%s\n", p->pname, h->altheader ?
			                "true" : "false");
	p->msglog(LDMSD_LALL, "%s: typeheader:%d\n", p->pname, h->typeheader);
	p->msglog(LDMSD_LALL, "%s: buffertype: %d\n", p->pname, h->buffer_type);
	p->msglog(LDMSD_LALL, "%s: buffer: %d\n", p->pname, h->buffer_sz);
	p->msglog(LDMSD_LALL, "%s: rename_template:%s\n", p->pname, h->rename_template);
	p->msglog(LDMSD_LALL, "%s: rename_uid: %" PRIu32 "\n", p->pname, h->rename_uid);
	p->msglog(LDMSD_LALL, "%s: rename_gid: %" PRIu32 "\n", p->pname, h->rename_gid);
	p->msglog(LDMSD_LALL, "%s: rename_perm: %o\n", p->pname, h->rename_perm);
	p->msglog(LDMSD_LALL, "%s: create_uid: %" PRIu32 "\n", p->pname, h->create_uid);
	p->msglog(LDMSD_LALL, "%s: create_gid: %" PRIu32 "\n", p->pname, h->create_gid);
	p->msglog(LDMSD_LALL, "%s: create_perm: %o\n", p->pname, h->create_perm);
	p->msglog(LDMSD_LALL, "%s: onp: %p\n", p->pname, h->onp);
}
