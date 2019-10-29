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
#include <libgen.h>

#include <ovis_event/ovis_event.h>
#include <coll/idx.h>

#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_store.h"

#include "ldmsd_plugattr.h"
#include "store_common.h"
#include "store_csv_common.h"

#define INST(x) ((ldmsd_plugin_inst_t)(x))
#define INST_LOG(inst, lvl, fmt, ...) \
		ldmsd_log((lvl), "%s: " fmt, INST(inst)->inst_name, \
								##__VA_ARGS__)

#define TV_SEC_COL  0
#define TV_USEC_COL 1
#define GROUP_COL   2
#define VALUE_COL   3

/* ROLLTYPES documents rolltype and is used in help output.
 * Also used for buffering */
#define ROLLTYPES "\
                     1: wake approximately every rollover seconds and roll.\n\
                     2: wake daily at rollover seconds after midnight (>=0)\n\
                        and roll.\n\
                     3: roll after approximately rollover records are\n\
                        written.\n\
                     4: roll after approximately rollover bytes are written.\n\
                     5: wake daily at rollover seconds after midnight and \n\
                        every rollagain seconds thereafter.\n"
#define MAXROLLTYPE 5
#define MINROLLTYPE 1
/* default -- do not roll */
#define DEFAULT_ROLLTYPE -1
/* minimum rollover for type 1;
    rolltype==1 and rollover < MIN_ROLL_1 -> rollover = MIN_ROLL_1
    also used for minimum sleep time for type 2;
    rolltype==2 and rollover results in sleep < MIN_ROLL_SLEEPTIME -> skip this
		    roll and do it the next day */
#define MIN_ROLL_1 10
/* minimum rollover for type 3;
   rolltype==3 and rollover < MIN_ROLL_RECORDS -> rollover = MIN_ROLL_RECORDS */
#define MIN_ROLL_RECORDS 3
/* minimum rollover for type 4;
   rolltype==4 and rollover < MIN_ROLL_BYTES -> rollover = MIN_ROLL_BYTES */
#define MIN_ROLL_BYTES 1024
/* Interval to check for passing the record or byte count limits. */
#define ROLL_LIMIT_INTERVAL 60

#define _stringify(_x) #_x
#define stringify(_x) _stringify(_x)

#define LOGFILE "/var/log/store_csv.log"

typedef enum {
	CSV_CFGINIT_PRE,
	CSV_CFGINIT_IN,
	CSV_CFGINIT_DONE,
	CSV_CFGINIT_FAILED
} csvcfg_state;

typedef struct store_csv_inst_s *store_csv_inst_t;
struct store_csv_inst_s {
	struct ldmsd_plugin_inst_s base;

	csvcfg_state cfgstate;
	struct plugattr *pa; /* plugin attributes from config */
	int rollover;
	int rollagain;
	struct ovis_event_s roll_ev; /* rollover event */

	/* rolltype determines how to interpret rollover values > 0. */
	int rolltype;

	const char *path; /* points to `path=` in pa */
	FILE *file;
	FILE *headerfile;
	printheader_t printheader;
	int udata;
	bool ietfcsv; /* we will add an option like v2 enabling this soon. */
	pthread_mutex_t lock;
	void *ucontext;
	bool conflict_warned;
	int64_t lastflush;
	int64_t store_count;
	int64_t byte_count;

	CSV_STORE_HANDLE_COMMON;

	pthread_mutex_t cfg_lock;
};

ovis_scheduler_t roll_sched; /* roll-over scheduler */
pthread_t roll_thread; /* rollover thread */


__attribute__((unused))
static char* allocStoreKey(store_csv_inst_t inst, const char* container,
			   const char* schema)
{
	if ((container == NULL) || (schema == NULL) ||
			(strlen(container) == 0) || (strlen(schema) == 0)) {
		INST_LOG(inst, LDMSD_LERROR,
			 "container or schema null or empty. "
			 "cannot create key\n");
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
	store_csv_inst_t inst = obj;
	struct roll_cb_arg *args = cb_arg;
	time_t appx = args->appx;
	struct csv_plugin_static *cps = args->cps;

	FILE* nhfp = NULL;
	FILE* nfp = NULL;
	char tmp_path[PATH_MAX];
	char tmp_headerpath[PATH_MAX];
	char tp1[PATH_MAX];
	char tp2[PATH_MAX];
	struct roll_common roc = { tp1, tp2 };
	//if we've got here then we've called new_store, but it might be closed
	pthread_mutex_lock(&inst->lock);
	switch (inst->rolltype) {
	case 1:
	case 2:
	case 5:
		break;
	case 3:
		if (inst->store_count < inst->rollover)  {
			goto out;
		} else {
			inst->store_count = 0;
			inst->lastflush = 0;
		}
		break;
	case 4:
		if (inst->byte_count < inst->rollover) {
			goto out;
		} else {
			inst->byte_count = 0;
			inst->lastflush = 0;
		}
		break;
	default:
		INST_LOG(inst, LDMSD_LDEBUG,
			 "Error: unexpected rolltype in store(%d)\n",
			 inst->rolltype);
		break;
	}


	if (inst->file)
		fflush(inst->file);
	if (inst->headerfile)
		fflush(inst->headerfile);

	//re name: if got here, then rollover requested
	snprintf(tmp_path, PATH_MAX, "%s.%d",
		 inst->path, (int) appx);
	nfp = fopen_perm(tmp_path, "a+", LDMSD_DEFAULT_FILE_PERM);
	if (!nfp){
		//we cant open the new file, skip
		INST_LOG(inst, LDMSD_LERROR, "Error: cannot open file <%s>\n",
			 tmp_path);
		goto out;
	}
	ch_output(nfp, tmp_path, CSHC(inst), cps);

	notify_output(NOTE_OPEN, tmp_path, NOTE_DAT,
		CSHC(inst), cps, inst->container,
		inst->schema);
	strcpy(roc.filename, tmp_path);

	if (inst->altheader){
		//re name: if got here, then rollover requested
		snprintf(tmp_headerpath, PATH_MAX,
			 "%s.HEADER.%d",
			 inst->path, (int)appx);
		/* truncate a separate headerfile if it exists.
		 * FIXME: do we still want to do this? */
		nhfp = fopen_perm(tmp_headerpath, "w", LDMSD_DEFAULT_FILE_PERM);
		if (!nhfp){
			fclose(nfp);
			INST_LOG(inst, LDMSD_LERROR,
				 "Error: cannot open file <%s>\n",
				 tmp_headerpath);
		} else {
			ch_output(nhfp, tmp_headerpath, CSHC(inst), cps);
		}
		notify_output(NOTE_OPEN, tmp_headerpath,
			NOTE_HDR, CSHC(inst), cps,
			inst->container,
			inst->schema);
		strcpy(roc.headerfilename, tmp_headerpath);
	} else {
		nhfp = fopen_perm(tmp_path, "a+", LDMSD_DEFAULT_FILE_PERM);
		if (!nhfp){
			fclose(nfp);
			INST_LOG(inst, LDMSD_LERROR,
				 "Error: cannot open file <%s>\n",
				 tmp_path);
		} else {
			ch_output(nhfp, tmp_path, CSHC(inst), cps);
		}
		notify_output(NOTE_OPEN, tmp_path, NOTE_HDR,
			CSHC(inst), cps,
			inst->container,
			inst->schema);
		strcpy(roc.headerfilename, tmp_path);
	}
	if (!nhfp) {
		goto out;
	}

	//close and swap
	if (inst->file) {
		fclose(inst->file);
		notify_output(NOTE_CLOSE, inst->
			filename, NOTE_DAT, CSHC(inst),
			cps, inst->container,
			inst->schema);
		rename_output(inst->filename, NOTE_DAT,
			CSHC(inst), cps);
	}
	if (inst->headerfile) {
		fclose(inst->headerfile);
	}
	if (inst->headerfilename) {
		notify_output(NOTE_CLOSE, inst->
			headerfilename, NOTE_HDR,
			CSHC(inst), cps,
			inst->container,
			inst->schema);
		rename_output(inst->headerfilename, NOTE_HDR,
			CSHC(inst), cps);
	}
	inst->file = nfp;
	replace_string(&(inst->filename), roc.filename);
	replace_string(&(inst->headerfilename), roc.headerfilename);
	inst->headerfile = nhfp;
	inst->printheader = DO_PRINT_HEADER;

out:
	pthread_mutex_unlock(&inst->lock);
}

/* Time-based rolltypes will always roll the files when this
function is called.
Volume-based rolltypes must check and shortcircuit within this
function.
*/
static int handleRollover(store_csv_inst_t inst, struct csv_plugin_static *cps)
{
	//get the config lock
	//for every handle we have, do the rollover

	struct roll_cb_arg rca;

	pthread_mutex_lock(&inst->cfg_lock);

	rca.appx = time(NULL);
	rca.cps = cps;
	/* old: idx_traverse(inst->store_idx, roll_cb, (void *)&rca); */
	/* new store api, one instance has one container */
	roll_cb(inst, &rca);

	pthread_mutex_unlock(&inst->cfg_lock);

	return 0;

}

static void* rolloverThreadInit(void* m)
{
	int rc;
	rc = ovis_scheduler_loop(roll_sched, 0);
	if (rc) {
		ldmsd_log(LDMSD_LERROR,
			  "store_csv: rollover scheduler exited, rc: %d\n", rc);
	}
	return NULL;
}

static void rolloverTask(ovis_event_t ev);

static int scheduleRollover(store_csv_inst_t inst)
{
	int tsleep;
	time_t rawtime;
	struct tm *info;
	int secSinceMidnight;
	int rc;

	switch (inst->rolltype) {
	case 1:
		tsleep = (inst->rollover < MIN_ROLL_1) ?
			 MIN_ROLL_1 : inst->rollover;
		break;
	case 2:
		time( &rawtime );
		info = localtime( &rawtime );
		secSinceMidnight = info->tm_hour*3600 +
				   info->tm_min*60 + info->tm_sec;
		tsleep = 86400 - secSinceMidnight + inst->rollover;
		if (tsleep < MIN_ROLL_1) {
			/* if we just did a roll then skip this one */
			tsleep +=86400;
		}
		break;
	case 3:
		if (inst->rollover < MIN_ROLL_RECORDS)
			inst->rollover = MIN_ROLL_RECORDS;
		tsleep = ROLL_LIMIT_INTERVAL;
		break;
	case 4:
		if (inst->rollover < MIN_ROLL_BYTES)
			inst->rollover = MIN_ROLL_BYTES;
		tsleep = ROLL_LIMIT_INTERVAL;
		break;
	case 5:
		time(&rawtime);
		info = localtime(&rawtime);
		secSinceMidnight = info->tm_hour*3600 +
				   info->tm_min*60 + info->tm_sec;

		if (secSinceMidnight < inst->rollover) {
			tsleep = inst->rollover - secSinceMidnight;
		} else {
			int y = secSinceMidnight - inst->rollover;
			int z = y / inst->rollagain;
			tsleep = (z + 1)*inst->rollagain + inst->rollover -
				 secSinceMidnight;
		}
		if (tsleep < MIN_ROLL_1) {
			tsleep += inst->rollagain;
		}
		break;
	default:
		tsleep = 60;
		break;
	}

	inst->roll_ev.param.cb_fn = rolloverTask;
	inst->roll_ev.param.type = OVIS_EVENT_TIMEOUT;
	inst->roll_ev.param.ctxt = inst;
	inst->roll_ev.param.timeout.tv_sec = tsleep;
	rc = ovis_scheduler_event_add(roll_sched, &inst->roll_ev);
	return rc;
}

static void rolloverTask(ovis_event_t ev)
{
	//if got here, then rollover requested
	store_csv_inst_t inst = ev->param.ctxt;
	ovis_scheduler_event_del(roll_sched, ev);
	handleRollover(inst, &PG);
	scheduleRollover(inst);
}

/* return 1 if blacklisted attr/kw found in avl/kwl, else 0 */
static int attr_blacklist(store_csv_inst_t inst, const char **bad,
			  const struct attr_value_list *kwl,
			  const struct attr_value_list *avl,
			  const char *context)
{
	if (!bad) {
		INST_LOG(inst, LDMSD_LERROR, "attr_blacklist miscalled.\n");
		return 1;
	}
	int badcount = 0;
	if (kwl) {
		const char **p = bad;
		while (*p != NULL) {
			int i = av_idx_of(kwl, *p);
			if (i != -1) {
				badcount++;
				INST_LOG(inst, LDMSD_LERROR, "%s %s.\n",
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
				INST_LOG(inst, LDMSD_LERROR, "%s %s.\n",
					 *p, context);
			}
			p++;
		}
	}
	if (badcount)
		return 1;
	return 0;
}


/* ==== CONFIG HANDLERS ==== */

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

static int update_config(store_csv_inst_t inst, struct attr_value_list *kwl,
			 struct attr_value_list *avl)
{
	int rc = 0;
	pthread_mutex_lock(&inst->cfg_lock);
	if (inst->file) {
		INST_LOG(inst, LDMSD_LWARNING,
			 "updating config not allowed after "
			 "store is running.\n");
		/* s_handle *could* consult pa again, but that
		is up to the implementation of store().
		Right now it never uses pa after open_store.
		*/
	} else {
		INST_LOG(inst, LDMSD_LINFO, "adding config.\n");
		int bl = attr_blacklist(inst, update_blacklist, kwl, avl,
					"not allowed in add");
		if (bl) {
			char *as = av_to_string(avl, 0);
			char *ks = av_to_string(kwl, 0);
			INST_LOG(inst, LDMSD_LINFO,
				 "fix config args %s %s\n", as, ks);
			rc = EINVAL;
			goto out;
		}

		rc = ldmsd_plugattr_add(inst->pa, avl, kwl, update_blacklist,
					update_blacklist, dep, KEY_PLUG_ATTR);
		if (rc == EEXIST) {
			INST_LOG(inst, LDMSD_LERROR, "cannot repeat config.\n");
		} else if (rc != 0) {
			INST_LOG(inst, LDMSD_LINFO,
				 "config failed (%d).\n", rc);
		}
	}
out:
	pthread_mutex_unlock(&inst->cfg_lock);
	return rc;
}



/* ============== Store Plugin APIs ================= */

/*
 * note: this should be residual from v2 where we may not have had the header info until a store was called
 * which then meant we had the mvec. ideally the print_header will always happen from the store_open.
 * Currently we still have to keep this to invert the metric order
 */
static int print_header_from_store(store_csv_inst_t inst,
				   ldms_set_t set,
				   ldmsd_strgp_t strgp)
{
	/* Only called from Store which already has the lock */
	FILE* fp;
	uint32_t len;
	int i, j;

	inst->printheader = DONT_PRINT_HEADER;

	fp = inst->headerfile;
	if (!fp) {
		INST_LOG(inst, LDMSD_LERROR,
			 "Cannot print header. No headerfile\n");
		return EINVAL;
	}

	/* This allows optional loading a float (Time) into an int field and
	   retaining usec as a separate field */
	fprintf(fp, "#Time,Time_usec,ProducerName");

	for (i = 0; i != strgp->metric_count; i++) {
		const char* name = ldms_metric_name_get(set, strgp->metric_arry[i]);
		enum ldms_value_type metric_type = ldms_metric_type_get(set, strgp->metric_arry[i]);

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
			len = ldms_metric_array_get_len(set,
							strgp->metric_arry[i]);
			if (inst->udata) {
				for (j = 0; j < len; j++) {
					//there is only 1 name for all of them.
					fprintf(fp, ",%s%d.userdata,%s%d.value",
						name, j, name, j);
				}
			} else {
				for (j = 0; j < len; j++) {
					//there is only 1 name for all of them.
					fprintf(fp, ",%s%s%d", name,
							HDR_ARRAY_SEP, j);
				}
			}
			break;
		default:
			if (inst->udata) {
				fprintf(fp, ",%s.userdata,%s.value",
					name, name);
			} else {
				fprintf(fp, ",%s", name);
			}
			break;
		}
	}

	fprintf(fp, "\n");

	/* Flush for the header, whether or not it is the data file as well */
	fflush(fp);
	fsync(fileno(fp));

	fclose(inst->headerfile);
	inst->headerfile = 0;

	return 0;
}


int store_csv_open(ldmsd_plugin_inst_t pi, ldmsd_strgp_t strgp)
{
	/* Perform `open` operation */
	store_csv_inst_t inst = (void*)pi;
	int rc = 0;
	char tmp_path[PATH_MAX];

	if (!inst->pa) {
		INST_LOG(inst, LDMSD_LERROR,
			 "config not called. cannot open.\n");
		return EINVAL;
	}

	if (inst->file) {
		INST_LOG(inst, LDMSD_LERROR,
			 "file already opened.\n");
		return EALREADY;
	}

	pthread_mutex_lock(&inst->cfg_lock);

	/* New in v3: this is a name change */

	inst->store_count = 0;
	inst->byte_count = 0;
	inst->lastflush = 0;

	inst->printheader = FIRST_PRINT_HEADER;

	/* create path if not already there. */
	snprintf(tmp_path, sizeof(tmp_path), "%s", inst->path);
	char *s;
	s = dirname(tmp_path);
	rc = create_outdir(s, CSHC(inst), &PG);
	if ((rc != 0) && (errno != EEXIST)) {
		INST_LOG(inst, LDMSD_LERROR,
			 "Failure %d creating directory containing '%s'\n",
			 errno, inst->path);
		goto out;
	}

	/* For both actual new store and reopened store, open the data file */
	time_t appx = time(NULL);
	if (inst->rolltype >= MINROLLTYPE) {
		//append the files with epoch. assume wont collide to the sec.
		snprintf(tmp_path, PATH_MAX, "%s.%d", inst->path, (int)appx);
	} else {
		snprintf(tmp_path, PATH_MAX, "%s", inst->path);
	}

	char tp1[PATH_MAX];
	char tp2[PATH_MAX];
	struct roll_common roc = { tp1, tp2 };
	inst->file = fopen_perm(tmp_path, "a+", LDMSD_DEFAULT_FILE_PERM);
	if (!inst->file) {
		INST_LOG(inst, LDMSD_LERROR, "Error %d opening the file %s.\n",
			 errno, inst->path);
		goto out;
	}
	ch_output(inst->file, tmp_path, CSHC(inst), &PG);
	strcpy(roc.filename, tmp_path);
	replace_string(&(inst->filename), roc.filename);

	/*
	 * Always reprint the header because this is a store that may have been
	 * closed and then reopened because a new metric has been added.
	 * New in v3: since it may be a new set of metrics, possibly append to the header.
	 */

	if (!inst->headerfile) {
		/* theoretically, we should never already have this file */
		if (inst->altheader) {
			char tmp_headerpath[PATH_MAX];
			if (inst->rolltype >= MINROLLTYPE) {
				snprintf(tmp_headerpath, PATH_MAX,
					 "%s.HEADER.%d", inst->path, (int)appx);
			} else {
				snprintf(tmp_headerpath, PATH_MAX,
					 "%s.HEADER", inst->path);
			}

			/* truncate a separate headerfile if its the first time */
			if (inst->printheader == FIRST_PRINT_HEADER){
				inst->headerfile = fopen_perm(tmp_headerpath,
						"w", LDMSD_DEFAULT_FILE_PERM);
			} else if (inst->printheader == DO_PRINT_HEADER){
				inst->headerfile = fopen_perm(tmp_headerpath,
						"a+", LDMSD_DEFAULT_FILE_PERM);
			}
			strcpy(roc.headerfilename, tmp_headerpath);
		} else {
			inst->headerfile = fopen_perm(tmp_path, "a+",
						      LDMSD_DEFAULT_FILE_PERM);
			strcpy(roc.headerfilename, tmp_path);
		}

		if (!inst->headerfile) {
			INST_LOG(inst, LDMSD_LERROR,
				 "Error: Cannot open headerfile\n");
			goto err1;
		}
		ch_output(inst->headerfile, tmp_path, CSHC(inst), &PG);
	}
	replace_string(&(inst->headerfilename), roc.headerfilename);

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

	notify_output(NOTE_OPEN, inst->filename, NOTE_DAT,
		CSHC(inst), &PG, inst->container, inst->schema);
	notify_output(NOTE_OPEN, inst->headerfilename, NOTE_HDR,
		CSHC(inst), &PG, inst->container, inst->schema);

	if (inst->rolltype)
		scheduleRollover(inst);
	goto out;

err1:
	fclose(inst->file);
	inst->file = NULL;
out:
	pthread_mutex_unlock(&inst->cfg_lock);
	return rc;
}

int store_csv_close(ldmsd_plugin_inst_t pi)
{
	/* Perform `close` operation */
	store_csv_inst_t inst = (void*)pi;

	pthread_mutex_lock(&inst->lock);
	INST_LOG(inst, LDMSD_LDEBUG, "Closing with path <%s>\n", inst->path);
	if (inst->file) {
		fflush(inst->file);
		fclose(inst->file);
		inst->file = NULL;
	}
	if (inst->headerfile)
		fclose(inst->headerfile);
	inst->headerfile = NULL;
	CLOSE_STORE_COMMON(inst);

	ovis_scheduler_event_del(roll_sched, &inst->roll_ev);

	pthread_mutex_unlock(&inst->lock);

	return 0;
}

int store_csv_flush(ldmsd_plugin_inst_t pi)
{
	/* Perform `flush` operation */
	store_csv_inst_t inst = (void*)pi;
	pthread_mutex_lock(&inst->lock);
	if (inst->file)
		fflush(inst->file);
	pthread_mutex_unlock(&inst->lock);
	return 0;
}

/* return 0 for success */
static int __print_metric(store_csv_inst_t inst, ldms_set_t set, int mid, int i)
{
	uint64_t udata = ldms_metric_user_data_get(set, mid);
	enum ldms_value_type type = ldms_metric_type_get(set, mid);
	int rc, len, j;
	ldms_mval_t mval;
	//use same formats as ldms_ls

	mval = ldms_metric_get(set, mid);

	switch (type) {
	case LDMS_V_CHAR:
	case LDMS_V_CHAR_ARRAY: /* str */
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
		if (inst->udata) {
			rc = fprintf(inst->file, ",%"PRIu64, udata);
			if (rc < 0)
				break;
			inst->byte_count += rc;
		}
		rc = csv_mval_fprint(inst->file, type, mval, -1, inst->ietfcsv);
		if (rc > 0)
			inst->byte_count += rc;
		break;
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
		len = ldms_metric_array_get_len(set, mid);
		for (j = 0; j < len; j++) {
			if (inst->udata) {
				rc = fprintf(inst->file, ",%"PRIu64, udata);
				if (rc < 0)
					break;
				inst->byte_count += rc;
			}
			rc = csv_mval_fprint(inst->file, type, mval, j,
					     inst->ietfcsv);
			if (rc < 0)
				break;
			inst->byte_count += rc;
		}
		break;
	default:
		if (!inst->conflict_warned) {
			INST_LOG(inst, LDMSD_LERROR,
				 "metric id %d: no name at list index %d.\n",
				 mid, i);
			INST_LOG(inst, LDMSD_LERROR,
				 "reconfigure to resolve schema definition "
				 "conflict for schema=%s and instance=%s.\n",
				 ldms_set_schema_name_get(set),
				 ldms_set_instance_name_get(set));
			inst->conflict_warned = true;
		}
		/* print no value */
		if (inst->udata) {
			rc = fprintf(inst->file, ",");
			if (rc < 0)
				break;
			inst->byte_count += rc;
		}
		rc = fprintf(inst->file, ",");
		if (rc > 0)
			inst->byte_count += rc;
		break;
	}

	if (rc < 0) {
		INST_LOG(inst, LDMSD_LERROR, "Error %d writing to '%s'\n",
			 rc, inst->path);
		return EIO;
	}

	return 0;
}

int store_csv_store(ldmsd_plugin_inst_t pi, ldms_set_t set, ldmsd_strgp_t strgp)
{
	/* `store` data from `set` into the store */
	store_csv_inst_t inst = (void*)pi;

	const struct ldms_timestamp _ts = ldms_transaction_timestamp_get(set);
	const struct ldms_timestamp *ts = &_ts;
	const char* pname;
	int i;
	int doflush = 0;
	int rc;

	pthread_mutex_lock(&inst->lock);

	if (!inst->file) {
		INST_LOG(inst, LDMSD_LERROR,
			 "Cannot insert values for <%s>: file is NULL\n",
			 inst->path);
		/* FIXME: will returning an error stop the store? */
		rc = EPERM;
		goto out;
	}

	// Temporarily still have to print header until can invert order of metrics from open
	/* FIXME: New in v3: should not ever have to print the header from here */
	switch (inst->printheader) {
	case DO_PRINT_HEADER:
		/* fall thru */
	case FIRST_PRINT_HEADER:
		rc = print_header_from_store(inst, set, strgp);
		if (rc) {
			INST_LOG(inst, LDMSD_LERROR,
				 "cannot print header: %d. Not storing\n", rc);
			inst->printheader = BAD_HEADER;
			goto out;
			/* FIXME: will returning an error stop the store? */
		}
		break;
	case BAD_HEADER:
		rc = EINVAL;
		goto out;
	default:
		/* ok to continue */
		break;
	}

	fprintf(inst->file, "%"PRIu32".%06"PRIu32 ",%"PRIu32,
		ts->sec, ts->usec, ts->usec);
	pname = ldms_set_producer_name_get(set);
	if (pname != NULL){
		fprintf(inst->file, ",%s", pname);
		inst->byte_count += strlen(pname);
	} else {
		fprintf(inst->file, ",");
	}

	/* FIXME: will we want to throw an error if we cannot write? */
	for (i = 0; i != strgp->metric_count; i++) {
		int mid = strgp->metric_arry[i];
		rc = __print_metric(inst, set, mid, i);
		if (rc)
			goto out;
	}
	fprintf(inst->file,"\n");

	inst->store_count++;


	if ((inst->buffer_type == 3) &&
			((inst->store_count - inst->lastflush) >=
			 inst->buffer_sz)) {
		inst->lastflush = inst->store_count;
		doflush = 1;
	} else if ((inst->buffer_type == 4) &&
			((inst->byte_count - inst->lastflush) >=
			 inst->buffer_sz)) {
		inst->lastflush = inst->byte_count;
		doflush = 1;
	}
	if ((inst->buffer_sz == 0) || doflush){
		fflush(inst->file);
		fsync(fileno(inst->file));
	}

out:
	pthread_mutex_unlock(&inst->lock);
	return rc;
}

/* ============== Common Plugin APIs ================= */

static
const char *store_csv_desc(ldmsd_plugin_inst_t pi)
{
	return "store_csv - store_csv store plugin";
}

static
char *_help = "\
config name=store_csv path=<path> rollover=<num> rolltype=<num>\n\
           [altheader=<0/!0> userdata=<0/!0>]\n\
           [buffer=<0/1/N> buffertype=<3/4>]\n\
           [rename_template=<metapath> [rename_uid=<int-uid> \n\
                                        [rename_gid=<int-gid]\n\
					rename_perm=<octal-mode>]]\n\
           [create_uid=<int-uid> [create_gid=<int-gid] \n\
	                         create_perm=<octal-mode>]\n\
         - Set the root path for the storage of csvs and some default \n\
	   parameters\n\
         - path      The path to the root of the csv directory\n\
         - altheader Header in a separate file (optional, default 0)\n"
NOTIFY_USAGE "\
         - userdata     UserData in printout (optional, default 0)\n\
         - metapath A string template for the file rename, where %[BCDPSTs]\n\
           are replaced per the man page Plugin_store_csv.\n\
         - rollover  Greater than or equal to zero; enables file rollover \n\
	             and sets interval\n\
         - rolltype  [1-n] Defines the policy used to schedule rollover events.\n"
ROLLTYPES "\
         - buffer    0 to disable buffering, 1 to enable it with autosize (default)\n\
                     N > 1 to flush after that many kb (> 4) or that many lines (>=1)\n\
         - buffertype [3,4] Defines the policy used to schedule buffer flush.\n\
                      Only applies for N > 1. Same as rolltypes.\n\
\n";

static
const char *store_csv_help(ldmsd_plugin_inst_t pi)
{
	return _help;
}

static
int store_csv_config(ldmsd_plugin_inst_t pi, json_entity_t json,
				      char *ebuf, int ebufsz)
{
	store_csv_inst_t inst = (void*)pi;
	ldmsd_store_type_t store = (void*)inst->base.base;
	int rc;
	int rollmethod = DEFAULT_ROLLTYPE;
	struct attr_value_list *avl, *kwl = NULL;

	rc = store->base.config(pi, json, ebuf, ebufsz);
	if (rc)
		return rc;

	avl = ldmsd_plugattr_json2attr_value_list(json);
	if (!avl) {
		rc = errno;
		return rc;
	}

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

	rc = ldmsd_plugattr_config_check(attributes, keywords, avl, kwl, dep,
					 PG.pname);
	if (rc != 0) {
		int warnon = (ldmsd_loglevel_get() > LDMSD_LWARNING);
		INST_LOG(inst, LDMSD_LERROR,
			 "config arguments unexpected.%s\n",
			 (warnon ?
			  " Enable log level WARNING for details." : ""));
		return EINVAL;
	}

	/* this section changes if strgp name is store handle instance name */
	if (inst->pa) {
		return update_config(inst, kwl, avl);
	}

	/* end strgp pseudo-instance region. */

	pthread_mutex_lock(&inst->cfg_lock);
	char* conf = av_value(avl, "opt_file");

	inst->pa = ldmsd_plugattr_create(conf, inst->base.inst_name, avl, kwl,
					 init_blacklist, init_blacklist,
					 dep, 0);
	if (!inst->pa) {
		INST_LOG(inst, LDMSD_LERROR, "plugin attr error, errno: %d\n",
			 errno);
		rc = EINVAL;
		goto out;
	}

	const char *s = ldmsd_plugattr_value(inst->pa, "path", NULL);
	rc = 0;
	if (!s) {
		INST_LOG(inst, LDMSD_LERROR,
			"config requires path=value be provided in opt_file or "
			"arguments.\n");
		ldmsd_plugattr_destroy(inst->pa);
		inst->pa = NULL;
		rc = EINVAL;
		goto out;
	}
	inst->path = s;

	if (inst->rolltype != -1) {
		INST_LOG(inst, LDMSD_LWARNING,
			 "repeated rollover config is ignored.\n");
		goto out; /* rollover configured exactly once */
	}
	int ragain = 0;
	int roll = -1;
	int cvt;
	cvt = ldmsd_plugattr_s32(inst->pa, "rollagain", NULL, &ragain);
	if (!cvt) {
		if (ragain < 0) {
			INST_LOG(inst, LDMSD_LERROR,
				"bad rollagain= value %d\n", ragain);
			rc = EINVAL;
			goto out;
		}
	}
	if (cvt == ENOTSUP) {
		INST_LOG(inst, LDMSD_LERROR, "improper rollagain= input.\n");
		rc = EINVAL;
		goto out;
	}

	cvt = ldmsd_plugattr_s32(inst->pa, "rollover", NULL, &roll);
	if (!cvt) {
		if (roll < 0) {
			INST_LOG(inst, LDMSD_LERROR,
				"Error: bad rollover value %d\n", roll);
			rc = EINVAL;
			goto out;
		}
	}
	if (cvt == ENOTSUP) {
		INST_LOG(inst, LDMSD_LERROR, "improper rollover= input.\n");
		rc = EINVAL;
		goto out;
	}

	cvt = ldmsd_plugattr_s32(inst->pa, "rolltype", NULL, &rollmethod);
	if (!cvt) {
		if (roll < 0) {
			/* rolltype not valid without rollover also */
			INST_LOG(inst, LDMSD_LERROR,
				"rolltype given without rollover.\n");
			rc = EINVAL;
			goto out;
		}
		if (rollmethod < MINROLLTYPE || rollmethod > MAXROLLTYPE) {
			INST_LOG(inst, LDMSD_LERROR,
				 "rolltype out of range.\n");
			rc = EINVAL;
			goto out;
		}
		if (rollmethod == 5 && (roll < 0 || ragain < roll ||
					ragain < MIN_ROLL_1)) {
			INST_LOG(inst, LDMSD_LERROR,
				 "rolltype=5 needs rollagain > "
				 "max(rollover,10)\n");
			INST_LOG(inst, LDMSD_LERROR,
				 "rollagain=%d rollover=%d\n", roll, ragain);
			rc = EINVAL;
			goto out;
		}
	}
	if (cvt == ENOTSUP) {
		INST_LOG(inst, LDMSD_LERROR, "improper rolltype= input.\n");
		rc = EINVAL;
		goto out;
	}

	inst->rollover = roll;
	inst->rollagain = ragain;
	if (rollmethod >= MINROLLTYPE) {
		inst->rolltype = rollmethod;
	}

out:
	if (!rc)
		inst->cfgstate = CSV_CFGINIT_DONE;

	pthread_mutex_unlock(&inst->cfg_lock);

	return rc;
}

static
void store_csv_del(ldmsd_plugin_inst_t pi)
{
	store_csv_inst_t inst = (void*)pi;

	ldmsd_plugattr_destroy(inst->pa);
}

static
int store_csv_init(ldmsd_plugin_inst_t pi)
{
	store_csv_inst_t inst = (void*)pi;
	ldmsd_store_type_t store = (void*)inst->base.base;

	/* override store operations */
	store->open = store_csv_open;
	store->close = store_csv_close;
	store->flush = store_csv_flush;
	store->store = store_csv_store;

	/* Initialize uid/gid/perm */
	inst->create_uid = inst->rename_uid = geteuid();
	inst->create_gid = inst->rename_gid = getegid();
	inst->create_perm = inst->rename_perm = 0600;

	return 0;
}

static
struct store_csv_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "store",
		.plugin_name = "store_csv",

                /* Common Plugin APIs */
		.desc   = store_csv_desc,
		.help   = store_csv_help,
		.init   = store_csv_init,
		.del    = store_csv_del,
		.config = store_csv_config,

	},
	.buffer_sz   = 1, /* default to system-driven flush */
	.buffer_type = 3,
	.roll_ev = OVIS_EVENT_INITIALIZER,
	.rolltype = -1,
};

ldmsd_plugin_inst_t new()
{
	store_csv_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}

__attribute__((constructor))
static void __init_once()
{
	int rc;
	roll_sched = ovis_scheduler_new();
	if (!roll_sched) {
		ldmsd_log(LDMSD_LERROR,
			  "store_csv: rollover scheduler creation failed, "
			  "errno: %d\n", errno);
		return;
	}
	rc = pthread_create(&roll_thread, NULL, rolloverThreadInit, NULL);
	if (rc) {
		ldmsd_log(LDMSD_LERROR, "store_csv: rollover thread creation "
			  "failed, rc: %d\n", rc);
		return;
	}
	PG.msglog = ldmsd_log;
}
