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
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY out OF THE USE
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
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_stream.h"


/**
 * CURRENT GROUND RULES:
 * 1) the json will be something like:
 *            {foo:1, bar:2, zed-data:[{count:1, name:xyz},{count:2, name:abc}]}
 * 2) can get this at the beginning and assume it will be true for all time
 * 3) there will be at most 1 list
 * 4) can have a list with no entries
 * 5) In the writeout, each dict will be in its own csv line with the singletons
 */


//user changes these in the code before build, for now
#undef NDATA_TIMING
#define NDATA 10000
#define TIMESTAMP_STORE

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))
#endif

#define _stringify(_x) #_x
#define stringify(_x) _stringify(_x)


#define PNAME "l2_stream_csv_store"

static ldmsd_msg_log_f msglog;
static int rollover;
#define DEFAULT_ROLLTYPE -1
#define ROLL_LIMIT_INTERVAL 60
#define ROLL_DEFAULT_SLEEP 60
#define ROLL_BY_RECORDS 3
//NOTE: only type ROLL_BY_RECORDS is supported for now
static int rolltype = DEFAULT_ROLLTYPE;
/** minimum rollover for type 3;
    rolltype==3 and rollover < MIN_ROLL_RECORDS -> rollover = MIN_ROLL_RECORDS */
#define MIN_ROLL_RECORDS 3
/** Interval to check for passing the record or byte count limits. */


static pthread_t rothread;
static int rothread_used = 0;

static char* root_path = NULL;
static char* container = NULL;
static int buffer = 0;
static pthread_mutex_t cfg_lock; //about config args, not about the header

static idx_t stream_idx;
// Well known:: written order will be singletonkeys followed by listentry keys
struct linedata {
	int nsingleton;
	char** singletonkey;
	int nlist;
	char* listkey; //will have at most one list;
	int ndict; /** variable number of dicts in list. all
                       dicts in list must have same keys */
	char** dictkey;
	int nkey;
	int nheaderkey;  //number of keys in the header
        char* header;
};



struct csv_stream_handle{
        /** key will be stream. NOT container/schema like store/csv since you
            can only subscribe once to a stream. it is like a schema in a
            canonical store that it will have to be unique and immutable. */
        char* stream; //this is the key for this handle
        char* basename; /** file base name. add the timestamp on:
                            (root_path + container + stream) */
        FILE* file;
        int store_count; //for the roll
        struct linedata dataline; //used to keep track of keys for the header
        pthread_mutex_t lock;
};

#ifdef NDATA_TIMING
static int temp_count = 0; /** tracks written lines for timing. this is
                               incremented over all stream, not per stream */
#endif

static void _clear_key_info(struct linedata* dataline){
	int i;

        //have stream lock
        if (dataline->header) free(dataline->header);
        dataline->header = NULL;
	for (i = 0; i < dataline->nsingleton; i++){
		if (dataline->singletonkey[i]) free(dataline->singletonkey[i]);
	}
	if (dataline->singletonkey) free(dataline->singletonkey);
	dataline->singletonkey = NULL;
	dataline->nsingleton = 0;

	if (dataline->listkey) free (dataline->listkey);
	dataline->listkey = NULL;
	dataline->nlist = 0;

	for (i = 0; i < dataline->ndict; i++){
		if (dataline->dictkey[i]) free (dataline->dictkey[i]);
		dataline->dictkey[i] = NULL;
	}
	dataline->ndict = 0;
	dataline->nheaderkey = 0;
        if (dataline->header) free(dataline->header);
        dataline->header = NULL;
}

static void close_streamstore(void *obj, void *cb_arg){

        if (!obj) return;

        pthread_mutex_lock(&cfg_lock);
        struct csv_stream_handle *stream_handle =
                (struct csv_stream_handle *)obj;

        pthread_mutex_lock(&stream_handle->lock);

        //FIXME: need a stream unsubscribe?

        msglog(LDMSD_LDEBUG, PNAME ": Closing stream store <%s>\n",
               stream_handle->stream);

        if (stream_handle->file) {
                fflush(stream_handle->file);
                fsync(fileno(stream_handle->file));
                fclose(stream_handle->file);
        }
        stream_handle->file = NULL;

        _clear_key_info(&stream_handle->dataline);
        stream_handle->store_count = 0;
        free(stream_handle->basename);
        stream_handle->basename = NULL;

        idx_delete(stream_idx,
                   stream_handle->stream, strlen(stream_handle->stream));
        free(stream_handle->stream);
        stream_handle->stream = NULL;

        pthread_mutex_unlock(&stream_handle->lock);
        pthread_mutex_destroy(&stream_handle->lock);


        pthread_mutex_unlock(&cfg_lock);

        return;
}


static int _parse_list_for_header(struct linedata *dataline, json_entity_t e){
	/** get specialized data for building header.
            this should only be list of dicts */
	json_entity_t li, di;
	int idict;
	int i;

        /** NOTE: if needed for performance reasons, can we eliminate some of
            the checking and jsut rely on getting NULL returns when we ask
            for the actual item that we would want? */

        if (!dataline){
                return -1;
        }

	// for getting the header, we only care about the first li
	li = json_item_first(e);

	if (li == NULL){
		msglog(LDMSD_LERROR,
                       PNAME ": list cannot be empty for header.\n");
		return -1;
	}
	if (li->type != JSON_DICT_VALUE){
		msglog(LDMSD_LERROR,
                       PNAME ": list can only have dict entries\n");
		return -1;
	}
	// parse the dict. process once to fill and 2nd time to fill
	for (i = 0; i < 2; i++){
		idict = 0;
		for (di = json_attr_first(li); di; di = json_attr_next(di)){
			//only allowed singleton entries
			/**  msglog(LDMSD_LDEBUG,
                             PNAME ": list %s dict item %d = %s\n",
                             dataline.listkey, idict,
                             di->value.attr_->name->value.str_->str); */
			if (i == 1) {
                                dataline->dictkey[idict] =
                                        strdup(di->value.attr_->name->value.str_->str);
			}
			idict++;
		}
		if (idict == 0){
			msglog(LDMSD_LDEBUG,
                               PNAME ": empty dict for header parse\n");
			return -1;
		}
		if (i == 0){
			dataline->dictkey =
                                (char**) calloc(idict, sizeof(char*));
			if (!(dataline->dictkey)) return ENOMEM;
			dataline->ndict = idict;
		}
	}
	return 0;

};

static int _get_header_from_data(struct linedata *dataline, json_entity_t e){
	// from the data, builds the header and supporting structs.
        // all lists and dict options must be in this line

	json_entity_t a;
	int isingleton, ilist;
	int i, rc;


        if (!dataline){
                return EINVAL;
        }
        _clear_key_info(dataline);

        jbuf_t jb = jbuf_new();  /** use jbuf as a variable length string to
                                     add things to. Only need to do once. */

	// process to build the header.
	i = 0;
	isingleton = 0;
	ilist = 0;
	for (a = json_attr_first(e); a; a = json_attr_next(a)){
		json_attr_t attr = a->value.attr_;
                /** msglog(LDMSD_LDEBUG,
                    PNAME ": get_header_from_data: parsing attr %d '%s' with value type %s\n",
                    i, attr->name->value.str_->str,
                    json_type_name(attr->value->type)); */
		switch (attr->value->type){
		case JSON_LIST_VALUE:
			/** assumes that all the dicts in a list will have
                            the same keys for all time */
			if (ilist != 0){
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
			//treat like a singleton
			isingleton++;
			break;
		case JSON_ATTR_VALUE:
			msglog(LDMSD_LERROR,
                               PNAME ": should not have ATTR type now\n");
			rc = EINVAL;
			goto err;
			break;
		default:
			//it's a singleton
			isingleton++;
			break;
		}
		i++;
	}

	if ((isingleton == 0) && (ilist == 0)){
		msglog(LDMSD_LERROR,
                       PNAME ": no keys for header. Waiting for next one\n");
		rc = EINVAL;
		goto err;
	}

        dataline->nsingleton = isingleton;
        dataline->nlist = ilist;
        dataline->singletonkey =
                (char**) calloc(dataline->nsingleton, sizeof(char*));
	if (dataline->nsingleton &&
            !dataline->singletonkey){
		rc = ENOMEM;
		goto err;
	}

	// process again to fill. will parse dicts here
	i = 0;
	isingleton = 0;
	for (a = json_attr_first(e); a; a = json_attr_next(a)){
		json_attr_t attr = a->value.attr_;
		switch (attr->value->type){
		case JSON_LIST_VALUE:
			dataline->listkey = strdup(attr->name->value.str_->str);
			rc = _parse_list_for_header(dataline,attr->value);
			if (rc) goto err;
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
			//it's a singleton
			dataline->singletonkey[isingleton++] =
                                strdup(attr->name->value.str_->str);
			break;
		}
		i++;
	}

	dataline->nheaderkey = dataline->nsingleton + dataline->ndict;

	/** order will be order of singletons and order of dict.
            repeat dicts will be separate entries in the csv */
	for (i = 0; i < dataline->nsingleton; i++){
		if (i < dataline->nheaderkey-1){
			jb = jbuf_append_str(jb, "%s,",
                                             dataline->singletonkey[i]);
		} else {
			jb = jbuf_append_str(jb, "%s",
                                             dataline->singletonkey[i]);
		}
	}

	for (i = 0; i < dataline->ndict; i++){
		if (i < dataline->ndict-1){
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
        jbuf_free(jb);

	return 0;

err:
        jbuf_free(jb);
	msglog(LDMSD_LDEBUG, PNAME ": header build failed\n");
	_clear_key_info(dataline);

	return rc;
}


static int _append_singleton(json_entity_t en, jbuf_t jb){

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
		//this should not happen
		msglog(LDMSD_LDEBUG,
                       PNAME, ": cannot process JSON type '%s' as singleton\n",
		       json_type_name(en->type));
		return -1;
		break;
	}

	return 0;
}


static int _print_header(struct csv_stream_handle *stream_handle){

        if (stream_handle &&
            stream_handle->file &&
            stream_handle->dataline.header){
                fprintf(stream_handle->file, "#%s\n",
                        stream_handle->dataline.header);
                fflush(stream_handle->file);
                fsync(fileno(stream_handle->file));
                stream_handle->store_count = 0; //first line in a file

                return 0;
        }

        return -1;
}



static int _print_data_lines(struct csv_stream_handle *stream_handle,
                             json_entity_t e){
	//well known order

	json_entity_t en, li;
	jbuf_t jbs, jb;
	int iheaderkey;
	int i;
	int rc;

	/** NOTE: if needed for performance reasons, can we eliminate some
            of the checking and jsut rely on getting NULL returns when we
            ask for the actual item that we would want? */

        if (!stream_handle || !stream_handle->file){
                return -1;
        }

        struct linedata *dataline = &stream_handle->dataline;

#if defined(TIMESTAMP_STORE)
	struct timeval tv_prev;
	gettimeofday(&tv_prev, 0);
#endif

	iheaderkey = 0;

	jbs = NULL;
	jbs = jbuf_new();
	if (dataline->nsingleton > 0){
		for (i = 0; i < dataline->nsingleton; i++){
			en = json_value_find(e, dataline->singletonkey[i]);
			if (en == NULL){
                                //this may or may not be ok....
			} else {
				rc = _append_singleton(en, jbs);
				if (rc){
					msglog(LDMSD_LDEBUG,
                                               PNAME ": Cannot print data because of a variable print problem\n");
					jbuf_free(jbs);
					return rc;
				}
			}
			if (iheaderkey < (dataline->nheaderkey-1)){
				jbs = jbuf_append_str(jbs, ",");
			}
			iheaderkey++;
		}
	} else {
		jbs = jbuf_append_str(jbs,""); //Need this for adding list items
	}

	//for each dict, write a separate line in the file
	//if header has no list or an empty list, just write the singletons
	if (dataline->nlist == 0){
#ifdef TIMESTAMP_STORE
		fprintf(stream_handle->file, "%s,%f\n", jbs->buf,
                        (tv_prev.tv_sec + tv_prev.tv_usec/1000000.0));
#else
		fprintf(stream_handle->file, "%s\n", jbs->buf);
#endif
		jbuf_free(jbs);
		return 0;
	}

	//if header has a list.....
	en = json_value_find(e, dataline->listkey);
	//if data has no list
	if (en == NULL){
		msglog(LDMSD_LDEBUG,
                       PNAME ": no match for %s\n", dataline->listkey);
		/** write out just the singleton line and empty vals
                    for the dict items */
		for (i = 0; i < dataline->ndict-1; i++){
			jbs = jbuf_append_str(jbs, ",");
		}
#ifdef TIMESTAMP_STORE
		fprintf(stream_handle->file, "%s,%f\n", jbs->buf,
                        (tv_prev.tv_sec + tv_prev.tv_usec/1000000.0));
#else
		fprintf(stream_handle->file, "%s\n", jbs->buf);
#endif
		jbuf_free(jbs);
		return 0;
	}

	//if we got the val, but its not a list
	if (en->type != JSON_LIST_VALUE){
		msglog(LDMSD_LERROR,
                       PNAME ": %s is not a LIST type %s. skipping this data.\n",
		       dataline->listkey, json_type_name(en->type));
		/** NOTE: this is bad. currently writing out nothing,
                    but could change this later. */
		jbuf_free(jbs);
		return -1;
	}

	//if there are no dicts
	if (json_item_first(en) == NULL){
		for (i = 0; i < dataline->ndict-1; i++){
			jbs = jbuf_append_str(jbs, ",");
		}
#ifdef TIMESTAMP_STORE
		fprintf(stream_handle->file, "%s,%f\n", jbs->buf,
                        (tv_prev.tv_sec + tv_prev.tv_usec/1000000.0));
#else
		fprintf(stream_handle->file, "%s\n", jbs->buf);
#endif
		jbuf_free(jbs);
		return 0;
	}

	//if there are dicts
	for (li = json_item_first(en); li; li = json_item_next(li)){
		if (li->type != JSON_DICT_VALUE){
			msglog(LDMSD_LERROR,
			       PNAME ": LIST %s has innards that are not a DICT type %s. skipping this data.\n",
			       dataline->listkey, json_type_name(li->type));
			//no output
			jbuf_free(jbs);
			return -1;
		}

		//each dict will be its own line
		jb = jbuf_new();
		jb = jbuf_append_str(jb, "%s", jbs->buf);
		for (i = 0; i < dataline->ndict; i++){
			json_entity_t edict =
                                json_value_find(li, dataline->dictkey[i]);
			if (edict == NULL){
				msglog(LDMSD_LDEBUG,
				       PNAME ": NULL return from find for key <%s>\n",
				       dataline->dictkey[i]);
				//print nothing
			} else {
				rc = _append_singleton(edict, jb);
				if (rc){
					msglog(LDMSD_LDEBUG,
                                               PNAME ": Cannot print data because of a variable print problem\n");
					jbuf_free(jbs);
					jbuf_free(jb);
					return rc;
				}
			}
			if (i < (dataline->ndict-1)) {
				jb = jbuf_append_str(jb, ",");
			}
		}
#ifdef TIMESTAMP_STORE
		fprintf(stream_handle->file, "%s,%f\n", jb->buf,
                        (tv_prev.tv_sec + tv_prev.tv_usec/1000000.0));
#else
		fprintf(stream_handle->file, "%s\n", jb->buf);
#endif
                stream_handle->store_count++; /** stream_cb has the lock, so
                                                  roll cannot be called while
                                                  this is going on. */
		jbuf_free(jb);

	}
	jbuf_free(jbs);




#ifdef NDATA_TIMING
        //note this is INFO not DEBUG
	temp_count++;
	if ((temp_count % NDATA) == 0){
		struct timespec tv_now;
		clock_gettime(CLOCK_REALTIME, &tv_now);
		msglog(LDMSD_LINFO, PNAME " %d lines processed timestamp %ld\n",
		       temp_count, tv_now.tv_sec);
	}
#endif

	return 1;
}


static int stream_cb(ldmsd_stream_client_t c, void *ctxt,
		     ldmsd_stream_type_t stream_type,
		     const char *msg, size_t msg_len,
		     json_entity_t e) {

        struct csv_stream_handle* stream_handle;
	int rc = 0;


        /**	msglog(LDMSD_LDEBUG,
                PNAME ": Calling stream_cb. msg '%s' on stream '%s'\n",
                msg, ldmsd_stream_client_name(c)); */

        const char *skey = ldmsd_stream_client_name(c);
        if (!skey){
                msglog(LDMSD_LERROR,
                       PNAME ": Cannot get stream_name from client\n");
                return -1;
        }

        pthread_mutex_lock(&cfg_lock); /** really don't need this, since cannot
                                           dynamically add streams and cannot
                                           destroy them */
        stream_handle = idx_find(stream_idx, (void *)skey, strlen(skey));
        if (!stream_handle){
                msglog(LDMSD_LERROR,
                       PNAME ": No stream_store for '%s'\n", skey);
                pthread_mutex_unlock(&cfg_lock);
                return -1;

        }

	pthread_mutex_lock(&stream_handle->lock);
        pthread_mutex_unlock(&cfg_lock);
        /** currently releasing this, since the only way to destroy is in the
            overall shutdown, plus want to be able to do the callback on
            independent streams at the same time */

	if (!stream_handle->file){
		msglog(LDMSD_LERROR,
                       PNAME ": Cannot insert values for '%s': file is NULL\n",
		       stream_handle->stream);
		rc = EPERM;
		goto out;
	}

	/** msg will be populated. if the type was json,
            entity will also be populated. */
	if (stream_type == LDMSD_STREAM_STRING){
		fprintf(stream_handle->file, "%s\n", msg);
		if (!buffer){
			fflush(stream_handle->file);
			fsync(fileno(stream_handle->file));
		}
	} else if (stream_type == LDMSD_STREAM_JSON){
		if (!e){
			msglog(LDMSD_LERROR, PNAME ": why is entity NULL?\n");
			rc = EINVAL;
			goto out;
		}

		if (e->type != JSON_DICT_VALUE) {
			msglog(LDMSD_LERROR,
                               PNAME ": Expected a dict object, not a %s.\n",
			       json_type_name(e->type));
			rc = EINVAL;
			goto out;
		}

                if (!stream_handle->dataline.header){
                        rc = _get_header_from_data(&stream_handle->dataline,e);
			if (rc != 0) {
				msglog(LDMSD_LDEBUG,
                                       PNAME ": error processing header from data <%d>\n", rc);
				goto out;
			}
                        _print_header(stream_handle);
		}

		_print_data_lines(stream_handle, e);
		if (!buffer){
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


static int open_streamstore(char* stream){
        struct csv_stream_handle *stream_handle;
        char *tmp_filename;
        char *tmp_basename;
	char *dpath;
        FILE *tmp_file;
        unsigned long tspath;
        int rc = 0;


	// already have the cfg_lock
	if (!root_path || !container || !stream) {
	     msglog(LDMSD_LERROR, PNAME ": config not called. cannot open.\n");
	     return ENOENT;
	}

        //NOTE: unlike store, this doesn't have the possibility of closing yet.
        stream_handle = idx_find(stream_idx, (void *)stream, strlen(stream));
        if (stream_handle){
                msglog(LDMSD_LERROR,
                       PNAME ": cannot open stream item. already have it\n");
		return EINVAL;
	}

	// add additional 12 for the timestamp
	size_t pathlen =
                strlen(root_path) + strlen(stream) + strlen(container) + 8 + 12;
        dpath = malloc(pathlen);
	tmp_basename = malloc(pathlen);
        tmp_filename = malloc(pathlen);
	if (!dpath || !tmp_filename || !tmp_basename){
		rc = ENOMEM;
		goto out;
	}

        tspath = (unsigned long)(time(NULL));
        sprintf(dpath, "%s/%s", root_path, container);
	sprintf(tmp_basename, "%s/%s/%s", root_path, container, stream);
        sprintf(tmp_filename, "%s/%s/%s.%lu",
                root_path, container, stream, tspath);

	msglog(LDMSD_LDEBUG, PNAME ": stream '%s' will have file '%s'\n",
               stream, tmp_filename);

	/* create path if not already there. */
	rc = mkdir(dpath, 0777);
	if ((rc != 0) && (errno != EEXIST)) {
		msglog(LDMSD_LERROR,
                       PNAME ": Failure %d creating directory '%s'\n",
                       errno, dpath);
		rc = ENOENT;
		goto err1;
	}
	rc = 0;

	tmp_file = fopen_perm(tmp_filename, "a+", LDMSD_DEFAULT_FILE_PERM);
	if (!tmp_file){
		msglog(LDMSD_LERROR, PNAME ": Error %d opening the file %s.\n",
		       errno,tmp_filename);
		rc = ENOENT;
		goto err1;
	}

        stream_handle = calloc(1, sizeof *stream_handle);
        if (!stream_handle){
                rc = ENOMEM;
                goto err1;
        }

       	pthread_mutex_init(&stream_handle->lock, NULL);
	pthread_mutex_lock(&stream_handle->lock);

        //swap
        stream_handle->file = tmp_file;
        stream_handle->basename = tmp_basename;
        stream_handle->stream = strdup(stream);

        idx_add(stream_idx, (void *)stream, strlen(stream), stream_handle);

	pthread_mutex_unlock(&stream_handle->lock);

	goto out;

err1:

        free(tmp_basename);
	pthread_mutex_unlock(&stream_handle->lock);
	pthread_mutex_destroy(&stream_handle->lock);

out:

        free(tmp_filename);
	free(dpath);
	return rc;

}

static void roll_cb(void *obj, void *cb_arg){
        FILE *nfp = NULL;
        char *tmp_filename = NULL;
        size_t pathlen;
        unsigned long tmp_tspath;

        //if we've got here then we've called a stream_store
        if (!obj){
                return;
        }
        struct csv_stream_handle *stream_handle =
                (struct csv_stream_handle *)obj;

        pthread_mutex_lock(&stream_handle->lock);

        switch (rolltype){
        case ROLL_BY_RECORDS:
                if (stream_handle->store_count < rollover){
                        goto out;
                }
                break;
        default:
                msglog(LDMSD_LDEBUG,
                       PNAME ": Error: unexpected rolltype in store(%d)\n",
                       rolltype);
                break;
        }

        if (stream_handle->file){ //this should always be true
                fflush(stream_handle->file);
                fsync(fileno(stream_handle->file));
        }

        //re name: if got here, then rollover requested.

        pathlen = strlen(stream_handle->basename) + 12;
        tmp_filename = malloc(pathlen);
        if (!tmp_filename){
                goto out;
        }
        tmp_tspath = (unsigned long)(time(NULL));
	sprintf(tmp_filename, "%s.%lu", stream_handle->basename, tmp_tspath);

	msglog(LDMSD_LDEBUG,
               PNAME ": stream '%s' will have file '%s'\n",
               stream_handle->stream, tmp_filename);

        nfp = fopen_perm(tmp_filename, "a+", LDMSD_DEFAULT_FILE_PERM);
	if (!nfp){
		msglog(LDMSD_LERROR, PNAME ": Error %d opening the file %s.\n",
		       errno,tmp_filename);
		goto out;
	}

        //close and swap
        if (stream_handle->file){ //this should always be true
                fclose(stream_handle->file);
        }
        stream_handle->file = nfp;
        _print_header(stream_handle);

out:

	if (tmp_filename) free(tmp_filename);
        pthread_mutex_unlock(&stream_handle->lock);

        //NOTE: nothing is done with the rc
}


static int handleRollover(){
        pthread_mutex_lock(&cfg_lock); /** don't add any stores during this,
                                           which currently cannot do anyway. */
        idx_traverse(stream_idx, roll_cb, NULL);
        pthread_mutex_unlock(&cfg_lock);

        return 0;
}

static void* rolloverThreadInit(void* m){
        //if got here, then rollover requested

        while(1){
                int tsleep;
                switch (rolltype){
                case ROLL_BY_RECORDS: //only currently valid type
                        if (rollover < MIN_ROLL_RECORDS)
                                rollover = MIN_ROLL_RECORDS;
                        tsleep = ROLL_LIMIT_INTERVAL;
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


/**
 * \brief Configuration
 */
static int config(struct ldmsd_plugin *self,
                  struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char* s;
        char* streamlist;
        char* templist = NULL;
        char* saveptr = NULL;
        char* pch = NULL;
	int rc;

	pthread_mutex_lock(&cfg_lock);
        //only call once
        if (root_path != NULL){
                msglog(LDMSD_LDEBUG, PNAME ": cannot call config again\n");
                pthread_mutex_unlock(&cfg_lock);
                return -1;
        }

	s = av_value(avl, "buffer");
	if (!s){
		buffer = atoi(s);
		msglog(LDMSD_LDEBUG,
                       PNAME ": setting buffer to '%d'\n", buffer);
	}

	s = av_value(avl, "stream");
	if (!s){
		msglog(LDMSD_LDEBUG, PNAME ": missing stream in config\n");
		rc = EINVAL;
		goto out;
	} else {
		streamlist = strdup(s);
	}

	s = av_value(avl, "path");
	if (!s){
		msglog(LDMSD_LDEBUG, PNAME ": missing path in config\n");
		rc = EINVAL;
		goto out;
	} else {
		root_path = strdup(s);
		msglog(LDMSD_LDEBUG,
                       PNAME ": setting root_path to '%s'\n", root_path);
	}


	s = av_value(avl, "container");
	if (!s){
		msglog(LDMSD_LDEBUG, PNAME ": missing container in config\n");
		rc = EINVAL;
		goto out;
	} else {
		container = strdup(s);
		msglog(LDMSD_LDEBUG,
                       PNAME ": setting container to '%s'\n", container);
	}


        templist = strdup(streamlist);
        if (!templist){
                rc = ENOMEM;
                goto out;
        }
        pch = strtok_r(templist, ",", &saveptr);
        while (pch != NULL){
                msglog(LDMSD_LDEBUG,
                       PNAME ": opening streamstore for '%s'\n", pch);
                rc = open_streamstore(pch);
                if (rc){
                        msglog(LDMSD_LERROR,
                               PNAME ": Error opening store for strean '%s'\n",
                               pch);
                        goto err;
                }
                pch = strtok_r(NULL, ",", &saveptr);
	}
        free(templist);
        templist = NULL;

        //only set rollover once we have created the files
        s = av_value(avl, "rollover");
        if (s) {
                rollover = atoi(s);
                rolltype = ROLL_BY_RECORDS;
                pthread_create(&rothread, NULL, rolloverThreadInit, NULL);
                rothread_used = 1;
                msglog(LDMSD_LDEBUG,
                       PNAME ": setting rollrecords to %d\n", rollover);
        }


        //subscribe to each one
        templist = strdup(streamlist);
        if (!templist){
                rc = ENOMEM;
                goto out;
        }
        pch = strtok_r(templist, ",", &saveptr);
        while (pch != NULL){
                msglog(LDMSD_LDEBUG,
                       PNAME ": subscribing to stream '%s'\n", pch);
                ldmsd_stream_subscribe(pch, stream_cb, self);
                pch = strtok_r(NULL, ",", &saveptr);
	}
        free(templist);
        templist = NULL;

        goto out;

err:
        //delete any created streamstores
        idx_traverse(stream_idx, close_streamstore, NULL);
        idx_destroy(stream_idx);
        stream_idx = idx_create();

out:
        if (templist) free(templist);
        if (streamlist) free(streamlist);
	pthread_mutex_unlock(&cfg_lock);

	return rc;
}

static void term(struct ldmsd_plugin *self)
{

        pthread_mutex_lock(&cfg_lock);
        free(root_path);
	root_path = NULL;
	free(container);
	container = NULL;
        buffer = 0;
        rolltype = DEFAULT_ROLLTYPE;

        idx_traverse(stream_idx, close_streamstore, NULL);
        idx_destroy(stream_idx);
        stream_idx = NULL;

        pthread_mutex_unlock(&cfg_lock);
        pthread_mutex_destroy(&cfg_lock);

	return;
}

static const char *usage(struct ldmsd_plugin *self)
{
	return  "    config name=l2_stream_csv_store path=<path> container=<container> stream=<stream> [buffer=<0/1>] [rollover=<N>]\n"
		"         - Set the root path for the storage of csvs and some default parameters\n"
		"         - path          The path to the root of the csv directory\n"
		"         - container     The directory under the path\n"
		"         - stream        a comma separated list of streams, each of which will also be its file name\n"
		" 	  - buffer        0 to disable buffering, 1 to enable it with autosize (default)\n"
                "         - rollover      N to enable rollover after approximately N lines in csv. Default no roll\n"
		;
}



static struct ldmsd_store l2_stream_csv_store = {
	.base = {
			.name = "l2_stream_csv_store",
			.type = LDMSD_PLUGIN_STORE,
			.term = term,
			.config = config,
			.usage = usage,
	},
};

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	msglog = pf;
	return &l2_stream_csv_store.base;
}

static void __attribute__ ((constructor)) l2_stream_csv_store_init();
static void l2_stream_csv_store_init()
{
        stream_idx = idx_create();
	pthread_mutex_init(&cfg_lock, NULL);
}

static void __attribute__ ((destructor)) l2_stream_csv_store_fini(void);
static void l2_stream_csv_store_fini()
{
	pthread_mutex_destroy(&cfg_lock);
        idx_destroy(stream_idx);
        if (rothread_used){
                void * dontcare = NULL;
                pthread_cancel(rothread);
                pthread_join(rothread, &dontcare);
        }
        stream_idx = NULL;

}
