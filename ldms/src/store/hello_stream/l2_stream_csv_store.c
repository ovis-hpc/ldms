/**
 * Copyright (c) 2019-2021 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2019-2020 Open Grid Computing, Inc. All rights reserved.
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
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_stream.h"


/**
 * CURRENT GROUND RULES:
 * 1) the json will be something like: {foo:1, bar:2, zed-data:[{count:1, name:xyz},{count:2, name:abc}]}
 * 2) can get this at the beginning and assume it will be true for all time
 * 3) there will be at most 1 list
 * 4) can have a list with no entries
 * 5) In the writeout, each dict will be in its own csv line with the singletons
 * 6) one stream only
 */

//TODO - will want rollover

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))
#endif


#define PNAME "l2_stream_csv_store"

static ldmsd_msg_log_f msglog;

#define _stringify(_x) #_x
#define stringify(_x) _stringify(_x)


static char* root_path;
static char* container;
static char* schema;
static unsigned long tsconfig;
FILE* streamfile;
static char* streamfile_name;
static pthread_mutex_t cfg_lock;
static pthread_mutex_t store_lock;
//Well known that the written order will be singletonkeys followed by listentry keys
struct linedata {
	int nsingleton;
	char** singletonkey;
	int nlist;
	char* listkey; //will have at most one list;
	int ndict; // there are a variable number of dicts in the list. all dicts in a list must have same keys
	char** dictkey;
	int nkey;
	int nheaderkey;  //number of keys in the header
};
static struct linedata dataline;
static int validheader = 0;
static int buffer = 0;
static int temp_count = 0;


static void _clear_key_info(){
	int i, j;

	validheader = 0;
	for (i = 0; i < dataline.nsingleton; i++){
		if (dataline.singletonkey[i]) free(dataline.singletonkey[i]);
	}
	if (dataline.singletonkey) free(dataline.singletonkey);
	dataline.singletonkey = NULL;
	dataline.nsingleton = 0;

	if (dataline.listkey) free (dataline.listkey);
	dataline.listkey = NULL;
	dataline.nlist = 0;

	for (i = 0; i< dataline.ndict; i++){
		if (dataline.dictkey[i]) free (dataline.dictkey[i]);
		dataline.dictkey[i] = NULL;
	}
	dataline.ndict = 0;
	dataline.nheaderkey = 0;
}


static int _parse_list_for_header(json_entity_t e){
	//get specialized data for building the header
	//this should only be list of dicts
	json_entity_t li, di;
	int idict;
	int i;

	//TODO: if needed for performance reasons, can we eliminate some of the checking and jsut rely on
	//getting NULL returns when we ask for the actual item that we would want?

	//for getting the header, we only care about the first li
	li = json_item_first(e);
	if (li == NULL){
		msglog(LDMSD_LERROR, PNAME ": list cannot be empty for header.\n");
		return -1;
	}
	if (li->type != JSON_DICT_VALUE){
		msglog(LDMSD_LERROR, PNAME ": list can only have dict entries\n");
		return -1;
	}
	//parse the dict. process once to fill and 2nd time to fill
	for (i = 0; i < 2; i++){
		idict = 0;
		for (di = json_attr_first(li); di; di = json_attr_next(di)){
			//only allowed singleton entries
			//                        msglog(LDMSD_LDEBUG, PNAME ": list %s dict item %d = %s\n",
			//                               dataline.listkey, idict, di->value.attr_->name->value.str_->str);
			if (i == 1){
			       dataline.dictkey[idict] = strdup(di->value.attr_->name->value.str_->str);
			}
			idict++;
		}
		if (idict == 0){
			msglog(LDMSD_LDEBUG, PNAME ": empty dict for header parse\n");
			return -1;
		}
		if (i == 0){
			dataline.dictkey = (char**) calloc(idict, sizeof(char*));
			if (!(dataline.dictkey)){
				return ENOMEM;
			}
			dataline.ndict = idict;
		}
	}
	return 0;

};

static int _get_header_from_data(json_entity_t e, jbuf_t jb){
	//from the data, builds the header and supporting structs. all lists and dict options must be in this line.

	json_entity_t a;
	int isingleton, ilist, iheaderkey;
	int i, j, rc;

	//TODO: check for thread safety. check the locks

	_clear_key_info();

	// process to build the header.
	msglog(LDMSD_LDEBUG, PNAME ":starting first pass\n");
	i = 0;
	isingleton = 0;
	ilist = 0;
	for (a = json_attr_first(e); a; a = json_attr_next(a)){
		json_attr_t attr = a->value.attr_;
		msglog(LDMSD_LDEBUG, PNAME ": get_header_from_data: parsing attr %d '%s' with value type %s\n",
		       i, attr->name->value.str_->str, json_type_name(attr->value->type));
		switch (attr->value->type){
		case JSON_LIST_VALUE:
			//assumes that all the dicts in a list will have the same keys for all time
			if (ilist != 0){
				msglog(LDMSD_LDEBUG, PNAME ": can only support 1 list in the header\n");
				rc = EINVAL;
				goto err;
			}
			ilist++;
			break;
		case JSON_DICT_VALUE:
			msglog(LDMSD_LERROR, PNAME ": not handling type JSON_DICT_VALUE in header\n");
			rc = EINVAL;
			goto err;
			break;
		case JSON_NULL_VALUE:
			//treat like a singleton
			isingleton++;
			break;
		case JSON_ATTR_VALUE:
			msglog(LDMSD_LERROR, PNAME ": should not have ATTR type now\n");
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
	msglog(LDMSD_LDEBUG, PNAME ": completed first pass\n");

	if ((isingleton == 0) && (ilist == 0)){
		msglog(LDMSD_LERROR, PNAME ": no keys for header. Waiting for next one\n");
		rc = EINVAL;
		goto err;
	}

	dataline.nsingleton = isingleton;
	dataline.nlist = ilist;
	dataline.singletonkey = (char**) calloc(dataline.nsingleton, sizeof(char*));
	if (dataline.nsingleton && !dataline.singletonkey){
		rc = ENOMEM;
		goto err;
	}

	// process again to fill. will parse dicts here
	msglog(LDMSD_LDEBUG, PNAME ": starting second pass\n");
	i = 0;
	isingleton = 0;
	for (a = json_attr_first(e); a; a = json_attr_next(a)){
		json_attr_t attr = a->value.attr_;
		msglog(LDMSD_LDEBUG, PNAME ": get_header_from_data: parsing attr %d '%s' with value type %s\n",
		       i, attr->name->value.str_->str, json_type_name(attr->value->type));
		switch (attr->value->type){
		case JSON_LIST_VALUE:
			dataline.listkey = strdup(attr->name->value.str_->str);
			rc = _parse_list_for_header(attr->value);
			if (rc) goto err;
			break;
		case JSON_DICT_VALUE:
			msglog(LDMSD_LERROR, PNAME ": not handling type JSON_DICT_VALUE in header\n");
			rc = EINVAL;
			goto err;
			break;
		case JSON_ATTR_VALUE:
			msglog(LDMSD_LERROR, PNAME ": should not have ATTR type now\n");
			rc = EINVAL;
			goto err;
			break;
		case JSON_NULL_VALUE:
		default:
			//it's a singleton
			dataline.singletonkey[isingleton++] = strdup(attr->name->value.str_->str);
			break;
		}
		i++;
	}

	msglog(LDMSD_LDEBUG, PNAME ": assembling header\n");
	dataline.nheaderkey = dataline.nsingleton  + dataline.ndict;

	//order will be order of singletons and order of dict. repeat dicts will be separate entries in the csv
	for (i = 0; i < dataline.nsingleton; i++){
		msglog(LDMSD_LDEBUG, "<%s>\n", dataline.singletonkey[i]);
		if (i < dataline.nheaderkey-1){
			jb = jbuf_append_str(jb, "%s,", dataline.singletonkey[i]);
		} else {
			jb = jbuf_append_str(jb, "%s", dataline.singletonkey[i]);
		}
	}

	for (i = 0; i < dataline.ndict; i++){
		msglog(LDMSD_LDEBUG, "<%s:%s>\n", dataline.listkey, dataline.dictkey[i]);
		if (i < dataline.ndict-1){
			jb = jbuf_append_str(jb, "%s:%s,", dataline.listkey, dataline.dictkey[i]);
		} else {
			jb = jbuf_append_str(jb, "%s:%s", dataline.listkey, dataline.dictkey[i]);
		}
	}
//	jb = jbuf_append_str(jb, ",store_recv_time");
	validheader = 1;

	return 0;

err:
	msglog(LDMSD_LDEBUG, PNAME ": header build failed\n");
	_clear_key_info();
	validheader = 0;

	return rc;
}


static int _print_singleton(json_entity_t en, jbuf_t jb){

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
		msglog(LDMSD_LDEBUG, PNAME, ": cannot process JSON type '%s' as singleton\n",
		       json_type_name(en->type));
		return -1;
		break;
	}

	return 0;
}


static int _print_data_lines(json_entity_t e, FILE* file){
	//well known order

	json_entity_t en, li;
	jbuf_t jbs, jb;
	int iheaderkey, ilines;
	int i;
	int rc;

	//TODO: if needed for performance reasons, can we eliminate some of the checking and jsut rely on
	//getting NULL returns when we ask for the actual item that we would want?

#if 0
	struct timeval tv_prev;
	struct timeval tv_now;
	struct timeval tv_diff;
	gettimeofday(&tv_prev, 0);
#endif

	//        msglog(LDMSD_LDEBUG, PNAME ": _print_data_lines begin\n");

	ilines = 0;
	iheaderkey = 0;

	jbs = NULL;
	jbs = jbuf_new();
	if (dataline.nsingleton > 0){
		for (i = 0; i < dataline.nsingleton; i++){
			en = json_value_find(e, dataline.singletonkey[i]);
			if (en == NULL){
				//                                msglog(LDMSD_LDEBUG, PNAME ": NULL return from find for key <%s>\n",
				//                                       dataline.singletonkey[i]);
				//this might be ok..
			} else {
				//                                msglog(LDMSD_LDEBUG, PNAME ": processing key '%d' type '%s'\n",
				//                                       i, json_type_name(en->type));
				rc = _print_singleton(en, jbs);
				if (rc){
					msglog(LDMSD_LDEBUG, PNAME ": Cannot print data because of a variable print problem\n");
					jbuf_free(jbs);
					return rc;
				}
			}
			if (iheaderkey < (dataline.nheaderkey-1)){
				jbs = jbuf_append_str(jbs, ",");
			}
			iheaderkey++;
		}
	} else {
		jbs = jbuf_append_str(jbs,""); //Need this for adding the list items
	}

	//for each dict, write a separate line in the file
	//if header has no list or an empty list, just write the singletons
	if (dataline.nlist == 0){
		//                msglog(LDMSD_LDEBUG, PNAME ": no list\n");
//		fprintf(streamfile, "%s,%f\n", jbs->buf, (tv_prev.tv_sec + tv_prev.tv_usec/1000000.0));
		fprintf(streamfile, "%s\n", jbs->buf);
		jbuf_free(jbs);
		return 0;
	}

	//if header has a list.....
	en = json_value_find(e, dataline.listkey);
	//if data has no list
	if (en == NULL){
		msglog(LDMSD_LDEBUG, PNAME ": no match for %s\n", dataline.listkey);
		//write out just the singleton line and  empty vals for the dict items
		for (i = 0; i < dataline.ndict-1; i++){
			jbs = jbuf_append_str(jbs, ",");
		}
//		fprintf(streamfile, "%s,%f\n", jbs->buf, (tv_prev.tv_sec + tv_prev.tv_usec/1000000.0));
		fprintf(streamfile, "%s\n", jbs->buf);
		jbuf_free(jbs);
		return 0;
	}

	//if we got the val, but its not a list
	if (en->type != JSON_LIST_VALUE){
		msglog(LDMSD_LERROR, PNAME ": %s is not a LIST type %s. skipping this data.\n",
		       dataline.listkey, json_type_name(en->type));
		//TODO: this is bad. currently writing out nothing
		jbuf_free(jbs);
		return -1;
	}

	//if there are no dicts
	if (json_item_first(en) == NULL){
		for (i = 0; i < dataline.ndict-1; i++){
			jbs = jbuf_append_str(jbs, ",");
		}
//		fprintf(streamfile, "%s,%f\n", jbs->buf, (tv_prev.tv_sec + tv_prev.tv_usec/1000000.0));
		fprintf(streamfile, "%s\n", jbs->buf);
		jbuf_free(jbs);
		return 0;
	}

	//if there are dicts
	for (li = json_item_first(en); li; li = json_item_next(li)){
		if (li->type != JSON_DICT_VALUE){
			msglog(LDMSD_LERROR,
			       PNAME ": LIST %s has innards that are not a DICT type %s. skipping this data.\n",
			       dataline.listkey, json_type_name(li->type));
			//no output
			jbuf_free(jbs);
			return -1;
		}

		//each dict will be its own line
		//                msglog(LDMSD_LDEBUG, PNAME ": beginning of dict\n");
		jb = jbuf_new();
		jb = jbuf_append_str(jb, "%s", jbs->buf);
		for (i = 0; i < dataline.ndict; i++){
			json_entity_t edict = json_value_find(li, dataline.dictkey[i]);
			if (edict == NULL){
				msglog(LDMSD_LDEBUG,
				       PNAME ": NULL return from find for key <%s>\n",
				       dataline.dictkey[i]);
				//print nothing
			} else {
				rc = _print_singleton(edict, jb);
				if (rc){
					msglog(LDMSD_LDEBUG, PNAME ": Cannot print data because of a variable print problem\n");
					jbuf_free(jbs);
					jbuf_free(jb);
					return rc;
				}
			}
			if (i < (dataline.ndict-1)) {
				jb = jbuf_append_str(jb, ",");
			}
		}
		//                msglog(LDMSD_LDEBUG, PNAME ": end of dict\n");
//		fprintf(streamfile, "%s,%f\n", jb->buf, (tv_prev.tv_sec + tv_prev.tv_usec/1000000.0));
		fprintf(streamfile, "%s\n", jb->buf);
		ilines++; //note only this case is being counted
		jbuf_free(jb);

	}
	jbuf_free(jbs);

	//note only if you get here is time counted
#if 0
	gettimeofday(&tv_now, 0);
	timersub(&tv_now, &tv_prev, &tv_diff);
	//        msglog(LDMSD_LINFO, PNAME ": print_lines %d duration %f\n", ilines, (tv_diff.tv_sec + tv_diff.tv_usec/1000000.0));
	//        fprintf(streamfile, "#print_lines %f duration %f\n", ilines, (tv_diff.tv_sec + tv_diff.tv_usec/1000000.0));
#endif

	temp_count++;
	if ((temp_count % 10000) == 0){
		struct timespec tv_now;
		clock_gettime(CLOCK_REALTIME, &tv_now);
//		msglog(LDMSD_LINFO, PNAME " %d lines timestamp %f\n",
//		       temp_count, (tv_now.tv_sec + tv_now.tv_nsec/1000000000.0));
		msglog(LDMSD_LINFO, PNAME " %d lines timestamp %ld\n",
		       temp_count, tv_now.tv_sec);
	}


	return 1;
}


static int stream_cb(ldmsd_stream_client_t c, void *ctxt,
		     ldmsd_stream_type_t stream_type,
		     const char *msg, size_t msg_len,
		     json_entity_t e) {

	int iheaderkey = 0;
	int rc = 0;
	int i = 0;


	msglog(LDMSD_LDEBUG, PNAME ": Calling stream_cb. msg '%s'\n", msg);
	pthread_mutex_lock(&store_lock);

	if (!streamfile){
		msglog(LDMSD_LERROR, PNAME ": Cannot insert values for '%s': file is NULL\n",
		       streamfile);
		rc = EPERM;
		goto out;
	}

	// msg will be populated. if the type was json, entity will also be populated.
	if (stream_type == LDMSD_STREAM_STRING){
		fprintf(streamfile, "%s\n", msg);
		if (!buffer){
			fflush(streamfile);
			fsync(fileno(streamfile));
		}
	} else if (stream_type == LDMSD_STREAM_JSON){
		if (!e){
			msglog(LDMSD_LERROR, PNAME ": why is entity NULL?\n");
			rc = EINVAL;
			goto out;
		}

		if (e->type != JSON_DICT_VALUE) {
			msglog(LDMSD_LERROR, PNAME ": Expected a dictionary object, not a %s.\n",
			       json_type_name(e->type));
			rc = EINVAL;
			goto out;
		}

		//testing
		msglog(LDMSD_LDEBUG, PNAME ": type is %s\n", json_type_name(e->type));

		if (!validheader){
			jbuf_t jb = jbuf_new();
			rc = _get_header_from_data(e, jb);
			if (rc != 0) {
				msglog(LDMSD_LDEBUG, PNAME ": error processing header from data <%d>\n", rc);
				jbuf_free(jb);
				goto out;
			}
			fprintf(streamfile, "%s\n", jb->buf);
			fflush(streamfile);
			fsync(fileno(streamfile));
			jbuf_free(jb);
		}

		_print_data_lines(e, streamfile);
		if (!buffer){
			fflush(streamfile);
			fsync(fileno(streamfile));
		}
	} else {
		msglog(LDMSD_LERROR, PNAME ": unknown stream type\n");
		rc = EINVAL;
		goto out;
	}

out:

	pthread_mutex_unlock(&store_lock);
	return rc;
}



static int reopen_container(){

	int rc = 0;
	char* path = NULL;
	char* dpath = NULL;


	//already have the cfg_lock
	if (!root_path || !container || !schema) {
	     msglog(LDMSD_LERROR, PNAME ": config not called. cannot open.\n");
	     return ENOENT;
	}

	if (streamfile){ //dont reopen
		return 0;
	}

	// add additional 12 for the timestamp
	size_t pathlen = strlen(root_path) + strlen(schema) + strlen(container) + 8 + 12;
	path = malloc(pathlen);
	if (!path){
		rc = ENOMEM;
		goto out;
	}
	if (streamfile_name)
		free(streamfile_name);
	dpath = malloc(pathlen);
	if (!dpath) {
		rc = ENOMEM;
		goto out;
	}
	sprintf(path, "%s/%s/%s-%lu", root_path, container, schema, tsconfig);
	sprintf(dpath, "%s/%s", root_path, container);
	streamfile_name = strdup(path);

	msglog(LDMSD_LDEBUG, PNAME ": schema '%s' will have file path '%s'\n", schema, path);

	pthread_mutex_init(&store_lock, NULL);
	pthread_mutex_lock(&store_lock);

	/* create path if not already there. */
	rc = mkdir(dpath, 0777);
	if ((rc != 0) && (errno != EEXIST)) {
		msglog(LDMSD_LERROR, PNAME ": Failure %d creating directory '%s'\n",
			 errno, dpath);
		rc = ENOENT;
		goto err1;
	}
	rc = 0;

	streamfile = fopen_perm(path, "a+", LDMSD_DEFAULT_FILE_PERM);
	if (!streamfile){
		msglog(LDMSD_LERROR, PNAME ": Error %d opening the file %s.\n",
		       errno,path);
		rc = ENOENT;
		goto err1;
	}
	pthread_mutex_unlock(&store_lock);

	goto out;

err1:
	fclose(streamfile);
	streamfile = NULL;
	free(streamfile_name);
	streamfile_name = NULL;
	pthread_mutex_unlock(&store_lock);
	pthread_mutex_destroy(&store_lock);

out:

	free(path);
	free(dpath);
	return rc;

}


/**
 * \brief Configuration
 */
static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char* s;
	int rc;

	pthread_mutex_lock(&cfg_lock);

	s = av_value(avl, "buffer");
	if (!s){
		buffer = atoi(s);
		msglog(LDMSD_LDEBUG, PNAME ": setting buffer to '%d'\n", buffer);
	}

	s = av_value(avl, "stream");
	if (!s){
		msglog(LDMSD_LDEBUG, PNAME ": missing stream in config\n");
		rc = EINVAL;
		goto out;
	} else {
		schema = strdup(s);
		msglog(LDMSD_LDEBUG, PNAME ": setting stream to '%s'\n", schema);
	}


	s = av_value(avl, "path");
	if (!s){
		msglog(LDMSD_LDEBUG, PNAME ": missing path in config\n");
		rc = EINVAL;
		goto out;
	} else {
		root_path = strdup(s);
		msglog(LDMSD_LDEBUG, PNAME ": setting root_path to '%s'\n", root_path);
	}


	s = av_value(avl, "container");
	if (!s){
		msglog(LDMSD_LDEBUG, PNAME ": missing container in config\n");
		rc = EINVAL;
		goto out;
	} else {
		container = strdup(s);
		msglog(LDMSD_LDEBUG, PNAME ": setting container to '%s'\n", container);
	}

	tsconfig = (unsigned long)(time(NULL));
	rc = reopen_container();
	if (rc) {
		msglog(LDMSD_LERROR, PNAME ": Error opening %s/%s/%s\n",
		       root_path, container, schema);
		rc = EINVAL;
		goto out;
	}

	msglog(LDMSD_LDEBUG, PNAME ": subscribing to stream '%s'\n", schema);
	ldmsd_stream_subscribe(schema, stream_cb, self);

out:
	pthread_mutex_unlock(&cfg_lock);

	return rc;
}

static void term(struct ldmsd_plugin *self)
{
	int i;

	if (streamfile)
		fclose(streamfile);
	streamfile = NULL;
	free(root_path);
	root_path = NULL;
	free(container);
	container = NULL;
	free(schema);
	schema = NULL;
	free(streamfile_name);
	streamfile_name = NULL;
	_clear_key_info();
	buffer = 0;

	return;
}

static const char *usage(struct ldmsd_plugin *self)
{
	return  "    config name=l2_stream_csv_store path=<path> container=<container> stream=<stream> [buffer=<0/1>] \n"
		"         - Set the root path for the storage of csvs and some default parameters\n"
		"         - path       The path to the root of the csv directory\n"
		"         - container  The directory under the path\n"
		"         - schema     The stream name which will also be the file name\n"
		" 	  - buffer     0 to disable buffering, 1 to enable it with autosize (default)\n"
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
	pthread_mutex_init(&cfg_lock, NULL);
}

static void __attribute__ ((destructor)) l2_stream_csv_store_fini(void);
static void l2_stream_csv_store_fini()
{
	pthread_mutex_destroy(&cfg_lock);
}
