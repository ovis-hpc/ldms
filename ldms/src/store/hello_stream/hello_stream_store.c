/**
 * Copyright (c) 2019-2020 National Technology & Engineering Solutions
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
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_stream.h"


/**
 * TODO: At a minimum, get the header and always print in the same order
 * LATER: need to support mulitple schema. Can we get an end so we know to release that one?
 */

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))
#endif


#define PNAME "hello_stream_store"
#define STOREHEADER "STOREHEADER"

static ldmsd_msg_log_f msglog;

#define _stringify(_x) #_x
#define stringify(_x) _stringify(_x)


static char* root_path;
static char* container;
static char* schema;
FILE* streamfile;
static char* streamfile_name;
static pthread_mutex_t cfg_lock;
static pthread_mutex_t store_lock;
static char** keyarray = NULL;
static int numkeys = 0;
static int buffer = 0;


static int _get_header_from_data(json_entity_t e){

	json_entity_t a;
	int i, j;

	// process once to count
	i = 0;
	for (a = json_attr_first(e); a; a = json_attr_next(a)){
		i++;
	}
	if (i == 0){
		msglog(LDMSD_LERROR, PNAME ": no keys for header. Waiting for next one\n");
		return -1;
	}

	if (keyarray){
		for (j = 0; j < numkeys; j++){
			free(keyarray[j]);
		}
		free(keyarray);
	}
	numkeys = i;
	keyarray = (char**) calloc(numkeys, sizeof(char*));
	if (!keyarray){
		numkeys = 0;
		return -1;
	}

	// process again to fill
	i = 0;
	for (a = json_attr_first(e); a; a = json_attr_next(a)){
		json_attr_t attr = a->value.attr_;
		if (!attr){
			msglog(LDMSD_LERROR, PNAME ": Why is attr null?\n");
		} else {
			keyarray[i] = strdup(attr->name->value.str_->str);
			fprintf(streamfile, "%s", keyarray[i]);
			i++;
			if (i < numkeys){
				fprintf(streamfile, ",");
			}
		}
	}
	fprintf(streamfile,"\n");

	return 0;
}

static int _get_header_from_headerline(json_entity_t e){

	int i, j;
	char *saveptr = NULL;
	char *pch, *tempstr;


	json_attr_t attr = e->value.attr_;
	if (!attr){
		msglog(LDMSD_LERROR, PNAME ": Why is attr null?\n");
		return -1;
	}

	// process once to count
	tempstr = strdup(attr->value->value.str_->str);
	if (!tempstr) return -1;

	i = 0;
	pch = strtok_r(tempstr, ",", &saveptr);
	while(pch!= NULL){
		i++;
		pch = strtok_r(NULL, ",", &saveptr);
	}
	free(tempstr);
	if (i == 0){
		msglog(LDMSD_LERROR, PNAME ": no keys for header. Waiting for next one\n");
		return -1;
	}

	if (keyarray){
		for (j = 0; j < numkeys; j++){
			free(keyarray[j]);
		}
		free(keyarray);
	}
	numkeys = i;
	keyarray = (char**) calloc(numkeys, sizeof(char*));
	if (!keyarray){
		numkeys = 0;
		return -1;
	}

	// process again to fill
	tempstr = strdup(attr->value->value.str_->str);
	i = 0;
	pch = strtok_r(tempstr, ",", &saveptr);
	while(pch!= NULL){
		keyarray[i] = strdup(pch);
		fprintf(streamfile, "%s", keyarray[i]);
		i++;
		if (i < numkeys){
			fprintf(streamfile, ",");
		}
		pch = strtok_r(NULL, ",", &saveptr);
	}
	fprintf(streamfile,"\n");
	free(tempstr);

	return 0;
}


static int stream_cb(ldmsd_stream_client_t c, void *ctxt,
		     ldmsd_stream_type_t stream_type,
		     const char *msg, size_t msg_len,
		     json_entity_t e) {

	int i = 0;
	int rc = 0;

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

		/**
		 * if we get a header, then parse it for the order.
		 * if we don't get a header, then find all the tags and put them in some order. that
		 * order will be used each time (note that we could put them in order when we send them
		 * and then just peel them off in order
		 */

		json_entity_t en = json_attr_find(e, STOREHEADER);
		if (en){
			msglog(LDMSD_LDEBUG, PNAME ": getting header from headerline\n");
			rc = _get_header_from_headerline(en);
			if (!rc) goto out;
		} else {
			if (keyarray == NULL){
				msglog(LDMSD_LDEBUG, PNAME ": getting header from keyarray\n");
				rc = _get_header_from_data(e);
				if (!rc) goto out;
			}

			//now write them all out in the same order
			for (i = 0; i < numkeys; i++){
				json_entity_t en = json_attr_find(e, keyarray[i]);
				//how many attr in this entity?
				if (en == NULL){
					msglog(LDMSD_LDEBUG, PNAME ": NULL return from find for key <%s>\n",
					       keyarray[i]);
					//print nothing
				} else {
					if (en->type == JSON_ATTR_VALUE){
						json_attr_t attr = en->value.attr_;
						if (!attr){
							msglog(LDMSD_LERROR, PNAME ": Why is attr null?\n");
						} else {
							fprintf(streamfile, "%s",
								attr->value->value.str_->str);
						}
					} else {
						msglog(LDMSD_LERROR, PNAME ": Bad JSON value type\n");
						//print nothing
					}
				}
				if (i < (numkeys-1)){
					fprintf(streamfile, ",");
				}
			}
			fprintf(streamfile, "\n");
			if (!buffer){
				fflush(streamfile);
				fsync(fileno(streamfile));
			}
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

	size_t pathlen = strlen(root_path) + strlen(schema) + strlen(container) + 8;
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
	sprintf(path, "%s/%s/%s", root_path, container, schema);
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
	for (i = 0; i < numkeys; i++){
		free(keyarray[i]);
	}
	free(keyarray);
	keyarray = NULL;
	numkeys = 0;
	buffer = 0;

	return;
}

static const char *usage(struct ldmsd_plugin *self)
{
	return  "    config name=hello_stream_store path=<path> container=<container> stream=<stream> [buffer=<0/1>] \n"
		"         - Set the root path for the storage of csvs and some default parameters\n"
		"         - path       The path to the root of the csv directory\n"
		"         - container  The directory under the path\n"
		"         - schema     The stream name which will also be the file name\n"
		" 	  - buffer     0 to disable buffering, 1 to enable it with autosize (default)\n"
		;
}



static struct ldmsd_store hello_stream_store = {
	.base = {
			.name = "hello_stream_store",
			.type = LDMSD_PLUGIN_STORE,
			.term = term,
			.config = config,
			.usage = usage,
	},
};

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	msglog = pf;
	return &hello_stream_store.base;
}

static void __attribute__ ((constructor)) hello_stream_store_init();
static void hello_stream_store_init()
{
	pthread_mutex_init(&cfg_lock, NULL);
}

static void __attribute__ ((destructor)) hello_stream_store_fini(void);
static void hello_stream_store_fini()
{
	pthread_mutex_destroy(&cfg_lock);
}
