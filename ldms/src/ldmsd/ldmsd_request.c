/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015-2020 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2015-2020 Open Grid Computing, Inc. All rights reserved.
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
#include "ldmsd_request.h"
#include "ldmsd_stream.h"
#include "ldms_xprt.h"

#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
/*
 * This file implements an LDMSD control protocol. The protocol is
 * message oriented and has message boundary markers.
 *
 * Every message has a unique msg_no identifier. Every record that is
 * part of the same message has the same msg_no value. The flags field
 * is a bit field as follows:
 *
 * 1 - Start of Message
 * 2 - End of Message
 *
 * The rec_len field is the size of the record including the header.
 * It is assumed that when reading from the socket that the next
 * message starts at cur_ptr + rec_len when cur_ptr starts at 0 and is
 * incremented by the read length for each socket operation.
 *
 * When processing protocol records, the header is stripped off and
 * all reqresp strings that share the same msg_no are concatenated
 * together until the record in which flags | End of Message is True
 * is received and then delivered to the ULP as a single message
 *
 */

pthread_mutex_t msg_tree_lock = PTHREAD_MUTEX_INITIALIZER;

int ldmsd_req_debug = 0; /* turn on / off using gdb or edit src to
                                 * see request/response debugging messages */

static int cleanup_requested = 0;

void __ldmsd_log(enum ldmsd_loglevel level, const char *fmt, va_list ap);

__attribute__((format(printf, 1, 2)))
static inline
void __dlog(const char *fmt, ...)
{
	if (!ldmsd_req_debug)
		return;
	va_list ap;
	va_start(ap, fmt);
	__ldmsd_log(LDMSD_LALL, fmt, ap);
	va_end(ap);
}


__attribute__((format(printf, 3, 4)))
size_t Snprintf(char **dst, size_t *len, char *fmt, ...);

static int msg_comparator(void *a, const void *b)
{
	msg_key_t ak = (msg_key_t)a;
	msg_key_t bk = (msg_key_t)b;
	int rc;

	rc = ak->conn_id - bk->conn_id;
	if (rc)
		return rc;
	return ak->msg_no - bk->msg_no;
}
struct rbt msg_tree = RBT_INITIALIZER(msg_comparator);

static
void ldmsd_req_ctxt_sec_get(ldmsd_req_ctxt_t rctxt, ldmsd_sec_ctxt_t sctxt)
{
	if (rctxt->xprt->xprt) {
		ldms_xprt_cred_get(rctxt->xprt->xprt, NULL, &sctxt->crd);
	} else {
		ldmsd_sec_ctxt_get(sctxt);
	}
}

typedef int
(*ldmsd_request_handler_t)(ldmsd_req_ctxt_t req_ctxt);
struct request_handler_entry {
	int req_id;
	ldmsd_request_handler_t handler;
	int flag; /* Lower 12 bit (mask 0777) for request permisson.
		   * The rest is reserved for ldmsd_request use. */
};

static int example_handler(ldmsd_req_ctxt_t req_ctxt);
static int prdcr_add_handler(ldmsd_req_ctxt_t req_ctxt);
static int prdcr_del_handler(ldmsd_req_ctxt_t req_ctxt);
static int prdcr_start_handler(ldmsd_req_ctxt_t req_ctxt);
static int prdcr_stop_handler(ldmsd_req_ctxt_t req_ctxt);
static int prdcr_start_regex_handler(ldmsd_req_ctxt_t req_ctxt);
static int prdcr_stop_regex_handler(ldmsd_req_ctxt_t req_ctxt);
static int prdcr_status_handler(ldmsd_req_ctxt_t req_ctxt);
static int prdcr_set_status_handler(ldmsd_req_ctxt_t req_ctxt);
static int prdcr_subscribe_regex_handler(ldmsd_req_ctxt_t req_ctxt);
static int strgp_add_handler(ldmsd_req_ctxt_t req_ctxt);
static int strgp_del_handler(ldmsd_req_ctxt_t req_ctxt);
static int strgp_start_handler(ldmsd_req_ctxt_t req_ctxt);
static int strgp_stop_handler(ldmsd_req_ctxt_t req_ctxt);
static int strgp_prdcr_add_handler(ldmsd_req_ctxt_t req_ctxt);
static int strgp_prdcr_del_handler(ldmsd_req_ctxt_t req_ctxt);
static int strgp_metric_add_handler(ldmsd_req_ctxt_t req_ctxt);
static int strgp_metric_del_handler(ldmsd_req_ctxt_t req_ctxt);
static int strgp_status_handler(ldmsd_req_ctxt_t req_ctxt);
static int updtr_add_handler(ldmsd_req_ctxt_t req_ctxt);
static int updtr_del_handler(ldmsd_req_ctxt_t req_ctxt);
static int updtr_prdcr_add_handler(ldmsd_req_ctxt_t req_ctxt);
static int updtr_prdcr_del_handler(ldmsd_req_ctxt_t req_ctxt);
static int updtr_match_add_handler(ldmsd_req_ctxt_t req_ctxt);
static int updtr_match_del_handler(ldmsd_req_ctxt_t req_ctxt);
static int updtr_start_handler(ldmsd_req_ctxt_t req_ctxt);
static int updtr_stop_handler(ldmsd_req_ctxt_t req_ctxt);
static int updtr_status_handler(ldmsd_req_ctxt_t req_ctxt);
static int plugn_start_handler(ldmsd_req_ctxt_t req_ctxt);
static int plugn_stop_handler(ldmsd_req_ctxt_t req_ctxt);
static int plugn_status_handler(ldmsd_req_ctxt_t req_ctxt);
static int plugn_load_handler(ldmsd_req_ctxt_t req_ctxt);
static int plugn_term_handler(ldmsd_req_ctxt_t req_ctxt);
static int plugn_config_handler(ldmsd_req_ctxt_t req_ctxt);
static int plugn_list_handler(ldmsd_req_ctxt_t req_ctxt);
static int plugn_sets_handler(ldmsd_req_ctxt_t req_ctxt);
static int set_udata_handler(ldmsd_req_ctxt_t req_ctxt);
static int set_udata_regex_handler(ldmsd_req_ctxt_t req_ctxt);
static int verbosity_change_handler(ldmsd_req_ctxt_t reqc);
static int daemon_status_handler(ldmsd_req_ctxt_t reqc);
static int version_handler(ldmsd_req_ctxt_t reqc);
static int env_handler(ldmsd_req_ctxt_t req_ctxt);
static int include_handler(ldmsd_req_ctxt_t req_ctxt);
static int oneshot_handler(ldmsd_req_ctxt_t req_ctxt);
static int logrotate_handler(ldmsd_req_ctxt_t req_ctxt);
static int exit_daemon_handler(ldmsd_req_ctxt_t req_ctxt);
static int greeting_handler(ldmsd_req_ctxt_t req_ctxt);
static int set_route_handler(ldmsd_req_ctxt_t req_ctxt);
static int unimplemented_handler(ldmsd_req_ctxt_t req_ctxt);
static int eperm_handler(ldmsd_req_ctxt_t req_ctxt);
static int ebusy_handler(ldmsd_req_ctxt_t reqc);
static int updtr_task_status_handler(ldmsd_req_ctxt_t req_ctxt);
static int prdcr_hint_tree_status_handler(ldmsd_req_ctxt_t reqc);

/* these are implemented in ldmsd_failover.c */
int failover_config_handler(ldmsd_req_ctxt_t req_ctxt);
int failover_peercfg_start_handler(ldmsd_req_ctxt_t req_ctxt);
int failover_peercfg_stop_handler(ldmsd_req_ctxt_t req_ctxt);
int failover_mod_handler(ldmsd_req_ctxt_t req_ctxt);
int failover_status_handler(ldmsd_req_ctxt_t req_ctxt);
int failover_pair_handler(ldmsd_req_ctxt_t req_ctxt);
int failover_reset_handler(ldmsd_req_ctxt_t req_ctxt);
int failover_cfgprdcr_handler(ldmsd_req_ctxt_t req_ctxt);
int failover_cfgupdtr_handler(ldmsd_req_ctxt_t req_ctxt);
int failover_cfgstrgp_handler(ldmsd_req_ctxt_t req_ctxt);
int failover_ping_handler(ldmsd_req_ctxt_t req_ctxt);
int failover_peercfg_handler(ldmsd_req_ctxt_t req);

int failover_start_handler(ldmsd_req_ctxt_t req_ctxt);
int failover_stop_handler(ldmsd_req_ctxt_t req_ctxt);

static int setgroup_add_handler(ldmsd_req_ctxt_t req_ctxt);
static int setgroup_mod_handler(ldmsd_req_ctxt_t req_ctxt);
static int setgroup_del_handler(ldmsd_req_ctxt_t req_ctxt);
static int setgroup_ins_handler(ldmsd_req_ctxt_t req_ctxt);
static int setgroup_rm_handler(ldmsd_req_ctxt_t req_ctxt);

static int stream_publish_handler(ldmsd_req_ctxt_t req_ctxt);
static int stream_subscribe_handler(ldmsd_req_ctxt_t reqc);

static int listen_handler(ldmsd_req_ctxt_t reqc);

static int auth_add_handler(ldmsd_req_ctxt_t reqc);
static int auth_del_handler(ldmsd_req_ctxt_t reqc);

/* executable for all */
#define XALL 0111
/* executable for user, and group */
#define XUG 0110

static struct request_handler_entry request_handler[] = {
	[LDMSD_EXAMPLE_REQ] = { LDMSD_EXAMPLE_REQ, example_handler, XALL },

	/* PRDCR */
	[LDMSD_PRDCR_ADD_REQ] = {
		LDMSD_PRDCR_ADD_REQ, prdcr_add_handler, XUG
	},
	[LDMSD_PRDCR_DEL_REQ] = {
		LDMSD_PRDCR_DEL_REQ, prdcr_del_handler, XUG
	},
	[LDMSD_PRDCR_START_REQ] = {
		LDMSD_PRDCR_START_REQ, prdcr_start_handler, XUG
	},
	[LDMSD_PRDCR_STOP_REQ] = {
		LDMSD_PRDCR_STOP_REQ, prdcr_stop_handler, XUG
	},
	[LDMSD_PRDCR_STATUS_REQ] = {
		LDMSD_PRDCR_STATUS_REQ, prdcr_status_handler,
		XALL | LDMSD_PERM_FAILOVER_ALLOWED
	},
	[LDMSD_PRDCR_SET_REQ] = {
		LDMSD_PRDCR_SET_REQ, prdcr_set_status_handler,
		XUG | LDMSD_PERM_FAILOVER_ALLOWED
	},
	[LDMSD_PRDCR_START_REGEX_REQ] = {
		LDMSD_PRDCR_START_REGEX_REQ, prdcr_start_regex_handler, XUG
	},
	[LDMSD_PRDCR_STOP_REGEX_REQ] = {
		LDMSD_PRDCR_STOP_REGEX_REQ, prdcr_stop_regex_handler, XUG
	},
	[LDMSD_PRDCR_HINT_TREE_REQ] = {
		LDMSD_PRDCR_HINT_TREE_REQ, prdcr_hint_tree_status_handler,
		XALL | LDMSD_PERM_FAILOVER_ALLOWED
	},
	[LDMSD_PRDCR_SUBSCRIBE_REQ] = {
		LDMSD_PRDCR_SUBSCRIBE_REQ, prdcr_subscribe_regex_handler,
		XUG | LDMSD_PERM_FAILOVER_ALLOWED
	},

	/* STRGP */
	[LDMSD_STRGP_ADD_REQ] = {
		LDMSD_STRGP_ADD_REQ, strgp_add_handler, XUG
	},
	[LDMSD_STRGP_DEL_REQ]  = {
		LDMSD_STRGP_DEL_REQ, strgp_del_handler, XUG
	},
	[LDMSD_STRGP_PRDCR_ADD_REQ] = {
		LDMSD_STRGP_PRDCR_ADD_REQ, strgp_prdcr_add_handler, XUG
	},
	[LDMSD_STRGP_PRDCR_DEL_REQ] = {
		LDMSD_STRGP_PRDCR_DEL_REQ, strgp_prdcr_del_handler, XUG
	},
	[LDMSD_STRGP_METRIC_ADD_REQ] = {
		LDMSD_STRGP_METRIC_ADD_REQ, strgp_metric_add_handler, XUG
	},
	[LDMSD_STRGP_METRIC_DEL_REQ] = {
		LDMSD_STRGP_METRIC_DEL_REQ, strgp_metric_del_handler, XUG
	},
	[LDMSD_STRGP_START_REQ] = {
		LDMSD_STRGP_START_REQ, strgp_start_handler, XUG
	},
	[LDMSD_STRGP_STOP_REQ] = {
		LDMSD_STRGP_STOP_REQ, strgp_stop_handler, XUG
	},
	[LDMSD_STRGP_STATUS_REQ] = {
		LDMSD_STRGP_STATUS_REQ, strgp_status_handler,
		XALL | LDMSD_PERM_FAILOVER_ALLOWED
	},

	/* UPDTR */
	[LDMSD_UPDTR_ADD_REQ] = {
		LDMSD_UPDTR_ADD_REQ, updtr_add_handler, XUG
	},
	[LDMSD_UPDTR_DEL_REQ] = {
		LDMSD_UPDTR_DEL_REQ, updtr_del_handler, XUG
	},
	[LDMSD_UPDTR_PRDCR_ADD_REQ] = {
		LDMSD_UPDTR_PRDCR_ADD_REQ, updtr_prdcr_add_handler, XUG
	},
	[LDMSD_UPDTR_PRDCR_DEL_REQ] = {
		LDMSD_UPDTR_PRDCR_DEL_REQ, updtr_prdcr_del_handler, XUG
	},
	[LDMSD_UPDTR_START_REQ] = {
		LDMSD_UPDTR_START_REQ, updtr_start_handler, XUG
	},
	[LDMSD_UPDTR_STOP_REQ] = {
		LDMSD_UPDTR_STOP_REQ, updtr_stop_handler, XUG
	},
	[LDMSD_UPDTR_MATCH_ADD_REQ] = {
		LDMSD_UPDTR_MATCH_ADD_REQ, updtr_match_add_handler, XUG
	},
	[LDMSD_UPDTR_MATCH_DEL_REQ] = {
		LDMSD_UPDTR_MATCH_DEL_REQ, updtr_match_del_handler, XUG
	},
	[LDMSD_UPDTR_STATUS_REQ] = {
		LDMSD_UPDTR_STATUS_REQ, updtr_status_handler,
		XALL | LDMSD_PERM_FAILOVER_ALLOWED
	},
	[LDMSD_UPDTR_TASK_REQ] = {
		LDMSD_UPDTR_TASK_REQ, updtr_task_status_handler,
		XALL | LDMSD_PERM_FAILOVER_ALLOWED
	},

	/* PLUGN */
	[LDMSD_PLUGN_START_REQ] = {
		LDMSD_PLUGN_START_REQ, plugn_start_handler, XUG
	},
	[LDMSD_PLUGN_STOP_REQ] = {
		LDMSD_PLUGN_STOP_REQ, plugn_stop_handler, XUG
	},
	[LDMSD_PLUGN_STATUS_REQ] = {
		LDMSD_PLUGN_STATUS_REQ, plugn_status_handler,
		XALL | LDMSD_PERM_FAILOVER_ALLOWED
	},
	[LDMSD_PLUGN_LOAD_REQ] = {
		LDMSD_PLUGN_LOAD_REQ, plugn_load_handler, XUG
	},
	[LDMSD_PLUGN_TERM_REQ] = {
		LDMSD_PLUGN_TERM_REQ, plugn_term_handler, XUG
	},
	[LDMSD_PLUGN_CONFIG_REQ] = {
		LDMSD_PLUGN_CONFIG_REQ, plugn_config_handler, XUG
	},
	[LDMSD_PLUGN_LIST_REQ] = {
		LDMSD_PLUGN_LIST_REQ, plugn_list_handler, XALL
	},
	[LDMSD_PLUGN_SETS_REQ] = {
		LDMSD_PLUGN_SETS_REQ, plugn_sets_handler, XALL
	},

	/* SET */
	[LDMSD_SET_UDATA_REQ] = {
		LDMSD_SET_UDATA_REQ, set_udata_handler, XUG
	},
	[LDMSD_SET_UDATA_REGEX_REQ] = {
		LDMSD_SET_UDATA_REGEX_REQ, set_udata_regex_handler, XUG
	},


	/* MISC */
	[LDMSD_VERBOSE_REQ] = {
		LDMSD_VERBOSE_REQ, verbosity_change_handler, XUG
	},
	[LDMSD_DAEMON_STATUS_REQ] = {
		LDMSD_DAEMON_STATUS_REQ, daemon_status_handler,
		XALL | LDMSD_PERM_FAILOVER_ALLOWED
	},
	[LDMSD_VERSION_REQ] = {
		LDMSD_VERSION_REQ, version_handler, XALL
	},
	[LDMSD_ENV_REQ] = {
		LDMSD_ENV_REQ, env_handler, XUG
	},
	[LDMSD_INCLUDE_REQ] = {
		LDMSD_INCLUDE_REQ, include_handler, XUG
	},
	[LDMSD_ONESHOT_REQ] = {
		LDMSD_ONESHOT_REQ, oneshot_handler, XUG
	},
	[LDMSD_LOGROTATE_REQ] = {
		LDMSD_LOGROTATE_REQ, logrotate_handler, XUG
	},
	[LDMSD_EXIT_DAEMON_REQ] = {
		LDMSD_EXIT_DAEMON_REQ, exit_daemon_handler, XUG
	},
	[LDMSD_GREETING_REQ] = {
		LDMSD_GREETING_REQ, greeting_handler, XUG
	},
	[LDMSD_SET_ROUTE_REQ] = {
		LDMSD_SET_ROUTE_REQ, set_route_handler, XUG
	},

	/* FAILOVER user commands */
	[LDMSD_FAILOVER_CONFIG_REQ] = {
		LDMSD_FAILOVER_CONFIG_REQ, failover_config_handler, XUG,
	},
	[LDMSD_FAILOVER_PEERCFG_STOP_REQ]  = {
		LDMSD_FAILOVER_PEERCFG_STOP_REQ,
		failover_peercfg_stop_handler,
		XUG | LDMSD_PERM_FAILOVER_ALLOWED,
	},
	[LDMSD_FAILOVER_PEERCFG_START_REQ]  = {
		LDMSD_FAILOVER_PEERCFG_START_REQ,
		failover_peercfg_start_handler,
		XUG | LDMSD_PERM_FAILOVER_ALLOWED,
	},
	[LDMSD_FAILOVER_STATUS_REQ]  = {
		LDMSD_FAILOVER_STATUS_REQ, failover_status_handler,
		XUG | LDMSD_PERM_FAILOVER_ALLOWED,
	},
	[LDMSD_FAILOVER_START_REQ] = {
		LDMSD_FAILOVER_START_REQ, failover_start_handler, XUG,
	},
	[LDMSD_FAILOVER_STOP_REQ] = {
		LDMSD_FAILOVER_STOP_REQ, failover_stop_handler,
		XUG | LDMSD_PERM_FAILOVER_ALLOWED,
	},

	/* FAILOVER internal requests */
	[LDMSD_FAILOVER_PAIR_REQ] = {
		LDMSD_FAILOVER_PAIR_REQ, failover_pair_handler,
		XUG | LDMSD_PERM_FAILOVER_INTERNAL,
	},
	[LDMSD_FAILOVER_RESET_REQ] = {
		LDMSD_FAILOVER_RESET_REQ, failover_reset_handler,
		XUG | LDMSD_PERM_FAILOVER_INTERNAL,
	},
	[LDMSD_FAILOVER_CFGPRDCR_REQ] = {
		LDMSD_FAILOVER_CFGPRDCR_REQ, failover_cfgprdcr_handler,
		XUG | LDMSD_PERM_FAILOVER_INTERNAL,
	},
	[LDMSD_FAILOVER_CFGUPDTR_REQ] = {
		LDMSD_FAILOVER_CFGUPDTR_REQ, failover_cfgupdtr_handler,
		XUG | LDMSD_PERM_FAILOVER_INTERNAL,
	},
	[LDMSD_FAILOVER_CFGSTRGP_REQ] = {
		LDMSD_FAILOVER_CFGSTRGP_REQ, failover_cfgstrgp_handler,
		XUG | LDMSD_PERM_FAILOVER_INTERNAL,
	},
	[LDMSD_FAILOVER_PING_REQ] = {
		LDMSD_FAILOVER_PING_REQ, failover_ping_handler,
		XUG | LDMSD_PERM_FAILOVER_INTERNAL,
	},
	[LDMSD_FAILOVER_PEERCFG_REQ] = {
		LDMSD_FAILOVER_PEERCFG_REQ, failover_peercfg_handler,
		XUG | LDMSD_PERM_FAILOVER_INTERNAL,
	},

	/* SETGROUP */
	[LDMSD_SETGROUP_ADD_REQ] = {
		LDMSD_SETGROUP_ADD_REQ, setgroup_add_handler, XUG,
	},
	[LDMSD_SETGROUP_MOD_REQ] = {
		LDMSD_SETGROUP_MOD_REQ, setgroup_mod_handler, XUG,
	},
	[LDMSD_SETGROUP_DEL_REQ] = {
		LDMSD_SETGROUP_DEL_REQ, setgroup_del_handler, XUG,
	},
	[LDMSD_SETGROUP_INS_REQ] = {
		LDMSD_SETGROUP_INS_REQ, setgroup_ins_handler, XUG,
	},
	[LDMSD_SETGROUP_RM_REQ] = {
		LDMSD_SETGROUP_RM_REQ, setgroup_rm_handler, XUG,
	},

	/* STREAM */
	[LDMSD_STREAM_PUBLISH_REQ] = {
		LDMSD_STREAM_PUBLISH_REQ, stream_publish_handler, XALL
	},
	[LDMSD_STREAM_SUBSCRIBE_REQ] = {
		LDMSD_STREAM_SUBSCRIBE_REQ, stream_subscribe_handler, XUG
	},

	/* LISTEN */
	[LDMSD_LISTEN_REQ] = {
		LDMSD_LISTEN_REQ, listen_handler, XUG,
	},

	/* AUTH */
	[LDMSD_AUTH_ADD_REQ] = {
		LDMSD_AUTH_ADD_REQ, auth_add_handler, XUG
	},
	[LDMSD_AUTH_DEL_REQ] = {
		LDMSD_AUTH_DEL_REQ, auth_del_handler, XUG
	},
};

/*
 * The process request function takes records and collects
 * them into messages. These messages are then delivered to the req_id
 * specific handlers.
 *
 * The assumptions are the following:
 * 1. msg_no is unique on the socket
 * 2. There may be multiple messages outstanding on the same socket
 */
static ldmsd_req_ctxt_t find_req_ctxt(struct req_ctxt_key *key)
{
	ldmsd_req_ctxt_t rm = NULL;
	struct rbn *rbn = rbt_find(&msg_tree, key);
	if (rbn)
		rm = container_of(rbn, struct ldmsd_req_ctxt, rbn);
	return rm;
}

/* The caller must hold the msg_tree lock. */
void __free_req_ctxt(ldmsd_req_ctxt_t reqc)
{
	rbt_del(&msg_tree, &reqc->rbn);
	if (reqc->line_buf)
		free(reqc->line_buf);
	if (reqc->req_buf)
		free(reqc->req_buf);
	if (reqc->rep_buf)
		free(reqc->rep_buf);
	free(reqc);
}

void req_ctxt_ref_get(ldmsd_req_ctxt_t reqc)
{
	assert(reqc->ref_count);
	__sync_fetch_and_add(&reqc->ref_count, 1);
}

/* Caller must hold the msg_tree lock. */
void req_ctxt_ref_put(ldmsd_req_ctxt_t reqc)
{
	if (0 == __sync_sub_and_fetch(&reqc->ref_count, 1))
		__free_req_ctxt(reqc);
}

/*
 * max_msg_len must be a positive number.
 *
 * The caller must hold the msg_tree lock.
 */
ldmsd_req_ctxt_t alloc_req_ctxt(struct req_ctxt_key *key, size_t max_msg_len)
{
	ldmsd_req_ctxt_t reqc;

	reqc = calloc(1, sizeof *reqc);
	if (!reqc)
		return NULL;
	reqc->ref_count = 1;
	/* leave one byte for terminating '\0' to accommodate string replies */
	reqc->line_len = LINE_BUF_LEN - 1;
	reqc->line_buf = malloc(LINE_BUF_LEN);
	if (!reqc->line_buf)
		goto err;
	reqc->line_buf[0] = '\0';
	reqc->req_len = max_msg_len * 2 - 1;
	reqc->req_off = sizeof(struct ldmsd_req_hdr_s);
	reqc->req_buf = malloc(max_msg_len * 2);
	if (!reqc->req_buf)
		goto err;
	*(uint32_t *)&reqc->req_buf[reqc->req_off] = 0; /* terminating discrim */
	reqc->rep_len = max_msg_len - 1;
	reqc->rep_off = sizeof(struct ldmsd_req_hdr_s);
	reqc->rep_buf = malloc(max_msg_len);
	if (!reqc->rep_buf)
		goto err;
	*(uint32_t *)&reqc->rep_buf[reqc->rep_off] = 0; /* terminating discrim */
	reqc->key = *key;
	rbn_init(&reqc->rbn, &reqc->key);
	rbt_ins(&msg_tree, &reqc->rbn);
	return reqc;
 err:
	__free_req_ctxt(reqc);
	return NULL;
}

void req_ctxt_tree_lock()
{
	pthread_mutex_lock(&msg_tree_lock);
}

void req_ctxt_tree_unlock()
{
	pthread_mutex_unlock(&msg_tree_lock);
}

static void free_cfg_xprt_ldms(ldmsd_cfg_xprt_t xprt)
{
	ldms_xprt_put(xprt->ldms.ldms);
	xprt->ldms.ldms = NULL;
	free(xprt);
}

/* Caller must hold the msg_tree lock. */
ldmsd_req_cmd_t alloc_req_cmd_ctxt(ldms_t ldms,
					size_t max_msg_sz,
					uint32_t req_id,
					ldmsd_req_ctxt_t orgn_reqc,
					ldmsd_req_resp_fn resp_handler,
					void *ctxt)
{
	static uint32_t msg_no = 0;

	ldmsd_req_cmd_t rcmd;
	struct req_ctxt_key key;
	ldmsd_cfg_xprt_t xprt = calloc(1, sizeof(*xprt));
	if (!xprt)
		return NULL;
	ldmsd_cfg_ldms_init(xprt, ldms);
	xprt->cleanup_fn = free_cfg_xprt_ldms;

	rcmd = calloc(1, sizeof(*rcmd));
	if (!rcmd)
		goto err0;

	key.msg_no = __sync_fetch_and_add(&msg_no, 1);
	key.conn_id = (uint64_t)(long unsigned)ldms;
	rcmd->reqc = alloc_req_ctxt(&key, max_msg_sz);
	if (!rcmd->reqc)
		goto err1;

	rcmd->reqc->ctxt = (void *)rcmd;
	rcmd->reqc->xprt = xprt;
	if (orgn_reqc) {
		req_ctxt_ref_get(orgn_reqc);
		rcmd->org_reqc = orgn_reqc;
	}
	rcmd->ctxt = ctxt;
	rcmd->reqc->req_id = req_id;
	rcmd->resp_handler = resp_handler;
	return rcmd;
err1:
	free(rcmd);
err0:
	free(xprt);
	return NULL;
}

ldmsd_req_cmd_t ldmsd_req_cmd_new(ldms_t ldms,
				    uint32_t req_id,
				    ldmsd_req_ctxt_t orgn_reqc,
				    ldmsd_req_resp_fn resp_handler,
				    void *ctxt)
{
	ldmsd_req_cmd_t ret;
	req_ctxt_tree_lock();
	ret = alloc_req_cmd_ctxt(ldms, ldms_xprt_msg_max(ldms),
					req_id, orgn_reqc,
					resp_handler, ctxt);
	req_ctxt_tree_unlock();
	return ret;
}

/* Caller must hold the msg_tree locks. */
void free_req_cmd_ctxt(ldmsd_req_cmd_t rcmd)
{
	if (rcmd->org_reqc)
		req_ctxt_ref_put(rcmd->org_reqc);
	if (rcmd->reqc)
		req_ctxt_ref_put(rcmd->reqc);
	if (rcmd->reqc->xprt->cleanup_fn)
		rcmd->reqc->xprt->cleanup_fn(rcmd->reqc->xprt);
	free(rcmd);
}

void ldmsd_req_cmd_free(ldmsd_req_cmd_t rcmd)
{
	req_ctxt_tree_lock();
	free_req_cmd_ctxt(rcmd);
	req_ctxt_tree_unlock();
}

static int string2attr_list(char *str, struct attr_value_list **__av_list,
					struct attr_value_list **__kw_list)
{
	char *cmd_s;
	struct attr_value_list *av_list;
	struct attr_value_list *kw_list;
	int tokens, rc;

	/*
	 * Count the numebr of spaces. That's the maximum number of
	 * tokens that could be present.
	 */
	for (tokens = 0, cmd_s = str; cmd_s[0] != '\0';) {
		tokens++;
		/* find whitespace */
		while (cmd_s[0] != '\0' && !isspace(cmd_s[0]))
			cmd_s++;
		/* Now skip whitespace to next token */
		while (cmd_s[0] != '\0' && isspace(cmd_s[0]))
			cmd_s++;
	}
	rc = ENOMEM;
	av_list = av_new(tokens);
	kw_list = av_new(tokens);
	if (!av_list || !kw_list)
		goto err;

	rc = tokenize(str, kw_list, av_list);
	if (rc)
		goto err;
	*__av_list = av_list;
	*__kw_list = kw_list;
	return 0;
err:
	if (av_list)
		av_free(av_list);
	if (kw_list)
		av_free(kw_list);
	*__av_list = NULL;
	*__kw_list = NULL;
	return rc;
}

int ldmsd_handle_request(ldmsd_req_ctxt_t reqc)
{
	struct request_handler_entry *ent;
	ldmsd_req_hdr_t request = (ldmsd_req_hdr_t)reqc->req_buf;
	ldms_t xprt = reqc->xprt->xprt;
	uid_t luid;
	gid_t lgid;
	mode_t mask;

	__dlog("handling req %s\n", ldmsd_req_id2str(reqc->req_id));

	/* Check for request id outside of range */
	if ((int)request->req_id < 0 ||
	    request->req_id >= (sizeof(request_handler)/sizeof(request_handler[0])))
		return unimplemented_handler(reqc);

	ent = &request_handler[request->req_id];

	/* Check for unimplemented request */
	if (!ent->handler)
		return unimplemented_handler(reqc);

	/* Check command permission */
	if (xprt) {
		/* NOTE: NULL xprt is a config file.
		 *       So, this is an in-band ldms xprt */

		/* check against inband mask */
		mask = ldmsd_inband_cfg_mask_get();
		if (0 == (mask & ent->flag))
			return ebusy_handler(reqc);

		/* check against credential */
		struct ldms_cred crd;
		ldms_xprt_cred_get(xprt, &crd, NULL);
		luid = crd.uid;
		lgid = crd.gid;
		if (0 != ldms_access_check(xprt, 0111, luid, lgid,
				ent->flag & 0111))
			return eperm_handler(reqc);
	}

	return request_handler[request->req_id].handler(reqc);
}

int ldmsd_handle_response(ldmsd_req_cmd_t rcmd)
{
	if (!rcmd->resp_handler) {
		ldmsd_log(LDMSD_LERROR, "No response handler "
				"for requeset id %" PRIu32 "\n", rcmd->reqc->req_id);
		return ENOTSUP;
	}

	return rcmd->resp_handler(rcmd);
}

__attribute__((format(printf, 3, 4)))
size_t Snprintf(char **dst, size_t *len, char *fmt, ...)
{
	va_list ap;
	va_list ap_copy;
	size_t cnt;

	if (!*dst) {
		*dst = malloc(1024);
		*len = 1024;
	}
	if (!*dst) {
		ldmsd_log(LDMSD_LERROR, "Out of memory\n");
		return 0;
	}

	va_start(ap, fmt);
	va_copy(ap_copy, ap);
	while (1) {
		cnt = vsnprintf(*dst, *len, fmt, ap_copy);
		va_end(ap_copy);
		if (cnt >= *len) {
			free(*dst);
			*len = cnt * 2;
			*dst = malloc(*len);
			assert(*dst);
			va_copy(ap_copy, ap);
			continue;
		}
		break;
	}
	va_end(ap);
	return cnt;
}

int linebuf_printf(struct ldmsd_req_ctxt *reqc, char *fmt, ...)
{
	va_list ap;
	va_list ap_copy;
	size_t cnt;

	va_start(ap, fmt);
	va_copy(ap_copy, ap);
	while (1) {
		cnt = vsnprintf(&reqc->line_buf[reqc->line_off],
				reqc->line_len - reqc->line_off, fmt, ap_copy);
		va_end(ap_copy);
		if (reqc->line_off + cnt >= reqc->line_len) {
			reqc->line_buf = realloc(reqc->line_buf,
						(2 * reqc->line_len) + cnt);
			if (!reqc->line_buf) {
				ldmsd_log(LDMSD_LERROR, "Out of memory\n");
				return ENOMEM;
			}
			va_copy(ap_copy, ap);
			reqc->line_len = (2 * reqc->line_len) + cnt;
			continue;
		}
		reqc->line_off += cnt;
		break;
	}
	va_end(ap);
	return 0;
}

int __ldmsd_append_buffer(struct ldmsd_req_ctxt *reqc,
		       const char *data, size_t data_len,
		       int msg_flags, int msg_type)
{
	req_ctxt_ref_get(reqc);
	ldmsd_req_hdr_t req_buff = (ldmsd_req_hdr_t)reqc->rep_buf;
	ldmsd_req_attr_t attr;
	size_t remaining;
	int flags, rc;

	do {
		remaining = reqc->rep_len - reqc->rep_off - sizeof(*req_buff);
		if (data_len < remaining)
			remaining = data_len;

		if (remaining && data) {
			memcpy(&reqc->rep_buf[reqc->rep_off], data, remaining);
			reqc->rep_off += remaining;
			data_len -= remaining;
			data += remaining;
		}

		if ((remaining == 0) ||
		    ((data_len == 0) && (msg_flags & LDMSD_REQ_EOM_F))) {
			/* If this is the first record in the response, set the
			 * SOM_F bit. If the caller set the EOM_F bit and we've
			 * exhausted data_len, set the EOM_F bit.
			 * If we've exhausted the reply buffer, unset the EOM_F bit.
			 */
			flags = msg_flags & ((!remaining && data_len)?(~LDMSD_REQ_EOM_F):LDMSD_REQ_EOM_F);
			flags |= (reqc->rec_no == 0?LDMSD_REQ_SOM_F:0);
			/* Record is full, send it on it's way */
			req_buff->marker = LDMSD_RECORD_MARKER;
			req_buff->type = msg_type;
			if (msg_type == LDMSD_REQ_TYPE_CONFIG_CMD)
				req_buff->req_id = reqc->req_id;
			else
				req_buff->rsp_err = reqc->errcode;
			req_buff->flags = flags;
			req_buff->msg_no = reqc->key.msg_no;
			req_buff->rec_len = reqc->rep_off;
			ldmsd_hton_req_hdr(req_buff);
			rc = reqc->xprt->send_fn(reqc->xprt, (char *)req_buff,
							ntohl(req_buff->rec_len));
			if (rc) {
				/* The content in reqc->rep_buf hasn't been sent. */
				ldmsd_log(LDMSD_LERROR, "failed to send the reply of "
						"the config request %d from "
						"config xprt id %" PRIu64 "\n",
						reqc->key.msg_no, reqc->key.conn_id);
				req_ctxt_ref_put(reqc);
				return rc;
			}
			reqc->rec_no++;
			/* Reset the reply buffer for the next record for this message */
			reqc->rep_off = sizeof(*req_buff);
			attr = ldmsd_first_attr(req_buff);
			attr->discrim = 0;
		}
	} while (data_len);

	req_ctxt_ref_put(reqc);
	return 0;
}

int ldmsd_append_reply(struct ldmsd_req_ctxt *reqc,
		       const char *data, size_t data_len, int msg_flags)
{
	return __ldmsd_append_buffer(reqc, data, data_len, msg_flags,
					LDMSD_REQ_TYPE_CONFIG_RESP);
}

int ldmsd_req_cmd_attr_append(ldmsd_req_cmd_t rcmd,
			      enum ldmsd_request_attr attr_id,
			      const void *value, int value_len)
{
	int rc;
	struct ldmsd_req_attr_s attr = {
				.attr_len = value_len,
				.attr_id = attr_id,
			};
	if (attr_id == LDMSD_ATTR_TERM) {
		attr.discrim = 0;
		return __ldmsd_append_buffer(rcmd->reqc, (void*)&attr.discrim,
					     sizeof(attr.discrim),
					     LDMSD_REQ_EOM_F,
					     LDMSD_REQ_TYPE_CONFIG_CMD);
	}
	if (attr_id >= LDMSD_ATTR_LAST)
		return EINVAL;
	attr.discrim = 1;
	ldmsd_hton_req_attr(&attr);
	rc = __ldmsd_append_buffer(rcmd->reqc, (void*)&attr, sizeof(attr),
				   0, LDMSD_REQ_TYPE_CONFIG_CMD);
	if (rc)
		return rc;
	if (value_len) {
		rc = __ldmsd_append_buffer(rcmd->reqc, value, value_len,
					   0, LDMSD_REQ_TYPE_CONFIG_CMD);
	}
	return rc;
}

/*
 * A convenient function that constructs a response with string attribute
 * if there is a message. Otherwise, only the terminating attribute is attached
 * to the request header.
 */
void ldmsd_send_req_response(ldmsd_req_ctxt_t reqc, const char *msg)
{
	struct ldmsd_req_attr_s attr;
	uint32_t flags = 0;
	if (!msg || (0 == strlen(msg))) {
		flags = LDMSD_REQ_SOM_F;
		goto endmsg;
	}
	attr.discrim = 1;
	attr.attr_id = LDMSD_ATTR_STRING;
	attr.attr_len = strlen(msg) + 1; /* +1 for '\0' */
	ldmsd_hton_req_attr(&attr);
	ldmsd_append_reply(reqc, (char *)&attr, sizeof(attr), LDMSD_REQ_SOM_F);
	ldmsd_append_reply(reqc, msg, strlen(msg) + 1, 0);
endmsg:
	attr.discrim = 0;
	ldmsd_append_reply(reqc, (char *)&attr.discrim, sizeof(uint32_t),
			flags | LDMSD_REQ_EOM_F);
}

void ldmsd_send_error_reply(ldmsd_cfg_xprt_t xprt, uint32_t msg_no,
			    uint32_t error, char *data, size_t data_len)
{
	ldmsd_req_hdr_t req_reply;
	ldmsd_req_attr_t attr;
	size_t reply_size = sizeof(*req_reply) + sizeof(*attr) + data_len + sizeof(uint32_t);
	req_reply = malloc(reply_size);
	if (!req_reply)
		return;
	req_reply->marker = LDMSD_RECORD_MARKER;
	req_reply->msg_no = msg_no;
	req_reply->flags = LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F;
	req_reply->rec_len = reply_size;
	req_reply->rsp_err = error;
	req_reply->type = LDMSD_REQ_TYPE_CONFIG_RESP;
	attr = ldmsd_first_attr(req_reply);
	attr->discrim = 1;
	attr->attr_id = LDMSD_ATTR_STRING;
	attr->attr_len = data_len;
	memcpy(attr + 1, data, data_len);
	attr = ldmsd_next_attr(attr);
	attr->discrim = 0;
	ldmsd_hton_req_msg(req_reply);
	xprt->send_fn(xprt, (char *)req_reply, reply_size);
}

void ldmsd_send_cfg_rec_adv(ldmsd_cfg_xprt_t xprt, uint32_t msg_no, uint32_t rec_len)
{
	ldmsd_req_hdr_t req_reply;
	ldmsd_req_attr_t attr;
	size_t reply_size = sizeof(*req_reply) + sizeof(*attr) + sizeof(rec_len) + sizeof(uint32_t);
	req_reply = malloc(reply_size);
	if (!req_reply)
		return;
	req_reply->marker = LDMSD_RECORD_MARKER;
	req_reply->msg_no = msg_no;
	req_reply->flags = LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F;
	req_reply->rec_len = reply_size;
	req_reply->rsp_err = E2BIG;
	req_reply->type = LDMSD_REQ_TYPE_CONFIG_RESP;
	attr = ldmsd_first_attr(req_reply);
	attr->discrim = 1;
	attr->attr_id = LDMSD_ATTR_REC_LEN;
	attr->attr_len = sizeof(rec_len);
	attr->attr_u32[0] = rec_len;
	attr = ldmsd_next_attr(attr);
	attr->discrim = 0;
	ldmsd_hton_req_msg(req_reply);
	xprt->send_fn(xprt, (char *)req_reply, reply_size);
}

extern void cleanup(int x, char *reason);
int ldmsd_process_config_request(ldmsd_cfg_xprt_t xprt, ldmsd_req_hdr_t request)
{
	struct req_ctxt_key key;
	ldmsd_req_ctxt_t reqc = NULL;
	size_t cnt;
	int rc = 0;
	char *oom_errstr = "ldmsd out of memory";
	size_t rec_len = ntohl(request->rec_len);

	key.msg_no = ntohl(request->msg_no);
	key.conn_id = (uint64_t)(long unsigned)xprt;

	if (ntohl(request->marker) != LDMSD_RECORD_MARKER) {
		char *msg = "Config request is missing record marker";
		ldmsd_send_error_reply(xprt, -1, EINVAL, msg, strlen(msg));
		rc = EINVAL;
		goto out;
	}

	__dlog("processing message %d:%lu %s\n",
		   key.msg_no, key.conn_id,
		   ldmsd_req_id2str(ntohl(request->req_id)));

	req_ctxt_tree_lock();
	if (ntohl(request->flags) & LDMSD_REQ_SOM_F) {
		/* Ensure that we don't already have this message in
		 * the tree */
		reqc = find_req_ctxt(&key);
		if (reqc) {
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				  "Duplicate message number %d:%" PRIu64 "received",
				  key.msg_no, key.conn_id);
			rc = EADDRINUSE;
			ldmsd_send_error_reply(xprt, key.msg_no, rc, reqc->line_buf, cnt);
			goto err_out;
		}
		reqc = alloc_req_ctxt(&key, xprt->max_msg);
		if (!reqc)
			goto oom;
		reqc->xprt = xprt;
		memcpy(reqc->req_buf, request, rec_len);
		reqc->req_off = rec_len;
	} else {
		reqc = find_req_ctxt(&key);
		if (!reqc) {
			char errstr[256];
			snprintf(errstr, 255, "The message no %" PRIu32
					" was not found.", key.msg_no);
			rc = ENOENT;
			ldmsd_log(LDMSD_LERROR, "The message no %" PRIu32 ":%" PRIu64
					" was not found.\n", key.msg_no, key.conn_id);
			ldmsd_send_error_reply(xprt, key.msg_no, rc,
						errstr, strlen(errstr));
			goto err_out;
		}
		/* Copy the data from this record to the tail of the request context */
		cnt = rec_len - sizeof(*request);
		if (reqc->req_len - reqc->req_off < cnt) {
			reqc->req_buf = realloc(reqc->req_buf, 2 * (reqc->req_len + 1));
			if (!reqc->req_buf)
				goto oom;
			reqc->req_len = reqc->req_len * 2 + 1; /* req_len = req_buf sz - 1 */
		}
		memcpy(&reqc->req_buf[reqc->req_off], request + 1, cnt);
		reqc->req_off += cnt;
	}
	req_ctxt_tree_unlock();

	if (0 == (ntohl(request->flags) & LDMSD_REQ_EOM_F))
		/* Not the end of the message */
		goto out;

	/* Convert the request byte order from network to host */
	ldmsd_ntoh_req_msg((ldmsd_req_hdr_t)reqc->req_buf);
	reqc->req_id = ((ldmsd_req_hdr_t)reqc->req_buf)->req_id;

	rc = ldmsd_handle_request(reqc);

	req_ctxt_tree_lock();
	req_ctxt_ref_put(reqc);
	req_ctxt_tree_unlock();

	if (cleanup_requested)
		cleanup(0, "user quit");
 out:
	return rc;
 oom:
	ldmsd_log(LDMSD_LCRITICAL, "%s\n", oom_errstr);
	rc = ENOMEM;
	ldmsd_send_error_reply(xprt, key.msg_no, rc, oom_errstr, strlen(oom_errstr));
 err_out:
	req_ctxt_tree_unlock();
	return rc;
}

/*
 * This function assumes that the response is sent using ldms xprt.
 */
int ldmsd_process_config_response(ldmsd_cfg_xprt_t xprt, ldmsd_req_hdr_t response)
{
	struct req_ctxt_key key;
	ldmsd_req_cmd_t rcmd = NULL;
	ldmsd_req_ctxt_t reqc = NULL;
	size_t cnt;
	int rc = 0;
	size_t rec_len = ntohl(response->rec_len);

	key.msg_no = ntohl(response->msg_no);
	key.conn_id = (uint64_t)(long unsigned)xprt->ldms.ldms;

	if (ntohl(response->marker) != LDMSD_RECORD_MARKER) {
		ldmsd_log(LDMSD_LERROR,
			  "Config request is missing record marker\n");
		rc = EINVAL;
		goto out;
	}

	__dlog("processing response %d:%lu\n", key.msg_no, key.conn_id);

	req_ctxt_tree_lock();
	reqc = find_req_ctxt(&key);
	if (!reqc) {
		char errstr[256];
		cnt = snprintf(errstr, 256, "Cannot find the original request"
					" of a response number %d:%" PRIu64,
					key.msg_no, key.conn_id);
		ldmsd_log(LDMSD_LERROR, "%s\n", errstr);
		rc = ENOENT;
		goto err_out;
	}
	/* Copy the data from this record to the tail of the request context */
	if (ntohl(response->flags) & LDMSD_REQ_SOM_F) {
		memcpy(reqc->req_buf, response, rec_len);
		reqc->req_off = rec_len;
	} else {
		cnt = rec_len - sizeof(*response);
		if (reqc->req_len - reqc->req_off < cnt) {
			reqc->req_buf = realloc(reqc->req_buf, 2 * (reqc->req_len + 1));
			if (!reqc->req_buf) {
				ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
				rc = ENOMEM;
				goto err_out;
			}
			reqc->req_len = reqc->req_len * 2 + 1; /* req_len = req_buf sz - 1 */
		}
		memcpy(&reqc->req_buf[reqc->req_off], response + 1, cnt);
		reqc->req_off += cnt;
	}
	req_ctxt_tree_unlock();

	if (0 == (ntohl(response->flags) & LDMSD_REQ_EOM_F))
		/* Not the end of the message */
		goto out;

	/* Convert the request byte order from network to host */
	ldmsd_ntoh_req_msg((ldmsd_req_hdr_t)reqc->req_buf);
	rcmd = (ldmsd_req_cmd_t)reqc->ctxt;
	rc = ldmsd_handle_response(rcmd);

	req_ctxt_tree_lock();
	free_req_cmd_ctxt(rcmd);
	req_ctxt_tree_unlock();
 out:
	return rc;
 err_out:
	req_ctxt_tree_unlock();
	return rc;
}

/**
 * This handler provides an example of how arguments are passed to
 * request handlers.
 *
 * If your request does not require arguments, then the argument list
 * may be ommited in it's entirely. If however, it does have
 * arguments, then the format of the reuest is as follows:
 *
 * +------------------+
 * |  ldms_req_hdr_s  |
 * +------------------+
 * | lmdsd_req_attr_s |
 * S     1st arg      S
 * +------------------+
 * | lmdsd_req_attr_s |
 * S     2nd arg      S
 * +------------------+
 * | lmdsd_req_attr_s |
 * S     3rd arg      S
 * +------------------+
 * S  0x0000_0000     S
 * +------------------+
 * S  request data    S
 * +------------------+
 *
 * The presence of an argument is indicated by the 'discrim' field of
 * the ldmsd_req_attr_s structure. If it is non-zero, then the
 * argument is present, otherwise, it indicates the end of the
 * argument list. The argument list is immediately followed by the
 * request payload.
 *
 * The example below takes a variable length argument list, formats
 * the arguments as a JSON array and returns the array to the caller.
 */

int __example_json_obj(ldmsd_req_ctxt_t reqc)
{
	int rc, count = 0;
	ldmsd_req_attr_t attr = ldmsd_first_attr((ldmsd_req_hdr_t)reqc->req_buf);
	reqc->errcode = 0;
	rc = linebuf_printf(reqc, "[");
	if (rc)
		return rc;
	while (attr->discrim) {
		if (count) {
			rc = linebuf_printf(reqc, ",\n");
			if (rc)
				return rc;
		}
		rc = linebuf_printf(reqc,
			       "{ \"attr_len\":%d,"
			       "\"attr_id\":%d,"
			       "\"attr_value\": \"%s\" }",
			       attr->attr_len,
			       attr->attr_id,
			       (char *)attr->attr_value);
		if (rc)
			return rc;
		count++;
		attr = ldmsd_next_attr(attr);
	}
	rc = linebuf_printf(reqc, "]");
	return rc;
}

static int example_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	int flags = 0;
	struct ldmsd_req_attr_s attr;
	rc = __example_json_obj(reqc);
	if (rc)
		return rc;

	/* Send the json attribut header */
	attr.discrim = 1;
	attr.attr_len = reqc->line_off;
	attr.attr_id = LDMSD_ATTR_JSON;
	ldmsd_hton_req_attr(&attr);
	rc = ldmsd_append_reply(reqc, (char *)&attr, sizeof(attr), LDMSD_REQ_SOM_F);
	if (rc)
		return rc;
	/* Send the json object string */
	rc = ldmsd_append_reply(reqc, reqc->line_buf, reqc->line_off, 0);
	if (rc)
		return rc;
	/* Send the terminating attribute header */
	attr.discrim = 0;
	rc = ldmsd_append_reply(reqc, (char *)&(attr.discrim), sizeof(uint32_t),
							flags | LDMSD_REQ_EOM_F);
	return rc;
}

static int prdcr_add_handler(ldmsd_req_ctxt_t reqc)
{
	ldmsd_prdcr_t prdcr;
	char *name, *host, *xprt, *attr_name, *type_s, *port_s, *interval_s;
	char *auth;
	name = host = xprt = type_s = port_s = interval_s = auth = NULL;
	enum ldmsd_prdcr_type type = -1;
	unsigned short port_no = 0;
	int interval_us = -1;
	size_t cnt;
	uid_t uid;
	gid_t gid;
	int perm;
	char *perm_s = NULL;
	reqc->errcode = 0;

	attr_name = "name";
	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!name)
		goto einval;

	attr_name = "type";
	type_s = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_TYPE);
	if (!type_s) {
		goto einval;
	} else {
		type = ldmsd_prdcr_str2type(type_s);
		if ((int)type < 0) {
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
					"The attribute type '%s' is invalid.",
					type_s);
			goto send_reply;
		}
		if (type == LDMSD_PRDCR_TYPE_LOCAL) {
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
					"Producer with type 'local' is "
					"not supported.");
			reqc->errcode = EINVAL;
			goto send_reply;
		}
	}

	attr_name = "xprt";
	xprt = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_XPRT);
	if (!xprt)
		goto einval;

	attr_name = "host";
	host = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_HOST);
	if (!host)
		goto einval;

	attr_name = "port";
	port_s = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_PORT);
	if (!port_s) {
		goto einval;
	} else {
		long ptmp = 0;
		ptmp = strtol(port_s, NULL, 0);
		if (ptmp < 1 || ptmp > USHRT_MAX) {
			goto einval;
		}
		port_no = (unsigned)ptmp;
	}

	attr_name = "interval";
	interval_s = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_INTERVAL);
	if (!interval_s) {
		goto einval;
	} else {
		 interval_us = strtol(interval_s, NULL, 0);
	}

	auth = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_AUTH);

	struct ldmsd_sec_ctxt sctxt;
	if (reqc->xprt->xprt) {
		/* the requester is the owner of the object */
		ldms_xprt_cred_get(reqc->xprt->xprt, NULL, &sctxt.crd);
	} else {
		ldmsd_sec_ctxt_get(&sctxt);
	}
	uid = sctxt.crd.uid;
	gid = sctxt.crd.gid;

	perm = 0770;
	perm_s = ldmsd_req_attr_str_value_get_by_name(reqc, "perm");
	if (perm_s)
		perm = strtol(perm_s, NULL, 0);

	prdcr = ldmsd_prdcr_new_with_auth(name, xprt, host, port_no, type,
					  interval_us, auth, uid, gid, perm);
	if (!prdcr) {
		if (errno == EEXIST)
			goto eexist;
		else if (errno == EAFNOSUPPORT)
			goto eafnosupport;
		else
			goto enomem;
	}

	goto send_reply;
enomem:
	reqc->errcode = ENOMEM;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"Memory allocation failed.");
	goto send_reply;
eexist:
	reqc->errcode = EEXIST;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The prdcr %s already exists.", name);
	goto send_reply;
eafnosupport:
	reqc->errcode = EAFNOSUPPORT;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"Error resolving hostname '%s'\n", host);
	goto send_reply;
einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The attribute '%s' is required.", attr_name);
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (name)
		free(name);
	if (type_s)
		free(type_s);
	if (port_s)
		free(port_s);
	if (interval_s)
		free(interval_s);
	if (host)
		free(host);
	if (xprt)
		free(xprt);
	if (perm_s)
		free(perm_s);
	return 0;
}

static int prdcr_del_handler(ldmsd_req_ctxt_t reqc)
{
	char *name = NULL, *attr_name;
	size_t cnt = 0;
	struct ldmsd_sec_ctxt sctxt;

	reqc->errcode = 0;

	attr_name = "name";
	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!name) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The attribute '%s' is required by prdcr_del.",
			       	attr_name);
		goto send_reply;
	}

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);

	reqc->errcode = ldmsd_prdcr_del(name, &sctxt);
	switch (reqc->errcode) {
	case 0:
		break;
	case ENOENT:
		Snprintf(&reqc->line_buf, &reqc->line_len,
				"The producer specified does not exist.");
		break;
	case EBUSY:
		Snprintf(&reqc->line_buf, &reqc->line_len,
				"The producer is in use.");
		break;
	case EACCES:
		Snprintf(&reqc->line_buf, &reqc->line_len,
				"Permission denied.");
		break;
	default:
		Snprintf(&reqc->line_buf, &reqc->line_len,
				"Error: %d %s",
				reqc->errcode, ovis_errno_abbvr(reqc->errcode));
	}

send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (name)
		free(name);
	return 0;
}

static int prdcr_start_handler(ldmsd_req_ctxt_t reqc)
{
	char *name, *interval_str;
	name = interval_str = NULL;
	size_t cnt = 0;
	struct ldmsd_sec_ctxt sctxt;

	reqc->errcode = 0;

	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!name) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The attribute 'name' is required by prdcr_start.");
		goto send_reply;
	}

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);
	interval_str = ldmsd_req_attr_str_value_get_by_id(reqc,
							LDMSD_ATTR_INTERVAL);
	reqc->errcode = ldmsd_prdcr_start(name, interval_str, &sctxt);
	switch (reqc->errcode) {
	case 0:
		break;
	case EBUSY:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The producer is already running.");
		break;
	case ENOENT:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The producer specified does not exist.");
		break;
	case EACCES:
		Snprintf(&reqc->line_buf, &reqc->line_len,
				"Permission denied.");
		break;
	default:
		Snprintf(&reqc->line_buf, &reqc->line_len,
				"Error: %d %s",
				reqc->errcode, ovis_errno_abbvr(reqc->errcode));
	}

send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (name)
		free(name);
	if (interval_str)
		free(interval_str);
	return 0;
}

static int prdcr_stop_handler(ldmsd_req_ctxt_t reqc)
{
	char *name = NULL;
	size_t cnt = 0;
	struct ldmsd_sec_ctxt sctxt;

	reqc->errcode = 0;

	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!name) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The attribute 'name' is required by prdcr_stop.");
		goto send_reply;
	}

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);

	reqc->errcode = ldmsd_prdcr_stop(name, &sctxt);
	switch (reqc->errcode) {
	case 0:
		break;
	case EBUSY:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The producer is already stopped.");
		break;
	case ENOENT:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The producer specified does not exist.");
		break;
	case EACCES:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Permission denied.");
		break;
	default:
		Snprintf(&reqc->line_buf, &reqc->line_len,
				"Error: %d %s",
				reqc->errcode, ovis_errno_abbvr(reqc->errcode));
	}

send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (name)
		free(name);
	return 0;
}

static int prdcr_start_regex_handler(ldmsd_req_ctxt_t reqc)
{
	char *prdcr_regex, *interval_str;
	prdcr_regex = interval_str = NULL;
	size_t cnt = 0;
	struct ldmsd_sec_ctxt sctxt;

	reqc->errcode = 0;

	prdcr_regex = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_REGEX);
	if (!prdcr_regex) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The attribute 'regex' is required by prdcr_start_regex.");
		goto send_reply;
	}

	interval_str = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_INTERVAL);

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);

	reqc->errcode = ldmsd_prdcr_start_regex(prdcr_regex, interval_str,
					reqc->line_buf, reqc->line_len, &sctxt);
	/* on error, reqc->line_buf will be filled */

send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (prdcr_regex)
		free(prdcr_regex);
	if (interval_str)
		free(interval_str);
	return 0;
}

static int prdcr_stop_regex_handler(ldmsd_req_ctxt_t reqc)
{
	char *prdcr_regex = NULL;
	size_t cnt = 0;
	struct ldmsd_sec_ctxt sctxt;

	reqc->errcode = 0;

	prdcr_regex = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_REGEX);
	if (!prdcr_regex) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The attribute 'regex' is required by prdcr_stop_regex.");
		goto send_reply;
	}

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);
	reqc->errcode = ldmsd_prdcr_stop_regex(prdcr_regex,
				reqc->line_buf, reqc->line_len, &sctxt);
	/* on error, reqc->line_buf will be filled */

send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (prdcr_regex)
		free(prdcr_regex);
	return 0;
}

static int prdcr_subscribe_regex_handler(ldmsd_req_ctxt_t reqc)
{
	char *prdcr_regex;
	char *stream_name;
	size_t cnt = 0;
	struct ldmsd_sec_ctxt sctxt;

	reqc->errcode = 0;

	prdcr_regex = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_REGEX);
	if (!prdcr_regex) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The attribute 'regex' is required by prdcr_stop_regex.");
		goto send_reply;
	}

	stream_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_STREAM);
	if (!stream_name) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The attribute 'stream' is required by prdcr_subscribe_regex.");
		goto send_reply;
	}

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);
	reqc->errcode = ldmsd_prdcr_subscribe_regex(prdcr_regex,
						    stream_name,
						    reqc->line_buf,
						    reqc->line_len, &sctxt);
	/* on error, reqc->line_buf will be filled */

send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (prdcr_regex)
		free(prdcr_regex);
	return 0;
}

int __prdcr_status_json_obj(ldmsd_req_ctxt_t reqc, ldmsd_prdcr_t prdcr, int prdcr_cnt)
{
	extern const char *prdcr_state_str(enum ldmsd_prdcr_state state);
	ldmsd_prdcr_set_t prv_set;
	int set_count = 0;
	int rc = 0;

	/* Append the string to line_buf */
	if (prdcr_cnt) {
		rc = linebuf_printf(reqc, ",\n");
		if (rc)
			return rc;
	}

	ldmsd_prdcr_lock(prdcr);
	rc = linebuf_printf(reqc,
			"{ \"name\":\"%s\","
			"\"type\":\"%s\","
			"\"host\":\"%s\","
			"\"port\":%hu,"
			"\"transport\":\"%s\","
			"\"reconnect_us\":\"%ld\","
			"\"state\":\"%s\","
			"\"sets\": [",
			prdcr->obj.name, ldmsd_prdcr_type2str(prdcr->type),
			prdcr->host_name, prdcr->port_no, prdcr->xprt_name,
			prdcr->conn_intrvl_us,
			prdcr_state_str(prdcr->conn_state));
	if (rc)
		goto out;

	set_count = 0;
	for (prv_set = ldmsd_prdcr_set_first(prdcr); prv_set;
	     prv_set = ldmsd_prdcr_set_next(prv_set)) {
		if (set_count) {
			rc = linebuf_printf(reqc, ",\n");
			if (rc)
				goto out;
		}

		rc = linebuf_printf(reqc,
			"{ \"inst_name\":\"%s\","
			"\"schema_name\":\"%s\","
			"\"state\":\"%s\"}",
			prv_set->inst_name,
			(prv_set->schema_name ? prv_set->schema_name : ""),
			ldmsd_prdcr_set_state_str(prv_set->state));
		if (rc)
			goto out;
		set_count++;
	}
	rc = linebuf_printf(reqc, "]}");
out:
	ldmsd_prdcr_unlock(prdcr);
	return rc;
}

static int prdcr_status_handler(ldmsd_req_ctxt_t reqc)
{
	int rc = 0;
	size_t cnt = 0;
	struct ldmsd_req_attr_s attr;
	ldmsd_prdcr_t prdcr = NULL;
	char *name;
	int count;

	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (name) {
		prdcr = ldmsd_prdcr_find(name);
		if (!prdcr) {
			/* Do not report any status */
			cnt = snprintf(reqc->line_buf, reqc->line_len,
					"prdcr '%s' doesn't exist.", name);
			reqc->errcode = ENOENT;
			ldmsd_send_req_response(reqc, reqc->line_buf);
			return 0;
		}
	}

	/* Construct the json object of the producer(s) */
	if (prdcr) {
		rc = __prdcr_status_json_obj(reqc, prdcr, 0);
		if (rc)
			goto out;
	} else {
		count = 0;
		ldmsd_cfg_lock(LDMSD_CFGOBJ_PRDCR);
		for (prdcr = ldmsd_prdcr_first(); prdcr;
				prdcr = ldmsd_prdcr_next(prdcr)) {
			rc = __prdcr_status_json_obj(reqc, prdcr, count);
			if (rc) {
				ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR);
				goto out;
			}
			count++;
		}
		ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR);
	}
	cnt = reqc->line_off + 2; /* +2 for '[' and ']' */

	/* Send the json attribute header */
	attr.discrim = 1;
	attr.attr_len = cnt;
	attr.attr_id = LDMSD_ATTR_JSON;
	ldmsd_hton_req_attr(&attr);
	rc = ldmsd_append_reply(reqc, (char *)&attr, sizeof(attr), LDMSD_REQ_SOM_F);
	if (rc)
		goto out;

	/* Send the json object */
	rc = ldmsd_append_reply(reqc, "[", 1, 0);
	if (rc)
		goto out;
	if (reqc->line_off) {
		rc = ldmsd_append_reply(reqc, reqc->line_buf, reqc->line_off, 0);
		if (rc)
			goto out;
	}
	rc = ldmsd_append_reply(reqc, "]", 1, 0);
	if (rc) {
		goto out;
	}

	/* Send the terminating attribute */
	attr.discrim = 0;
	rc = ldmsd_append_reply(reqc, (char *)&attr.discrim,
			sizeof(uint32_t), LDMSD_REQ_EOM_F);
out:
	if (name)
		free(name);
	if (prdcr)
		ldmsd_prdcr_put(prdcr);
	return rc;
}

size_t __prdcr_set_status(ldmsd_req_ctxt_t reqc, ldmsd_prdcr_set_t prd_set)
{
	struct ldms_timestamp ts = { 0, 0 }, dur = { 0, 0 };
	const char *producer_name = "";
	if (prd_set->set) {
		ts = ldms_transaction_timestamp_get(prd_set->set);
		dur = ldms_transaction_duration_get(prd_set->set);
		producer_name = ldms_set_producer_name_get(prd_set->set);
	}
	return linebuf_printf(reqc,
		"{ "
		"\"inst_name\":\"%s\","
		"\"schema_name\":\"%s\","
		"\"state\":\"%s\","
		"\"origin\":\"%s\","
		"\"producer\":\"%s\","
		"\"timestamp.sec\":\"%d\","
		"\"timestamp.usec\":\"%d\","
		"\"duration.sec\":\"%u\","
		"\"duration.usec\":\"%u\""
		"}",
		prd_set->inst_name, prd_set->schema_name,
		ldmsd_prdcr_set_state_str(prd_set->state),
		producer_name,
		prd_set->prdcr->obj.name,
		ts.sec, ts.usec,
		dur.sec, dur.usec);
}

/* This function must be called with producer lock held */
int __prdcr_set_status_handler(ldmsd_req_ctxt_t reqc, ldmsd_prdcr_t prdcr,
			int *count, const char *setname, const char *schema)
{
	int rc = 0;
	ldmsd_prdcr_set_t prd_set;

	if (setname) {
		prd_set = ldmsd_prdcr_set_find(prdcr, setname);
		if (!prd_set)
			return 0;
		if (schema && (0 != strcmp(prd_set->schema_name, schema)))
			return 0;
		if (*count) {
			rc = linebuf_printf(reqc, ",\n");
			if (rc)
				return rc;
		}
		rc = __prdcr_set_status(reqc, prd_set);
		if (rc)
			return rc;
		(*count)++;
	} else {
		for (prd_set = ldmsd_prdcr_set_first(prdcr); prd_set;
			prd_set = ldmsd_prdcr_set_next(prd_set)) {
			if (schema && (0 != strcmp(prd_set->schema_name, schema)))
				continue;

			if (*count) {
				rc = linebuf_printf(reqc, ",\n");
				if (rc)
					return rc;
			}
			rc = __prdcr_set_status(reqc, prd_set);
			if (rc)
				return rc;
			(*count)++;
		}
	}
	return rc;
}

int __prdcr_set_status_json_obj(ldmsd_req_ctxt_t reqc)
{
	char *prdcr_name, *setname, *schema;
	prdcr_name = setname = schema = NULL;
	ldmsd_prdcr_t prdcr = NULL;
	int rc, count = 0;
	reqc->errcode = 0;

	prdcr_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_PRODUCER);
	setname = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_INSTANCE);
	schema = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_SCHEMA);

	rc = linebuf_printf(reqc, "[");
	if (rc)
		return rc;
	if (prdcr_name) {
		prdcr = ldmsd_prdcr_find(prdcr_name);
		if (!prdcr)
			goto out;
	}

	if (prdcr) {
		ldmsd_prdcr_lock(prdcr);
		rc = __prdcr_set_status_handler(reqc, prdcr, &count,
						setname, schema);
		ldmsd_prdcr_unlock(prdcr);
	} else {
		ldmsd_cfg_lock(LDMSD_CFGOBJ_PRDCR);
		for (prdcr = ldmsd_prdcr_first(); prdcr;
				prdcr = ldmsd_prdcr_next(prdcr)) {
			ldmsd_prdcr_lock(prdcr);
			rc = __prdcr_set_status_handler(reqc, prdcr, &count,
							setname, schema);
			ldmsd_prdcr_unlock(prdcr);
			if (rc) {
				ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR);
				goto out;
			}
		}
		ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR);
	}

out:
	rc = linebuf_printf(reqc, "]");
	if (prdcr_name)
		free(prdcr_name);
	if (setname)
		free(setname);
	if (schema)
		free(schema);
	return rc;
}

static int prdcr_set_status_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	struct ldmsd_req_attr_s attr;

	rc = __prdcr_set_status_json_obj(reqc);
	if (rc)
		return rc;
	attr.discrim = 1;
	attr.attr_len = reqc->line_off;
	attr.attr_id = LDMSD_ATTR_JSON;
	ldmsd_hton_req_attr(&attr);
	rc = ldmsd_append_reply(reqc, (char *)&attr,
				sizeof(attr), LDMSD_REQ_SOM_F);
	if (rc)
		return rc;

	rc = ldmsd_append_reply(reqc, reqc->line_buf, reqc->line_off, 0);
	if (rc)
		return rc;
	attr.discrim = 0;
	rc = ldmsd_append_reply(reqc, (char *)&attr.discrim,
			sizeof(uint32_t), LDMSD_REQ_EOM_F);
	return rc;
}

static int strgp_add_handler(ldmsd_req_ctxt_t reqc)
{
	char *attr_name, *name, *plugin, *container, *schema;
	name = plugin = container = schema = NULL;
	size_t cnt = 0;
	uid_t uid;
	gid_t gid;
	int perm;
	char *perm_s = NULL;

	reqc->errcode = 0;

	attr_name = "name";
	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!name)
		goto einval;

	attr_name = "plugin";
	plugin = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_PLUGIN);
	if (!plugin)
		goto einval;

	attr_name = "container";
	container = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_CONTAINER);
	if (!container)
		goto einval;

	attr_name = "schema";
	schema = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_SCHEMA);
	if (!schema)
		goto einval;

	struct ldmsd_plugin_cfg *store;
	store = ldmsd_get_plugin(plugin);
	if (!store) {
		reqc->errcode = ENOENT;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The plugin %s does not exist. Forgot load?\n", plugin);
		goto send_reply;
	}

	struct ldmsd_sec_ctxt sctxt;
	if (reqc->xprt->xprt) {
		ldms_xprt_cred_get(reqc->xprt->xprt, NULL, &sctxt.crd);
	} else {
		ldmsd_sec_ctxt_get(&sctxt);
	}
	uid = sctxt.crd.uid;
	gid = sctxt.crd.gid;

	perm = 0770;
	perm_s = ldmsd_req_attr_str_value_get_by_name(reqc, "perm");
	if (perm_s)
		perm = strtol(perm_s, NULL, 0);

	ldmsd_strgp_t strgp = ldmsd_strgp_new_with_auth(name, uid, gid, perm);
	if (!strgp) {
		if (errno == EEXIST)
			goto eexist;
		else
			goto enomem;
	}

	strgp->plugin_name = strdup(plugin);
	if (!strgp->plugin_name)
		goto enomem_1;

	strgp->schema = strdup(schema);
	if (!strgp->schema)
		goto enomem_2;

	strgp->container = strdup(container);
	if (!strgp->container)
		goto enomem_3;

	goto send_reply;

enomem_3:
	free(strgp->schema);
enomem_2:
	free(strgp->plugin_name);
enomem_1:
	free(strgp);
enomem:
	reqc->errcode = ENOMEM;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Memory allocation failed.");
	goto send_reply;
eexist:
	reqc->errcode = EEXIST;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The prdcr %s already exists.", name);
	goto send_reply;
einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The attribute '%s' is required by strgp_add.",
		       	attr_name);
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (name)
		free(name);
	if (plugin)
		free(plugin);
	if (container)
		free(container);
	if (schema)
		free(schema);
	if (perm_s)
		free(perm_s);
	return 0;
}

static int strgp_del_handler(ldmsd_req_ctxt_t reqc)
{
	char *name = NULL;
	size_t cnt = 0;
	struct ldmsd_sec_ctxt sctxt;

	reqc->errcode = 0;

	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!name) {
		reqc->errcode= EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The attribute 'name' is required"
				"by strgp_del.");
		goto send_reply;
	}

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);

	reqc->errcode = ldmsd_strgp_del(name, &sctxt);
	switch (reqc->errcode) {
	case 0:
		break;
	case ENOENT:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The storage policy specified does not exist.");
		break;
	case EBUSY:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The storage policy is in use.");
		break;
	case EACCES:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Permission denied.");
		break;
	default:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "Error %d %s", reqc->errcode,
			       ovis_errno_abbvr(reqc->errcode));
	}

send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (name)
		free(name);
	return 0;
}

static int strgp_prdcr_add_handler(ldmsd_req_ctxt_t reqc)
{
	char *name, *regex_str, *attr_name;
	name = regex_str = NULL;
	struct ldmsd_sec_ctxt sctxt;

	size_t cnt = 0;
	reqc->errcode = 0;

	attr_name = "name";
	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!name)
		goto einval;

	attr_name = "regex";
	regex_str = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_REGEX);
	if (!regex_str)
		goto einval;

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);
	reqc->errcode = ldmsd_strgp_prdcr_add(name, regex_str,
				reqc->line_buf, reqc->line_len, &sctxt);
	switch (reqc->errcode) {
	case 0:
		break;
	case ENOENT:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The storage policy specified "
				"does not exist.");
		break;
	case EBUSY:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Configuration changes cannot be made "
				"while the storage policy is running.");
		break;
	case ENOMEM:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
					"Out of memory");
		break;
	case EACCES:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
					"Permission denied.");
		break;
	default:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "Error %d %s", reqc->errcode,
			       ovis_errno_abbvr(reqc->errcode));
	}
	goto send_reply;
einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The attribute '%s' is required by %s.", attr_name,
			"strgp_prdcr_add");
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (name)
		free(name);
	if (regex_str)
		free(regex_str);
	return 0;
}

static int strgp_prdcr_del_handler(ldmsd_req_ctxt_t reqc)
{
	char *name, *regex_str, *attr_name;
	name = regex_str = NULL;
	size_t cnt = 0;
	struct ldmsd_sec_ctxt sctxt;

	reqc->errcode = 0;

	attr_name = "name";
	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!name)
		goto einval;

	attr_name = "regex";
	regex_str = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_REGEX);
	if (!regex_str)
		goto einval;

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);
	reqc->errcode = ldmsd_strgp_prdcr_del(name, regex_str, &sctxt);
	switch (reqc->errcode) {
	case 0:
		break;
	case ENOENT:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The storage policy specified "
				"does not exist.");
		break;
	case EBUSY:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"Configuration changes cannot be made "
			"while the storage policy is running.");
		break;
	case EEXIST:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The specified regex does not match "
				"any condition.");
		reqc->errcode = ENOENT;
		break;
	case EACCES:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Permission denied.");
		break;
	default:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "Error %d %s", reqc->errcode,
			       ovis_errno_abbvr(reqc->errcode));
	}
	goto send_reply;
einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The attribute '%s' is required by %s.", attr_name,
			"strgp_prdcr_del");
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (name)
		free(name);
	if (regex_str)
		free(regex_str);
	return 0;
}

static int strgp_metric_add_handler(ldmsd_req_ctxt_t reqc)
{
	char *name, *metric_name, *attr_name;
	name = metric_name = NULL;
	size_t cnt = 0;
	struct ldmsd_sec_ctxt sctxt;

	reqc->errcode = 0;

	attr_name = "name";
	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!name)
		goto einval;

	attr_name = "metric";
	metric_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_METRIC);
	if (!metric_name)
		goto einval;

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);
	reqc->errcode = ldmsd_strgp_metric_add(name, metric_name, &sctxt);
	switch (reqc->errcode) {
	case 0:
		break;
	case ENOENT:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The storage policy specified "
				"does not exist.");
		break;
	case EBUSY:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Configuration changes cannot be made "
				"while the storage policy is running.");
		break;
	case EEXIST:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The specified metric is already present.");
		break;
	case ENOMEM:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Memory allocation failure.");
		break;
	case EACCES:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Permission denied.");
		break;
	default:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "Error %d %s", reqc->errcode,
			       ovis_errno_abbvr(reqc->errcode));
	}
	goto send_reply;
einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The attribute '%s' is required by %s.", attr_name,
			"strgp_metric_add");
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (name)
		free(name);
	if (metric_name)
		free(metric_name);
	return 0;
}

static int strgp_metric_del_handler(ldmsd_req_ctxt_t reqc)
{
	char *name, *metric_name, *attr_name;
	name = metric_name = NULL;
	size_t cnt = 0;
	struct ldmsd_sec_ctxt sctxt;

	reqc->errcode = 0;

	attr_name = "name";
	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!name)
		goto einval;

	attr_name = "metric";
	metric_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_METRIC);
	if (!metric_name)
		goto einval;

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);

	reqc->errcode = ldmsd_strgp_metric_del(name, metric_name, &sctxt);
	switch (reqc->errcode) {
	case 0:
		break;
	case ENOENT:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The storage policy specified does not exist.");
		break;
	case EBUSY:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Configuration changes cannot be made "
				"while the storage policy is running.");
		break;
	case EEXIST:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The specified metric was not found.");
		reqc->errcode = ENOENT;
		break;
	case EACCES:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "Permission denied.");
		break;
	default:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "Error %d %s", reqc->errcode,
			       ovis_errno_abbvr(reqc->errcode));
	}
	goto send_reply;
einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The attribute '%s' is required by %s.", attr_name,
			"strgp_metric_del");
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (name)
		free(name);
	if (metric_name)
		free(metric_name);
	return 0;
}

static int strgp_start_handler(ldmsd_req_ctxt_t reqc)
{
	char *name, *attr_name;
	name = NULL;
	size_t cnt = 0;
	struct ldmsd_sec_ctxt sctxt;

	reqc->errcode = 0;

	attr_name = "name";
	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!name) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"%dThe attribute '%s' is required by %s.",
				EINVAL, attr_name, "strgp_start");
		goto send_reply;
	}

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);
	reqc->errcode = ldmsd_strgp_start(name, &sctxt);
	switch (reqc->errcode) {
	case ENOENT:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The storage policy does not exist.");
		goto send_reply;
	case EPERM:
	case EACCES:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"Permission denied.");
		goto send_reply;
	case EBUSY:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The storage policy is already running.");
		goto send_reply;
	case 0:
		break;
	default:
		break;
	}

send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (name)
		free(name);
	return 0;
}

static int strgp_stop_handler(ldmsd_req_ctxt_t reqc)
{
	char *name, *attr_name;
	name = NULL;
	size_t cnt = 0;
	struct ldmsd_sec_ctxt sctxt;

	reqc->errcode = 0;

	attr_name = "name";
	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!name) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The attribute '%s' is required by %s.",
			       	attr_name, "strgp_stop");
		goto send_reply;
	}

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);
	reqc->errcode = ldmsd_strgp_stop(name, &sctxt);
	switch (reqc->errcode) {
	case 0:
		break;
	case ENOENT:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The storage policy does not exist.");
		break;
	case EBUSY:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The storage policy is not running.");
		break;
	case EACCES:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "Permission denied.");
		break;
	default:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "Error %d %s", reqc->errcode,
			       ovis_errno_abbvr(reqc->errcode));
	}
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (name)
		free(name);
	return 0;
}

int __strgp_status_json_obj(ldmsd_req_ctxt_t reqc, ldmsd_strgp_t strgp,
							int strgp_cnt)
{
	int rc;
	int match_count, metric_count;
	ldmsd_name_match_t match;
	ldmsd_strgp_metric_t metric;

	if (strgp_cnt) {
		rc = linebuf_printf(reqc, ",\n");
		if (rc)
			goto out;
	}

	ldmsd_strgp_lock(strgp);
	rc = linebuf_printf(reqc,
		       "{\"name\":\"%s\","
		       "\"container\":\"%s\","
		       "\"schema\":\"%s\","
		       "\"plugin\":\"%s\","
		       "\"state\":\"%s\","
		       "\"producers\":[",
		       strgp->obj.name,
		       strgp->container,
		       strgp->schema,
		       strgp->plugin_name,
		       ldmsd_strgp_state_str(strgp->state));
	if (rc)
		goto out;

	match_count = 0;
	for (match = ldmsd_strgp_prdcr_first(strgp); match;
	     match = ldmsd_strgp_prdcr_next(match)) {
		if (match_count) {
			rc = linebuf_printf(reqc, ",");
			if (rc)
				goto out;
		}
		match_count++;
		rc = linebuf_printf(reqc, "\"%s\"", match->regex_str);
		if (rc)
			goto out;
	}
	rc = linebuf_printf(reqc, "],\"metrics\":[");
	if (rc)
		goto out;

	metric_count = 0;
	for (metric = ldmsd_strgp_metric_first(strgp); metric;
	     metric = ldmsd_strgp_metric_next(metric)) {
		if (metric_count) {
			rc = linebuf_printf(reqc, ",");
			if (rc)
				goto out;
		}
		metric_count++;
		rc = linebuf_printf(reqc, "\"%s\"", metric->name);
		if (rc)
			goto out;
	}
	rc = linebuf_printf(reqc, "]}");
out:
	ldmsd_strgp_unlock(strgp);
	return rc;
}

static int strgp_status_handler(ldmsd_req_ctxt_t reqc)
{
	int rc = 0;
	size_t cnt = 0;
	struct ldmsd_req_attr_s attr;
	char *name;
	ldmsd_strgp_t strgp = NULL;
	int strgp_cnt;

	reqc->errcode = 0;
	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (name) {
		strgp = ldmsd_strgp_find(name);
		if (!strgp) {
			/* Not report any status */
			cnt = snprintf(reqc->line_buf, reqc->line_len,
				"strgp '%s' doesn't exist.", name);
			reqc->errcode = ENOENT;
			ldmsd_send_req_response(reqc, reqc->line_buf);
			return 0;
		}
	}

	/* Construct the json object of the strgp(s) */
	if (strgp) {
		rc = __strgp_status_json_obj(reqc, strgp, 0);
	} else {
		strgp_cnt = 0;
		ldmsd_cfg_lock(LDMSD_CFGOBJ_STRGP);
		for (strgp = ldmsd_strgp_first(); strgp;
			strgp = ldmsd_strgp_next(strgp)) {
			rc = __strgp_status_json_obj(reqc, strgp, strgp_cnt);
			if (rc) {
				ldmsd_cfg_unlock(LDMSD_CFGOBJ_STRGP);
				goto out;
			}
			strgp_cnt++;
		}
		ldmsd_cfg_unlock(LDMSD_CFGOBJ_STRGP);
	}
	cnt = reqc->line_off + 2; /* +2 for '[' and ']' */

	/* Send the json attribute header */
	attr.discrim = 1;
	attr.attr_len = cnt;
	attr.attr_id = LDMSD_ATTR_JSON;
	ldmsd_hton_req_attr(&attr);
	rc = ldmsd_append_reply(reqc, (char *)&attr, sizeof(attr), LDMSD_REQ_SOM_F);
	if (rc)
		goto out;

	/* Send the json object */
	rc = ldmsd_append_reply(reqc, "[", 1, 0);
	if (rc)
		goto out;
	if (reqc->line_off) {
		rc = ldmsd_append_reply(reqc, reqc->line_buf, reqc->line_off, 0);
		if (rc)
			goto out;
	}
	rc = ldmsd_append_reply(reqc, "]", 1, 0);
	if (rc)
		goto out;

	/* Send the terminating attribute */
	attr.discrim = 0;
	rc = ldmsd_append_reply(reqc, (char *)&attr.discrim, sizeof(uint32_t),
								LDMSD_REQ_EOM_F);
out:
	if (name)
		free(name);
	if (strgp)
		ldmsd_strgp_put(strgp);
	return rc;
}

static int updtr_add_handler(ldmsd_req_ctxt_t reqc)
{
	char *name, *offset_str, *interval_str, *push, *auto_interval, *attr_name;
	name = offset_str = interval_str = push = auto_interval = NULL;
	size_t cnt = 0;
	uid_t uid;
	gid_t gid;
	int perm;
	char *perm_s = NULL;
	int push_flags, is_auto_task;

	reqc->errcode = 0;

	attr_name = "name";
	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!name) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "The attribute 'name' is required.");
		goto send_reply;
	}
	if (0 == strncmp(LDMSD_FAILOVER_NAME_PREFIX, name,
			 sizeof(LDMSD_FAILOVER_NAME_PREFIX)-1)) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "%s is an invalid updtr name",
			       name);
		goto send_reply;
	}

	attr_name = "interval";
	interval_str = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_INTERVAL);
	if (!interval_str) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "The 'interval' attribute is required.");
		goto send_reply;
	}

	offset_str = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_OFFSET);
	push = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_PUSH);
	auto_interval = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_AUTO_INTERVAL);

	struct ldmsd_sec_ctxt sctxt;
	if (reqc->xprt->xprt) {
		ldms_xprt_cred_get(reqc->xprt->xprt, NULL, &sctxt.crd);
	} else {
		ldmsd_sec_ctxt_get(&sctxt);
	}
	uid = sctxt.crd.uid;
	gid = sctxt.crd.gid;

	perm = 0770;
	perm_s = ldmsd_req_attr_str_value_get_by_name(reqc, "perm");
	if (perm_s)
		perm = strtoul(perm_s, NULL, 0);

	if (auto_interval) {
		if (0 == strcasecmp(auto_interval, "true")) {
			if (push) {
				reqc->errcode = EINVAL;
				cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
						"auto_interval and push are "
						"incompatible options");
				goto send_reply;
			}
			is_auto_task = 1;
		} else if (0 == strcasecmp(auto_interval, "false")) {
			is_auto_task = 0;
		} else {
			reqc->errcode = EINVAL;
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				       "The auto_interval option requires "
				       "either 'true', or 'false'\n");
			goto send_reply;
		}
	} else {
		is_auto_task = 0;
	}
	push_flags = 0;
	if (push) {
		if (0 == strcasecmp(push, "onchange")) {
			push_flags = LDMSD_UPDTR_F_PUSH | LDMSD_UPDTR_F_PUSH_CHANGE;
		} else if (0 == strcasecmp(push, "true") || 0 == strcasecmp(push, "yes")) {
			push_flags = LDMSD_UPDTR_F_PUSH;
		} else {
			reqc->errcode = EINVAL;
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				       "The valud push options are \"onchange\", \"true\" "
				       "or \"yes\"\n");
			goto send_reply;
		}
		is_auto_task = 0;
	}
	ldmsd_updtr_t updtr = ldmsd_updtr_new_with_auth(name, interval_str,
							offset_str ? offset_str : "0",
							push_flags,
							is_auto_task,
							uid, gid, perm);
	if (!updtr) {
		reqc->errcode = errno;
		if (errno == EEXIST) {
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				       "The updtr %s already exists.", name);
		} else if (errno == ENOMEM) {
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				       "Out of memory");
		} else {
			if (!reqc->errcode)
				reqc->errcode = EINVAL;
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				       "The updtr could not be created.");
		}
	}

send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (name)
		free(name);
	if (interval_str)
		free(interval_str);
	if (auto_interval)
		free(auto_interval);
	if (offset_str)
		free(offset_str);
	if (push)
		free(push);
	if (perm_s)
		free(perm_s);
	return 0;
}

static int updtr_del_handler(ldmsd_req_ctxt_t reqc)
{
	char *name = NULL;
	size_t cnt = 0;
	struct ldmsd_sec_ctxt sctxt;

	reqc->errcode = 0;

	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!name)
		goto einval;

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);
	reqc->errcode = ldmsd_updtr_del(name, &sctxt);
	switch (reqc->errcode) {
	case 0:
		break;
	case ENOENT:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The updater specified does not exist.");
		break;
	case EBUSY:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The updater is in use.");
		break;
	case EACCES:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "Permission denied.");
		break;
	default:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "Error %d %s", reqc->errcode,
			       ovis_errno_abbvr(reqc->errcode));
	}
	goto send_reply;

einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The attribute 'name' is required by updtr_del.");
	goto send_reply;
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (name)
		free(name);
	return 0;
}

static int updtr_prdcr_add_handler(ldmsd_req_ctxt_t reqc)
{
	char *updtr_name, *prdcr_regex, *attr_name;
	updtr_name = prdcr_regex = NULL;
	size_t cnt = 0;
	struct ldmsd_sec_ctxt sctxt;
	reqc->errcode = 0;

	attr_name = "name";
	updtr_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!updtr_name)
		goto einval;

	if (0 == strncmp(LDMSD_FAILOVER_NAME_PREFIX, updtr_name,
			 sizeof(LDMSD_FAILOVER_NAME_PREFIX) - 1)) {
		goto ename;
	}

	attr_name = "regex";
	prdcr_regex = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_REGEX);
	if (!prdcr_regex)
		goto einval;

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);
	reqc->errcode = ldmsd_updtr_prdcr_add(updtr_name, prdcr_regex,
				reqc->line_buf, reqc->line_len, &sctxt);
	switch (reqc->errcode) {
	case 0:
		break;
	case EACCES:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "Permission denied.");
		break;
	case ENOENT:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The updater specified does not exist.");
		break;
	case EBUSY:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Configuration changes cannot be "
				"made while the updater is running.");
		break;
	case ENOMEM:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Memory allocation failure.");
		break;
	default:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "Error %d %s", reqc->errcode,
			       ovis_errno_abbvr(reqc->errcode));
	}
	goto send_reply;

ename:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"Bad prdcr name");
	goto send_reply;
einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The attribute '%s' is required by %s.", attr_name,
			"updtr_prdcr_add");
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (updtr_name)
		free(updtr_name);
	if (prdcr_regex)
		free(prdcr_regex);
	return 0;
}

static int updtr_prdcr_del_handler(ldmsd_req_ctxt_t reqc)
{
	char *updtr_name, *prdcr_regex, *attr_name;
	updtr_name = prdcr_regex = NULL;
	size_t cnt = 0;
	struct ldmsd_sec_ctxt sctxt;

	reqc->errcode = 0;

	attr_name = "name";
	updtr_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!updtr_name)
		goto einval;

	attr_name = "regex";
	prdcr_regex = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_REGEX);
	if (!prdcr_regex)
		goto einval;

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);
	reqc->errcode = ldmsd_updtr_prdcr_del(updtr_name, prdcr_regex,
			reqc->line_buf, reqc->line_len, &sctxt);
	switch (reqc->errcode) {
	case 0:
		break;
	case EACCES:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "Permission denied.");
		break;
	case ENOMEM:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The updater specified does not exist.");
		break;
	case EBUSY:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Configuration changes cannot be "
				"made while the updater is running,");
		break;
	case ENOENT:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The updater specified does not exist.");
		break;
	default:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "Error %d %s", reqc->errcode,
			       ovis_errno_abbvr(reqc->errcode));
	}
	goto send_reply;
einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The attribute '%s' is required by %s.", attr_name,
			"updtr_prdcr_del");
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (updtr_name)
		free(updtr_name);
	if (prdcr_regex)
		free(prdcr_regex);
	return 0;
}

static int updtr_match_add_handler(ldmsd_req_ctxt_t reqc)
{
	char *updtr_name, *regex_str, *match_str, *attr_name;
	updtr_name = regex_str = match_str = NULL;
	size_t cnt = 0;
	struct ldmsd_sec_ctxt sctxt;

	reqc->errcode = 0;

	attr_name = "name";
	updtr_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!updtr_name)
		goto einval;
	attr_name = "regex";
	regex_str = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_REGEX);
	if (!regex_str)
		goto einval;

	match_str = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_MATCH);

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);
	reqc->errcode = ldmsd_updtr_match_add(updtr_name, regex_str, match_str,
			reqc->line_buf, reqc->line_len, &sctxt);
	switch (reqc->errcode) {
	case 0:
		break;
	case ENOENT:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The updater specified does not exist.");
		break;
	case EBUSY:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Configuration changes cannot be made "
				"while the updater is running.");
		break;
	case ENOMEM:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Out of memory.");
		break;
	case EINVAL:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The value '%s' for match= is invalid.",
				match_str);
		break;
	case EACCES:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "Permission denied.");
		break;
	default:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "Error %d %s", reqc->errcode,
			       ovis_errno_abbvr(reqc->errcode));
	}
	goto send_reply;

einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The attribute '%s' is required by %s.", attr_name,
			"updtr_match_add");
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (updtr_name)
		free(updtr_name);
	if (regex_str)
		free(regex_str);
	if (match_str)
		free(match_str);
	return 0;
}

static int updtr_match_del_handler(ldmsd_req_ctxt_t reqc)
{
	char *updtr_name, *regex_str, *match_str, *attr_name;
	updtr_name = regex_str = match_str = NULL;
	size_t cnt = 0;
	struct ldmsd_sec_ctxt sctxt;

	reqc->errcode = 0;

	attr_name = "name";
	updtr_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!updtr_name)
		goto einval;
	attr_name = "regex";
	regex_str = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_REGEX);
	if (!regex_str)
		goto einval;

	match_str  = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_MATCH);

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);
	reqc->errcode = ldmsd_updtr_match_del(updtr_name, regex_str, match_str,
					      &sctxt);
	switch (reqc->errcode) {
	case 0:
		break;
	case ENOENT:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The updater specified does not exist.");
		break;
	case EBUSY:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Configuration changes cannot be made "
				"while the updater is running.");
		break;
	case -ENOENT:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The specified regex does not match any condition.");
		reqc->errcode = -reqc->errcode;
		break;
	case EINVAL:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"Unrecognized match type '%s'", match_str);
		break;
	case EACCES:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "Permission denied.");
		break;
	default:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "Error %d %s", reqc->errcode,
			       ovis_errno_abbvr(reqc->errcode));
	}
	goto send_reply;
einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The attribute '%s' is required by %s.", attr_name,
			"updtr_match_del");
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (updtr_name)
		free(updtr_name);
	if (regex_str)
		free(regex_str);
	if (match_str)
		free(match_str);
	return 0;
}

static int updtr_start_handler(ldmsd_req_ctxt_t reqc)
{
	char *updtr_name, *interval_str, *offset_str, *auto_interval;
	updtr_name = interval_str = offset_str = auto_interval = NULL;
	size_t cnt = 0;
	struct ldmsd_sec_ctxt sctxt;

	reqc->errcode = 0;

	updtr_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!updtr_name) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The updater name must be specified.");
		goto send_reply;
	}
	interval_str = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_INTERVAL);
	offset_str  = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_OFFSET);
	auto_interval = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_AUTO_INTERVAL);

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);
	reqc->errcode = ldmsd_updtr_start(updtr_name, interval_str, offset_str,
					  auto_interval, &sctxt);
	switch (reqc->errcode) {
	case 0:
		break;
	case ENOENT:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The updater specified does not exist.");
		break;
	case EBUSY:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The updater is already running.");
		break;
	case EACCES:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "Permission denied.");
		break;
	default:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "Error %d %s", reqc->errcode,
			       ovis_errno_abbvr(reqc->errcode));
	}

send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (updtr_name)
		free(updtr_name);
	if (interval_str)
		free(interval_str);
	if (offset_str)
		free(offset_str);
	return 0;
}

static int updtr_stop_handler(ldmsd_req_ctxt_t reqc)
{
	char *updtr_name = NULL;
	size_t cnt = 0;
	struct ldmsd_sec_ctxt sctxt;

	reqc->errcode = 0;

	updtr_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!updtr_name) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The updater name must be specified.");
		goto send_reply;
	}

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);
	reqc->errcode = ldmsd_updtr_stop(updtr_name, &sctxt);
	switch (reqc->errcode) {
	case 0:
		break;
	case ENOENT:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The updater specified does not exist.");
		break;
	case EBUSY:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The updater is already stopped.");
		break;
	case EACCES:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "Permission denied.");
		break;
	default:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "Error %d %s", reqc->errcode,
			       ovis_errno_abbvr(reqc->errcode));
	}

send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (updtr_name)
		free(updtr_name);
	return 0;
}

static const char *update_mode(int push_flags)
{
	if (!push_flags)
		return "Pull";
	if (push_flags & LDMSD_UPDTR_F_PUSH_CHANGE)
		return "Push on Change";
	return "Push on Request";
}

int __updtr_status_json_obj(ldmsd_req_ctxt_t reqc, ldmsd_updtr_t updtr,
							int updtr_cnt)
{
	int rc;
	ldmsd_prdcr_ref_t ref;
	ldmsd_prdcr_t prdcr;
	int prdcr_count;
	long default_offset = 0;
	const char *prdcr_state_str(enum ldmsd_prdcr_state state);

	if (updtr_cnt) {
		rc = linebuf_printf(reqc, ",\n");
		if (rc)
			return rc;
	}

	ldmsd_updtr_lock(updtr);
	if (updtr->default_task.sched.offset_us != LDMSD_UPDT_HINT_OFFSET_NONE)
		default_offset = updtr->default_task.sched.offset_us;
	rc = linebuf_printf(reqc,
		"{\"name\":\"%s\","
		"\"interval\":\"%ld\","
		"\"offset\":\"%ld\","
		"\"sync\":\"%s\","
		"\"mode\":\"%s\","
		"\"state\":\"%s\","
		"\"producers\":[",
		updtr->obj.name,
		updtr->default_task.sched.intrvl_us,
		default_offset,
		((updtr->default_task.task_flags==LDMSD_TASK_F_SYNCHRONOUS)?"true":"false"),
		update_mode(updtr->push_flags),
		ldmsd_updtr_state_str(updtr->state));
	if (rc)
		goto out;

	prdcr_count = 0;
	for (ref = ldmsd_updtr_prdcr_first(updtr); ref;
	     ref = ldmsd_updtr_prdcr_next(ref)) {
		if (prdcr_count) {
			rc = linebuf_printf(reqc, ",\n");
			if (rc)
				goto out;
		}
		prdcr_count++;
		prdcr = ref->prdcr;
		rc = linebuf_printf(reqc,
			       "{\"name\":\"%s\","
			       "\"host\":\"%s\","
			       "\"port\":%hu,"
			       "\"transport\":\"%s\","
			       "\"state\":\"%s\"}",
			       prdcr->obj.name,
			       prdcr->host_name,
			       prdcr->port_no,
			       prdcr->xprt_name,
			       prdcr_state_str(prdcr->conn_state));
		if (rc)
			goto out;
	}
	rc = linebuf_printf(reqc, "]}");
out:
	ldmsd_updtr_unlock(updtr);
	return rc;
}

static int updtr_status_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	size_t cnt = 0;
	struct ldmsd_req_attr_s attr;
	char *name;
	int updtr_cnt;
	ldmsd_updtr_t updtr = NULL;

	reqc->errcode = 0;

	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (name) {
		updtr = ldmsd_updtr_find(name);
		if (!updtr) {
			/* Not report any status */
			cnt = snprintf(reqc->line_buf, reqc->line_len,
				"updtr '%s' doesn't exist.", name);
			reqc->errcode = ENOENT;
			ldmsd_send_req_response(reqc, reqc->line_buf);
			return 0;
		}
	}

	/* Construct the json object of the updater(s) */
	if (updtr) {
		rc = __updtr_status_json_obj(reqc, updtr, 0);
		if (rc)
			goto out;
	} else {
		updtr_cnt = 0;
		ldmsd_cfg_lock(LDMSD_CFGOBJ_UPDTR);
		for (updtr = ldmsd_updtr_first(); updtr;
				updtr = ldmsd_updtr_next(updtr)) {
			rc = __updtr_status_json_obj(reqc, updtr, updtr_cnt);
			if (rc) {
				ldmsd_cfg_unlock(LDMSD_CFGOBJ_UPDTR);
				goto out;
			}
			updtr_cnt++;
		}
		ldmsd_cfg_unlock(LDMSD_CFGOBJ_UPDTR);
	}
	cnt = reqc->line_off + 2; /* +2 for '[' and ']' */

	/* Send the json attribute header */
	attr.discrim = 1;
	attr.attr_len = cnt;
	attr.attr_id = LDMSD_ATTR_JSON;
	ldmsd_hton_req_attr(&attr);
	rc = ldmsd_append_reply(reqc, (char *)&attr, sizeof(attr), LDMSD_REQ_SOM_F);
	if (rc)
		goto out;

	/* send the json object */
	rc = ldmsd_append_reply(reqc, "[", 1, 0);
	if (rc)
		goto out;
	if (reqc->line_off) {
		rc = ldmsd_append_reply(reqc, reqc->line_buf, reqc->line_off, 0);
		if (rc)
			goto out;
	}
	rc = ldmsd_append_reply(reqc, "]", 1, 0);
	if (rc)
		goto out;

	/* Send the terminating attribute */
	attr.discrim = 0;
	rc = ldmsd_append_reply(reqc, (char *)&attr.discrim, sizeof(uint32_t),
								LDMSD_REQ_EOM_F);
out:
	if (name)
		free(name);
	if (updtr)
		ldmsd_updtr_put(updtr);
	return rc;
}

static int __updtr_task_json_obj(ldmsd_req_ctxt_t reqc, ldmsd_updtr_task_t task)
{
	int rc;
	rc = linebuf_printf(reqc,
			"{\"interval_us\":\"%ld\",", task->sched.intrvl_us);
	if (rc)
		return rc;
	if (task->sched.offset_us == LDMSD_UPDT_HINT_OFFSET_NONE) {
		rc = linebuf_printf(reqc, "\"offset_us\":\"None\",");
	} else {
		rc = linebuf_printf(reqc,
			"\"offset_us\":\"%ld\",", task->sched.offset_us);
	}
	if (rc)
		return rc;
	rc = linebuf_printf(reqc, "\"default_task\":\"%s\"}",
			(task->is_default)?"true":"false");
	return rc;
}

int __updtr_task_tree_json_obj(ldmsd_req_ctxt_t reqc,
				ldmsd_updtr_t updtr)
{
	int rc;
	ldmsd_updtr_task_t task;
	struct rbn *rbn;
	rc = 0;

	ldmsd_updtr_lock(updtr);
	rc = linebuf_printf(reqc,
			"{\"name\":\"%s\","
			"\"tasks\":[", updtr->obj.name);
	if (rc)
		goto out;

	task = &updtr->default_task;
	rc = __updtr_task_json_obj(reqc, task);
	if (rc)
		goto out;

	rbn = rbt_min(&updtr->task_tree);
	while (rbn) {
		rc = linebuf_printf(reqc, ",");
		if (rc)
			goto out;
		task = container_of(rbn, struct ldmsd_updtr_task, rbn);
		rc = __updtr_task_json_obj(reqc, task);
		if (rc)
			goto out;
		rbn = rbn_succ(rbn);
	}
	rc = linebuf_printf(reqc, "]}");
out:
	ldmsd_updtr_unlock(updtr);
	return rc;
}

static int updtr_task_status_handler(ldmsd_req_ctxt_t reqc)
{
	int rc, updtr_count;
	size_t cnt = 0;
	char *name;
	ldmsd_updtr_t updtr = NULL;
	struct ldmsd_req_attr_s attr;

	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (name) {
		updtr = ldmsd_updtr_find(name);
		if (!updtr) {
			cnt = snprintf(reqc->line_buf, reqc->line_len, "updtr '%s' not found", name);
			ldmsd_send_error_reply(reqc->xprt, reqc->rec_no, ENOENT,
							reqc->line_buf, cnt);
			return 0;
		}
		rc = __updtr_task_tree_json_obj(reqc, updtr);
		if (rc)
			goto err;
	} else {
		updtr_count = 0;
		ldmsd_cfg_lock(LDMSD_CFGOBJ_UPDTR);
		for (updtr = ldmsd_updtr_first(); updtr; updtr = ldmsd_updtr_next(updtr)) {
			if (updtr_count) {
				rc = linebuf_printf(reqc, ",\n");
				if (rc) {
					ldmsd_cfg_unlock(LDMSD_CFGOBJ_UPDTR);
					goto err;
				}
			}
			rc = __updtr_task_tree_json_obj(reqc, updtr);
			if (rc) {
				ldmsd_cfg_unlock(LDMSD_CFGOBJ_UPDTR);
				goto err;
			}
			updtr_count += 1;
		}
		ldmsd_cfg_unlock(LDMSD_CFGOBJ_UPDTR);
	}
	cnt = reqc->line_off + 2; /* +2 for the [ and ]. */

	attr.discrim = 1;
	attr.attr_len = cnt;
	attr.attr_id = LDMSD_ATTR_JSON;
	ldmsd_hton_req_attr(&attr);
	rc = ldmsd_append_reply(reqc, (char *)&attr, sizeof(attr), LDMSD_REQ_SOM_F);
	if (rc)
		goto err;

	rc = ldmsd_append_reply(reqc, "[", 1, 0);
	if (rc)
		goto err;
	rc = ldmsd_append_reply(reqc, reqc->line_buf, reqc->line_off, 0);
	if (rc)
		goto err;
	rc = ldmsd_append_reply(reqc, "]", 1, 0);
	if (rc)
		goto err;

	attr.discrim = 0;
	rc = ldmsd_append_reply(reqc, (char *)&attr.discrim,
				sizeof(uint32_t), LDMSD_REQ_EOM_F);
	goto out;

err:
	ldmsd_send_error_reply(reqc->xprt, reqc->rec_no, rc,
						"internal error", 15);
out:
	if (name)
		free(name);
	if (updtr)
		ldmsd_updtr_put(updtr);
	return rc;
}

int __prdcr_hint_set_list_json_obj(ldmsd_req_ctxt_t reqc,
				ldmsd_updt_hint_set_list_t list)
{
	ldmsd_prdcr_set_t set;
	int rc, set_count;
	set_count = 0;

	if (LIST_EMPTY(&list->list))
		return 0;
	set = LIST_FIRST(&list->list);
	pthread_mutex_lock(&set->lock);
	rc = linebuf_printf(reqc,
				"{\"interval_us\":\"%ld\",",
				set->updt_hint.intrvl_us);
	if (rc)
		goto err;
	if (set->updt_hint.offset_us == LDMSD_UPDT_HINT_OFFSET_NONE) {
		rc = linebuf_printf(reqc, "\"offset_us\":\"None\",");
	} else {
		rc = linebuf_printf(reqc,
				"\"offset_us\":\"%ld\",",
				set->updt_hint.offset_us);
	}
	if (rc)
		goto err;
	rc = linebuf_printf(reqc, "\"sets\":[");
	if (rc)
		goto err;
	while (set) {
		if (set_count) {
			rc = linebuf_printf(reqc, ",");
			if (rc)
				goto err;
		}
		rc = linebuf_printf(reqc, "\"%s\"", set->inst_name);
		if (rc)
			goto err;
		set_count++;
		pthread_mutex_unlock(&set->lock);
		set = LIST_NEXT(set, updt_hint_entry);
		if (!set)
			break;
		pthread_mutex_lock(&set->lock);
	}
	rc = linebuf_printf(reqc, "]}");
	return rc;
err:
	pthread_mutex_unlock(&set->lock);
	return rc;
}

/* The caller must hold the prdcr lock */
int __prdcr_hint_set_tree_json_obj(ldmsd_req_ctxt_t reqc, ldmsd_prdcr_t prdcr)
{
	int rc, count;
	struct rbn *rbn;
	ldmsd_updt_hint_set_list_t list;

	rc = linebuf_printf(reqc,
			"{\"name\":\"%s\","
			"\"hints\":[", prdcr->obj.name);
	if (rc)
		return rc;
	count = 0;
	rbn = rbt_min(&prdcr->hint_set_tree);
	while (rbn) {
		if (0 < count)
			rc = linebuf_printf(reqc, ",");
		list = container_of(rbn, struct ldmsd_updt_hint_set_list, rbn);
		rc = __prdcr_hint_set_list_json_obj(reqc, list);
		if (rc)
			return rc;
		count++;
		rbn = rbn_succ(rbn);
	}
	rc = linebuf_printf(reqc, "]}");
	return rc;
}

static int prdcr_hint_tree_status_handler(ldmsd_req_ctxt_t reqc)
{
	size_t cnt = 0;
	int rc, prdcr_count;
	ldmsd_prdcr_t prdcr = NULL;
	char *name;
	struct ldmsd_req_attr_s attr;

	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (name) {
		prdcr = ldmsd_prdcr_find(name);
		if (!prdcr) {
			cnt = snprintf(reqc->line_buf, reqc->line_len,
					"prdcr '%s' not found", name);
			reqc->errcode = ENOENT;
			ldmsd_send_req_response(reqc, reqc->line_buf);
			return 0;
		}
		ldmsd_prdcr_lock(prdcr);
		rc = __prdcr_hint_set_tree_json_obj(reqc, prdcr);
		ldmsd_prdcr_unlock(prdcr);
		if (rc)
			goto intr_err;
	} else {
		prdcr_count = 0;
		ldmsd_cfg_lock(LDMSD_CFGOBJ_PRDCR);
		for (prdcr = ldmsd_prdcr_first(); prdcr;
				prdcr = ldmsd_prdcr_next(prdcr)) {
			if (prdcr_count) {
				rc = linebuf_printf(reqc, ",\n");
			}
			ldmsd_prdcr_lock(prdcr);
			rc = __prdcr_hint_set_tree_json_obj(reqc, prdcr);
			ldmsd_prdcr_unlock(prdcr);
			if (rc) {
				ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR);
				goto intr_err;
			}
			prdcr_count +=1;
		}
		ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR);
	}

	cnt = reqc->line_off + 2; /*   +2 for [ and ] */

	attr.discrim = 1;
	attr.attr_id = LDMSD_ATTR_JSON;
	attr.attr_len = cnt;
	ldmsd_hton_req_attr(&attr);
	rc = ldmsd_append_reply(reqc, (char *)&attr,
			sizeof(struct ldmsd_req_attr_s), LDMSD_REQ_SOM_F);
	if (rc)
		goto intr_err;
	rc = ldmsd_append_reply(reqc, "[", 1, 0);
	if (rc)
		goto intr_err;
	rc = ldmsd_append_reply(reqc, reqc->line_buf, reqc->line_off, 0);
	if (rc)
		goto intr_err;
	rc = ldmsd_append_reply(reqc, "]", 1, 0);
	if (rc)
		goto intr_err;
	attr.discrim = 0;
	rc = ldmsd_append_reply(reqc, (char *)&attr.discrim,
				sizeof(uint32_t), LDMSD_REQ_EOM_F);
	if (rc)
		goto intr_err;
	goto out;

intr_err:
	ldmsd_send_error_reply(reqc->xprt, reqc->rec_no, EINTR,
				"interval error", 14);
out:
	if (name)
		free(name);
	if (prdcr)
		ldmsd_prdcr_put(prdcr);
	return rc;
}

static int setgroup_add_handler(ldmsd_req_ctxt_t reqc)
{
	int rc = 0;
	char *name = NULL;
	char *producer = NULL;
	char *interval = NULL; /* for update hint */
	char *offset = NULL; /* for update hint */
	ldms_set_t grp = NULL;
	long offset_us;

	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!name) {
		Snprintf(&reqc->line_buf, &reqc->line_len,
			"missing `name` attribute");
		rc = EINVAL;
		goto out;
	}

	producer = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_PRODUCER);
	if (!producer) {
		producer = strdup(ldmsd_myname_get());
		if (!producer) {
			Snprintf(&reqc->line_buf, &reqc->line_len,
				"Memory allocation error");
			rc = ENOMEM;
			goto out;
		}
	}

	grp = ldmsd_group_new(name);
	if (!grp) {
		rc = errno;
		if (errno == EEXIST) {
			Snprintf(&reqc->line_buf, &reqc->line_len,
				"A set or a group existed with the given name.");
		} else {
			Snprintf(&reqc->line_buf, &reqc->line_len,
				"Group creation error: %d", rc);
		}
		goto out;
	}
	rc = ldms_set_producer_name_set(grp, producer);
	if (rc) {
		Snprintf(&reqc->line_buf, &reqc->line_len,
				"Group producer name set error: %d", rc);
		goto err;
	}
	interval = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_INTERVAL);
	offset = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_OFFSET);
	if (interval) {
		if (offset) {
			offset_us = atoi(offset);
		} else {
			offset_us = LDMSD_UPDT_HINT_OFFSET_NONE;
		}
		rc = ldmsd_set_update_hint_set(grp, atoi(interval), offset_us);
		if (rc) {
			Snprintf(&reqc->line_buf, &reqc->line_len,
					"Hint update error: %d", rc);
			goto err;
		}
	}
	rc = ldms_set_publish(grp);
	if (rc) {
		Snprintf(&reqc->line_buf, &reqc->line_len,
				"Group set publish error: %d", rc);
		goto err;
	}
	/* rc is 0 */
	goto out;

err:
	ldms_set_delete(grp);
	grp = NULL;
out:
	reqc->errcode = rc;
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (name)
		free(name);
	if (producer)
		free(producer);
	if (interval)
		free(interval);
	if (offset)
		free(offset);
	if (grp)
		ldms_set_put(grp);
	return rc;
}

static int setgroup_mod_handler(ldmsd_req_ctxt_t reqc)
{
	int rc = 0;
	char *name = NULL;
	char *interval = NULL; /* for update hint */
	char *offset = NULL; /* for update hint */
	ldms_set_t grp = NULL;
	long offset_us;

	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!name) {
		Snprintf(&reqc->line_buf, &reqc->line_len,
			"missing `name` attribute");
		rc = EINVAL;
		goto out;
	}

	grp = ldms_set_by_name(name);
	if (!grp) {
		rc = errno;
		if (errno == ENOENT) {
			Snprintf(&reqc->line_buf, &reqc->line_len,
				"Group not found.");
		} else {
			Snprintf(&reqc->line_buf, &reqc->line_len,
				"Group get error: %d", rc);
		}
		goto out;
	}

	if (0 == (ldmsd_group_check(grp) & LDMSD_GROUP_IS_GROUP)) {
		/* not a group */
		rc = EINVAL;
		Snprintf(&reqc->line_buf, &reqc->line_len,
				"Not a group");
		goto out;
	}

	interval = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_INTERVAL);
	offset = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_OFFSET);
	if (interval) {
		if (offset) {
			offset_us = atoi(offset);
		} else {
			offset_us = LDMSD_UPDT_HINT_OFFSET_NONE;
		}
		rc = ldmsd_set_update_hint_set(grp, atoi(interval), offset_us);
		if (rc) {
			Snprintf(&reqc->line_buf, &reqc->line_len,
					"Hint update error: %d", rc);
			goto err;
		}
	}
	/* rc is 0 */
	goto out;

err:
	ldms_set_put(grp);
	grp = NULL;
out:
	reqc->errcode = rc;
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (name)
		free(name);
	if (interval)
		free(interval);
	if (offset)
		free(offset);
	if (grp)
		ldms_set_put(grp);
	return rc;
}

static int setgroup_del_handler(ldmsd_req_ctxt_t reqc)
{
	int rc = 0;
	char *name = NULL;
	ldms_set_t grp;

	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!name) {
		Snprintf(&reqc->line_buf, &reqc->line_len,
			"missing `name` attribute");
		rc = EINVAL;
		goto out;
	}

	grp = ldms_set_by_name(name);
	if (!grp) {
		rc = errno;
		if (rc == ENOENT) {
			Snprintf(&reqc->line_buf, &reqc->line_len,
				"Group not found.");
		} else {
			Snprintf(&reqc->line_buf, &reqc->line_len,
				"Group get error: %d", rc);
		}
		goto out;
	}

	if (0 == (ldmsd_group_check(grp) & LDMSD_GROUP_IS_GROUP)) {
		/* not a group */
		rc = EINVAL;
		Snprintf(&reqc->line_buf, &reqc->line_len,
				"Not a group");
		goto out;
	}

	ldms_set_delete(grp);
	rc = 0;

out:
	reqc->errcode = rc;
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (name)
		free(name);
	return rc;
}

static int setgroup_ins_handler(ldmsd_req_ctxt_t reqc)
{
	int rc = 0;
	const char *delim = ",";
	char *name = NULL;
	char *instance = NULL;
	char *sname;
	char *p;
	ldms_set_t grp = NULL;

	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!name) {
		Snprintf(&reqc->line_buf, &reqc->line_len,
			"missing `name` attribute");
		rc = EINVAL;
		goto out;
	}

	grp = ldms_set_by_name(name);
	if (!grp) {
		rc = errno;
		if (rc == ENOENT) {
			Snprintf(&reqc->line_buf, &reqc->line_len,
				"Group not found.");
		} else {
			Snprintf(&reqc->line_buf, &reqc->line_len,
				"Group get error: %d", rc);
		}
		goto out;
	}

	if (0 == (ldmsd_group_check(grp) & LDMSD_GROUP_IS_GROUP)) {
		/* not a group */
		rc = EINVAL;
		Snprintf(&reqc->line_buf, &reqc->line_len,
				"Not a group");
		goto out;
	}

	instance = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_INSTANCE);
	sname = strtok_r(instance, delim, &p);
	while (sname) {
		rc = ldmsd_group_set_add(grp, sname);
		if (rc) {
			Snprintf(&reqc->line_buf, &reqc->line_len,
				"Group set insert error: %d", rc);
			goto out;
		}
		sname = strtok_r(NULL, delim, &p);
	}
	/* rc is 0 */

out:
	reqc->errcode = rc;
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (name)
		free(name);
	if (instance)
		free(instance);
	if (grp)
		ldms_set_put(grp);
	return rc;
}

static int setgroup_rm_handler(ldmsd_req_ctxt_t reqc)
{
	int rc = 0;
	const char *delim = ",";
	char *name = NULL;
	char *instance = NULL;
	char *sname;
	char *p;
	ldms_set_t grp = NULL;

	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!name) {
		Snprintf(&reqc->line_buf, &reqc->line_len,
			"missing `name` attribute");
		rc = EINVAL;
		goto out;
	}

	grp = ldms_set_by_name(name);
	if (!grp) {
		rc = errno;
		if (rc == ENOENT) {
			Snprintf(&reqc->line_buf, &reqc->line_len,
				"Group not found.");
		} else {
			Snprintf(&reqc->line_buf, &reqc->line_len,
				"Group get error: %d", rc);
		}
		goto out;
	}

	if (0 == (ldmsd_group_check(grp) & LDMSD_GROUP_IS_GROUP)) {
		/* not a group */
		rc = EINVAL;
		Snprintf(&reqc->line_buf, &reqc->line_len,
				"Not a group");
		goto out;
	}

	instance = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_INSTANCE);
	sname = strtok_r(instance, delim, &p);
	while (sname) {
		rc = ldmsd_group_set_rm(grp, sname);
		if (rc) {
			Snprintf(&reqc->line_buf, &reqc->line_len,
				"Group set remove error: %d", rc);
			goto out;
		}
		sname = strtok_r(NULL, delim, &p);
	}
	/* rc is 0 */

out:
	reqc->errcode = rc;
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (name)
		free(name);
	if (instance)
		free(instance);
	if (grp)
		ldms_set_put(grp);
	return rc;
}

static char *state_str[] = {
	[LDMSD_PLUGIN_OTHER] = "other",
	[LDMSD_PLUGIN_SAMPLER] = "sampler",
	[LDMSD_PLUGIN_STORE] = "store",
};

static char *plugn_state_str(enum ldmsd_plugin_type type)
{
	if (type <= LDMSD_PLUGIN_STORE)
		return state_str[type];
	return "unknown";
}

extern int ldmsd_start_sampler(char *plugin_name, char *interval, char *offset);
extern int ldmsd_stop_sampler(char *plugin);
extern int ldmsd_load_plugin(char *plugin_name, char *errstr, size_t errlen);
extern int ldmsd_term_plugin(char *plugin_name);
extern int ldmsd_config_plugin(char *plugin_name,
			struct attr_value_list *_av_list,
			struct attr_value_list *_kw_list);

static int plugn_start_handler(ldmsd_req_ctxt_t reqc)
{
	char *plugin_name, *interval_us, *offset, *attr_name;
	plugin_name = interval_us = offset = NULL;
	size_t cnt = 0;

	attr_name = "name";
	plugin_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!plugin_name)
		goto einval;
	attr_name = "interval";
	interval_us = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_INTERVAL);
	if (!interval_us)
		goto einval;

	offset = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_OFFSET);

	reqc->errcode = ldmsd_start_sampler(plugin_name, interval_us, offset);
	if (reqc->errcode == 0) {
		goto send_reply;
	} else if (reqc->errcode == EINVAL) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"interval '%s' invalid", interval_us);
	} else if (reqc->errcode == -EINVAL) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The specified plugin is not a sampler.");
	} else if (reqc->errcode == ENOENT) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Sampler '%s' not found.", plugin_name);
	} else if (reqc->errcode == EBUSY) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Sampler '%s' is already running.", plugin_name);
	} else if (reqc->errcode == EDOM) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Sampler parameters interval and offset are "
				"incompatible.");
	} else {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Failed to start the sampler '%s'.", plugin_name);
	}
	goto send_reply;

einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The attribute '%s' is required by start.", attr_name);
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (plugin_name)
		free(plugin_name);
	if (interval_us)
		free(interval_us);
	if (offset)
		free(offset);
	return 0;
}

static int plugn_stop_handler(ldmsd_req_ctxt_t reqc)
{
	char *plugin_name, *attr_name;
	plugin_name = NULL;
	size_t cnt = 0;

	attr_name = "name";
	plugin_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!plugin_name)
		goto einval;

	reqc->errcode = ldmsd_stop_sampler(plugin_name);
	if (reqc->errcode == 0) {
		goto send_reply;
	} else if (reqc->errcode == ENOENT) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Sampler '%s' not found.", plugin_name);
	} else if (reqc->errcode == EINVAL) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The plugin '%s' is not a sampler.",
				plugin_name);
	} else if (reqc->errcode == -EBUSY) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The sampler '%s' is not running.", plugin_name);
	} else {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Failed to stop sampler '%s'", plugin_name);
	}
	goto send_reply;

einval:
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The attribute '%s' is required by stop.", attr_name);
	reqc->errcode = EINVAL;
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (plugin_name)
		free(plugin_name);
	return 0;
}

int __plugn_status_json_obj(ldmsd_req_ctxt_t reqc)
{
	extern struct plugin_list plugin_list;
	struct ldmsd_plugin_cfg *p;
	int rc, count;
	reqc->errcode = 0;

	rc = linebuf_printf(reqc, "[");
	if (rc)
		return rc;
	count = 0;
	LIST_FOREACH(p, &plugin_list, entry) {
		if (count) {
			rc = linebuf_printf(reqc, ",\n");
			if (rc)
				return rc;
		}

		count++;
		rc = linebuf_printf(reqc,
			       "{\"name\":\"%s\",\"type\":\"%s\","
			       "\"sample_interval_us\":%ld,"
			       "\"sample_offset_us\":%ld,"
			       "\"libpath\":\"%s\"}",
			       p->plugin->name,
			       plugn_state_str(p->plugin->type),
			       p->sample_interval_us, p->sample_offset_us,
			       p->libpath);
		if (rc)
			return rc;
	}
	rc = linebuf_printf(reqc, "]");
	return rc;
}

static int plugn_status_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	struct ldmsd_req_attr_s attr;

	rc = __plugn_status_json_obj(reqc);
	if (rc)
		return rc;

	attr.discrim = 1;
	attr.attr_len = reqc->line_off;
	attr.attr_id = LDMSD_ATTR_JSON;
	ldmsd_hton_req_attr(&attr);
	rc = ldmsd_append_reply(reqc, (char *)&attr, sizeof(attr), LDMSD_REQ_SOM_F);
	if (rc)
		return rc;
	rc = ldmsd_append_reply(reqc, reqc->line_buf, reqc->line_off, 0);
	if (rc)
		return rc;

	attr.discrim = 0;
	rc = ldmsd_append_reply(reqc, (char *)&attr.discrim,
				sizeof(uint32_t), LDMSD_REQ_EOM_F);
	return rc;
}

static int plugn_load_handler(ldmsd_req_ctxt_t reqc)
{
	char *plugin_name, *attr_name;
	plugin_name = NULL;
	size_t cnt = 0;

	attr_name = "name";
	plugin_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!plugin_name) {
		ldmsd_log(LDMSD_LERROR, "load plugin called without name=$plugin");
		goto einval;
	}

	reqc->errcode = ldmsd_load_plugin(plugin_name, reqc->line_buf,
							reqc->line_len);
	if (reqc->errcode)
		cnt = strlen(reqc->line_buf) + 1;
	goto send_reply;

einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The attribute '%s' is required by load.", attr_name);
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (plugin_name)
		free(plugin_name);
	return 0;
}

static int plugn_term_handler(ldmsd_req_ctxt_t reqc)
{
	char *plugin_name, *attr_name;
	plugin_name = NULL;
	size_t cnt = 0;

	attr_name = "name";
	plugin_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!plugin_name)
		goto einval;

	reqc->errcode = ldmsd_term_plugin(plugin_name);
	if (reqc->errcode == 0) {
		goto send_reply;
	} else if (reqc->errcode == ENOENT) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"plugin '%s' not found.", plugin_name);
	} else if (reqc->errcode == EINVAL) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The specified plugin '%s' has "
				"active users and cannot be terminated.",
				plugin_name);
	} else {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Failed to terminate the plugin '%s'.",
				plugin_name);
	}
	goto send_reply;

einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The attribute '%s' is required by term.", attr_name);
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (plugin_name)
		free(plugin_name);
	return 0;
}

static int plugn_config_handler(ldmsd_req_ctxt_t reqc)
{
	char *plugin_name, *config_attr, *attr_name;
	plugin_name = config_attr = NULL;
	struct attr_value_list *av_list = NULL;
	struct attr_value_list *kw_list = NULL;
	size_t cnt = 0;
	reqc->errcode = 0;

	attr_name = "name";
	plugin_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!plugin_name)
		goto einval;
	config_attr = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_STRING);
	if (!config_attr) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"No config attributes are provided.");
		reqc->errcode = EINVAL;
		goto send_reply;
	}

	char *cmd_s;
	int tokens;

	/*
	 * Count the numebr of spaces. That's the maximum number of
	 * tokens that could be present.
	 */
	for (tokens = 0, cmd_s = config_attr; cmd_s[0] != '\0';) {
		tokens++;
		/* find whitespace */
		while (cmd_s[0] != '\0' && !isspace(cmd_s[0]))
			cmd_s++;
		/* Now skip whitespace to next token */
		while (cmd_s[0] != '\0' && isspace(cmd_s[0]))
			cmd_s++;
	}
	reqc->errcode = ENOMEM;
	av_list = av_new(tokens);
	kw_list = av_new(tokens);
	if (!av_list || !kw_list) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Out of memory");
		goto err;
	}

	reqc->errcode = tokenize(config_attr, kw_list, av_list);
	if (reqc->errcode) {
		ldmsd_log(LDMSD_LERROR, "Memory allocation failure "
				"processing '%s'\n", config_attr);
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Out of memory");
		reqc->errcode = ENOMEM;
		goto err;
	}

	reqc->errcode = ldmsd_config_plugin(plugin_name, av_list, kw_list);
	if (reqc->errcode) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Plugin '%s' configuration error.",
				plugin_name);
	}
	goto send_reply;

einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The attribute '%s' is required by config.",
		       	attr_name);
	goto send_reply;
err:
	if (kw_list)
		av_free(kw_list);
	if (av_list)
		av_free(av_list);
	kw_list = NULL;
	av_list = NULL;
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (plugin_name)
		free(plugin_name);
	if (config_attr)
		free(config_attr);
	if (kw_list)
		av_free(kw_list);
	if (av_list)
		av_free(av_list);
	return 0;
}

extern struct plugin_list plugin_list;
int __plugn_list_string(ldmsd_req_ctxt_t reqc)
{
	char *name = NULL;
	int rc, count = 0;
	struct ldmsd_plugin_cfg *p;
	rc = 0;

	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);

	LIST_FOREACH(p, &plugin_list, entry) {
		if (name && (0 != strcmp(name, p->name)))
			continue;

		if (p->plugin->usage) {
			rc = linebuf_printf(reqc, "%s\n%s",
					p->name, p->plugin->usage(p->plugin));
		} else {
			rc = linebuf_printf(reqc, "%s\n", p->name);
		}
		if (rc)
			goto out;
		count++;
	}
	if (name && (0 == count)) {
		reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
				"Plugin '%s' not loaded.", name);
		reqc->errcode = ENOENT;
	}
out:
	if (name)
		free(name);
	return rc;
}

static int plugn_list_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	struct ldmsd_req_attr_s attr;

	rc = __plugn_list_string(reqc);
	if (rc)
		return rc;

	attr.discrim = 1;
	attr.attr_len = reqc->line_off;
	attr.attr_id = LDMSD_ATTR_STRING;
	ldmsd_hton_req_attr(&attr);
	rc = ldmsd_append_reply(reqc, (char *)&attr, sizeof(attr), LDMSD_REQ_SOM_F);
	if (rc)
		return rc;
	rc = ldmsd_append_reply(reqc, reqc->line_buf, reqc->line_off, 0);
	if (rc)
		return rc;
	attr.discrim = 0;
	rc = ldmsd_append_reply(reqc, (char *)&attr.discrim, sizeof(uint32_t), LDMSD_REQ_EOM_F);
	return rc;
}

/* Caller must hold the set tree lock. */
int __plugn_sets_json_obj(ldmsd_req_ctxt_t reqc,
				ldmsd_plugin_set_list_t list)
{
	ldmsd_plugin_set_t set;
	int rc, set_count;
	set = LIST_FIRST(&list->list);
	if (!set)
		return 0;
	rc = linebuf_printf(reqc,
			"{"
			"\"plugin\":\"%s\","
			"\"sets\":[",
			set->plugin_name);
	if (rc)
		return rc;
	set_count = 0;
	LIST_FOREACH(set, &list->list, entry) {
		if (set_count) {
			rc = linebuf_printf(reqc, ",");
			if (rc)
				return rc;
		}
		rc = linebuf_printf(reqc, "\"%s\"", set->inst_name);
		if (rc)
			return rc;
		set_count++;
		if (rc)
			return rc;
	}
	rc = linebuf_printf(reqc, "]}");
	return rc;
}

static int plugn_sets_handler(ldmsd_req_ctxt_t reqc)
{
	int rc = 0;
	size_t cnt = 0;
	struct ldmsd_req_attr_s attr;
	ldmsd_plugin_set_list_t list;
	char *plugin;
	int plugn_count;

	plugin = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	ldmsd_set_tree_lock();
	if (plugin) {
		list = ldmsd_plugin_set_list_find(plugin);
		if (!list) {
			cnt = snprintf(reqc->line_buf, reqc->line_len,
					"No sets registered for the plugin '%s' "
					"or the plugin isn't loaded",
					plugin);
			reqc->errcode = ENOENT;
			ldmsd_set_tree_unlock();
			goto err0;
		}
		rc = __plugn_sets_json_obj(reqc, list);
		if (rc) {
			ldmsd_set_tree_unlock();
			goto err;
		}
	} else {
		plugn_count = 0;
		for (list = ldmsd_plugin_set_list_first(); list;
				list = ldmsd_plugin_set_list_next(list)) {
			if (plugn_count) {
				rc = linebuf_printf(reqc, ",");
				if (rc)
					goto err;
			}
			rc = __plugn_sets_json_obj(reqc, list);
			if (rc) {
				ldmsd_set_tree_unlock();
				goto err;
			}
			plugn_count += 1;
		}
	}
	ldmsd_set_tree_unlock();
	cnt = reqc->line_off + 2; /* +2 for '[' and ']'*/

	attr.discrim = 1;
	attr.attr_len = cnt;
	attr.attr_id = LDMSD_ATTR_JSON;
	ldmsd_hton_req_attr(&attr);
	rc = ldmsd_append_reply(reqc, (char *)&attr, sizeof(attr), LDMSD_REQ_SOM_F);
	if (rc)
		goto out;

	rc = ldmsd_append_reply(reqc, "[", 1, 0);
	if (rc)
		goto out;
	if (reqc->line_off) {
		rc = ldmsd_append_reply(reqc, reqc->line_buf, reqc->line_off, 0);
		if (rc)
			goto out;
	}
	rc = ldmsd_append_reply(reqc, "]", 1, 0);
	if (rc)
		goto out;
	attr.discrim = 0;
	rc = ldmsd_append_reply(reqc, (char *)&attr.discrim,
				sizeof(uint32_t), LDMSD_REQ_EOM_F);
out:
	if (plugin)
		free(plugin);
	return rc;

err:
	ldmsd_send_error_reply(reqc->xprt, reqc->rec_no, rc,
						"internal error", 15);
	goto out;
err0:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	goto out;
}

extern int ldmsd_set_udata(const char *set_name, const char *metric_name,
			   const char *udata_s, ldmsd_sec_ctxt_t sctxt);
static int set_udata_handler(ldmsd_req_ctxt_t reqc)
{
	char *set_name, *metric_name, *udata, *attr_name;
	set_name = metric_name = udata = NULL;
	size_t cnt = 0;
	struct ldmsd_sec_ctxt sctxt;

	reqc->errcode = 0;

	attr_name = "instance";
	set_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_INSTANCE);
	if (!set_name)
		goto einval;
	attr_name = "metric";
	metric_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_METRIC);
	if (!metric_name)
		goto einval;
	attr_name = "udata";
	udata = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_UDATA);
	if (!udata)
		goto einval;

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);

	reqc->errcode = ldmsd_set_udata(set_name, metric_name, udata, &sctxt);
	switch (reqc->errcode) {
	case 0:
		break;
	case EACCES:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "Permission denied.");
		break;
	case ENOENT:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Set '%s' not found.", set_name);
		break;
	case -ENOENT:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Metric '%s' not found in Set '%s'.",
				metric_name, set_name);
		break;
	case EINVAL:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"User data '%s' is invalid.", udata);
		break;
	default:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "Error %d %s", reqc->errcode,
			       ovis_errno_abbvr(reqc->errcode));
	}
	goto out;

einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The attribute '%s' is required.", attr_name);
out:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (set_name)
		free(set_name);
	if (metric_name)
		free(metric_name);
	if (udata)
		free(udata);
	return 0;
}

extern int ldmsd_set_udata_regex(char *set_name, char *regex_str,
		char *base_s, char *inc_s, char *er_str, size_t errsz,
		ldmsd_sec_ctxt_t sctxt);
static int set_udata_regex_handler(ldmsd_req_ctxt_t reqc)
{
	char *set_name, *regex, *base_s, *inc_s, *attr_name;
	set_name = regex = base_s = inc_s = NULL;
	size_t cnt = 0;
	struct ldmsd_sec_ctxt sctxt;

	reqc->errcode = 0;

	attr_name = "instance";
	set_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_INSTANCE);
	if (!set_name)
		goto einval;
	attr_name = "regex";
	regex = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_REGEX);
	if (!regex)
		goto einval;
	attr_name = "base";
	base_s = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_BASE);
	if (!base_s)
		goto einval;

	inc_s = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_INCREMENT);

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);
	reqc->errcode = ldmsd_set_udata_regex(set_name, regex, base_s, inc_s,
				reqc->line_buf, reqc->line_len, &sctxt);
	goto out;
einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The attribute '%s' is required.", attr_name);
out:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (set_name)
		free(set_name);
	if (base_s)
		free(base_s);
	if (regex)
		free(regex);
	if (inc_s)
		free(inc_s);
	return 0;
}

static int verbosity_change_handler(ldmsd_req_ctxt_t reqc)
{
	char *level_s = NULL;
	size_t cnt = 0;
	int is_test = 0;

	level_s = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_LEVEL);
	if (!level_s) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The attribute 'level' is required.");
		goto out;
	}

	int rc = ldmsd_loglevel_set(level_s);
	if (rc < 0) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Invalid verbosity level, expecting DEBUG, "
				"INFO, ERROR, CRITICAL and QUIET\n");
		goto out;
	}

	if (ldmsd_req_attr_keyword_exist_by_id(reqc->req_buf, LDMSD_ATTR_TEST))
		is_test = 1;

	if (is_test) {
		ldmsd_log(LDMSD_LDEBUG, "TEST DEBUG\n");
		ldmsd_log(LDMSD_LINFO, "TEST INFO\n");
		ldmsd_log(LDMSD_LWARNING, "TEST WARNING\n");
		ldmsd_log(LDMSD_LERROR, "TEST ERROR\n");
		ldmsd_log(LDMSD_LCRITICAL, "TEST CRITICAL\n");
		ldmsd_log(LDMSD_LALL, "TEST ALWAYS\n");
	}

out:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (level_s)
		free(level_s);
	return 0;
}

int __daemon_status_json_obj(ldmsd_req_ctxt_t reqc)
{
	int rc = 0;

	extern int ev_thread_count;
	extern pthread_t *ev_thread;
	extern int *ev_count;
	int i;

	rc = linebuf_printf(reqc, "[");
	if (rc)
		return rc;
	for (i = 0; i < ev_thread_count; i++) {
		if (i) {
			rc = linebuf_printf(reqc, ",\n");
			if (rc)
				return rc;
		}

		rc = linebuf_printf(reqc,
				"{ \"thread\":\"%p\","
				"\"task_count\":\"%d\"}",
				(void *)ev_thread[i], ev_count[i]);
		if (rc)
			return rc;
	}
	rc = linebuf_printf(reqc, "]");
	return rc;
}

static int daemon_status_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	struct ldmsd_req_attr_s attr;

	rc = __daemon_status_json_obj(reqc);
	if (rc)
		return rc;

	attr.discrim = 1;
	attr.attr_len = reqc->line_off;
	attr.attr_id = LDMSD_ATTR_JSON;
	ldmsd_hton_req_attr(&attr);
	rc = ldmsd_append_reply(reqc, (char *)&attr, sizeof(attr), LDMSD_REQ_SOM_F);
	if (rc)
		return rc;
	rc = ldmsd_append_reply(reqc, reqc->line_buf, reqc->line_off, 0);
	if (rc)
		return rc;
	attr.discrim = 0;
	ldmsd_append_reply(reqc, (char *)&attr.discrim, sizeof(uint32_t), LDMSD_REQ_EOM_F);
	return rc;
}

static int version_handler(ldmsd_req_ctxt_t reqc)
{
	struct ldms_version ldms_version;
	struct ldmsd_version ldmsd_version;

	ldms_version_get(&ldms_version);
	size_t cnt = snprintf(reqc->line_buf, reqc->line_len,
			"LDMS Version: %hhu.%hhu.%hhu.%hhu\n",
			ldms_version.major, ldms_version.minor,
			ldms_version.patch, ldms_version.flags);

	ldmsd_version_get(&ldmsd_version);
	cnt += snprintf(&reqc->line_buf[cnt], reqc->line_len-cnt,
			"LDMSD Version: %hhu.%hhu.%hhu.%hhu",
			ldmsd_version.major, ldmsd_version.minor,
			ldmsd_version.patch, ldmsd_version.flags);
	ldmsd_send_req_response(reqc, reqc->line_buf);
	return 0;


}

static int env_handler(ldmsd_req_ctxt_t reqc)
{
	int rc = 0;
	size_t cnt = 0;
	char *env_s = NULL;
	struct attr_value_list *av_list = NULL;
	struct attr_value_list *kw_list = NULL;
	char *exp_val = NULL;

	ldmsd_req_attr_t attr = ldmsd_first_attr((ldmsd_req_hdr_t)reqc->req_buf);
	while (attr->discrim) {
		switch (attr->attr_id) {
		case LDMSD_ATTR_STRING:
			env_s = (char *)attr->attr_value;
			break;
		default:
			break;
		}
		attr = ldmsd_next_attr(attr);
	}
	if (!env_s) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"No environment names/values are given.");
		reqc->errcode = EINVAL;
		goto out;
	}

	if (reqc->xprt->trust) {
		exp_val = str_repl_cmd(env_s);
		if (!exp_val) {
			rc = errno;
			goto out;
		}
		env_s = exp_val;
	}

	rc = string2attr_list(env_s, &av_list, &kw_list);
	if (rc) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Out of memory.");
		reqc->errcode = ENOMEM;
		goto out;
	}

	int i;
	for (i = 0; i < av_list->count; i++) {
		struct attr_value *v = &av_list->list[i];
		rc = setenv(v->name, v->value, 1);
		if (rc) {
			rc = errno;
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
					"Failed to set '%s=%s': %s",
					v->name, v->value, strerror(rc));
			goto out;
		}
	}
out:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (kw_list)
		av_free(kw_list);
	if (av_list)
		av_free(av_list);
	if (exp_val)
		free(exp_val);
	return rc;
}

static int include_handler(ldmsd_req_ctxt_t reqc)
{
	char *path = NULL;
	int rc = 0;
	size_t cnt = 0;

	path = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_PATH);
	if (!path) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The attribute 'path' is required by include.");
		goto out;
	}
	int lineno = -1;
	reqc->errcode = process_config_file(path, &lineno, reqc->xprt->trust);
	if (reqc->errcode) {
		if (lineno == 0) {
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Failed to process cfg '%s' at line %d: %s",
				path, lineno, strerror(reqc->errcode));
		} else {
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Failed to process cfg '%s' at line '%d'",
				path, lineno);
		}
	}

out:
	if (path)
		free(path);
	ldmsd_send_req_response(reqc, reqc->line_buf);
	return rc;
}

extern int ldmsd_oneshot_sample(const char *name, const char *time_s,
					char *errstr, size_t errlen);
static int oneshot_handler(ldmsd_req_ctxt_t reqc)
{
	char *name, *time_s, *attr_name;
	name = time_s = NULL;
	size_t cnt = 0;
	int rc = 0;

	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!name) {
		attr_name = "name";
		goto einval;
	}
	time_s = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_TIME);
	if (!time_s) {
		attr_name = "time";
		goto einval;
	}

	reqc->errcode = ldmsd_oneshot_sample(name, time_s,
				reqc->line_buf, reqc->line_len);
	if (reqc->errcode) {
		cnt = strlen(reqc->line_buf) + 1;
		goto out;
	}
	ldmsd_send_req_response(reqc, NULL);
	if (name)
		free(name);
	if (time_s)
		free(time_s);
	return rc;

einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The attribute '%s' is required by oneshot.",
		       	attr_name);

out:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (name)
		free(name);
	if (time_s)
		free(time_s);
	return rc;
}

extern int ldmsd_logrotate();
static int logrotate_handler(ldmsd_req_ctxt_t reqc)
{
	size_t cnt = 0;
	reqc->errcode = ldmsd_logrotate();
	if (reqc->errcode) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Failed to rotate the log file. %s",
				strerror(reqc->errcode));
	}
	ldmsd_send_req_response(reqc, reqc->line_buf);
	return 0;
}

static int exit_daemon_handler(ldmsd_req_ctxt_t reqc)
{
	cleanup_requested = 1;
	ldmsd_log(LDMSD_LINFO, "User requested exit.\n");
	Snprintf(&reqc->line_buf, &reqc->line_len,
				"exit daemon request received");
	ldmsd_send_req_response(reqc, reqc->line_buf);
	return 0;
}

static int __greeting_path_resp_handler(ldmsd_req_cmd_t rcmd)
{
	struct ldmsd_req_attr_s my_attr;
	ldmsd_req_attr_t server_attr;
	char *path;
	server_attr = ldmsd_first_attr((ldmsd_req_hdr_t)rcmd->reqc->req_buf);
	my_attr.discrim = 1;
	my_attr.attr_id = LDMSD_ATTR_STRING;
	/* +1 for : */
	my_attr.attr_len = server_attr->attr_len + strlen((char *)rcmd->ctxt) + 2;
	path = malloc(my_attr.attr_len);
	if (!path) {
		rcmd->org_reqc->errcode = ENOMEM;
		ldmsd_send_req_response(rcmd->org_reqc, "Out of memory");
		return 0;
	}
	ldmsd_hton_req_attr(&my_attr);
	ldmsd_append_reply(rcmd->org_reqc, (char *)&my_attr, sizeof(my_attr), LDMSD_REQ_SOM_F);
	memcpy(path, server_attr->attr_value, server_attr->attr_len);
	path[server_attr->attr_len] = ':';
	strcpy(&path[server_attr->attr_len + 1], rcmd->ctxt);
	ldmsd_append_reply(rcmd->org_reqc, path, ntohl(my_attr.attr_len), 0);
	my_attr.discrim = 0;
	ldmsd_append_reply(rcmd->org_reqc, (char *)&my_attr.discrim,
				sizeof(my_attr.discrim), LDMSD_REQ_EOM_F);
	free(path);
	free(rcmd->ctxt);
	return 0;
}

static int __greeting_path_req_handler(ldmsd_req_ctxt_t reqc)
{
	ldmsd_prdcr_t prdcr;
	ldmsd_req_cmd_t rcmd;
	struct ldmsd_req_attr_s attr;
	ldmsd_cfg_lock(LDMSD_CFGOBJ_PRDCR);
	prdcr = ldmsd_prdcr_first();
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR);;
	char *myself = strdup(ldmsd_myhostname_get());
	if (!myself) {
		ldmsd_log(LDMSD_LERROR, "Out of memory\n");
		return ENOMEM;
	}
	if (!prdcr) {
		attr.discrim = 1;
		attr.attr_id = LDMSD_ATTR_STRING;
		attr.attr_len = strlen(myself);
		ldmsd_hton_req_attr(&attr);
		ldmsd_append_reply(reqc, (char *)&attr, sizeof(attr), LDMSD_REQ_SOM_F);
		ldmsd_append_reply(reqc, myself, strlen(myself), 0);
		free(myself);
		attr.discrim = 0;
		ldmsd_append_reply(reqc, (char *)&attr.discrim, sizeof(attr.discrim), LDMSD_REQ_EOM_F);
	} else {
		ldmsd_prdcr_lock(prdcr);
		rcmd = alloc_req_cmd_ctxt(prdcr->xprt, ldms_xprt_msg_max(prdcr->xprt),
						LDMSD_GREETING_REQ, reqc,
						__greeting_path_resp_handler, myself);
		ldmsd_prdcr_unlock(prdcr);
		if (!rcmd) {
			reqc->errcode = ENOMEM;
			ldmsd_send_req_response(reqc, "Out of Memory");
			free(myself);
			return 0;
		}
		attr.attr_id = LDMSD_ATTR_PATH;
		attr.attr_len = 0;
		attr.discrim = 1;
		ldmsd_hton_req_attr(&attr);
		__ldmsd_append_buffer(rcmd->reqc, (char *)&attr, sizeof(attr),
					LDMSD_REQ_SOM_F, LDMSD_REQ_TYPE_CONFIG_CMD);
		attr.discrim = 0;
		__ldmsd_append_buffer(rcmd->reqc, (char *)&attr.discrim,
					sizeof(attr.discrim), LDMSD_REQ_EOM_F,
						LDMSD_REQ_TYPE_CONFIG_CMD);
	}
	return 0;
}

static int greeting_handler(ldmsd_req_ctxt_t reqc)
{
	char *str = 0;
	char *rep_len_str = 0;
	char *num_rec_str = 0;
	int rep_len = 0;
	int num_rec = 0;
	size_t cnt = 0;
	int i;

	rep_len_str = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_OFFSET);
	num_rec_str = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_LEVEL);
	str = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (str) {
		cnt = snprintf(reqc->line_buf, reqc->line_len, "Hello '%s'", str);
		ldmsd_log(LDMSD_LDEBUG, "strlen(name)=%zu. %s\n", strlen(str), str);
		ldmsd_send_req_response(reqc, reqc->line_buf);
	} else if (ldmsd_req_attr_keyword_exist_by_name(reqc->req_buf, "test")) {
		cnt = snprintf(reqc->line_buf, reqc->line_len, "Hi");
		ldmsd_send_req_response(reqc, reqc->line_buf);
	} else if (rep_len_str) {
		rep_len = atoi(rep_len_str);
		char *buf = malloc(rep_len + 1);
		if (!buf) {
			cnt = snprintf(reqc->line_buf, reqc->line_len,
					"ldmsd out of memory");
			buf = reqc->line_buf;
			reqc->errcode = ENOMEM;
		} else {
			cnt = snprintf(buf, rep_len + 1, "%0*d", rep_len, rep_len);
		}
		ldmsd_send_req_response(reqc, buf);
		free(buf);
	} else if (num_rec_str) {
		num_rec = atoi(num_rec_str);
		if (num_rec <= 1) {
			if (num_rec < 1) {
				cnt = snprintf(reqc->line_buf, reqc->line_len,
						"Invalid. level >= 1");
				reqc->errcode = EINVAL;
			} else {
				cnt = snprintf(reqc->line_buf, reqc->line_len,
						"single record 0");
			}
			ldmsd_send_req_response(reqc, reqc->line_buf);
			goto out;
		}

		struct ldmsd_req_attr_s attr;
		size_t remaining;
		attr.attr_id = LDMSD_ATTR_STRING;
		attr.discrim = 1;
		attr.attr_len = reqc->rep_len - 2*sizeof(struct ldmsd_req_hdr_s)
						- sizeof(struct ldmsd_req_attr_s);
		ldmsd_hton_req_attr(&attr);
		int msg_flag = LDMSD_REQ_SOM_F;

		/* Construct the message */
		for (i = 0; i < num_rec; i++) {
			remaining = reqc->rep_len - 2* sizeof(struct ldmsd_req_hdr_s);
			ldmsd_append_reply(reqc, (char *)&attr, sizeof(attr), msg_flag);
			remaining -= sizeof(struct ldmsd_req_attr_s);
			cnt = snprintf(reqc->line_buf, reqc->line_len, "%d", i);
			ldmsd_append_reply(reqc, reqc->line_buf, cnt, 0);
			remaining -= cnt;
			while (reqc->line_len < remaining) {
				cnt = snprintf(reqc->line_buf, reqc->line_len, "%*s",
							(int)reqc->line_len, "");
				ldmsd_append_reply(reqc, reqc->line_buf, cnt, 0);
				remaining -= cnt;
			}
			if (remaining) {
				cnt = snprintf(reqc->line_buf, reqc->line_len,
						"%*s", (int)remaining, " ");
				ldmsd_append_reply(reqc, reqc->line_buf, cnt, 0);
			}
			msg_flag = 0;
		}
		attr.discrim = 0;
		ldmsd_append_reply(reqc, (char *)&attr.discrim, sizeof(uint32_t),
								LDMSD_REQ_EOM_F);
	} else if (ldmsd_req_attr_keyword_exist_by_id(reqc->req_buf, LDMSD_ATTR_PATH)) {
		(void) __greeting_path_req_handler(reqc);
	} else {
		ldmsd_send_req_response(reqc, NULL);
	}
out:
	if (rep_len_str)
		free(rep_len_str);
	if (num_rec_str)
		free(num_rec_str);
	return 0;
}

static int unimplemented_handler(ldmsd_req_ctxt_t reqc)
{
	size_t cnt;
	reqc->errcode = ENOSYS;

	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The request is not implemented");
	ldmsd_send_req_response(reqc, reqc->line_buf);
	return 0;
}

static int eperm_handler(ldmsd_req_ctxt_t reqc)
{
	reqc->errcode = EPERM;
	Snprintf(&reqc->line_buf, &reqc->line_len,
			"Operation not permitted.");
	ldmsd_send_req_response(reqc, reqc->line_buf);
	return 0;
}

static int ebusy_handler(ldmsd_req_ctxt_t reqc)
{
	reqc->errcode = EBUSY;
	Snprintf(&reqc->line_buf, &reqc->line_len,
			"Daemon busy.");
	ldmsd_send_req_response(reqc, reqc->line_buf);
	return 0;
}

int ldmsd_set_route_request(ldmsd_prdcr_t prdcr,
			ldmsd_req_ctxt_t org_reqc, char *inst_name,
			ldmsd_req_resp_fn resp_handler, void *ctxt)
{
	size_t inst_name_len;
	ldmsd_req_cmd_t rcmd;
	struct ldmsd_req_attr_s attr;
	int rc;

	rcmd = alloc_req_cmd_ctxt(prdcr->xprt, ldms_xprt_msg_max(prdcr->xprt),
					LDMSD_SET_ROUTE_REQ, org_reqc,
					resp_handler, ctxt);
	if (!rcmd)
		return ENOMEM;

	inst_name_len = strlen(inst_name) + 1;
	/* instance name attribute */
	attr.attr_id = LDMSD_ATTR_INSTANCE;
	attr.attr_len = inst_name_len;
	attr.discrim = 1;
	ldmsd_hton_req_attr(&attr);
	rc = __ldmsd_append_buffer(rcmd->reqc, (char *)&attr, sizeof(attr),
					LDMSD_REQ_SOM_F, LDMSD_REQ_TYPE_CONFIG_CMD);
	if (rc)
		goto out;
	rc = __ldmsd_append_buffer(rcmd->reqc, inst_name, inst_name_len,
						0, LDMSD_REQ_TYPE_CONFIG_CMD);
	if (rc)
		goto out;

	/* Keyword type to specify that this is an internal request */
	attr.attr_id = LDMSD_ATTR_TYPE;
	attr.attr_len = 0;
	attr.discrim = 1;
	ldmsd_hton_req_attr(&attr);
	rc = __ldmsd_append_buffer(rcmd->reqc, (char *)&attr, sizeof(attr),
						0, LDMSD_REQ_TYPE_CONFIG_CMD);
	if (rc)
		goto out;

	/* Terminating discrim */
	attr.discrim = 0;
	rc = __ldmsd_append_buffer(rcmd->reqc, (char *)&attr.discrim, sizeof(uint32_t),
					LDMSD_REQ_EOM_F, LDMSD_REQ_TYPE_CONFIG_CMD);
out:
	if (rc) {
		/* rc is not zero only if sending fails (a transport error) so
		 * no need to keep the request command context around */
		free_req_cmd_ctxt(rcmd);
	}

	return rc;
}

size_t __set_route_json_get(int is_internal, ldmsd_req_ctxt_t reqc,
						ldmsd_set_info_t info)
{
	size_t cnt = 0;
	if (!is_internal) {
		cnt = snprintf(reqc->line_buf, reqc->line_len,
					"{"
					"\"instance\":\"%s\","
					"\"schema\":\"%s\","
					"\"route\":"
					"[",
					ldms_set_instance_name_get(info->set),
					ldms_set_schema_name_get(info->set));
	}
	if (info->origin_type == LDMSD_SET_ORIGIN_SAMP_PI) {
		if (!is_internal) {
			cnt = snprintf(reqc->line_buf, reqc->line_len,
						"{"
						"\"instance\":\"%s\","
						"\"schema\":\"%s\","
						"\"route\":"
						"[",
						ldms_set_instance_name_get(info->set),
						info->prd_set->schema_name);
		}
		cnt += snprintf(&reqc->line_buf[cnt], reqc->line_len - cnt,
				"{"
				"\"host\":\"%s\","
				"\"type\":\"%s\","
				"\"detail\":"
					"{"
					"\"name\":\"%s\","
					"\"interval_us\":\"%lu\","
					"\"offset_us\":\"%ld\","
					"\"sync\":\"%s\","
					"\"trans_start_sec\":\"%ld\","
					"\"trans_start_usec\":\"%ld\","
					"\"trans_end_sec\":\"%ld\","
					"\"trans_end_usec\":\"%ld\""
					"}"
				"}",
				ldmsd_myhostname_get(),
				ldmsd_set_info_origin_enum2str(info->origin_type),
				info->origin_name,
				info->interval_us,
				info->offset_us,
				((info->sync)?"true":"false"),
				info->start.tv_sec,
				info->start.tv_usec,
				info->end.tv_sec,
				info->end.tv_usec);
		if (!is_internal) {
			cnt += snprintf(&reqc->line_buf[cnt], reqc->line_len - cnt, "]}");
		}
	} else {
		cnt += snprintf(&reqc->line_buf[cnt], reqc->line_len - cnt,
				"{"
				"\"host\":\"%s\","
				"\"type\":\"%s\","
				"\"detail\":"
					"{"
					"\"name\":\"%s\","
					"\"host\":\"%s\","
					"\"update_int\":\"%ld\","
					"\"update_off\":\"%ld\","
					"\"update_sync\":\"%s\","
					"\"last_start_sec\":\"%ld\","
					"\"last_start_usec\":\"%ld\","
					"\"last_end_sec\":\"%ld\","
					"\"last_end_usec\":\"%ld\""
					"}"
				"}",
				ldmsd_myhostname_get(),
				ldmsd_set_info_origin_enum2str(info->origin_type),
				info->origin_name,
				info->prd_set->prdcr->host_name,
				info->interval_us,
				info->offset_us,
				((info->sync)?"true":"false"),
				info->start.tv_sec,
				info->start.tv_usec,
				info->end.tv_sec,
				info->end.tv_usec);
	}

	return cnt;
}

struct set_route_req_ctxt {
	char *my_info;
	int is_internal;
};

static int set_route_resp_handler(ldmsd_req_cmd_t rcmd)
{
	struct ldmsd_req_attr_s my_attr;
	ldmsd_req_attr_t attr;
	ldmsd_req_ctxt_t reqc = rcmd->reqc;
	ldmsd_req_ctxt_t org_reqc = rcmd->org_reqc;
	struct set_route_req_ctxt *ctxt = (struct set_route_req_ctxt *)rcmd->ctxt;

	attr = ldmsd_first_attr((ldmsd_req_hdr_t)reqc->req_buf);

	my_attr.attr_id = LDMSD_ATTR_JSON;
	/* +1 for a command between two json objects */
	my_attr.attr_len = strlen(ctxt->my_info) + attr->attr_len + 1;
	if (!ctxt->is_internal) {
		/* +2 for a square bracket and a curly bracket */
		my_attr.attr_len += 2;
	}
	my_attr.discrim = 1;
	ldmsd_hton_req_attr(&my_attr);
	(void) ldmsd_append_reply(org_reqc, (char *)&my_attr, sizeof(my_attr), 0);
	(void) ldmsd_append_reply(org_reqc, ctxt->my_info, strlen(ctxt->my_info), 0);
	(void) ldmsd_append_reply(org_reqc, ",", 1, 0);
	if (!ctxt->is_internal) {
		/* -1 to exclude the terminating character */
		(void) ldmsd_append_reply(org_reqc, (char *)attr->attr_value, attr->attr_len - 1, 0);
		(void) ldmsd_append_reply(org_reqc, "]}", 3, 0);
	} else {
		(void) ldmsd_append_reply(org_reqc, (char *)attr->attr_value, attr->attr_len, 0);
	}

	my_attr.discrim = 0;
	(void) ldmsd_append_reply(org_reqc, (char *)&my_attr.discrim,
					sizeof(uint32_t), LDMSD_REQ_EOM_F);
	free(ctxt->my_info);
	free(ctxt);
	return 0;
}

static int set_route_handler(ldmsd_req_ctxt_t reqc)
{
	size_t cnt;
	char *inst_name;
	struct set_route_req_ctxt *ctxt;
	int is_internal = 0;
	int rc = 0;
	ldmsd_set_info_t info;
	struct ldmsd_req_attr_s attr;

	inst_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_INSTANCE);
	if (!inst_name) {
		cnt = snprintf(reqc->line_buf, reqc->line_len,
				"The attribute 'instance' is required.");
		reqc->errcode = EINVAL;
		(void) ldmsd_send_req_response(reqc, reqc->line_buf);
		goto out;
	}

	is_internal = ldmsd_req_attr_keyword_exist_by_id(reqc->req_buf, LDMSD_ATTR_TYPE);

	info = ldmsd_set_info_get(inst_name);
	if (!info) {
		/* The set does not exist. */
		cnt = snprintf(reqc->line_buf, reqc->line_len,
				"%s: Set '%s' not exist.",
				ldmsd_myhostname_get(), inst_name);
		(void) ldmsd_send_error_reply(reqc->xprt, reqc->key.msg_no, ENOENT,
				reqc->line_buf, cnt + 1);
		goto out;
	}

	cnt = __set_route_json_get(is_internal, reqc, info);
	if (info->origin_type == LDMSD_SET_ORIGIN_PRDCR) {
		ctxt = malloc(sizeof(*ctxt));
		if (!ctxt) {
			reqc->errcode = ENOMEM;
			cnt = snprintf(reqc->line_buf, reqc->line_len,
						"ldmsd: Out of memory");
			ldmsd_send_req_response(reqc, reqc->line_buf);
			goto err0;
		}
		ctxt->is_internal = is_internal;
		ctxt->my_info = malloc(cnt + 1);
		if (!ctxt->my_info) {
			reqc->errcode = ENOMEM;
			cnt = snprintf(reqc->line_buf, reqc->line_len,
						"ldmsd: Out of memory");
			ldmsd_send_req_response(reqc, reqc->line_buf);
			goto err1;
		}
		memcpy(ctxt->my_info, reqc->line_buf, cnt + 1);
		rc = ldmsd_set_route_request(info->prd_set->prdcr,
				reqc, inst_name, set_route_resp_handler, ctxt);
		if (rc) {
			reqc->errcode = rc;
			cnt = snprintf(reqc->line_buf, reqc->line_len,
					"%s: error forwarding set_route_request to "
					"prdcr '%s'", ldmsd_myhostname_get(),
					info->origin_name);
			ldmsd_send_req_response(reqc, reqc->line_buf);
			goto err2;
		}
	} else {
		attr.attr_id = LDMSD_ATTR_JSON;
		attr.discrim = 1;
		attr.attr_len = cnt + 1;
		ldmsd_hton_req_attr(&attr);
		(void) __ldmsd_append_buffer(reqc, (char *)&attr, sizeof(attr),
				LDMSD_REQ_SOM_F, LDMSD_REQ_TYPE_CONFIG_RESP);
		(void) __ldmsd_append_buffer(reqc, reqc->line_buf, cnt + 1,
				0, LDMSD_REQ_TYPE_CONFIG_RESP);
		attr.discrim = 0;
		(void) __ldmsd_append_buffer(reqc, (char *)&attr.discrim,
				sizeof(uint32_t),
				LDMSD_REQ_EOM_F, LDMSD_REQ_TYPE_CONFIG_RESP);
	}
	return 0;
err2:
	free(ctxt->my_info);
err1:
	free(ctxt);
err0:
	ldmsd_set_info_delete(info);
out:
	return rc;
}

static int stream_publish_handler(ldmsd_req_ctxt_t reqc)
{
	char *stream_name;
	ldmsd_stream_type_t stream_type = LDMSD_STREAM_STRING;
	ldmsd_req_attr_t attr;
	json_parser_t parser;
	json_entity_t entity = NULL;
	int cnt;

	stream_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!stream_name) {
		reqc->errcode = EINVAL;
		ldmsd_log(LDMSD_LERROR, "%s: The stream name is missing "
			  "in the config message\n", __func__);
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "The stream name is missing.");
		goto err_reply;
	}

	/* Check for string */
	attr = ldmsd_req_attr_get_by_id(reqc->req_buf, LDMSD_ATTR_STRING);
	if (attr)
		goto out;

	/* Check for JSon */
	attr = ldmsd_req_attr_get_by_id(reqc->req_buf, LDMSD_ATTR_JSON);
	if (attr) {
		parser = json_parser_new(0);
		if (!parser) {
			ldmsd_log(LDMSD_LERROR,
				  "%s: error creating JSon parser.\n", __func__);
			reqc->errcode = ENOMEM;
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				       "Could not create the JSon parser.");
			goto err_reply;
		}
		int rc = json_parse_buffer(parser,
					   (char *)attr->attr_value, attr->attr_len,
					   &entity);
		json_parser_free(parser);
		if (rc) {
			ldmsd_log(LDMSD_LERROR,
				  "%s: syntax error parsing JSon payload.\n", __func__);
			reqc->errcode = EINVAL;
			goto err_reply;
		}
		stream_type = LDMSD_STREAM_JSON;
	} else {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"No data provided.");
		reqc->errcode = EINVAL;
		goto err_reply;
	}
out:
	ldmsd_stream_deliver(stream_name, stream_type,
			     (char *)attr->attr_value, attr->attr_len, entity);
	free(stream_name);
	json_entity_free(entity);
	return 0;
err_reply:
	if (stream_name)
		free(stream_name);
	// ldmsd_send_req_response(reqc, reqc->line_buf);
	return 0;
}

static int __on_republish_resp(ldmsd_req_cmd_t rcmd)
{
	return 0;
}

static int stream_republish_cb(ldmsd_stream_client_t c, void *ctxt,
			       ldmsd_stream_type_t stream_type,
			       const char *data, size_t data_len,
			       json_entity_t entity)
{
	ldms_t ldms = ldms_xprt_get(ctxt);
	int rc, attr_id = LDMSD_ATTR_STRING;
	const char *stream = ldmsd_stream_client_name(c);
	ldmsd_req_cmd_t rcmd = ldmsd_req_cmd_new(ldms, LDMSD_STREAM_PUBLISH_REQ,
						 NULL, __on_republish_resp, NULL);
	rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_NAME, stream);
	if (rc)
		goto out;
	if (stream_type == LDMSD_STREAM_JSON)
		attr_id = LDMSD_ATTR_JSON;
	rc = ldmsd_req_cmd_attr_append_str(rcmd, attr_id, data);
	if (rc)
		goto out;
	rc = ldmsd_req_cmd_attr_term(rcmd);
 out:
	ldmsd_req_cmd_free(rcmd);
	return rc;
}

static int stream_subscribe_handler(ldmsd_req_ctxt_t reqc)

{
	char *stream_name;
	int cnt;

	stream_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!stream_name) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "The stream name is missing.");
		goto send_reply;
	}

	ldmsd_stream_subscribe(stream_name, stream_republish_cb, reqc->xprt->ldms.ldms);
	reqc->errcode = 0;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len, "OK");
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	return 0;
}

int ldmsd_auth_opt_add(struct attr_value_list *auth_attrs, char *name, char *val)
{
	struct attr_value *attr;
	attr = &(auth_attrs->list[auth_attrs->count]);
	if (auth_attrs->count == auth_attrs->size) {
		ldmsd_log(LDMSD_LERROR, "Too many auth options\n");
		return EINVAL;
	}
	attr->name = strdup(name);
	if (!attr->name)
		return ENOMEM;
	attr->value = strdup(val);
	if (!attr->value)
		return ENOMEM;
	auth_attrs->count++;
	return 0;
}

static int listen_handler(ldmsd_req_ctxt_t reqc)
{
	ldmsd_listen_t listen;
	char *xprt, *port, *host, *auth, *attr_name;
	unsigned short port_no = -1;
	xprt = port = host = auth = NULL;

	if (ldmsd_is_initialized()) {
		/*
		 * Adding a new listening endpoint is prohibited
		 * after LDMSD is initialized.
		 */
		reqc->errcode = EPERM;
		linebuf_printf(reqc, "LDMSD is started. "
				"Adding a listening endpoint is prohibited.");
		goto send_reply;
	}

	attr_name = "xprt";
	xprt = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_XPRT);
	if (!xprt)
		goto einval;
	port = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_PORT);
	if (port) {
		port_no = atoi(port);
		if (port_no < 1 || port_no > USHRT_MAX) {
			reqc->errcode = EINVAL;
			(void) snprintf(reqc->line_buf, reqc->line_len,
					"'%s' transport with invalid port '%s'",
					xprt, port);
			goto send_reply;
		}
	}
	host =ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_HOST);
	auth = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_AUTH);

	listen = ldmsd_listen_new(xprt, port, host, auth);
	if (!listen) {
		if (errno == EEXIST)
			goto eexist;
		else
			goto enomem;
	}
	goto send_reply;

eexist:
	reqc->errcode = EEXIST;
	(void) snprintf(reqc->line_buf, reqc->line_len,
			"The listening endpoint %s:%s is already exists",
			xprt, port);
	goto send_reply;
enomem:
	reqc->errcode = ENOMEM;
	(void) snprintf(reqc->line_buf, reqc->line_len, "Out of memory");
	goto send_reply;
einval:
	reqc->errcode = EINVAL;
	(void) snprintf(reqc->line_buf, reqc->line_len,
			"The attribute '%s' is required.", attr_name);
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (xprt)
		free(xprt);
	if (port)
		free(port);
	if (host)
		free(host);
	if (auth)
		free(auth);
	return 0;
}

static int auth_add_handler(ldmsd_req_ctxt_t reqc)
{
	int rc = 0;
	const char *attr_name;
	char *name = NULL, *plugin = NULL, *auth_args = NULL;
	char *str, *ptr1, *ptr2, *lval, *rval;
	struct attr_value_list *auth_opts = NULL;
	ldmsd_auth_t auth_dom;

	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!name) {
		attr_name = "name";
		goto attr_required;
	}

	plugin = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_PLUGIN);
	if (!plugin) {
		plugin = strdup(name);
		if (!plugin)
			goto enomem;
	}

	auth_args = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_STRING);
	if (auth_args) {
		auth_opts = av_new(LDMSD_AUTH_OPT_MAX);
		if (!auth_opts)
			goto enomem;
		str = strtok_r(auth_args, " ", &ptr1);
		while (str) {
			lval = strtok_r(str, "=", &ptr2);
			rval = strtok_r(NULL, "", &ptr2);
			rc = ldmsd_auth_opt_add(auth_opts, lval, rval);
			if (rc) {
				(void) snprintf(reqc->line_buf, reqc->line_len,
					"Failed to process the authentication options");
				goto send_reply;
			}
			str = strtok_r(NULL, " ", &ptr1);
		}
	}

	auth_dom = ldmsd_auth_new_with_auth(name, plugin, auth_opts,
					    geteuid(), getegid(), 0600);
	if (!auth_dom) {
		reqc->errcode = errno;
		(void) snprintf(reqc->line_buf, reqc->line_len,
				"Authentication domain creation failed, "
				"errno: %d", errno);
		goto send_reply;
	}

	goto send_reply;

enomem:
	reqc->errcode = ENOMEM;
	(void) snprintf(reqc->line_buf, reqc->line_len, "Out of memory");
	goto send_reply;
attr_required:
	reqc->errcode = EINVAL;
	(void) snprintf(reqc->line_buf, reqc->line_len,
			"Attribute '%s' is required", attr_name);
	goto send_reply;
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	/* cleanup */
	if (name)
		free(name);
	if (plugin)
		free(plugin);
	if (auth_args)
		free(auth_args);
	if (auth_opts)
		av_free(auth_opts);
	return 0;
}

static int auth_del_handler(ldmsd_req_ctxt_t reqc)
{
	const char *attr_name;
	char *name = NULL;
	struct ldmsd_sec_ctxt sctxt;

	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!name) {
		attr_name = "name";
		goto attr_required;
	}

	ldmsd_sec_ctxt_get(&sctxt);
	reqc->errcode = ldmsd_auth_del(name, &sctxt);
	switch (reqc->errcode) {
	case EACCES:
		snprintf(reqc->line_buf, reqc->line_len, "Permission denied");
		break;
	case ENOENT:
		snprintf(reqc->line_buf, reqc->line_len,
			 "'%s' authentication domain not found", name);
		break;
	default:
		snprintf(reqc->line_buf, reqc->line_len,
			 "Failed to delete authentication domain '%s', "
			 "error: %d", name, reqc->errcode);
		break;
	}

	goto send_reply;

attr_required:
	reqc->errcode = EINVAL;
	(void) snprintf(reqc->line_buf, reqc->line_len,
			"Attribute '%s' is required", attr_name);
	goto send_reply;
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	/* cleanup */
	if (name)
		free(name);
	return 0;
}
