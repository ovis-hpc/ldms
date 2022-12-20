/**
 * Copyright (c) 2021 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2021 Open Grid Computing, Inc. All rights reserved.
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

#define PNAME "blob_stream_writer"

static ldmsd_msg_log_f msglog;
static pthread_mutex_t cfg_lock;
static int closing;
static char* root_path;
static char* container;
enum writer_state {
	WS_NEW,
	WS_OPEN,
	WS_REOPEN,
	WS_CLOSED,
	WS_ERR
};

typedef struct stream_data {
	/* set at create */
	pthread_mutex_t write_lock;
	enum writer_state ws;
	char* stream_name;
	char* streamfile_name;
	char* offsetfile_name;
	char* timingfile_name;
	char* typefile_name;
	ldmsd_stream_client_t subscription;
	/* set at first write */
	FILE* offsetfile;
	FILE* streamfile;
	FILE* timingfile;
	FILE* typefile;
	long offset;
	LIST_ENTRY(stream_data) entry;
} *stream_data_t;

LIST_HEAD(stream_data_list, stream_data);
static struct stream_data_list data_list;

static void stream_data_open(stream_data_t sd);

static int debug;
static int timing;
static int types;

char blob_stream_char_to_type(char c)
{
	switch (c) {
	case 's':
		return LDMSD_STREAM_STRING;
	case 'j':
		return LDMSD_STREAM_JSON;
		break;
#if 0
	case 'b':
		return LDMSD_STREAM_BINARY;
		break;
#endif
	default:
		return '\0';
	}
}

char blob_stream_type_to_char(ldmsd_stream_type_t stream_type)
{
	switch (stream_type) {
	case LDMSD_STREAM_STRING:
		return 's';
	case LDMSD_STREAM_JSON:
		return 'j';
#if 0
	case LDMSD_STREAM_BINARY:
		return 'b';
#endif
	default:
		msglog(LDMSD_LERROR, PNAME ": unexpected stream type %d\n",
			stream_type);
		return '\0';
	}
}

/* open, if not open or already closed, and write to stream files. */
static int stream_cb(ldmsd_stream_client_t c, void *ctxt,
		     ldmsd_stream_type_t stream_type,
		     const char *msg, size_t msg_len,
		     json_entity_t e)
{
	int rc = 0;
	stream_data_t sd = ctxt;
	if (!sd) {
		msglog(LDMSD_LERROR, PNAME ": stream_cb ctxt is NULL\n");
		return EINVAL;
	}

	pthread_mutex_lock(&sd->write_lock);
	if (sd->ws == WS_NEW) {
		stream_data_open(sd);
	}
	if (sd->ws != WS_OPEN) {
		goto out;
	}
	assert(sd->streamfile != NULL);

	uint64_t le = htole64(sd->offset);
	rc = fwrite(&le, sizeof(uint64_t), 1, sd->offsetfile);
	if ( rc != 1) {
		msglog(LDMSD_LERROR, PNAME ": error writing offset to %s\n",
			sd->offsetfile_name);
	}
	if (debug)
		msglog(LDMSD_LDEBUG, PNAME ": offset=%ld ...\n", sd->offset);

	if (sd->timingfile) {
		struct timeval now;
		gettimeofday(&now, NULL);
		uint64_t tbuf[2];
		tbuf[0] = htole64((uint64_t)now.tv_sec);
		tbuf[1] = htole64((uint64_t)now.tv_usec);
		rc = fwrite(tbuf, sizeof(uint64_t), 2, sd->timingfile);
		if ( rc != 2) {
			msglog(LDMSD_LERROR, PNAME ": error writing time to %s\n",
				sd->timingfile_name);
		}
	}

	if (sd->typefile) {
		char st = blob_stream_type_to_char(stream_type);
		rc = fwrite(&st, 1, 1, sd->typefile);
		if (rc != 1) {
			int ferr = ferror(sd->typefile);
			msglog(LDMSD_LERROR, PNAME ": short type write in %s: %s\n",
				sd->typefile_name, STRERROR(ferr));
		}
	}
	rc = fwrite(msg, 1, msg_len, sd->streamfile);
	sd->offset += rc;
	if (rc != msg_len) {
		int ferr = ferror(sd->streamfile);
		msglog(LDMSD_LERROR, PNAME ": short write starting at %s:%ld: %s\n",
			sd->streamfile_name, sd->offset, STRERROR(ferr));
	}
	if (debug)
		msglog(LDMSD_LDEBUG, PNAME ": msg=%.50s ...\n", msg);

out:
	pthread_mutex_unlock(&sd->write_lock);
	return rc;
}


static stream_data_t stream_data_create(const char *stream)
{
	stream_data_t sd;
	sd = calloc(1, sizeof(*sd));
	if (!sd)
		return NULL;
	sd->stream_name = strdup(stream);
	if (!sd->stream_name) {
		free(sd);
		return NULL;
	}
	pthread_mutex_init(&sd->write_lock, NULL);
	return sd;
}

static void stream_data_open(stream_data_t sd)
{
	if (!sd->streamfile_name || !sd->offsetfile_name) {
		sd->ws = WS_ERR;
		return;
	}
	time_t t = time(NULL);
	size_t end = strlen(sd->offsetfile_name);
	sprintf(sd->offsetfile_name + end, "%ld", (long)t);
	end = strlen(sd->streamfile_name);
	sprintf(sd->streamfile_name + end, "%ld", (long)t);

	sd->offsetfile = fopen_perm(sd->offsetfile_name, "w", 0640);
	sd->streamfile = fopen_perm(sd->streamfile_name, "w", 0640);
	if (!sd->offsetfile || !sd->streamfile) {
		msglog(LDMSD_LERROR, PNAME ": Error '%s' opening the files %s, %s.\n",
		       STRERROR(errno), sd->offsetfile_name, sd->streamfile_name);
		sd->ws = WS_ERR;
		return;
	}
	if (sd->timingfile_name) {
		end = strlen(sd->timingfile_name);
		sprintf(sd->timingfile_name + end, "%ld", (long)t);
		sd->timingfile = fopen_perm(sd->timingfile_name, "w", 0640);
		if (!sd->timingfile) {
			msglog(LDMSD_LERROR, PNAME ": Error '%s' opening the file %s.\n",
			       STRERROR(errno), sd->offsetfile_name, sd->timingfile_name);
			sd->ws = WS_ERR;
			return;
		}
	}
	if (sd->typefile_name) {
		end = strlen(sd->typefile_name);
		sprintf(sd->typefile_name + end, "%ld", (long)t);
		sd->typefile = fopen_perm(sd->typefile_name, "w", 0640);
		if (!sd->typefile) {
			msglog(LDMSD_LERROR, PNAME ": Error '%s' opening the file %s.\n",
			       STRERROR(errno), sd->offsetfile_name, sd->typefile_name);
			sd->ws = WS_ERR;
			return;
		}
	}
	sd->ws = WS_OPEN;
	char magic[] = "bloboff";
	int rc = fwrite(magic, strlen(magic)+1, 1, sd->offsetfile);
	sd->offset += 8;
	if (rc != 1) {
		msglog(LDMSD_LERROR, PNAME ": Error '%s' writing to file %s.\n",
		       STRERROR(errno), sd->offsetfile_name);
		sd->ws = WS_ERR;
		return;
	}
	char magic2[] = "blobdat";
	rc = fwrite(magic2, strlen(magic2)+1, 1, sd->streamfile);
	if (rc != 1) {
		msglog(LDMSD_LERROR, PNAME ": Error '%s' writing to file %s.\n",
		       STRERROR(errno), sd->streamfile_name);
		sd->ws = WS_ERR;
		return;
	}
	if (sd->timingfile) {
		char magic3[] = "blobtim";
		int rc = fwrite(magic3, strlen(magic3)+1, 1, sd->timingfile);
		if (rc != 1) {
			msglog(LDMSD_LERROR, PNAME ": Error '%s' writing to file %s.\n",
			       STRERROR(errno), sd->timingfile_name);
			sd->ws = WS_ERR;
			return;
		}
	}
	if (sd->typefile) {
		char magic4[] = "blobtyp";
		int rc = fwrite(magic4, strlen(magic4)+1, 1, sd->typefile);
		if (rc != 1) {
			msglog(LDMSD_LERROR, PNAME ": Error '%s' writing to file %s.\n",
			       STRERROR(errno), sd->typefile_name);
			sd->ws = WS_ERR;
			return;
		}
	}
}

static void reset_paths(stream_data_t sd)
{
	if (!sd)
		return;
	free(sd->offsetfile_name);
	sd->offsetfile_name = NULL;
	free(sd->streamfile_name);
	sd->streamfile_name = NULL;
	free(sd->timingfile_name);
	sd->timingfile_name = NULL;
	free(sd->typefile_name);
	sd->typefile_name = NULL;
	if (sd->typefile) {
		fclose(sd->typefile);
		sd->typefile = NULL;
	}
	if (sd->streamfile) {
		fclose(sd->streamfile);
		sd->streamfile = NULL;
	}
	if (sd->offsetfile) {
		fclose(sd->offsetfile);
		sd->offsetfile = NULL;
	}
	sd->ws = WS_NEW;
}

/* base directory path name space for:
 * path/container/stream.OFFSET.time()_at_open
 * time substring must be reset at open.
 * write_lock must be held when this is called.
 */
static int set_paths(stream_data_t sd)
{
	if (!sd)
		return EINVAL;
	size_t pathlen = strlen(root_path) + strlen(sd->stream_name)
		+ strlen(container) + 32;
	char dpath[pathlen];
	sprintf(dpath, "%s/%s", root_path, container);
	int rc = f_mkdir_p(dpath, 0750);
	if ((rc != 0) && (errno != EEXIST)) {
		msglog(LDMSD_LERROR, PNAME ": Failure %d creating directory '%s'\n",
			 errno, dpath);
		rc = ENOENT;
		sd->ws = WS_ERR;
		return rc;
	}

	if (timing) {
		sd->timingfile_name = malloc(pathlen);
		if (!sd->timingfile_name) {
			sd->ws = WS_ERR;
			return ENOMEM;
		}
		snprintf(sd->timingfile_name, pathlen, "%s/%s/%s.TIMING.", root_path,
			container, sd->stream_name);
	}

	if (types) {
		sd->typefile_name = malloc(pathlen);
		if (!sd->typefile_name) {
			sd->ws = WS_ERR;
			return ENOMEM;
		}
		snprintf(sd->typefile_name, pathlen, "%s/%s/%s.TYPE.", root_path,
			container, sd->stream_name);
	}

	sd->streamfile_name = malloc(pathlen);
	if (!sd->streamfile_name) {
		sd->ws = WS_ERR;
		return ENOMEM;
	}
	snprintf(sd->streamfile_name, pathlen, "%s/%s/%s.DAT.", root_path,
		container, sd->stream_name);

	sd->offsetfile_name = malloc(pathlen);
	if (!sd->offsetfile_name) {
		sd->ws = WS_ERR;
		return ENOMEM;
	}
	snprintf(sd->offsetfile_name, pathlen, "%s/%s/%s.OFFSET.", root_path,
		container, sd->stream_name);
	if (!sd->subscription) {
		msglog(LDMSD_LDEBUG, PNAME ": subscribing to stream '%s'\n",
			sd->stream_name);
		sd->subscription = ldmsd_stream_subscribe(sd->stream_name,
			stream_cb, sd);
		/* stream dispatch to stream_cb now holds a reference to sd. */
	}
	return 0;
}

static int add_stream(const char *stream)
{
	if (!stream)
		return EINVAL;
	stream_data_t old = NULL;
	stream_data_t sd = NULL;
	LIST_FOREACH(sd, &data_list, entry) {
		pthread_mutex_lock(&sd->write_lock);
		if ( 0 == strcmp(stream, sd->stream_name)) {
			sd->ws = WS_REOPEN;
			old = sd;
			pthread_mutex_unlock(&sd->write_lock);
			break;
		}
		pthread_mutex_unlock(&sd->write_lock);
	}
	if (!old) {
		sd = stream_data_create(stream);
		if (!sd)
			return ENOMEM;
		LIST_INSERT_HEAD(&data_list, sd, entry);
	}
	return 0;
}

/**
 * \brief Configuration
 */
static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char* s;
	int rc;

	if (!self || !avl)
		return EINVAL;
	pthread_mutex_lock(&cfg_lock);
	if (closing) {
		pthread_mutex_unlock(&cfg_lock);
		return EINVAL;
	}

	int i, size = avl->count;
	for (i = 0; i < size; i++) {
		if (strcmp("stream", av_name(avl, i)) == 0) {
			const char *sn = av_value_at_idx(avl, i);
			rc = add_stream(sn);
			if (rc) {
				msglog(LDMSD_LERROR, PNAME ": failed to add"
					" stream %s.\n", sn);
				goto out;
			}
		}
	}
	if (LIST_FIRST(&data_list) == NULL) {
		msglog(LDMSD_LERROR, PNAME ": missing 'stream=...' in config\n");
		rc = EINVAL;
		goto out;
	}


	s = av_value(avl, "debug");
	if (s) {
		debug = 1;
	}

	timing = 0;
	s = av_value(avl, "timing");
	if (s) {
		timing = 1;
	}

	types = 0;
	s = av_value(avl, "types");
	if (s) {
		types = 1;
	}

	s = av_value(avl, "path");
	if (!s) {
		msglog(LDMSD_LERROR, PNAME ": missing path in config\n");
		rc = EINVAL;
		goto out;
	} else {
		root_path = strdup(s);
	}


	s = av_value(avl, "container");
	if (!s){
		msglog(LDMSD_LERROR, PNAME ": missing container in config\n");
		rc = EINVAL;
		goto out;
	} else {
		container = strdup(s);
	}
	if (!container || !root_path) {
		rc = ENOMEM;
		msglog(LDMSD_LERROR, PNAME ": out of memory in config.\n");
		goto out;
	}

	stream_data_t sd = NULL;
	LIST_FOREACH(sd, &data_list, entry) {
		pthread_mutex_lock(&sd->write_lock);
		if (sd->ws == WS_REOPEN) {
			reset_paths(sd);
		}
		if (sd->ws == WS_NEW) {
			rc = set_paths(sd);
			if (rc) {
				msglog(LDMSD_LERROR, PNAME ": config: problem '%s'\n",
					STRERROR(rc));
			}
		}
		pthread_mutex_unlock(&sd->write_lock);
	}

out:
	pthread_mutex_unlock(&cfg_lock);

	return rc;
}

static void stream_data_close( stream_data_t sd )
{
	if (!sd)
		return;
	pthread_mutex_lock(&sd->write_lock);
	if (sd->timingfile) {
		fclose(sd->timingfile);
		sd->timingfile = NULL;
	}
	if (sd->typefile) {
		fclose(sd->typefile);
		sd->typefile = NULL;
	}
	if (sd->streamfile) {
		fclose(sd->streamfile);
		sd->streamfile = NULL;
	}
	if (sd->offsetfile) {
		fclose(sd->offsetfile);
		sd->offsetfile = NULL;
	}
	free(sd->typefile_name);
	sd->typefile_name = NULL;
	free(sd->streamfile_name);
	sd->streamfile_name = NULL;
	free(sd->offsetfile_name);
	sd->offsetfile_name = NULL;
	free(sd->stream_name);
	sd->stream_name = NULL;
	ldmsd_stream_close(sd->subscription);
	/* sd reference is no longer hiding inside cb handler */
	sd->subscription = NULL;
	sd->ws = WS_CLOSED;
	pthread_mutex_unlock(&sd->write_lock);
	pthread_mutex_destroy(&sd->write_lock);
}

static void term(struct ldmsd_plugin *self)
{
	pthread_mutex_lock(&cfg_lock);
	closing = 1;
	stream_data_t sd = LIST_FIRST(&data_list);
	while (sd) {
		stream_data_close(sd);
		LIST_REMOVE(sd, entry);
		free(sd);
		sd = LIST_FIRST(&data_list);
	}
	pthread_mutex_unlock(&cfg_lock);
	free(root_path);
	root_path = NULL;
	free(container);
	container = NULL;

	return;
}

static const char *usage(struct ldmsd_plugin *self)
{
	return  "    config name=blob_stream_writer path=<path> container=<container> stream=<stream> \n"
		"         - Set the root path for the storage of csvs and some default parameters\n"
		"         - path       The path to the root of the csv directory\n"
		"         - container  The directory under the path\n"
		"         - stream     The stream name which will also be the file name\n"
		"                      The stream argument may be repeated.\n"
		;
}

static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	return NULL;
}

static int sample(struct ldmsd_sampler *self)
{
	return 0;
}

static struct ldmsd_sampler blob_stream_writer = {
	.base = {
			.name = "blob_stream_writer",
			.type = LDMSD_PLUGIN_SAMPLER,
			.term = term,
			.config = config,
			.usage = usage,
	},
	.get_set = get_set,
	.sample = sample
};

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	msglog = pf;
	LIST_INIT(&data_list);
	return &blob_stream_writer.base;
}

static void __attribute__ ((constructor)) blob_stream_writer_init();
static void blob_stream_writer_init()
{
	pthread_mutex_init(&cfg_lock, NULL);
}

static void __attribute__ ((destructor)) blob_stream_writer_fini(void);
static void blob_stream_writer_fini()
{
	pthread_mutex_destroy(&cfg_lock);
}
