/* -*- c-basic-offset: 8 -*-
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

#include "ovis_ev/ev.h"
#include "ldmsd_event.h"

ev_worker_t logger_w;
ev_worker_t configfile_w;
ev_worker_t cfg_w;
ev_worker_t msg_tree_w;

ev_worker_t prdcr_tree_w;
ev_worker_t updtr_tree_w;
ev_worker_t strgp_tree_w;

ev_worker_t *prdcr_pool;
ev_worker_t *strgp_pool;
ev_worker_t *prdset_pool;
ev_worker_t *updtr_pool;

ev_type_t log_type;
ev_type_t recv_rec_type;
ev_type_t reqc_type; /* add to msg_tree, rem to msg_tree, send to cfg */
ev_type_t deferred_start_type;
ev_type_t ib_cfg_type;
ev_type_t ib_rsp_type;
ev_type_t ob_rsp_type;
ev_type_t xprt_term_type;

ev_type_t cfgfile_type;
ev_type_t cfgobj_cfg_type;
ev_type_t cfgobj_rsp_type;

ev_type_t prdcr_connect_type;
ev_type_t prdcr_xprt_type;
ev_type_t prdcr_filter_req_type;

ev_type_t dir_complete_type;
ev_type_t lookup_complete_type;
ev_type_t update_complete_type;

ev_type_t prdset_state_type;
ev_type_t update_hint_type;
ev_type_t prdset_del_type;

ev_type_t updtr_state_type;

ev_type_t cleanup_type;
ev_type_t strgp_cleanup_type;

int default_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t ev)
{
	ldmsd_log(LDMSD_LINFO, "Unhandled Event: type=%s, id=%d, status: %s, src: %s, dst: %s.\n",
		  ev_type_name(ev_type(ev)), ev_type_id(ev_type(ev)), status?"FLUSH":"OK",
				  (src)?ev_worker_name(src):"", (dst)?ev_worker_name(dst):"");
	return 0;
}

extern int log_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t ev);
extern int reqc_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t ev);
extern int recv_rec_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t ev);
extern int msg_tree_xprt_term_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t ev);
extern int deferred_start_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t ev);

extern int recv_cfg_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t ev);

extern int
configfile_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e);
extern int
configfile_resp_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e);

extern int
prdcr_tree_cfg_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t ev);
extern int
prdcr_tree_cfg_rsp_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e);
extern int
prdcr_tree_updtr_state_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e);
extern int
prdcr_cfg_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e);
extern int
prdcr_connect_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e);
extern int
prdcr_xprt_event_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e);
extern int
prdcr_dir_complete_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e);
extern int
prdcr_updtr_state_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e);
extern int
prdcr_tree_prdcr_filter_req_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e);

extern int
prdset_del_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e);
extern int
prdset_updtr_state_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e);
extern int
prdset_lookup_complete_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e);
extern int
prdset_update_complete_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e);
extern int
prdset_update_hint_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e);
extern int
prdset_state_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e);

extern int
updtr_tree_prdset_state_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e);
extern int
updtr_tree_cfg_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t ev);
extern int
updtr_tree_cfg_rsp_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e);
extern int
updtr_cfg_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e);
extern int
updtr_prdset_state_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e);
extern int
updtr_status_prdcr_filter_req_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e);

extern int
strgp_status_prdcr_filter_req_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e);
extern int
strgp_tree_cfg_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e);
extern int
strgp_tree_cfg_rsp_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e);
extern int
strgp_tree_prdset_state_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e);
extern int
strgp_prdset_state_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e);
extern int
strgp_cfg_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e);
extern int
strgp_cleanup_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e);

extern int
strgp_tree_cleanup_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e);

int ldmsd_worker_init(void)
{
	int i;
	char s[128];

	logger_w = ev_worker_new("logger", log_actor);
	if (!logger_w)
		goto enomem;

	configfile_w = ev_worker_new("configfile", default_actor);
	if (!configfile_w)
		return ENOMEM;
	ev_dispatch(configfile_w, cfgfile_type, configfile_actor);

	/* msg_tree */
	msg_tree_w = ev_worker_new("msg_tree", default_actor);
	if (!msg_tree_w)
		goto enomem;

	ev_dispatch(msg_tree_w, reqc_type, reqc_actor);
	ev_dispatch(msg_tree_w, recv_rec_type, recv_rec_actor);
	ev_dispatch(msg_tree_w, xprt_term_type, msg_tree_xprt_term_actor);
	ev_dispatch(msg_tree_w, deferred_start_type, deferred_start_actor);

	/* cfg worker */
	cfg_w = ev_worker_new("cfg", recv_cfg_actor);
	if (!cfg_w)
		goto enomem;

	/* prdcr_tree worker */
	prdcr_tree_w = ev_worker_new("prdcr_tree", default_actor);
	if (!prdcr_tree_w)
		goto enomem;

	ev_dispatch(prdcr_tree_w, ib_cfg_type, prdcr_tree_cfg_actor);
	ev_dispatch(prdcr_tree_w, cfgobj_rsp_type, prdcr_tree_cfg_rsp_actor);
	ev_dispatch(prdcr_tree_w, updtr_state_type, prdcr_tree_updtr_state_actor);
	ev_dispatch(prdcr_tree_w, prdcr_filter_req_type, prdcr_tree_prdcr_filter_req_actor);

	/* prdcr worker pool */
	prdcr_pool = malloc(ldmsd_num_prdcr_workers_get() * sizeof(ev_worker_t));
	if (!prdcr_pool)
		goto enomem;
	for (i = 0; i < ldmsd_num_prdcr_workers_get(); i++) {
		snprintf(s, 127, "prdcr_w_%d", i + 1);
		prdcr_pool[i] = ev_worker_new(s, default_actor);
		if (!prdcr_pool[i])
			goto enomem;

		ev_dispatch(prdcr_pool[i], cfgobj_cfg_type, prdcr_cfg_actor);
		ev_dispatch(prdcr_pool[i], prdcr_connect_type, prdcr_connect_actor);
		ev_dispatch(prdcr_pool[i], prdcr_xprt_type, prdcr_xprt_event_actor);
		ev_dispatch(prdcr_pool[i], dir_complete_type, prdcr_dir_complete_actor);
		ev_dispatch(prdcr_pool[i], updtr_state_type, prdcr_updtr_state_actor);
	}

	prdset_pool = malloc(ldmsd_num_prdset_workers_get() * sizeof(ev_worker_t));
	if (!prdset_pool)
		goto enomem;
	for (i = 0; i < ldmsd_num_prdset_workers_get(); i++) {
		snprintf(s, 127, "prdset_w_%d", i + 1);
		prdset_pool[i] = ev_worker_new(s, default_actor);
		if (!prdset_pool[i])
			goto enomem;

		ev_dispatch(prdset_pool[i], updtr_state_type, prdset_updtr_state_actor);
		ev_dispatch(prdset_pool[i], lookup_complete_type, prdset_lookup_complete_actor);
		ev_dispatch(prdset_pool[i], update_complete_type, prdset_update_complete_actor);
		ev_dispatch(prdset_pool[i], prdset_del_type, prdset_del_actor);
		ev_dispatch(prdset_pool[i], update_hint_type, prdset_update_hint_actor);
		ev_dispatch(prdset_pool[i], prdset_state_type, prdset_state_actor);
	}

	/* updtr_tree worker */
	updtr_tree_w = ev_worker_new("updtr_tree", default_actor);
	if (!updtr_tree_w)
		goto enomem;
	ev_dispatch(updtr_tree_w, ib_cfg_type, updtr_tree_cfg_actor);
	ev_dispatch(updtr_tree_w, cfgobj_rsp_type, updtr_tree_cfg_rsp_actor);
	ev_dispatch(updtr_tree_w, prdset_state_type, updtr_tree_prdset_state_actor);

	updtr_pool = malloc(ldmsd_num_updtr_workers_get() * sizeof(ev_worker_t));
	if (!updtr_pool)
		goto enomem;
	for (i = 0; i < ldmsd_num_updtr_workers_get(); i++) {
		snprintf(s, 127, "updtr_w_%d", i + 1);
		updtr_pool[i] = ev_worker_new(s, default_actor);
		if (!updtr_pool[i])
			goto enomem;
		ev_dispatch(updtr_pool[i], cfgobj_cfg_type, updtr_cfg_actor);
		ev_dispatch(updtr_pool[i], prdset_state_type, updtr_prdset_state_actor);
		ev_dispatch(updtr_pool[i], prdcr_filter_req_type,
				updtr_status_prdcr_filter_req_actor);
	}

	/* strgp_tree worker */
	strgp_tree_w = ev_worker_new("strgp_tree", default_actor);
	if (!strgp_tree_w)
		goto enomem;
	ev_dispatch(strgp_tree_w, ib_cfg_type, strgp_tree_cfg_actor);
	ev_dispatch(strgp_tree_w, cfgobj_rsp_type, strgp_tree_cfg_rsp_actor);
	ev_dispatch(strgp_tree_w, prdset_state_type, strgp_tree_prdset_state_actor);
	ev_dispatch(strgp_tree_w, cleanup_type, strgp_tree_cleanup_actor);

	strgp_pool = malloc(ldmsd_num_strgp_workers_get() * sizeof(ev_worker_t));
	if (!strgp_pool)
		goto enomem;
	for (i = 0; i < ldmsd_num_strgp_workers_get(); i++) {
		snprintf(s, 127, "strgp_w_%d", i + 1);
		strgp_pool[i] = ev_worker_new(s, default_actor);
		if (!strgp_pool[i])
			goto enomem;
		ev_dispatch(strgp_pool[i], cfgobj_cfg_type, strgp_cfg_actor);
		ev_dispatch(strgp_pool[i], prdset_state_type, strgp_prdset_state_actor);
		ev_dispatch(strgp_pool[i], prdcr_filter_req_type, strgp_status_prdcr_filter_req_actor);
		ev_dispatch(strgp_pool[i], strgp_cleanup_type, strgp_cleanup_actor);
	}

	return 0;
enomem:
	return ENOMEM;
}

int ldmsd_ev_init(void)
{
	/* Event type */
	log_type = ev_type_new("ldmsd:log", sizeof(struct log_data));

	cfgfile_type = ev_type_new("ldmsd:configfile", sizeof(struct cfgfile_data));

	xprt_term_type = ev_type_new("ldms:xprt_term", sizeof(struct xprt_term_data));
	recv_rec_type = ev_type_new("ldms_xprt:recv", sizeof(struct recv_rec_data));
	reqc_type = ev_type_new("ldmsd:reqc_ev", sizeof(struct reqc_data));
	deferred_start_type = ev_type_new("ldmsd:deferred_start", 0);
	ib_cfg_type = ev_type_new("recv_cfg", sizeof(struct cfg_data));
	ib_rsp_type = ev_type_new("recv_rsp", sizeof(struct rsp_data));
	ob_rsp_type = ev_type_new("outbound_rsp", sizeof(struct ob_rsp_data));

	cfgobj_cfg_type = ev_type_new("cfgobj_tree:cfgobj:cfg_req", sizeof(struct cfgobj_data));
	cfgobj_rsp_type = ev_type_new("cfgobj:cfgobj_tree:cfg_rsp", sizeof(struct cfgobj_rsp_data));

	dir_complete_type = ev_type_new("ldms_xprt:dir_add", sizeof(struct dir_data));
	lookup_complete_type = ev_type_new("ldms_xprt:lookup_complete",
					  sizeof(struct lookup_data));
	update_complete_type = ev_type_new("ldms_xprt:update_complete",
					  sizeof(struct update_complete_data));

	prdcr_connect_type = ev_type_new("prdcr_connect", sizeof(struct cfgobj_data));
	prdcr_xprt_type = ev_type_new("ldms_xprt_event", sizeof(struct cfgobj_data));

	prdset_state_type = ev_type_new("prdset_state", sizeof(struct prdset_state_data));
	update_hint_type = ev_type_new("prdset_update_hint", sizeof(struct update_hint_data));
	prdset_del_type = ev_type_new("prdset_del", sizeof(struct prdset_data));

	updtr_state_type = ev_type_new("updtr_state", sizeof(struct updtr_state_data));

	prdcr_filter_req_type = ev_type_new("prdcr_list_req", sizeof(struct prdcr_filter_req_data));

	cleanup_type = ev_type_new("cleanup", sizeof(struct cleanup_data));
	strgp_cleanup_type = ev_type_new("strgp_clean", sizeof(struct strgp_data));
	return 0;
}

ev_worker_t __assign_worker(ev_worker_t *pool, int *_idx, int count)
{
	int i = *_idx;
	ev_worker_t w = pool[i];
	i++;
	i = i%count;
	*_idx = i;
	return w;
}

ev_worker_t assign_prdcr_worker()
{
	/*
	 * TODO: Use a more sophisticated than a round-robin
	 */
	static int i = 0;
	return __assign_worker(prdcr_pool, &i, ldmsd_num_prdcr_workers_get());
}

ev_worker_t assign_prdset_worker()
{
	/*
	 * TODO: Use a more sophisticated than a round-robin
	 */
	static int i = 0;
	return __assign_worker(prdset_pool, &i, ldmsd_num_prdset_workers_get());
}

ev_worker_t assign_updtr_worker()
{
	static int i = 0;
	return __assign_worker(updtr_pool, &i, ldmsd_num_updtr_workers_get());
}

ev_worker_t assign_strgp_worker()
{
	static int i = 0;
	return __assign_worker(strgp_pool, &i, ldmsd_num_strgp_workers_get());
}
