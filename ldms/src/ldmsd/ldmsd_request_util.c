/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2017-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2017-2018 Open Grid Computing, Inc. All rights reserved.
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
	{  "auth_add",           LDMSD_AUTH_ADD_REQ  },
	{  "auth_del",           LDMSD_AUTH_DEL_REQ  },
	{  "config",             LDMSD_PLUGN_CONFIG_REQ  },
	{  "daemon",             LDMSD_DAEMON_STATUS_REQ  },
	{  "daemon_exit",        LDMSD_EXIT_DAEMON_REQ  },
	{  "daemon_status",      LDMSD_DAEMON_STATUS_REQ  },
	{  "env",                LDMSD_ENV_REQ  },
	{  "exit",               LDMSD_EXIT_DAEMON_REQ  },
	{  "failover_config",    LDMSD_FAILOVER_CONFIG_REQ  },
	{  "failover_peercfg_start", LDMSD_FAILOVER_PEERCFG_START_REQ  },
	{  "failover_peercfg_stop",  LDMSD_FAILOVER_PEERCFG_STOP_REQ  },
	{  "failover_start",     LDMSD_FAILOVER_START_REQ  },
	{  "failover_status",    LDMSD_FAILOVER_STATUS_REQ  },
	{  "failover_stop",      LDMSD_FAILOVER_STOP_REQ  },
	{  "greeting",           LDMSD_GREETING_REQ  },
	{  "include",            LDMSD_INCLUDE_REQ  },
	{  "listen",             LDMSD_LISTEN_REQ },
	{  "load",               LDMSD_PLUGN_LOAD_REQ  },
	{  "loglevel",           LDMSD_VERBOSE_REQ  },
	{  "logrotate",          LDMSD_LOGROTATE_REQ  },
	{  "metric_sets_default_authz", LDMSD_SET_DEFAULT_AUTHZ_REQ  },
	{  "oneshot",            LDMSD_ONESHOT_REQ  },
	{  "option",             LDMSD_CMDLINE_OPTIONS_SET_REQ  },
	{  "plugn_sets",         LDMSD_PLUGN_SETS_REQ  },
	{  "plugn_status",       LDMSD_PLUGN_STATUS_REQ  },
	{  "prdcr_add",          LDMSD_PRDCR_ADD_REQ  },
	{  "prdcr_del",          LDMSD_PRDCR_DEL_REQ  },
	{  "prdcr_hint_tree",    LDMSD_PRDCR_HINT_TREE_REQ  },
	{  "prdcr_set_status",   LDMSD_PRDCR_SET_REQ  },
	{  "prdcr_start",        LDMSD_PRDCR_START_REQ  },
	{  "prdcr_start_regex",  LDMSD_PRDCR_START_REGEX_REQ  },
	{  "prdcr_stats",        LDMSD_PRDCR_STATS_REQ  },
	{  "prdcr_status",       LDMSD_PRDCR_STATUS_REQ  },
	{  "prdcr_stop",         LDMSD_PRDCR_STOP_REQ  },
	{  "prdcr_stop_regex",   LDMSD_PRDCR_STOP_REGEX_REQ  },
	{  "prdcr_stream_dir",   LDMSD_PRDCR_STREAM_DIR_REQ  },
	{  "prdcr_subscribe",    LDMSD_PRDCR_SUBSCRIBE_REQ },
	{  "prdcr_unsubscribe",  LDMSD_PRDCR_UNSUBSCRIBE_REQ },
	{  "set_route",          LDMSD_SET_ROUTE_REQ  },
	{  "set_stats",          LDMSD_SET_STATS_REQ  },
	{  "setgroup_add",       LDMSD_SETGROUP_ADD_REQ  },
	{  "setgroup_del",       LDMSD_SETGROUP_DEL_REQ  },
	{  "setgroup_ins",       LDMSD_SETGROUP_INS_REQ  },
	{  "setgroup_rm",        LDMSD_SETGROUP_RM_REQ  },
	{  "start",              LDMSD_PLUGN_START_REQ  },
	{  "stop",               LDMSD_PLUGN_STOP_REQ  },
	{  "stream_client_dump", LDMSD_STREAM_CLIENT_DUMP_REQ  },
	{  "stream_dir",         LDMSD_STREAM_DIR_REQ  },
	{  "strgp_add",          LDMSD_STRGP_ADD_REQ  },
	{  "strgp_del",          LDMSD_STRGP_DEL_REQ  },
	{  "strgp_metric_add",   LDMSD_STRGP_METRIC_ADD_REQ  },
	{  "strgp_metric_del",   LDMSD_STRGP_METRIC_DEL_REQ  },
	{  "strgp_prdcr_add",    LDMSD_STRGP_PRDCR_ADD_REQ  },
	{  "strgp_prdcr_del",    LDMSD_STRGP_PRDCR_DEL_REQ  },
	{  "strgp_start",        LDMSD_STRGP_START_REQ  },
	{  "strgp_status",       LDMSD_STRGP_STATUS_REQ  },
	{  "strgp_stop",         LDMSD_STRGP_STOP_REQ  },
	{  "subscribe",          LDMSD_STREAM_SUBSCRIBE_REQ  },
	{  "term",               LDMSD_PLUGN_TERM_REQ  },
	{  "thread_stats",       LDMSD_THREAD_STATS_REQ  },
	{  "udata",              LDMSD_SET_UDATA_REQ  },
	{  "udata_regex",        LDMSD_SET_UDATA_REGEX_REQ  },
	{  "update_time_stats",  LDMSD_UPDATE_TIME_STATS_REQ  },
	{  "updtr_add",          LDMSD_UPDTR_ADD_REQ  },
	{  "updtr_del",          LDMSD_UPDTR_DEL_REQ  },
	{  "updtr_match_add",    LDMSD_UPDTR_MATCH_ADD_REQ  },
	{  "updtr_match_del",    LDMSD_UPDTR_MATCH_DEL_REQ  },
	{  "updtr_match_list",   LDMSD_UPDTR_MATCH_LIST_REQ },
	{  "updtr_prdcr_add",    LDMSD_UPDTR_PRDCR_ADD_REQ  },
	{  "updtr_prdcr_del",    LDMSD_UPDTR_PRDCR_DEL_REQ  },
	{  "updtr_start",        LDMSD_UPDTR_START_REQ  },
	{  "updtr_status",       LDMSD_UPDTR_STATUS_REQ  },
	{  "updtr_stop",         LDMSD_UPDTR_STOP_REQ  },
	{  "updtr_task",         LDMSD_UPDTR_TASK_REQ  },
	{  "usage",              LDMSD_PLUGN_LIST_REQ  },
	{  "version",            LDMSD_VERSION_REQ  },
	{  "xprt_stats",         LDMSD_XPRT_STATS_REQ  },
};

/* This table need to be sorted by keyword for bsearch() */
const struct req_str_id attr_str_id_table[] = {
	{  "auth",              LDMSD_ATTR_AUTH  },
	{  "auto_interval",     LDMSD_ATTR_AUTO_INTERVAL  },
	{  "auto_switch",       LDMSD_ATTR_AUTO_SWITCH  },
	{  "base",              LDMSD_ATTR_BASE  },
	{  "container",         LDMSD_ATTR_CONTAINER  },
	{  "decomposition",     LDMSD_ATTR_DECOMP  },
	{  "flush",		LDMSD_ATTR_INTERVAL },
	{  "gid",               LDMSD_ATTR_GID  },
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
	{  "peer_name",         LDMSD_ATTR_PEER_NAME },
	{  "perm",              LDMSD_ATTR_PERM  },
	{  "plugin",            LDMSD_ATTR_PLUGIN  },
	{  "port",              LDMSD_ATTR_PORT  },
	{  "producer",          LDMSD_ATTR_PRODUCER  },
	{  "push",              LDMSD_ATTR_PUSH  },
	{  "regex",             LDMSD_ATTR_REGEX  },
	{  "schema",            LDMSD_ATTR_SCHEMA  },
	{  "stream",            LDMSD_ATTR_STREAM  },
	{  "string",            LDMSD_ATTR_STRING  },
	{  "test",              LDMSD_ATTR_TEST  },
	{  "time",              LDMSD_ATTR_TIME  },
	{  "timeout_factor",    LDMSD_ATTR_TIMEOUT_FACTOR  },
	{  "type",              LDMSD_ATTR_TYPE  },
	{  "udata",             LDMSD_ATTR_UDATA  },
	{  "uid",               LDMSD_ATTR_UID  },
	{  "xprt",              LDMSD_ATTR_XPRT  },
};

int32_t req_str_id_cmp(const struct req_str_id *a, const struct req_str_id *b)
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

const char *ldmsd_req_id2str(enum ldmsd_request req_id)
{
	switch (req_id) {
	case LDMSD_EXAMPLE_REQ  : return "EXAMPLE_REQ";
	case LDMSD_GREETING_REQ : return "GREETING_REQ";

	case LDMSD_PRDCR_ADD_REQ         : return "PRDCR_ADD_REQ";
	case LDMSD_PRDCR_DEL_REQ         : return "PRDCR_DEL_REQ";
	case LDMSD_PRDCR_START_REQ       : return "PRDCR_START_REQ";
	case LDMSD_PRDCR_STOP_REQ        : return "PRDCR_STOP_REQ";
	case LDMSD_PRDCR_STATUS_REQ      : return "PRDCR_STATUS_REQ";
	case LDMSD_PRDCR_START_REGEX_REQ : return "PRDCR_START_REGEX_REQ";
	case LDMSD_PRDCR_STOP_REGEX_REQ  : return "PRDCR_STOP_REGEX_REQ";
	case LDMSD_PRDCR_SET_REQ         : return "PRDCR_SET_REQ";
	case LDMSD_PRDCR_HINT_TREE_REQ   : return "PRDCR_HINT_TREE_REQ";
	case LDMSD_PRDCR_SUBSCRIBE_REQ   : return "PRDCR_SUBSCRIBE_REQ";
	case LDMSD_PRDCR_UNSUBSCRIBE_REQ : return "PRDCR_UNSUBSCRIBE_REQ";

	case LDMSD_STRGP_ADD_REQ        : return "STRGP_ADD_REQ";
	case LDMSD_STRGP_DEL_REQ        : return "STRGP_DEL_REQ";
	case LDMSD_STRGP_START_REQ      : return "STRGP_START_REQ";
	case LDMSD_STRGP_STOP_REQ       : return "STRGP_STOP_REQ";
	case LDMSD_STRGP_STATUS_REQ     : return "STRGP_STATUS_REQ";
	case LDMSD_STRGP_PRDCR_ADD_REQ  : return "STRGP_PRDCR_ADD_REQ";
	case LDMSD_STRGP_PRDCR_DEL_REQ  : return "STRGP_PRDCR_DEL_REQ";
	case LDMSD_STRGP_METRIC_ADD_REQ : return "STRGP_METRIC_ADD_REQ";
	case LDMSD_STRGP_METRIC_DEL_REQ : return "STRGP_METRIC_DEL_REQ";

	case LDMSD_UPDTR_ADD_REQ        : return "UPDTR_ADD_REQ";
	case LDMSD_UPDTR_DEL_REQ        : return "UPDTR_DEL_REQ";
	case LDMSD_UPDTR_START_REQ      : return "UPDTR_START_REQ";
	case LDMSD_UPDTR_STOP_REQ       : return "UPDTR_STOP_REQ";
	case LDMSD_UPDTR_STATUS_REQ     : return "UPDTR_STATUS_REQ";
	case LDMSD_UPDTR_PRDCR_ADD_REQ  : return "UPDTR_PRDCR_ADD_REQ";
	case LDMSD_UPDTR_PRDCR_DEL_REQ  : return "UPDTR_PRDCR_DEL_REQ";
	case LDMSD_UPDTR_MATCH_ADD_REQ  : return "UPDTR_MATCH_ADD_REQ";
	case LDMSD_UPDTR_MATCH_DEL_REQ  : return "UPDTR_MATCH_DEL_REQ";
	case LDMSD_UPDTR_MATCH_LIST_REQ : return "UPDTR_MATCH_LIST_REQ";
	case LDMSD_UPDTR_TASK_REQ       : return "UPDTR_TASK_REQ";

	case LDMSD_SMPLR_ADD_REQ   : return "SMPLR_ADD_REQ";
	case LDMSD_SMPLR_DEL_REQ   : return "SMPLR_DEL_REQ";
	case LDMSD_SMPLR_START_REQ : return "SMPLR_START_REQ";
	case LDMSD_SMPLR_STOP_REQ  : return "SMPLR_STOP_REQ";

	case LDMSD_PLUGN_ADD_REQ    : return "PLUGN_ADD_REQ";
	case LDMSD_PLUGN_DEL_REQ    : return "PLUGN_DEL_REQ";
	case LDMSD_PLUGN_START_REQ  : return "PLUGN_START_REQ";
	case LDMSD_PLUGN_STOP_REQ   : return "PLUGN_STOP_REQ";
	case LDMSD_PLUGN_STATUS_REQ : return "PLUGN_STATUS_REQ";
	case LDMSD_PLUGN_LOAD_REQ   : return "PLUGN_LOAD_REQ";
	case LDMSD_PLUGN_TERM_REQ   : return "PLUGN_TERM_REQ";
	case LDMSD_PLUGN_CONFIG_REQ : return "PLUGN_CONFIG_REQ";
	case LDMSD_PLUGN_LIST_REQ   : return "PLUGN_LIST_REQ";
	case LDMSD_PLUGN_SETS_REQ   : return "PLUGN_SETS_REQ";

	case LDMSD_SET_UDATA_REQ         : return "SET_UDATA_REQ";
	case LDMSD_SET_UDATA_REGEX_REQ   : return "SET_UDATA_REGEX_REQ";
	case LDMSD_VERBOSE_REQ           : return "VERBOSE_REQ";
	case LDMSD_DAEMON_STATUS_REQ     : return "DAEMON_STATUS_REQ";
	case LDMSD_VERSION_REQ           : return "VERSION_REQ";
	case LDMSD_ENV_REQ               : return "ENV_REQ";
	case LDMSD_INCLUDE_REQ           : return "INCLUDE_REQ";
	case LDMSD_ONESHOT_REQ           : return "ONESHOT_REQ";
	case LDMSD_LOGROTATE_REQ         : return "LOGROTATE_REQ";
	case LDMSD_EXIT_DAEMON_REQ       : return "EXIT_DAEMON_REQ";
	case LDMSD_RECORD_LEN_ADVICE_REQ : return "RECORD_LEN_ADVICE_REQ";
	case LDMSD_SET_ROUTE_REQ         : return "SET_ROUTE_REQ";
	case LDMSD_CMDLINE_OPTIONS_SET_REQ : return "CMDLINE_OPTION_SET_REQ";

	/* failover requests by user */
	case LDMSD_FAILOVER_CONFIG_REQ        : return "FAILOVER_CONFIG_REQ";
	case LDMSD_FAILOVER_PEERCFG_START_REQ : return "FAILOVER_PEERCFG_START_REQ";
	case LDMSD_FAILOVER_PEERCFG_STOP_REQ  : return "FAILOVER_PEERCFG_STOP_REQ";
	case LDMSD_FAILOVER_MOD_REQ           : return "FAILOVER_MOD_REQ";
	case LDMSD_FAILOVER_STATUS_REQ        : return "FAILOVER_STATUS_REQ";

	/* failover requests by ldmsd (not exposed to users) */
	case LDMSD_FAILOVER_PAIR_REQ      : return "FAILOVER_PAIR_REQ";
	case LDMSD_FAILOVER_RESET_REQ     : return "FAILOVER_RESET_REQ";
	case LDMSD_FAILOVER_CFGPRDCR_REQ  : return "FAILOVER_CFGPRDCR_REQ";
	case LDMSD_FAILOVER_CFGUPDTR_REQ  : return "FAILOVER_CFGUPDTR_REQ";
	case LDMSD_FAILOVER_CFGSTRGP_REQ  : return "FAILOVER_CFGSTRGP_REQ";
	case LDMSD_FAILOVER_PING_REQ      : return "FAILOVER_PING_REQ";
	case LDMSD_FAILOVER_PEERCFG_REQ   : return "FAILOVER_PEERCFG_REQ";

	/* additional failover requests by user */
	case LDMSD_FAILOVER_START_REQ : return "FAILOVER_START_REQ";
	case LDMSD_FAILOVER_STOP_REQ  : return "FAILOVER_STOP_REQ";

	/* Set Group Requests */
	case LDMSD_SETGROUP_ADD_REQ : return "SETGROUP_ADD_REQ";
	case LDMSD_SETGROUP_MOD_REQ : return "SETGROUP_MOD_REQ";
	case LDMSD_SETGROUP_DEL_REQ : return "SETGROUP_DEL_REQ";
	case LDMSD_SETGROUP_INS_REQ : return "SETGROUP_INS_REQ";
	case LDMSD_SETGROUP_RM_REQ  : return "SETGROUP_RM_REQ";

	case LDMSD_STREAM_SUBSCRIBE_REQ : return "STREAM_SUBSCRIBE_REQ";
	case LDMSD_STREAM_PUBLISH_REQ : return "STREAM_PUBLISH_REQ";
	case LDMSD_STREAM_NEW_REQ : return "STREAM_NEW_REQ";
	case LDMSD_STREAM_DIR_REQ : return "STREAM_DIR_REQ";
	default: return "UNKNOWN_REQ";
	}
}

/*
 * If both \c name and \c value are NULL, the end attribute is added to req_buf.
 * If \c name is NULL but \c value is not NULL, the attribute of type ATTR_STRING
 * is added to req_buf.
 * If \c name and \c value are not NULL, the attribute of the corresponding type
 * is added to req_buf.
 * Otherwise, EINVAL is returned.
 *
 * \c request_sz is the total size of the allocated memory for \c request including
 * the data. If appending an attribute makes \c request->rec_len larger than
 * \c request_sz, ENOMEM is returned.
 */
static int add_attr_from_attr_str(const char *name, const char *value,
				  ldmsd_req_hdr_t *request, size_t *_req_sz,
				  ldmsd_msg_log_f msglog)
{
	ldmsd_req_attr_t attr;
	size_t attr_sz, val_sz;
	ldmsd_req_hdr_t req = *request;
	char *buf = (char *)*request;
	size_t req_sz = *_req_sz;
	int is_terminating = 0;

	/* Calculating the attribute size */
	if (!name && !value) {
		/* Terminate the attribute list */
		value = NULL;
		val_sz = 0;
		attr_sz = sizeof(uint32_t);
		is_terminating = 1;
	} else {
		if (value)
			val_sz = strlen(value) + 1; /* include '\0' */
		else
			/* keyword */
			val_sz = 0;
		attr_sz = sizeof(struct ldmsd_req_attr_s) + val_sz;
	}

	/* Make sure that the buffer is large enough */
	while (req_sz - req->rec_len < attr_sz) {
		char *tmp = realloc(buf, req_sz * 2);
		if (!tmp) {
			msglog(LDMSD_LERROR, "Out of memory\n", name);
			return ENOMEM;
		}
		buf = tmp;
		*request = req = (ldmsd_req_hdr_t)buf;
		req_sz = req_sz * 2;
	}

	if (!is_terminating) {
		attr = (ldmsd_req_attr_t)&buf[req->rec_len];
		attr->discrim = 1;
		attr->attr_len = val_sz;
		if (!name) {
			/* Caller wants the attribute id of ATTR_STRING */
			attr->attr_id = LDMSD_ATTR_STRING;
		} else {
			attr->attr_id = ldmsd_req_attr_str2id(name);
			if ((int)attr->attr_id < 0) {
				msglog(LDMSD_LERROR, "Invalid attribute: %s\n", name);
				return EINVAL;
			}
		}
		if (val_sz)
			memcpy(attr->attr_value, value, val_sz);
	} else {
		memset(&buf[req->rec_len], 0, sizeof(uint32_t));
	}

	(*request)->rec_len += attr_sz;
	*_req_sz = req_sz;
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

static const char *__ldmsd_cfg_delim = " \t";
struct ldmsd_parse_ctxt {
	ldmsd_req_hdr_t request;
	size_t request_sz;
	char *av;
	int line_no;
	uint32_t msg_no;
	ldmsd_msg_log_f msglog;
};

#define LDMSD_REQ_ARRAY_CARD_INIT 5

int __ldmsd_parse_generic(struct ldmsd_parse_ctxt *ctxt)
{
	int rc;
	char *ptr, *name, *value, *dummy;
	char *av = ctxt->av;
	dummy = NULL;
	av = strtok_r(av, __ldmsd_cfg_delim, &ptr);
	while (av) {
		dummy = strdup(av); /* preserve the original string if we need to create multiple records */
		if (!dummy) {
			rc = ENOMEM;
			goto out;
		}
		__get_attr_name_value(dummy, &name, &value);
		if (!name) {
			/* av is neither attribute value nor keyword */
			rc = EINVAL;
			goto out;
		}
		rc = add_attr_from_attr_str(name, value,
					    &ctxt->request,
					    &ctxt->request_sz,
					    ctxt->msglog);
		if (rc)
			goto out;
		av = strtok_r(NULL, __ldmsd_cfg_delim, &ptr);
		free(dummy);
		dummy = NULL;
	}
	rc = 0;
out:
	ctxt->av = av;
	if (dummy)
		free(dummy);
	return rc;
}

int __ldmsd_parse_plugin_config(struct ldmsd_parse_ctxt *ctxt)
{
	char *av = ctxt->av;
	size_t len = strlen(av);
	size_t cnt = 0;
	char *tmp, *name, *value, *ptr, *dummy;
	int rc;
	dummy = NULL;
	tmp = malloc(len);
	if (!tmp) {
		rc = ENOMEM;
		goto out;
	}
	av = strtok_r(av, __ldmsd_cfg_delim, &ptr);
	while (av) {
		ctxt->av = av;
		dummy = strdup(av);
		if (!dummy) {
			rc = ENOMEM;
			goto out;
		}
		__get_attr_name_value(dummy, &name, &value);
		if (!name) {
			/* av is neither attribute value nor keyword */
			rc = EINVAL;
			goto out;
		}
		if (0 == strncmp(name, "name", 4)) {
			/* Find the name attribute */
			rc = add_attr_from_attr_str(name, value,
						    &ctxt->request,
						    &ctxt->request_sz,
						    ctxt->msglog);
			if (rc)
				goto out;
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
		av = strtok_r(NULL, __ldmsd_cfg_delim, &ptr);
		free(dummy);
		dummy = NULL;
	}
	tmp[cnt-1] = '\0'; /* Replace the last ' ' with '\0' */
	/* Add an attribute of type 'STRING' */
	rc = add_attr_from_attr_str(NULL, tmp,
				    &ctxt->request,
				    &ctxt->request_sz,
				    ctxt->msglog);
out:
	if (tmp)
		free(tmp);
	if (dummy)
		free(dummy);
	return rc;
}

int __ldmsd_parse_env(struct ldmsd_parse_ctxt *ctxt)
{
	char *av = ctxt->av;
	size_t len = strlen(av) + 1;
	size_t cnt = 0;
	char *tmp, *name, *value, *ptr, *dummy;
	int rc;
	dummy = NULL;
	tmp = malloc(len);
	if (!tmp) {
		rc = ENOMEM;
		goto out;
	}
	av = strtok_r(av, __ldmsd_cfg_delim, &ptr);
	while (av) {
		ctxt->av = av;
		dummy = strdup(av);
		if (!dummy) {
			rc = ENOMEM;
			goto out;
		}
		__get_attr_name_value(dummy, &name, &value);
		if (!name) {
			/* av is neither attribute value nor keyword */
			rc = EINVAL;
			goto out;
		}
		/* Construct the other attribute into a ATTR_STRING */
		if (value) {
			cnt += snprintf(&tmp[cnt], len - cnt,
					"%s=%s ", name, value);
		} else {
			cnt += snprintf(&tmp[cnt], len - cnt,
					"%s ", name);
		}
		av = strtok_r(NULL, __ldmsd_cfg_delim, &ptr);
		free(dummy);
		dummy = NULL;
	}
	tmp[cnt-1] = '\0'; /* Replace the last ' ' with '\0' */
	/* Add an attribute of type 'STRING' */
	rc = add_attr_from_attr_str(NULL, tmp,
				    &ctxt->request,
				    &ctxt->request_sz,
				    ctxt->msglog);
out:
	if (tmp)
		free(tmp);
	if (dummy)
		free(dummy);
	return rc;
}

int __parse_xprt_endpoint(struct ldmsd_parse_ctxt *ctxt,
		const char *name, const char *value,
		char *buf, size_t buflen, size_t *bufcnt)
{
	int rc = 0;
	size_t cnt = *bufcnt;
	if ((0 == strncmp(name, "xprt", 4)) ||
		(0 == strncmp(name, "port", 4)) ||
		(0 == strncmp(name, "host", 4)) ||
		(0 == strncmp(name, "auth", 4))) {
		/* xprt, port, host, auth */
		rc = add_attr_from_attr_str(name, value,
					    &ctxt->request,
					    &ctxt->request_sz,
					    ctxt->msglog);
		if (rc)
			goto out;
	} else {
		/* Construct the auth attributes into a ATTR_STRING */
		if (value) {
			cnt += snprintf(&buf[cnt], buflen - cnt,
					"%s=%s ", name, value);
		} else {
			cnt += snprintf(&buf[cnt], buflen - cnt,
					"%s ", name);
		}
	}
	*bufcnt = cnt;
out:
	return rc;
}

int __ldmsd_parse_listen_req(struct ldmsd_parse_ctxt *ctxt)
{
	char *av = ctxt->av;
	size_t len = strlen(av);
	size_t cnt = 0;
	char *tmp, *name, *value, *ptr, *dummy;
	int rc = 0;
	dummy = NULL;
	tmp = malloc(len);
	if (!tmp) {
		rc = ENOMEM;
		goto out;
	}
	av = strtok_r(av, __ldmsd_cfg_delim, &ptr);
	while (av) {
		ctxt->av = av;
		dummy = strdup(av);
		if (!dummy) {
			rc = ENOMEM;
			goto out;
		}
		__get_attr_name_value(dummy, &name, &value);
		if (!name) {
			/* av is neither attribute value nor keyword */
			rc = EINVAL;
			goto out;
		}
		rc = __parse_xprt_endpoint(ctxt, name, value, tmp, len, &cnt);
		if (rc)
			goto out;
		av = strtok_r(NULL, __ldmsd_cfg_delim, &ptr);
		free(dummy);
		dummy = NULL;
	}

	if (cnt) {
		tmp[cnt-1] = '\0'; /* Replace the last ' ' with '\0' */
		/* Add an attribute of type 'STRING' */
		rc = add_attr_from_attr_str(NULL, tmp,
					    &ctxt->request,
					    &ctxt->request_sz,
					    ctxt->msglog);
	}

out:
	if (tmp)
		free(tmp);
	if (dummy)
		free(dummy);
	return rc;
}

int __ldmsd_parse_auth_add_req(struct ldmsd_parse_ctxt *ctxt)
{
	char *av = ctxt->av;
	size_t len = strlen(av);
	size_t cnt = 0;
	char *tmp, *name, *value, *ptr, *dummy;
	int rc = 0;
	dummy = NULL;
	tmp = malloc(len);
	if (!tmp) {
		rc = ENOMEM;
		goto out;
	}
	av = strtok_r(av, __ldmsd_cfg_delim, &ptr);
	while (av) {
		ctxt->av = av;
		dummy = strdup(av);
		if (!dummy) {
			rc = ENOMEM;
			goto out;
		}
		__get_attr_name_value(dummy, &name, &value);
		if (!name) {
			/* av is neither attribute value nor keyword */
			rc = EINVAL;
			goto out;
		}
		if (0 == strcmp(name, "name") || 0 == strcmp(name, "plugin")) {
			rc = add_attr_from_attr_str(name, value,
						    &ctxt->request,
						    &ctxt->request_sz,
						    ctxt->msglog);
			if (rc)
				goto out;
		} else {
			cnt += snprintf(&tmp[cnt], len - cnt, "%s=%s ", name, value);
		}
		av = strtok_r(NULL, __ldmsd_cfg_delim, &ptr);
		free(dummy);
		dummy = NULL;
	}

	if (cnt) {
		tmp[cnt-1] = '\0'; /* Replace the last ' ' with '\0' */
		/* Add an attribute of type 'STRING' */
		rc = add_attr_from_attr_str(NULL, tmp,
					    &ctxt->request,
					    &ctxt->request_sz,
					    ctxt->msglog);
	}

out:
	if (tmp)
		free(tmp);
	if (dummy)
		free(dummy);
	return rc;
}

int __ldmsd_parse_cmdline_req(struct ldmsd_parse_ctxt *ctxt)
{
	/* Treat the attribute string as a single STRING attribute value */
	return add_attr_from_attr_str(NULL, ctxt->av,
			&ctxt->request, &ctxt->request_sz, ctxt->msglog);
}

struct ldmsd_req_array *ldmsd_parse_config_str(const char *cfg, uint32_t msg_no,
					size_t xprt_max_msg, ldmsd_msg_log_f msglog)
{
	char *av, *verb, *dummy;
	struct ldmsd_parse_ctxt ctxt = {0};
	struct ldmsd_req_array *req_array;
	int rc, array_sz;
	ldmsd_req_hdr_t req;
	size_t req_off, remaining;
	size_t req_hdr_sz = sizeof(struct ldmsd_req_hdr_s);

	errno = ENOMEM;
	req_array = calloc(1, sizeof(*req_array) + LDMSD_REQ_ARRAY_CARD_INIT * sizeof(ldmsd_req_hdr_t));
	if (!req_array)
		return NULL;
	req_array->num_reqs = 0;
	array_sz = LDMSD_REQ_ARRAY_CARD_INIT;

	dummy = strdup(cfg);
	if (!dummy) {
		ldmsd_req_array_free(req_array);
		return NULL;
	}

	verb = dummy;
	/* Get the request id */
	av = strchr(dummy, ' ');
	if (av) {
		*av = '\0';
		av++;
	}

	ctxt.request_sz = xprt_max_msg;
	ctxt.msglog = msglog;
	ctxt.av = av;
	ctxt.request = calloc(1, ctxt.request_sz);
	if (!ctxt.request) {
		rc = ENOMEM;
		goto err;
	}

	ctxt.request->marker = LDMSD_RECORD_MARKER;
	ctxt.request->type = LDMSD_REQ_TYPE_CONFIG_CMD;
	ctxt.request->flags = LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F;
	ctxt.request->msg_no = msg_no;
	ctxt.request->req_id = ldmsd_req_str2id(verb);
	if (((int)ctxt.request->req_id < 0) || (ctxt.request->req_id == LDMSD_NOTSUPPORT_REQ)) {
		rc = ENOSYS;
		goto err;
	}
	ctxt.request->rec_len = sizeof(*ctxt.request);
	if (!ctxt.av)
		goto out;

	switch (ctxt.request->req_id) {
	case LDMSD_PLUGN_CONFIG_REQ:
		rc = __ldmsd_parse_plugin_config(&ctxt);
		break;
	case LDMSD_ENV_REQ:
		rc = __ldmsd_parse_env(&ctxt);
		break;
	case LDMSD_LISTEN_REQ:
		rc = __ldmsd_parse_listen_req(&ctxt);
		break;
	case LDMSD_AUTH_ADD_REQ:
		rc = __ldmsd_parse_auth_add_req(&ctxt);
		break;
	case LDMSD_CMDLINE_OPTIONS_SET_REQ:
		rc = __ldmsd_parse_cmdline_req(&ctxt);
		break;
	default:
		rc = __ldmsd_parse_generic(&ctxt);
		break;
	}
	if (rc)
		goto err;
out:
	/* Add the terminating attribute */
	rc = add_attr_from_attr_str(NULL, NULL, &ctxt.request,
				    &ctxt.request_sz, ctxt.msglog);
	if (rc)
		goto err;
	/* Make sure that all records aren't larger than xprt_max_msg. */
	req_off = 0; /* offset from the end of the header */
	/*
	 * Size of the attribute list in \c ctxt.request.
	 */
	size_t data_len = ctxt.request->rec_len - req_hdr_sz;
	ldmsd_hton_req_msg(ctxt.request);
	/*
	 * The message is chunked in multiple records if the length is larger than
	 * the max of the transport message size. An attribute may be split
	 * and sent separatedly in multiple records. In other words,
	 * a record may contain an incomplete attribute. Only the last record
	 * of a request contains the terminating attribute. The last record is
	 * the record that the flags contain LDMSD_REQ_EOM_F, i.e.,
	 * (req->flags & LDMSD_REQ_EOM_F != 0).
	 *
	 * The client must aggregate all records into a single message before processing the message.
	 */
	while (1) {
		if (req_array->num_reqs == array_sz) {
			req_array = realloc(req_array, sizeof(*req_array) +
					(array_sz * 2) * sizeof(ldmsd_req_hdr_t));
			if (!req_array) {
				rc = ENOMEM;
				goto err;
			}
			array_sz *= 2;
		}
		req_array->reqs[req_array->num_reqs] = calloc(1, xprt_max_msg);
		if (!req_array->reqs[req_array->num_reqs]) {
			rc = ENOMEM;
			goto err;
		}
		req = req_array->reqs[req_array->num_reqs];
		memcpy(req, ctxt.request, req_hdr_sz);
		if (0 == req_array->num_reqs)
			req->flags = LDMSD_REQ_SOM_F;
		else
			req->flags= 0;
		req->rec_len = req_hdr_sz;
		remaining = xprt_max_msg - req_hdr_sz;
		if (remaining >= data_len) {
			/* Last record */
			remaining = data_len;
		}
		memcpy(req + 1, &((char *)(ctxt.request + 1))[req_off], remaining);
		req_off += remaining;
		data_len -= remaining;
		req->rec_len += remaining;
		req_array->num_reqs++;
		/* convert to network-byte-order */
		req->flags = htonl(req->flags);
		req->rec_len = htonl(req->rec_len);
		if (data_len == 0) {
			/*
			 * All data has been copied to a record.
			 * This is the last record.
			 */
			req->flags |= htonl(LDMSD_REQ_EOM_F);
			break;
		}
	}
	free(ctxt.request);
	free(dummy);
	errno = 0;
	return req_array;
err:
	errno = rc;
	free(dummy);
	ldmsd_req_array_free(req_array);
	if (ctxt.request)
		free(ctxt.request);
	return NULL;
}

void ldmsd_req_array_free(struct ldmsd_req_array *req_array)
{
	if (!req_array)
		return;
	int i;
	for (i = 0; i < req_array->num_reqs; i++)
		free(req_array->reqs[i]);
	free(req_array);
}

ldmsd_req_attr_t ldmsd_req_attr_get_by_id(char *request, uint32_t attr_id)
{
	ldmsd_req_hdr_t req = (ldmsd_req_hdr_t)request;
	ldmsd_req_attr_t attr = ldmsd_first_attr(req);
	while (attr->discrim) {
		if (attr->attr_id == attr_id)
			return attr;
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

char *ldmsd_req_attr_str_value_get_by_id(ldmsd_req_ctxt_t req, uint32_t attr_id)
{
	ldmsd_req_attr_t attr = ldmsd_req_attr_get_by_id(req->req_buf, attr_id);
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

char *ldmsd_req_attr_str_value_get_by_name(ldmsd_req_ctxt_t req, const char *name)
{
	int32_t attr_id = ldmsd_req_attr_str2id(name);
	if (attr_id < 0)
		return NULL;
	return ldmsd_req_attr_str_value_get_by_id(req, attr_id);
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

uint32_t ldmsd_msg_no_get()
{
	static uint32_t msg_no = 1;
	return __sync_fetch_and_add(&msg_no, 1);
}

void ldmsd_msg_buf_init(struct ldmsd_msg_buf *buf)
{
	buf->off = 0;
	buf->flags = 0;
}

struct ldmsd_msg_buf *ldmsd_msg_buf_new(size_t len)
{
	struct ldmsd_msg_buf *buf;
	buf = malloc(sizeof(*buf));
	if (!buf)
		return NULL;
	buf->buf = malloc(len);
	if (!buf->buf) {
		free(buf);
		return NULL;
	}
	buf->len = len;
	ldmsd_msg_buf_init(buf);
	return buf;
}

void ldmsd_msg_buf_free(struct ldmsd_msg_buf *buf)
{
	if (!buf)
		return;
	free(buf->buf);
	free(buf);
}

int ldmsd_msg_buf_send(struct ldmsd_msg_buf *buf,
			void *xprt, uint32_t msg_no,
			ldmsd_msg_send_fn_t send_fn,
			int msg_flags, int msg_type,
			uint32_t req_id_n_rsp_err,
			const char *data, size_t data_len)
{
	ldmsd_req_hdr_t msg_buff;
	size_t remaining;
	int rc;

	if (msg_flags & LDMSD_REQ_SOM_F)
		buf->off = sizeof(struct ldmsd_req_hdr_s);

	msg_buff = (ldmsd_req_hdr_t)buf->buf;
	buf->flags |= msg_flags;
	do {
		remaining = buf->len - buf->off;
		remaining = (data_len < remaining)?data_len:remaining;
		if (remaining && data)
			memcpy(&buf->buf[buf->off], data, remaining);

		data_len -= remaining;
		buf->off += remaining;
		data += remaining;

		if ((0 == remaining) ||
			((0 == data_len) && (msg_flags & LDMSD_REQ_EOM_F))) {
			/* The buffer is full. */
			if (data_len != 0)
				buf->flags &= ~LDMSD_REQ_EOM_F;
			else if (msg_flags & LDMSD_REQ_EOM_F)
				buf->flags |= LDMSD_REQ_EOM_F;

			if (msg_type == LDMSD_REQ_TYPE_CONFIG_CMD)
				msg_buff->req_id = req_id_n_rsp_err;
			else
				msg_buff->rsp_err = req_id_n_rsp_err;

			msg_buff->type = msg_type;
			msg_buff->msg_no = msg_no;
			msg_buff->marker = LDMSD_RECORD_MARKER;
			msg_buff->rec_len = buf->off;
			msg_buff->flags = buf->flags;

			ldmsd_hton_req_hdr(msg_buff);
			rc = send_fn(xprt, (char *)msg_buff, ntohl(msg_buff->rec_len));
			if (rc)
				return rc;

			ldmsd_msg_buf_init(buf);
			buf->off = sizeof(struct ldmsd_req_hdr_s);
		}
	} while (data_len);

	return 0;
}

int ldmsd_msg_gather(struct ldmsd_msg_buf *buf, ldmsd_req_hdr_t req)
{
	void *ptr;
	size_t len = ntohl(req->rec_len);
	uint32_t flags = ntohl(req->flags);
	ldmsd_req_hdr_t hdr = (ldmsd_req_hdr_t)buf->buf;

	if (flags & LDMSD_REQ_SOM_F) {
		ptr = req;
	} else {
		ptr = req + 1;
		len -= sizeof(*req);
	}
	if (buf->len - buf->off < len) {
		char *new_buf = realloc(buf->buf, 2 * buf->len + len);
		if (!new_buf)
			return ENOMEM;
		buf->buf = new_buf;
		buf->len = 2 * buf->len + len;
		hdr = (ldmsd_req_hdr_t)buf->buf;
	}
	memcpy(&buf->buf[buf->off], ptr, len);
	buf->off += len;
	hdr->rec_len = htonl(buf->off);
	if (flags & LDMSD_REQ_EOM_F)
		return 0;
	return EBUSY;
}
