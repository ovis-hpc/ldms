/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013 Sandia Corporation. All rights reserved.
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

#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <netinet/in.h>
#include <ocm/ocm.h>
#include <coll/str_map.h>
#include <ovis_util/util.h>
#include <pthread.h>
#include <event2/event.h>

#include "ldms.h"
#include "ldmsd.h"

ocm_t ocm = NULL;
str_map_t ocm_verb_fn = NULL;


typedef void (*ocm_cfg_cmd_handle_fn)(ocm_cfg_cmd_t cmd);

int ocm_req_cb(struct ocm_event *e)
{
	/* ldmsd shall not serve configuration  for anyone */
	const char *key = ocm_cfg_req_key(e->req);
	ocm_event_resp_err(e, ENOSYS, key, "Not implemented.");
	return 0;
}

/**
 * This is quivalent to the combination of load, config and start in ldmsctl.
 */
void ocm_handle_cfg_cmd_config(ocm_cfg_cmd_t cmd)
{
	int i;
	char *plugin_name;
	char err_str[LEN_ERRSTR];
	int rc = 0;
	const struct ocm_value *v = ocm_av_get_value(cmd, "name");
	if (!v) {
		ldms_log("ocm: error, attribute 'name' not found for 'load'"
				" command.\n");
		return;
	}
	plugin_name = (char*)v->s.str;

	/* load */
	rc = ldmsd_load_plugin(plugin_name, err_str);
	if (rc) {
		ldms_log("%s\n", err_str);
		return;
	}

	/* config */
	struct attr_value_list *av_list = av_new(128);
	struct attr_value_list *kw_list = av_new(128);

	struct ocm_av_iter iter;
	ocm_av_iter_init(&iter, cmd);
	const char *attr;
	char *_value;
	int count = 0;

	while (ocm_av_iter_next(&iter, &attr, &v) == 0) {
		if (strcmp(attr, "metric_id") == 0)
			continue; /* skip metric_id */
		av_list->list[count].name = (char*)attr;
		av_list->list[count].value = (char*)v->s.str;
		count++;
	}

	av_list->count = count;
	rc = ldmsd_config_plugin(plugin_name, av_list, kw_list, err_str);
	if (rc) {
		ldms_log("%s\n", err_str);
		return;
	}

	/* set metric_id here :) */
	v = ocm_av_get_value(cmd, "metric_id");
	if (!v)
		return; /* not a sampler */
	ocm_cfg_cmd_t mcmd = v->cmd;

	v = ocm_av_get_value(cmd, "instance_name");
	ldms_set_t set = ldms_get_set(v->s.str);

	ocm_av_iter_init(&iter, mcmd);

	while (ocm_av_iter_next(&iter, &attr, &v) == 0) {
		rc = _ldmsd_set_udata(set, attr, v->u64, err_str);
		if (rc)
			ldms_log("ocm: error, %s\n", err_str);
	}
}

void ocm_handle_cfg_cmd_start(ocm_cfg_cmd_t cmd)
{
	/* sample */
	const struct ocm_value *v;
	const char *plugin_name, *interval, *offset;
	char *attr;
	char err_str[LEN_ERRSTR];
	int rc = 0;

	attr = "name";
	v = ocm_av_get_value(cmd, attr);
	if (!v) {
		ldms_log("ocm: failed to start a sampler. "
				"the attribute 'name' not found\n");
		return;
	}
	plugin_name = v->s.str;

	attr = "interval";
	v = ocm_av_get_value(cmd, attr);
	if (!v) {
		ldms_log("ocm: attribute '%s' not found for plugin '%s'\n",
							attr, plugin_name);
		return;
	}
	interval = v->s.str;

	v = ocm_av_get_value(cmd, "offset");
	if (v)
		offset = v->s.str;
	else
		offset = NULL;
	rc = ldmsd_start_sampler(plugin_name, interval, offset, err_str);
}

/**
 * Equivalent to command add in ldmsctl.
 */
void ocm_handle_cfg_cmd_add_host(ocm_cfg_cmd_t cmd)
{
	char err_str[LEN_ERRSTR];
	const struct ocm_value *v;
	const char *type;
	const char *host;
	const char *xprt = NULL;
	const char *sets = NULL;
	const char *port = NULL;
	const char *interval = NULL;
	const char *offset = NULL;
	int rc = 0;

	/* Handle all the EINVAL cases first */
	v = ocm_av_get_value(cmd, "type");
	if (!v || v->type != OCM_VALUE_STR) {
		ldms_log("ocm: error, 'type' is not specified in 'add_host'.\n");
		return;
	}
	type = v->s.str;

	v = ocm_av_get_value(cmd, "host");
	if (!v || v->type != OCM_VALUE_STR) {
		ldms_log("ocm: error, 'host' is not specified in 'add_host'.\n");
		return;
	}
	host = v->s.str;

	v = ocm_av_get_value(cmd, "sets");
	if (v)
		sets = (char*)v->s.str;

	v = ocm_av_get_value(cmd, "port");
	if (v)
		port = v->s.str;

	v = ocm_av_get_value(cmd, "xprt");
	if (v)
		xprt = v->s.str;

	v = ocm_av_get_value(cmd, "interval");
	if (v)
		interval = v->s.str;

	v = ocm_av_get_value(cmd, "offset");
	if (v)
		offset = v->s.str;

	rc = ldmsd_add_host(host, type, xprt, port, sets, interval,
						offset, err_str);
	if (rc) {
		ldms_log("ocm: failed to add the host '%s': %s\n",
							host, err_str);
	}
	return;
}

/**
 * Equivalent to command store in ldmsctl.
 */
void ocm_handle_cfg_cmd_store(ocm_cfg_cmd_t cmd)
{
	char err_str[LEN_ERRSTR];
	const char *set_name;
	const char *store_name;
	const char *comp_type;
	const char *attr;
	char *metrics = NULL;
	char *hosts = NULL;
	const char *container;
	const struct ocm_value *v;

	attr = "name";
	v = ocm_av_get_value(cmd, attr);
	if (!v)
		goto einval;
	store_name = v->s.str;

	attr = "comp_type";
	v = ocm_av_get_value(cmd, attr);
	if (!v)
		goto einval;
	comp_type = v->s.str;

	attr = "set";
	v = ocm_av_get_value(cmd, attr);
	if (!v)
		goto einval;
	set_name = v->s.str;

	attr = "container";
	v = ocm_av_get_value(cmd, attr);
	if (!v)
		goto einval;
	container = v->s.str;

	v = ocm_av_get_value(cmd, "metrics");
	if (v)
		metrics = (char*)v->s.str;

	v = ocm_av_get_value(cmd, "hosts");
	if (v)
		hosts = (char*)v->s.str;

	int rc = ldmsd_store(store_name, comp_type, set_name, container,
						metrics, hosts, err_str);
	if (rc) {
		ldms_log("ocm: failed to start the store container '%s': %s\n",
							container, err_str);
	}
	return;
einval:
	ldms_log("ocm: error, the '%s' attribute is not specified.\n", attr);
	return;
}

void ocm_handle_cfg(ocm_cfg_t cfg)
{
	struct ocm_cfg_cmd_iter cmd_iter;
	ocm_cfg_cmd_iter_init(&cmd_iter, cfg);
	ocm_cfg_cmd_t cmd;
	while (ocm_cfg_cmd_iter_next(&cmd_iter, &cmd) == 0) {
		const char *verb = ocm_cfg_cmd_verb(cmd);
		ocm_cfg_cmd_handle_fn fn = (void*)str_map_get(ocm_verb_fn, verb);
		if (!fn) {
			ldms_log("ocm: error: unknown verb \"%s\""
					" (cfg_key: %s)\n",
					verb, ocm_cfg_key(cfg));
			continue;
		}
		fn(cmd);
	}
}

int ocm_cfg_cb(struct ocm_event *e)
{
	switch (e->type) {
	case OCM_EVENT_CFG_RECEIVED:
		ocm_handle_cfg(e->cfg);
		break;
	case OCM_EVENT_ERROR:
		ldms_log("ocm: error key: %s, msg: %s\n", ocm_err_key(e->err),
				ocm_err_msg(e->err));
		break;
	case OCM_EVENT_CFG_REQUESTED:
		ocm_event_resp_err(e, ENOSYS, ocm_cfg_req_key(e->req),
				"Not implemented.");
		break;
	default:
		ldms_log("ocm: error, unknown ocm_event: %d\n", e->type);
	}
	return 0;
}

int ldmsd_ocm_init(const char *svc_type, uint16_t port)
{
	int rc;
	ocm_cb_fn_t cb;
	ocm = ocm_create("sock", port, ocm_req_cb, ldms_log);
	if (!ocm)
		return errno;
	char key[1024];
	if (gethostname(key, 1024))
		return errno;
	sprintf(key + strlen(key), "/%s", svc_type);
	ocm_register(ocm, key, ocm_cfg_cb);
	ocm_verb_fn = str_map_create(4091);
	if (!ocm_verb_fn)
		return errno;
	str_map_insert(ocm_verb_fn, "config", (uint64_t)ocm_handle_cfg_cmd_config);
	str_map_insert(ocm_verb_fn, "start", (uint64_t)ocm_handle_cfg_cmd_start);
	str_map_insert(ocm_verb_fn, "add", (uint64_t)ocm_handle_cfg_cmd_add_host);
	str_map_insert(ocm_verb_fn, "store", (uint64_t)ocm_handle_cfg_cmd_store);
	rc = ocm_enable(ocm);
	return rc;
}
