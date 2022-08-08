/**
 * Copyright (c) 2019-2021 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2019-2021 Open Grid Computing, Inc. All rights reserved.
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
#include <ovis_json/ovis_json.h>
#include <coll/idx.h>
#include <coll/rbt.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_stream.h"

/*
 * CURRENT GROUND RULES:
 * 1) the json will be something like:
 *            {foo:1, bar:2, zed-data:[{count:1, name:xyz},{count:2, name:abc}]}
 * 2) can get this at the beginning and assume it will be true for all time
 * 3) there will be at most 1 list
 * 4) can have a list with no entries
 * 5) In the writeout, each dict will be in its own csv line with the singletons
 */

/*
 * user changes these in the code before build, for now
 * timestamp store only for json lines, not text
 */
#undef TIMESTAMP_STORE
#undef STREAM_CSV_DIAGNOSTICS
#define CB_MSG_LOG 50000

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))
#endif

#define _stringify(_x) #_x
#define stringify(_x) _stringify(_x)

#define PNAME "stream_csv_store"

static ldmsd_msg_log_f msglog;
static int flushtime = 0;
static int rollover = 0;
static int rollagain = 0;
/* rolltype determines how to interpret rollover values > 0. */
#define DEFAULT_ROLLTYPE -1
static int rolltype = DEFAULT_ROLLTYPE;
/* default -- do not roll */
#define MIN_FLUSH_TIME 120
/* Interval to invoke orthogonal flush */

/* ROLLTYPES documents rolltype and is used in help output. Also used for buffering */
#define ROLLTYPES \
"                     1: wake approximately every rollover seconds and roll.\n" \
"                     2: wake daily at rollover seconds after midnight (>=0) and roll.\n" \
"                     3: roll after approximately rollover records are written.\n" \
"                     4: roll after approximately rollover bytes are written.\n" \
"                     5: wake daily at rollover seconds after midnight and every rollagain seconds thereafter.\n"

#define MAXROLLTYPE 5
#define MINROLLTYPE 1
#define ROLL_BY_INTERVAL 1
#define ROLL_BY_MIDNIGHT 2
#define ROLL_BY_RECORDS 3
#define ROLL_BY_BYTES 4
#define ROLL_BY_MIDNIGHT_AGAIN 5
/* default -- do not roll */
#define DEFAULT_ROLLTYPE -1
/*
 * minimum rollover for type 1;
 * rolltype==1 and rollover < MIN_ROLL_1 -> rollover = MIN_ROLL_1
 * also used for minimum sleep time for type 2;
 * rolltype==2 and rollover results in sleep < MIN_ROLL_SLEEPTIME -> skip this roll and do it the next day
 */
#define MIN_ROLL_1 10
/*
 * minimum rollover for type 3;
 * rolltype==3 and rollover < MIN_ROLL_RECORDS > rollover = MIN_ROLL_RECORDS
 */
#define MIN_ROLL_RECORDS 3
/*
 * minimum rollover for type 4;
 * rolltype==4 and rollover < MIN_ROLL_BYTES -> rollover = MIN_ROLL_BYTES
 */
#define MIN_ROLL_BYTES 1024
/* Interval to check for passing the record or byte count limits. */
#define ROLL_LIMIT_INTERVAL 60
#define ROLL_DEFAULT_SLEEP 60

static pthread_t rothread;
static int rothread_used = 0;
static pthread_t flthread;
static int flthread_used = 0;

static char *root_path = NULL;
static char *container = NULL;
static int buffer;
static pthread_mutex_t cfg_lock = PTHREAD_MUTEX_INITIALIZER;

; /* seralizes config args and stream_idx */

static idx_t stream_idx;

typedef enum {
	CFG_PRE, CFG_BASIC, CFG_DONE, CFG_FAILED
} cfg_state;
static cfg_state cfgstate = CFG_PRE;

/* Well known: written order will be singletonkeys followed by listentry keys */
struct linedata {
	int nsingleton;
	char **singletonkey;
	int nlist;
	char *listkey; /* will have at most one list; */
	/*
	 * variable number of dicts in list. all
	 * dicts in list must have same keys
	 */
	int ndict;
	char **dictkey;
	int nkey;
	int nheaderkey; /* number of keys in the header */
	char *header;
};

struct csv_stream_handle {
	/*
	 * key will be stream. NOT container/schema like store/csv since you
	 * can only subscribe once to a stream. it is like a schema in a
	 * canonical store that it will have to be unique and immutable.
	 */
	char *stream; /* this is the key for this handle */
	/*
	 * file base name. add the timestamp on:
	 * (root_path + container + stream)
	 */
	char *basename;
	FILE *file;

	int64_t store_count; /* for the roll. Cumulative since last roll */
	int64_t byte_count; /* for the roll. Cumulative since last roll */
	struct timeval tlastrcv; /* for the flush. */
	struct linedata dataline; /* used to keep track of keys for the header */
	ldmsd_stream_client_t client; /* subscribe/unsubscribe */
	pthread_mutex_t lock;
};

static void _clear_key_info(struct linedata *dataline)
{
	int i;

	/* have stream lock */
	if (!dataline)
		return;

	free(dataline->header);
	dataline->header = NULL;
	for (i = 0; i < dataline->nsingleton; i++) {
		free(dataline->singletonkey[i]);
	}
	free(dataline->singletonkey);
	dataline->singletonkey = NULL;
	dataline->nsingleton = 0;

	free(dataline->listkey);
	dataline->listkey = NULL;
	dataline->nlist = 0;

	for (i = 0; i < dataline->ndict; i++) {
		free(dataline->dictkey[i]);
		dataline->dictkey[i] = NULL;
	}
	free(dataline->dictkey);
	dataline->dictkey = NULL;

	dataline->ndict = 0;
	dataline->nheaderkey = 0;
	free(dataline->header);
	dataline->header = NULL;
}

static void close_streamstore(void *obj, void *cb_arg)
{
	/* cfg_lock is held outside of this */

	if (!obj)
		return;

	struct csv_stream_handle *stream_handle =
			(struct csv_stream_handle*) obj;
	if (!stream_handle) {
		return;
	}

	pthread_mutex_lock(&stream_handle->lock);
	msglog(LDMSD_LDEBUG, PNAME ": Closing stream store <%s>\n",
			stream_handle->stream);

	/* unsubscribe */
	if (stream_handle->client)
		ldmsd_stream_close(stream_handle->client);
	stream_handle->client = NULL;

	if (stream_handle->file) {
		fflush(stream_handle->file);
		fsync(fileno(stream_handle->file));
		fclose(stream_handle->file);
	}
	stream_handle->file = NULL;

	_clear_key_info(&stream_handle->dataline);
	stream_handle->store_count = 0;
	stream_handle->byte_count = 0;
	stream_handle->tlastrcv.tv_sec = 0;
	stream_handle->tlastrcv.tv_usec = 0;
	free(stream_handle->basename);
	stream_handle->basename = NULL;

	idx_delete(stream_idx, stream_handle->stream,
			strlen(stream_handle->stream));
	free(stream_handle->stream);
	stream_handle->stream = NULL;

	pthread_mutex_unlock(&stream_handle->lock);
	pthread_mutex_destroy(&stream_handle->lock);
	free(stream_handle);

	return;
}

static int _parse_list_for_header(struct linedata *dataline, json_entity_t e)
{
	/*
	 * get specialized data for building header.
	 * this should only be list of dicts
	 */
	json_entity_t li, di;
	int idict;
	int i;

	/*
	 * NOTE: if needed for performance reasons, can we eliminate some of
	 * the checking and jsut rely on getting NULL returns when we ask
	 * for the actual item that we would want?
	 */
	if (!dataline) {
		return -1;
	}

	/* for getting the header, we only care about the first li */
	li = json_item_first(e);

	if (li == NULL) {
		msglog(LDMSD_LERROR, PNAME ": list cannot be empty for header.\n");
		return -1;
	}
	if (li->type != JSON_DICT_VALUE) {
		msglog(LDMSD_LERROR, PNAME ": list can only have dict entries\n");
		return -1;
	}
	/* parse the dict. process once to fill and 2nd time to fill */
	for (i = 0; i < 2; i++) {
		idict = 0;
		for (di = json_attr_first(li); di; di = json_attr_next(di)) {
			/* only allowed singleton entries */
			if (i == 1) {
				dataline->dictkey[idict] =
					strdup(di->value.attr_->name->value.str_->str);
				if (!dataline->dictkey[idict])
					return ENOMEM;
			}
			idict++;
		}
		if (idict == 0) {
			msglog(LDMSD_LDEBUG, PNAME ": empty dict for header parse\n");
			return -1;
		}
		if (i == 0) {
			dataline->dictkey = (char**) calloc(idict, sizeof(char*));
			if (!(dataline->dictkey))
				return ENOMEM;
			dataline->ndict = idict;
		}
	}
	return 0;

}

static int _get_header_from_data(struct linedata *dataline, json_entity_t e)
{
	/* from the data, builds the header and supporting structs. */
	/* all lists and dict options must be in this line */

	json_entity_t a;
	int isingleton, ilist;
	int i, rc;

	if (!dataline) {
		return EINVAL;
	}
	_clear_key_info(dataline);

	/*
	 * use jbuf as a variable length string to add things to.
	 * Only need to do once.
	 */
	jbuf_t jb = jbuf_new();

	/* process to build the header. */
	i = 0;
	isingleton = 0;
	ilist = 0;
	for (a = json_attr_first(e); a; a = json_attr_next(a)) {
		json_attr_t attr = a->value.attr_;
		switch (attr->value->type) {
		case JSON_LIST_VALUE:
			/*
			 * assumes that all the dicts in a list will have
			 * the same keys for all time
			 */
			if (ilist != 0) {
				msglog(LDMSD_LDEBUG,
					PNAME ": can only support 1 list in the header\n");
				rc = EINVAL;
				goto err;
			}
			ilist++;
			break;
		case JSON_DICT_VALUE:
			msglog(LDMSD_LERROR,
				PNAME ": not handling type JSON_DICT_VALUE in header\n");
			rc = EINVAL;
			goto err;
			break;
		case JSON_NULL_VALUE:
			/* treat like a singleton */
			isingleton++;
			break;
		case JSON_ATTR_VALUE:
			msglog(LDMSD_LERROR,
				PNAME ": should not have ATTR type now\n");
			rc = EINVAL;
			goto err;
			break;
		default:
			/* it's a singleton */
			isingleton++;
			break;
		}
		i++;
	}

	if ((isingleton == 0) && (ilist == 0)) {
		msglog(LDMSD_LERROR,
			PNAME ": no keys for header. Waiting for next one\n");
		rc = EINVAL;
		goto err;
	}

	dataline->nsingleton = isingleton;
	dataline->nlist = ilist;
	dataline->singletonkey = (char**) calloc(dataline->nsingleton, sizeof(char*));
	if (dataline->nsingleton && !dataline->singletonkey) {
		rc = ENOMEM;
		goto err;
	}

	/* process again to fill. will parse dicts here */
	i = 0;
	isingleton = 0;
	for (a = json_attr_first(e); a; a = json_attr_next(a)) {
		json_attr_t attr = a->value.attr_;
		switch (attr->value->type) {
		case JSON_LIST_VALUE:
			dataline->listkey = strdup(attr->name->value.str_->str);
			if (!dataline->listkey)
				return ENOMEM;
			rc = _parse_list_for_header(dataline, attr->value);
			if (rc)
				goto err;
			break;
		case JSON_DICT_VALUE:
			msglog(LDMSD_LERROR,
				PNAME ": not handling type JSON_DICT_VALUE in header\n");
			rc = EINVAL;
			goto err;
			break;
		case JSON_ATTR_VALUE:
			msglog(LDMSD_LERROR,
					PNAME ": should not have ATTR type now\n");
			rc = EINVAL;
			goto err;
			break;
		case JSON_NULL_VALUE:
		default:
			/* it's a singleton */
			dataline->singletonkey[isingleton] =
					strdup(attr->name->value.str_->str);
			if (!dataline->singletonkey[isingleton])
				return ENOMEM;
			isingleton++;
			break;
		}
		i++;
	}

	dataline->nheaderkey = dataline->nsingleton + dataline->ndict;

	/*
	 * order will be order of singletons and order of dict.
	 * repeat dicts will be separate entries in the csv
	 */
	for (i = 0; i < dataline->nsingleton; i++) {
		if (i < dataline->nheaderkey - 1) {
			jb = jbuf_append_str(jb, "%s,",
					dataline->singletonkey[i]);
		} else {
			jb = jbuf_append_str(jb, "%s",
					dataline->singletonkey[i]);
		}
	}

	for (i = 0; i < dataline->ndict; i++) {
		if (i < dataline->ndict - 1) {
			jb = jbuf_append_str(jb, "%s:%s,", dataline->listkey,
					dataline->dictkey[i]);
		} else {
			jb = jbuf_append_str(jb, "%s:%s", dataline->listkey,
					dataline->dictkey[i]);
		}
	}
#ifdef TIMESTAMP_STORE
	jb = jbuf_append_str(jb, ",store_recv_time");
#endif
	dataline->header = strdup(jb->buf);
	if (!dataline->header)
		return ENOMEM;
	jbuf_free(jb);

	return 0;

err:
	jbuf_free(jb);
	msglog(LDMSD_LDEBUG, PNAME ": header build failed\n");
	_clear_key_info(dataline);

	return rc;
}

static int _append_singleton(json_entity_t en, jbuf_t jb)
{

	switch (en->type) {
	case JSON_INT_VALUE:
		jb = jbuf_append_str(jb, "%ld", en->value.int_);
		break;
	case JSON_BOOL_VALUE:
		if (en->value.bool_)
			jb = jbuf_append_str(jb, "true");
		else
			jb = jbuf_append_str(jb, "false");
		break;
	case JSON_FLOAT_VALUE:
		jb = jbuf_append_str(jb, "%f", en->value.double_);
		break;
	case JSON_STRING_VALUE:
		jb = jbuf_append_str(jb, "\"%s\"", en->value.str_->str);
		break;
	case JSON_NULL_VALUE:
		jb = jbuf_append_str(jb, "null");
		break;
	default:
		/* this should not happen */
		msglog(LDMSD_LDEBUG,
				PNAME, ": cannot process JSON type '%s' "
				"as singleton\n", json_type_name(en->type));
		return -1;
		break;
	}

	return 0;
}

static int _print_header(struct csv_stream_handle *stream_handle)
{

	/* don't count the header in the store_count or byte_count */
	if (stream_handle && stream_handle->file
			&& stream_handle->dataline.header) {
		fprintf(stream_handle->file, "#%s\n",
				stream_handle->dataline.header);
		fflush(stream_handle->file);
		fsync(fileno(stream_handle->file));
		return 0;
	}

	return -1;
}

static int _print_data_lines(struct csv_stream_handle *stream_handle,
				struct timeval *tv_prev, json_entity_t e)
{
	/* well known order */

	json_entity_t en, li;
	jbuf_t jbs, jb;
	int iheaderkey;
	int i;
	int rc;

	/*
	 * NOTE: if needed for performance reasons, can we eliminate some
	 * of the checking and jsut rely on getting NULL returns when we
	 * ask for the actual item that we would want?
	 */
	if (!stream_handle || !stream_handle->file) {
		return -1;
	}

	struct linedata *dataline = &stream_handle->dataline;

	iheaderkey = 0;

	jbs = NULL;
	jbs = jbuf_new();
	if (dataline->nsingleton > 0) {
		for (i = 0; i < dataline->nsingleton; i++) {
			en = json_value_find(e, dataline->singletonkey[i]);
			if (en == NULL) {
				/* this may or may not be ok.... */
			} else {
				rc = _append_singleton(en, jbs);
				if (rc) {
					msglog(LDMSD_LDEBUG,
						PNAME ": Cannot print data because "
						"of a variable print problem\n");
					jbuf_free(jbs);
					return rc;
				}
			}
			if (iheaderkey < (dataline->nheaderkey - 1)) {
				jbs = jbuf_append_str(jbs, ",");
			}
			iheaderkey++;
		}
	} else {
		jbs = jbuf_append_str(jbs, ""); /* Need this for adding list items */
	}

	/*
	 * for each dict, write a separate line in the file
	 * if header has no list or an empty list, just write the singletons
	 */
	if (dataline->nlist == 0) {
#ifdef TIMESTAMP_STORE
		rc = fprintf(stream_handle->file, "%s,%f\n", jbs->buf,
			(tv_prev->tv_sec + tv_prev->tv_usec/1000000.0));
#else
		rc = fprintf(stream_handle->file, "%s\n", jbs->buf);

#endif
		jbuf_free(jbs);
		stream_handle->byte_count += rc;
		/*
		 * stream_cb has the lock, so roll cannot be called while
		 * this is going on.
		 */
		stream_handle->store_count++;
		goto out;
	}

	/* if header has a list..... */
	en = json_value_find(e, dataline->listkey);
	/* if data has no list */
	if (en == NULL) {
		msglog(LDMSD_LDEBUG, PNAME ": no match for %s\n", dataline->listkey);
		/* write out just the singleton line and empty vals for the dict items */
		for (i = 0; i < dataline->ndict - 1; i++) {
			jbs = jbuf_append_str(jbs, ",");
		}
#ifdef TIMESTAMP_STORE
		rc = fprintf(stream_handle->file, "%s,%f\n", jbs->buf,
			(tv_prev->tv_sec + tv_prev->tv_usec/1000000.0));
#else
		rc = fprintf(stream_handle->file, "%s\n", jbs->buf);
#endif
		jbuf_free(jbs);
		stream_handle->byte_count += rc;
		/* stream_cb has the lock, so roll cannot be called while this is going on. */
		stream_handle->store_count++;
		goto out;
	}

	/* if we got the val, but its not a list */
	if (en->type != JSON_LIST_VALUE) {
		msglog(LDMSD_LERROR, PNAME ": %s is not a LIST type %s. "
						"skipping this data.\n",
						dataline->listkey,
						json_type_name(en->type));
		/*
		 * NOTE: this is bad. currently writing out nothing,
		 * but could change this later.
		 */
		jbuf_free(jbs);
		return -1;
	}

	/* if there are no dicts */
	if (json_item_first(en) == NULL) {
		for (i = 0; i < dataline->ndict - 1; i++) {
			jbs = jbuf_append_str(jbs, ",");
		}
#ifdef TIMESTAMP_STORE
		rc = fprintf(stream_handle->file, "%s,%f\n", jbs->buf,
			(tv_prev->tv_sec + tv_prev->tv_usec/1000000.0));
#else
		rc = fprintf(stream_handle->file, "%s\n", jbs->buf);
#endif
		jbuf_free(jbs);
		stream_handle->byte_count += rc;
		/* stream_cb has the lock, so roll cannot be called while this is going on. */
		stream_handle->store_count++;
		goto out;
	}

	/* if there are dicts */
	for (li = json_item_first(en); li; li = json_item_next(li)) {
		if (li->type != JSON_DICT_VALUE) {
			msglog(LDMSD_LERROR,
					PNAME ": LIST %s has innards that are not "
					"a DICT type %s. skipping this data.\n",
					dataline->listkey,
					json_type_name(li->type));
			/* no output */
			jbuf_free(jbs);
			return -1;
		}

		/* each dict will be its own line */
		jb = jbuf_new();
		jb = jbuf_append_str(jb, "%s", jbs->buf);
		for (i = 0; i < dataline->ndict; i++) {
			json_entity_t edict = json_value_find(li, dataline->dictkey[i]);
			if (edict == NULL) {
				msglog(LDMSD_LDEBUG,
						PNAME ": NULL return from find for "
						"key <%s>\n", dataline->dictkey[i]);
				/* print nothing */
			} else {
				rc = _append_singleton(edict, jb);
				if (rc) {
					msglog(LDMSD_LDEBUG,
						PNAME ": Cannot print data because "
						"of a variable print problem\n");
					jbuf_free(jbs);
					jbuf_free(jb);
					return rc;
				}
			}
			if (i < (dataline->ndict - 1)) {
				jb = jbuf_append_str(jb, ",");
			}
		}
#ifdef TIMESTAMP_STORE
		rc = fprintf(stream_handle->file, "%s,%f\n", jb->buf,
			(tv_prev->tv_sec + tv_prev->tv_usec/1000000.0));
#else
		rc = fprintf(stream_handle->file, "%s\n", jb->buf);
#endif
		jbuf_free(jb);
		stream_handle->byte_count += rc;
		 /* stream_cb has the lock, so roll cannot be called while this is going on. */
		stream_handle->store_count++;
	}
	jbuf_free(jbs);

out:
#ifdef STREAM_CSV_DIAGNOSTICS
	msglog(LDMSD_LDEBUG, PNAME ": message processed. store_count = %d\n",
						stream_handle->store_count);
#endif
	return 0;
}

static void _roll_innards(struct csv_stream_handle *stream_handle);
static int stream_cb(ldmsd_stream_client_t c, void *ctxt,
			ldmsd_stream_type_t stream_type, const char *msg,
			size_t msg_len, json_entity_t e)
{

	struct csv_stream_handle *stream_handle;
	struct timeval tv_prev;
	int gottime = 0;
	int rc = 0;

#ifdef STREAM_CSV_DIAGNOSTICS
	static uint64_t msg_count = 0;


	/* diagnostics logging */
	msglog(LDMSD_LDEBUG,
		PNAME ": Calling stream_cb. msg_count %d on stream '%s'\n",
		msg_count, ldmsd_stream_client_name(c));

	msg_count += 1;
	if (0 == (msg_count % CB_MSG_LOG)) {
		extern struct rbt *msg_tree;
		time_t t = time(NULL);
		msglog(LDMSD_LERROR, "timestamp %ld msg_tree %ld msg_count %lu\n",
						t, msg_tree->card, msg_count);
	}
#endif

	/* don't need to check the cfgstate. if you've subscribed, it's ok */
	const char *skey = ldmsd_stream_client_name(c);
	if (!skey) {
		msglog(LDMSD_LERROR,
				PNAME ": Cannot get stream_name from client\n");
		return -1;
	}

        pthread_mutex_lock(&cfg_lock);
        /* really don't need lock since streams cannot be dynamically added */
	stream_handle = idx_find(stream_idx, (void*) skey, strlen(skey));
	if (!stream_handle) {
		msglog(LDMSD_LERROR, PNAME ": No stream_store for '%s'\n", skey);
                pthread_mutex_unlock(&cfg_lock);
		return -1;
	}

	pthread_mutex_lock(&stream_handle->lock);
        pthread_mutex_unlock(&cfg_lock);
	/*
	 * currently releasing this, since the only way to destroy is in the
	 * overall shutdown, plus want to be able to do the callback on
	 * independent streams at the same time
	 */

#ifdef TIMESTAMP_STORE
	gettimeofday(&tv_prev, 0);
	gottime = 1;
#endif
	/* also get the time if flushtime is set */
	if (flushtime > 0) {
		if (!gottime)
			gettimeofday(&tv_prev, 0);
		/*
		 * update the last recv time for this stream, whether
		 * or not we end up processing data for it or not.
		 */
		stream_handle->tlastrcv = tv_prev;
	}

	if (!stream_handle->file) {
		msglog(LDMSD_LERROR, PNAME ": Cannot insert values for '%s': "
					"file is NULL\n", stream_handle->stream);
		rc = EPERM;
		goto out;
	}

	/*
	 * msg will be populated. if the type was json,
	 * entity will also be populated.
	 */

	if (stream_type == LDMSD_STREAM_STRING) {
		/* note that the string might have a newline as part of it */
#ifdef TIMESTAMP_STORE
		rc = fprintf(stream_handle->file, "%s,%f\n",
				msg,
				tv_prev.tv_sec + tv_prev.tv_usec/1000000.0);
#else
		rc = fprintf(stream_handle->file, "%s\n", msg);
#endif
		stream_handle->byte_count += rc;
		stream_handle->store_count++;

		if (!buffer) {
			/*
			 * stream_cb has the lock, so roll cannot be called while
			 * this is going on.
			 */
			fflush(stream_handle->file);
			fsync(fileno(stream_handle->file));
		}
	} else if (stream_type == LDMSD_STREAM_JSON) {
		if (!e) {
			msglog(LDMSD_LERROR, PNAME ": why is entity NULL?\n");
			rc = EINVAL;
			goto out;
		}

		if (e->type != JSON_DICT_VALUE) {
			msglog(LDMSD_LERROR, PNAME ": Expected a dict object, "
					"not a %s.\n", json_type_name(e->type));
			rc = EINVAL;
			goto out;
		}

		if (!stream_handle->dataline.header) {
			rc = _get_header_from_data(&stream_handle->dataline, e);
			if (rc != 0) {
				msglog(LDMSD_LDEBUG, PNAME ": error getting header "
							"from data <%d>\n", rc);
				goto out;
			}
			/*
			 * header does not count toward bytes and lines
			 * to simplify diagnostics
			 */
			_print_header(stream_handle);
		}

		_print_data_lines(stream_handle, &tv_prev, e);

		if (!buffer) {
			fflush(stream_handle->file);
			fsync(fileno(stream_handle->file));
		}
	} else {
		msglog(LDMSD_LERROR, PNAME ": unknown stream type\n");
		rc = EINVAL;
		goto out;
	}

out:
	pthread_mutex_unlock(&stream_handle->lock);
	return rc;
}

static int open_streamstore(char *stream)
{
	struct csv_stream_handle *stream_handle;
	char *tmp_filename;
	char *tmp_basename;
	char *dpath;
	FILE *tmp_file;
	unsigned long tspath;
	int rc = 0;

	/* already have the cfg_lock */
	if ((cfgstate != CFG_BASIC) && (cfgstate != CFG_DONE)) {
		msglog(LDMSD_LERROR, PNAME ": config not called. cannot open.\n");
		return ENOENT;
	}

	/* NOTE: unlike store, this doesn't have the possibility of closing yet. */
	stream_handle = idx_find(stream_idx, (void*) stream, strlen(stream));
	if (stream_handle) {
		msglog(LDMSD_LERROR, PNAME ": cannot open stream item. already have it\n");
		return EINVAL;
	}

	/* add additional 12 for the timestamp */
	size_t pathlen = strlen(root_path) + strlen(stream) +
					strlen(container) + 8 + 12;
	dpath = malloc(pathlen);
	tmp_basename = malloc(pathlen);
	tmp_filename = malloc(pathlen);
	if (!dpath || !tmp_filename || !tmp_basename) {
		rc = ENOMEM;
		goto out;
	}

	tspath = (unsigned long) (time(NULL));
	sprintf(dpath, "%s/%s", root_path, container);
	sprintf(tmp_basename, "%s/%s/%s", root_path, container, stream);
	sprintf(tmp_filename, "%s/%s/%s.%lu", root_path, container, stream,
			tspath);

	msglog(LDMSD_LDEBUG, PNAME ": stream '%s' will have file '%s'\n",
						stream, tmp_filename);

	/* create path if not already there. */
	rc = f_mkdir_p(dpath, 0777);
	if ((rc != 0) && (errno != EEXIST)) {
		msglog(LDMSD_LERROR, PNAME ": Failure '%s' creating directory '%s'\n",
							STRERROR(errno), dpath);
		rc = errno;
		goto err1;
	}
	rc = 0;

	tmp_file = fopen_perm(tmp_filename, "a+", LDMSD_DEFAULT_FILE_PERM);
	if (!tmp_file) {
		msglog(LDMSD_LERROR, PNAME ": Error %d opening the file %s.\n",
							errno, tmp_filename);
		rc = ENOENT;
		goto err1;
	}

	stream_handle = calloc(1, sizeof *stream_handle);
	if (!stream_handle) {
		rc = ENOMEM;
		goto err1;
	}

	pthread_mutex_init(&stream_handle->lock, NULL);
	pthread_mutex_lock(&stream_handle->lock);

	/* swap */
	stream_handle->file = tmp_file;
	stream_handle->basename = tmp_basename;
	stream_handle->stream = strdup(stream);
	if (!stream_handle->stream) {
		rc = ENOMEM;
		goto err1;
	}

	/* NOTE: subscribing but rollover not set yet */
	msglog(LDMSD_LDEBUG, PNAME ": subscribing to stream '%s'\n", stream);
	stream_handle->client = ldmsd_stream_subscribe(stream, stream_cb,
								stream_handle);
	idx_add(stream_idx, (void*) stream, strlen(stream), stream_handle);
	pthread_mutex_unlock(&stream_handle->lock);

	goto out;

err1:
	free(tmp_basename);
	if (stream_handle) {
		free(stream_handle->file);
		free(stream_handle->basename);
		free(stream_handle->stream);
		pthread_mutex_unlock(&stream_handle->lock);
		pthread_mutex_destroy(&stream_handle->lock);
	}
	free(stream_handle);

out:
	free(tmp_filename);
	free(dpath);
	return rc;
}

static void _roll_innards(struct csv_stream_handle *stream_handle)
{
	FILE *nfp = NULL;
	char *tmp_filename = NULL;
	size_t pathlen;
	unsigned long tmp_tspath;

	/* this fct enables roll on demand (e.g., reset the headers)*/

	/* stream_handle->lock should be held coming into this */

	if (!stream_handle)
		return;

	if (stream_handle->file) { /* this should always be true */
		fflush(stream_handle->file);
		fsync(fileno(stream_handle->file));
	}

	pathlen = strlen(stream_handle->basename) + 12;
	tmp_filename = malloc(pathlen);
	if (!tmp_filename) {
		goto out;
	}
	tmp_tspath = (unsigned long) (time(NULL));
	sprintf(tmp_filename, "%s.%lu", stream_handle->basename, tmp_tspath);

	msglog(LDMSD_LDEBUG, PNAME ": stream '%s' will have file '%s'\n",
					stream_handle->stream, tmp_filename);

	nfp = fopen_perm(tmp_filename, "a+", LDMSD_DEFAULT_FILE_PERM);
	if (!nfp) {
		msglog(LDMSD_LERROR, PNAME ": Error %d opening the file %s.\n",
							errno, tmp_filename);
		goto out;
	}

	/* close and swap */
	if (stream_handle->file) { /* this should always be true */
		fclose(stream_handle->file);
	}
	stream_handle->file = nfp;

out:
	free(tmp_filename);

}

static void roll_cb(void *obj, void *cb_arg)
{

	/* if we've got here then we've called a stream_store */
	if (!obj)
		return;

	struct csv_stream_handle *stream_handle = (struct csv_stream_handle*) obj;

	pthread_mutex_lock(&stream_handle->lock);

	switch (rolltype) {
	case ROLL_BY_INTERVAL:
	case ROLL_BY_MIDNIGHT:
	case ROLL_BY_MIDNIGHT_AGAIN:
		if (stream_handle->store_count == 0)
			goto out;
		break;
	case ROLL_BY_RECORDS:
		if (stream_handle->store_count < rollover) {
			goto out;
		}
		break;
	case ROLL_BY_BYTES:
		if (stream_handle->byte_count < rollover) {
			goto out;
		}
		break;
	default:
		msglog(LDMSD_LDEBUG, PNAME ": Error: unexpected rolltype "
					"in store(%d)\n", rolltype);
		break;
	}

	/* always 0 these,so they don't overflow */
	stream_handle->store_count = 0;
	stream_handle->byte_count = 0;

	_roll_innards(stream_handle);
	/* only print the header if its on a roll */
	_print_header(stream_handle);

out:
	pthread_mutex_unlock(&stream_handle->lock);
}

struct flush_cb_arg {
	time_t appx;
};

static void flush_cb(void *obj, void *cb_arg)
{
	/* flushtime must be > 0 to even get here */

	if ((!obj) || (!cb_arg))
		return;

	struct csv_stream_handle *stream_handle = (struct csv_stream_handle*) obj;
	if (!stream_handle)
		return;

	struct flush_cb_arg *fca = (struct flush_cb_arg*) cb_arg;
	time_t timex = fca->appx;

	pthread_mutex_lock(&stream_handle->lock);

	if (stream_handle->file) { /* this should always be true */
		if (timex - stream_handle->tlastrcv.tv_sec > flushtime) {
			msglog(LDMSD_LDEBUG, PNAME ": Async Flush: %s "
					"(curr %ld last %ld)\n",
					stream_handle->stream, timex,
					stream_handle->tlastrcv.tv_sec);
			fflush(stream_handle->file);
			fsync(fileno(stream_handle->file));
		}
	}

	pthread_mutex_unlock(&stream_handle->lock);
}

static int handleRollover()
{
	/* don't need to check cfgstate */

	/*
	 * don't add any stores during this,
	 * which currently cannot do anyway.
	 */
	pthread_mutex_lock(&cfg_lock);
	idx_traverse(stream_idx, roll_cb, NULL);
	pthread_mutex_unlock(&cfg_lock);

	return 0;
}

static int handleFlush()
{

	struct flush_cb_arg fca;

	/*
	 * don't add any stores during this,
	 * which currently cannot do anyway.
	 */
	pthread_mutex_lock(&cfg_lock);
	fca.appx = time(NULL);
	idx_traverse(stream_idx, flush_cb, (void*) &fca);
	pthread_mutex_unlock(&cfg_lock);

	return 0;
}

static void* rolloverThreadInit(void *m)
{
	/* if got here, then rollover requested */

	while (1) {
		int tsleep;
		switch (rolltype) {
		case ROLL_BY_INTERVAL:
			tsleep = ((rollover < MIN_ROLL_1)?MIN_ROLL_1:rollover);
			break;
		case ROLL_BY_MIDNIGHT: {
			time_t rawtime;
			struct tm info;

			time(&rawtime);
			localtime_r(&rawtime, &info);
			int secSinceMidnight = info.tm_hour * 3600 +
						info.tm_min * 60 + info.tm_sec;
			tsleep = 86400 - secSinceMidnight + rollover;
			if (tsleep < MIN_ROLL_1) {
				/* if we just did a roll then skip this one */
				tsleep += 86400;
			}
		}
			break;
		case ROLL_BY_RECORDS:
			if (rollover < MIN_ROLL_RECORDS)
				rollover = MIN_ROLL_RECORDS;
			tsleep = ROLL_LIMIT_INTERVAL;
			break;
		case ROLL_BY_BYTES:
			if (rollover < MIN_ROLL_BYTES)
				rollover = MIN_ROLL_BYTES;
			tsleep = ROLL_LIMIT_INTERVAL;
			break;
		case ROLL_BY_MIDNIGHT_AGAIN: {
			time_t rawtime;
			struct tm info;

			time(&rawtime);
			localtime_r(&rawtime, &info);
			int secSinceMidnight = info.tm_hour * 3600 +
						info.tm_min * 60 + info.tm_sec;

			if (secSinceMidnight < rollover) {
				tsleep = rollover - secSinceMidnight;
			} else {
				int y = secSinceMidnight - rollover;
				int z = y / rollagain;
				tsleep = (z + 1) * rollagain + rollover - secSinceMidnight;
			}
			if (tsleep < MIN_ROLL_1) {
				tsleep += rollagain;
			}
		}
			break;
		default:
			tsleep = ROLL_DEFAULT_SLEEP;
			break;
		}

		sleep(tsleep);
		int oldstate = 0;
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);
		handleRollover();
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &oldstate);
	}

	return NULL;
}

static void* flushThreadInit(void *m)
{
	/* if got here, then orthogonal flush requested */

	while (1) {
		if (flushtime < MIN_FLUSH_TIME)
			flushtime = MIN_FLUSH_TIME;

		sleep(flushtime);
		int oldstate = 0;
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);
		handleFlush();
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &oldstate);
	}

	return NULL;
}

/**
 * \brief Configuration
 */
static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl,
		struct attr_value_list *avl)
{
	char *s = NULL;
	char *m = NULL;
	char *streamlist = NULL;
	char *templist = NULL;
	char *saveptr = NULL;
	char *pch = NULL;
	int rc;

	pthread_mutex_lock(&cfg_lock);
	/* only call once. cannot reset state from subscribe. */
	if (cfgstate != CFG_PRE) {
		msglog(LDMSD_LDEBUG, PNAME ": cannot call config again\n");
		pthread_mutex_unlock(&cfg_lock);
		return -1;
	}

        stream_idx = idx_create();
	if (!stream_idx) {
		msglog(LDMSD_LERROR, PNAME ": should have empty stream_idx\n");
		pthread_mutex_unlock(&cfg_lock);
		return -1;
	}

	buffer = 1; /* default */
	s = av_value(avl, "buffer");
	if (s) {
		buffer = atoi(s);
		msglog(LDMSD_LDEBUG, PNAME ": setting buffer to '%d'\n", buffer);
	}

	s = av_value(avl, "stream");
	if (!s) {
		msglog(LDMSD_LDEBUG, PNAME ": missing stream in config\n");
		rc = EINVAL;
		goto out;
	} else {
		streamlist = strdup(s);
		if (!streamlist) {
			rc = ENOMEM;
			goto out;
		}
	}

	s = av_value(avl, "path");
	if (!s) {
		msglog(LDMSD_LDEBUG, PNAME ": missing path in config\n");
		rc = EINVAL;
		goto out;
	} else {
		root_path = strdup(s);
		if (!root_path) {
			rc = ENOMEM;
			goto out;
		}
		msglog(LDMSD_LDEBUG, PNAME ": setting root_path to '%s'\n", root_path);
	}

	s = av_value(avl, "container");
	if (!s) {
		msglog(LDMSD_LDEBUG, PNAME ": missing container in config\n");
		rc = EINVAL;
		goto out;
	} else {
		container = strdup(s);
		if (!container) {
			rc = ENOMEM;
			goto out;
		}
		msglog(LDMSD_LDEBUG, PNAME ": setting container to '%s'\n", container);
	}

	cfgstate = CFG_BASIC;

	templist = strdup(streamlist);
	if (!templist) {
		rc = ENOMEM;
		goto out;
	}
	pch = strtok_r(templist, ",", &saveptr);
	while (pch != NULL) {
		msglog(LDMSD_LDEBUG, PNAME ": opening streamstore for '%s'\n", pch);
		rc = open_streamstore(pch);
		if (rc) {
			msglog(LDMSD_LERROR, PNAME ": Error opening store for stream '%s'\n", pch);
			goto err;
		}
		pch = strtok_r(NULL, ",", &saveptr);
	}
	free(templist);
	templist = NULL;

	/* only set rollover once we have created the files */
	s = av_value(avl, "rolltype");
	m = av_value(avl, "rollover");
	if ((s && !m) || (m && !s)) {
		msglog(LDMSD_LERROR, PNAME ": rolltype given without rollover.\n");
		rolltype = DEFAULT_ROLLTYPE;
		rc = EINVAL;
		goto err;
	}
	if (s) {
		rolltype = atoi(s);
		rollover = atoi(m);
		if (rolltype < MINROLLTYPE || rolltype > MAXROLLTYPE) {
			msglog(LDMSD_LERROR, PNAME ": rolltype out of range '%s'.\n", s);
			rc = EINVAL;
			goto err;
		}

		if (rollover < 0) {
			msglog(LDMSD_LERROR, PNAME ": Error: bad rollover value '%s'\n", m);
			rc = EINVAL;
			goto err;
		}

		if (rolltype == MAXROLLTYPE) {
			s = av_value(avl, "rollagain");
			if (s) {
				rollagain = atoi(s);
				if (rollagain < 0) {
					msglog(LDMSD_LERROR, PNAME
						": bad rollagain value '%s'\n", s);
					rc = EINVAL;
					goto err;
				}
				if (rollagain < rollover||
				rollagain < MIN_ROLL_1) {
					msglog(LDMSD_LERROR, PNAME
						"rolltype=5 needs rollagain"
						" > max(rollover,10) :"
						" rollagain='%s' "
						" rollover='%s'\n",
						rollover, rollagain);
					rc = EINVAL;
					goto err;
				}
			} else {
				msglog(LDMSD_LERROR, PNAME ": rolltype %d requires"
						" rollagain=<value>\n", rolltype);
			}
		}

		pthread_create(&rothread, NULL, rolloverThreadInit, NULL);
		rothread_used = 1;
		msglog(LDMSD_LDEBUG, PNAME ": using rolltype %d rollover %d\n",
							rolltype, rollover);
	}

	/* only set flush once we have created the files */
	s = av_value(avl, "flushtime");
	if (s) {
		flushtime = atoi(s);

		pthread_create(&flthread, NULL, flushThreadInit, NULL);
		flthread_used = 1;
		msglog(LDMSD_LDEBUG, PNAME ": setting default flush time to %d\n",
									flushtime);
	}

	goto out;

err:
	/* delete any created streamstores */
	rolltype = DEFAULT_ROLLTYPE;
	rollover = 0;
	flushtime = 0;
	idx_traverse(stream_idx, close_streamstore, NULL);
	idx_destroy(stream_idx);
	stream_idx = idx_create();

	msglog(LDMSD_LDEBUG, PNAME ": failed config\n");

out:
	free(templist);
	templist = NULL;
	free(streamlist);
	streamlist = NULL;
	if (rc == 0)
		cfgstate = CFG_DONE;
	else
		cfgstate = CFG_FAILED;

	pthread_mutex_unlock(&cfg_lock);

	return rc;
}

static void term(struct ldmsd_plugin *self)
{

	if (rothread_used) {
		void *dontcare = NULL;
		pthread_cancel(rothread);
		pthread_join(rothread, &dontcare);
	}
	if (flthread_used) {
		void *dontcare = NULL;
		pthread_cancel(flthread);
		pthread_join(flthread, &dontcare);
	}

	pthread_mutex_lock(&cfg_lock);

	if (stream_idx) {
		idx_traverse(stream_idx, close_streamstore, NULL);
		idx_destroy(stream_idx);
		stream_idx = NULL;
	}

	free(root_path);
	root_path = NULL;
	free(container);
	container = NULL;
	buffer = 1;
	rolltype = DEFAULT_ROLLTYPE;
	rollover = 0;
	rollagain = 0;
	flushtime = 0;

	cfgstate = CFG_PRE;
	pthread_mutex_unlock(&cfg_lock);

	return;
}

static const char* usage(struct ldmsd_plugin *self)
{
	return "    config name=stream_csv_store path=<path> container=<container> stream=<stream> \n"
			"          [flushtime=<N>] [buffer=<0/1>] [rollover=<N> rolltype=<N>]\n"
			"         - Set the root path for the storage of csvs and some default parameters\n"
			"         - path          The path to the root of the csv directory\n"
			"         - container     The directory under the path\n"
			"         - stream        a comma separated list of streams, each of which will also be its file name\n"
			"         - flushtime     Time in sec for a regular flush (independent of any other rollover or flush directives)\n"
			" 	  - buffer        0 to disable buffering, 1 to enable it with autosize (default)\n"
			"         - rollover      Greater than or equal to zero; enables file rollover and sets interval\n"
			"         - rolltype      [1-n] Defines the policy used to schedule rollover events.\n"
	ROLLTYPES
	"\n";
}

static struct ldmsd_store stream_csv_store = {
	.base = {
		.name = "stream_csv_store",
		.type = LDMSD_PLUGIN_STORE,
		.term = term,
		.config = config,
		.usage = usage,
	},
};

struct ldmsd_plugin* get_plugin(ldmsd_msg_log_f pf)
{
	msglog = pf;
	return &stream_csv_store.base;
}
