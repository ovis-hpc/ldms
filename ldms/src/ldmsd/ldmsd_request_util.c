/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2017 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2017 Sandia Corporation. All rights reserved.
 * Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
 * license for use of this work by or on behalf of the U.S. Government.
 * Export of this program may require a license from the United States
 * Government.
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

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <ovis_util/util.h>
#include "ldmsd.h"
#include "ldmsd_request.h"

struct req_str_id {
	const char *str;
	uint32_t id;
};

const struct req_str_id req_str_id_table[] = {
	/* This table need to be sorted by keyword for bsearch() */
	{  "config",             LDMSD_PLUGN_CONFIG_REQ  },
	{  "daemon",             LDMSD_DAEMON_STATUS_REQ  },
	{  "env",                LDMSD_ENV_REQ  },
	{  "exit",               LDMSD_EXIT_DAEMON_REQ  },
	{  "greeting",           LDMSD_GREETING_REQ  },
	{  "include",            LDMSD_INCLUDE_REQ  },
	{  "load",               LDMSD_PLUGN_LOAD_REQ  },
	{  "loglevel",           LDMSD_VERBOSE_REQ  },
	{  "logrotate",          LDMSD_LOGROTATE_REQ  },
	{  "oneshot",            LDMSD_ONESHOT_REQ  },
	{  "plugn_status",       LDMSD_PLUGN_STATUS_REQ  },
	{  "plugn_sets",         LDMSD_PLUGN_SETS_REQ  },
	{  "prdcr_add",          LDMSD_PRDCR_ADD_REQ  },
	{  "prdcr_del",          LDMSD_PRDCR_DEL_REQ  },
	{  "prdcr_set_status",   LDMSD_PRDCR_SET_REQ  },
	{  "prdcr_start",        LDMSD_PRDCR_START_REQ  },
	{  "prdcr_start_regex",  LDMSD_PRDCR_START_REGEX_REQ  },
	{  "prdcr_status",       LDMSD_PRDCR_STATUS_REQ  },
	{  "prdcr_stop",         LDMSD_PRDCR_STOP_REQ  },
	{  "prdcr_stop_regex",   LDMSD_PRDCR_STOP_REGEX_REQ  },
	{  "start",              LDMSD_PLUGN_START_REQ  },
	{  "stop",               LDMSD_PLUGN_STOP_REQ  },
	{  "strgp_add",          LDMSD_STRGP_ADD_REQ  },
	{  "strgp_del",          LDMSD_STRGP_DEL_REQ  },
	{  "strgp_metric_add",   LDMSD_STRGP_METRIC_ADD_REQ  },
	{  "strgp_metric_del",   LDMSD_STRGP_METRIC_DEL_REQ  },
	{  "strgp_prdcr_add",    LDMSD_STRGP_PRDCR_ADD_REQ  },
	{  "strgp_prdcr_del",    LDMSD_STRGP_PRDCR_DEL_REQ  },
	{  "strgp_start",        LDMSD_STRGP_START_REQ  },
	{  "strgp_status",       LDMSD_STRGP_STATUS_REQ  },
	{  "strgp_stop",         LDMSD_STRGP_STOP_REQ  },
	{  "term",               LDMSD_PLUGN_TERM_REQ  },
	{  "udata",              LDMSD_SET_UDATA_REQ  },
	{  "udata_regex",        LDMSD_SET_UDATA_REGEX_REQ  },
	{  "updtr_add",          LDMSD_UPDTR_ADD_REQ  },
	{  "updtr_del",          LDMSD_UPDTR_DEL_REQ  },
	{  "updtr_match_add",    LDMSD_UPDTR_MATCH_ADD_REQ  },
	{  "updtr_match_del",    LDMSD_UPDTR_MATCH_DEL_REQ  },
	{  "updtr_prdcr_add",    LDMSD_UPDTR_PRDCR_ADD_REQ  },
	{  "updtr_prdcr_del",    LDMSD_UPDTR_PRDCR_DEL_REQ  },
	{  "updtr_start",        LDMSD_UPDTR_START_REQ  },
	{  "updtr_status",       LDMSD_UPDTR_STATUS_REQ  },
	{  "updtr_stop",         LDMSD_UPDTR_STOP_REQ  },
	{  "usage",              LDMSD_PLUGN_LIST_REQ  },
	{  "version",            LDMSD_VERSION_REQ  },
};

/* This table need to be sorted by keyword for bsearch() */
const struct req_str_id attr_str_id_table[] = {
	{  "base",              LDMSD_ATTR_BASE  },
	{  "container",         LDMSD_ATTR_CONTAINER  },
	{  "host",              LDMSD_ATTR_HOST  },
	{  "incr",              LDMSD_ATTR_INCREMENT  },
	{  "instance",          LDMSD_ATTR_INSTANCE  },
	{  "interval",          LDMSD_ATTR_INTERVAL  },
	{  "interval_us",       LDMSD_ATTR_INTERVAL  },
	{  "level",             LDMSD_ATTR_LEVEL  },
	{  "match",             LDMSD_ATTR_MATCH  },
	{  "metric",            LDMSD_ATTR_METRIC  },
	{  "name",              LDMSD_ATTR_NAME  },
	{  "offset",            LDMSD_ATTR_OFFSET  },
	{  "path",              LDMSD_ATTR_PATH  },
	{  "perm",              LDMSD_ATTR_PERM  },
	{  "plugin",            LDMSD_ATTR_PLUGIN  },
	{  "port",              LDMSD_ATTR_PORT  },
	{  "producer",          LDMSD_ATTR_PRODUCER  },
	{  "push",              LDMSD_ATTR_PUSH  },
	{  "regex",             LDMSD_ATTR_REGEX  },
	{  "schema",            LDMSD_ATTR_SCHEMA  },
	{  "string",            LDMSD_ATTR_STRING  },
	{  "test",              LDMSD_ATTR_TEST  },
	{  "time",              LDMSD_ATTR_TIME  },
	{  "type",              LDMSD_ATTR_TYPE  },
	{  "udata",             LDMSD_ATTR_UDATA  },
	{  "xprt",              LDMSD_ATTR_XPRT  },
};

uint32_t req_str_id_cmp(const struct req_str_id *a, const struct req_str_id *b)
{
	return strcmp(a->str, b->str);
}

uint32_t ldmsd_req_str2id(const char *verb)
{
	struct req_str_id key = {verb, -1};
	struct req_str_id *x = bsearch(&key, req_str_id_table,
			sizeof(req_str_id_table)/sizeof(*req_str_id_table),
			sizeof(*req_str_id_table), (void *)req_str_id_cmp);
	if (!x)
		return LDMSD_NOTSUPPORT_REQ;
	return x->id;
}

int32_t ldmsd_req_attr_str2id(const char *name)
{
	struct req_str_id key = {name, -1};
	struct req_str_id *x = bsearch(&key, attr_str_id_table,
			sizeof(attr_str_id_table)/sizeof(*attr_str_id_table),
			sizeof(*attr_str_id_table), (void *)req_str_id_cmp);
	if (!x)
		return -1;
	return x->id;
}

/*
 * If both \c name and \c value are NULL, the end attribute is added to req_buf.
 * If \c name is NULL but \c value is not NULL, the attribute of type ATTR_STRING
 * is added to req_buf.
 * If \c name and \c value are not NULL, the attribute of the corresponding type
 * is added to req_buf.
 * Otherwise, EINVAL is returned.
 */
static int add_attr_from_attr_str(char *name, char *value, ldmsd_req_hdr_t *request,
				  size_t *rec_off, size_t *rec_len)
{
	ldmsd_req_attr_t attr;
	size_t attr_sz, val_sz;
	ldmsd_req_hdr_t req = *request;
	char *buf = (char *)*request;
	size_t offset = *rec_off;
	size_t len = *rec_len;

	if (!name && !value) {
		/* Terminate the attribute list */
		memset(&buf[offset], 0, sizeof(uint32_t));
		attr_sz = sizeof(uint32_t);
		goto out;
	}

	if (value)
		val_sz = strlen(value) + 1; /* include '\0' */
	else
		/* keyword */
		val_sz = 0;

	attr_sz = sizeof(struct ldmsd_req_attr_s) + val_sz;
	while (len - offset < attr_sz) {
		buf = realloc(buf, len * 2);
		if (!buf) {
			return ENOMEM;
		}
		*request = req = (ldmsd_req_hdr_t)buf;
		len = len * 2;
	}

	attr = (ldmsd_req_attr_t)&buf[offset];
	attr->discrim = 1;
	attr->attr_len = val_sz;
	if (!name) {
		/* Caller wants the attribute id of ATTR_STRING */
		attr->attr_id = LDMSD_ATTR_STRING;
	} else {
		attr->attr_id = ldmsd_req_attr_str2id(name);
		if ((int)attr->attr_id < 0)
			return EINVAL;
	}

	if (val_sz)
		memcpy(attr->attr_value, value, val_sz);

 out:
	offset += attr_sz;
	(*request)->rec_len += attr_sz;
	*rec_len = len;
	*rec_off = offset;
	return 0;
}

void __get_attr_name_value(char *av, char **name, char **value)
{
	assert((av && name && value) ||
			(NULL == "__get_attr_name_value() invalid parameter"));
	if (!av || !name || !value) {
		if (name)
			*name = NULL;
		if (value)
			*value = NULL;
		return;
	}
	*name = av;
	char *delim;
	delim = strchr(av, '=');
	if (delim) {
		*value = delim;
		**value = '\0';
		(*value)++;
	} else {
		*value = NULL;
	}
}

ldmsd_req_hdr_t ldmsd_parse_config_str(const char *cfg, uint32_t msg_no)
{
	static const char *delim = " \t";
	char *av, *verb, *tmp, *ptr, *name, *value, *dummy;
	int rc;
	ldmsd_req_hdr_t request;
	size_t cfg_len = strlen(cfg);
	size_t rec_off, rec_len;
	dummy = strdup(cfg);
	if (!dummy)
		return NULL;

	verb = dummy;
	/* Get the request id */
	av = strchr(dummy, ' ');
	if (av) {
		*av = '\0';
		av++;
	}

	request = calloc(1, LDMSD_MAX_CONFIG_STR_LEN); /* arbitrary but substantial */
	if (!request)
		goto err;
	request->marker = LDMSD_RECORD_MARKER;
	request->type = LDMSD_REQ_TYPE_CONFIG_CMD;
	request->flags = LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F;
	request->msg_no = msg_no;
	request->req_id = ldmsd_req_str2id(verb);
	if (((int)request->req_id < 0) || (request->req_id == LDMSD_NOTSUPPORT_REQ)) {
		rc = ENOSYS;
		goto err;
	}
	rec_len = LDMSD_MAX_CONFIG_STR_LEN;
	request->rec_len = rec_off = sizeof(*request);
	if (!av)
		goto last_attr;

	if (request->req_id == LDMSD_PLUGN_CONFIG_REQ) {
		size_t len = strlen(av);
		size_t cnt = 0;
		tmp = malloc(len);
		if (!tmp) {
			rc = ENOMEM;
			goto err;
		}
		av = strtok_r(av, delim, &ptr);
		while (av) {
			__get_attr_name_value(av, &name, &value);
			if (!name) {
				/* av is neither attribute value nor keyword */
				rc = EINVAL;
				free(tmp);
				goto err;
			}
			if (0 == strncmp(name, "name", 4)) {
				/* Find the name attribute */
				rc = add_attr_from_attr_str(name, value, &request,
							    &rec_off, &rec_len);
				if (rc) {
					free(tmp);
					goto err;
				}

			} else {
				/* Construct the other attribute into a ATTR_STRING */
				if (value) {
					cnt += snprintf(&tmp[cnt], len - cnt,
							"%s=%s ", name, value);
				} else {
					cnt += snprintf(&tmp[cnt], len - cnt,
							"%s ", name);
				}
			}
			av = strtok_r(NULL, delim, &ptr);
		}
		tmp[cnt-1] = '\0'; /* Replace the last ' ' with '\0' */
		/* Add an attribute of type 'STRING' */
		rc = add_attr_from_attr_str(NULL, tmp, &request, &rec_off, &rec_len);
		free(tmp);
		if (rc)
			goto err;
	} else {
		av = strtok_r(av, delim, &ptr);
		while (av) {
			__get_attr_name_value(av, &name, &value);
			if (!name) {
				/* av is neither attribute value nor keyword */
				rc = EINVAL;
				goto err;
			}
			rc = add_attr_from_attr_str(name, value,
						    &request, &rec_off, &rec_len);
			if (rc)
				goto err;
			av = strtok_r(NULL, delim, &ptr);
		}
	}
last_attr:
	/* Add the end attribute */
	rc = add_attr_from_attr_str(NULL, NULL, &request, &rec_off, &rec_len);
	free(dummy);
	return request;
err:
	free(dummy);
	return NULL;
}

ldmsd_req_attr_t ldmsd_req_attr_get_by_id(char *request, uint32_t attr_id)
{
	ldmsd_req_hdr_t req = (ldmsd_req_hdr_t)request;
	ldmsd_req_attr_t attr = ldmsd_first_attr(req);
	while (attr->discrim) {
		if (attr->attr_id == attr_id) {
			return attr;
		}
		attr = ldmsd_next_attr(attr);
	}
	return NULL;
}

ldmsd_req_attr_t ldmsd_req_attr_get_by_name(char *request, const char *name)
{
	int32_t attr_id = ldmsd_req_attr_str2id(name);
	if (attr_id < 0)
		return NULL;
	return ldmsd_req_attr_get_by_id(request, attr_id);
}

char *ldmsd_req_attr_str_value_get_by_id(char *request, uint32_t attr_id)
{
	ldmsd_req_attr_t attr = ldmsd_req_attr_get_by_id(request, attr_id);
	if (!attr)
		return NULL;
	return str_repl_env_vars((char *)attr->attr_value);
}

int ldmsd_req_attr_keyword_exist_by_id(char *request, uint32_t attr_id)
{
	ldmsd_req_attr_t attr = ldmsd_req_attr_get_by_id(request, attr_id);
	if (attr)
		return 1;
	return 0;
}

char *ldmsd_req_attr_str_value_get_by_name(char *request, const char *name)
{
	int32_t attr_id = ldmsd_req_attr_str2id(name);
	if (attr_id < 0)
		return NULL;
	return ldmsd_req_attr_str_value_get_by_id(request, attr_id);
}

int ldmsd_req_attr_keyword_exist_by_name(char *request, const char *name)
{
	int32_t attr_id = ldmsd_req_attr_str2id(name);
	if (attr_id < 0)
		return -ENOENT;
	return ldmsd_req_attr_keyword_exist_by_id(request, attr_id);
}

void ldmsd_ntoh_req_attr(ldmsd_req_attr_t attr)
{
	attr->attr_id = ntohl(attr->attr_id);
	attr->attr_len = ntohl(attr->attr_len);
	attr->discrim = ntohl(attr->discrim);
}

void ldmsd_ntoh_req_hdr(ldmsd_req_hdr_t req)
{
	req->flags = ntohl(req->flags);
	req->marker = ntohl(req->marker);
	req->msg_no = ntohl(req->msg_no);
	req->rec_len = ntohl(req->rec_len);
	req->req_id = ntohl(req->req_id);
	req->type = ntohl(req->type);
}

/**
 * Expect \c req to contain the whole message
 */
void ldmsd_ntoh_req_msg(ldmsd_req_hdr_t req)
{
	ldmsd_req_attr_t attr;
	ldmsd_ntoh_req_hdr(req);

	if (req->rec_len == sizeof(*req))
		return;

	attr = ldmsd_first_attr(req);
	while (attr && attr->discrim) {
		ldmsd_ntoh_req_attr(attr);
		attr = ldmsd_next_attr(attr);
	}
}

void ldmsd_hton_req_hdr(ldmsd_req_hdr_t req)
{
	req->flags = htonl(req->flags);
	req->marker = htonl(req->marker);
	req->msg_no = htonl(req->msg_no);
	req->rec_len = htonl(req->rec_len);
	req->req_id = htonl(req->req_id);
	req->type = htonl(req->type);
}

void ldmsd_hton_req_attr(ldmsd_req_attr_t attr)
{
	attr->attr_id = htonl(attr->attr_id);
	attr->attr_len = htonl(attr->attr_len);
	attr->discrim = htonl(attr->discrim);
}

void ldmsd_hton_req_msg(ldmsd_req_hdr_t resp)
{
	ldmsd_req_attr_t attr, next_attr;
	ldmsd_hton_req_hdr(resp);

	if (ntohl(resp->rec_len) == sizeof(*resp))
		return;

	attr = ldmsd_first_attr(resp);
	while (attr && attr->discrim) {
		next_attr = ldmsd_next_attr(attr);
		ldmsd_hton_req_attr(attr);
		attr = next_attr;
	}
}
