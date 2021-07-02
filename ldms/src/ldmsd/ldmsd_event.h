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
extern ev_worker_t msg_tree_w;

/* Event Types and data */
/* LDMSD log */
extern ev_type_t log_type;

extern ev_type_t xprt_term_type;

/* LDMSD messages & request contexts */
extern ev_type_t recv_rec_type;
extern ev_type_t reqc_type; /* add to msg_tree, rem to msg_tree, send to cfg */
extern ev_type_t deferred_start_type;
extern ev_type_t cfg_type;

struct log_data {
	uint8_t is_rotate;
	enum ldmsd_loglevel level;
	char *msg;
	struct timeval tv;
	struct tm tm;
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

struct xprt_term_data {
	ldms_t x;
};

int ldmsd_ev_init(void);
int ldmsd_worker_init(void);

#endif /* __LDMSD_EVENT_H__ */
