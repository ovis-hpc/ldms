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

/* Functions/strcture implemented in ldmsd.c */
#define LDMSD_DEFAULT_GATHER_INTERVAL 1000000
struct plugin {
	void *handle;
	char *name;
	char *libpath;
	unsigned long sample_interval_us;
	long sample_offset_us;
	int synchronous;
	int thread_id;
	int ref_count;
	union {
		struct ldmsd_plugin *plugin;
		struct ldmsd_sampler *sampler;
		struct ldmsd_store *store;
	};
	struct timeval timeout;
	struct event *event;
	pthread_mutex_t lock;
	LIST_ENTRY(plugin) entry;
};

extern pthread_mutex_t sp_list_lock;
extern LIST_HEAD(ldmsd_store_policy_list, ldmsd_store_policy) sp_list;
extern pthread_mutex_t host_list_lock;
extern LIST_HEAD(host_list_s, hostspec) host_list;

void ldms_log(const char *fmt, ...);
struct plugin *get_plugin(char *name);
struct plugin *new_plugin(char *plugin_name, char *err_str);
void plugin_sampler_cb(int fd, short sig, void *arg);
int find_least_busy_thread();
struct event_base *get_ev_base(int idx);
int sp_create_hset_ref_list(struct hostspec *hs,
			   struct ldmsd_store_policy *sp,
			   const char *hostname,
			   const char *_metrics);
struct ldmsd_store_policy *get_store_policy(const char *container,
			const char *set_name, const char *comp_type);
int create_metric_idx_list(struct ldmsd_store_policy *sp, const char *_metrics,
				ldms_mvec_t mvec);
struct store_instance *ldmsd_store_instance_get(struct ldmsd_store *store,
						struct ldmsd_store_policy *sp);
void destroy_store_policy(struct ldmsd_store_policy *sp);
void host_sampler_cb(int fd, short sig, void *arg);
struct hostset *find_host_set(struct hostspec *hs, const char *set_name);
struct hostset *hset_new();
int calculate_timeout(int thread_id, unsigned long interval_us,
			     long offset_us, struct timeval* tv);

int ocm_req_cb(struct ocm_event *e)
{
	/* ldmsd shall not serve configuration for anyone */
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
	char err_str[128];
	const struct ocm_value *v = ocm_av_get_value(cmd, "name");
	if (!v) {
		ldms_log("ocm: error, attribute 'name' not found for 'load'"
				" command.\n");
		return;
	}
	plugin_name = (char*)v->s.str;

	/* load */
	struct plugin *pi = get_plugin(plugin_name);
	if (pi) {
		ldms_log("Plugin '%s' already loaded", plugin_name);
		return;
	}
	pi = new_plugin(plugin_name, err_str);
	if (!pi) {
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
	/* This is not a real component ID. It is needed just for backward
	 * compatibility. */
	av_list->list[count].name = "component_id";
	av_list->list[count].value = "0";
	count++;

	av_list->count = count;

	pthread_mutex_lock(&pi->lock);
	int rc = pi->plugin->config(kw_list, av_list);
	if (rc) {
		ldms_log("Plugin '%s' configure error\n", plugin_name);
		pthread_mutex_unlock(&pi->lock);
		return;
	}

	free(av_list);
	free(kw_list);

	/* set metric_id here :) */
	v = ocm_av_get_value(cmd, "metric_id");
	if (!v)
		goto out; /* this is not a sampler */
	ocm_cfg_cmd_t mcmd = v->cmd;

	v = ocm_av_get_value(cmd, "set");
	ldms_set_t set = ldms_get_set(v->s.str);

	ocm_av_iter_init(&iter, mcmd);

	while (ocm_av_iter_next(&iter, &attr, &v) == 0) {
		ldms_metric_t m = ldms_get_metric(set, attr);
		if (m) {
			ldms_set_user_data(m, v->u64);
		} else {
			ldms_log("ocm: error, cannot find metric %s.\n", attr);
		}
	}

out:
	pthread_mutex_unlock(&pi->lock);
}

void ocm_handle_cfg_cmd_start(ocm_cfg_cmd_t cmd)
{
	/* sample */
	const struct ocm_value *v;
	const char *plugin_name;
	v = ocm_av_get_value(cmd, "name");
	plugin_name = v->s.str;
	struct plugin *pi = get_plugin((char*)plugin_name);

	if (!pi) {
		ldms_log("ocm: erorr, trying to start unloaded plugin: %s\n",
				plugin_name);
	}

	pthread_mutex_lock(&pi->lock);

	uint64_t interval = 60000000; /* default 60 sec. */
	v = ocm_av_get_value(cmd, "interval");
	if (v) {
		interval = v->u64;
		pi->sample_interval_us = interval;
	} else {
		ldms_log("ocm: warning, no 'interval' for plugin '%s',"
				" using %d.\n",
				plugin_name, interval);
	}

	v = ocm_av_get_value(cmd, "offset");
	if (v) {
		pi->synchronous = 1;
		pi->sample_offset_us = v->i64;
	} else {
		pi->synchronous = 0;
	}

	pi->ref_count++;

	pi->thread_id = find_least_busy_thread();
	pi->event = evtimer_new(get_ev_base(pi->thread_id), plugin_sampler_cb, pi);
	if (pi->synchronous){
		calculate_timeout(-1, pi->sample_interval_us,
				  pi->sample_offset_us, &pi->timeout);
	} else {
		pi->timeout.tv_sec = interval / 1000000;
		pi->timeout.tv_usec = interval % 1000000;
	}

	evtimer_add(pi->event, &pi->timeout);

	pthread_mutex_unlock(&pi->lock);
}

/**
 * Equivalent to command add in ldmsctl.
 */
void ocm_handle_cfg_cmd_add_host(ocm_cfg_cmd_t cmd)
{
	struct sockaddr_in sin;
	struct hostspec *hs;
	const struct ocm_value *v;
	const char *attr;
	const char *type;
	const char *host;
	char *xprt;
	char *sets;
	int host_type;
	long interval = LDMSD_DEFAULT_GATHER_INTERVAL;
	long port_no = LDMS_DEFAULT_PORT;

	/* Handle all the EINVAL cases first */
	v = ocm_av_get_value(cmd, "type");
	if (!v || v->type != OCM_VALUE_STR) {
		ldms_log("ocm: error, 'type' is not specified in 'add_host'.\n");
		return;
	}
	type = v->s.str;
	host_type = str_to_host_type(type);
	if (host_type < 0) {
		ldms_log("ocm: error, '%s' is an invalid host type.\n", type);
		return ;
	}
	v = ocm_av_get_value(cmd, "host");
	if (!v || v->type != OCM_VALUE_STR) {
		ldms_log("ocm: error, 'host' is not specified in 'add_host'.\n");
		return;
	}
	host = v->s.str;

	/*
	 * If the connection type is either active or passive,
	 * need a set list to create hostsets.
	 */
	v = ocm_av_get_value(cmd, "sets");
	if (v)
		sets = (char*)v->s.str;
	else
		sets = NULL;
	if (host_type != BRIDGING) {
		if (!sets) {
			ldms_log("ocm: error, 'sets' attribute not found.\n");
			return;
		}
	} else {
		if (sets) {
			ldms_log("ocm: error, Aborted!. Use type=ACTIVE to "
					"collect the sets.\n");
			return;
		}
	}

	hs = calloc(1, sizeof(*hs));
	if (!hs)
		goto enomem;
	hs->hostname = strdup(host);
	if (!hs->hostname)
		goto enomem;

	int rc = resolve(hs->hostname, &sin);
	if (rc) {
		ldms_log("ocm: error, the host '%s' could not be resolved "
			"due to error %d.\n", hs->hostname, rc);
		goto err;
	}

	v = ocm_av_get_value(cmd, "port");
	if (v) {
		port_no = v->u16;
	} else {
		port_no = LDMS_DEFAULT_PORT;
	}

	v = ocm_av_get_value(cmd, "xprt");
	if (v)
		xprt = strdup(v->s.str);
	else
		xprt = strdup("sock");
	if (!xprt)
		goto enomem;

	v = ocm_av_get_value(cmd, "interval");
	if (v)
		interval = v->u64;

	v = ocm_av_get_value(cmd, "offset");
	if (v) {
		hs->synchronous = 1;
		hs->sample_offset = v->i64;
	}

	sin.sin_port = htons(port_no);
	hs->type = host_type;
	hs->sin = sin;
	hs->xprt_name = xprt;
	hs->sample_interval = interval;
	hs->connect_interval = 20000000; /* twenty seconds */
	hs->conn_state = HOST_DISCONNECTED;
	pthread_mutex_init(&hs->set_list_lock, 0);
	pthread_mutex_init(&hs->conn_state_lock, NULL);

	hs->thread_id = find_least_busy_thread();
	hs->event = evtimer_new(get_ev_base(hs->thread_id),
				host_sampler_cb, hs);
	/* First connection attempt happens 'right away' */
	hs->timeout.tv_sec = 0; // hs->connect_interval / 1000000;
	hs->timeout.tv_usec = 500000; // hs->connect_interval % 1000000;

	/* No hostsets will be created if the connection type is bridging. */
	if (host_type == BRIDGING)
		goto add_timeout;

	char *set_name = strtok(sets, ",");
	struct hostset *hset;
	while (set_name) {
		/* Check to see if it's already there */
		hset = find_host_set(hs, set_name);
		if (!hset) {
			hset = hset_new();
			if (!hset) {
				goto clean_set_list;
			}

			hset->name = strdup(set_name);
			if (!hset->name) {
				free(hset);
				goto clean_set_list;
			}

			hset->host = hs;

			LIST_INSERT_HEAD(&hs->set_list, hset, entry);
		}
		set_name = strtok(NULL, ",");
	}
add_timeout:
	evtimer_add(hs->event, &hs->timeout);

	pthread_mutex_lock(&host_list_lock);
	LIST_INSERT_HEAD(&host_list, hs, link);
	pthread_mutex_unlock(&host_list_lock);

	return;

clean_set_list:
	while (hset = LIST_FIRST(&hs->set_list)) {
		LIST_REMOVE(hset, entry);
		free(hset->name);
		free(hset);
	}
enomem:
	rc = ENOMEM;
	ldms_log("ocm: error, Memory allocation failure.\n");
err:
	if (hs->hostname)
		free(hs->hostname);
	if (hs->xprt_name)
		free(hs->xprt_name);
	free(hs);
}

/**
 * Equivalent to command store in ldmsctl.
 */
void ocm_handle_cfg_cmd_store(ocm_cfg_cmd_t cmd)
{
	const char *err_str;
	const char *set_name;
	const char *store_name;
	const char *comp_type;
	const char *attr;
	char *metrics = NULL;
	char *hosts = NULL;
	const char *container;
	struct hostspec *hs;
	struct plugin *store;
	const struct ocm_value *v;

	if (LIST_EMPTY(&host_list)) {
		ldms_log("ocm: error, No hosts were added. No metrics to "
				"be stored. Aborted!\n");
		return ;
	}

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

	store = get_plugin((char*)store_name);
	if (!store) {
		err_str = "The storage plugin was not found.";
		goto enoent;
	}

	v = ocm_av_get_value(cmd, "hosts");
	if (v)
		hosts = (char*)v->s.str;

	struct ldmsd_store_policy *sp = get_store_policy(container,
					set_name, comp_type);
	if (!sp)
		goto enomem;

	int rc;
	/* Creating the hostset_ref_list for the store policy */
	if (!hosts) {
		/* No given hosts */
		pthread_mutex_lock(&host_list_lock);
		LIST_FOREACH(hs, &host_list, link) {
			rc = sp_create_hset_ref_list(hs, sp, hs->hostname,
							metrics);
			if (rc) {
				pthread_mutex_unlock(&host_list_lock);
				goto destroy_store_policy;
			}
		}
		pthread_mutex_unlock(&host_list_lock);
	} else {
		/* Given hosts */
		char *hostname = strtok(hosts, ",");
		while (hostname) {
			pthread_mutex_lock(&host_list_lock);
			/*
			 * Find the given hosts
			 */
			LIST_FOREACH(hs, &host_list, link) {
				if (0 != strcmp(hs->hostname, hostname))
					continue;

				rc = sp_create_hset_ref_list(hs, sp, hostname,
								metrics);
				if (rc) {
					pthread_mutex_unlock(&host_list_lock);
					goto destroy_store_policy;
				}
				break;
			}
			pthread_mutex_unlock(&host_list_lock);
			/* Host not found */
			if (!hs) {
				ldms_log("ocm: error, could not find the host "
					"'%s'.", hostname);
				return ;
			}
			hostname = strtok(NULL, ",");
		}
	}
	/* Done creating the hostset_ref_list for the store policy */

	/* Try to create the metric index list */
	struct hostset_ref *hset_ref;
	ldms_mvec_t mvec;
	LIST_FOREACH(hset_ref, &sp->hset_ref_list, entry) {
		mvec = hset_ref->hset->mvec;
		if (mvec) {
			rc = create_metric_idx_list(sp, metrics, mvec);
			if (rc)
				goto destroy_store_policy;
		} else {
			continue;
		}
		if (sp->state == STORE_POLICY_READY)
			break;
	}

	/*
	 * If the above loop fails to create the metric list,
	 * and, if metrics are given, create a blank metric list.
	 */
	if (sp->state != STORE_POLICY_READY && metrics) {
		char *metric = strtok(metrics, ",");
		struct ldmsd_store_metric_index *smi;
		while (metric) {
			smi = malloc(sizeof(*smi));
			if (!smi) {
				ldms_log("ocm: error, memory allocation"
						" failed.\n");
				goto destroy_store_policy;
			}


			smi->name = strdup(metric);
			if (!smi->name) {
				free(smi);
				ldms_log("ocm: error, memory allocation"
						" failed.\n");
				goto destroy_store_policy;
			}
			LIST_INSERT_HEAD(&sp->metric_list, smi, entry);
			sp->metric_count++;
			metric = strtok(NULL, ",");
		}
	}

	struct store_instance *si;
	si = ldmsd_store_instance_get(store->store, sp);
	if (!si) {
		ldms_log("ocm: error, memory allocation failed.\n");
		destroy_store_policy(sp);
		goto enomem;
	}

	sp->si = si;

	struct ldmsd_store_policy_ref *sp_ref;
	struct ldmsd_store_policy_ref_list fake_list;
	LIST_INIT(&fake_list);
	/* allocate first */
	LIST_FOREACH(hset_ref, &sp->hset_ref_list, entry) {
		sp_ref = malloc(sizeof(*sp_ref));
		if (!sp_ref) {
			ldms_log("ocm: error, memory allocation failed.\n");
			free(si);
			goto clean_fake_list;
		}
		LIST_INSERT_HEAD(&fake_list, sp_ref, entry);
	}

	/* Hand the store policy handle to all hostsets */
	LIST_FOREACH(hset_ref, &sp->hset_ref_list, entry) {
		sp_ref = LIST_FIRST(&fake_list);
		sp_ref->lsp = sp;
		LIST_REMOVE(sp_ref, entry);
		LIST_INSERT_HEAD(&hset_ref->hset->lsp_list, sp_ref, entry);
	}

	pthread_mutex_lock(&sp_list_lock);
	LIST_INSERT_HEAD(&sp_list, sp, link);
	pthread_mutex_unlock(&sp_list_lock);

	ldms_log("ocm: Added the store '%s' successfully.\n", container);
	return ;

clean_fake_list:
	while (sp_ref = LIST_FIRST(&fake_list)) {
		LIST_REMOVE(sp_ref, entry);
		free(sp_ref);
	}
destroy_store_policy:
	destroy_store_policy(sp);
	return ;
enomem:
	ldms_log("ocm: error, memory allocation failed.\n");
	return ;
einval:
	ldms_log("ocm: error, the '%s' attribute is not specified.\n", attr);
	return ;
enoent:
	ldms_log("ocm: error, the plugin '%s' was not found.", store_name);
	return ;
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
	ocm_enable(ocm);
}
