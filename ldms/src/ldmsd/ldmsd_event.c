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

ev_type_t log_type;
ev_type_t recv_rec_type;
ev_type_t reqc_type; /* add to msg_tree, rem to msg_tree, send to cfg */
ev_type_t deferred_start_type;
ev_type_t ib_cfg_type;
ev_type_t ib_rsp_type;
ev_type_t ob_rsp_type;
ev_type_t xprt_term_type;

ev_type_t cfgfile_type;

int default_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t ev)
{
	ldmsd_log(LDMSD_LINFO, "Unhandled Event: type=%s, id=%d\n",
		  ev_type_name(ev_type(ev)), ev_type_id(ev_type(ev)));
	ldmsd_log(LDMSD_LINFO, "    status  : %s\n", status ? "FLUSH" : "OK" );
	ldmsd_log(LDMSD_LINFO, "    src     : %s\n", (src)?ev_worker_name(src):"");
	ldmsd_log(LDMSD_LINFO, "    dst     : %s\n", (dst)?ev_worker_name(dst):"");
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

int ldmsd_worker_init(void)
{
	logger_w = ev_worker_new("logger", log_actor);
	if (!logger_w)
		return ENOMEM;

	configfile_w = ev_worker_new("configfile", default_actor);
	if (!configfile_w)
		return ENOMEM;
	ev_dispatch(configfile_w, cfgfile_type, configfile_actor);

	/* msg_tree */
	msg_tree_w = ev_worker_new("msg_tree", default_actor);
	if (!msg_tree_w)
		return ENOMEM;

	ev_dispatch(msg_tree_w, reqc_type, reqc_actor);
	ev_dispatch(msg_tree_w, recv_rec_type, recv_rec_actor);
	ev_dispatch(msg_tree_w, xprt_term_type, msg_tree_xprt_term_actor);
	ev_dispatch(msg_tree_w, deferred_start_type, deferred_start_actor);

	/* cfg worker */
	cfg_w = ev_worker_new("cfg", recv_cfg_actor);
	if (!cfg_w)
		return ENOMEM;
	return 0;
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

	return 0;
}
