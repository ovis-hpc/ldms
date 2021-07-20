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
#ifndef __LDMSD_EVENT_H__
#define __LDMSD_EVENT_H__
#include "ovis_json/ovis_json.h"
#include "ovis_ev/ev.h"
#include "ldmsd_stream.h"
#include "ldmsd_request.h"
#include "ldmsd.h"


/* Worker */
extern ev_worker_t logger_w;
extern ev_worker_t configfile_w;
extern ev_worker_t msg_tree_w;
extern ev_worker_t cfg_w;

extern ev_worker_t prdcr_tree_w;
extern ev_worker_t updtr_tree_w;
extern ev_worker_t strgp_tree_w;
extern ev_worker_t failover_w;

/* Pool of workers that own the same type of resources */
extern ev_worker_t *prdcr_pool;
extern ev_worker_t *prdset_pool;
extern ev_worker_t *updtr_pool;
extern ev_worker_t *strgp_pool;

/* Event Types and data */
struct log_data {
	uint8_t is_rotate;
	enum ldmsd_loglevel level;
	char *msg;
	struct timeval tv;
	struct tm tm;
};

typedef int (*req_filter_fn_t)(ldmsd_cfg_xprt_t, ldmsd_req_hdr_t, void *);
struct cfgfile_data {
	FILE *fin;
	char *path;
	int trust;
	req_filter_fn_t filter_fn;
	void *ctxt;
};

struct recv_rec_data {
	ldmsd_req_hdr_t rec;
	struct ldmsd_cfg_xprt_s xprt;
};

struct reqc_data {
	enum {
		REQC_EV_TYPE_ADD = 1,
		REQC_EV_TYPE_REM,
		REQC_EV_TYPE_CFG
	} type;
	ldmsd_req_ctxt_t reqc;
};

struct cfg_data { /* TODO: rename this to recv_cfg_data */
	ldmsd_req_ctxt_t reqc;
	void *ctxt;
};

struct rsp_data { /* TODO: rename this to recv_rsp_data */
	ldmsd_req_cmd_t rcmd;
	void *ctxt;
};

/* Outbound response data */
struct ob_rsp_data {
	ldmsd_req_hdr_t hdr;
	void *ctxt;
};

struct cfgobj_data {
	ldmsd_cfgobj_t obj;
	void *ctxt;
};

struct cfgobj_rsp_data {
	void *rsp; /* Response of each cfgobj */
	void *ctxt; /* Context from the cfgobj_cfg event */
};

struct xprt_term_data {
	ldms_t x;
};

struct prdset_info {
	struct ref_s ref;
	char *inst_name;
	char *schema_name;
	char *prdcr_name;
	ev_worker_t prdset_worker;
	ldms_set_t set_snapshot;
	void *ctxt;
};

struct prdset_state_data {
	enum ldmsd_prdcr_set_state state;
	struct prdset_info *prdset_info;
	void *obj;
};

struct prdset_data {
	ldmsd_prdcr_set_t prdset;
};

struct update_hint_data {
	ldmsd_prdcr_set_t prdset;
	struct ldmsd_updtr_schedule hint;
};

struct updtr_info {
	struct ref_s ref;
	char *name;
	struct ldmsd_updtr_schedule sched;
	uint8_t push_flags;
	uint8_t is_auto;
	struct ldmsd_match_queue prdcr_list;
	struct ldmsd_match_queue match_list;
};

struct updtr_state_data {
	enum ldmsd_updtr_state state;
	struct updtr_info *updtr_info;
	void *obj; /* Object corresponding to the worker that would handle the event */
	void *ctxt; /* Context for \c obj to use */
};

struct strgp_state_data {
	enum ldmsd_strgp_state state;
	void *obj;
};

struct strgp_data {
	ldmsd_strgp_t strgp;
	void *ctxt;
};

struct dir_data {
	ldms_dir_t dir;
	ldmsd_prdcr_t prdcr;
	ldmsd_prdcr_set_t prdset;
	struct ldmsd_updtr_schedule hint;
};

struct lookup_data {
	enum ldms_lookup_status status;
	int more;
	ldms_set_t set;
	ldmsd_prdcr_set_t prdset;
	struct updtr_info *updtr_info;
};

struct prdcr_filter_req_data {
	struct ldmsd_match_queue filter;
	ev_worker_t resp_worker; /* Worker that subscribes to the response */
	struct ldmsd_match_queue prdcr_list; /* List of producer name matched the filter */
	struct ldmsd_cfgobj_cfg_rsp *rsp;
	struct ldmsd_cfgobj_cfg_ctxt *cfg_ctxt;
};

struct update_complete_data {
	int status;
	ldms_set_t set;
	ldmsd_prdcr_set_t prdset;
};

typedef void (*proc_exit_fn)(void *args);
struct cleanup_data {
	proc_exit_fn exit_fn;
	void *exit_args;
};

typedef struct ldmsd_failover *ldmsd_failover_t;
struct failover_data {
	ldmsd_failover_t f;
	void *ctxt;
};

/* Event Types */
/* LDMSD log */
extern ev_type_t log_type;

/* LDMSD messages & request contexts */
extern ev_type_t recv_rec_type;
extern ev_type_t reqc_type; /* add to msg_tree, rem to msg_tree, send to cfg */
extern ev_type_t deferred_start_type;
extern ev_type_t ib_cfg_type; /* Inbound request event type */
extern ev_type_t ib_rsp_type; /* Inbound response event type */
extern ev_type_t ob_rsp_type; /* Outbound response event type */

extern ev_type_t xprt_term_type;

extern ev_type_t deferred_start_type;

/* ldms xprt */
extern ev_type_t dir_complete_type;
extern ev_type_t lookup_complete_type;
extern ev_type_t update_complete_type;


/* Configuration */
extern ev_type_t cfgobj_cfg_type;
extern ev_type_t cfgobj_rsp_type;
extern ev_type_t cfgfile_type;

/* Producers */
extern ev_type_t prdcr_connect_type;
extern ev_type_t prdcr_xprt_type;
extern ev_type_t prdcr_filter_req_type;

/* producer sets */
extern ev_type_t prdset_state_type;
extern ev_type_t update_hint_type;
extern ev_type_t prdset_del_type;

/* Updaters */
extern ev_type_t updtr_state_type;

extern ev_type_t cleanup_type;
extern ev_type_t strgp_cleanup_type;

/* Failover */
extern ev_type_t failover_routine_type;
extern ev_type_t failover_xprt_type;

int ldmsd_ev_init(void);
int ldmsd_worker_init(void);
#endif /* __LDMSD_EVENT_H__ */
