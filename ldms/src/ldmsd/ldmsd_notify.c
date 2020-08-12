/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2020 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2020 Open Grid Computing, Inc. All rights reserved.
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

/**
 * \file ldmsd_notify.c
 *
 * This file contains code related to:
 * 1) processing received notifications,
 * 2) managing notification clients (other ldmsds), and
 * 3) sending notification messages to notification clients (other ldmsds).
 */
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <coll/rbt.h>
#include <pthread.h>
#include <ovis_util/util.h>
#include <json/json_util.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_plugin.h"
#include "ldmsd_sampler.h"
#include "ldmsd_store.h"
#include "ldmsd_request.h"
#include "ldmsd_stream.h"
#include "ldms_xprt.h"
#include "ldmsd_notify.h"

/* 1 to enable __DLOG, 0 to disable it */
#if 0
#define __DLOG(fmt, ...) ldmsd_lall(fmt, ##__VA_ARGS__)
#else
#define __DLOG(fmt, ...) /* no-op */
#endif

static int __notify_update_hint_handler(ldmsd_req_ctxt_t reqc);

static struct notify_handle_entry_s notify_handler_tbl[] = {
	{ "update_hint", __notify_update_hint_handler, 0110 },
};

static int __notify_handle_ent_cmp(const void *_key, const void *_ent)
{
	const char *key = _key;
	const struct notify_handle_entry_s *ent = _ent;
	return strcmp(key, ent->type);
}

notify_handle_entry_t ldmsd_notify_handler_find(const char *notify_type)
{
	notify_handle_entry_t ent = bsearch(notify_type, notify_handler_tbl,
			  ARRAY_SIZE(notify_handler_tbl), sizeof(*ent),
			  __notify_handle_ent_cmp);
	return ent;
}

static int __notify_update_hint_handler(ldmsd_req_ctxt_t reqc)
{
	long interval = 0, offset = LDMSD_UPDT_HINT_OFFSET_NONE;
	json_entity_t jent;
	ldms_set_t set;
	ldmsd_prdcr_set_t prd_set;
	ldmsd_set_ctxt_t set_ctxt;
	int rc;

	__DLOG("DEBUG: update_hint handler\n");

	/* interval */
	jent = json_value_find(reqc->json, "interval");
	if (!jent) {
		ldmsd_lwarning("`interval` is missing from `update_hint` notification.\n");
		return ENOENT;
	}
	if (jent->type != JSON_INT_VALUE) {
		ldmsd_lwarning("`interval` in `update_hint` notification is not an integer.\n");
		return EINVAL;
	}
	interval = jent->value.int_;

	/* offset */
	jent = json_value_find(reqc->json, "offset");
	if (jent) {
		if (jent->type != JSON_INT_VALUE) {
			ldmsd_lwarning("`offset` in `update_hint` notification is not an integer.\n");
			return EINVAL;
		}
		offset = jent->value.int_;
	}
	/* it is OK if offset is not set */

	/* set */
	jent = json_value_find(reqc->json, "set");
	if (!jent) {
		ldmsd_lwarning("`set` is missing from `update_hint` notification.\n");
		return ENOENT;
	}
	if (jent->type != JSON_STRING_VALUE) {
		ldmsd_lwarning("`set` in `update_hint` notification is not a string.\n");
		return EINVAL;
	}
	set = ldms_set_by_name(jent->value.str_->str);
	if (!set) {
		/* the set may no longer exist when the message arrived */
		return 0;
	}
	set_ctxt = ldms_ctxt_get(set);
	if (!set_ctxt) {
		rc = EINVAL;
		goto err;
	}
	if (set_ctxt->type != LDMSD_SET_CTXT_PRDCR) {
		rc = EINVAL;
		goto err;
	}
	prd_set = container_of(set_ctxt, struct ldmsd_prdcr_set, set_ctxt);
	prd_set->updt_hint.intrvl_us = interval;
	prd_set->updt_hint.offset_us = offset;
	__DLOG("DEBUG: update_hint succeeded\n");
	return 0;
 err:
	ldms_set_put(set);
	return rc;
}
