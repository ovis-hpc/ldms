/**
 * Copyright (c) 2013-2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2013-2019 Open Grid Computing, Inc. All rights reserved.
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

#include <ctype.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
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

static idx_t store_idx;
struct plugattr *pa = NULL; /* plugin attributes from config */
static int rollover;
static int rollagain;
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


static ldmsd_msg_log_f msglog;
static pthread_t rothread;
static int rothread_used = 0;

#define _stringify(_x) #_x
#define stringify(_x) _stringify(_x)

#define LOGFILE "/var/log/store_csv.log"

/*
 * New in v3: no more id_pos. Always producer name which is always written out before the metrics.
 */

struct csv_store_handle {
	struct ldmsd_store *store;
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
	CSV_STORE_HANDLE_COMMON;
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
	char tmp_path[PATH_MAX];
	char tmp_headerpath[PATH_MAX];
	char tmp_typepath[PATH_MAX];
	char tp1[PATH_MAX];
	char tp2[PATH_MAX];
	char tp3[PATH_MAX];
	struct roll_common roc = { tp1, tp2, tp3 };
	//if we've got here then we've called new_store, but it might be closed
	pthread_mutex_lock(&s_handle->lock);
	switch (rolltype) {
	case 1:
	case 2:
	case 5:
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

	//re name: if got here, then rollover requested
	snprintf(tmp_path, PATH_MAX, "%s.%d",
		 s_handle->path, (int) appx);
	nfp = fopen_perm(tmp_path, "a+", LDMSD_DEFAULT_FILE_PERM);
	if (!nfp){
		//we cant open the new file, skip
		msglog(LDMSD_LERROR, PNAME ": Error: cannot open file <%s>\n",
		       tmp_path);
		goto out;
	}
	ch_output(nfp, tmp_path, CSHC(s_handle), cps);

	notify_output(NOTE_OPEN, tmp_path, NOTE_DAT, CSHC(s_handle), cps);
	strcpy(roc.filename, tmp_path);

	if (s_handle->altheader){
		//re name: if got here, then rollover requested
		snprintf(tmp_headerpath, PATH_MAX,
			 "%s.HEADER.%d",
			 s_handle->path, (int)appx);
		/* truncate a separate headerfile if it exists.
		 * FIXME: do we still want to do this? */
		nhfp = fopen_perm(tmp_headerpath, "w", LDMSD_DEFAULT_FILE_PERM);
		if (!nhfp){
			fclose(nfp);
			msglog(LDMSD_LERROR, PNAME ": Error: cannot open file <%s>\n",
			       tmp_headerpath);
		} else {
			ch_output(nhfp, tmp_headerpath, CSHC(s_handle), cps);
		}
		notify_output(NOTE_OPEN, tmp_headerpath,
			NOTE_HDR, CSHC(s_handle), cps);
		strcpy(roc.headerfilename, tmp_headerpath);
	} else {
		nhfp = fopen_perm(tmp_path, "a+", LDMSD_DEFAULT_FILE_PERM);
		if (!nhfp){
			fclose(nfp);
			msglog(LDMSD_LERROR, PNAME ": Error: cannot open file <%s>\n",
			       tmp_path);
		} else {
			ch_output(nhfp, tmp_path, CSHC(s_handle), cps);
		}
		notify_output(NOTE_OPEN, tmp_path, NOTE_HDR,
			CSHC(s_handle), cps);
		strcpy(roc.headerfilename, tmp_path);
	}
	if (!nhfp) {
		goto out;
	}

	//close and swap
	if (s_handle->file) {
		fclose(s_handle->file);
		notify_output(NOTE_CLOSE, s_handle-> filename, NOTE_DAT,
			CSHC(s_handle), cps);
		rename_output(s_handle->filename, NOTE_DAT,
			CSHC(s_handle), cps);
	}
	if (s_handle->headerfile) {
		fclose(s_handle->headerfile);
	}
	if (s_handle->headerfilename) {
		notify_output(NOTE_CLOSE, s_handle-> headerfilename, NOTE_HDR,
			CSHC(s_handle), cps);
		rename_output(s_handle->headerfilename, NOTE_HDR,
			CSHC(s_handle), cps);
	}
	if (s_handle->typefilename) {
		rename_output(s_handle->typefilename, NOTE_KIND,
			CSHC(s_handle), cps);
		snprintf(tmp_typepath, PATH_MAX, "%s.KIND.%d",
			s_handle->path, (int)appx);
		strcpy(roc.typefilename, tmp_typepath);
		replace_string(&(s_handle->typefilename), roc.typefilename);
	}
	s_handle->file = nfp;
	replace_string(&(s_handle->filename), roc.filename);
	replace_string(&(s_handle->headerfilename), roc.headerfilename);
	s_handle->headerfile = nhfp;
	s_handle->otime = appx;
	s_handle->printheader = DO_PRINT_HEADER;

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

	bool r = false; /* default if not in pa */
	int cvt = ldmsd_plugattr_bool(pa, "userdata", k, &r);
	if (cvt == -1) {
		msglog(LDMSD_LERROR, PNAME ": improper userdata= input.\n");
		return EINVAL;
	}
	s_handle->udata = r;

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
			msglog(LDMSD_LERROR,
				"%s: rolltype=5 needs rollagain > max(rollover,10)\n");
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
		NOTIFY_USAGE
		"         - userdata     UserData in printout (optional, default 0)\n"
		"         - metapath A string template for the file rename, where %[BCDPSTs]\n"
		"           are replaced per the man page Plugin_store_csv.\n"
		"         - rollover  Greater than or equal to zero; enables file rollover and sets interval\n"
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

/*
 * note: this should be residual from v2 where we may not have had the header info until a store was called
 * which then meant we had the mvec. ideally the print_header will always happen from the store_open.
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
		&PG, set, metric_array, metric_count);

	/* Flush for the header, whether or not it is the data file as well */
	fflush(fp);
	fsync(fileno(fp));

	fclose(s_handle->headerfile);
	s_handle->headerfile = 0;
	fp = NULL;

	/* dump data types header, or whine and continue to other headers. */
	if (s_handle->typeheader > 0 && s_handle->typeheader <= TH_MAX) {
		fp = fopen(s_handle->typefilename, "w");
		if (!fp) {
			int rc = errno;
			PG.msglog(LDMSD_LERROR, PNAME ": print_header: %s "
				"failed to open types file (%d).\n",
				s_handle->typefilename, rc);
		} else {
			ch_output(fp, tmp_path, CSHC(s_handle), &PG);
			csv_format_types_common(s_handle->typeheader, fp, 
				s_handle->typefilename, CCSHC(s_handle),
				s_handle->udata, &PG, set,
				metric_array, metric_count);
			fclose(fp);
			struct csv_store_handle_common *sh = CSHC(s_handle);
			notify_output(NOTE_OPEN, tmp_path, NOTE_KIND, sh, &PG);
			notify_output(NOTE_CLOSE, tmp_path, NOTE_KIND, sh, &PG);
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

static ldmsd_store_handle_t
open_store(struct ldmsd_store *s, const char *container, const char* schema,
		struct ldmsd_strgp_metric_list *list, void *ucontext)
{
	struct csv_store_handle *s_handle = NULL;
	int add_handle = 0;
	int rc = 0;
	char* skey = NULL;
	char* path = NULL;
	char *hcontainer = NULL;
	char *hschema = NULL;

	if (!pa) {
		msglog(LDMSD_LERROR, PNAME ": config not called. cannot open.\n");
		return NULL;
	}

	pthread_mutex_lock(&cfg_lock);
	skey = allocStoreKey(container, schema);
	if (skey == NULL){
		msglog(LDMSD_LERROR, PNAME ": Cannot open store\n");
		goto out;
	}

	s_handle = idx_find(store_idx, (void *)skey, strlen(skey));
	const char *root_path = ldmsd_plugattr_value(pa, "path", skey);
	/* ideally, s_handle should always be null, because we would have closed
	 * and removed an existing container before reopening it. defensively
	 * keeping this like v2.
	 * Otherwise then could always do all these steps.
	 */
	if (!s_handle) {
		size_t pathlen = strlen(root_path) +
			strlen(schema) +
			strlen(container) + 8;
		path = malloc(pathlen);
		if (!path)
			goto out;

		/* New in v3: this is a name change */
		sprintf(path, "%s/%s/%s", root_path, container, schema);
		s_handle = calloc(1, sizeof *s_handle);
		if (!s_handle)
			goto err0;

		s_handle->ucontext = ucontext;
		s_handle->store = s;
		add_handle = 1;

		pthread_mutex_init(&s_handle->lock, NULL);
		s_handle->path = path;
		s_handle->store_key = strdup(skey);
		hcontainer = container ? strdup(container) : NULL;
		hschema = schema ? strdup(schema) : NULL;
		if (!s_handle->store_key || ! hcontainer || ! hschema ||
			config_handle(s_handle)) {
			/* Take the lock becauase we will unlock in the err path */
			pthread_mutex_lock(&s_handle->lock);
			goto err1;
		}
		s_handle->container = hcontainer;
		s_handle->schema = hschema;
		hcontainer = hschema = NULL;

		s_handle->store_count = 0;
		s_handle->byte_count = 0;
		s_handle->lastflush = 0;

		s_handle->printheader = FIRST_PRINT_HEADER;
	} else {
		s_handle->printheader = DO_PRINT_HEADER;
	}

	/* Take the lock in case its a store that has been closed */
	pthread_mutex_lock(&s_handle->lock);

	/* create path if not already there. */
	char *dpath = strdup(s_handle->path);
	if (!dpath) {
		msglog(LDMSD_LERROR, PNAME ": strdup failed creating directory '%s'\n",
			 errno, s_handle->path);
		goto err2;
	}
	sprintf(dpath, "%s/%s", root_path, container);
	rc = create_outdir(dpath, CSHC(s_handle), &PG);
	free(dpath);
	if ((rc != 0) && (errno != EEXIST)) {
		msglog(LDMSD_LERROR, PNAME ": Failure %d creating directory containing '%s'\n",
			 errno, path);
		goto err2;
	}

	/* For both actual new store and reopened store, open the data file */
	char tmp_path[PATH_MAX];
	time_t appx = time(NULL);
	if (rolltype >= MINROLLTYPE){
		//append the files with epoch. assume wont collide to the sec.
		snprintf(tmp_path, PATH_MAX, "%s.%d",
			 s_handle->path, (int)appx);
	} else {
		snprintf(tmp_path, PATH_MAX, "%s",
			 s_handle->path);
	}

	char tp1[PATH_MAX];
	char tp2[PATH_MAX];
	char tp3[PATH_MAX];
	struct roll_common roc = { tp1, tp2, tp3 };
	if (!s_handle->file) { /* theoretically, we should never already have this file */
		s_handle->file = fopen_perm(tmp_path, "a+", LDMSD_DEFAULT_FILE_PERM);
		s_handle->otime = appx;
	}
	if (!s_handle->file){
		msglog(LDMSD_LERROR, PNAME ": Error %d opening the file %s.\n",
		       errno, s_handle->path);
		goto err2;
	}
	ch_output(s_handle->file, tmp_path, CSHC(s_handle), &PG);
	strcpy(roc.filename, tmp_path);
	replace_string(&(s_handle->filename), roc.filename);

	/*
	 * Always reprint the header because this is a store that may have been
	 * closed and then reopened because a new metric has been added.
	 * New in v3: since it may be a new set of metrics, possibly append to the header.
	 */

	if (!s_handle->headerfile){ /* theoretically, we should never already have this file */
		if (s_handle->altheader) {
			char tmp_headerpath[PATH_MAX];
			if (rolltype >= MINROLLTYPE){
				snprintf(tmp_headerpath, PATH_MAX,
					 "%s.HEADER.%d", s_handle->path, (int)appx);
			} else {
				snprintf(tmp_headerpath, PATH_MAX,
					 "%s.HEADER", s_handle->path);
			}

			/* truncate a separate headerfile if its the first time */
			if (s_handle->printheader == FIRST_PRINT_HEADER){
				s_handle->headerfile = fopen_perm(tmp_headerpath, "w", LDMSD_DEFAULT_FILE_PERM);
			} else if (s_handle->printheader == DO_PRINT_HEADER){
				s_handle->headerfile = fopen_perm(tmp_headerpath, "a+", LDMSD_DEFAULT_FILE_PERM);
			}
			strcpy(roc.headerfilename, tmp_headerpath);
		} else {
			s_handle->headerfile = fopen_perm(tmp_path, "a+", LDMSD_DEFAULT_FILE_PERM);
			strcpy(roc.headerfilename, tmp_path);
		}

		if (!s_handle->headerfile) {
			msglog(LDMSD_LERROR, PNAME ": Error: Cannot open headerfile\n");
			goto err3;
		}
		ch_output(s_handle->headerfile, tmp_path, CSHC(s_handle), &PG);
	}
	replace_string(&(s_handle->headerfilename), roc.headerfilename);

	if (s_handle->typeheader > 0) {
		char tmp_typepath[PATH_MAX];
		if (rolltype >= MINROLLTYPE){
			snprintf(tmp_typepath, PATH_MAX, "%s.KIND.%d",
				s_handle->path, (int)appx);
		} else {
			snprintf(tmp_typepath, PATH_MAX, "%s.KIND",
				s_handle->path);
		}
		strcpy(roc.typefilename, tmp_typepath);
		replace_string(&(s_handle->typefilename), roc.typefilename);
	}

	/* ideally here should always be the printing of the header, and
	 * we could drop keeping track of printheader.
	 * FIXME: cant do this because only at store() do we know types/sizes
	 * of metrics that go with names of metrics.
	 * print_header_from_open(s_handle, struct ldmsd_store_metric_index_list *list, void *ucontext);
	 * Must make open_store a conditional part of store() if
	 * this is to be done.
	 */

#if 0
	print_csv_store_handle_common(CSHC(s_handle), &PG);
	ldmsd_plugattr_log(LDMSD_LINFO, pa, NULL);
#endif
	if (add_handle) {
		idx_add(store_idx, (void *)skey, strlen(skey), s_handle);
	}

	notify_output(NOTE_OPEN, s_handle->filename, NOTE_DAT, CSHC(s_handle), &PG);
	notify_output(NOTE_OPEN, s_handle->headerfilename, NOTE_HDR,
		CSHC(s_handle), &PG);
	pthread_mutex_unlock(&s_handle->lock);
	goto out;

err3:
	fclose(s_handle->file);
	s_handle->file = NULL;
err2:
	free(s_handle->store_key);
err1:
	pthread_mutex_unlock(&s_handle->lock);
	pthread_mutex_destroy(&s_handle->lock);
	free(s_handle);
	s_handle = NULL;
err0:
	free(path);
out:
	if (skey)
		  free(skey);
	if (hschema)
		  free(hschema);
	if (hcontainer)
		  free(hcontainer);
	pthread_mutex_unlock(&cfg_lock);
	return s_handle;
}

static int store(ldmsd_store_handle_t _s_handle, ldms_set_t set, int *metric_array, size_t metric_count)
{
	const struct ldms_timestamp _ts = ldms_transaction_timestamp_get(set);
	const struct ldms_timestamp *ts = &_ts;
	const char* pname;
	uint64_t udata;
	struct csv_store_handle *s_handle;
	uint32_t len;
	int i, j;
	int doflush = 0;
	int rc, rcu;

	s_handle = _s_handle;
	if (!s_handle)
		return EINVAL;

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

	fprintf(s_handle->file, "%"PRIu32".%06"PRIu32 ",%"PRIu32,
		ts->sec, ts->usec, ts->usec);
	pname = ldms_set_producer_name_get(set);
	if (pname != NULL){
		fprintf(s_handle->file, ",%s", pname);
		s_handle->byte_count += strlen(pname);
	} else {
		fprintf(s_handle->file, ",");
	}

	/* FIXME: will we want to throw an error if we cannot write? */
	char *wsqt = ""; /* ietf quotation wrapping strings */
	if (s_handle->ietfcsv) {
		wsqt = "\"";
	}
	const char * str;
	for (i = 0; i != metric_count; i++) {
		udata = ldms_metric_user_data_get(set, metric_array[i]);
		enum ldms_value_type metric_type = ldms_metric_type_get(set, metric_array[i]);
		//use same formats as ldms_ls
		switch (metric_type){
		case LDMS_V_CHAR_ARRAY:
			/* our csv does not included embedded nuls */
			str = ldms_metric_array_get_str(set, metric_array[i]);
			if (!str) {
				str = "";
			}
			if (s_handle->udata) {
				rc = fprintf(s_handle->file, ",%"PRIu64, udata);
				if (rc < 0)
					msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
					       rc, s_handle->path);
				else
					s_handle->byte_count += rc;
			}
			rc = fprintf(s_handle->file, ",%s%s%s", wsqt, str, wsqt);
			if (rc < 0)
				msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
				       rc, s_handle->path);
			else
				s_handle->byte_count += rc;
			break;
		case LDMS_V_U8_ARRAY:
			len = ldms_metric_array_get_len(set, metric_array[i]);
			for (j = 0; j < len; j++){
				if (s_handle->udata) {
					rcu = fprintf(s_handle->file, ",%"PRIu64, udata);
					if (rcu < 0)
						msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
						       rcu, s_handle->path);
					else
						s_handle->byte_count += rcu;
				}
				rc = fprintf(s_handle->file, ",%hhu",
					     ldms_metric_array_get_u8(set, metric_array[i], j));
				if (rc < 0)
					msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
					       rc, s_handle->path);
				else
					s_handle->byte_count += rc;
			}
			break;
		case LDMS_V_U8:
			if (s_handle->udata) {
				rcu = fprintf(s_handle->file, ",%"PRIu64, udata);
				if (rcu < 0)
					msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
					       rcu, s_handle->path);
				else
					s_handle->byte_count += rcu;
			}
			rc = fprintf(s_handle->file, ",%hhu",
				     ldms_metric_get_u8(set, metric_array[i]));
			if (rc < 0)
				msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
						rc, s_handle->path);
			else
				s_handle->byte_count += rc;
			break;
		case LDMS_V_S8_ARRAY:
			len = ldms_metric_array_get_len(set, metric_array[i]);
			for (j = 0; j < len; j++){
				if (s_handle->udata) {
					rcu = fprintf(s_handle->file, ",%"PRIu64, udata);
					if (rcu < 0)
						msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
						       rcu, s_handle->path);
					else
						s_handle->byte_count += rcu;
				}
				rc = fprintf(s_handle->file, ",%hhd",
					     ldms_metric_array_get_s8(set, metric_array[i], j));
				if (rc < 0)
					msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
							rc, s_handle->path);
				else
					s_handle->byte_count += rc;
			}
			break;
		case LDMS_V_S8:
			if (s_handle->udata) {
				rcu = fprintf(s_handle->file, ",%"PRIu64, udata);
				if (rcu < 0)
					msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
					       rcu, s_handle->path);
				else
					s_handle->byte_count += rcu;
			}
			rc = fprintf(s_handle->file, ",%hhd",
					     ldms_metric_get_s8(set, metric_array[i]));
			if (rc < 0)
				msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
							rc, s_handle->path);
			else
				s_handle->byte_count += rc;
			break;
		case LDMS_V_U16_ARRAY:
			len = ldms_metric_array_get_len(set, metric_array[i]);
			for (j = 0; j < len; j++){
				if (s_handle->udata) {
					rcu = fprintf(s_handle->file, ",%"PRIu64, udata);
					if (rcu < 0)
						msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
						       rcu, s_handle->path);
					else
						s_handle->byte_count += rcu;
				}
				rc = fprintf(s_handle->file, ",%hu",
						ldms_metric_array_get_u16(set, metric_array[i], j));
				if (rc < 0)
					msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
							rc, s_handle->path);
				else
					s_handle->byte_count += rc;
			}
			break;
		case LDMS_V_U16:
			if (s_handle->udata) {
				rcu = fprintf(s_handle->file, ",%"PRIu64, udata);
				if (rcu < 0)
					msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
					       rcu, s_handle->path);
				else
					s_handle->byte_count += rcu;
			}
			rc = fprintf(s_handle->file, ",%hu",
					ldms_metric_get_u16(set, metric_array[i]));
			if (rc < 0)
				msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
						rc, s_handle->path);
			else
				s_handle->byte_count += rc;
			break;
		case LDMS_V_S16_ARRAY:
			len = ldms_metric_array_get_len(set, metric_array[i]);
			for (j = 0; j < len; j++){
				if (s_handle->udata) {
					rcu = fprintf(s_handle->file, ",%"PRIu64, udata);
					if (rcu < 0)
						msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
						       rcu, s_handle->path);
					else
						s_handle->byte_count += rcu;
				}
				rc = fprintf(s_handle->file, ",%hd",
						ldms_metric_array_get_s16(set, metric_array[i], j));
				if (rc < 0)
					msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
							rc, s_handle->path);
				else
					s_handle->byte_count += rc;
			}
			break;
		case LDMS_V_S16:
			if (s_handle->udata) {
				rcu = fprintf(s_handle->file, ",%"PRIu64, udata);
				if (rcu < 0)
					msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
					       rcu, s_handle->path);
				else
					s_handle->byte_count += rcu;
			}
			rc = fprintf(s_handle->file, ",%hd",
					ldms_metric_get_s16(set, metric_array[i]));
			if (rc < 0)
				msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
						rc, s_handle->path);
			else
				s_handle->byte_count += rc;

			break;
		case LDMS_V_U32_ARRAY:
			len = ldms_metric_array_get_len(set, metric_array[i]);
			for (j = 0; j < len; j++){
				if (s_handle->udata) {
					rcu = fprintf(s_handle->file, ",%"PRIu64, udata);
					if (rcu < 0)
						msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
						       rcu, s_handle->path);
					else
						s_handle->byte_count += rcu;
				}
				rc = fprintf(s_handle->file, ",%" PRIu32,
						ldms_metric_array_get_u32(set, metric_array[i], j));
				if (rc < 0)
					msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
					       rc, s_handle->path);
				else
					s_handle->byte_count += rc;
			}
			break;
		case LDMS_V_U32:
			if (s_handle->udata) {
				rcu = fprintf(s_handle->file, ",%"PRIu64, udata);
				if (rcu < 0)
					msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
					       rcu, s_handle->path);
				else
					s_handle->byte_count += rcu;
			}
			rc = fprintf(s_handle->file, ",%" PRIu32,
					ldms_metric_get_u32(set, metric_array[i]));
			if (rc < 0)
				msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
						rc, s_handle->path);
			else
				s_handle->byte_count += rc;
			break;
		case LDMS_V_S32_ARRAY:
			len = ldms_metric_array_get_len(set, metric_array[i]);
			for (j = 0; j < len; j++){
				if (s_handle->udata) {
					rcu = fprintf(s_handle->file, ",%"PRIu64, udata);
					if (rcu < 0)
						msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
						       rcu, s_handle->path);
					else
						s_handle->byte_count += rcu;
				}
				rc = fprintf(s_handle->file, ",%" PRId32,
						ldms_metric_array_get_s32(set, metric_array[i], j));
				if (rc < 0)
					msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
							rc, s_handle->path);
				else
					s_handle->byte_count += rc;
			}
			break;
		case LDMS_V_S32:
			if (s_handle->udata) {
				rcu = fprintf(s_handle->file, ",%"PRIu64, udata);
				if (rcu < 0)
					msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
					       rcu, s_handle->path);
				else
					s_handle->byte_count += rcu;
			}
			rc = fprintf(s_handle->file, ",%" PRId32,
					ldms_metric_get_s32(set, metric_array[i]));
			if (rc < 0)
				msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
						rc, s_handle->path);
			else
				s_handle->byte_count += rc;
			break;
		case LDMS_V_U64_ARRAY:
			len = ldms_metric_array_get_len(set, metric_array[i]);
			for (j = 0; j < len; j++){
				if (s_handle->udata) {
					rcu = fprintf(s_handle->file, ",%"PRIu64, udata);
					if (rcu < 0)
						msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
						       rcu, s_handle->path);
					else
						s_handle->byte_count += rcu;
				}
				rc = fprintf(s_handle->file, ",%" PRIu64,
						ldms_metric_array_get_u64(set, metric_array[i], j));
				if (rc < 0)
					msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
							rc, s_handle->path);
				else
					s_handle->byte_count += rc;
			}
			break;
		case LDMS_V_U64:
			if (s_handle->udata) {
				rcu = fprintf(s_handle->file, ",%"PRIu64, udata);
				if (rcu < 0)
					msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
					       rcu, s_handle->path);
				else
					s_handle->byte_count += rcu;
			}
			rc = fprintf(s_handle->file, ",%"PRIu64,
					ldms_metric_get_u64(set, metric_array[i]));
			if (rc < 0)
				msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
						rc, s_handle->path);
			break;
		case LDMS_V_S64_ARRAY:
			len = ldms_metric_array_get_len(set, metric_array[i]);
			for (j = 0; j < len; j++){
				if (s_handle->udata) {
					rcu = fprintf(s_handle->file, ",%"PRIu64, udata);
					if (rcu < 0)
						msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
						       rcu, s_handle->path);
					else
						s_handle->byte_count += rcu;
				}
				rc = fprintf(s_handle->file, ",%" PRId64,
						ldms_metric_array_get_s64(set, metric_array[i], j));
				if (rc < 0)
					msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
							rc, s_handle->path);
				else
					s_handle->byte_count += rc;
			}
			break;
		case LDMS_V_S64:
			if (s_handle->udata) {
				rcu = fprintf(s_handle->file, ",%"PRIu64, udata);
				if (rcu < 0)
					msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
					       rcu, s_handle->path);
				else
					s_handle->byte_count += rcu;
			}
			rc = fprintf(s_handle->file, ",%" PRId64,
					ldms_metric_get_s64(set, metric_array[i]));
			if (rc < 0)
				msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
						rc, s_handle->path);
			else
				s_handle->byte_count += rc;
			break;
		case LDMS_V_F32_ARRAY:
			len = ldms_metric_array_get_len(set, metric_array[i]);
			for (j = 0; j < len; j++){
				if (s_handle->udata) {
					rcu = fprintf(s_handle->file, ",%"PRIu64, udata);
					if (rcu < 0)
						msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
						       rcu, s_handle->path);
					else
						s_handle->byte_count += rcu;
				}
				rc = fprintf(s_handle->file, ",%.9g",
						ldms_metric_array_get_float(set, metric_array[i], j));
				if (rc < 0)
					msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
							rc, s_handle->path);
				else
					s_handle->byte_count += rc;
			}
			break;
		case LDMS_V_F32:
			if (s_handle->udata) {
				rcu = fprintf(s_handle->file, ",%"PRIu64, udata);
				if (rcu < 0)
					msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
					       rcu, s_handle->path);
				else
					s_handle->byte_count += rcu;
			}
			rc = fprintf(s_handle->file, ",%.9g",
					ldms_metric_get_float(set, metric_array[i]));
			if (rc < 0)
				msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
						rc, s_handle->path);
			else
				s_handle->byte_count += rc;
			break;
		case LDMS_V_D64_ARRAY:
			len = ldms_metric_array_get_len(set, metric_array[i]);
			for (j = 0; j < len; j++){
				if (s_handle->udata) {
					rcu = fprintf(s_handle->file, ",%"PRIu64, udata);
					if (rcu < 0)
						msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
						       rcu, s_handle->path);
					else
						s_handle->byte_count += rcu;
				}
				rc = fprintf(s_handle->file, ",%.17g",
						ldms_metric_array_get_double(set, metric_array[i], j));
				if (rc < 0)
					msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
							rc, s_handle->path);
				else
					s_handle->byte_count += rc;
			}
			break;
		case LDMS_V_D64:
			if (s_handle->udata) {
				rcu = fprintf(s_handle->file, ",%"PRIu64, udata);
				if (rcu < 0)
					msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
					       rcu, s_handle->path);
				else
					s_handle->byte_count += rcu;
			}
			rc = fprintf(s_handle->file, ",%.17g",
					ldms_metric_get_double(set, metric_array[i]));
			if (rc < 0)
				msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
						rc, s_handle->path);
			else
				s_handle->byte_count += rc;
			break;
		default:
			if (!s_handle->conflict_warned) {
				msglog(LDMSD_LERROR, PNAME ":  metric id %d: no name at list index %d.\n", metric_array[i], i);
				msglog(LDMSD_LERROR, PNAME ": reconfigure to resolve schema definition conflict for schema=%s and instance=%s.\n",
					ldms_set_schema_name_get(set),
					ldms_set_instance_name_get(set));
				s_handle->conflict_warned = true;
			}
			/* print no value */
			if (s_handle->udata) {
				rcu = fprintf(s_handle->file, ",");
				if (rcu < 0)
					msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
					       rcu, s_handle->path);
				else
					s_handle->byte_count += rcu;
			}
			rc = fprintf(s_handle->file, ",");
			if (rc < 0)
				msglog(LDMSD_LERROR, PNAME ": Error %d writing to '%s'\n",
						rc, s_handle->path);
			else
				s_handle->byte_count += rc;
			break;
		}
	}
	fprintf(s_handle->file,"\n");

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

static void close_store(ldmsd_store_handle_t _s_handle)
{
	/* note: closing a store removes from the idx list.
	 * note: do not remove the specialkeys
	 */

	pthread_mutex_lock(&cfg_lock);
	struct csv_store_handle *s_handle = _s_handle;
	if (!s_handle) {
		pthread_mutex_unlock(&cfg_lock);
		return;
	}

	pthread_mutex_lock(&s_handle->lock);
	msglog(LDMSD_LDEBUG, PNAME ": Closing with path <%s>\n",
	       s_handle->path);
	fflush(s_handle->file);
	s_handle->store = NULL;
	if (s_handle->path)
		free(s_handle->path);
	s_handle->path = NULL;
	s_handle->ucontext = NULL;
	if (s_handle->file)
		fclose(s_handle->file);
	s_handle->file = NULL;
	if (s_handle->headerfile)
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
	pthread_mutex_unlock(&cfg_lock);

	free(s_handle); //FIXME: should this happen?
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
