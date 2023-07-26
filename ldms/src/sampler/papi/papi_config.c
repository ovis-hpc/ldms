/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2019 Open Grid Computing, Inc. All rights reserved.
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
#include <sys/errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <papi.h>
#include <ovis_json/ovis_json.h>
#include  "../sampler_base.h"
#include "papi_sampler.h"

int papi_process_config_data(job_data_t job, char *buf, size_t buflen,
						    ovis_log_t logger)
{
	json_parser_t p = NULL;
	json_entity_t e = NULL;
	json_entity_t schema_attr;
	json_entity_t events_attr;
	json_entity_t events_list;
	int rc;

	p = json_parser_new(0);
	if (!p) {
		ovis_log(logger, OVIS_LERROR,
		       "Parser could not be created.\n");
		rc = ENOMEM;
		goto out;
	}

	rc = json_parse_buffer(p, buf, buflen, &e);
	if (rc) {
		ovis_log(logger, OVIS_LERROR,
		       "configuration file syntax error.\n");
		goto out;
	}

	schema_attr = json_attr_find(e, "schema");
	if (!schema_attr) {
		ovis_log(logger, OVIS_LERROR,
		       "The configuration file is missing the "
		       "'schema' attribute.\n");
		rc = ENOENT;
		goto out;
	}
	events_attr = json_attr_find(e, "events");
	if (!events_attr) {
		ovis_log(logger, OVIS_LERROR,
		       "The configuration file is missing the "
		       "'events' attribute.\n");
		rc = ENOENT;
		goto out;
	}
	if (JSON_STRING_VALUE != json_entity_type(json_attr_value(schema_attr))) {
		ovis_log(logger, OVIS_LERROR,
		       "The 'schema' attribute must be a string.\n");
		rc = EINVAL;
		goto out;
	}
	events_list = json_attr_value(events_attr);
	if (JSON_LIST_VALUE != json_entity_type(events_list)) {
		ovis_log(logger, OVIS_LERROR,
		       "The 'events' attribute must be a list.\n");
		rc = EINVAL;
		goto out;
	}

	json_str_t str = json_value_str(json_attr_value(schema_attr));
	job->schema_name = strdup(str->str);
	if (!job->schema_name) {
		ovis_log(logger, OVIS_LERROR,
		       "Error duplicating schema name string.\n");
		rc = ENOMEM;
		goto out;
	}

	json_entity_t event_name;
	int event_code;
	char *event_str;
	papi_event_t ev;
	TAILQ_INIT(&job->event_list);
	for (event_name = json_item_first(events_list); event_name;
	     event_name = json_item_next(event_name)) {
		if (JSON_STRING_VALUE != json_entity_type(event_name)) {
			ovis_log(logger, OVIS_LERROR,
			       "Event names must be strings.\n");
			goto out;
		}
		event_str = json_value_str(event_name)->str;
		rc = PAPI_event_name_to_code(event_str, &event_code);
		if (rc) {
			ovis_log(logger, OVIS_LERROR, "PAPI error '%s' "
			       "translating event code '%s'\n",
			       PAPI_strerror(rc), event_str);
			goto out;
		}
		rc = PAPI_query_event(event_code);
		if (rc != PAPI_OK) {
			ovis_log(logger, OVIS_LERROR, "PAPI error '%s' "
			       "query event '%s'\n",
			       PAPI_strerror(rc), event_str);
			goto out;
		}
		ev = malloc(sizeof *ev);
		if (!ev) {
			ovis_log(logger, OVIS_LERROR, "papi_sampler[%d]: Memory "
			       "allocation failure.\n", __LINE__);
			goto out;
		}
		rc = PAPI_get_event_info(event_code, &ev->event_info);
		if (rc != PAPI_OK) {
			ovis_log(logger, OVIS_LERROR, "PAPI error '%s' "
			       "getting event info '%s'\n",
			       PAPI_strerror(rc), event_str);
			goto out;
		}
		ev->event_code = event_code;
		ev->event_name = strdup(event_str);
		if (!ev->event_name) {
			ovis_log(logger, OVIS_LERROR, "papi_sampler[%d]: Memory "
			       "allocation failure.\n", __LINE__);
			goto out;
		}
		TAILQ_INSERT_TAIL(&job->event_list, ev, entry);
	}
 out:
	if (e)
		json_entity_free(e);
	if (p)
		json_parser_free(p);
	return rc;
}

/**
 * Process the configuration file:
 *
 * \param path The path to the configuration file
 * \param schema_ptr Pointer to string to receive schema name
 * \param event_set Pointer to EventSet
 * \retval 0 - success
 * \retval errno
 */
int papi_process_config_file(job_data_t job, const char *path, ovis_log_t logger)
{
	struct stat statbuf;
	char *buf;
	FILE *fp;
	int rc = stat(path, &statbuf);
	if (rc)
		return errno;

	buf = malloc(statbuf.st_size+2);
	if (!buf) {
		ovis_log(logger, OVIS_LERROR,
		       "configuration file of size %zd is too large\n",
		       statbuf.st_size);
		return ENOMEM;
	}
	fp = fopen(path, "r");
	rc = errno;
	if (!fp) {
		ovis_log(logger, OVIS_LERROR,
		       "Error %d opening configuration file '%s'\n",
		       errno, path);
		goto out;
	}
	rc = fread(buf, 1, statbuf.st_size, fp);
	fclose(fp);
	if (rc <= 0) {
		rc = errno;
		ovis_log(logger, OVIS_LERROR,
		       "Error %d reading configuration file '%s'\n",
		       rc, path);
		goto out;
	}
	buf[rc] = '\0';
	buf[rc+1] = '\0';
	rc = papi_process_config_data(job, buf, rc, logger);
 out:
	free(buf);
	return rc;
}

