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
#include <limits.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <coll/rbt.h>
#include <pthread.h>
#include <unistd.h>
#include <getopt.h>
#include <pwd.h>
#include <grp.h>
#include <ovis_util/util.h>
#include <ovis_json/ovis_json.h>
#include <arpa/inet.h>
#include "mmalloc.h"
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

int ldmsd_req_debug = 0; /* turn bits on / off using gdb or -L
			 * to see request/response and other messages */
FILE *ldmsd_req_debug_file = NULL; /* change with -L or
				    * ldmsd.c:process_log_config */

static int cleanup_requested = 0;

void __ldmsd_log(enum ldmsd_loglevel level, const char *fmt, va_list ap);

static char * __thread_stats_as_json(size_t *json_sz);
static char * __xprt_stats_as_json(size_t *json_sz);
extern const char *prdcr_state_str(enum ldmsd_prdcr_state state);

struct timeval ldmsd_req_last_time;
__attribute__((format(printf, 2, 3)))
void __dlog(int match, const char *fmt, ...)
{
	if (!ldmsd_req_debug || !(match & ldmsd_req_debug))
		return;
	va_list ap;
	va_start(ap, fmt);
	if (ldmsd_req_debug_file) {
		struct timeval tv;
		if (ldmsd_req_debug & (DLOG_EPOCH | DLOG_DELTA)) {
			gettimeofday(&tv, NULL);
			if (ldmsd_req_debug & DLOG_EPOCH) {
				fprintf(ldmsd_req_debug_file,"%lu.%06lu: ",
					tv.tv_sec, tv.tv_usec);
			}
			if (ldmsd_req_debug & DLOG_DELTA) {
				struct timeval *end = &tv;
				struct timeval *start = &ldmsd_req_last_time;
				double delta = (end->tv_sec - start->tv_sec)*1.0
					+ 1e-6*(end->tv_usec - start->tv_usec);

				fprintf(ldmsd_req_debug_file, "%.6f: ",
					delta);
			}
			ldmsd_req_last_time = tv;
		}
		vfprintf(ldmsd_req_debug_file, fmt, ap);
		fflush(ldmsd_req_debug_file);
	} else {
		__ldmsd_log(LDMSD_LALL, fmt, ap);
	}
	va_end(ap);
}


__attribute__((format(printf, 3, 4)))
size_t Snprintf(char **dst, size_t *len, char *fmt, ...);

#define REQ_CTXT_KEY_F_CFGFILE 1 /* requests from configuration files */
#define REQ_CTXT_KEY_F_REM_REQ 2 /* remote requests */
#define REQ_CTXT_KEY_F_LOC_REQ 3 /* requests awaiting for replies */

static int msg_comparator(void *a, const void *b)
{
	msg_key_t ak = (msg_key_t)a;
	msg_key_t bk = (msg_key_t)b;
	int rc;

	rc = ak->flags - bk->flags;
	if (rc)
		return rc;
	rc = ak->conn_id - bk->conn_id;
	if (rc)
		return rc;
	return ak->msg_no - bk->msg_no;
}
struct rbt msg_tree = RBT_INITIALIZER(msg_comparator);

static
void ldmsd_req_ctxt_sec_get(ldmsd_req_ctxt_t rctxt, ldmsd_sec_ctxt_t sctxt)
{
	if (LDMSD_CFG_TYPE_LDMS == rctxt->xprt->type) {
		ldms_xprt_cred_get(rctxt->xprt->ldms.ldms, NULL, &sctxt->crd);
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
static int prdcr_unsubscribe_regex_handler(ldmsd_req_ctxt_t req_ctxt);
static int prdcr_stream_dir_handler(ldmsd_req_ctxt_t req_ctxt);
static int strgp_add_handler(ldmsd_req_ctxt_t req_ctxt);
static int strgp_del_handler(ldmsd_req_ctxt_t req_ctxt);
static int strgp_start_handler(ldmsd_req_ctxt_t req_ctxt);
static int strgp_stop_handler(ldmsd_req_ctxt_t req_ctxt);
static int strgp_prdcr_add_handler(ldmsd_req_ctxt_t req_ctxt);
static int strgp_prdcr_del_handler(ldmsd_req_ctxt_t req_ctxt);
static int strgp_metric_add_handler(ldmsd_req_ctxt_t req_ctxt);
static int strgp_metric_del_handler(ldmsd_req_ctxt_t req_ctxt);
static int strgp_status_handler(ldmsd_req_ctxt_t req_ctxt);
static int store_time_stats_handler(ldmsd_req_ctxt_t reqc);
static int updtr_add_handler(ldmsd_req_ctxt_t req_ctxt);
static int updtr_del_handler(ldmsd_req_ctxt_t req_ctxt);
static int updtr_prdcr_add_handler(ldmsd_req_ctxt_t req_ctxt);
static int updtr_prdcr_del_handler(ldmsd_req_ctxt_t req_ctxt);
static int updtr_match_add_handler(ldmsd_req_ctxt_t req_ctxt);
static int updtr_match_del_handler(ldmsd_req_ctxt_t req_ctxt);
static int updtr_match_list_handler(ldmsd_req_ctxt_t req_ctxt);
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
static int xprt_stats_handler(ldmsd_req_ctxt_t req_ctxt);
static int prdcr_stats_handler(ldmsd_req_ctxt_t req_ctxt);
static int thread_stats_handler(ldmsd_req_ctxt_t req_ctxt);
static int set_stats_handler(ldmsd_req_ctxt_t req_ctxt);
static int unimplemented_handler(ldmsd_req_ctxt_t req_ctxt);
static int eperm_handler(ldmsd_req_ctxt_t req_ctxt);
static int ebusy_handler(ldmsd_req_ctxt_t reqc);
static int updtr_task_status_handler(ldmsd_req_ctxt_t req_ctxt);
static int prdcr_hint_tree_status_handler(ldmsd_req_ctxt_t reqc);
static int update_time_stats_handler(ldmsd_req_ctxt_t reqc);

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
static int stream_unsubscribe_handler(ldmsd_req_ctxt_t reqc);
static int stream_client_dump_handler(ldmsd_req_ctxt_t reqc);
static int stream_new_handler(ldmsd_req_ctxt_t reqc);
static int stream_dir_handler(ldmsd_req_ctxt_t reqc);

static int listen_handler(ldmsd_req_ctxt_t reqc);

static int auth_add_handler(ldmsd_req_ctxt_t reqc);
static int auth_del_handler(ldmsd_req_ctxt_t reqc);

static int set_default_authz_handler(ldmsd_req_ctxt_t reqc);
static int cmd_line_arg_set_handler(ldmsd_req_ctxt_t reqc);

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
	[LDMSD_PRDCR_UNSUBSCRIBE_REQ] = {
		LDMSD_PRDCR_UNSUBSCRIBE_REQ, prdcr_unsubscribe_regex_handler,
		XUG | LDMSD_PERM_FAILOVER_ALLOWED
	},
	[LDMSD_PRDCR_STREAM_DIR_REQ] = {
		LDMSD_PRDCR_STREAM_DIR_REQ, prdcr_stream_dir_handler,
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
	[LDMSD_STORE_TIME_STATS_REQ] = {
		LDMSD_STORE_TIME_STATS_REQ, store_time_stats_handler,
		XALL
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
	[LDMSD_UPDTR_MATCH_LIST_REQ] = {
		LDMSD_UPDTR_MATCH_LIST_REQ, updtr_match_list_handler, XUG
	},
	[LDMSD_UPDTR_STATUS_REQ] = {
		LDMSD_UPDTR_STATUS_REQ, updtr_status_handler,
		XALL | LDMSD_PERM_FAILOVER_ALLOWED
	},
	[LDMSD_UPDTR_TASK_REQ] = {
		LDMSD_UPDTR_TASK_REQ, updtr_task_status_handler,
		XALL | LDMSD_PERM_FAILOVER_ALLOWED
	},
	[LDMSD_UPDATE_TIME_STATS_REQ] = {
		LDMSD_UPDATE_TIME_STATS_REQ, update_time_stats_handler,
		XALL
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

	/* Transport Stats Request */
	[LDMSD_XPRT_STATS_REQ] = {
		LDMSD_XPRT_STATS_REQ, xprt_stats_handler, XALL
	},
	[LDMSD_THREAD_STATS_REQ] = {
		LDMSD_THREAD_STATS_REQ, thread_stats_handler, XALL
	},
	[LDMSD_PRDCR_STATS_REQ] = {
		LDMSD_PRDCR_STATS_REQ, prdcr_stats_handler, XALL
	},
	[LDMSD_SET_STATS_REQ] = {
		LDMSD_SET_STATS_REQ, set_stats_handler, XALL
	},

	[LDMSD_SET_DEFAULT_AUTHZ_REQ] = {
		LDMSD_SET_DEFAULT_AUTHZ_REQ, set_default_authz_handler, XUG
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
	[LDMSD_STREAM_UNSUBSCRIBE_REQ] = {
		LDMSD_STREAM_UNSUBSCRIBE_REQ, stream_unsubscribe_handler, XUG
	},
	[LDMSD_STREAM_CLIENT_DUMP_REQ] = {
		LDMSD_STREAM_CLIENT_DUMP_REQ, stream_client_dump_handler, XUG
	},
	[LDMSD_STREAM_NEW_REQ] = {
		LDMSD_STREAM_NEW_REQ, stream_new_handler, XUG
	},
	[LDMSD_STREAM_DIR_REQ] = {
		LDMSD_STREAM_DIR_REQ, stream_dir_handler, XUG
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

	/* CMD-LINE options */
	[LDMSD_CMDLINE_OPTIONS_SET_REQ] = {
		LDMSD_CMDLINE_OPTIONS_SET_REQ, cmd_line_arg_set_handler, XUG
	},
};

int is_req_id_priority(enum ldmsd_request req_id)
{
	switch (req_id) {
	case LDMSD_ENV_REQ:
	case LDMSD_VERBOSE_REQ:
	case LDMSD_LISTEN_REQ:
	case LDMSD_AUTH_ADD_REQ:
	case LDMSD_CMDLINE_OPTIONS_SET_REQ:
	case LDMSD_INCLUDE_REQ:
		return 1;
	default:
		return 0;
	}
}

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
	free(reqc->line_buf);
	ldmsd_msg_buf_free(reqc->_req_buf);
	ldmsd_msg_buf_free(reqc->rep_buf);
	if (reqc->xprt->cleanup_fn)
		reqc->xprt->cleanup_fn(reqc->xprt);
	else
		free(reqc->xprt);
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
	reqc->_req_buf = ldmsd_msg_buf_new(max_msg_len * 2);
	if (!reqc->_req_buf)
		goto err;
	reqc->req_buf = reqc->_req_buf->buf;
	*(uint32_t *)&reqc->req_buf[reqc->_req_buf->off] = 0; /* terminating discrim */

	reqc->rep_buf = ldmsd_msg_buf_new(max_msg_len);
	if (!reqc->rep_buf)
		goto err;

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
	key.flags = REQ_CTXT_KEY_F_LOC_REQ;
	key.msg_no = ldmsd_msg_no_get();
	key.conn_id = ldms_xprt_conn_id(ldms);
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
	rcmd->msg_flags = LDMSD_REQ_SOM_F;
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
	if (!rcmd)
		return;
	if (rcmd->org_reqc)
		req_ctxt_ref_put(rcmd->org_reqc);
	if (rcmd->reqc)
		req_ctxt_ref_put(rcmd->reqc);
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
	av_free(av_list);
	av_free(kw_list);
	*__av_list = NULL;
	*__kw_list = NULL;
	return rc;
}

int validate_ldmsd_req(ldmsd_req_hdr_t rh)
{
	ldmsd_req_attr_t attr;
	unsigned int off;
	uint32_t rec_len = ntohl(rh->rec_len);
	uint32_t attr_len;

	/* Request consists of only the header and terminating discriminator */
	if (rec_len == sizeof(*rh) + sizeof(attr->discrim)) {
		attr = (ldmsd_req_attr_t)(rh + 1);
		if (ntohl(attr->discrim) != 0)
			return 0;
		else
			return 1;
	}
	/* Request contains, at least, one attribute*/
	else if (rec_len < sizeof(*rh) + sizeof(*attr))
		return 0;

	off = sizeof(*rh);
	attr = (ldmsd_req_attr_t)(rh+1);

	while (attr->discrim) {
		attr_len = ntohl(attr->attr_len);

		off += sizeof(*attr) + attr_len;
		/* Check attr doesn't reference beyond end of request buffer */
		if (off + sizeof(attr->discrim) > rec_len)
			return 0;
		/* Check null termination of each attr_value */
		else if (attr_len && (attr->attr_value[attr_len-1] != '\0'))
			return 0;
		attr = (ldmsd_req_attr_t)&attr->attr_value[attr_len];
		if (attr->discrim) {
			if (off + sizeof(*attr) + sizeof(attr->discrim) > rec_len)
				return 0;
		}
	}
	return 1;
}

int ldmsd_handle_request(ldmsd_req_ctxt_t reqc)
{
	struct request_handler_entry *ent;
	ldmsd_req_hdr_t request = (ldmsd_req_hdr_t)reqc->req_buf;
	ldms_t ldms = NULL;
	uid_t luid;
	gid_t lgid;
	mode_t mask;

	if (LDMSD_CFG_TYPE_LDMS == reqc->xprt->type)
		ldms = reqc->xprt->ldms.ldms;

	__dlog(DLOG_DEBUG, "handling req %s\n", ldmsd_req_id2str(reqc->req_id));

	/* Check for request id outside of range */
	if ((int)request->req_id < 0 ||
	    request->req_id >= (sizeof(request_handler)/sizeof(request_handler[0])))
		return unimplemented_handler(reqc);

	ent = &request_handler[request->req_id];

	/* Check for unimplemented request */
	if (!ent->handler)
		return unimplemented_handler(reqc);

	/* Check command permission */
	if (ldms) {
		/* NOTE: NULL xprt is a config file.
		 *       So, this is an in-band ldms xprt */

		/* check against inband mask */
		mask = ldmsd_inband_cfg_mask_get();
		if (0 == (mask & ent->flag))
			return ebusy_handler(reqc);

		/* check against credential */
		struct ldms_cred crd;
		ldms_xprt_cred_get(ldms, &crd, NULL);
		luid = crd.uid;
		lgid = crd.gid;
		if (0 != ldms_access_check(ldms, 0111, luid, lgid,
				ent->flag & 0111))
			return eperm_handler(reqc);
	}

	return request_handler[request->req_id].handler(reqc);
}

int ldmsd_handle_response(ldmsd_req_cmd_t rcmd)
{
	if (!rcmd->resp_handler) {
		ldmsd_log(LDMSD_LERROR, "No response handler "
				"for request id %" PRIu32 "\n", rcmd->reqc->req_id);
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

__attribute__((format(printf, 2, 3)))
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
	int rc;
	uint32_t code;
	if (msg_type == LDMSD_REQ_TYPE_CONFIG_CMD)
		code = reqc->req_id;
	else
		code = reqc->errcode;
	req_ctxt_ref_get(reqc);
	rc = ldmsd_msg_buf_send(reqc->rep_buf,
				reqc->xprt, reqc->key.msg_no,
				reqc->xprt->send_fn,
				msg_flags, msg_type, code,
				data, data_len);
	req_ctxt_ref_put(reqc);
	return rc;
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
		rc = __ldmsd_append_buffer(rcmd->reqc, (void*)&attr.discrim,
					     sizeof(attr.discrim),
					     rcmd->msg_flags|LDMSD_REQ_EOM_F,
					     LDMSD_REQ_TYPE_CONFIG_CMD);
//		ldmsd_msg_buf_free(rcmd->reqc->rep_buf);
//		rcmd->reqc->rep_buf = NULL;
		return rc;
	}

	if (attr_id >= LDMSD_ATTR_LAST)
		return EINVAL;

	attr.discrim = 1;
	ldmsd_hton_req_attr(&attr);
	rc = __ldmsd_append_buffer(rcmd->reqc, (void*)&attr, sizeof(attr),
				   rcmd->msg_flags, LDMSD_REQ_TYPE_CONFIG_CMD);
	if (LDMSD_REQ_SOM_F == rcmd->msg_flags)
		rcmd->msg_flags = 0;
	if (rc)
		return rc;
	if (value_len) {
		rc = __ldmsd_append_buffer(rcmd->reqc, value, value_len,
				   rcmd->msg_flags, LDMSD_REQ_TYPE_CONFIG_CMD);
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

	key.msg_no = ntohl(request->msg_no);
	if (LDMSD_CFG_TYPE_LDMS == xprt->type) {
		key.flags = REQ_CTXT_KEY_F_REM_REQ;
		key.conn_id = ldms_xprt_conn_id(xprt->ldms.ldms);
	} else {
		key.flags = REQ_CTXT_KEY_F_CFGFILE;
		key.conn_id = xprt->file.cfgfile_id;
	}

	if (ntohl(request->marker) != LDMSD_RECORD_MARKER) {
		char *msg = "Config request is missing record marker";
		ldmsd_send_error_reply(xprt, -1, EINVAL, msg, strlen(msg));
		rc = EINVAL;
		goto out;
	}

	__dlog(DLOG_DEBUG, "processing message %d:%lu %s\n",
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
		reqc->xprt = malloc(sizeof(*xprt));
		if (!reqc->xprt) {
			req_ctxt_ref_put(reqc);
			goto oom;
		}
		memcpy(reqc->xprt, xprt, sizeof(*xprt));
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
	}

	rc = ldmsd_msg_gather(reqc->_req_buf, request);
	if (rc && (EBUSY != rc))
		goto err_out;
	else
		rc = 0;

	/*
	 * Point req_buf to _req_buf->buf,
	 * in case _req_buf->buf was re-alloc'ed.
	 */
	reqc->req_buf = reqc->_req_buf->buf;
	req_ctxt_tree_unlock();

	if (0 == (ntohl(request->flags) & LDMSD_REQ_EOM_F))
		/* Not the end of the message */
		goto out;

	/* Validate the request */
	rc = validate_ldmsd_req((ldmsd_req_hdr_t)reqc->req_buf);
	if (!rc) {
		char *errstr = "LDMSD received a bad request.";
		ldmsd_log(LDMSD_LERROR, "%s\n", errstr);
		ldmsd_send_error_reply(xprt, key.msg_no, rc, errstr, strlen(errstr)+1);
		goto err_out;
	}

	/* Convert the request byte order from network to host */
	ldmsd_ntoh_req_msg((ldmsd_req_hdr_t)reqc->req_buf);
	reqc->req_id = ((ldmsd_req_hdr_t)reqc->req_buf)->req_id;

	rc = ldmsd_handle_request(reqc);
	if (xprt != reqc->xprt)
		memcpy(xprt, reqc->xprt, sizeof(*xprt));

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

	key.flags = REQ_CTXT_KEY_F_LOC_REQ;
	key.msg_no = ntohl(response->msg_no);
	if (xprt->ldms.ldms)
		key.conn_id = ldms_xprt_conn_id(xprt->ldms.ldms);
	else
		key.conn_id = (uint64_t)xprt;

	if (ntohl(response->marker) != LDMSD_RECORD_MARKER) {
		ldmsd_log(LDMSD_LERROR,
			  "Config request is missing record marker\n");
		rc = EINVAL;
		goto out;
	}

	__dlog(DLOG_DEBUG, "processing response %d:%lu\n", key.msg_no, key.conn_id);

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

	rc = ldmsd_msg_gather(reqc->_req_buf, response);
	if (rc && (EBUSY != rc))
		goto err_out;
	else
		rc = 0;

	/*
	 * Point req_buf to _req_buf->buf,
	 * in case _req_buf->buf was re-alloc'ed.
	 */
	reqc->req_buf = reqc->_req_buf->buf;

	req_ctxt_tree_unlock();

	if (0 == (ntohl(response->flags) & LDMSD_REQ_EOM_F))
		/* Not the end of the message */
		goto out;

	/* Validate the request */
	rc = validate_ldmsd_req((ldmsd_req_hdr_t)reqc->req_buf);
	if (!rc) {
		char *errstr = "LDMSD received a bad response.";
		ldmsd_log(LDMSD_LERROR, "%s\n", errstr);
		goto err_out;
	}

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
	enum ldmsd_prdcr_type type = -1;
	unsigned short port_no = 0;
	int interval_us = -1;
	size_t cnt;
	uid_t uid;
	gid_t gid;
	int perm;
	char *perm_s = NULL;

	reqc->errcode = 0;
	name = host = xprt = type_s = port_s = interval_s = auth = NULL;

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
	ldmsd_req_ctxt_sec_get(reqc, &sctxt);
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
		else if (errno == ENOENT)
			goto ebadauth;
		else
			goto enomem;
	}
	__dlog(DLOG_CFGOK, "prdcr_add name=%s xprt=%s host=%s port=%u type=%s "
		"interval=%d auth=%s uid=%d gid=%d perm=%o\n",
		name, xprt, host, port_no, type_s,
		interval_us, auth ? auth : "none", (int)uid, (int)gid,
		(unsigned)perm);

	goto send_reply;
ebadauth:
	reqc->errcode = ENOENT;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"Authentication name not found, check the auth_add configuration.");
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
	free(name);
	free(type_s);
	free(port_s);
	free(interval_s);
	free(host);
	free(xprt);
	free(perm_s);
	free(auth);
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
		__dlog(DLOG_CFGOK, "prdcr_del name=%s\n", name);
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
		__dlog(DLOG_CFGOK, "prdcr_start name=%s interval=%s\n",
			name, interval_str);
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
	free(name);
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
		__dlog(DLOG_CFGOK, "prdcr_stop name=%s\n", name);
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
	if (reqc->line_buf[0] == '\0' || reqc->line_buf[0] == '0')
		__dlog(DLOG_CFGOK, "prdcr_start_regex regex=%s%s%s\n",
			prdcr_regex, interval_str ? " interval=" :"",
			interval_str ? interval_str : "");

send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(prdcr_regex);
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
	if (reqc->line_buf[0] == '\0' || reqc->line_buf[0] == '0')
		__dlog(DLOG_CFGOK, "prdcr_stop_regex regex=%s\n", prdcr_regex);

send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(prdcr_regex);
	return 0;
}

static int prdcr_subscribe_regex_handler(ldmsd_req_ctxt_t reqc)
{
	char *prdcr_regex;
	char *stream_name = NULL;
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
	if (reqc->line_buf[0] == '\0' || reqc->line_buf[0] == '0')
		__dlog(DLOG_CFGOK, "prdcr_subscribe_regex prdcr_regex=%s stream=%s\n",
			prdcr_regex, stream_name);

send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(prdcr_regex);
	free(stream_name);
	return 0;
}

static int prdcr_unsubscribe_regex_handler(ldmsd_req_ctxt_t reqc)
{
	char *prdcr_regex;
	char *stream_name = NULL;
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
	reqc->errcode = ldmsd_prdcr_unsubscribe_regex(prdcr_regex,
						      stream_name,
						      reqc->line_buf,
						      reqc->line_len, &sctxt);
	/* on error, reqc->line_buf will be filled */
	if (reqc->line_buf[0] == '\0' || reqc->line_buf[0] == '0')
		__dlog(DLOG_CFGOK, "prdcr_unsubscribe_regex prdcr_regex=%s stream=%s\n",
			prdcr_regex, stream_name);

send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(prdcr_regex);
	free(stream_name);
	return 0;
}

struct prdcr_stream_dir_regex_ctxt {
	int sent_req; /* Number of producers the STREAM_INFO_REG sent to */
	int all; /* 1 if LDMSD sent a request to all matched producers */
	int recv_resp; /* Number of responses received by LDMSD */
	json_entity_t stream_dict;
	pthread_mutex_t lock;
};

struct prdcr_stream_dir_ctxt {
	const char *prdcr_name;
	struct prdcr_stream_dir_regex_ctxt *base;
};

static int __process_stream_dir(struct prdcr_stream_dir_ctxt *ctxt, char *data, size_t data_len)
{
	int rc = 0;
	json_parser_t parser;
	json_entity_t d, a, s, ss, p;
	char *stream_name;

	parser = json_parser_new(0);
	if (!parser)
		return ENOMEM;
	rc = json_parse_buffer(parser, data, data_len, &d);
	json_parser_free(parser);
	if (rc)
		return rc;

	for (a = json_attr_first(d); a; a = json_attr_next(a)) {
		stream_name = json_attr_name(a)->str;
		s = json_attr_value(a);
		ss = json_value_find(ctxt->base->stream_dict, stream_name);
		if (!ss) {
			ss = json_entity_new(JSON_DICT_VALUE);
			json_attr_add(ctxt->base->stream_dict, stream_name, ss);
		}
		assert(!json_value_find(ss, ctxt->prdcr_name)); /* Receive stream_dir from this producer twice */
		json_attr_rem(s, "publishers"); /* We need to know only the overall statistic on sampler. */
		p = json_entity_copy(s);
		if (!p) {
			rc = ENOMEM;
			goto free_json;
		}
		json_attr_add(ss, ctxt->prdcr_name, p);
	}
free_json:
	json_entity_free(d);
	return rc;
}

static int __on_stream_dir_resp(ldmsd_req_cmd_t rcmd)
{
	int rc = 0;
	struct prdcr_stream_dir_ctxt *ctxt = rcmd->ctxt;
	struct prdcr_stream_dir_regex_ctxt *base = ctxt->base;

	__sync_fetch_and_add(&base->recv_resp, 1);
	ldmsd_req_hdr_t resp = (ldmsd_req_hdr_t)(rcmd->reqc->req_buf);
	ldmsd_req_attr_t attr = ldmsd_first_attr(resp);
	assert(attr->attr_id == LDMSD_ATTR_JSON);
	pthread_mutex_lock(&ctxt->base->lock);
	rc = __process_stream_dir(ctxt, (char*)attr->attr_value, attr->attr_len);
	pthread_mutex_unlock(&ctxt->base->lock);
	free(ctxt);

	if (!base->all)
		goto out;
	if (base->sent_req != base->recv_resp)
		goto out;

	/* Respond to the client */
	jbuf_t jb;
	struct ldmsd_req_attr_s _a;
	jb = json_entity_dump(NULL, base->stream_dict);

	_a.discrim = 1;
	_a.attr_id = LDMSD_ATTR_JSON;
	_a.attr_len = jb->cursor + 1;
	ldmsd_hton_req_attr(&_a);
	ldmsd_append_reply(rcmd->org_reqc, (char*)&_a, sizeof(_a), LDMSD_REQ_SOM_F);
	ldmsd_append_reply(rcmd->org_reqc, jb->buf, jb->cursor+1, 0);
	uint32_t discrim = 0;
	ldmsd_append_reply(rcmd->org_reqc, (char*)&(discrim),
				sizeof(discrim), LDMSD_REQ_EOM_F);
	free(base);
out:
	return rc;
}

static int __prdcr_stream_dir(ldmsd_prdcr_t prdcr, ldmsd_req_ctxt_t oreqc,
				struct prdcr_stream_dir_regex_ctxt *base)
{
	int rc;
	ldmsd_req_cmd_t rcmd;
	struct prdcr_stream_dir_ctxt *ctxt;

	ctxt = malloc(sizeof(*ctxt));
	if (!ctxt)
		return ENOMEM;
	ctxt->prdcr_name = prdcr->obj.name;
	ctxt->base = base;

	ldmsd_prdcr_lock(prdcr);
	if (prdcr->conn_state == LDMSD_PRDCR_STATE_CONNECTED) {
		/* issue stream subscribe request right away if connected */
		rcmd = ldmsd_req_cmd_new(prdcr->xprt, LDMSD_STREAM_DIR_REQ,
					 oreqc, __on_stream_dir_resp, ctxt);
		rc = errno;
		if (!rcmd)
			goto rcmd_err;

		rc = ldmsd_req_cmd_attr_term(rcmd);
		if (rc)
			goto rcmd_err;
		__sync_fetch_and_add(&base->sent_req, 1);
	}
	ldmsd_prdcr_unlock(prdcr);
	return 0;

 rcmd_err:
	ldmsd_prdcr_unlock(prdcr);
	if (rcmd)
		ldmsd_req_cmd_free(rcmd);
	return rc;
}

int prdcr_stream_dir_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	char *prdcr_regex;
	size_t cnt = 0;
	struct ldmsd_sec_ctxt sctxt;
	regex_t regex;
	ldmsd_prdcr_t prdcr;
	struct prdcr_stream_dir_regex_ctxt *ctxt;
	int count = 0;

	prdcr_regex = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_REGEX);
	if (!prdcr_regex) {
		rc = EINVAL;
		cnt = snprintf(reqc->line_buf, reqc->line_len,
				"The attribute 'regex' is required by prdcr_stop_regex.");
		goto send_reply;
	}
	ldmsd_req_ctxt_sec_get(reqc, &sctxt);

	ctxt = calloc(1, sizeof(*ctxt));
	if (!ctxt)
		return ENOMEM;
	ctxt->stream_dict = json_entity_new(JSON_DICT_VALUE);
	if (!ctxt->stream_dict) {
		rc = ENOMEM;
		goto free_ctxt;
	}
	pthread_mutex_init(&ctxt->lock, NULL);

	rc = ldmsd_compile_regex(&regex, prdcr_regex, reqc->line_buf, reqc->line_len);
	if (rc)
		goto free_ctxt;

	ldmsd_cfg_lock(LDMSD_CFGOBJ_PRDCR);
	for (prdcr = ldmsd_prdcr_first(); prdcr; prdcr = ldmsd_prdcr_next(prdcr)) {
		rc = regexec(&regex, prdcr->obj.name, 0, NULL, 0);
		if (rc)
			continue;
		(void) __prdcr_stream_dir(prdcr, reqc, ctxt); /* Ignore the failed one */
		count++;
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR);
	ctxt->all = 1;
	regfree(&regex);
	/* Don't reply now. LDMSD will reply when receiving the response from the producers. */
	if (0 == count) {
		snprintf(reqc->line_buf, reqc->line_len, "No matched producers");
		rc = ENOENT;
		goto send_reply;
	}
	return 0;

free_ctxt:
	free(ctxt);
send_reply:
	reqc->errcode = rc;
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(prdcr_regex);
	return 0;
}

int __prdcr_status_json_obj(ldmsd_req_ctxt_t reqc, ldmsd_prdcr_t prdcr, int prdcr_cnt)
{
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
	free(prdcr_name);
	free(setname);
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

int strgp_decomp_init(ldmsd_strgp_t strgp, ldmsd_req_ctxt_t req);

static int strgp_add_handler(ldmsd_req_ctxt_t reqc)
{
	char *attr_name, *name, *plugin, *container, *schema, *interval, *regex;
	char *decomp;
	name = plugin = container = schema = NULL;
	size_t cnt = 0;
	uid_t uid;
	gid_t gid;
	int perm, rc;
	char *perm_s = NULL;
	struct timespec flush_interval = {0, 0};
	struct ldmsd_sec_ctxt sec_ctxt = {};

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

	schema = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_SCHEMA);
	regex = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_REGEX);
	decomp = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_DECOMP);
	if (schema) {
		/* must not have regex */
		if (regex) {
			reqc->errcode = EINVAL;
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				       "The attributes 'schema' and 'regex' "
				       "are mutually exclusive.");
			goto send_reply;
		}
	} else if (regex) {
		/* must use decomp */
		if (!decomp) {
			reqc->errcode = EINVAL;
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				       "The attributes 'decomposition' is "
				       "required when 'regex' is used.");
			goto send_reply;
		}
	} else {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "The attribute 'schema' or 'regex' is "
			       "required by strgp_add.");
		goto send_reply;
	}

	attr_name = "flush";
	interval = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_INTERVAL);
	if (interval) {
		int rc = ldmsd_timespec_from_str(&flush_interval, interval);
		if (rc) {
			reqc->errcode = ENOENT;
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
					"The specified flush interval, \"%s\", is invalid.", interval);
			goto send_reply;
		}
	}


	struct ldmsd_plugin_cfg *store;
	store = ldmsd_get_plugin(plugin);
	if (!store) {
		reqc->errcode = ENOENT;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The plugin %s does not exist. Forgot load?\n", plugin);
		goto send_reply;
	}

	struct ldmsd_sec_ctxt sctxt;
	ldmsd_req_ctxt_sec_get(reqc, &sctxt);
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

	strgp->store = store->store;
	strgp->plugin_name = strdup(plugin);
	if (!strgp->plugin_name)
		goto enomem;


	char regex_err[512] = "";
	if (regex) {
		rc = ldmsd_compile_regex(&strgp->schema_regex, regex, regex_err, sizeof(regex_err));
		if (rc)
			goto eregex;
	} else {
		strgp->schema = strdup(schema);
		if (!strgp->schema)
			goto enomem;
	}

	strgp->container = strdup(container);
	if (!strgp->container)
		goto enomem;

	strgp->flush_interval = flush_interval;

	if (decomp) {
		strgp->decomp_name = strdup(decomp);
		if (!strgp->decomp_name)
			goto enomem;
		/* reqc->errcode, reqc->line_buf will be populated if there is an error */
		if (strgp_decomp_init(strgp, reqc)) {
			goto send_reply;
		}
	} else {
		strgp->decomp_name = NULL;
	}
	if (reqc->line_buf[0] == '\0' || reqc->line_buf[0] == '0')
		__dlog(DLOG_CFGOK, "strgp_add name=%s plugin=%s container=%s"
			"%s%s" "%s%s" "%s%s" "%s%s" "%s%s\n",
			name, plugin, container,
			schema ? " schema=" : "", schema ? schema : "",
			regex ? " regex=" : "", regex ? regex : "",
			decomp ? " decomp=" : "", decomp ? decomp : "",
			interval ? " flush=" : "", interval ? interval : "",
			perm_s ? " perm=" : "", perm_s ? perm_s : ""
			);

	goto send_reply;

eregex:
	reqc->errcode = rc;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Regular expression error: %s", regex_err);
	goto strgp_del;
enomem:
	reqc->errcode = ENOMEM;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Memory allocation failed.");
	if (!strgp)
		goto send_reply;
	/* let through */
strgp_del:
	ldmsd_strgp_del(name, &sec_ctxt);
	goto send_reply;
eexist:
	reqc->errcode = EEXIST;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The strgp %s already exists.", name);
	goto send_reply;
einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The attribute '%s' is required by strgp_add.",
		       	attr_name);
send_reply:
	if (reqc->errcode)
		ldmsd_strgp_del(name, &sec_ctxt);
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(name);
	free(plugin);
	free(container);
	free(schema);
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
		__dlog(DLOG_CFGOK, "strgp_del name=%s\n", name);
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
		__dlog(DLOG_CFGOK, "strgp_prdcr_add name=%s regex=%s\n",
			name, regex_str);
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
	free(name);
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
		__dlog(DLOG_CFGOK, "strgp_prdcr_del name=%s regex=%s\n",
			name, regex_str);
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
	free(name);
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
		__dlog(DLOG_CFGOK, "strgp_metric_add name=%s metric=%s\n",
			name, metric_name);
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
	free(name);
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
		__dlog(DLOG_CFGOK, "strgp_metric_del name=%s metric=%s\n",
			name, metric_name);
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
	free(name);
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
		__dlog(DLOG_CFGOK, "strgp_start name=%s\n", name);
		break;
	default:
		break;
	}

send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
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
		__dlog(DLOG_CFGOK, "strgp_stop name=%s\n", name);
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
		       "\"flush\":\"%ld.%06ld\","
		       "\"state\":\"%s\","
		       "\"producers\":[",
		       strgp->obj.name,
		       strgp->container,
		       strgp->schema,
		       strgp->plugin_name,
		       strgp->flush_interval.tv_sec,
		       (strgp->flush_interval.tv_nsec/1000),
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
	__dlog(DLOG_QUERY, "strgp_status %s%s\n",
		name ? " name=" : "", name ? name : "");
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
	free(name);
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
	char *endptr;
	int push_flags, is_auto_task;
	long interval, offset;

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
	} else {
		/*
		 * Verify that the given interval value is valid.
		 */
		if ('\0' == interval_str[0]) {
			reqc->errcode = EINVAL;
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
					"The given update interval value is "
					"an empty string.");
			goto send_reply;
		}
		interval = strtol(interval_str, &endptr, 0);
		if ('\0' != endptr[0]) {
			reqc->errcode = EINVAL;
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The given update interval value (%s) is not a number.",
				interval_str);
			goto send_reply;
		} else {
			if (0 >= interval) {
				reqc->errcode = EINVAL;
				cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
						"The update interval value must "
						"be larger than 0.");
				goto send_reply;
			}
		}
	}

	offset_str = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_OFFSET);
	if (offset_str) {
		/*
		 * Verify that the given offset value is valid.
		 */
		if ('\0' == offset_str[0]) {
			reqc->errcode = EINVAL;
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
					"The given update offset value is an empty string.");
			goto send_reply;
		}
		offset = strtol(offset_str, &endptr, 0);
		if ('\0' != endptr[0]) {
			reqc->errcode = EINVAL;
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
					"The given update offset value (%s) "
					"is not a number.", offset_str);
			goto send_reply;
		}
		if (interval_str && (interval < labs(offset) * 2)) {
			reqc->errcode = EINVAL;
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
					"The absolute value of the offset value "
					"must not be larger than the half of "
					"the update interval.");
			goto send_reply;
		}
	}

	push = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_PUSH);
	auto_interval = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_AUTO_INTERVAL);

	struct ldmsd_sec_ctxt sctxt;
	ldmsd_req_ctxt_sec_get(reqc, &sctxt);
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
	} else {
		__dlog(DLOG_CFGOK, "updtr_add name=%s interval=%s offset=%s%s%s"
			"%s%s%s%s\n", name, interval_str,
			offset_str ? offset_str : "0",
			auto_interval ? " auto_interval=" : "",
			auto_interval ? auto_interval : "",
			push ? " push=" : "", push ? push : "",
			perm_s ? " perm" : "", perm_s ? perm_s : "");
	}

send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(name);
	free(interval_str);
	free(auto_interval);
	free(offset_str);
	free(push);
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
		__dlog(DLOG_CFGOK, "updtr_del name=%s\n", name);
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
		__dlog(DLOG_CFGOK, "updtr_prdcr_add name=%s regex=%s\n",
			updtr_name, prdcr_regex);
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
	free(updtr_name);
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
		__dlog(DLOG_CFGOK, "updtr_prdcr_del name=%s regex=%s\n",
			updtr_name, prdcr_regex);
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
	free(updtr_name);
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
		__dlog(DLOG_CFGOK, "updtr_match_add name=%s regex=%s%s%s\n",
			updtr_name, regex_str, match_str ? " match=" : "",
			match_str ? match_str : "");
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
	case -EINVAL:
		cnt = snprintf(reqc->line_buf, reqc->line_len,
				"The regex value '%s' is invalid.",
				regex_str);
		reqc->errcode = EINVAL;
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
	free(updtr_name);
	free(regex_str);
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
		__dlog(DLOG_CFGOK, "updtr_match_del name=%s regex=%s%s%s\n",
			updtr_name, regex_str, match_str ? " match=" : "",
			match_str ? match_str : "");
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
	free(updtr_name);
	free(regex_str);
	free(match_str);
	return 0;
}

int __updtr_match_list_json_obj(ldmsd_req_ctxt_t reqc, ldmsd_updtr_t updtr,
							int updtr_cnt)
{
	int rc, set_count;
	set_count = 0;
	ldmsd_name_match_t cur_set;
	long default_offset = 0;

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
		"\"match_sets\":[",
		updtr->obj.name);
	if (rc)
		goto out;

	set_count = 0;
	for (cur_set = ldmsd_updtr_match_first(updtr); cur_set;
		cur_set = ldmsd_updtr_match_next(cur_set)) {
		if (set_count) {
			rc = linebuf_printf(reqc, ",\n");
			if (rc)
				goto out;
		}
		set_count++;
		rc = linebuf_printf(reqc,
				"\"%s\"",
				cur_set->regex_str);
		if (rc)
			goto out;
	}
	rc = linebuf_printf(reqc, "]}");
out:
	ldmsd_updtr_unlock(updtr);
	return rc;
}

static int updtr_match_list_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	size_t cnt = 0;
	struct ldmsd_req_attr_s attr;
	char *name;
	ldmsd_updtr_t updtr = NULL;
	int updtr_cnt;

	reqc->errcode = 0;

	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	__dlog(DLOG_QUERY, "updtr_match_list%s%s\n",
		name ? " name=" : "", name ? name : "");
	if (name) {
		updtr = ldmsd_updtr_find(name);
		if (!updtr) {
			cnt = snprintf(reqc->line_buf, reqc->line_len,
				"updtr '%s' does not exist.", name);
			reqc->errcode = ENOENT;
			ldmsd_send_req_response(reqc, reqc->line_buf);
			return 0;
		}
	}

	if (updtr) {
		rc = __updtr_match_list_json_obj(reqc, updtr, 0);
		if (rc)
			goto out;
	} else {
		updtr_cnt = 0;
		ldmsd_cfg_lock(LDMSD_CFGOBJ_UPDTR);
		for (updtr = ldmsd_updtr_first(); updtr;
				updtr = ldmsd_updtr_next(updtr)) {
			rc = __updtr_match_list_json_obj(reqc, updtr, updtr_cnt);
			if (rc) {
				ldmsd_cfg_unlock(LDMSD_CFGOBJ_UPDTR);
				goto out;
			}
			updtr_cnt++;
		}
		ldmsd_cfg_unlock(LDMSD_CFGOBJ_UPDTR);
	}
	cnt = reqc->line_off + 2; /* +2 for '[' and ']' */

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
	rc = ldmsd_append_reply(reqc, (char *)&attr.discrim, sizeof(uint32_t),
								LDMSD_REQ_EOM_F);
out:
	free(name);
	if (updtr)
		ldmsd_updtr_put(updtr);
	return rc;
}

static int updtr_start_handler(ldmsd_req_ctxt_t reqc)
{
	char *updtr_name, *interval_str, *offset_str, *auto_interval;
	updtr_name = interval_str = offset_str = auto_interval = NULL;
	size_t cnt = 0;
	char *endptr;
	long interval, offset = 0;
	struct ldmsd_sec_ctxt sctxt;

	reqc->errcode = 0;

	updtr_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!updtr_name) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The updater name must be specified.");
		goto send_reply;
	}
	offset_str  = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_OFFSET);
	if (offset_str) {
		/*
		 * Verify that the given offset value is valid.
		 */
		if ('\0' == offset_str[0]) {
			reqc->errcode = EINVAL;
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
					"The given update offset value is an empty string.");
			goto send_reply;
		}
		offset = strtol(offset_str, &endptr, 0);
		if ('\0' != endptr[0]) {
			reqc->errcode = EINVAL;
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
					"The given update offset value (%s) "
					"is not a number.", offset_str);
			goto send_reply;
		}
	}
	interval_str = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_INTERVAL);
	if (interval_str) {
		if ('\0' == interval_str[0]) {
			reqc->errcode = EINVAL;
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
					"The given update interval value is "
					"an empty string.");
			goto send_reply;
		}
		interval = strtol(interval_str, &endptr, 0);
		if ('\0' != endptr[0]) {
			reqc->errcode = EINVAL;
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The given update interval value (%s) is not a number.",
				interval_str);
			goto send_reply;
		} else {
			if (0 >= interval) {
				reqc->errcode = EINVAL;
				cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
						"The update interval value must "
						"be larger than 0. (%ld)", interval);
				goto send_reply;
			}
			if (offset_str && interval < labs(offset) * 2) {
				reqc->errcode = EINVAL;
				cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
						"The absolute value of the offset value "
						"must not be larger than the half of "
						"the update interval. (i=%ld,o=%ld)",
						interval, offset);
				goto send_reply;
			}
		}
	}


	auto_interval = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_AUTO_INTERVAL);

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);
	reqc->errcode = ldmsd_updtr_start(updtr_name, interval_str, offset_str,
					  auto_interval, &sctxt);
	__dlog(DLOG_CFGOK, "UPDTR_START %d\n", reqc->errcode);
	switch (reqc->errcode) {
	case 0:
		__dlog(DLOG_CFGOK, "updtr_start name=%s" "%s%s" "%s%s" "%s%s\n",
			updtr_name,
			interval_str ? " interval=" : "",
			interval_str ? interval_str : "",
			offset_str ? " offset=" : "",
			offset_str ? offset_str : "",
			auto_interval ? " auto_interval=" : "",
			auto_interval ? auto_interval : "");
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
	free(updtr_name);
	free(interval_str);
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
		__dlog(DLOG_CFGOK, "updtr_stop name=%s\n", updtr_name);
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

void __updtr_stats(ldmsd_prdcr_set_t prdset, int *skipped_cnt,
					  int *oversampled_cnt)
{
	*skipped_cnt = *skipped_cnt + prdset->skipped_upd_cnt;
	*oversampled_cnt = *oversampled_cnt + prdset->oversampled_cnt;
}

int __updtr_status_json_obj(ldmsd_req_ctxt_t reqc, ldmsd_updtr_t updtr,
							int updtr_cnt)
{
	int rc;
	ldmsd_prdcr_ref_t ref;
	ldmsd_prdcr_t prdcr;
	int prdcr_count;
	ldmsd_prdcr_set_t prdset;
	ldmsd_name_match_t match = NULL;
	long default_offset = 0;
	int skipped_cnt = 0;
	int oversampled_cnt = 0;
	const char *str;

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
		"\"auto\":\"%s\","
		"\"state\":\"%s\","
		"\"producers\":[",
		updtr->obj.name,
		updtr->default_task.sched.intrvl_us,
		default_offset,
		((updtr->default_task.task_flags==LDMSD_TASK_F_SYNCHRONOUS)?"true":"false"),
		update_mode(updtr->push_flags),
		(updtr->is_auto_task ? "true" : "false"),
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

		if (LIST_EMPTY(&updtr->match_list)) {
			prdset = ldmsd_prdcr_set_first(prdcr);
			while (prdset) {
				__updtr_stats(prdset, &skipped_cnt,
						     &oversampled_cnt);
				prdset = ldmsd_prdcr_set_next(prdset);
			}
		} else {
			LIST_FOREACH(match, &updtr->match_list, entry) {
				prdset = ldmsd_prdcr_set_first(prdcr);
				while (prdset) {
					if (match) {
						if (match->selector == LDMSD_NAME_MATCH_INST_NAME)
							str = prdset->inst_name;
						else
							str = prdset->schema_name;
						rc = regexec(&match->regex, str, 0, NULL, 0);
						if (rc)
							goto next;
					}
					__updtr_stats(prdset, &skipped_cnt,
							     &oversampled_cnt);
				next:
					prdset = ldmsd_prdcr_set_next(prdset);
				}
			}
		}
	}
	rc = linebuf_printf(reqc, "],"
				  "\"outstanding count\":%d,"
				  "\"oversampled count\":%d}",
				  skipped_cnt, oversampled_cnt);
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
	__dlog(DLOG_QUERY, "updtr_status %s%s\n",
		name ? " name=" : "", name ? name : "");
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
	__dlog(DLOG_QUERY, "updtr_task_status%s%s\n",
		name ? " name=" : "", name ? name : "");
	if (name) {
		updtr = ldmsd_updtr_find(name);
		if (!updtr) {
			cnt = snprintf(reqc->line_buf, reqc->line_len, "updtr '%s' not found", name);
			ldmsd_send_error_reply(reqc->xprt, reqc->key.msg_no, ENOENT,
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
	ldmsd_send_error_reply(reqc->xprt, reqc->key.msg_no, rc,
						"internal error", 15);
out:
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
	__dlog(DLOG_QUERY, "prdcr_hint_tree_status%s%s\n",
		name ? " name=" : "", name ? name : "");
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
	ldmsd_send_error_reply(reqc->xprt, reqc->key.msg_no, EINTR,
				"interval error", 14);
out:
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
	__dlog(DLOG_CFGOK, "setgroup_add name=%s" "%s%s" "%s%s" "%s%s\n",
		name, producer ? " producer=" : "", producer ? producer : "",
		interval ? " interval=" : "", interval ? interval : "",
		offset ? " offset=" : "", offset ? offset : "");
	goto out;

err:
	ldms_set_delete(grp);
	grp = NULL;
out:
	reqc->errcode = rc;
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(name);
	free(producer);
	free(interval);
	free(offset);
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
	__dlog(DLOG_CFGOK, "setgroup_mod name=%s" "%s%s" "%s%s\n",
		name, interval ? " interval=" : "", interval ? interval : "",
		offset ? " offset=" : "", offset ? offset : "");
	goto out;

err:
	ldms_set_put(grp);
	grp = NULL;
out:
	reqc->errcode = rc;
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(name);
	free(interval);
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

	__dlog(DLOG_CFGOK, "setgroup_del name=%s\n", name);
	ldms_set_delete(grp);
	rc = 0;

out:
	reqc->errcode = rc;
	ldmsd_send_req_response(reqc, reqc->line_buf);
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
	__dlog(DLOG_CFGOK, "setgroup_ins name=%s%s%s\n", name,
		instance ? " instance=" : "", instance ? instance : "");

out:
	reqc->errcode = rc;
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(name);
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
	__dlog(DLOG_CFGOK, "setgroup_rm name=%s%s%s\n", name,
		instance ? " instance=" : "", instance ? instance : "");

out:
	reqc->errcode = rc;
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(name);
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
		__dlog(DLOG_CFGOK, "start name=%s%s%s%s%s\n", plugin_name,
			interval_us ? " interval=" : "",
			interval_us ? interval_us : "",
			offset ? " offset=" : "", offset ? offset : "");

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
	free(plugin_name);
	free(interval_us);
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
		__dlog(DLOG_CFGOK, "stop name=%s\n", plugin_name);
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

	__dlog(DLOG_QUERY, "plugn_status\n");
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
	else
		__dlog(DLOG_CFGOK, "load name=%s\n", plugin_name);
	goto send_reply;

einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The attribute '%s' is required by load.", attr_name);
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
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
		__dlog(DLOG_CFGOK, "term name=%s\n", plugin_name);
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
	free(plugin_name);
	return 0;
}

static int plugn_config_handler(ldmsd_req_ctxt_t reqc)
{
	char *plugin_name, *config_attr, *attr_name;
	plugin_name = config_attr = NULL;
	struct attr_value_list *av_list = NULL;
	struct attr_value_list *kw_list = NULL;
	char *attr_copy = NULL;
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
	 * Count the number of spaces. That's the maximum number of
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
	attr_copy = strdup(config_attr);
	if (!av_list || !kw_list || !attr_copy) {
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
	} else {
		__dlog(DLOG_CFGOK, "config name=%s %s\n", plugin_name,
			attr_copy);
	}
	goto send_reply;

einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The attribute '%s' is required by config.",
		       	attr_name);
	goto send_reply;
err:
	av_free(kw_list);
	av_free(av_list);
	free(attr_copy);
	kw_list = NULL;
	av_list = NULL;
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(plugin_name);
	free(config_attr);
	av_free(kw_list);
	av_free(av_list);
	free(attr_copy);
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
	free(name);
	return rc;
}

static int plugn_list_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	struct ldmsd_req_attr_s attr;

	__dlog(DLOG_QUERY, "usage\n");
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
	__dlog(DLOG_QUERY, "plugn_sets%s%s\n", plugin? " name=" : "",
		plugin ? plugin : "");
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
	free(plugin);
	return rc;

err:
	ldmsd_send_error_reply(reqc->xprt, reqc->key.msg_no, rc,
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
		__dlog(DLOG_CFGOK, "udata instance=%s metric=%s udata=%s\n",
			set_name, metric_name, udata);
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
	free(set_name);
	free(metric_name);
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
	if (!reqc->errcode)
		__dlog(DLOG_CFGOK, "udata_regex instance=%s regex=%s base=%s"
			"%s%s\n", set_name, regex, base_s,
			inc_s ? " incr=" : "", inc_s ? inc_s : "");
	goto out;
einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The attribute '%s' is required.", attr_name);
out:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(set_name);
	free(base_s);
	free(regex);
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

	__dlog(DLOG_CFGOK, "loglevel level=%s%s\n", level_s,
		is_test ? " test" : "");
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
	free(level_s);
	return 0;
}

int __daemon_status_json_obj(ldmsd_req_ctxt_t reqc)
{
	int rc;
	char *json_s;
	size_t json_sz;

	rc = linebuf_printf(reqc, "{\"state\":\"ready\",\n");
	if (rc)
		return rc;
	json_s = __thread_stats_as_json(&json_sz);
	if (!json_s)
		return ENOMEM;
	rc = linebuf_printf(reqc, "\"thread_stats\":%s\n", json_s);
	free(json_s);
	if (rc)
		return rc;
	rc = linebuf_printf(reqc, "}");
	return rc;
}

static int daemon_status_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	struct ldmsd_req_attr_s attr;

	__dlog(DLOG_QUERY, "daemon_status\n");
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

	__dlog(DLOG_QUERY, "version\n");
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
					v->name, v->value, STRERROR(rc));
			goto out;
		}
		__dlog(DLOG_CFGOK, "env %s=%s\n", v->name, v->value);
	}
out:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	av_free(kw_list);
	av_free(av_list);
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
				path, lineno, STRERROR(reqc->errcode));
		} else {
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Failed to process cfg '%s' at line '%d'",
				path, lineno);
		}
	}

out:
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
	__dlog(DLOG_CFGOK, "oneshot name=%s time=%s\n", name, time_s);
	ldmsd_send_req_response(reqc, NULL);
	free(name);
	free(time_s);
	return rc;

einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The attribute '%s' is required by oneshot.",
		       	attr_name);

out:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(name);
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
				STRERROR(reqc->errcode));
	}
	ldmsd_send_req_response(reqc, reqc->line_buf);
	return 0;
}

static int exit_daemon_handler(ldmsd_req_ctxt_t reqc)
{
	cleanup_requested = 1;
	__dlog(DLOG_CFGOK, "daemon_exit\n");
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
	my_attr.attr_len = server_attr->attr_len + strlen((char *)rcmd->ctxt) + 1;
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
	char *myself = strdup(ldmsd_myname_get());
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
		ldmsd_append_reply(reqc, myself, strlen(myself)+1, 0);
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
		attr.attr_len = reqc->rep_buf->len - 2*sizeof(struct ldmsd_req_hdr_s)
						- sizeof(struct ldmsd_req_attr_s);
		ldmsd_hton_req_attr(&attr);
		int msg_flag = LDMSD_REQ_SOM_F;

		/* Construct the message */
		for (i = 0; i < num_rec; i++) {
			remaining = reqc->rep_buf->len - 2* sizeof(struct ldmsd_req_hdr_s);
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
	free(rep_len_str);
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
					"\"trans_start_nsec\":\"%ld\","
					"\"trans_end_sec\":\"%ld\","
					"\"trans_end_nsec\":\"%ld\""
					"}"
				"}",
				ldmsd_myname_get(),
				ldmsd_set_info_origin_enum2str(info->origin_type),
				info->origin_name,
				info->interval_us,
				info->offset_us,
				((info->sync)?"true":"false"),
				info->start.tv_sec,
				info->start.tv_nsec,
				info->end.tv_sec,
				info->end.tv_nsec);
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
					"\"last_start_nsec\":\"%ld\","
					"\"last_end_sec\":\"%ld\","
					"\"last_end_nsec\":\"%ld\""
					"}"
				"}",
				ldmsd_myname_get(),
				ldmsd_set_info_origin_enum2str(info->origin_type),
				info->origin_name,
				info->prd_set->prdcr->host_name,
				info->interval_us,
				info->offset_us,
				((info->sync)?"true":"false"),
				info->start.tv_sec,
				info->start.tv_nsec,
				info->end.tv_sec,
				info->end.tv_nsec);
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
	my_attr.attr_len = strlen(ctxt->my_info) + attr->attr_len;
	if (!ctxt->is_internal) {
		/* +2 for a square bracket and a curly bracket and '\0'*/
		my_attr.attr_len += 3;
	}
	my_attr.discrim = 1;
	ldmsd_hton_req_attr(&my_attr);
	(void) ldmsd_append_reply(org_reqc, (char *)&my_attr, sizeof(my_attr), LDMSD_REQ_SOM_F);
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
	__dlog(DLOG_QUERY, "set_route instance=%s\n", inst_name);
	is_internal = ldmsd_req_attr_keyword_exist_by_id(reqc->req_buf, LDMSD_ATTR_TYPE);

	info = ldmsd_set_info_get(inst_name);
	if (!info) {
		/* The set does not exist. */
		cnt = snprintf(reqc->line_buf, reqc->line_len,
				"%s: Set '%s' not exist.",
				ldmsd_myname_get(), inst_name);
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
					"prdcr '%s'", ldmsd_myname_get(),
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
	rc = 0;
	goto out;
err2:
	free(ctxt->my_info);
err1:
	free(ctxt);
err0:
	ldmsd_set_info_delete(info);
out:
	free(inst_name);
	return rc;
}
/*
 * {
 *     "compute_time_us"        : <int>,
 *     "connect_rate_s"         : <float>,
 *     "connect_request_rate_s" : <float>,
 *     "disconnect_rate_s"      : <float>,
 *     "reject_rate_s"          : <float>,
 *     "auth_fail_rate_s"       : <float>,
 *     "xprt_count"  : <int>,
 *     "open_count"  : <int>,
 *     "close_count" : <int>,
 *     "lookup_req"  : {
 *        "count"    : <int>
 *        "total_us" : <int>
 *        "min_us"   : <int>
 *        "max_us"   : <int>
 *        "mean_us"  : <int>
 *        "max_xprt" : { "host" : <string>, "xprt" : <string>, "port" : <int> },
 *        "min_xprt" : { "host" : <string>, "xprt" : <string>, "port" : <int> }
 *     },
 *     "lookup_resp" : {
 *        "count"    : <int>
 *        "total_us" : <int>
 *        "min_us"   : <int>
 *        "max_us"   : <int>
 *        "mean_us"  : <int>
 *        "max_xprt" : { "host" : <string>, "xprt" : <string>, "port" : <int> },
 *        "min_xprt" : { "host" : <string>, "xprt" : <string>, "port" : <int> }
 *     },
 *     "update_req"  : {
 *        "count"    : <int>
 *        "total_us" : <int>
 *        "min_us"   : <int>
 *        "max_us"   : <int>
 *        "mean_us"  : <int>
 *        "max_xprt" : { "host" : <string>, "xprt" : <string>, "port" : <int> },
 *        "min_xprt" : { "host" : <string>, "xprt" : <string>, "port" : <int> }
 *     }
 *     "update_resp" : {
 *        "count"    : <int>
 *        "total_us" : <int>
 *        "min_us"   : <int>
 *        "max_us"   : <int>
 *        "mean_us"  : <int>
 *        "max_xprt" : { "host" : <string>, "xprt" : <string>, "port" : <int> },
 *        "min_xprt" : { "host" : <string>, "xprt" : <string>, "port" : <int> }
 *     }
 * }
 */
struct op_summary {
	uint64_t op_count;
	uint64_t op_total_us;
	uint64_t op_min_us;
	struct ldms_xprt *op_min_xprt;
	uint64_t op_max_us;
	struct ldms_xprt *op_max_xprt;
	uint64_t op_mean_us;
};

#define __APPEND_SZ 4096
#define __APPEND(...) do {					\
	int len = snprintf(s, sz, __VA_ARGS__);			\
	if (len >= sz) {					\
		uint64_t off = (uint64_t)s - (uint64_t)buff;	\
		sz += LDMS_ROUNDUP(len-sz, __APPEND_SZ);	\
		s = realloc(buff, off + sz);			\
		if (!s) {					\
			goto __APPEND_ERR;			\
		}						\
		buff = s;					\
		s = &buff[off];					\
		continue;					\
	}							\
	s += len;						\
	sz -= len;						\
	break;							\
} while(1)

static char *__xprt_stats_as_json(size_t *json_sz)
{
	char *buff;
	char *s;
	size_t sz = __APPEND_SZ;
	struct ldms_xprt_stats xs;
	struct op_summary op_sum[LDMS_XPRT_OP_COUNT];
	struct ldms_xprt *x;
	enum ldms_xprt_ops_e op_e;
	int xprt_count = 0;
	int xprt_connect_count = 0;
	int xprt_connecting_count = 0;
	int xprt_listen_count = 0;
	int xprt_close_count = 0;
	struct timespec start, end;
	struct sockaddr_storage ss_local, ss_remote;
	struct sockaddr_in *sin;
	socklen_t socklen;
	zap_err_t zerr;
	char ip_str[32];
	char xprt_type[16];
	struct ldms_xprt_rate_data rate_data;
	int reset = 0;

	xprt_type[sizeof(xprt_type)-1] = 0; /* NULL-terminate at the end */

	(void)clock_gettime(CLOCK_REALTIME, &start);

	ldms_xprt_rate_data(&rate_data, reset);

	buff = malloc(sz);
	if (!buff)
		return NULL;
	s = buff;

	memset(op_sum, 0, sizeof(op_sum));
	for (op_e = 0; op_e < LDMS_XPRT_OP_COUNT; op_e++)
		op_sum[op_e].op_min_us = LLONG_MAX;

	/* Compute summary statistics across all of the transports */
	for (x = ldms_xprt_first(); x; x = ldms_xprt_next(x)) {
		ldms_stats_entry_t op;

		ldms_xprt_stats(x, &xs);
		xprt_count += 1;
		zap_ep_state_t ep_state =
			(x->zap_ep ? zap_ep_state(x->zap_ep) : ZAP_EP_CLOSE);
		switch (ep_state) {
		case ZAP_EP_LISTENING:
			xprt_listen_count += 1;
			break;
		case ZAP_EP_ACCEPTING:
		case ZAP_EP_CONNECTING:
			xprt_connecting_count += 1;
			break;
		case ZAP_EP_CONNECTED:
			xprt_connect_count += 1;
			break;
		case ZAP_EP_INIT:
		case ZAP_EP_PEER_CLOSE:
		case ZAP_EP_CLOSE:
		case ZAP_EP_ERROR:
			xprt_close_count += 1;
		}
		for (op_e = 0; op_e < LDMS_XPRT_OP_COUNT; op_e++) {
			op = &xs.ops[op_e];
			if (!op->count)
				continue;
			op_sum[op_e].op_total_us += op->total_us;
			op_sum[op_e].op_count += op->count;
			if (op->min_us < op_sum[op_e].op_min_us) {
				op_sum[op_e].op_min_us = op->min_us;
				op_sum[op_e].op_min_xprt = ldms_xprt_get(x);
			}
			if (op->max_us > op_sum[op_e].op_max_us) {
				op_sum[op_e].op_max_us = op->max_us;
				op_sum[op_e].op_max_xprt = ldms_xprt_get(x);
			}
		}
		assert(x->ref_count > 1);
		ldms_xprt_put(x);
	}
	for (op_e = 0; op_e < LDMS_XPRT_OP_COUNT; op_e++) {
		if (op_sum[op_e].op_count) {
			op_sum[op_e].op_mean_us =
				op_sum[op_e].op_total_us / op_sum[op_e].op_count;
		}
	}

	(void)clock_gettime(CLOCK_REALTIME, &end);
	uint64_t compute_time = ldms_timespec_diff_us(&start, &end);

	__APPEND("{");
	__APPEND(" \"compute_time_us\": %ld,\n", compute_time);
	__APPEND(" \"connect_rate_s\": %f,\n", rate_data.connect_rate_s);
	__APPEND(" \"connect_request_rate_s\": %f,\n", rate_data.connect_request_rate_s);
	__APPEND(" \"disconnect_rate_s\": %f,\n", rate_data.disconnect_rate_s);
	__APPEND(" \"reject_rate_s\": %f,\n", rate_data.reject_rate_s);
	__APPEND(" \"auth_fail_rate_s\": %f,\n", rate_data.auth_fail_rate_s);
	__APPEND(" \"xprt_count\": %d,\n", xprt_count);
	__APPEND(" \"connect_count\": %d,\n", xprt_connect_count);
	__APPEND(" \"connecting_count\": %d,\n", xprt_connecting_count);
	__APPEND(" \"listen_count\": %d,\n", xprt_listen_count);
	__APPEND(" \"close_count\": %d,\n", xprt_close_count);
	__APPEND(" \"duration\": %g,\n", rate_data.duration);
	__APPEND(" \"op_stats\": {\n");
	for (op_e = 0; op_e < LDMS_XPRT_OP_COUNT; op_e++) {
		struct op_summary *op;
		op = &op_sum[op_e];
		__APPEND(" \"%s\" : {\n", ldms_xprt_op_names[op_e]);
		__APPEND("    \"count\": %ld,\n", op->op_count);
		__APPEND("    \"total_us\": %ld,\n", op->op_total_us);
		__APPEND("    \"min_us\": %ld,\n", (op->op_count ? op->op_min_us: 0));
		sin = (struct sockaddr_in *)&ss_remote;
		memset(&ss_remote, 0, sizeof(ss_remote));
		strncpy(ip_str, "0.0.0.0:0", sizeof(ip_str));
		strncpy(xprt_type, "????", sizeof(xprt_type));
		if (op->op_min_xprt && op->op_min_xprt->zap_ep) {
			socklen = sizeof(ss_local);
			zerr = zap_get_name(op->op_min_xprt->zap_ep,
					    (struct sockaddr *)&ss_local,
					    (struct sockaddr *)&ss_remote,
					    &socklen);
			memccpy(xprt_type, ldms_xprt_type_name(op->op_min_xprt),
				0, sizeof(xprt_type)-1);
			inet_ntop(sin->sin_family, &sin->sin_addr, ip_str, sizeof(ip_str));
		}
		if (op->op_min_xprt)
			ldms_xprt_put(op->op_min_xprt);
		__APPEND("    \"min_peer\": \"%s:%hu\"\n,", ip_str, ntohs(sin->sin_port));
		__APPEND("    \"min_peer_type\": \"%s\"\n,", xprt_type);

		__APPEND("    \"max_us\": %ld,\n", (op->op_count ? op->op_max_us : 0));
		memset(&ss_remote, 0, sizeof(ss_remote));

		if (op->op_max_xprt && op->op_max_xprt->zap_ep) {
			socklen = sizeof(ss_local);
			zerr = zap_get_name(op->op_max_xprt->zap_ep,
					    (struct sockaddr *)&ss_local,
					    (struct sockaddr *)&ss_remote,
					    &socklen);
			memccpy(xprt_type, ldms_xprt_type_name(op->op_max_xprt),
				0, sizeof(xprt_type)-1);
			inet_ntop(sin->sin_family, &sin->sin_addr, ip_str, sizeof(ip_str));
		}
		if (op->op_max_xprt)
			ldms_xprt_put(op->op_max_xprt);
		__APPEND("    \"max_peer\": \"%s:%hu\"\n,", ip_str, ntohs(sin->sin_port));
		__APPEND("    \"max_peer_type\": \"%s\"\n,", xprt_type);
		__APPEND("    \"mean_us\": %ld\n", op->op_mean_us);
		if (op_e < LDMS_XPRT_OP_COUNT - 1)
			__APPEND(" },\n");
		else
			__APPEND(" }\n");
	}
	__APPEND(" }\n"); /* op_stats */
	__APPEND("}");
	*json_sz = s - buff + 1;
	return buff;
__APPEND_ERR:
	return NULL;
}

static int xprt_stats_handler(ldmsd_req_ctxt_t req)
{
	char *s, *json_s;
	size_t json_sz;
	int reset = 0;
	struct ldmsd_req_attr_s attr;

	s = ldmsd_req_attr_str_value_get_by_id(req, LDMSD_ATTR_RESET);
	__dlog(DLOG_QUERY, "xprt_stats%s%s\n", s ? " reset=" : "", s? s : "");
	if (s) {
		if (0 != strcasecmp(s, "false"))
			reset = 1;
		free(s);
	}

	json_s = __xprt_stats_as_json(&json_sz);
	if (!json_s)
		goto err;

	attr.discrim = 1;
	attr.attr_id = LDMSD_ATTR_JSON;
	attr.attr_len = json_sz;
	ldmsd_hton_req_attr(&attr);

	if (ldmsd_append_reply(req, (const char *)&attr, sizeof(attr), LDMSD_REQ_SOM_F))
		goto err;

	if (ldmsd_append_reply(req, json_s, json_sz, 0))
		goto err;

	attr.discrim = 0;
	if (ldmsd_append_reply(req, (const char *)&attr.discrim, sizeof(attr.discrim), LDMSD_REQ_EOM_F))
		goto err;

	free(json_s);
	return 0;

err:
	free(json_s);
	req->errcode = ENOMEM;
	ldmsd_send_req_response(req, "Memory allocation error.");
	return ENOMEM;
}

/*
 * Sends a JSON formatted summary of Zap thread statistics as follows:
 *
 * { "count" : <int>,
 *   "entries" : [
 * 		{ "name" : <string>,
 *  	  "sample_count" : <float>,
 *  	  "sample_rate" : <float>,
 *        "utilization" : <float>
 *      },
 *      . . .
 *   ]
 * }
 */
static char * __thread_stats_as_json(size_t *json_sz)
{
	char *buff, *s;
	size_t sz = __APPEND_SZ;
	int i;
	struct timespec start, end;
	struct zap_thrstat_result *res;

	(void)clock_gettime(CLOCK_REALTIME, &start);

	res = zap_thrstat_get_result();
	if (!res)
		return NULL;

	buff = malloc(sz);
	if (!buff)
		goto __APPEND_ERR;
	s = buff;

	__APPEND("{");
	__APPEND(" \"count\": %d,\n", res->count);
	__APPEND(" \"entries\": [\n");
	for (i = 0; i < res->count; i++) {
		__APPEND("  {\n");
		__APPEND("   \"name\": \"%s\",\n", res->entries[i].name);
		__APPEND("   \"sample_count\": %g,\n", res->entries[i].sample_count);
		__APPEND("   \"sample_rate\": %g,\n", res->entries[i].sample_rate);
		__APPEND("   \"utilization\": %g,\n", res->entries[i].utilization);
		__APPEND("   \"sq_sz\": %lu,\n", res->entries[i].sq_sz);
		__APPEND("   \"n_eps\": %lu\n", res->entries[i].n_eps);
		if (i < res->count - 1)
			__APPEND("  },\n");
		else
			__APPEND("  }\n");
	}
	(void)clock_gettime(CLOCK_REALTIME, &end);
	uint64_t compute_time = ldms_timespec_diff_us(&start, &end);
	__APPEND(" ],\n"); /* end of entries array */
	__APPEND(" \"compute_time\": %ld\n", compute_time);
	__APPEND("}"); /* end */

	*json_sz = s - buff + 1;
	zap_thrstat_free_result(res);
	return buff;
__APPEND_ERR:
	zap_thrstat_free_result(res);
	free(buff);
	return NULL;
}

static int thread_stats_handler(ldmsd_req_ctxt_t req)
{
	char *json_s, *s;
	size_t json_sz;
	int reset = 0;
	struct ldmsd_req_attr_s attr;

	s = ldmsd_req_attr_str_value_get_by_id(req, LDMSD_ATTR_RESET);
	__dlog(DLOG_QUERY, "thread_stats%s%s\n", s ? " reset=" : "", s? s : "");
	if (s) {
		if (0 != strcasecmp(s, "false"))
			reset = 1;
		free(s);
	}

	json_s = __thread_stats_as_json(&json_sz);
	if (!json_s)
		goto err;

	attr.discrim = 1;
	attr.attr_id = LDMSD_ATTR_JSON;
	attr.attr_len = json_sz;
	ldmsd_hton_req_attr(&attr);
	if (ldmsd_append_reply(req, (const char *)&attr, sizeof(attr), LDMSD_REQ_SOM_F))
		goto err;
	if (ldmsd_append_reply(req, json_s, json_sz, 0))
		goto err;
	attr.discrim = 0;
	if (ldmsd_append_reply(req, (const char *)&attr, sizeof(attr.discrim), LDMSD_REQ_EOM_F))
		goto err;

	free(json_s);
	if (reset)
		zap_thrstat_reset_all();
	return 0;
err:
	free(json_s);
	req->errcode = ENOMEM;
	ldmsd_send_req_response(req, "Memory allocation failure.");
	return ENOMEM;
}

/*
 * Sends a JSON formatted summary of Producer statistics as follows:
 *
 * {
 *   "prdcr_count" : <int>,
 *   "stopped" : <int>,
 *   "disconnected" : <int>,
 *   "connecting" : <int>,
 * 	 "connected" : <int>,
 *   "stopping"	: <int>,
 *   "compute_time" : <int>
 * }
 */
static char * __prdcr_stats_as_json(size_t *json_sz)
{
	ldmsd_prdcr_t prdcr;
	struct timespec start, end;
	char *buff, *s;
	size_t sz = __APPEND_SZ;
	int prdcr_count = 0, stopped_count = 0, disconnected_count = 0,
		connecting_count = 0, connected_count = 0, stopping_count = 0,
		set_count = 0;

	(void)clock_gettime(CLOCK_REALTIME, &start);
	ldmsd_cfg_lock(LDMSD_CFGOBJ_PRDCR);
	for (prdcr = ldmsd_prdcr_first(); prdcr;
			prdcr = ldmsd_prdcr_next(prdcr)) {
		prdcr_count += 1;
		switch (prdcr->conn_state) {
		case LDMSD_PRDCR_STATE_STOPPED:
			stopped_count++;
			break;
		case LDMSD_PRDCR_STATE_DISCONNECTED:
			disconnected_count++;
			break;
		case LDMSD_PRDCR_STATE_CONNECTING:
			connecting_count++;
			break;
		case LDMSD_PRDCR_STATE_CONNECTED:
			connected_count++;
			break;
		case LDMSD_PRDCR_STATE_STOPPING:
			stopping_count++;
			break;
		}
		set_count += rbt_card(&prdcr->set_tree);
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR);

	buff = malloc(sz);
	if (!buff)
		goto __APPEND_ERR;
	s = buff;

	__APPEND("{");
	__APPEND(" \"prdcr_count\": %d,\n", prdcr_count);
	__APPEND(" \"stopped_count\": %d,\n", stopped_count);
	__APPEND(" \"disconnected_count\": %d,\n", disconnected_count);
	__APPEND(" \"connecting_count\": %d,\n", connecting_count);
	__APPEND(" \"connected_count\": %d,\n", connected_count);
	__APPEND(" \"stopping_count\": %d,\n", stopping_count);
	__APPEND(" \"set_count\": %d,\n", set_count);
	(void)clock_gettime(CLOCK_REALTIME, &end);
	uint64_t compute_time = ldms_timespec_diff_us(&start, &end);
	__APPEND(" \"compute_time\": %ld\n", compute_time);
	__APPEND("}"); /* end */

	*json_sz = s - buff + 1;
	return buff;

__APPEND_ERR:
	free(buff);
	return NULL;
}

static int prdcr_stats_handler(ldmsd_req_ctxt_t req)
{
	char *json_s;
	size_t json_sz;
	struct ldmsd_req_attr_s attr;

	__dlog(DLOG_QUERY, "prdcr_stats\n");
	json_s = __prdcr_stats_as_json(&json_sz);
	if (!json_s)
		goto err;

	attr.discrim = 1;
	attr.attr_id = LDMSD_ATTR_JSON;
	attr.attr_len = json_sz;
	ldmsd_hton_req_attr(&attr);
	if (ldmsd_append_reply(req, (const char *)&attr, sizeof(attr), LDMSD_REQ_SOM_F))
		goto err;
	if (ldmsd_append_reply(req, json_s, json_sz, 0))
		goto err;
	attr.discrim = 0;
	if (ldmsd_append_reply(req, (const char *)&attr, sizeof(attr.discrim), LDMSD_REQ_EOM_F))
		goto err;

	free(json_s);
	return 0;
err:
	free(json_s);
	req->errcode = ENOMEM;
	ldmsd_send_req_response(req, "Memory allocation failure.");
	return ENOMEM;
}

/*
 * Sends a JSON formatted summary of Metric Set statistics as follows:
 *
 * {
 *   "active_count" : <int>,
 *   "deleting_count" : <int>,
 *   "mem_total" : <int>,
 *   "mem_used" : <int>,
 *   "mem_free" : <int>,
 *   "compute_time" : <int>
 * }
 */
static char * __set_stats_as_json(size_t *json_sz)
{
	struct timespec start, end;
	char *buff, *s;
	size_t sz = __APPEND_SZ;
	struct mm_stat stats;
	int rc;
	double freq;
	double set_load = 0;
	uint32_t data_sz;
	ldmsd_name_match_t match;
	ldmsd_updtr_t updtr = NULL;

	(void)clock_gettime(CLOCK_REALTIME, &start);
	mm_stats(&stats);

	buff = malloc(sz);
	if (!buff)
		goto __APPEND_ERR;
	s = buff;

	ldmsd_cfg_lock(LDMSD_CFGOBJ_UPDTR);
	for (updtr = ldmsd_updtr_first(); updtr;
			updtr = ldmsd_updtr_next(updtr)) {
		if (!LIST_EMPTY(&updtr->match_list)) {
			LIST_FOREACH(match, &updtr->match_list, entry) {
				ldmsd_prdcr_ref_t ref;
				for (ref = ldmsd_updtr_prdcr_first(updtr); ref;
						ref = ldmsd_updtr_prdcr_next(ref)) {
					ldmsd_prdcr_lock(ref->prdcr);
					ldmsd_prdcr_set_t prd_set;
					for (prd_set = ldmsd_prdcr_set_first(ref->prdcr); prd_set;
							prd_set = ldmsd_prdcr_set_next(prd_set)) {
						rc = regexec(&match->regex, prd_set->inst_name, 0, NULL, 0);
						if (rc)
							continue;
						freq = 1000000 / (double)prd_set->updt_interval;
						if (prd_set->set) {
							data_sz = ldms_set_data_sz_get(prd_set->set);
							set_load += data_sz * freq;
						}
					}
					ldmsd_prdcr_unlock(ref->prdcr);
				}
			}
		} else {
			ldmsd_prdcr_ref_t ref;
			for (ref = ldmsd_updtr_prdcr_first(updtr); ref;
					ref = ldmsd_updtr_prdcr_next(ref)) {
				ldmsd_prdcr_lock(ref->prdcr);
				ldmsd_prdcr_set_t prd_set;
				for (prd_set = ldmsd_prdcr_set_first(ref->prdcr); prd_set;
						prd_set = ldmsd_prdcr_set_next(prd_set)) {
					freq = 1000000 / (double)prd_set->updt_interval;
					if (prd_set->set) {
						data_sz = ldms_set_data_sz_get(prd_set->set);
						set_load += data_sz * freq;
					}
				}
				ldmsd_prdcr_unlock(ref->prdcr);
			}
		}
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_UPDTR);

	__APPEND("{");
	__APPEND(" \"active_count\": %d,\n", ldms_set_count());
	__APPEND(" \"deleting_count\": %d,\n", ldms_set_deleting_count());
	__APPEND(" \"mem_total_kb\": %g,\n", (double)stats.size / 1024.0);
	__APPEND(" \"mem_free_kb\": %g,\n", (double)(stats.bytes * stats.grain) / 1024.0);
	__APPEND(" \"mem_used_kb\": %g,\n", (double)(stats.size - (stats.bytes * stats.grain)) / 1024.0);
	__APPEND(" \"set_load\": %g,\n", set_load);
	(void)clock_gettime(CLOCK_REALTIME, &end);
	uint64_t compute_time = ldms_timespec_diff_us(&start, &end);
	__APPEND(" \"compute_time\": %ld\n", compute_time);
	__APPEND("}");

	*json_sz = s - buff + 1;
	return buff;

__APPEND_ERR:
	free(buff);
	return NULL;
}

static int set_stats_handler(ldmsd_req_ctxt_t req)
{
	char *json_s;
	size_t json_sz;
	struct ldmsd_req_attr_s attr;

	__dlog(DLOG_QUERY, "set_stats\n");
	json_s = __set_stats_as_json(&json_sz);
	if (!json_s)
		goto err;

	attr.discrim = 1;
	attr.attr_id = LDMSD_ATTR_JSON;
	attr.attr_len = json_sz;
	ldmsd_hton_req_attr(&attr);
	if (ldmsd_append_reply(req, (const char *)&attr, sizeof(attr), LDMSD_REQ_SOM_F))
		goto err;
	if (ldmsd_append_reply(req, json_s, json_sz, 0))
		goto err;
	attr.discrim = 0;
	if (ldmsd_append_reply(req, (const char *)&attr, sizeof(attr.discrim), LDMSD_REQ_EOM_F))
		goto err;

	free(json_s);
	return 0;
err:
	free(json_s);
	req->errcode = ENOMEM;
	ldmsd_send_req_response(req, "Memory allocation failure.");
	return ENOMEM;
}

static const char *__xprt_prdcr_name_get(ldms_t x)
{
	ldmsd_xprt_ctxt_t ctxt = x->app_ctxt;
	if (!ctxt)
		return NULL;
	return ctxt->name;
}

static int stream_publish_handler(ldmsd_req_ctxt_t reqc)
{
	char *stream_name;
	ldmsd_stream_type_t stream_type = LDMSD_STREAM_STRING;
	ldmsd_req_attr_t attr;
	int cnt;
	char *p_name;

	stream_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!stream_name) {
		reqc->errcode = EINVAL;
		ldmsd_log(LDMSD_LERROR, "%s: The stream name is missing "
			  "in the config message\n", __func__);
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "The stream name is missing.");
		goto err_reply;
	}

	reqc->errcode = 0;
	/*
	 * If a LDMSD_ATTR_TYPE attribute exists, the publisher does not want
	 * an acknowledge response.
	 */
	attr = ldmsd_req_attr_get_by_id(reqc->req_buf, LDMSD_ATTR_TYPE);
	if (!attr)
		ldmsd_send_req_response(reqc, "ACK");

	/* Check for string */
	attr = ldmsd_req_attr_get_by_id(reqc->req_buf, LDMSD_ATTR_STRING);
	if (attr)
		goto out_1;

	/* Check for JSon */
	attr = ldmsd_req_attr_get_by_id(reqc->req_buf, LDMSD_ATTR_JSON);
	if (attr) {
		stream_type = LDMSD_STREAM_JSON;
	} else {
		goto out_0;
	}
out_1:
	p_name = (char *)__xprt_prdcr_name_get(reqc->xprt->ldms.ldms);
	ldmsd_stream_deliver(stream_name, stream_type,
			     (char *)attr->attr_value,
			     attr->attr_len, NULL, p_name);
out_0:
	free(stream_name);
	return 0;
err_reply:
	free(stream_name);
	ldmsd_send_req_response(reqc, reqc->line_buf);
	return 0;
}

static int __on_republish_resp(ldmsd_req_cmd_t rcmd)
{
	ldmsd_req_attr_t attr;
	ldmsd_req_hdr_t resp = (ldmsd_req_hdr_t)(rcmd->reqc->req_buf);
	attr = ldmsd_first_attr(resp);
	ldmsd_log(LDMSD_LDEBUG, "%s: %s\n", __func__, (char *)attr->attr_value);
	return 0;
}

static int stream_republish_cb(ldmsd_stream_client_t c, void *ctxt,
			       ldmsd_stream_type_t stream_type,
			       const char *data, size_t data_len,
			       json_entity_t entity)
{
	ldms_t ldms = (ldms_t)ctxt;
	int rc, attr_id = LDMSD_ATTR_STRING;
	const char *stream = ldmsd_stream_client_name(c);
	ldmsd_req_cmd_t rcmd = ldmsd_req_cmd_new(ldms, LDMSD_STREAM_PUBLISH_REQ,
						 NULL, __on_republish_resp, NULL);
	if (!rcmd) {
		ldmsd_log(LDMSD_LCRITICAL, "ldmsd is out of memory\n");
		return ENOMEM;
	}
	rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_NAME, stream);
	if (rc)
		goto out;
	/*
	 * Add an LDMSD_ATTR_TYPE attribute to let the peer know
	 * that we don't want an acknowledge response.
	 */
	rc = ldmsd_req_cmd_attr_append_str(rcmd, LDMSD_ATTR_TYPE, "");
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

/* RSE: remote stream entry */
struct __RSE_key_s {
	/* xprt ref */
	ldms_t xprt;
	/* stream name */
	char name[];
};

typedef struct __RSE_s {
	struct rbn rbn;
	ldmsd_stream_client_t client;
	struct __RSE_key_s key;
} *__RSE_t;

int __RSE_cmp(void *tree_key, const void *key)
{
	const struct __RSE_key_s *k0, *k1;

	k0 = tree_key;
	k1 = key;
	if (k0->xprt < k1->xprt)
		return -1;
	if (k0->xprt > k1->xprt)
		return 1;
	/* reaching here means same xprt */
	return strcmp(k0->name, k1->name);
}

pthread_mutex_t __RSE_rbt_mutex = PTHREAD_MUTEX_INITIALIZER;
struct rbt __RSE_rbt = RBT_INITIALIZER(__RSE_cmp);

static inline
void __RSE_rbt_lock()
{
	pthread_mutex_lock(&__RSE_rbt_mutex);
}

static inline
void __RSE_rbt_unlock()
{
	pthread_mutex_unlock(&__RSE_rbt_mutex);
}

static inline
__RSE_t __RSE_alloc(const char *name, ldms_t xprt)
{
	__RSE_t ent;
	ent = calloc(1, sizeof(*ent) + strlen(name) + 1);
	if (!ent)
		return NULL;
	sprintf(ent->key.name, "%s", name);
	ent->key.xprt = xprt;
	rbn_init(&ent->rbn, &ent->key);
	ldms_xprt_get(xprt);
	return ent;
}

static inline
void __RSE_free(__RSE_t ent)
{
	ldms_xprt_put(ent->key.xprt);
	free(ent);
}

static inline
__RSE_t __RSE_find(const struct __RSE_key_s *key)
{
	/* caller must hold __RSE_rbt_mutex */
	struct rbn *rbn;
	rbn = rbt_find(&__RSE_rbt, key);
	if (!rbn)
		return NULL;
	return container_of(rbn, struct __RSE_s, rbn);
}

static inline
void __RSE_ins(__RSE_t ent)
{
	/* caller must hold __RSE_rbt_mutex */
	rbt_ins(&__RSE_rbt, &ent->rbn);
}

static inline
void __RSE_del(__RSE_t ent)
{
	/* caller must hold __RSE_rbt_mutex */
	rbt_del(&__RSE_rbt, &ent->rbn);
}

static int stream_subscribe_handler(ldmsd_req_ctxt_t reqc)
{
	char *stream_name;
	int cnt;
	int len;
	__RSE_t ent;
	char _buff[sizeof(struct __RSE_key_s) + 256]; /* should be enough for stream name */
	struct __RSE_key_s *key = (void*)_buff;

	stream_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!stream_name) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "The stream name is missing.");
		goto send_reply;
	}
	key->xprt = reqc->xprt->ldms.ldms;
	len = snprintf(key->name, 256, "%s", stream_name);
	if (len >= 256) {
		reqc->errcode = ENAMETOOLONG;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "The stream name is too long (%d >= %d).",
			       len, 256);
		goto send_reply;
	}
	__RSE_rbt_lock();
	ent = __RSE_find(key);
	if (ent) {
		__RSE_rbt_unlock();
		reqc->errcode = EEXIST;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "Already subscribed to `%s` stream",
			       stream_name);
		goto send_reply;
	}
	ent = __RSE_alloc(stream_name, reqc->xprt->ldms.ldms);
	if (!ent) {
		__RSE_rbt_unlock();
		reqc->errcode = ENOMEM;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "Memory allocation failed");
		goto send_reply;
	}

	ent->client = ldmsd_stream_subscribe(stream_name, stream_republish_cb,
					     ent->key.xprt);
	if (!ent->client) {
		__RSE_rbt_unlock();
		__RSE_free(ent);
		reqc->errcode = errno;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "ldmsd_stream_subscribe() error: %d", errno);
		goto send_reply;
	}
	ldmsd_stream_flags_set(ent->client, LDMSD_STREAM_F_RAW);
	__RSE_ins(ent);
	reqc->errcode = 0;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len, "OK");
	__RSE_rbt_unlock();
	__dlog(DLOG_CFGOK, "subscribe name=%s\n", stream_name);
send_reply:
	free(stream_name);
	ldmsd_send_req_response(reqc, reqc->line_buf);
	return 0;
}

static int stream_unsubscribe_handler(ldmsd_req_ctxt_t reqc)

{
	char *stream_name;
	int cnt;
	int len;
	__RSE_t ent;
	char _buff[sizeof(struct __RSE_key_s) + 256]; /* should be enough for stream name */
	struct __RSE_key_s *key = (void*)_buff;

	stream_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!stream_name) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "The stream name is missing.");
		goto send_reply;
	}
	key->xprt = reqc->xprt->ldms.ldms;
	len = snprintf(key->name, 256, "%s", stream_name);
	if (len >= 256) {
		reqc->errcode = ENAMETOOLONG;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "The stream name is too long (%d >= %d).",
			       len, 256);
		goto send_reply;
	}
	__RSE_rbt_lock();
	ent = __RSE_find(key);
	if (!ent) {
		__RSE_rbt_unlock();
		reqc->errcode = ENOENT;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "`%s` stream not found", stream_name);
		goto send_reply;
	}
	__RSE_del(ent);
	ldmsd_stream_close(ent->client);
	__RSE_free(ent);
	reqc->errcode = 0;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len, "OK");
	__RSE_rbt_unlock();
	__dlog(DLOG_CFGOK, "unsubscribe name=%s\n", stream_name);

send_reply:
	free(stream_name);
	ldmsd_send_req_response(reqc, reqc->line_buf);
	return 0;
}

static int stream_client_dump_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	struct ldmsd_req_attr_s attr;
	char *json;

	__dlog(DLOG_QUERY, "stream_client_dump\n");
	/* constructin JSON reply */
	json = ldmsd_stream_client_dump();
	if (!json)
		return errno;
	rc = linebuf_printf(reqc, "%s", json);
	free(json);
	if (rc)
		return rc;

	/* sending messages */
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

static int stream_new_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	char *name;

	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!name) {
		ldmsd_log(LDMSD_LERROR, "Received %s without the stream name\n",
				ldmsd_req_id2str(reqc->req_id));
		return 0;
	}
	rc = ldmsd_stream_new(name);
	if (rc) {
		ldmsd_log(LDMSD_LERROR, "Error %d: failed to create stream %s\n",
									rc, name);
		free(name);
	}
	return 0;
}

static int stream_dir_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	char *s;
	size_t len;
	struct ldmsd_req_attr_s attr;

	s = ldmsd_stream_dir_dump();
	if (!s) {
		reqc->errcode = errno;
		rc = snprintf(reqc->line_buf, reqc->line_len,
				"Failed to get stream_info_dump.");
		ldmsd_send_req_response(reqc, reqc->line_buf);
		return 0;
	}
	attr.discrim = 1;
	attr.attr_id = LDMSD_ATTR_JSON;
	attr.attr_len = len = strlen(s) + 1;
	ldmsd_hton_req_attr(&attr);
	rc = ldmsd_append_reply(reqc, (char *)&attr, sizeof(attr), LDMSD_REQ_SOM_F);
	if (rc)
		goto out;

	rc = ldmsd_append_reply(reqc, s, strlen(s) + 1, 0);
	if (rc)
		goto out;

	attr.discrim = 0;
	rc = ldmsd_append_reply(reqc, (char *)(&attr.discrim),
				sizeof(attr.discrim), LDMSD_REQ_EOM_F);
out:
	free(s);
	return rc;
}

void ldmsd_xprt_term(ldms_t x)
{
	__RSE_t ent;
	struct rbn *rbn;
	char _buff[sizeof(struct __RSE_key_s) + 256] = {};
	struct __RSE_key_s *key = (void*)_buff;

	key->xprt = x;
	__RSE_rbt_lock();
	rbn = rbt_find_lub(&__RSE_rbt, key);
	while (rbn) {
		ent = container_of(rbn, struct __RSE_s, rbn);
		if (key->xprt != ent->key.xprt)
			break;
		/* points rbn to the successor before removing ent */
		rbn = rbn_succ(rbn);
		/* delete from the tree */
		__RSE_del(ent);
		ldmsd_stream_close(ent->client);
		__RSE_free(ent);
	}
	__RSE_rbt_unlock();

	/* Free outstanding configuration requests */
	req_ctxt_tree_lock();
	ldmsd_req_ctxt_t reqc;
	rbn = rbt_min(&msg_tree);
	while (rbn) {
		struct rbn *next_rbn = rbn_succ(rbn);
		reqc = container_of(rbn, struct ldmsd_req_ctxt, rbn);
		if (reqc->key.conn_id == ldms_xprt_conn_id(x))
			__free_req_ctxt(reqc);
		rbn = next_rbn;
	}
	req_ctxt_tree_unlock();
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

extern int ldmsd_listen_start(ldmsd_listen_t listen);
static int listen_handler(ldmsd_req_ctxt_t reqc)
{
	ldmsd_listen_t listen;
	char *xprt, *port, *host, *auth, *attr_name;
	unsigned short port_no = -1;
	xprt = port = host = auth = NULL;

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
		else if (errno == ENOENT) {
			reqc->errcode = ENOENT;
			(void) snprintf(reqc->line_buf, reqc->line_len,
					"Listen error: authentication domain "
					"'%s' not found. Please make sure "
					"that it is created with `auth_add` "
					"config command.",
					auth);
			goto send_reply;
		}
		else
			goto enomem;
	}

	if (ldmsd_is_initialized()) {
		reqc->errcode = ldmsd_listen_start(listen);
		if (reqc->errcode) {
			(void)snprintf(reqc->line_buf, reqc->line_len,
				"Failed to listen on the endpoint %s:%s.",
				xprt, port);
		}
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
	goto send_reply;
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(xprt);
	free(port);
	free(host);
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
	free(name);
	free(plugin);
	free(auth_args);
	av_free(auth_opts);
	return 0;
}

static int auth_del_handler(ldmsd_req_ctxt_t reqc)
{
	const char *attr_name;
	char *name;
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
	free(name);
	return 0;
}

static int set_default_authz_handler(ldmsd_req_ctxt_t reqc)
{
	char *value = NULL;
	uid_t uid;
	bool uid_is_supplied = false;
	gid_t gid;
	bool gid_is_supplied = false;
	mode_t perm;
	bool perm_is_supplied = false;

	reqc->errcode = 0;

	/* Each of LDMSD_ATTR_UID, LDMSD_ATTR_GID, and LDMSD_ATTR_PERM
	   are optional */

	value = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_UID);
	if (value) {
		/* Check if the value is a valid user name */
		struct passwd *pwd = getpwnam(value);
		if (pwd) {
			uid = pwd->pw_uid;
			uid_is_supplied = true;
		} else {
			/* Check if the value is a valid UID */
			char *endptr;
			errno = 0;
			uid = strtol(value, &endptr, 0);
			if (errno == 0 && endptr != value && *endptr == '\0') {
				uid_is_supplied = true;
			}
		}
		if (!uid_is_supplied) {
			reqc->errcode = EINVAL;
			(void) snprintf(reqc->line_buf, reqc->line_len,
					"uid value \"%s\" is not a valid UID or user name",
					value);
		}
		free(value);
	}

	value = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_GID);
	if (value) {
		/* Check if the value is a valid user name */
		struct group *grp = getgrnam(value);
		if (grp) {
			gid = grp->gr_gid;
			gid_is_supplied = true;
		} else {
			/* Check if the value is a valid GID */
			char *endptr;
			errno = 0;
			gid = strtol(value, &endptr, 0);
			if (errno == 0 && endptr != value && *endptr == '\0') {
				gid_is_supplied = true;
			}
		}
		if (!gid_is_supplied) {
			reqc->errcode = EINVAL;
			(void) snprintf(reqc->line_buf, reqc->line_len,
					"gid value \"%s\" is not a valid GID or user name",
					value);
		}
		free(value);
	}

	value = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_PERM);
	if (value) {
		char *endptr;
		errno = 0;
		perm = strtol(value, &endptr, 8);
		if (errno || endptr == value || *endptr != '\0') {
			reqc->errcode = EINVAL;
			(void) snprintf(reqc->line_buf, reqc->line_len,
					"String to permission bits conversion failed");
		} else if (perm < 0 || perm > 0777) {
			reqc->errcode = EINVAL;
			(void) snprintf(reqc->line_buf, reqc->line_len,
					"Permissions value is out of range");
		} else {
			perm_is_supplied = true;
		}
		free(value);
	}

	/* only set/get the values if no errors occurred in parsing the rpc */
	if (reqc->errcode == 0) {
		int set_flags = 0;
		if (uid_is_supplied)
			set_flags |= DEFAULT_AUTHZ_SET_UID;
		if (gid_is_supplied)
			set_flags |= DEFAULT_AUTHZ_SET_GID;
		if (perm_is_supplied)
			set_flags |= DEFAULT_AUTHZ_SET_PERM;
		ldms_set_default_authz(&uid, &gid, &perm, set_flags);

		(void) snprintf(reqc->line_buf, reqc->line_len,
				"defaults: uid=%d, gid=%d, perm=%o",
				(int)uid, (int)gid, (int)perm);
	}

	ldmsd_send_req_response(reqc, reqc->line_buf);

	return 0;
}

extern struct option long_opts[];
extern char *short_opts;
extern int ldmsd_process_cmd_line_arg(char opt, char *value);
static int __cmdline_options(ldmsd_req_ctxt_t reqc, int argc, char *argv[])
{
	int opt, opt_idx;
	reqc->errcode = 0;
	opterr = 0;
	optind = 0;
	while (0 < (opt = getopt_long(argc, argv,
				  short_opts, long_opts,
				  &opt_idx))) {
		switch (opt) {
		case 'x':
			reqc->errcode = EINVAL;
			snprintf(reqc->line_buf, reqc->line_len,
					"The 'option' command does not support 'x' or 'xprt'. "
					"Use the 'listen' command instead.");
			return reqc->errcode;
		case 'F':
		case 'u':
		case 'V':
			reqc->errcode = EINVAL;
			snprintf(reqc->line_buf, reqc->line_len,
					"The option '%s' must be given at the command line.",
					argv[optind-1]);
			return reqc->errcode;
		default:
			reqc->errcode = ldmsd_process_cmd_line_arg(opt, optarg);
			if (reqc->errcode == ENOENT) {
				snprintf(reqc->line_buf, reqc->line_len,
					"Unknown cmd-line option or it must be "
					"given at the command line: %s\n", argv[optind-1]);
				return reqc->errcode;
			}
			break;
		}
	}
	return 0;
}

static int cmd_line_arg_set_handler(ldmsd_req_ctxt_t reqc)
{
	char *s, *dummy, *token, *ptr1;
	int rc = 0;
	int argc;
	char **argv = NULL;
	int count = 0;

	dummy = NULL;
	s = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_STRING);
	if (!s) {
		snprintf(reqc->line_buf, reqc->line_len, "No options are given.");
		reqc->errcode = EINVAL;
		goto send_reply;
	}
	dummy = strdup(s);
	if (!dummy)
		goto enomem;

	/* Count number of tokens */
	for (token = strtok_r(dummy, " \t\n", &ptr1); token;
			token = strtok_r(NULL, " \t\n", &ptr1)) {
		count++;
	}

	argv = calloc(count + 2, sizeof(char *)); /* +2 is for "option" and NULL*/
	if (!argv)
		goto enomem;

	/* Populate argv */
	opterr = 0;
	argv[0] = "option";
	argc = 1;
	for (token = strtok_r(s, " \t\n", &ptr1); token;
			token = strtok_r(NULL, " \t\n", &ptr1)) {
		argv[argc++] = token;
	}

	(void) __cmdline_options(reqc, argc, argv);
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(s);
	free(dummy);
	free(argv);
	return rc;
enomem:
	snprintf(reqc->line_buf, reqc->line_len, "ldmsd is out of memory.");
	reqc->errcode = ENOMEM;
	ldmsd_log(LDMSD_LCRITICAL, "Out of memroy\n");
	rc = ENOMEM;
	goto send_reply;
}

static int
__prdset_upd_time_stats_json_obj(ldmsd_req_ctxt_t reqc, ldmsd_updtr_t updtr,
				ldmsd_prdcr_t prdcr, ldmsd_name_match_t match)
{
	int rc;
	ldmsd_prdcr_set_t prdset;
	int cnt = 0;
	const char *str;

	ldmsd_prdcr_lock(prdcr);
	if (prdcr->conn_state != LDMSD_PRDCR_STATE_CONNECTED || prdcr->xprt->disconnected)
		goto unlock;
	rc = linebuf_printf(reqc, "\"%s\":{", prdcr->obj.name);
	if (rc)
		goto unlock;
	prdset = ldmsd_prdcr_set_first(prdcr);
	while (prdset) {
		if (match) {
			if (match->selector == LDMSD_NAME_MATCH_INST_NAME)
				str = prdset->inst_name;
			else
				str = prdset->schema_name;
			rc = regexec(&match->regex, str, 0, NULL, 0);
			if (rc)
				goto next_prdset;
		}
		pthread_mutex_lock(&prdset->lock);
		rc = linebuf_printf(reqc, "%s\"%s\":{"
				"\"min\":%lf,"
				"\"max\":%lf,"
				"\"avg\":%lf,"
				"\"cnt\":%d"
				"}",
				(cnt?",":""),
				prdset->inst_name,
				prdset->updt_stat.min,
				prdset->updt_stat.max,
				prdset->updt_stat.avg,
				prdset->updt_stat.count);
		pthread_mutex_unlock(&prdset->lock);
		if (rc)
			goto end_quote;
		cnt++;
next_prdset:
		prdset = ldmsd_prdcr_set_next(prdset);
	}
end_quote:
	rc = linebuf_printf(reqc, "}");
unlock:
	ldmsd_prdcr_unlock(prdcr);
	return rc;
}

extern ldmsd_prdcr_ref_t updtr_prdcr_ref_first(ldmsd_updtr_t updtr);
extern ldmsd_prdcr_ref_t updtr_prdcr_ref_next(ldmsd_prdcr_ref_t ref);
static int
__upd_time_stats_json_obj(ldmsd_req_ctxt_t reqc, ldmsd_updtr_t updtr)
{
	int rc;
	ldmsd_name_match_t match;
	int cnt = 0;

	rc = linebuf_printf(reqc, "\"%s\":{", updtr->obj.name);
	if (rc)
		return rc;

	if (!LIST_EMPTY(&updtr->match_list)) {
		LIST_FOREACH(match, &updtr->match_list, entry) {
			ldmsd_prdcr_ref_t ref;
			for (ref = updtr_prdcr_ref_first(updtr); ref;
					ref = updtr_prdcr_ref_next(ref)) {
				if (cnt)
					rc = linebuf_printf(reqc, ",");
				if (rc)
					goto out;
				rc = __prdset_upd_time_stats_json_obj(reqc, updtr,
							  ref->prdcr, match);
				if (rc)
					goto out;
				cnt++;
			}
		}
	} else {
		ldmsd_prdcr_ref_t ref;
		for (ref = updtr_prdcr_ref_first(updtr); ref;
				ref = updtr_prdcr_ref_next(ref)) {
			if (cnt)
				rc = linebuf_printf(reqc, ",");
			if (rc)
				goto out;
			rc = __prdset_upd_time_stats_json_obj(reqc, updtr,
						   ref->prdcr, NULL);
			if (rc)
				goto out;
			cnt++;
		}
	}
	rc = linebuf_printf(reqc, "}");
out:
	return rc;
}

static int update_time_stats_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	ldmsd_updtr_t updtr;
	char *name;
	int cnt = 0;

	rc = linebuf_printf(reqc, "{");
	if (rc)
		goto err;

	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (name) {
		updtr = ldmsd_updtr_find(name);
		if (!updtr) {
			/* Not report any status */
			snprintf(reqc->line_buf, reqc->line_len,
				"updtr '%s' doesn't exist.", name);
			reqc->errcode = ENOENT;
			ldmsd_send_req_response(reqc, reqc->line_buf);
			return 0;
		}
		rc = __upd_time_stats_json_obj(reqc, updtr);
	} else {
		ldmsd_cfg_lock(LDMSD_CFGOBJ_UPDTR);
		for (updtr = ldmsd_updtr_first(); updtr;
				updtr = ldmsd_updtr_next(updtr)) {
			if (cnt) {
				rc = linebuf_printf(reqc, ",");
				if (rc) {
					ldmsd_cfg_unlock(LDMSD_CFGOBJ_UPDTR);
					goto err;
				}
			}
			rc = __upd_time_stats_json_obj(reqc, updtr);
			if (rc) {
				ldmsd_cfg_unlock(LDMSD_CFGOBJ_UPDTR);
				goto err;
			}
			cnt++;
		}
		ldmsd_cfg_unlock(LDMSD_CFGOBJ_UPDTR);
	}
	rc = linebuf_printf(reqc, "}");
	if (rc)
		goto err;
	ldmsd_send_req_response(reqc, reqc->line_buf);
	goto out;
err:
	snprintf(reqc->line_buf, reqc->line_len, "Failed to query the update time stats. Error %d.", rc);
	reqc->errcode = rc;
	ldmsd_send_req_response(reqc, reqc->line_buf);
out:
	free(name);
	return rc;
}

static int __store_time_stats_json_obj(ldmsd_req_ctxt_t reqc, ldmsd_strgp_t strgp)
{
	int rc;

	rc = linebuf_printf(reqc, "\"%s\":{\"min\":%lf,"
					  "\"max\":%lf,"
					  "\"avg\":%lf,"
					  "\"cnt\":%d}",
					  strgp->obj.name,
					  strgp->stat.min,
					  strgp->stat.max,
					  strgp->stat.avg,
					  strgp->stat.count);
	return rc;
}

static int store_time_stats_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	char *name;
	ldmsd_strgp_t strgp;
	int cnt = 0;

	rc = linebuf_printf(reqc, "{");
	if (rc)
		goto err;

	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (name) {
		strgp = ldmsd_strgp_find(name);
		if (!strgp) {
			/* Not report any status */
			snprintf(reqc->line_buf, reqc->line_len,
				"strgp '%s' doesn't exist.", name);
			reqc->errcode = ENOENT;
			ldmsd_send_req_response(reqc, reqc->line_buf);
			return 0;
		}
		rc = __store_time_stats_json_obj(reqc, strgp);
	} else {
		ldmsd_cfg_lock(LDMSD_CFGOBJ_STRGP);
		for (strgp = ldmsd_strgp_first(); strgp;
				strgp = ldmsd_strgp_next(strgp)) {
			if (cnt) {
				rc = linebuf_printf(reqc, ",");
				if (rc) {
					ldmsd_cfg_unlock(LDMSD_CFGOBJ_STRGP);
					goto err;
				}
			}
			ldmsd_strgp_lock(strgp);
			rc = __store_time_stats_json_obj(reqc, strgp);
			if (rc) {
				ldmsd_strgp_unlock(strgp);
				ldmsd_cfg_unlock(LDMSD_CFGOBJ_STRGP);
				goto err;
			}
			ldmsd_strgp_unlock(strgp);
			cnt++;
		}
		ldmsd_cfg_unlock(LDMSD_CFGOBJ_STRGP);
	}

	rc = linebuf_printf(reqc, "}");
	if (rc)
		goto err;
	ldmsd_send_req_response(reqc, reqc->line_buf);
	goto out;
err:
	snprintf(reqc->line_buf, reqc->line_len, "Failed to query the store "
						 "time stats. Error %d.", rc);
	reqc->errcode = rc;
	ldmsd_send_req_response(reqc, reqc->line_buf);
out:
	free(name);
	return rc;
}
