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
	else if (type == LDMSD_SET_ORIGIN_SAMP_PI)
		return "sampler plugin";
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
	extern struct plugin_list plugin_list;
	ldmsd_set_info_t info;
	struct ldms_timestamp t;
	struct timeval dur;
	struct ldmsd_plugin_set_list *plugn_set_list;
	struct ldmsd_plugin_set *plugn_set = NULL;
	struct ldmsd_plugin_cfg *pi;

	ldms_set_t lset = ldms_set_by_name(inst_name);
	if (!lset)
		return NULL;

	info = calloc(1, sizeof(*info));
	if (!info)
		return NULL;

	info->set = lset;
	/* Determine if the set is responsible by a sampler plugin */
	ldmsd_set_tree_lock();
	plugn_set_list = ldmsd_plugin_set_list_first();
	while (plugn_set_list) {
		LIST_FOREACH(plugn_set, &plugn_set_list->list, entry) {
			if (0 == strcmp(plugn_set->inst_name, inst_name)) {
				break;
			}
		}
		if (plugn_set)
			break;
 		plugn_set_list = ldmsd_plugin_set_list_next(plugn_set_list);
	}
	ldmsd_set_tree_unlock();
	if (plugn_set) {
		/* The set is created by a sampler plugin */
		pi = ldmsd_get_plugin(plugn_set->plugin_name);
		if (!pi) {
			ldmsd_log(LDMSD_LERROR, "Set '%s' is created by "
					"an unloaded plugin '%s'\n",
					inst_name, plugn_set->plugin_name);
		} else {
			pi->ref_count++;
			info->interval_us = pi->sample_interval_us;
			info->offset_us = pi->sample_offset_us;
			info->sync = pi->synchronous;
			info->pi = pi;
		}
		info->origin_name = strdup(plugn_set->plugin_name);
		info->origin_type = LDMSD_SET_ORIGIN_SAMP_PI;

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
			ldmsd_prdcr_set_ref_get(prd_set);
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
		ldmsd_prdcr_set_ref_put(info->prd_set);
		info->prd_set = NULL;
	}
	if (info->pi) {
		info->pi->ref_count--;
		info->pi = NULL;
	}
	free(info);
}

int __sampler_set_info_add(struct ldmsd_plugin *pi, char *interval, char *offset)
{
	ldmsd_plugin_set_t set;
	int rc;
	long interval_us;
	long offset_us;

	if (pi->type != LDMSD_PLUGIN_SAMPLER)
		return EINVAL;
	if (!interval)
		return EINVAL;
	interval_us = strtol(interval, NULL, 0);
	if (offset)
		offset_us = strtol(offset, NULL, 0);
	else
		offset_us = LDMSD_UPDT_HINT_OFFSET_NONE;
	for (set = ldmsd_plugin_set_first(pi->name); set;
				set = ldmsd_plugin_set_next(set)) {
		rc = ldmsd_set_update_hint_set(set->set, interval_us, offset_us);
		if (rc) {
			ldmsd_log(LDMSD_LERROR, "Error %d: Failed to add "
					"the update hint to set '%s'\n",
					rc, ldms_set_instance_name_get(set->set));
			return rc;
		}
	}
	return 0;
}

static void stop_sampler(struct ldmsd_plugin_cfg *pi)
{
	EV_DATA(pi->sample_ev, struct sample_data)->reschedule = 0;
	pi->ref_count--;
}

int sample_actor(ev_worker_t src, ev_worker_t dst, ev_t ev)
{
	struct ldmsd_plugin_cfg *pi = EV_DATA(ev, struct sample_data)->pi;
	pthread_mutex_lock(&pi->lock);
	assert(pi->plugin->type == LDMSD_PLUGIN_SAMPLER);
	int rc = pi->sampler->sample(pi->sampler);
	if (rc) {
		/*
		 * If the sampler reports an error don't reschedule
		 * the timeout. This is an indication of a configuration
		 * error that needs to be corrected.
		*/
		ldmsd_log(LDMSD_LERROR, "'%s': failed to sample. Stopping "
				"the plug-in.\n", pi->name);
	} else {
		struct timespec to;
		clock_gettime(CLOCK_REALTIME, &to);
		to.tv_sec += pi->sample_interval_us / 1000000;
		to.tv_nsec = pi->sample_offset_us * 1000;
		ev_post(src, dst, ev, &to);
	}
	pthread_mutex_unlock(&pi->lock);
}

/*
 * Start the sampler
 */
int ldmsd_start_sampler(char *plugin_name, char *interval, char *offset)
{
	char *endptr;
	int rc = 0;
	unsigned long sample_interval;
	long sample_offset = 0;
	int synchronous = 0;
	struct ldmsd_plugin_cfg *pi;

	sample_interval = strtoul(interval, &endptr, 0);
	if (endptr[0] != '\0')
		return EINVAL;

	pi = ldmsd_get_plugin((char *)plugin_name);
	if (!pi)
		return ENOENT;

	pthread_mutex_lock(&pi->lock);
	if (pi->plugin->type != LDMSD_PLUGIN_SAMPLER) {
		rc = EINVAL;
		goto out;
	}

	rc = __sampler_set_info_add(pi->plugin, interval, offset);
	if (rc)
		goto out;

	pi->sample_interval_us = sample_interval;
	if (offset) {
		sample_offset = strtol(offset, NULL, 0);
		if ( !((sample_interval >= 10) &&
		       (sample_interval >= labs(sample_offset)*2)) ){
			rc = EDOM;
			goto out;
		}
		pi->synchronous = 1;
		pi->sample_offset_us = sample_offset;
	}

	struct timespec to;

	clock_gettime(CLOCK_REALTIME, &to);
	if (pi->synchronous) {
		to.tv_sec += 1;
		to.tv_nsec = sample_offset;
	}

	pi->ref_count++;

	if (!pi->sample_ev) {
		pi->sample_ev = ev_new(smplr_sample_type);
		EV_DATA(pi->sample_ev, struct sample_data)->pi = pi;
	}
	ev_post(sampler, sampler, pi->sample_ev, &to);
out:
	pthread_mutex_unlock(&pi->lock);
	return rc;
}

ev_worker_t oneshot_worker;
struct oneshot {
	struct ldmsd_plugin_cfg *pi;
};
ev_type_t oneshot_event;

static int oneshot_actor(ev_worker_t src, ev_worker_t dst, ev_t ev)
{
	struct ldmsd_plugin_cfg *pi = EV_DATA(ev, struct oneshot)->pi;
	pthread_mutex_lock(&pi->lock);
	assert(pi->plugin->type == LDMSD_PLUGIN_SAMPLER);
	pi->sampler->sample(pi->sampler);
	pi->ref_count--;
	pthread_mutex_unlock(&pi->lock);
	/* Put the create reference on the event */
	ev_put(ev);
}

int ldmsd_oneshot_sample(const char *plugin_name, const char *ts,
			 char *errstr, size_t errlen)
{
	int rc = 0;
	struct ldmsd_plugin_cfg *pi;
	struct timespec to, now;

	if (!oneshot_event)
		oneshot_event = ev_type_new("smplr:oneshot", sizeof(struct oneshot));
	if (!oneshot_worker)
		oneshot_worker = ev_worker_new("smplr:oneshot", oneshot_actor);
	if (!oneshot_event || !oneshot_worker) {
		rc = ENOMEM;;
		snprintf(errstr, errlen,
			 "%s: memory allocation failure.", __func__);
		goto out;
	}

	clock_gettime(CLOCK_REALTIME, &now);
	if (0 == strncmp(ts, "now", 3)) {
		to = now;
	} else {
		uint32_t sched = strtoul(ts, NULL, 0);
		to = now;
		to.tv_sec += sched;
	}
	to.tv_nsec = 0;

	pi = ldmsd_get_plugin((char *)plugin_name);
	if (!pi) {
		rc = ENOENT;
		snprintf(errstr, errlen, "Sampler not found.");
		return rc;
	}
	pthread_mutex_lock(&pi->lock);
	if (pi->plugin->type != LDMSD_PLUGIN_SAMPLER) {
		rc = EINVAL;
		snprintf(errstr, errlen,
			 "The specified plugin is not a sampler.");
		goto out;
	}
	pi->ref_count++;

	ev_t os_ev = ev_new(oneshot_event);
	EV_DATA(os_ev, struct oneshot)->pi = pi;
	ev_post(oneshot_worker, oneshot_worker, os_ev, &to);
out:
	pthread_mutex_unlock(&pi->lock);
	return rc;
}

/*
 * Stop the sampler
 */
int ldmsd_stop_sampler(char *plugin_name)
{
	int rc = 0;
	struct ldmsd_plugin_cfg *pi;

	pi = ldmsd_get_plugin(plugin_name);
	if (!pi)
		return ENOENT;
	pthread_mutex_lock(&pi->lock);
	/* Ensure this is a sampler */
	if (pi->plugin->type != LDMSD_PLUGIN_SAMPLER) {
		rc = EINVAL;
		goto out;
	}
	if (pi->sample_ev) {
		EV_DATA(pi->sample_ev, struct sample_data)->reschedule = 0;
	} else {
		rc = -EBUSY;
	}
out:
	pthread_mutex_unlock(&pi->lock);
	return rc;
}

