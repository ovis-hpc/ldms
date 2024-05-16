/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2024 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2024 Open Grid Computing, Inc. All rights reserved.
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
#ifndef __LDMS_QGROUP_H__
#define __LDMS_QGROUP_H__

#include "ovis_event/ovis_event.h"
#include "coll/rbt.h"
#include "ovis_ref/ref.h"
#include "ldms.h"
#include "ldms_rail.h"
#include "ldms_private.h"

typedef struct ldms_qgroup_member_s *ldms_qgroup_member_t;

struct __qgroup_rep_s {
	STAILQ_ENTRY(__qgroup_rep_s) entry;
	struct ldms_rail_ep_s *rep;
};

struct ldms_qgroup_s {
	struct ref_s ref;
	uint64_t quota; /* our quota */
	ldms_qgroup_state_t state;
	struct ldms_qgroup_cfg_s cfg;
	struct rbt rbt;
	pthread_mutex_t mutex;
	pthread_cond_t  cond;
	struct ovis_event_s ask_oev;
	struct ovis_event_s reset_oev;
	STAILQ_HEAD(, __qgroup_rep_s) eps_stq;
};

struct ldms_qgroup_member_s {
	struct rbn rbn;

	/* config info */
	char   c_host[256]; /* see rfc1034 */
	char   c_port[32];  /* port or service name */
	char   c_xprt[32];  /* the transport type */
	char   c_auth[32];  /* auth type */
	struct attr_value_list *c_auth_av_list;

	ldms_qgroup_t qg;

	pthread_mutex_t mutex;
	pthread_cond_t  cond;

	ldms_rail_t x;
	ldms_qgroup_member_state_t state;
	struct ref_s ref;
};

struct ldms_rail_ep_s;

/* no need to expose these */
int ldms_qgroup_add_rep(struct ldms_rail_ep_s *rep);
int ldms_qgroup_quota_acquire(uint64_t q);
int ldms_qgroup_quota_release(uint64_t q);

#endif
