/*
 * Copyright (c) 2013-2019,2022 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2013-2019,2022 Open Grid Computing, Inc. All rights reserved.
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

#include <ctype.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <linux/limits.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <coll/idx.h>
#include <coll/rbt.h>
#include <assert.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_plugattr.h"
#include "store_common.h"
#include "store_csv_common.h"

#define TV_SEC_COL    0
#define TV_USEC_COL    1
#define GROUP_COL    2
#define VALUE_COL    3

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))
#endif


#define PNAME "store_csv"

static idx_t store_idx; /* protected by cfg_lock */
struct plugattr *pa = NULL; /* plugin attributes from config */
static int rollover;
static int rollagain;
/** rollempty determines if timed rollovers are performed even
 * when no data has been written (producing empty files).
 */
static bool rollempty = true;
/** rolltype determines how to interpret rollover values > 0. */
static int rolltype = -1;
/** ROLLTYPES documents rolltype and is used in help output. Also used for buffering */
#define ROLLTYPES \
"                     1: wake approximately every rollover seconds and roll.\n" \
"                     2: wake daily at rollover seconds after midnight (>=0) and roll.\n" \
"                     3: roll after approximately rollover records are written.\n" \
"                     4: roll after approximately rollover bytes are written.\n" \
"                     5: wake daily at rollover seconds after midnight and every rollagain seconds thereafter.\n"

#define MAXROLLTYPE 5
#define MINROLLTYPE 1
/** default -- do not roll */
#define DEFAULT_ROLLTYPE -1
/** minimum rollover for type 1;
    rolltype==1 and rollover < MIN_ROLL_1 -> rollover = MIN_ROLL_1
    also used for minimum sleep time for type 2;
    rolltype==2 and rollover results in sleep < MIN_ROLL_SLEEPTIME -> skip this roll and do it the next day */
#define MIN_ROLL_1 10
/** minimum rollover for type 3;
    rolltype==3 and rollover < MIN_ROLL_RECORDS -> rollover = MIN_ROLL_RECORDS */
#define MIN_ROLL_RECORDS 3
/** minimum rollover for type 4;
    rolltype==4 and rollover < MIN_ROLL_BYTES -> rollover = MIN_ROLL_BYTES */
#define MIN_ROLL_BYTES 1024
/** Interval to check for passing the record or byte count limits. */
#define ROLL_LIMIT_INTERVAL 60


__attribute__(( format(printf, 2, 3) ))
static ldmsd_msg_log_f msglog;
static pthread_t rothread;
static int rothread_used = 0;

#define ERR_LOG(FMT, ...) do { \
		msglog(LDMSD_LERROR, PNAME ": " FMT, ## __VA_ARGS__); \
		assert(0 == "DEBUG"); \
	} while(0)

#define INFO_LOG(FMT, ...) do { \
		msglog(LDMSD_LINFO, PNAME ": " FMT, ## __VA_ARGS__); \
	} while(0)

#define _stringify(_x) #_x
#define stringify(_x) _stringify(_x)

#define LOGFILE "/var/log/store_csv.log"

struct csv_lent {
	ldms_mval_t mval; /* list entry value */
	uint64_t udata; /* User data */
	enum ldms_value_type mtype; /* list entry's value type */
	size_t count; /* array length if the value type is an array. */
	int idx; /* list entry's index */
};

typedef enum csv_store_handle_type {
	CSV_STORE_HANDLE,     /* for `struct csv_store_handle`     */
	CSV_ROW_STORE_HANDLE, /* for `struct csv_row_store_handle` */
} csv_store_handle_type_t;

/*
 * New in v3: no more id_pos. Always producer name which is always written out before the metrics.
 */

struct csv_store_handle {
	csv_store_handle_type_t type;
	char *path;
	FILE *file;
	FILE *headerfile;
	printheader_t printheader;
	int udata;
	pthread_mutex_t lock;
	void *ucontext;
	bool conflict_warned;
	int64_t lastflush;
	int64_t store_count;
	int64_t byte_count;
	int num_lists; /* Number of list metrics */
	struct csv_lent *lents;
	int ref_count; /* number of strgp using the csv file; protected by cfg_lock */
	CSV_STORE_HANDLE_COMMON;
};

struct csv_row_schema_key_s {
	const struct ldms_digest_s *digest; /* row schema digest */
	const char *name; /* row schema name */
};

int csv_row_schema_key_cmp(void *tree_key, const void *key)
{
	int ret;
	const struct csv_row_schema_key_s *tk = tree_key, *k =key;
	ret = memcmp(tk->digest, k->digest, sizeof(*tk->digest));
	if (ret)
		return ret;
	return strcmp(tk->name, k->name);
}

struct csv_row_schema_rbn_s {
	struct rbn rbn;
	struct csv_row_schema_key_s key;
	struct ldms_digest_s digest;
	char name[128];
	struct csv_store_handle *s_handle;
};

/* This is `strgp->store_handle` in the new decomposition path. */
struct csv_row_store_handle {
	csv_store_handle_type_t type;
	struct rbt row_schema_rbt;
};

static pthread_mutex_t cfg_lock;

static char* allocStoreKey(const char* container, const char* schema){

  if ((container == NULL) || (schema == NULL) ||
      (strlen(container) == 0) ||
      (strlen(schema) == 0)){
    msglog(LDMSD_LERROR, PNAME ": container or schema null or empty. cannot create key\n");
    return NULL;
  }

  size_t pathlen = strlen(schema) + strlen(container) + 8;
  char* path = malloc(pathlen);
  if (!path)
    return NULL;

  snprintf(path, pathlen, "%s/%s", container, schema);
  return path;
}


struct roll_cb_arg {
	struct csv_plugin_static *cps;
	time_t appx;
};

static void roll_cb(void *obj, void *cb_arg)
{
	if (!obj || !cb_arg)
		return;
	struct csv_store_handle *s_handle = (struct csv_store_handle *)obj;
	struct roll_cb_arg * args = (struct roll_cb_arg *)cb_arg;
	time_t appx = args->appx;
	struct csv_plugin_static *cps = args->cps;

	FILE* nhfp = NULL;
	FILE* nfp = NULL;

	char *new_filename = NULL;
	char *new_headerfilename = NULL;
	char *new_typefilename = NULL;
	int len;

	//if we've got here then we've called new_store, but it might be closed
	pthread_mutex_lock(&s_handle->lock);
	switch (rolltype) {
	case 1:
	case 2:
	case 5:
		if (!s_handle->store_count && !rollempty)
			/* skip rollover of empty files */
			goto out;
		break;
	case 3:
		if (s_handle->store_count < rollover)  {
			goto out;
		} else {
			s_handle->store_count = 0;
			s_handle->lastflush = 0;
		}
		break;
	case 4:
		if (s_handle->byte_count < rollover) {
			goto out;
		} else {
			s_handle->byte_count = 0;
			s_handle->lastflush = 0;
		}
		break;
	default:
		msglog(LDMSD_LDEBUG, PNAME ": Error: unexpected rolltype in store(%d)\n",
		       rolltype);
		break;
	}


	if (s_handle->file)
		fflush(s_handle->file);
	if (s_handle->headerfile)
		fflush(s_handle->headerfile);

	/* == preparing new filenames == */

	/* new filename */
	len = asprintf(&new_filename, "%s.%ld", s_handle->path, appx);
	if (len < 0) {
		ERR_LOG("out of memory: %s:%s():%d\n", __FILE__, __func__, __LINE__);
		goto out;
	}

	/* new headerfilename */
	if (s_handle->altheader) {
		/* rolltype >= 1 */
		len = asprintf(&new_headerfilename, "%s.HEADER.%ld", s_handle->path, appx);
	} else {
		len = asprintf(&new_headerfilename, "%s", new_filename);
	}
	if (len < 0) {
		ERR_LOG("out of memory: %s:%s():%d\n", __FILE__, __func__, __LINE__);
		goto err_1;
	}

	/* new typefilename */
	if (s_handle->typeheader) {
		len = asprintf(&new_typefilename,  "%s.KIND.%ld", s_handle->path, appx);
		if (len < 0) {
			ERR_LOG("out of memory: %s:%s():%d\n", __FILE__, __func__, __LINE__);
			goto err_2;
		}
	}

	/* open files */

	//re name: if got here, then rollover requested
	nfp = fopen_perm(new_filename, "a+", LDMSD_DEFAULT_FILE_PERM);
	if (!nfp){
		//we cant open the new file, skip
		ERR_LOG("cannot open file <%s>\n", new_filename);
		goto err_3;
	}
	ch_output(nfp, new_filename, CSHC(s_handle), cps);

	if (s_handle->altheader){
		/* truncate a separate headerfile if it exists.
		 * FIXME: do we still want to do this? */
		nhfp = fopen_perm(new_headerfilename, "w", LDMSD_DEFAULT_FILE_PERM);
		if (!nhfp){
			fclose(nfp);
			ERR_LOG("cannot open file <%s>\n", new_headerfilename);
			goto err_4;
		}
		ch_output(nhfp, new_headerfilename, CSHC(s_handle), cps);
	} else {
		nhfp = nfp;
	}

	//close and swap
	if (s_handle->headerfile && s_handle->altheader) {
		/* if s_handle->altheader == 0, headerfile is file */
		fclose(s_handle->headerfile);
	}
	if (s_handle->file) {
		fclose(s_handle->file);
		rename_output(s_handle->filename, FTYPE_DATA,
			CSHC(s_handle), cps);
	}
	if (s_handle->altheader != 0 && s_handle->headerfilename) {
		rename_output(s_handle->headerfilename, FTYPE_HDR,
			      CSHC(s_handle), cps);
	}
	if (s_handle->typeheader != 0 && s_handle->typefilename) {
		rename_output(s_handle->typefilename, FTYPE_KIND,
			CSHC(s_handle), cps);
		free(s_handle->typefilename);
		s_handle->typefilename = new_typefilename;
	}
	s_handle->file = nfp;
	free(s_handle->filename);
	s_handle->filename = new_filename;
	s_handle->headerfile = nhfp;
	free(s_handle->headerfilename);
	s_handle->headerfilename = new_headerfilename;
	s_handle->otime = appx;
	s_handle->printheader = DO_PRINT_HEADER;
	s_handle->store_count = 0;
	goto out;

err_4:
	fclose(nfp);
err_3:
	free(new_typefilename); /* may be NULL, but it is OK */
err_2:
	free(new_headerfilename);
err_1:
	free(new_filename);
out:
	pthread_mutex_unlock(&s_handle->lock);
}

/* Time-based rolltypes will always roll the files when this
function is called.
Volume-based rolltypes must check and shortcircuit within this
function.
*/
static int handleRollover(struct csv_plugin_static *cps){
	//get the config lock
	//for every handle we have, do the rollover

	struct roll_cb_arg rca;

	pthread_mutex_lock(&cfg_lock);

	rca.appx = time(NULL);
	rca.cps = cps;
	idx_traverse(store_idx, roll_cb, (void *)&rca);

	pthread_mutex_unlock(&cfg_lock);

	return 0;

}

static void* rolloverThreadInit(void* m){
	//if got here, then rollover requested

	while(1){
		int tsleep;
		switch (rolltype) {
		case 1:
			tsleep = (rollover < MIN_ROLL_1) ?
				 MIN_ROLL_1 : rollover;
			break;
		case 2: {
				time_t rawtime;
				struct tm info;

				time( &rawtime );
				localtime_r( &rawtime, &info );
				int secSinceMidnight = info.tm_hour*3600 +
					info.tm_min*60 + info.tm_sec;
				tsleep = 86400 - secSinceMidnight + rollover;
				if (tsleep < MIN_ROLL_1){
				/* if we just did a roll then skip this one */
					tsleep+=86400;
				}
			}
			break;
		case 3:
			if (rollover < MIN_ROLL_RECORDS)
				rollover = MIN_ROLL_RECORDS;
			tsleep = ROLL_LIMIT_INTERVAL;
			break;
		case 4:
			if (rollover < MIN_ROLL_BYTES)
				rollover = MIN_ROLL_BYTES;
			tsleep = ROLL_LIMIT_INTERVAL;
			break;
		case 5: {
				time_t rawtime;
				struct tm info;

				time( &rawtime );
				localtime_r( &rawtime, &info );
				int secSinceMidnight = info.tm_hour*3600 +
					info.tm_min*60 + info.tm_sec;

				if (secSinceMidnight < rollover) {
					tsleep = rollover - secSinceMidnight;
				} else {
					int y = secSinceMidnight - rollover;
					int z = y / rollagain;
					tsleep = (z + 1)*rollagain + rollover - secSinceMidnight;
				}
				if (tsleep < MIN_ROLL_1) {
					tsleep += rollagain;
				}
			}
			break;
		default:
			tsleep = 60;
			break;
		}
		sleep(tsleep);
		int oldstate = 0;
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);
		handleRollover(&PG);
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &oldstate);
	}

	return NULL;
}

/**
 * configure a store handle from pa. protect by locking in open_store.
 */
static int config_handle(struct csv_store_handle *s_handle)
{
	int cic_err = OPEN_STORE_COMMON(pa, s_handle);
	if ( cic_err != 0 ) {
		return cic_err;
	}
	const char *k = s_handle->store_key;
	char *c;

	bool r = false; /* default if not in pa */
	int cvt = ldmsd_plugattr_bool(pa, "userdata", k, &r);
	if (cvt == -1) {
		msglog(LDMSD_LERROR, PNAME ": improper userdata= input.\n");
		return EINVAL;
	}
	s_handle->udata = r;
	r = true;
	cvt = ldmsd_plugattr_bool(pa, "expand_array", k, &r);
	if (cvt == -1) {
		msglog(LDMSD_LERROR, PNAME ": improper expand_array= input.\n");
		return EINVAL;
	}
	s_handle->expand_array = r;
	if (!s_handle->expand_array) {
		s_handle->array_sep = ':';
		s_handle->array_lquote = '\"';
		s_handle->array_rquote = '\"';
		c = (char *)ldmsd_plugattr_value(pa, "array_sep", k);
		if (c)
			s_handle->array_sep = c[0];
		c = (char *)ldmsd_plugattr_value(pa, "array_lquote", k);
		if (c)
			s_handle->array_lquote = c[0];
		c = (char *)ldmsd_plugattr_value(pa, "array_rquote", k);
		if (c)
			s_handle->array_rquote = c[0];
	}
	return 0;
}

/* return 1 if blacklisted attr/kw found in avl/kwl, else 0 */
static int attr_blacklist(const char **bad, const struct attr_value_list *kwl, const struct attr_value_list *avl, const char *context)
{
	if (!bad) {
		msglog(LDMSD_LERROR, PNAME ": attr_blacklist miscalled.\n");
		return 1;
	}
	int badcount = 0;
	if (kwl) {
		const char **p = bad;
		while (*p != NULL) {
			int i = av_idx_of(kwl, *p);
			if (i != -1) {
				badcount++;
				msglog(LDMSD_LERROR, PNAME ": %s %s.\n",
					*p, context);
			}
			p++;
		}
	}
	if (avl) {
		const char **p = bad;
		while (*p != NULL) {
			int i = av_idx_of(avl, *p);
			if (i != -1) {
				badcount++;
				msglog(LDMSD_LERROR, PNAME ": %s %s.\n",
					*p, context);
			}
			p++;
		}
	}
	if (badcount)
		return 1;
	return 0;
}

/* list of everything deprecated from v3, with hints. */
static struct pa_deprecated dep[] = {
	{"action", "init", 1, "action= no longer supported; remove action=init (and if present container= and schema=) to configure default settings."},
	{"action", "custom", 1, "action= no longer supported; remove action=custom and specify container=C schema=S to customize."},
	{"action", NULL, 1, "action= no longer supported; remove action= and consult the man page Plugin_store_csv"},
	{"idpos", NULL, 1, "v2 option idpos= no longer supported; remove it."},
	{"id_pos", NULL, 1, "v2 option idpos= no longer supported; remove it."},
	{"transportdata", NULL, 1, "v3 option transportdata= not yet supported; remove it and contact ovis-help if you need it."},
	NULL_PA_DEP
};

static const char *init_blacklist[] = {
	NULL
};

static const char *update_blacklist[] = {
	"config",
	"name",
	"rollagain",
	"rollover",
	"rolltype",
	"rollempty",
	NULL
};

/* the container/schema value pair is the key for our plugin */
#define KEY_PLUG_ATTR 2, "container", "schema"

static int update_config(struct attr_value_list *kwl, struct attr_value_list *avl, const char *container, const char *schema)
{
	int rc = 0;
	char *k = allocStoreKey(container, schema);
	if (!k) {
		return ENOMEM;
	}
	pthread_mutex_lock(&cfg_lock);
	struct csv_store_handle *s_handle = NULL;
	s_handle = idx_find(store_idx, (void *)k, strlen(k));
	if (s_handle) {
		msglog(LDMSD_LWARNING, PNAME " updating config on %s not allowed after store is running.\n", k);
		/* s_handle *could* consult pa again, but that
		is up to the implementation of store().
		Right now it never uses pa after open_store.
	       	*/
	} else {
		msglog(LDMSD_LINFO, PNAME " adding config on %s.\n", k);
		int bl = attr_blacklist(update_blacklist, kwl, avl,
			      	"not allowed in add");
		if (bl) {
			char *as = av_to_string(avl, 0);
			char *ks = av_to_string(kwl, 0);
			msglog(LDMSD_LINFO, PNAME ": fix config args %s %s\n",
				       	as, ks);
			free(as);
			free(ks);
			rc = EINVAL;
			goto out;
		}
		rc = ldmsd_plugattr_add(pa, avl, kwl, update_blacklist, update_blacklist, dep, KEY_PLUG_ATTR);
		if (rc == EEXIST) {
			msglog(LDMSD_LERROR, PNAME
				" cannot repeat config on %s.\n", k);
		} else if (rc != 0) {
			msglog(LDMSD_LINFO, PNAME
				" config failed (%d) on %s.\n", rc, k);
		}
	}
out:
	free(k);
	pthread_mutex_unlock(&cfg_lock);
	return rc;
}

/**
 * \brief Configuration
 */
static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	int rollmethod = DEFAULT_ROLLTYPE;
	int rc;

	static const char *attributes[] = {
		"container",
		"schema",
		"path",
		"userdata",
		"rollagain",
		"rollover",
		"rolltype",
		"rollempty",
		CSV_STORE_ATTR_COMMON,
		NULL
	};
	static const char *keywords[] = { NULL };

	rc = ldmsd_plugattr_config_check(attributes, keywords, avl, kwl, dep, PG.pname);
	if (rc != 0) {
		int warnon = (ldmsd_loglevel_get() > LDMSD_LWARNING);
		msglog(LDMSD_LERROR, PNAME " config arguments unexpected.%s\n",
		       	(warnon ? " Enable log level WARNING for details." : ""));
		return EINVAL;
	}

	char* cn = av_value(avl, "container");
	char* sn = av_value(avl, "schema");
	char* conf = av_value(avl, "opt_file");
	/* this section changes if strgp name is store handle instance name */
	if ((cn && !sn) || (sn && !cn)) {
		msglog(LDMSD_LERROR, PNAME
			" config arguments schema and container must be used together (for particular container/schema) or not used (to set defaults).\n");
		if (sn)
			msglog(LDMSD_LERROR, PNAME ": schema=%s.\n", cn);
		if (cn)
			msglog(LDMSD_LERROR, PNAME ": container=%s.\n", cn);
		return EINVAL;
	}
	if (cn && conf) {
		msglog(LDMSD_LERROR, PNAME
			" config arguments schema and opt_file must not be used together.\n");
		return EINVAL;
	}
	if (cn && !pa) {
		msglog(LDMSD_LERROR, PNAME
			"plugin defaults must be configured before specifics for container=%s schema=%s\n",
			cn, sn);
		return EINVAL;
	}
	if (cn && pa) {
		msglog(LDMSD_LDEBUG, PNAME ": parsing specific schema option\n");
		return update_config(kwl, avl, cn, sn);
	}
	if (pa) {
		msglog(LDMSD_LINFO, PNAME ": config of defaults cannot be repeated.\n");
		return EINVAL;
	}

	/* end strgp pseudo-instance region. */

	pthread_mutex_lock(&cfg_lock);

	pa = ldmsd_plugattr_create(conf, PNAME, avl, kwl,
			init_blacklist, init_blacklist, dep, KEY_PLUG_ATTR);
	if (!pa) {
		msglog(LDMSD_LERROR, PNAME ": Reminder: omit 'config name=<>' from lines in opt_file\n");
		msglog(LDMSD_LERROR, PNAME ": error parsing %s\n", conf);
		rc = EINVAL;
		goto out;
	}

	msglog(LDMSD_LDEBUG, PNAME ": parsed %s\n", conf);

	const char *s = ldmsd_plugattr_value(pa, "path", NULL);
	rc = 0;
	if (!s) {
		msglog(LDMSD_LERROR, PNAME
			": config requires path=value be provided in opt_file or arguments.\n");
		ldmsd_plugattr_destroy(pa);
		pa = NULL;
		rc = EINVAL;
		goto out;
	}

	if (rolltype != -1) {
		msglog(LDMSD_LWARNING, "%s: repeated rollover config is ignored.\n", PNAME);
		goto out; /* rollover configured exactly once */
	}
	int ragain = 0;
	int roll = -1;
	int cvt;
	cvt = ldmsd_plugattr_s32(pa, "rollagain", NULL, &ragain);
	if (!cvt) {
		if (ragain < 0) {
			msglog(LDMSD_LERROR, PNAME
				": bad rollagain= value %d\n", ragain);
			rc = EINVAL;
			goto out;
		}
	}
	if (cvt == ENOTSUP) {
		msglog(LDMSD_LERROR, PNAME ": improper rollagain= input.\n");
		rc = EINVAL;
		goto out;
	}

	cvt = ldmsd_plugattr_s32(pa, "rollover", NULL, &roll);
	if (!cvt) {
		if (roll < 0) {
			msglog(LDMSD_LERROR, PNAME
				": Error: bad rollover value %d\n", roll);
			rc = EINVAL;
			goto out;
		}
	}
	if (cvt == ENOTSUP) {
		msglog(LDMSD_LERROR, PNAME ": improper rollover= input.\n");
		rc = EINVAL;
		goto out;
	}

	cvt = ldmsd_plugattr_s32(pa, "rolltype", NULL, &rollmethod);
	if (!cvt) {
		if (roll < 0) {
			/* rolltype not valid without rollover also */
			msglog(LDMSD_LERROR, PNAME
				": rolltype given without rollover.\n");
			rc = EINVAL;
			goto out;
		}
		if (rollmethod < MINROLLTYPE || rollmethod > MAXROLLTYPE) {
			msglog(LDMSD_LERROR, PNAME
				": rolltype out of range.\n");
			rc = EINVAL;
			goto out;
		}
		if (rollmethod == 5 && (roll < 0 || ragain < roll || ragain < MIN_ROLL_1)) {
			ERR_LOG( "rolltype=5 needs rollagain > max(rollover,10)\n");
			msglog(LDMSD_LERROR, PNAME ": rollagain=%d rollover=%d\n",
			       roll, ragain);
			rc = EINVAL;
			goto out;
		}
	}
	if (cvt == ENOTSUP) {
		msglog(LDMSD_LERROR, PNAME ": improper rolltype= input.\n");
		rc = EINVAL;
		goto out;
	}
	cvt = ldmsd_plugattr_bool(pa, "rollempty", NULL, &rollempty);
	if (cvt == -1) {
		msglog(LDMSD_LERROR, PNAME ": expected boole for rollempty= input.\n");
		rc = EINVAL;
		goto out;
	}

	rollover = roll;
	rollagain = ragain;
	if (rollmethod >= MINROLLTYPE) {
		rolltype = rollmethod;
		pthread_create(&rothread, NULL, rolloverThreadInit, NULL);
		rothread_used = 1;
	}

out:
	pthread_mutex_unlock(&cfg_lock);

	return rc;
}

static void term(struct ldmsd_plugin *self)
{
/* clean up any allocated globals here that are not handled by store_csv_fini */
	pthread_mutex_lock(&cfg_lock);
	ldmsd_plugattr_destroy(pa);
	pa = NULL;
	pthread_mutex_unlock(&cfg_lock);
}

static const char *usage(struct ldmsd_plugin *self)
{
	return  "    config name=store_csv path=<path> rollover=<num> rolltype=<num>\n"
		"           [altheader=<0/!0> userdata=<0/!0>]\n"
		"           [buffer=<0/1/N> buffertype=<3/4>]\n"
		"           [rename_template=<metapath> [rename_uid=<int-uid> [rename_gid=<int-gid]\n"
		"               rename_perm=<octal-mode>]]\n"
		"           [create_uid=<int-uid> [create_gid=<int-gid] create_perm=<octal-mode>]\n"
		"         - Set the root path for the storage of csvs and some default parameters\n"
		"         - path      The path to the root of the csv directory\n"
		"         - altheader Header in a separate file (optional, default 0)\n"
		"         - typeheader Type header line in extra .KIND file (optional, default 0)\n"
		"                0- no header, 1- types 1:1 with csv columns,\n"
	       	"                2- types in array notation\n"
		"         - time_format Format initial time-related fields (optional, default 0)\n"
		"                0- Time,Time_usec: <seconds since epoch>.<microseconds>,<microseconds>\n"
		"                1- Time_msec,Time_usec: <milliseconds since epoch>,<microseconds remainder>\n"
		FILE_PROPS_USAGE
		"         - userdata     UserData in printout (optional, default 0)\n"
		"         - metapath A string template for the file rename, where %[BCDPSTs]\n"
		"           are replaced per the man page Plugin_store_csv.\n"
		"         - rollover  Greater than or equal to zero; enables file rollover and sets interval\n"
		"         - rollempty 0/1; 0 suppresses rollover of empty files, 1 allows (default)\n"
		"         - rolltype  [1-n] Defines the policy used to schedule rollover events.\n"
		ROLLTYPES
		"         - buffer    0 to disable buffering, 1 to enable it with autosize (default)\n"
		"                     N > 1 to flush after that many kb (> 4) or that many lines (>=1)\n"
		"         - buffertype [3,4] Defines the policy used to schedule buffer flush.\n"
		"                      Only applies for N > 1. Same as rolltypes.\n"
		"\n"
		;
}

static void *get_ucontext(ldmsd_store_handle_t _s_handle)
{
	struct csv_store_handle *s_handle = _s_handle;
	return s_handle->ucontext;
}

/* caller MUST hold the s_handle->lock */
static int print_header_from_row(struct csv_store_handle *s_handle,
				 ldms_set_t set, struct ldmsd_row_s *row)
{
	/* Only called from Store which already has the lock */
	FILE* fp;

	if (s_handle == NULL){
		msglog(LDMSD_LERROR, PNAME ": Null store handle. Cannot print header\n");
		return EINVAL;
	}
	s_handle->printheader = DONT_PRINT_HEADER;

	fp = s_handle->headerfile;
	if (!fp){
		msglog(LDMSD_LERROR, PNAME ": Cannot print header. No headerfile\n");
		return EINVAL;
	}
	csv_row_format_header(fp, s_handle->headerfilename, CCSHC(s_handle), s_handle->udata,
                                 &PG, set, row,
                                 s_handle->time_format);

	/* Flush for the header, whether or not it is the data file as well */
	fflush(fp);
	fsync(fileno(fp));

	if (s_handle->headerfile && s_handle->altheader)
		fclose(s_handle->headerfile);
	s_handle->headerfile = 0;
	fp = NULL;

	/* dump data types header, or whine and continue to other headers. */
	if (s_handle->typeheader > 0 && s_handle->typeheader <= TH_MAX) {
		fp = fopen_perm(s_handle->typefilename, "w", LDMSD_DEFAULT_FILE_PERM);
		if (!fp) {
			int rc = errno;
			PG.msglog(LDMSD_LERROR, PNAME ": print_header: %s "
				"failed to open types file (%d).\n",
				s_handle->typefilename, rc);
		} else {
			ch_output(fp, s_handle->typefilename, CSHC(s_handle), &PG);
			csv_row_format_types_common(s_handle->typeheader, fp,
				s_handle->typefilename, CCSHC(s_handle),
				s_handle->udata, &PG, set,
				row);
			fclose(fp);
		}
	}

	return 0;
}

/*
 * note: this should be residual from v2 where we may not have had the header info until a store was called
 * which then meant we had the mvec. ideally the print_header will always happen from the open_store,
 * but in point of fact is currently _never_ called in open_store.
 * Currently we still have to keep this to invert the metric order
 */
static int print_header_from_store(struct csv_store_handle *s_handle, ldms_set_t set,
				   int *metric_array, size_t metric_count)
{
	/* Only called from Store which already has the lock */
	FILE* fp;
	char tmp_path[PATH_MAX];

	if (s_handle == NULL){
		msglog(LDMSD_LERROR, PNAME ": Null store handle. Cannot print header\n");
		return EINVAL;
	}
	s_handle->printheader = DONT_PRINT_HEADER;

	fp = s_handle->headerfile;
	if (!fp){
		msglog(LDMSD_LERROR, PNAME ": Cannot print header. No headerfile\n");
		return EINVAL;
	}
	int ec;
	if (s_handle->altheader) {
		if (rolltype >= MINROLLTYPE)
			ec = snprintf(tmp_path, PATH_MAX, "%s.HEADER.%d",
				s_handle->path, (int)s_handle->otime);
		else
			ec = snprintf(tmp_path, PATH_MAX, "%s.HEADER",
				s_handle->path);
	} else {
		if (rolltype >= MINROLLTYPE)
			ec = snprintf(tmp_path, PATH_MAX, "%s.%d",
				s_handle->path, (int)s_handle->otime);
		else
			ec = snprintf(tmp_path, PATH_MAX, "%s", s_handle->path);
	}
	(void)ec;
	csv_format_header_common(fp, tmp_path, CCSHC(s_handle), s_handle->udata,
                                 &PG, set, metric_array, metric_count,
                                 s_handle->time_format);

	/* Flush for the header, whether or not it is the data file as well */
	fflush(fp);
	fsync(fileno(fp));

	if (s_handle->headerfile && s_handle->altheader)
		fclose(s_handle->headerfile);
	s_handle->headerfile = 0;
	fp = NULL;

	/* dump data types header, or whine and continue to other headers. */
	if (s_handle->typeheader > 0 && s_handle->typeheader <= TH_MAX) {
		fp = fopen_perm(s_handle->typefilename, "w", LDMSD_DEFAULT_FILE_PERM);
		if (!fp) {
			int rc = errno;
			PG.msglog(LDMSD_LERROR, PNAME ": print_header: %s "
				"failed to open types file (%d).\n",
				s_handle->typefilename, rc);
		} else {
			ch_output(fp, s_handle->typefilename, CSHC(s_handle), &PG);
			csv_format_types_common(s_handle->typeheader, fp,
				s_handle->typefilename, CCSHC(s_handle),
				s_handle->udata, &PG, set,
				metric_array, metric_count);
			fclose(fp);
		}
	}

	return 0;
}

/*
 *  Would like to do this instead, but cannot currently get array size in open_store
 */
#if 0 /* print_header_from_open proto */
static int print_header_from_open(struct csv_store_handle *s_handle,
				  struct ldmsd_store_metric_list *metric_list,
				  void *ucontext){
}
#endif

static struct csv_store_handle *
csv_store_handle_get(const char *container, const char *schema);

static void csv_store_handle_put(struct csv_store_handle *s_handle);

static ldmsd_store_handle_t
open_store(struct ldmsd_store *s, const char *container, const char* schema,
		struct ldmsd_strgp_metric_list *list, void *ucontext)
{
	struct csv_store_handle *s_handle = NULL;

	if (!pa) {
		msglog(LDMSD_LERROR, PNAME ": config not called. cannot open.\n");
		return NULL;
	}

	pthread_mutex_lock(&cfg_lock);
	s_handle = csv_store_handle_get(container, schema);
	pthread_mutex_unlock(&cfg_lock);
	return s_handle;
}

static inline void __print_check(struct csv_store_handle *sh, int rc)
{
	if (rc < 0) {
		msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
		       rc, sh->path);
	} else {
		sh->byte_count += rc;
	}
}

static void
store_metric(struct csv_store_handle *sh, const char *wsqt, uint64_t udata,
		enum ldms_value_type mtype, size_t count, ldms_mval_t mval)
{
	int rc, i;
	ldms_mval_t v;
	switch (mtype) {
	case LDMS_V_CHAR_ARRAY:
		if (sh->udata) {
			rc = fprintf(sh->file, ",%"PRIu64, udata);
			__print_check(sh, rc);
		}
		/* our csv does not included embedded nuls */
		rc = fprintf(sh->file, ",%s%s%s", wsqt, mval->a_char, wsqt);
		__print_check(sh, rc);
		break;
	case LDMS_V_CHAR:
		if (sh->udata) {
			rc = fprintf(sh->file, ",%"PRIu64, udata);
			__print_check(sh, rc);
		}
		rc = fprintf(sh->file, ",%c", mval->v_char);
		__print_check(sh, rc);
		break;
	case LDMS_V_U8_ARRAY:
		if (sh->expand_array) {
			for (i = 0; i < count; i++){
				if (sh->udata) {
					rc = fprintf(sh->file, ",%"PRIu64, udata);
					__print_check(sh, rc);
				}
				rc = fprintf(sh->file, ",%hhu", mval->a_u8[i]);
				__print_check(sh, rc);
			}
		} else {
			if (sh->udata) {
				rc = fprintf(sh->file, ",%"PRIu64, udata);
				__print_check(sh, rc);
			}
			for (i = 0; i < count; i++) {
				if (i == 0) {
					rc = fprintf(sh->file, ",%c%hhu",
							sh->array_lquote,
							mval->a_u8[i]);
				} else {
					rc = fprintf(sh->file, "%c%hhu",
							sh->array_sep,
							mval->a_u8[i]);
				}
				__print_check(sh, rc);
			}
			rc = fprintf(sh->file, "%c", sh->array_rquote);
			__print_check(sh, rc);
		}
		break;
	case LDMS_V_U8:
		if (sh->udata) {
			rc = fprintf(sh->file, ",%"PRIu64, udata);
			__print_check(sh, rc);
		}
		rc = fprintf(sh->file, ",%hhu", mval->v_u8);
		__print_check(sh, rc);
		break;
	case LDMS_V_S8_ARRAY:
		if (sh->expand_array) {
			for (i = 0; i < count; i++) {
				if (sh->udata) {
					rc = fprintf(sh->file, ",%"PRIu64, udata);
					__print_check(sh, rc);
				}
				rc = fprintf(sh->file, ",%hhd", mval->a_s8[i]);
				__print_check(sh, rc);
			}
		} else {
			if (sh->udata) {
				rc = fprintf(sh->file, ",%"PRIu64, udata);
				__print_check(sh, rc);
			}
			for (i = 0; i < count; i++) {
				if (i == 0) {
					rc = fprintf(sh->file, ",%c%hhd",
							sh->array_lquote,
							mval->a_s8[i]);
				} else {
					rc = fprintf(sh->file, "%c%hhd",
							sh->array_sep,
							mval->a_s8[i]);
				}
				__print_check(sh, rc);
			}
			rc = fprintf(sh->file, "%c", sh->array_rquote);
			__print_check(sh, rc);
		}
		break;
	case LDMS_V_S8:
		if (sh->udata) {
			rc = fprintf(sh->file, ",%"PRIu64, udata);
			__print_check(sh, rc);
		}
		rc = fprintf(sh->file, ",%hhd", mval->v_s8);
		__print_check(sh, rc);
		break;
	case LDMS_V_U16_ARRAY:
		if (sh->expand_array) {
			for (i = 0; i < count; i++) {
				if (sh->udata) {
					rc = fprintf(sh->file, ",%"PRIu64, udata);
					__print_check(sh, rc);
				}
				rc = fprintf(sh->file, ",%hu", mval->a_u16[i]);
				__print_check(sh, rc);
			}
		} else {
			if (sh->udata) {
				rc = fprintf(sh->file, ",%"PRIu64, udata);
				__print_check(sh, rc);
			}
			for (i = 0; i < count; i++) {
				if (i == 0) {
					rc = fprintf(sh->file, ",%c%hu",
							sh->array_lquote,
							mval->a_u16[i]);
				} else {
					rc = fprintf(sh->file, "%c%hu",
							sh->array_sep,
							mval->a_u16[i]);
				}
				__print_check(sh, rc);
			}
			rc = fprintf(sh->file, "%c", sh->array_rquote);
			__print_check(sh, rc);
		}
		break;
	case LDMS_V_U16:
		if (sh->udata) {
			rc = fprintf(sh->file, ",%"PRIu64, udata);
			__print_check(sh, rc);
		}
		rc = fprintf(sh->file, ",%hu", mval->v_u16);
		__print_check(sh, rc);
		break;
	case LDMS_V_S16_ARRAY:
		if (sh->expand_array) {
			for (i = 0; i < count; i++) {
				if (sh->udata) {
					rc = fprintf(sh->file, ",%"PRIu64, udata);
					__print_check(sh, rc);
				}
				rc = fprintf(sh->file, ",%hd", mval->a_s16[i]);
				__print_check(sh, rc);
			}
		} else {
			if (sh->udata) {
				rc = fprintf(sh->file, ",%"PRIu64, udata);
				__print_check(sh, rc);
			}
			for (i = 0; i < count; i++) {
				if (i == 0) {
					rc = fprintf(sh->file, ",%c%hd",
							sh->array_lquote,
							mval->a_s16[i]);
				} else {
					rc = fprintf(sh->file, "%c%hd",
							sh->array_sep,
							mval->a_s16[i]);
				}
				__print_check(sh, rc);
			}
			rc = fprintf(sh->file, "%c", sh->array_rquote);
			__print_check(sh, rc);
		}
		break;
	case LDMS_V_S16:
		if (sh->udata) {
			rc = fprintf(sh->file, ",%"PRIu64, udata);
			__print_check(sh, rc);
		}
		rc = fprintf(sh->file, ",%hd", mval->v_s16);
		__print_check(sh, rc);
		break;
	case LDMS_V_U32_ARRAY:
		if (sh->expand_array) {
			for (i = 0; i < count; i++) {
				if (sh->udata) {
					rc = fprintf(sh->file, ",%"PRIu64, udata);
					__print_check(sh, rc);
				}
				rc = fprintf(sh->file, ",%" PRIu32, mval->a_u32[i]);
				__print_check(sh, rc);
			}
		} else {
			if (sh->udata) {
				rc = fprintf(sh->file, ",%"PRIu64, udata);
				__print_check(sh, rc);
			}
			for (i = 0; i < count; i++) {
				if (i == 0) {
					rc = fprintf(sh->file, ",%c%" PRIu32,
							sh->array_lquote,
							mval->a_u32[i]);
				} else {
					rc = fprintf(sh->file, "%c%" PRIu32,
							sh->array_sep,
							mval->a_u32[i]);
				}
				__print_check(sh, rc);
			}
			rc = fprintf(sh->file, "%c", sh->array_rquote);
			__print_check(sh, rc);
		}
		break;
	case LDMS_V_U32:
		if (sh->udata) {
			rc = fprintf(sh->file, ",%"PRIu64, udata);
			__print_check(sh, rc);
		}
		rc = fprintf(sh->file, ",%" PRIu32, mval->v_u32);
		__print_check(sh, rc);
		break;
	case LDMS_V_S32_ARRAY:
		if (sh->expand_array) {
			for (i = 0; i < count; i++) {
				if (sh->udata) {
					rc = fprintf(sh->file, ",%"PRIu64, udata);
					__print_check(sh, rc);
				}
				rc = fprintf(sh->file, ",%" PRId32, mval->a_s32[i]);
				__print_check(sh, rc);
			}
		} else {
			if (sh->udata) {
				rc = fprintf(sh->file, ",%"PRIu64, udata);
				__print_check(sh, rc);
			}
			for (i = 0; i < count; i++) {
				if (i == 0) {
					rc = fprintf(sh->file, ",%c%" PRId32,
							sh->array_lquote,
							mval->a_s32[i]);
				} else {
					rc = fprintf(sh->file, "%c%" PRId32,
							sh->array_sep,
							mval->a_s32[i]);
				}
				__print_check(sh, rc);
			}
			rc = fprintf(sh->file, "%c", sh->array_rquote);
			__print_check(sh, rc);
		}
		break;
	case LDMS_V_S32:
		if (sh->udata) {
			rc = fprintf(sh->file, ",%"PRIu64, udata);
			__print_check(sh, rc);
		}
		rc = fprintf(sh->file, ",%" PRId32, mval->v_s32);
		__print_check(sh, rc);
		break;
	case LDMS_V_U64_ARRAY:
		if (sh->expand_array) {
			for (i = 0; i < count; i++) {
				if (sh->udata) {
					rc = fprintf(sh->file, ",%"PRIu64, udata);
					__print_check(sh, rc);
				}
				rc = fprintf(sh->file, ",%" PRIu64, mval->a_u64[i]);
				__print_check(sh, rc);
			}
		} else {
			if (sh->udata) {
				rc = fprintf(sh->file, ",%"PRIu64, udata);
				__print_check(sh, rc);
			}
			for (i = 0; i < count; i++) {
				if (i == 0) {
					rc = fprintf(sh->file, ",%c%" PRIu64,
							sh->array_lquote,
							mval->a_u64[i]);
				} else {
					rc = fprintf(sh->file, "%c%" PRIu64,
							sh->array_sep,
							mval->a_u64[i]);
				}
				__print_check(sh, rc);
			}
			rc = fprintf(sh->file, "%c", sh->array_rquote);
			__print_check(sh, rc);
		}
		break;
	case LDMS_V_U64:
		if (sh->udata) {
			rc = fprintf(sh->file, ",%"PRIu64, udata);
			__print_check(sh, rc);
		}
		rc = fprintf(sh->file, ",%"PRIu64, mval->v_u64);
		__print_check(sh, rc);
		break;
	case LDMS_V_S64_ARRAY:
		if (sh->expand_array) {
			for (i = 0; i < count; i++) {
				if (sh->udata) {
					rc = fprintf(sh->file, ",%"PRIu64, udata);
					__print_check(sh, rc);
				}
				rc = fprintf(sh->file, ",%" PRId64, mval->a_s64[i]);
				__print_check(sh, rc);
			}
		} else {
			if (sh->udata) {
				rc = fprintf(sh->file, ",%"PRIu64, udata);
				__print_check(sh, rc);
			}
			for (i = 0; i < count; i++) {
				if (i == 0) {
					rc = fprintf(sh->file, ",\"%" PRId64, mval->a_s64[i]);
				} else {
					rc = fprintf(sh->file, ",%" PRId64, mval->a_s64[i]);
				}
				__print_check(sh, rc);
			}
			rc = fprintf(sh->file, "\"");
			__print_check(sh, rc);
		}
		break;
	case LDMS_V_S64:
		if (sh->udata) {
			rc = fprintf(sh->file, ",%"PRIu64, udata);
			__print_check(sh, rc);
		}
		rc = fprintf(sh->file, ",%" PRId64, mval->v_s64);
		__print_check(sh, rc);
		break;
	case LDMS_V_F32_ARRAY:
		if (sh->expand_array) {
			for (i = 0; i < count; i++) {
				if (sh->udata) {
					rc = fprintf(sh->file, ",%"PRIu64, udata);
					__print_check(sh, rc);
				}
				rc = fprintf(sh->file, ",%.9g", mval->a_f[i]);
				__print_check(sh, rc);
			}
		} else {
			if (sh->udata) {
				rc = fprintf(sh->file, ",%"PRIu64, udata);
				__print_check(sh, rc);
			}
			for (i = 0; i < count; i++) {
				if (i == 0) {
					rc = fprintf(sh->file, ",%c%.9g",
							sh->array_lquote,
							mval->a_f[i]);
				} else {
					rc = fprintf(sh->file, "%c%.9g",
							sh->array_sep,
							mval->a_f[i]);
				}
				__print_check(sh, rc);
			}
			rc = fprintf(sh->file, "%c", sh->array_rquote);
			__print_check(sh, rc);
		}
		break;
	case LDMS_V_F32:
		if (sh->udata) {
			rc = fprintf(sh->file, ",%"PRIu64, udata);
			__print_check(sh, rc);
		}
		rc = fprintf(sh->file, ",%.9g", mval->v_f);
		__print_check(sh, rc);
		break;
	case LDMS_V_D64_ARRAY:
		if (sh->expand_array) {
			for (i = 0; i < count; i++) {
				if (sh->udata) {
					rc = fprintf(sh->file, ",%"PRIu64, udata);
					__print_check(sh, rc);
				}
				rc = fprintf(sh->file, ",%.17g", mval->a_d[i]);
				__print_check(sh, rc);
			}
		} else {
			if (sh->udata) {
				rc = fprintf(sh->file, ",%"PRIu64, udata);
				__print_check(sh, rc);
			}
			for (i = 0; i < count; i++) {
				if (i == 0) {
					rc = fprintf(sh->file, ",%c%.17g",
							sh->array_lquote,
							mval->a_d[i]);
				} else {
					rc = fprintf(sh->file, "%c%.17g",
							sh->array_sep,
							mval->a_d[i]);
				}
				__print_check(sh, rc);
			}
			rc = fprintf(sh->file, "%c", sh->array_rquote);
			__print_check(sh, rc);
		}
		break;
	case LDMS_V_D64:
		if (sh->udata) {
			rc = fprintf(sh->file, ",%"PRIu64, udata);
			__print_check(sh, rc);
		}
		rc = fprintf(sh->file, ",%.17g", mval->v_d);
		__print_check(sh, rc);
		break;
	case LDMS_V_RECORD_INST:
		for (i = 0; i < ldms_record_card(mval); i++) {
			mtype = ldms_record_metric_type_get(mval, i, &count);
			v = ldms_record_metric_get(mval, i);
			store_metric(sh, wsqt, udata, mtype, count, v);
		}
		break;
	default:
		msglog(LDMSD_LERROR, PNAME ": Received unrecognized metric value type %d\n", mtype);
		/* print no value */
		if (sh->udata) {
			rc = fprintf(sh->file, ",");
			__print_check(sh, rc);
		}
		rc = fprintf(sh->file, ",");
		__print_check(sh, rc);
		break;
	}
}

static void
store_time_job_app(struct csv_store_handle *sh, const struct ldms_timestamp *ts, ldms_set_t set)
{
	const char *pname;
	/* Print timestamp fields */
	if (sh->time_format == TF_MILLISEC) {
		/* Alternate time format. First field is milliseconds-since-epoch,
		   and the second field is the left-over microseconds */
		fprintf(sh->file, "%"PRIu64",%"PRIu32,
			((uint64_t)ts->sec * 1000) + (ts->usec / 1000),
			ts->usec % 1000);
	} else {
		/* Traditional time format, where the first field is
		   <seconds>.<microseconds>, second is microseconds repeated */
		fprintf(sh->file, "%"PRIu32".%06"PRIu32 ",%"PRIu32,
			ts->sec, ts->usec, ts->usec);
	}
	pname = ldms_set_producer_name_get(set);
	if (pname != NULL){
		fprintf(sh->file, ",%s", pname);
		sh->byte_count += strlen(pname);
	} else {
		fprintf(sh->file, ",");
	}
}

static int store(ldmsd_store_handle_t _s_handle, ldms_set_t set, int *metric_array, size_t metric_count)
{
	const struct ldms_timestamp _ts = ldms_transaction_timestamp_get(set);
	const struct ldms_timestamp *ts = &_ts;
	uint64_t udata;
	struct csv_store_handle *s_handle;
	int i;
	int doflush = 0;
	int rc;
	ldms_mval_t mval;
	enum ldms_value_type metric_type;

	s_handle = _s_handle;
	if (!s_handle)
		return EINVAL;

	if (s_handle->num_lists < 0) {
		s_handle->num_lists = 0;
		for (i = 0; i < metric_count; i++) {
			if (LDMS_V_LIST == ldms_metric_type_get(set, metric_array[i])) {
				s_handle->num_lists++;
				if (0 == ldms_list_len(set,
					ldms_metric_get(set, metric_array[i]))) {
					msglog(LDMSD_LERROR, PNAME ": set '%s' contains an empty list '%s'.\n",
						ldms_set_instance_name_get(set),
						ldms_metric_name_get(set, metric_array[i]));
					/*
					 * We cannot determine the number of columns.
					 * Do nothing.
					 */
					return 0;
				}
			}
		}
		s_handle->lents = malloc(s_handle->num_lists * sizeof(*s_handle->lents));
		if (!s_handle->lents) {
			msglog(LDMSD_LCRITICAL, PNAME ": Out of memory\n");
			return ENOMEM;
		}
	}
	for (i = 0; i < s_handle->num_lists; i++) {
		memset(&s_handle->lents[i], 0, sizeof(struct csv_lent));
		s_handle->lents[i].idx = -1;
	}

	pthread_mutex_lock(&s_handle->lock);
	if (!s_handle->file){
		msglog(LDMSD_LERROR, PNAME ": Cannot insert values for <%s>: file is NULL\n",
		       s_handle->path);
		pthread_mutex_unlock(&s_handle->lock);
		/* FIXME: will returning an error stop the store? */
		return EPERM;
	}

	// Temporarily still have to print header until can invert order of metrics from open
	/* FIXME: New in v3: should not ever have to print the header from here */
	switch (s_handle->printheader){
	case DO_PRINT_HEADER:
		/* fall thru */
	case FIRST_PRINT_HEADER:
		rc = print_header_from_store(s_handle, set, metric_array, metric_count);
		if (rc){
			msglog(LDMSD_LERROR, PNAME ": %s cannot print header: %d. Not storing\n",
			       s_handle->store_key, rc);
			s_handle->printheader = BAD_HEADER;
			pthread_mutex_unlock(&s_handle->lock);
			/* FIXME: will returning an error stop the store? */
			return rc;
		}
		break;
	case BAD_HEADER:
		pthread_mutex_unlock(&s_handle->lock);
		return EINVAL;
		break;
	default:
		/* ok to continue */
		break;
	}

	/* FIXME: will we want to throw an error if we cannot write? */
	char *wsqt = ""; /* ietf quotation wrapping strings */
	if (s_handle->ietfcsv) {
		wsqt = "\"";
	}

	int done = 0;
	const char *name;
	size_t count;
	union ldms_value v;
	struct csv_lent *lents = s_handle->lents;
	do {
		int lidx = 0;
		store_time_job_app(s_handle, ts, set);
		for (i = 0; i < metric_count; i++) {
			mval = ldms_metric_get(set, metric_array[i]);
			udata = ldms_metric_user_data_get(set, metric_array[i]);
			metric_type = ldms_metric_type_get(set, metric_array[i]);
			if (LDMS_V_RECORD_TYPE == metric_type) {
				continue;
			} else if (LDMS_V_LIST ==  metric_type) {
				/* List entry */
				if (0 == ldms_list_len(set, mval)) {
					name = ldms_metric_name_get(set, metric_array[i]);
					msglog(LDMSD_LERROR, PNAME " : set '%s' "
						"containing an empty list '%s', "
						"which is not supported. \n",
						ldms_set_instance_name_get(set), name);
					break;
				}
				if (lents[lidx].idx + 1 == ldms_list_len(set, mval)) {
					/* done, do nothing */
				} else {
					if (!lents[lidx].mval) {
						/*
						 * lents[lidx].idx starts from 0 already.
						 */
						lents[lidx].mval = ldms_list_first(set, mval,
									&lents[lidx].mtype,
									&lents[lidx].count);
					} else {
						lents[lidx].mval = ldms_list_next(set,
									lents[lidx].mval,
									&lents[lidx].mtype,
									&lents[lidx].count);
					}
					lents[lidx].idx++;
					if (lents[lidx].idx + 1 == ldms_list_len(set, mval))
						done++;
				}

				/* Store list entry index */
				v.v_u64 = lents[lidx].idx;
				store_metric(s_handle, wsqt, udata, LDMS_V_U64, 1, &v);

				assert(lents[lidx].mval);
				/* Store list entry */
				store_metric(s_handle, wsqt, udata,
						lents[lidx].mtype,
						lents[lidx].count,
						lents[lidx].mval);
				lidx++;
			} else {
				if (ldms_type_is_array(metric_type)) {
					count = ldms_metric_array_get_len(set, metric_array[i]);
				} else {
					count = 1;
				}
				store_metric(s_handle, wsqt, udata,
					     metric_type, count, mval);
			}
		}
		fprintf(s_handle->file,"\n");
	} while (done < s_handle->num_lists);

	s_handle->store_count++;

	if ((s_handle->buffer_type == 3) &&
	    ((s_handle->store_count - s_handle->lastflush) >=
	     s_handle->buffer_sz)){
		s_handle->lastflush = s_handle->store_count;
		doflush = 1;
	} else if ((s_handle->buffer_type == 4) &&
		 ((s_handle->byte_count - s_handle->lastflush) >=
		  s_handle->buffer_sz)){
		s_handle->lastflush = s_handle->byte_count;
		doflush = 1;
	}
	if ((s_handle->buffer_sz == 0) || doflush){
		fflush(s_handle->file);
		fsync(fileno(s_handle->file));
	}
	pthread_mutex_unlock(&s_handle->lock);

	return 0;
}

static int flush_store(ldmsd_store_handle_t _s_handle)
{
	struct csv_store_handle *s_handle = _s_handle;
	if (!s_handle) {
		msglog(LDMSD_LERROR, PNAME ": flush error.\n");
		return -1;
	}
	pthread_mutex_lock(&s_handle->lock);
	fflush(s_handle->file);
	pthread_mutex_unlock(&s_handle->lock);
	return 0;
}

static void __csv_handle_close(struct csv_store_handle *s_handle)
{
	pthread_mutex_lock(&s_handle->lock);
	msglog(LDMSD_LDEBUG, PNAME ": Closing with path <%s>\n",
	       s_handle->path);
	fflush(s_handle->file);
	if (s_handle->path)
		free(s_handle->path);
	s_handle->path = NULL;
	s_handle->ucontext = NULL;
	if (s_handle->file)
		fclose(s_handle->file);
	s_handle->file = NULL;
	if (s_handle->headerfile && s_handle->altheader)
		fclose(s_handle->headerfile);
	s_handle->headerfile = NULL;
	CLOSE_STORE_COMMON(s_handle);

	idx_delete(store_idx, s_handle->store_key, strlen(s_handle->store_key));

	if (s_handle->store_key)
		free(s_handle->store_key);
	free(s_handle->container);
	free(s_handle->schema);
	pthread_mutex_unlock(&s_handle->lock);
	pthread_mutex_destroy(&s_handle->lock);
	free(s_handle);
}

/* must hold cfg_lock */
static void csv_store_handle_put(struct csv_store_handle *s_handle)
{
	assert(s_handle->ref_count > 0);
	if (s_handle->ref_count == 0) {
		ERR_LOG("%s:%s():%d s_handle->ref_count is 0\n", __FILE__, __func__, __LINE__);
		return;
	}
	s_handle->ref_count--;
	if (s_handle->ref_count == 0)
		__csv_handle_close(s_handle);
}

/* must hold cfg_lock */
static void __csv_row_handle_close(struct csv_row_store_handle *rs_handle)
{
	struct csv_row_schema_rbn_s *rbn;
	while ((rbn = (void*)rbt_min(&rs_handle->row_schema_rbt))) {
		rbt_del(&rs_handle->row_schema_rbt, &rbn->rbn);
		csv_store_handle_put(rbn->s_handle);
		free(rbn);
	}
	free(rs_handle);
}

static void close_store(ldmsd_store_handle_t _s_handle)
{
	pthread_mutex_lock(&cfg_lock);
	struct csv_store_handle *s_handle = _s_handle;
	if (!s_handle) {
		pthread_mutex_unlock(&cfg_lock);
		return;
	}

	/* The s_handle may be CSV_STORE_HANDLE or CSV_ROW_STORE_HANDLE.
	 * - In the case of CSV_STORE_HANDLE, the _s_handle is the handle
	 *   returned by open_store().
	 * - In the case of CSV_ROW_STORE_HANDLE, the _s_handle is
	 *   `strgp->store_handle` that was set in `commit_rows()`.
	 */
	switch (s_handle->type) {
	case CSV_STORE_HANDLE:
		csv_store_handle_put(s_handle);
		break;
	case CSV_ROW_STORE_HANDLE:
		__csv_row_handle_close((void*)s_handle);
		break;
	default:
		ERR_LOG("Unknown csv_handle type: %d\n", s_handle->type);
	}
	pthread_mutex_unlock(&cfg_lock);
}

static struct csv_row_store_handle *
row_store_new()
{
	struct csv_row_store_handle *rs_handle;
	rs_handle = calloc(1, sizeof(*rs_handle));
	if (!rs_handle)
		return NULL;
	rs_handle->type = CSV_ROW_STORE_HANDLE;
	rbt_init(&rs_handle->row_schema_rbt, csv_row_schema_key_cmp);
	return rs_handle;
}

/*
 * - caller MUST hold cfg_lock
 * - `store_key` is "<CONTAINER>/<SCHEMA>"
 */
static struct csv_store_handle *
csv_store_handle_get(const char *container, const char *schema)
{
	struct csv_store_handle *s_handle;
	int len, rc;
	char *store_key;
	char path[PATH_MAX];

	store_key = allocStoreKey(container, schema);
	if (!store_key)
		goto err_0;

	s_handle = idx_find(store_idx, (void *)store_key, strlen(store_key));
	if (s_handle) {
		assert(s_handle->ref_count > 0);
		s_handle->ref_count++;
		free(store_key); /* s_handle already have store_key */
		goto out;
	}

	/* CSV file not opened yet */
	s_handle = calloc(1, sizeof(*s_handle));
	if (!s_handle) {
		ERR_LOG("Not enough memory (%s:%s():%d)", __FILE__, __func__, __LINE__);
		goto err_1;
	}
	s_handle->num_lists = -1;
	s_handle->ref_count = 1;
	s_handle->type = CSV_STORE_HANDLE;
	s_handle->store_key = store_key; /* give store_key to s_handle */
	const char *root_path = ldmsd_plugattr_value(pa, "path", store_key);
	if (!root_path) {
		ERR_LOG("`path` plugin attribute is not set\n");
		errno = EINVAL;
		goto err_2;
	}
	len = asprintf(&s_handle->path, "%s/%s/%s", root_path, container, schema);
	if (len < 0) {
		ERR_LOG("Not enough memory (%s:%s():%d)", __FILE__, __func__, __LINE__);
		goto err_2;
	}
	s_handle->container = strdup(container);
	if (!s_handle->container) {
		ERR_LOG("Not enough memory (%s:%s():%d)", __FILE__, __func__, __LINE__);
		goto err_3;
	}
	s_handle->schema = strdup(schema);
	if (!s_handle->schema) {
		ERR_LOG("Not enough memory (%s:%s():%d)", __FILE__, __func__, __LINE__);
		goto err_4;
	}
	s_handle->printheader = FIRST_PRINT_HEADER;
	rc = config_handle(s_handle);
	if (rc) /* error already logged */
		goto err_5;

	snprintf(path, sizeof(path), "%s/%s", root_path, container);
	rc = create_outdir(path, CSHC(s_handle), &PG);
	if (rc) {
		ERR_LOG("Failure %d creating directory containing '%s'\n", errno, path);
		goto err_5;
	}

	time_t appx = time(NULL);

	/* csv filename */
	if (rolltype >= MINROLLTYPE){
		//append the files with epoch. assume wont collide to the sec.
		len = asprintf(&s_handle->filename, "%s.%ld", s_handle->path, appx);
	} else {
		len = asprintf(&s_handle->filename, "%s", s_handle->path);
	}
	if (len < 0) {
		ERR_LOG("Not enough memory (%s:%s():%d)", __FILE__, __func__, __LINE__);
		goto err_5;
	}

	/* the CSV FILE */
	s_handle->file = fopen_perm(s_handle->filename, "a+", LDMSD_DEFAULT_FILE_PERM);
	s_handle->otime = appx;
	if (!s_handle->file) {
		ERR_LOG("Error %d opening the file %s.\n", errno, s_handle->path);
		goto err_6;
	}
	ch_output(s_handle->file, s_handle->filename, CSHC(s_handle), &PG);

	/* header file name */
	if (s_handle->altheader) {
		if (rolltype >= MINROLLTYPE) {
			len = asprintf(&s_handle->headerfilename, "%s.HEADER.%ld", s_handle->path, appx);
		} else {
			len = asprintf(&s_handle->headerfilename, "%s.HEADER", s_handle->path);
		}
	} else {
		/* header file is the csv file */
		len = asprintf(&s_handle->headerfilename, "%s", s_handle->filename);
	}

	if (len < 0) {
		ERR_LOG("Not enough memory (%s:%s():%d)", __FILE__, __func__, __LINE__);
		goto err_7;
	}

	/* the header FILE */
	if (s_handle->altheader) {
		const char *_mode;
		if (s_handle->printheader == FIRST_PRINT_HEADER){
			_mode = "w";
		} else {
			_mode = "a+";
		}
		s_handle->headerfile = fopen_perm(s_handle->headerfilename, _mode, LDMSD_DEFAULT_FILE_PERM);
		if (!s_handle->headerfile) {
			ERR_LOG("Error: Cannot open headerfile '%s', errno: %d\n", s_handle->headerfilename, errno);
			goto err_8;
		}
		ch_output(s_handle->headerfile, s_handle->headerfilename, CSHC(s_handle), &PG);
	} else {
		s_handle->headerfile = s_handle->file;
	}

	if (s_handle->typeheader > 0) {
		if (rolltype >= MINROLLTYPE){
			len = asprintf(&s_handle->typefilename,  "%s.KIND.%ld",
				s_handle->path, appx);
		} else {
			len = asprintf(&s_handle->typefilename,  "%s.KIND",
				s_handle->path);
		}
		if (len < 0) {
			ERR_LOG("Not enough memory (%s:%s():%d)", __FILE__, __func__, __LINE__);
			goto err_9;
		}
	}

	idx_add(store_idx, s_handle->store_key, strlen(s_handle->store_key), s_handle);

 out:
	return s_handle;

 err_9:
	if (s_handle->altheader)
		fclose(s_handle->headerfile);
 err_8:
	free(s_handle->headerfilename);
 err_7:
	fclose(s_handle->file);
 err_6:
	free(s_handle->filename);
 err_5:
	free(s_handle->schema);
 err_4:
	free(s_handle->container);
 err_3:
	free(s_handle->path);
 err_2:
	free(s_handle);
 err_1:
	free(store_key);
 err_0:
	return NULL;
}

/* protected by strgp->lock */
static struct csv_row_schema_rbn_s *
csv_row_schema_get(ldmsd_strgp_t strgp, struct csv_row_store_handle *rs_handle,
		   struct csv_row_schema_key_s *key)
{
	struct csv_row_schema_rbn_s *rrbn;

	rrbn = (void*)rbt_find(&rs_handle->row_schema_rbt, key);
	if (rrbn)
		return rrbn;
	rrbn = calloc(1, sizeof(*rrbn));
	if (!rrbn) {
		ERR_LOG("Out of memory: %s:%s():%d\n", __FILE__, __func__, __LINE__);
		goto err_0;
	}
	rbn_init(&rrbn->rbn, &rrbn->key);
	rrbn->key.name = rrbn->name;
	rrbn->key.digest = &rrbn->digest;
	snprintf(rrbn->name, sizeof(rrbn->name), "%s", key->name);
	memcpy(&rrbn->digest, key->digest, sizeof(*key->digest));

	pthread_mutex_lock(&cfg_lock);
	rrbn->s_handle = csv_store_handle_get(strgp->container, key->name);
	pthread_mutex_unlock(&cfg_lock);
	if (!rrbn->s_handle)
		goto err_1;

	rbt_ins(&rs_handle->row_schema_rbt, &rrbn->rbn);
	return rrbn;
 err_1:
	free(rrbn);
 err_0:
	return NULL;
}

typedef struct csv_store_col_info_s {
	struct csv_store_handle *s_handle;
	ldms_mval_t v;
	const char *ustr;
	const char *sep;
	const char *wsqt;
	int i;
} *csv_store_col_info_t;

typedef int (*csv_store_col_fn)(csv_store_col_info_t ci);

static int store_col_char(csv_store_col_info_t ci)
{
	return fprintf(ci->s_handle->file, "%s%s%c", ci->ustr, ci->sep, ci->v->v_char);
}

static int store_col_u8(csv_store_col_info_t ci)
{
	return fprintf(ci->s_handle->file, "%s%s%hhu", ci->ustr, ci->sep, ci->v->v_u8);
}

static int store_col_s8(csv_store_col_info_t ci)
{
	return fprintf(ci->s_handle->file, "%s%s%hhd", ci->ustr, ci->sep, ci->v->v_s8);
}

static int store_col_u16(csv_store_col_info_t ci)
{
	return fprintf(ci->s_handle->file, "%s%s%hu", ci->ustr, ci->sep, ci->v->v_u16);
}

static int store_col_s16(csv_store_col_info_t ci)
{
	return fprintf(ci->s_handle->file, "%s%s%hd", ci->ustr, ci->sep, ci->v->v_s16);
}

static int store_col_u32(csv_store_col_info_t ci)
{
	return fprintf(ci->s_handle->file, "%s%s%u", ci->ustr, ci->sep, ci->v->v_u32);
}

static int store_col_s32(csv_store_col_info_t ci)
{
	return fprintf(ci->s_handle->file, "%s%s%d", ci->ustr, ci->sep, ci->v->v_s32);
}

static int store_col_u64(csv_store_col_info_t ci)
{
	return fprintf(ci->s_handle->file, "%s%s%lu", ci->ustr, ci->sep, ci->v->v_u64);
}

static int store_col_s64(csv_store_col_info_t ci)
{
	return fprintf(ci->s_handle->file, "%s%s%ld", ci->ustr, ci->sep, ci->v->v_s64);
}

static int store_col_f(csv_store_col_info_t ci)
{
	return fprintf(ci->s_handle->file, "%s%s%f", ci->ustr, ci->sep, ci->v->v_f);
}

static int store_col_d(csv_store_col_info_t ci)
{
	return fprintf(ci->s_handle->file, "%s%s%f", ci->ustr, ci->sep, ci->v->v_d);
}

static int store_col_ts(csv_store_col_info_t ci)
{
	if (ci->s_handle->time_format == TF_MILLISEC) {
		/* Alternate time format. First field is milliseconds-since-epoch,
		   and the second field is the left-over microseconds */
		return fprintf(ci->s_handle->file, "%s%s%lu,%u",
				ci->ustr, ci->sep,
				((uint64_t)ci->v->v_ts.sec * 1000) + (ci->v->v_ts.usec / 1000),
				ci->v->v_ts.usec % 1000);
	} else {
		/* Traditional time format, where the first field is
		   <seconds>.<microseconds>, second is microseconds repeated */
		return fprintf(ci->s_handle->file, "%s%s%u.%06u,%u",
				ci->ustr, ci->sep, ci->v->v_ts.sec,
				ci->v->v_ts.usec, ci->v->v_ts.usec);
	}
}

static int store_col_char_array(csv_store_col_info_t ci)
{
	return fprintf(ci->s_handle->file, "%s%s%s%s%s",
			ci->ustr, ci->sep, ci->wsqt, ci->v->a_char, ci->wsqt);
}

static int store_col_u8_array(csv_store_col_info_t ci)
{
	return fprintf(ci->s_handle->file, "%s%s%hhu", ci->ustr, ci->sep, ci->v->a_u8[ci->i]);
}

static int store_col_s8_array(csv_store_col_info_t ci)
{
	return fprintf(ci->s_handle->file, "%s%s%hhd", ci->ustr, ci->sep, ci->v->a_s8[ci->i]);
}

static int store_col_u16_array(csv_store_col_info_t ci)
{
	return fprintf(ci->s_handle->file, "%s%s%hu", ci->ustr, ci->sep, ci->v->a_u16[ci->i]);
}

static int store_col_s16_array(csv_store_col_info_t ci)
{
	return fprintf(ci->s_handle->file, "%s%s%hd", ci->ustr, ci->sep, ci->v->a_s16[ci->i]);
}

static int store_col_u32_array(csv_store_col_info_t ci)
{
	return fprintf(ci->s_handle->file, "%s%s%u", ci->ustr, ci->sep, ci->v->a_u32[ci->i]);
}

static int store_col_s32_array(csv_store_col_info_t ci)
{
	return fprintf(ci->s_handle->file, "%s%s%d", ci->ustr, ci->sep, ci->v->a_s32[ci->i]);
}

static int store_col_u64_array(csv_store_col_info_t ci)
{
	return fprintf(ci->s_handle->file, "%s%s%lu", ci->ustr, ci->sep, ci->v->a_u64[ci->i]);
}

static int store_col_s64_array(csv_store_col_info_t ci)
{
	return fprintf(ci->s_handle->file, "%s%s%ld", ci->ustr, ci->sep, ci->v->a_s64[ci->i]);
}

static int store_col_f_array(csv_store_col_info_t ci)
{
	return fprintf(ci->s_handle->file, "%s%s%.9g", ci->ustr, ci->sep, ci->v->a_f[ci->i]);
}

static int store_col_d_array(csv_store_col_info_t ci)
{
	return fprintf(ci->s_handle->file, "%s%s%.17g", ci->ustr, ci->sep, ci->v->a_d[ci->i]);
}

csv_store_col_fn __store_col_fn_tbl[] = {
	[LDMS_V_CHAR]       = store_col_char,
	[LDMS_V_U8]         = store_col_u8,
	[LDMS_V_S8]         = store_col_s8,
	[LDMS_V_U16]        = store_col_u16,
	[LDMS_V_S16]        = store_col_s16,
	[LDMS_V_U32]        = store_col_u32,
	[LDMS_V_S32]        = store_col_s32,
	[LDMS_V_U64]        = store_col_u64,
	[LDMS_V_S64]        = store_col_s64,
	[LDMS_V_F32]        = store_col_f,
	[LDMS_V_D64]        = store_col_d,
	[LDMS_V_CHAR_ARRAY] = store_col_char_array,
	[LDMS_V_U8_ARRAY]   = store_col_u8_array,
	[LDMS_V_S8_ARRAY]   = store_col_s8_array,
	[LDMS_V_U16_ARRAY]  = store_col_u16_array,
	[LDMS_V_S16_ARRAY]  = store_col_s16_array,
	[LDMS_V_U32_ARRAY]  = store_col_u32_array,
	[LDMS_V_S32_ARRAY]  = store_col_s32_array,
	[LDMS_V_U64_ARRAY]  = store_col_u64_array,
	[LDMS_V_S64_ARRAY]  = store_col_s64_array,
	[LDMS_V_F32_ARRAY]  = store_col_f_array,
	[LDMS_V_D64_ARRAY]  = store_col_d_array,
	[LDMS_V_TIMESTAMP]  = store_col_ts,
	[LDMS_V_LAST+1]     = NULL,
};

/* caller MUST hold s_handle->lock */
static int store_col(ldms_set_t set, struct csv_store_handle *s_handle,
		     ldmsd_col_t col, int is_first)
{
	uint64_t udata;
	int rc = 0, i, len;
	const char *ustr = "";
	char udata_str[64] = "";
	char udata_str_arr[64] = ""; /* for array elements */
	const char *sep = is_first?"":",";
	struct csv_store_col_info_s ci;
	csv_store_col_fn col_fn;

	col_fn = col->type > LDMS_V_LAST ? NULL : __store_col_fn_tbl[col->type];
	if (!col_fn) {
		ERR_LOG("Unsupported type %d: %s\n", col->type, ldms_metric_type_to_str(col->type));
		return EINVAL;
	}

	if (s_handle->udata && !is_phony_metric_id(col->metric_id)) {
		/* NOTE: Phony metrics do NOT have udata. */
		udata = ldms_metric_user_data_get(set, col->metric_id);
		snprintf(udata_str, sizeof(udata_str), "%s%lu", sep, udata);
		sep = ",";
		snprintf(udata_str_arr, sizeof(udata_str_arr), "%s%lu", sep, udata);
		ustr = udata_str;
	}

	ci.ustr = ustr;
	ci.sep = sep;
	ci.s_handle = s_handle;
	ci.v = col->mval;
	ci.wsqt = s_handle->ietfcsv?"\"":""; /* ietf quotation wrapping strings */

	if (ldms_type_is_array(col->type) && col->type != LDMS_V_CHAR_ARRAY) {
		/* array */
		for (i = 0; i < col->array_len; i++) {
			ci.i = i;
			len = col_fn(&ci);
			if (len < 0) {
				rc = errno;
				ERR_LOG("Error %d writing to '%s'\n", errno, s_handle->filename);
			} else {
				s_handle->byte_count += len;
			}
			if (i == 0) {
				/* make sure to have "," for the rest */
				ci.ustr = udata_str_arr;
				ci.sep = ",";
			}
		}
	} else {
		/* single value */
		len = col_fn(&ci);
		if (len < 0) {
			rc = errno;
			ERR_LOG("Error %d writing to '%s'\n", errno, s_handle->filename);
		} else {
			s_handle->byte_count += len;
		}
	}

	return rc;
}

static int
store_row(ldmsd_strgp_t strgp, ldms_set_t set, struct csv_store_handle *s_handle, ldmsd_row_t row)
{
	int rc, i, col_rc;
	ldmsd_col_t col;

	rc = 0;

	pthread_mutex_lock(&s_handle->lock);

	/* headers */
	switch (s_handle->printheader){
	case DO_PRINT_HEADER:
		/* fall thru */
	case FIRST_PRINT_HEADER:
		rc = print_header_from_row(s_handle, set, row);
		if (rc){
			msglog(LDMSD_LERROR, PNAME ": %s cannot print header: %d. Not storing\n",
			       s_handle->store_key, rc);
			s_handle->printheader = BAD_HEADER;
			/* FIXME: will returning an error stop the store? */
			goto out;
		}
		break;
	case BAD_HEADER:
		rc = EINVAL;
		goto out;
	default:
		/* ok to continue */
		break;
	}

	for (i = 0; i < row->col_count; i++) {
		col = &row->cols[i];
		col_rc = store_col(set, s_handle, col, 0 == i);
		if (col_rc)
			rc = col_rc;
	}
	fprintf(s_handle->file, "\n");
	int doflush = 0;
	if ((s_handle->buffer_type == 3) &&
	    ((s_handle->store_count - s_handle->lastflush) >=
	     s_handle->buffer_sz)){
		s_handle->lastflush = s_handle->store_count;
		doflush = 1;
	} else if ((s_handle->buffer_type == 4) &&
		 ((s_handle->byte_count - s_handle->lastflush) >=
		  s_handle->buffer_sz)){
		s_handle->lastflush = s_handle->byte_count;
		doflush = 1;
	}
	if ((s_handle->buffer_sz == 0) || doflush){
		fflush(s_handle->file);
		fsync(fileno(s_handle->file));
	}
 out:
	pthread_mutex_unlock(&s_handle->lock);
	return rc;
}

/* This function is protected by strgp->lock */
static int
commit_rows(ldmsd_strgp_t strgp, ldms_set_t set,
	    ldmsd_row_list_t row_list, int row_count)
{
	struct csv_row_store_handle *rs_handle;
	struct csv_row_schema_rbn_s *rbn;
	struct csv_row_schema_key_s key;
	ldmsd_row_t row;

	rs_handle = strgp->store_handle;
	if (!rs_handle) {
		rs_handle = strgp->store_handle = row_store_new();
		if (!rs_handle)
			return ENOMEM;
	}

	if (rs_handle->type != CSV_ROW_STORE_HANDLE) {
		ERR_LOG("Invalid handle type\n");
		assert(0 == "Invalid handle type");
		return EINVAL;
	}

	TAILQ_FOREACH(row, row_list, entry) {
		/* get schema */
		key.digest = row->schema_digest;
		key.name = row->schema_name;
		/* protected by strgp->lock */
		rbn = (void*)rbt_find(&rs_handle->row_schema_rbt, &key);
		if (!rbn) {
			rbn = csv_row_schema_get(strgp, rs_handle, &key);
			if (!rbn) {
				/* csv_row_schema_get() already log the error */
				continue;
			}
		}

		/* write row */
		store_row(strgp, set, rbn->s_handle, row);
	}
	return 0;
}

static struct ldmsd_store store_csv = {
	.base = {
			.name = "csv",
			.type = LDMSD_PLUGIN_STORE,
			.term = term,
			.config = config,
			.usage = usage,
	},
	.open = open_store,
	.get_context = get_ucontext,
	.store = store,
	.flush = flush_store,
	.close = close_store,
	.commit = commit_rows,
};

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	msglog = pf;
	PG.msglog = pf;
	PG.pname = PNAME;
	return &store_csv.base;
}

static void __attribute__ ((constructor)) store_csv_init();
static void store_csv_init()
{
	store_idx = idx_create();
	pthread_mutex_init(&cfg_lock, NULL);
	LIB_CTOR_COMMON(PG);
}

static void __attribute__ ((destructor)) store_csv_fini(void);
static void store_csv_fini()
{
	pthread_mutex_destroy(&cfg_lock);
	idx_destroy(store_idx);
	ldmsd_plugattr_destroy(pa);
	LIB_DTOR_COMMON(PG);
	if (rothread_used) {
		void * dontcare = NULL;
		pthread_cancel(rothread);
		pthread_join(rothread, &dontcare);
	}
	pa = NULL;
	store_idx = NULL;
}
