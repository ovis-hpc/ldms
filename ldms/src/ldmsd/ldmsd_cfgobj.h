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

#ifndef __LDMSD_CFGOBJ_H_
#define __LDMSD_CFGOBJ_H_

#include "ovis_ref/ref.h"

struct ldmsd_cfgobj_cfg_ctxt {
	struct ref_s ref;
	int is_all;
	int num_sent;
	int num_recv;
	ldmsd_req_ctxt_t reqc;
	struct ldmsd_sec_ctxt sctxt;
};

struct ldmsd_cfgobj_cfg_rsp {
	int errcode;
	char *errmsg;
	void *ctxt;
};

void ldmsd_cfgobj_cfg_ctxt_free(void *args);
int ldmsd_cfgtree_done(struct ldmsd_cfgobj_cfg_ctxt *ctxt);
int ldmsd_cfgtree_post2cfgobj(ldmsd_cfgobj_t obj, ev_worker_t src,
				ev_worker_t dst, ldmsd_req_ctxt_t reqc,
					struct ldmsd_cfgobj_cfg_ctxt *ctxt);
int ldmsd_cfgobj_tree_del_rsp_handler(ldmsd_req_ctxt_t reqc,
					struct ldmsd_cfgobj_cfg_rsp *rsp,
					struct ldmsd_cfgobj_cfg_ctxt *ctxt,
					enum ldmsd_cfgobj_type type);
ev_worker_t ldmsd_cfgobj_worker_get(ldmsd_cfgobj_t obj);
#endif /* __LDMSD_CFGOBJ_H_ */
