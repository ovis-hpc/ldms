/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
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

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <coll/rbt.h>
#include <ovis_util/util.h>
#include <ev/ev.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldms_xprt.h"
#include "config.h"
#include "ovis_event/ovis_event.h"

#ifndef HZ
#define HZ 100
#endif

void ldmsd_smplr___del(ldmsd_cfgobj_t obj)
{
	ldmsd_smplr_t smplr = (ldmsd_smplr_t)obj;
	free(smplr->instance);
	free(smplr->producer);
	ldmsd_cfgobj___del(obj);
}

const char *smplr_state_str(enum ldmsd_smplr_state state)
{
	switch (state) {
	case LDMSD_SMPLR_STATE_STOPPED:
		return "STOPPED";
	case LDMSD_SMPLR_STATE_RUNNING:
		return "RUNNING";
	}
	return "BAD STATE";
}

ldmsd_smplr_t
ldmsd_smplr_new_with_auth(const char *name,
			  struct ldmsd_plugin_cfg *plugin,
			  uint64_t component_id,
			  const char *producer, const char *instance,
			  uid_t uid, gid_t gid, int perm)
{
	ldmsd_smplr_t smplr;
	char *prod, *inst;
	ev_t sample_ev, start_ev, stop_ev;

	ldmsd_log(LDMSD_LDEBUG, "ldmsd_smplr_new(name %s plugin %s "
		  "component_id %ld "
		  "producer %s instance uid %d gid %d perm %x\n",
		  name, plugin->name, component_id,
		  producer, instance, uid, gid, perm);

	prod = strdup(producer);
	if (!prod)
		goto err_0;

	inst = strdup(producer);
	if (!inst)
		goto err_1;

	sample_ev = ev_new(smplr_sample_type);
	if (!sample_ev)
		goto err_2;
	start_ev = ev_new(smplr_start_type);
	if (!start_ev)
		goto err_3;

	stop_ev = ev_new(smplr_stop_type);
	if (!stop_ev)
		goto err_4;

	smplr = (struct ldmsd_smplr *)
		ldmsd_cfgobj_new_with_auth(name, LDMSD_CFGOBJ_SMPLR,
				sizeof *smplr, ldmsd_smplr___del,
				uid, gid, perm);
	if (!smplr)
		goto err_5;

	smplr->interval_us = 0;
	smplr->offset_us = 0;
	smplr->state = LDMSD_SMPLR_STATE_STOPPED;
	smplr->plugin = plugin;
	smplr->producer = prod;
	smplr->instance = inst;
	smplr->component_id = component_id;

	EV_DATA(sample_ev, struct sample_data)->smplr = smplr;
	EV_DATA(sample_ev, struct sample_data)->reschedule = 0;
	smplr->sample_ev = sample_ev;
	EV_DATA(start_ev, struct start_data)->entity = smplr;
	smplr->start_ev = start_ev;
	EV_DATA(stop_ev, struct stop_data)->entity = smplr;
	smplr->stop_ev = stop_ev;

	ldmsd_smplr_unlock(smplr);
	return smplr;
 err_5:
	ev_put(stop_ev);
 err_4:
	ev_put(start_ev);
 err_3:
	ev_put(sample_ev);
 err_2:
	free(inst);
 err_1:
	free(prod);
 err_0:
	return NULL;
}

int ldmsd_smplr_del(const char *smplr_name, ldmsd_sec_ctxt_t ctxt)
{
	int rc = 0;
	ldmsd_smplr_t smplr;

	smplr = (ldmsd_smplr_t)ldmsd_cfgobj_find(smplr_name, LDMSD_CFGOBJ_SMPLR);
	if (!smplr) {
		rc = ENOENT;
		goto out_0;
	}

	ldmsd_smplr_lock(smplr);
	rc = ldmsd_cfgobj_access_check(&smplr->obj, 0222, ctxt);
	if (rc)
		goto out_1;
	if (smplr->state != LDMSD_SMPLR_STATE_STOPPED) {
		rc = EBUSY;
		goto out_1;
	}
	if (smplr->obj.ref_count > 2) {
		rc = EBUSY;
		goto out_1;
	}

	ldmsd_cfgobj_del(smplr_name, LDMSD_CFGOBJ_SMPLR);
	ldmsd_smplr_put(smplr);
	rc = 0;
out_1:
	ldmsd_smplr_unlock(smplr);
	ldmsd_smplr_put(smplr); /* `find` reference */
out_0:
	return rc;
}

int ldmsd_set_update_hint_set(ldms_set_t set, long interval_us, long offset_us)
{
	char value[128];
	if (offset_us == LDMSD_UPDT_HINT_OFFSET_NONE)
		snprintf(value, 127, "%ld:", interval_us);
	else
		snprintf(value, 127, "%ld:%ld", interval_us, offset_us);
	return ldms_set_info_set(set, LDMSD_SET_INFO_UPDATE_HINT_KEY, value);
}

int ldmsd_set_update_hint_get(ldms_set_t set, long *interval_us, long *offset_us)
{
	char *value, *tmp, *endptr;
	*interval_us = 0;
	*offset_us = LDMSD_UPDT_HINT_OFFSET_NONE;
	value = ldms_set_info_get(set, LDMSD_SET_INFO_UPDATE_HINT_KEY);
	if (!value)
		return 0;
	tmp = strtok_r(value, ":", &endptr);
	*interval_us = strtol(tmp, NULL, 0);
	tmp = strtok_r(NULL, ":", &endptr);
	if (tmp)
		*offset_us = strtol(tmp, NULL, 0);
	ldmsd_log(LDMSD_LDEBUG, "set '%s': getting updtr hint '%s'\n",
			ldms_set_instance_name_get(set), value);
	free(value);
	return 0;
}

char *ldmsd_set_info_origin_enum2str(enum ldmsd_set_origin_type type)
{
	if (type == LDMSD_SET_ORIGIN_PRDCR)
		return "producer";
	else if (type == LDMSD_SET_ORIGIN_SMPLR)
		return "sampler";
	else
		return "";
}

void __transaction_end_time_get(struct timeval *start, struct timeval *dur,
							struct timeval *end__)
{
	end__->tv_sec = start->tv_sec + dur->tv_sec;
	end__->tv_usec = start->tv_usec + dur->tv_usec;
	if (end__->tv_usec > 1000000) {
		end__->tv_sec += 1;
		end__->tv_usec -= 1000000;
	}
}

static ldmsd_smplr_t find_smplr_by_plugn(const char *plugn_name)
{
	ldmsd_smplr_t smplr;

	for (smplr = (ldmsd_smplr_t)ldmsd_cfgobj_first(LDMSD_CFGOBJ_SMPLR); smplr;
	     smplr = (ldmsd_smplr_t)ldmsd_cfgobj_next(&smplr->obj)) {
		if (0 == strcmp(smplr->obj.name, plugn_name))
			return smplr;
	}
	return NULL;
}

/*
 * Get the set information
 *
 * When \c info is unused, ldmsd_set_info_delete() must be called to free \c info.
 */
ldmsd_set_info_t ldmsd_set_info_get(const char *inst_name)
{
	extern struct plugin_list plugin_list;
	ldmsd_set_info_t info;
	struct ldms_timestamp t;
	struct timeval dur;
	struct ldmsd_plugin_set_list *plugn_set_list;
	struct ldmsd_plugin_set *plugn_set = NULL;
	ldmsd_smplr_t smplr;

	ldms_set_t lset = ldms_set_by_name(inst_name);
	if (!lset)
		return NULL;

	info = calloc(1, sizeof(*info));
	if (!info)
		return NULL;

	info->set = lset;

	ldmsd_set_tree_lock();
	plugn_set_list = ldmsd_plugin_set_list_first();
	smplr = NULL;
	while (plugn_set_list) {
		LIST_FOREACH(plugn_set, &plugn_set_list->list, entry) {
			if (0 == strcmp(plugn_set->inst_name, inst_name)) {
				smplr =  find_smplr_by_plugn(plugn_set->plugin_name);
			}
		}
		if (plugn_set)
			break;
 		plugn_set_list = ldmsd_plugin_set_list_next(plugn_set_list);
	}
	ldmsd_set_tree_unlock();
	if (smplr) {
		info->interval_us = smplr->interval_us;
		info->offset_us = smplr->offset_us;
		info->sync = smplr->synchronous;
		info->origin_name = strdup(plugn_set->plugin_name);
		info->origin_type = LDMSD_SET_ORIGIN_SMPLR;

		t = ldms_transaction_timestamp_get(lset);
		info->start.tv_sec = (long int)t.sec;
		info->start.tv_usec = (long int)t.usec;
		if (!ldms_set_is_consistent(lset)) {
			info->end.tv_sec = 0;
			info->end.tv_usec = 0;
		} else {
			t = ldms_transaction_duration_get(lset);
			dur.tv_sec = (long int)t.sec;
			dur.tv_usec = (long int)t.usec;
			__transaction_end_time_get(&info->start,
					&dur, &info->end);
		}
		goto out;
	}

	/*
	 * The set isn't created by a sampler plugin.
	 *
	 * Now search in the producer list.
	 */
	ldmsd_prdcr_t prdcr;
	ldmsd_prdcr_set_t prd_set = NULL;
	ldmsd_cfg_lock(LDMSD_CFGOBJ_PRDCR);
	prdcr = ldmsd_prdcr_first();
	while (prdcr) {
		ldmsd_prdcr_lock(prdcr);
		prd_set = ldmsd_prdcr_set_find(prdcr, inst_name);
		if (prd_set) {
			info->origin_name = strdup(prdcr->obj.name);
			ldmsd_prdcr_unlock(prdcr);
			ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR);
			info->origin_type = LDMSD_SET_ORIGIN_PRDCR;
			ldmsd_prdcr_set_ref_get(prd_set, "sampler");
			info->prd_set = prd_set;
			info->interval_us = prd_set->updt_interval;
			info->offset_us = prd_set->updt_offset;
			info->sync = prd_set->updt_sync;
			info->start = prd_set->updt_start;
			if (prd_set->state == LDMSD_PRDCR_SET_STATE_UPDATING) {
				info->end.tv_sec = 0;
				info->end.tv_usec = 0;
			} else {
				info->end = prd_set->updt_end;
			}
			goto out;
		}
		ldmsd_prdcr_unlock(prdcr);
		prdcr = ldmsd_prdcr_next(prdcr);
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR);
out:

	return info;
}

/*
 * Delete the set information
 */
void ldmsd_set_info_delete(ldmsd_set_info_t info)
{
	if (info->set) {
		ldms_set_put(info->set);
		info->set = NULL;
	}
	if (info->origin_name) {
		free(info->origin_name);
		info->origin_name = NULL;
	}
	if ((info->origin_type == LDMSD_SET_ORIGIN_PRDCR) && info->prd_set) {
		ldmsd_prdcr_set_ref_put(info->prd_set, "sampler");
		info->prd_set = NULL;
	}
	free(info);
}

int __sampler_set_info_add(ldmsd_smplr_t smplr)
{
	ldmsd_plugin_set_t set;
	int rc;

	for (set = ldmsd_plugin_set_first(smplr->plugin->name); set;
	     set = ldmsd_plugin_set_next(set)) {
		rc = ldmsd_set_update_hint_set(set->set, smplr->interval_us, smplr->offset_us);
		if (rc) {
			ldmsd_log(LDMSD_LERROR, "Error %d: Failed to add "
					"the update hint to set '%s'\n",
					rc, ldms_set_instance_name_get(set->set));
			return rc;
		}
	}
	return 0;
}

static void stop_sampler(ldmsd_smplr_t smplr)
{
	EV_DATA(smplr->sample_ev, struct sample_data)->reschedule = 0;
}

int sample_actor(ev_worker_t src, ev_worker_t dst, ev_t ev)
{
	ldmsd_smplr_t smplr = EV_DATA(ev, struct sample_data)->smplr;
	ldmsd_smplr_lock(smplr);
	int rc = smplr->plugin->sampler->sample(smplr->plugin->sampler);
	if (rc) {
		/*
		 * If the sampler reports an error don't reschedule
		 * the timeout. This is an indication of a configuration
		 * error that needs to be corrected.
		 */
		ldmsd_log(LDMSD_LERROR, "'%s': failed to sample. Stopping "
				"the plug-in.\n", smplr->obj.name);
	} else if (EV_DATA(ev, struct sample_data)->reschedule) {
		struct timespec to;
		clock_gettime(CLOCK_REALTIME, &to);
		to.tv_sec += smplr->interval_us / 1000000;
		to.tv_nsec = smplr->offset_us * 1000;
		ev_post(src, dst, ev, &to);
	}
	ldmsd_smplr_unlock(smplr);
}

/*
 * Start the sampler
 */
static int _start_smplr(char *smplr_name, char *interval, char *offset, int is_one_shot)
{
	int rc;
	unsigned long sample_interval;
	long sample_offset;
	int synchronous;
	ldmsd_smplr_t smplr;

	sample_interval = strtoul(interval, NULL, 0);
	if (errno == ERANGE || sample_interval < HZ)
		return ERANGE;
	if (offset) {
		sample_offset = strtoul(offset, NULL, 0);
		if (errno == ERANGE
		    || sample_offset > 999999
		    || sample_offset > sample_interval)
			return ERANGE;
		synchronous = 1;
	} else {
		sample_offset = 0;
		synchronous = 0;
	}

	smplr = (ldmsd_smplr_t)ldmsd_cfgobj_find(smplr_name, LDMSD_CFGOBJ_SMPLR);
	if (!smplr)
		return ENOENT;

	ldmsd_smplr_lock(smplr);
	if (smplr->state != LDMSD_SMPLR_STATE_STOPPED) {
		rc = EBUSY;
		goto out;
	}
	smplr->interval_us = sample_interval;
	smplr->offset_us = sample_offset;
	smplr->synchronous = synchronous;

	if (!is_one_shot) {
		rc = __sampler_set_info_add(smplr);
		if (rc) {
			ldmsd_lerror("%s: sampler %s was unable to update sample hint info.\n",
				     __func__, smplr_name);
		}
		smplr->state = LDMSD_SMPLR_STATE_RUNNING;
	}

	struct timespec to;
	clock_gettime(CLOCK_REALTIME, &to);
	ev_sched_to(&to, smplr->interval_us / 1000000, smplr->offset_us * 1000);
	if (smplr->synchronous)
		to.tv_nsec = smplr->offset_us * 1000;
	ldmsd_smplr_unlock(smplr);
	ldmsd_smplr_put(smplr);

	EV_DATA(smplr->sample_ev, struct sample_data)->reschedule = !is_one_shot;
	ev_post(sampler, sampler, smplr->sample_ev, &to);
	return 0;
 out:
	ldmsd_smplr_unlock(smplr);
	ldmsd_smplr_put(smplr);
	return rc;
}

int ldmsd_start_smplr(char *smplr_name, char *interval, char *offset)
{
	return _start_smplr(smplr_name, interval, offset, 0);
}


/*
 * Stop the sampler
 */
int ldmsd_stop_smplr(char *smplr_name)
{
	ldmsd_smplr_t smplr = (ldmsd_smplr_t)ldmsd_cfgobj_find(smplr_name, LDMSD_CFGOBJ_SMPLR);
	if (!smplr)
		return ENOENT;

	ldmsd_smplr_lock(smplr);
	if (smplr->state != LDMSD_SMPLR_STATE_RUNNING)
		goto out;

	smplr->state = LDMSD_SMPLR_STATE_STOPPED;
	EV_DATA(smplr->sample_ev, struct sample_data)->reschedule = 0;
	ldmsd_smplr_unlock(smplr);
	ldmsd_smplr_put(smplr);
	return 0;
 out:
	ldmsd_smplr_unlock(smplr);
	ldmsd_smplr_put(smplr);
	return EBUSY;
}

int ldmsd_oneshot_smplr(char *smplr_name, char *interval, char *offset)
{
	return _start_smplr(smplr_name, interval, offset, 1);
}

