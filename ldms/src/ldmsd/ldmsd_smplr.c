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
#include <math.h>
#include <coll/rbt.h>
#include <ovis_util/util.h>
#include <ev/ev.h>
#include <json/json_util.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_plugin.h"
#include "ldmsd_sampler.h"
#include "ldmsd_request.h"
#include "ldms_xprt.h"
#include "config.h"
#include "ovis_event/ovis_event.h"

#ifndef HZ
#define HZ 100
#endif

void ldmsd_smplr___del(ldmsd_cfgobj_t obj)
{
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

/*
 * Get the set information
 *
 * When \c info is unused, ldmsd_set_info_delete() must be called to free \c info.
 */
ldmsd_set_info_t ldmsd_set_info_get(const char *inst_name)
{
	ldmsd_set_info_t info;
	struct ldms_timestamp t;
	struct timeval dur;

	ldmsd_set_ctxt_t set_ctxt;
	ldmsd_sampler_type_t samp;
	ldmsd_prdcr_set_t prd_set;
	ldmsd_prdcr_t prdcr;

	ldmsd_smplr_t smplr;

	ldms_set_t lset = ldms_set_by_name(inst_name);
	if (!lset)
		return NULL;
	set_ctxt = ldms_ctxt_get(lset);
	if (!set_ctxt) /* set w/o ref to prdcr or sampler */
		return NULL;

	info = calloc(1, sizeof(*info));
	if (!info)
		return NULL;

	info->set = lset;

	if (set_ctxt->type == LDMSD_SET_CTXT_SAMP) {
		/* created by sampler */
		samp = container_of(set_ctxt, struct ldmsd_sampler_type_s,
				    set_ctxt);
		info->origin_type = LDMSD_SET_ORIGIN_SMPLR;
		info->origin_name = strdup(samp->base.inst->inst_name);
		smplr = samp->smplr;
		if (smplr) {
			info->interval_us = smplr->interval_us;
			info->offset_us = smplr->offset_us;
			info->sync = smplr->synchronous;
		} else {
			/* not associated with smplr yet */
			info->interval_us = 0;
			info->offset_us = LDMSD_UPDT_HINT_OFFSET_NONE;
			info->sync = 0;
		}

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

	/* created by prdcr (lookup) */
	prd_set = container_of(set_ctxt, struct ldmsd_prdcr_set, set_ctxt);
	prdcr = prd_set->prdcr;
	info->origin_name = strdup(prdcr->obj.name);
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
	int rc = 0;
	ldmsd_sampler_type_t samp = LDMSD_SAMPLER(smplr->pi);
	ldmsd_set_entry_t ent;
	LIST_FOREACH(ent, &samp->set_list, entry) {
		rc = ldmsd_set_update_hint_set(ent->set,
				smplr->interval_us * samp->set_array_card,
				smplr->offset_us);
		if (rc)
			return rc;
	}
	return rc;
}

int sample_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t ev)
{
	ldmsd_smplr_t smplr = EV_DATA(ev, struct sample_data)->smplr;
	ldmsd_sampler_type_t samp = LDMSD_SAMPLER(smplr->pi);
	ldmsd_smplr_lock(smplr);
	int rc;

	rc = samp->sample(smplr->pi);
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
		ev_sched_to(&to, smplr->interval_us / 1000000, 0);
		to.tv_nsec = smplr->offset_us * 1000;
		ev_post(src, dst, ev, &to);
	}
	ldmsd_smplr_unlock(smplr);
	return rc;
}

/*
 * Start the sampler
 *
 * The caller must hold the smplr lock.
 */
int __ldmsd_smplr_start(ldmsd_smplr_t smplr, int is_one_shot)
{
	int rc;
	if (smplr->state != LDMSD_SMPLR_STATE_STOPPED)
		return EBUSY;

	if (!is_one_shot) {
		rc = __sampler_set_info_add(smplr);
		if (rc) {
			ldmsd_lerror("%s: sampler %s was unable to update sample hint info.\n",
				     __func__, smplr->obj.name);
		}
		smplr->state = LDMSD_SMPLR_STATE_RUNNING;
	}

	struct timespec to;
	ev_sched_to(&to, smplr->interval_us / 1000000, smplr->offset_us * 1000);
	if (smplr->synchronous)
		to.tv_nsec = smplr->offset_us * 1000;

	EV_DATA(smplr->sample_ev, struct sample_data)->reschedule = !is_one_shot;
	ev_post(sampler, sampler, smplr->sample_ev, &to);
	return 0;
}

int ldmsd_smplr_start(char *smplr_name, char *interval, char *offset,
					int is_one_shot, int flags,
					ldmsd_sec_ctxt_t sctxt)
{
	int rc = 0;
	unsigned long sample_interval = 0;
	long sample_offset = 0;
	int synchronous;
	ldmsd_smplr_t smplr;

	if (interval) {
		sample_interval = strtoul(interval, NULL, 0);
		if (errno == ERANGE || sample_interval < HZ)
			return ERANGE;
	}

	if (offset) {
		sample_offset = strtoul(offset, NULL, 0);
		if (errno == ERANGE
		    || sample_offset > 999999
		    || sample_offset > sample_interval)
			return ERANGE;
		synchronous = 1;
	}

	smplr = (ldmsd_smplr_t)ldmsd_cfgobj_find(smplr_name, LDMSD_CFGOBJ_SMPLR);
	if (!smplr)
		return ENOENT;

	ldmsd_smplr_lock(smplr);

	rc = ldmsd_cfgobj_access_check(&smplr->obj, 0222, sctxt);
	if (rc)
		goto out;
	if (interval)
		smplr->interval_us = sample_interval;
	if (offset) {
		smplr->offset_us = sample_offset;
		smplr->synchronous = synchronous;
	}

	if (flags & LDMSD_PERM_DSTART)
		smplr->obj.perm |= LDMSD_PERM_DSTART;
	else
		rc = __ldmsd_smplr_start(smplr, is_one_shot);
out:
	ldmsd_smplr_unlock(smplr);
	ldmsd_smplr_put(smplr);
	return rc;
}

/*
 * Stop the sampler
 */
int ldmsd_smplr_stop(const char *smplr_name, ldmsd_sec_ctxt_t sctxt)
{
	int rc;
	ldmsd_smplr_t smplr = (ldmsd_smplr_t)ldmsd_cfgobj_find(smplr_name, LDMSD_CFGOBJ_SMPLR);
	if (!smplr)
		return ENOENT;

	ldmsd_smplr_lock(smplr);
	rc = ldmsd_cfgobj_access_check(&smplr->obj, 0222, sctxt);
	if (rc)
		goto out;
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

int ldmsd_smplr_oneshot(char *smplr_name, char *ts, ldmsd_sec_ctxt_t sctxt)
{
	int rc;
	ldmsd_smplr_t smplr;
	time_t now, sched;
	double diff;

	if (0 == strncmp(ts, "now", 3)) {
		if (3 == strlen(ts)) {
			sched = 0;
		} else {
			ts += 4;
			sched = strtoul(ts, NULL, 10);
		}
	} else {
		sched = strtoul(ts, NULL, 10);
		now = time(0);
		if (now < 0) {
			rc = errno;
			return -rc;
		}
		diff = difftime(sched, now);
		if (diff < 0)
			return EINVAL;
		sched = floor(diff);
	}

	smplr = ldmsd_smplr_find(smplr_name);
	if (!smplr)
		return ENOENT;

	ldmsd_smplr_lock(smplr);
	smplr->interval_us = sched * 1000000;
	smplr->offset_us = 0;
	smplr->synchronous = 1;

	rc = __ldmsd_smplr_start(smplr, 1);
	ldmsd_smplr_unlock(smplr);
	ldmsd_smplr_put(smplr);
	return rc;
}

json_entity_t ldmsd_smplr_query(ldmsd_cfgobj_t obj)
{
	json_entity_t query;
	ldmsd_smplr_t smplr = (ldmsd_smplr_t)obj;

	query = ldmsd_cfgobj_query_result_new(obj);
	if (!query)
		goto oom;
	query = json_dict_build(query,
			JSON_INT_VALUE, "interval", smplr->interval_us,
			JSON_INT_VALUE, "offset", smplr->offset_us,
			JSON_BOOL_VALUE, "synchronous", smplr->synchronous,
			JSON_STRING_VALUE, "plugin_instance", smplr->pi->inst_name,
			JSON_STRING_VALUE, "state", ldmsd_smplr_state_str(smplr->state),
			-1);
	if (!query)
		goto oom;
	return ldmsd_result_new(0, NULL, query);
oom:
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
	return NULL;
}

json_entity_t ldmsd_smplr_export(ldmsd_cfgobj_t obj)
{
	json_entity_t query;
	ldmsd_smplr_t smplr = (ldmsd_smplr_t)obj;

	query = ldmsd_cfgobj_query_result_new(obj);
	if (!query)
		goto oom;
	query = json_dict_build(query,
			JSON_INT_VALUE, "interval", smplr->interval_us,
			JSON_INT_VALUE, "offset", smplr->offset_us,
			JSON_STRING_VALUE, "plugin_instance", smplr->pi->inst_name,
			-1);
	if (!query)
		goto oom;
	return ldmsd_result_new(0, NULL, query);
oom:
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
	return NULL;
}

json_entity_t __smplr_attr_get(json_entity_t dft, json_entity_t spc,
				ldmsd_plugin_inst_t *_inst, long *_interval_us,
				long *_offset_us, int *_perm)
{
	json_entity_t err = NULL;
	json_entity_t plugin, interval, offset, perm;
	ldmsd_req_buf_t buf;
	plugin = interval = offset = perm = NULL;

	buf = ldmsd_req_buf_alloc(1024);
	if (!buf)
		goto oom;

	if (spc) {
		plugin = json_value_find(spc, "plugin_instance");
		interval = json_value_find(spc, "interval");
		offset = json_value_find(spc, "offset");
		perm = json_value_find(spc, "perm");
	}

	if (!plugin && dft)
		plugin = json_value_find(dft, "plugin_instance");
	if (!interval && dft)
		interval = json_value_find(dft, "interval");
	if (!offset && dft)
		offset = json_value_find(dft, "offset");
	if (!perm && dft)
		perm = json_value_find(dft, "perm");

	/* plugin instance */
	if (plugin) {
		if (JSON_STRING_VALUE != json_entity_type(plugin)) {
			err = json_dict_build(err, JSON_STRING_VALUE, "container",
					"'container' is not a string.", -1);
			if (!err)
				goto oom;
		} else {
			char *s = json_value_str(plugin)->str;
			ldmsd_plugin_inst_t inst = ldmsd_plugin_inst_find(s);
			if (!inst) {
				int rc = ldmsd_req_buf_append(buf, "Sampler plugin "
						"instance '%s' not found.", s);
				if (rc < 0)
					goto oom;
				err = json_dict_build(err, JSON_STRING_VALUE,
						"plugin", buf->buf, -1);
				if (!err)
					goto oom;
			} else {
				*_inst = inst;
			}
		}
	} else {
		*_inst = NULL;
	}

	/* sample interval */
	if (!interval) {
		*_interval_us = LDMSD_ATTR_NA;
	} else {
		if (JSON_STRING_VALUE == json_entity_type(interval)) {
			*_interval_us = ldmsd_time_str2us(json_value_str(interval)->str);
		} else if (JSON_INT_VALUE == json_entity_type(interval)) {
			*_interval_us = json_value_int(interval);
		} else {
			err = json_dict_build(err, JSON_STRING_VALUE, "interval",
					"'interval' is neither a string or an integer.", -1);
			if (!err)
				goto oom;
		}
	}

	/* update offset */
	*_offset_us = LDMSD_ATTR_NA;
	if (offset) {
		if (JSON_STRING_VALUE == json_entity_type(offset)) {
			char *offset_s = json_value_str(offset)->str;
			if (0 == strcasecmp("none", offset_s)) {
				*_offset_us = LDMSD_UPDT_HINT_OFFSET_NONE;
			} else {
				*_offset_us = ldmsd_time_str2us(offset_s);
			}
		} else if (JSON_INT_VALUE == json_entity_type(offset)) {
			*_offset_us = json_value_int(offset);
		} else {
			*_offset_us = LDMSD_ATTR_INVALID;
			err = json_dict_build(err, JSON_STRING_VALUE, "offset",
					"'offset' is neither a string or an integer.", -1);
			if (!err)
				goto oom;
		}
	}

	/* permission */
	if (perm) {
		if (JSON_STRING_VALUE != json_entity_type(perm)) {
			err = json_dict_build(err, JSON_STRING_VALUE,
						"'perm' is not a string.", -1);
			if (!err)
				goto oom;
		} else {
			*_perm = strtol(json_value_str(perm)->str, NULL, 0);
		}
	} else {
		*_perm = LDMSD_ATTR_NA;
	}

	ldmsd_req_buf_free(buf);
	return err;
oom:
	errno = ENOMEM;
	if (err)
		json_entity_free(err);
	if (buf)
		ldmsd_req_buf_free(buf);
	return NULL;
}

int ldmsd_smplr_enable(ldmsd_cfgobj_t obj)
{
	ldmsd_smplr_t smplr = (ldmsd_smplr_t)obj;
	if (smplr->state != LDMSD_SMPLR_STATE_STOPPED)
		return EBUSY;
	smplr->state = LDMSD_SMPLR_STATE_RUNNING;
	__sampler_set_info_add(smplr);
	struct timespec to;
	ev_sched_to(&to, smplr->interval_us / 1000000, smplr->offset_us * 1000);
	if (smplr->synchronous)
		to.tv_nsec = smplr->offset_us * 1000;

	EV_DATA(smplr->sample_ev, struct sample_data)->reschedule = 1;
	ev_post(sampler, sampler, smplr->sample_ev, &to);
	return 0;
}

int ldmsd_smplr_disable(ldmsd_cfgobj_t obj)
{
	ldmsd_smplr_t smplr = (ldmsd_smplr_t)obj;
	if (smplr->state != LDMSD_SMPLR_STATE_RUNNING)
		return EBUSY;
	smplr->state = LDMSD_SMPLR_STATE_STOPPED;
	EV_DATA(smplr->sample_ev, struct sample_data)->reschedule = 0;
	return 0;
}

json_entity_t ldmsd_smplr_update(ldmsd_cfgobj_t obj, short enabled,
				json_entity_t dft, json_entity_t spc)
{
	ldmsd_plugin_inst_t inst = NULL;
	long interval_us, offset_us;
	int perm;
	json_entity_t err;
	ldmsd_smplr_t smplr = (ldmsd_smplr_t)obj;

	if (obj->enabled && enabled)
		return ldmsd_result_new(EBUSY, NULL, NULL);

	err = __smplr_attr_get(dft, spc, &inst, &interval_us, &offset_us, &perm);
	if (!err) {
		if (ENOMEM == errno)
			goto oom;
	} else {
		return ldmsd_result_new(EINVAL, 0, err);
	}

	if (inst) {
		ldmsd_plugin_inst_put(inst);
		err = json_dict_build(err, JSON_STRING_VALUE, "plugin",
				"The plugin instance cannot be changed.");
		if (!err)
			goto oom;
	}
	if (err)
		return ldmsd_result_new(EINVAL, 0, err);

	if (LDMSD_ATTR_NA != interval_us)
		smplr->interval_us = interval_us;
	if (LDMSD_ATTR_NA != offset_us)
		smplr->offset_us = offset_us;

	obj->enabled = ((0 <= enabled)?enabled:obj->enabled);
	return ldmsd_result_new(0, NULL, NULL);
oom:
	errno = ENOMEM;
	return NULL;
}

json_entity_t ldmsd_smplr_create(const char *name, short enabled,
				json_entity_t dft, json_entity_t spc,
				uid_t uid, gid_t gid)
{
	int rc, perm;
	ldmsd_plugin_inst_t inst = NULL;
	long interval_us, offset_us;
	json_entity_t err;
	ldmsd_req_buf_t buf;
	ldmsd_smplr_t smplr;
	ev_t sample_ev, start_ev, stop_ev;
	sample_ev = start_ev = stop_ev = NULL;

	buf = ldmsd_req_buf_alloc(1024);
	if (!buf)
		goto oom;

	err = __smplr_attr_get(dft, spc, &inst, &interval_us, &offset_us, &perm);
	if (!err) {
		if (ENOMEM == errno)
			goto oom;
	} else {
		return ldmsd_result_new(EINVAL, 0, err);
	}

	if (!inst) {
		/*
		 * If 'plugin' is given but the plugin instance doesn't exist.
		 * the function won't reach this point.
		 */
		err = json_dict_build(err, JSON_STRING_VALUE, "instance",
						"'plugin' is missing.", -1);
		if (!err)
			goto oom;
	}

	if (LDMSD_ATTR_NA == interval_us) {
		err = json_dict_build(err, JSON_STRING_VALUE, "interval",
						"'interval' is missing.", -1);
		if (!err)
			goto oom;
		if (LDMSD_ATTR_NA != offset_us) {
			err = json_dict_build(err, JSON_STRING_VALUE, "offset",
							"'offset' is ignore because "
							"'interval' isn't given.",
							-1);
			if (!err)
				goto oom;
		}
	} else {
		if (LDMSD_ATTR_NA == offset_us) {
			offset_us = LDMSD_UPDT_HINT_OFFSET_NONE;
		} else if ((offset_us < 0) || (offset_us >= interval_us)) {
			rc = ldmsd_req_buf_append(buf, "The value '%ld' is "
					"either less than 0 or equal or larger "
					"than the interval value '%ld'.",
					offset_us, interval_us);
			if (rc < 0)
				goto oom;
			err = json_dict_build(err, JSON_STRING_VALUE, "offset",
					buf->buf, -1);
		}
	}

	if (LDMSD_ATTR_NA == perm)
		perm = 0770;

	if (err)
		return ldmsd_result_new(EINVAL, 0, err);

	sample_ev = ev_new(smplr_sample_type);
	if (!sample_ev)
		goto oom;
	start_ev = ev_new(smplr_start_type);
	if (!start_ev)
		goto oom;
	stop_ev = ev_new(smplr_stop_type);
	if (!stop_ev)
		goto oom;

	smplr = (ldmsd_smplr_t)ldmsd_cfgobj_new(name, LDMSD_CFGOBJ_SMPLR,
					sizeof(*smplr), ldmsd_smplr___del,
					ldmsd_smplr_update,
					ldmsd_cfgobj_delete,
					ldmsd_smplr_query,
					ldmsd_smplr_export,
					ldmsd_smplr_enable,
					ldmsd_smplr_disable,
					uid, gid, perm, enabled);
	if (!smplr)
		goto oom;
	smplr->interval_us = interval_us;
	smplr->offset_us = offset_us;
	if (offset_us == LDMSD_UPDT_HINT_OFFSET_NONE)
		smplr->synchronous = 0;
	else
		smplr->synchronous = 1;
	smplr->state = LDMSD_SMPLR_STATE_STOPPED;
	smplr->pi = inst;

	EV_DATA(sample_ev, struct sample_data)->smplr = smplr;
	EV_DATA(sample_ev, struct sample_data)->reschedule = 0;
	smplr->sample_ev = sample_ev;
	EV_DATA(start_ev, struct start_data)->entity = smplr;
	smplr->start_ev = start_ev;
	EV_DATA(stop_ev, struct stop_data)->entity = smplr;
	smplr->stop_ev = stop_ev;
	ldmsd_smplr_unlock(smplr);
	if (buf)
		ldmsd_req_buf_free(buf);
	return ldmsd_result_new(0, NULL, NULL);
oom:
	if (buf)
		ldmsd_req_buf_free(buf);
	if (sample_ev)
		ev_put(sample_ev);
	if (stop_ev)
		ev_put(stop_ev);
	if (start_ev)
		ev_put(start_ev);

	return NULL;
}
