/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015-2020,2023,2025 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2015-2020,2023,2025 Open Grid Computing, Inc. All rights reserved.
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
#include <netdb.h>
#include <coll/rbt.h>
#include <pthread.h>
#include <unistd.h>
#include <getopt.h>
#include <pwd.h>
#include <grp.h>
#include <regex.h>
#include <ovis_util/util.h>
#include <ovis_json/ovis_json.h>
#include <arpa/inet.h>
#include "mmalloc.h"
#include "zap/zap.h"
#include "ldms.h"
#include "ldms_xprt.h"
#include "ldmsd.h"
#include "ldmsd_request.h"
#include "ldmsd_stream.h"

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

static ovis_log_t config_log;

pthread_mutex_t msg_tree_lock = PTHREAD_MUTEX_INITIALIZER;

int ldmsd_req_debug = 0; /* turn bits on / off using gdb or -L
			 * to see request/response and other messages */
FILE *ldmsd_req_debug_file = NULL; /* change with -L or
				    * ldmsd.c:process_log_config */

static int stream_enabled = 1;
static int cleanup_requested = 0;

static char * __thread_stats_as_json(size_t *json_sz);
static char * __xprt_stats_as_json(size_t *json_sz, int reset, int level);
extern const char *prdcr_state_str(enum ldmsd_prdcr_state state);

extern int ldmsd_quota; /* defined in ldmsd.c */

#define CONFIG_PLAYBACK_ENABLED(_match_) ((_match_) & ldmsd_req_debug)
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
		ovis_log(config_log, OVIS_LALWAYS, fmt, ap);
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
static int ldmsd_cfg_cntr_handler(ldmsd_req_ctxt_t req_ctxt);
static int dump_cfg_handler(ldmsd_req_ctxt_t req_ctxt);
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
static int prdcr_stream_status_handler(ldmsd_req_ctxt_t req_ctxt);
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
static int plugn_usage_handler(ldmsd_req_ctxt_t req_ctxt);
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
static int set_sec_mod_handler(ldmsd_req_ctxt_t reqc);
static int log_status_handler(ldmsd_req_ctxt_t reqc);
static int stats_reset_handler(ldmsd_req_ctxt_t reqc);
static int profiling_handler(ldmsd_req_ctxt_t req);

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
static int stream_status_handler(ldmsd_req_ctxt_t reqc);
static int stream_disable_handler(ldmsd_req_ctxt_t reqc);

static int msg_stats_handler(ldmsd_req_ctxt_t reqc);
static int msg_client_stats_handler(ldmsd_req_ctxt_t reqc);
static int msg_disable_handler(ldmsd_req_ctxt_t reqc);

static int listen_handler(ldmsd_req_ctxt_t reqc);

static int auth_add_handler(ldmsd_req_ctxt_t reqc);
static int auth_del_handler(ldmsd_req_ctxt_t reqc);

static int set_default_authz_handler(ldmsd_req_ctxt_t reqc);

/* The handler of configuration that needs to the start-up time. */
static int cmd_line_arg_set_handler(ldmsd_req_ctxt_t reqc);
static int default_auth_handler(ldmsd_req_ctxt_t reqc);
static int set_memory_handler(ldmsd_req_ctxt_t reqc);
static int log_file_handler(ldmsd_req_ctxt_t reqc);
static int publish_kernel_handler(ldmsd_req_ctxt_t reqc);
static int daemon_name_set_handler(ldmsd_req_ctxt_t reqc);
static int worker_threads_set_handler(ldmsd_req_ctxt_t reqc);
static int default_quota_set_handler(ldmsd_req_ctxt_t reqc);
static int pid_file_handler(ldmsd_req_ctxt_t reqc);
static int banner_mode_handler(ldmsd_req_ctxt_t reqc);

/* Sampler Advertisement */
static int prdcr_listen_add_handler(ldmsd_req_ctxt_t reqc);
static int prdcr_listen_del_handler(ldmsd_req_ctxt_t reqc);
static int prdcr_listen_start_handler(ldmsd_req_ctxt_t reqc);
static int prdcr_listen_stop_handler(ldmsd_req_ctxt_t reqc);
static int prdcr_listen_status_handler(ldmsd_req_ctxt_t reqc);
static int advertiser_add_handler(ldmsd_req_ctxt_t reqc);
static int advertiser_start_handler(ldmsd_req_ctxt_t reqc);
static int advertiser_stop_handler(ldmsd_req_ctxt_t reqc);
static int advertiser_del_handler(ldmsd_req_ctxt_t reqc);
static int advertise_handler(ldmsd_req_ctxt_t reqc);

/* Quota Group (qgroup) */
static int qgroup_config_handler(ldmsd_req_ctxt_t reqc);
static int qgroup_member_add_handler(ldmsd_req_ctxt_t reqc);
static int qgroup_member_del_handler(ldmsd_req_ctxt_t reqc);
static int qgroup_start_handler(ldmsd_req_ctxt_t reqc);
static int qgroup_stop_handler(ldmsd_req_ctxt_t reqc);
static int qgroup_info_handler(ldmsd_req_ctxt_t reqc);

/* executable for all */
#define XALL 0111
/* executable for user, and group */
#define XUG 0110
/* flag if config request modifies existing configuration */
#define MOD 010000

static struct request_handler_entry request_handler[] = {
	[LDMSD_EXAMPLE_REQ] = { LDMSD_EXAMPLE_REQ, example_handler, XALL },
	[LDMSD_CFG_CNTR_REQ] = { LDMSD_CFG_CNTR_REQ, ldmsd_cfg_cntr_handler, XUG },
	[LDMSD_DUMP_CFG_REQ] = { LDMSD_DUMP_CFG_REQ, dump_cfg_handler, XUG },

	/* PRDCR */
	[LDMSD_PRDCR_ADD_REQ] = {
		LDMSD_PRDCR_ADD_REQ, prdcr_add_handler, XUG | MOD
	},
	[LDMSD_PRDCR_DEL_REQ] = {
		LDMSD_PRDCR_DEL_REQ, prdcr_del_handler, XUG | MOD
	},
	[LDMSD_PRDCR_START_REQ] = {
		LDMSD_PRDCR_START_REQ, prdcr_start_handler, XUG | MOD
	},
	[LDMSD_PRDCR_STOP_REQ] = {
		LDMSD_PRDCR_STOP_REQ, prdcr_stop_handler, XUG | MOD
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
		LDMSD_PRDCR_START_REGEX_REQ, prdcr_start_regex_handler, XUG | MOD
	},
	[LDMSD_PRDCR_STOP_REGEX_REQ] = {
		LDMSD_PRDCR_STOP_REGEX_REQ, prdcr_stop_regex_handler, XUG | MOD
	},
	[LDMSD_PRDCR_HINT_TREE_REQ] = {
		LDMSD_PRDCR_HINT_TREE_REQ, prdcr_hint_tree_status_handler,
		XALL | LDMSD_PERM_FAILOVER_ALLOWED
	},
	[LDMSD_PRDCR_SUBSCRIBE_REQ] = {
		LDMSD_PRDCR_SUBSCRIBE_REQ, prdcr_subscribe_regex_handler,
		XUG | LDMSD_PERM_FAILOVER_ALLOWED | MOD
	},
	[LDMSD_PRDCR_UNSUBSCRIBE_REQ] = {
		LDMSD_PRDCR_UNSUBSCRIBE_REQ, prdcr_unsubscribe_regex_handler,
		XUG | LDMSD_PERM_FAILOVER_ALLOWED | MOD
	},
	[LDMSD_PRDCR_STREAM_STATUS_REQ] = {
		LDMSD_PRDCR_STREAM_STATUS_REQ, prdcr_stream_status_handler,
		XUG | LDMSD_PERM_FAILOVER_ALLOWED
	},
	[LDMSD_BRIDGE_ADD_REQ] = {
		LDMSD_BRIDGE_ADD_REQ, prdcr_add_handler, XUG | MOD
	},

	/* STRGP */
	[LDMSD_STRGP_ADD_REQ] = {
		LDMSD_STRGP_ADD_REQ, strgp_add_handler, XUG | MOD
	},
	[LDMSD_STRGP_DEL_REQ]  = {
		LDMSD_STRGP_DEL_REQ, strgp_del_handler, XUG | MOD
	},
	[LDMSD_STRGP_PRDCR_ADD_REQ] = {
		LDMSD_STRGP_PRDCR_ADD_REQ, strgp_prdcr_add_handler, XUG | MOD
	},
	[LDMSD_STRGP_PRDCR_DEL_REQ] = {
		LDMSD_STRGP_PRDCR_DEL_REQ, strgp_prdcr_del_handler, XUG | MOD
	},
	[LDMSD_STRGP_METRIC_ADD_REQ] = {
		LDMSD_STRGP_METRIC_ADD_REQ, strgp_metric_add_handler, XUG | MOD
	},
	[LDMSD_STRGP_METRIC_DEL_REQ] = {
		LDMSD_STRGP_METRIC_DEL_REQ, strgp_metric_del_handler, XUG | MOD
	},
	[LDMSD_STRGP_START_REQ] = {
		LDMSD_STRGP_START_REQ, strgp_start_handler, XUG | MOD
	},
	[LDMSD_STRGP_STOP_REQ] = {
		LDMSD_STRGP_STOP_REQ, strgp_stop_handler, XUG | MOD
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
		LDMSD_UPDTR_ADD_REQ, updtr_add_handler, XUG | MOD
	},
	[LDMSD_UPDTR_DEL_REQ] = {
		LDMSD_UPDTR_DEL_REQ, updtr_del_handler, XUG | MOD
	},
	[LDMSD_UPDTR_PRDCR_ADD_REQ] = {
		LDMSD_UPDTR_PRDCR_ADD_REQ, updtr_prdcr_add_handler, XUG | MOD
	},
	[LDMSD_UPDTR_PRDCR_DEL_REQ] = {
		LDMSD_UPDTR_PRDCR_DEL_REQ, updtr_prdcr_del_handler, XUG | MOD
	},
	[LDMSD_UPDTR_START_REQ] = {
		LDMSD_UPDTR_START_REQ, updtr_start_handler, XUG | MOD
	},
	[LDMSD_UPDTR_STOP_REQ] = {
		LDMSD_UPDTR_STOP_REQ, updtr_stop_handler, XUG | MOD
	},
	[LDMSD_UPDTR_MATCH_ADD_REQ] = {
		LDMSD_UPDTR_MATCH_ADD_REQ, updtr_match_add_handler, XUG | MOD
	},
	[LDMSD_UPDTR_MATCH_DEL_REQ] = {
		LDMSD_UPDTR_MATCH_DEL_REQ, updtr_match_del_handler, XUG | MOD
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
		LDMSD_PLUGN_START_REQ, plugn_start_handler, XUG | MOD
	},
	[LDMSD_PLUGN_STOP_REQ] = {
		LDMSD_PLUGN_STOP_REQ, plugn_stop_handler, XUG | MOD
	},
	[LDMSD_PLUGN_STATUS_REQ] = {
		LDMSD_PLUGN_STATUS_REQ, plugn_status_handler,
		XALL | LDMSD_PERM_FAILOVER_ALLOWED
	},
	[LDMSD_PLUGN_LOAD_REQ] = {
		LDMSD_PLUGN_LOAD_REQ, plugn_load_handler, XUG | MOD
	},
	[LDMSD_PLUGN_TERM_REQ] = {
		LDMSD_PLUGN_TERM_REQ, plugn_term_handler, XUG | MOD
	},
	[LDMSD_PLUGN_CONFIG_REQ] = {
		LDMSD_PLUGN_CONFIG_REQ, plugn_config_handler, XUG | MOD
	},
	[LDMSD_PLUGN_USAGE_REQ] = {
		LDMSD_PLUGN_USAGE_REQ, plugn_usage_handler, XALL
	},
	[LDMSD_PLUGN_SETS_REQ] = {
		LDMSD_PLUGN_SETS_REQ, plugn_sets_handler, XALL
	},

	/* SET */
	[LDMSD_SET_UDATA_REQ] = {
		LDMSD_SET_UDATA_REQ, set_udata_handler, XUG | MOD
	},
	[LDMSD_SET_UDATA_REGEX_REQ] = {
		LDMSD_SET_UDATA_REGEX_REQ, set_udata_regex_handler, XUG | MOD
	},
	[LDMSD_SET_SEC_MOD_REQ] = {
		LDMSD_SET_SEC_MOD_REQ, set_sec_mod_handler, XUG | MOD
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
		LDMSD_ENV_REQ, env_handler, XUG | MOD
	},
	[LDMSD_INCLUDE_REQ] = {
		LDMSD_INCLUDE_REQ, include_handler, XUG | MOD
	},
	[LDMSD_ONESHOT_REQ] = {
		LDMSD_ONESHOT_REQ, oneshot_handler, XUG | MOD
	},
	[LDMSD_LOGROTATE_REQ] = {
		LDMSD_LOGROTATE_REQ, logrotate_handler, XUG | MOD
	},
	[LDMSD_EXIT_DAEMON_REQ] = {
		LDMSD_EXIT_DAEMON_REQ, exit_daemon_handler, XUG | MOD
	},
	[LDMSD_GREETING_REQ] = {
		LDMSD_GREETING_REQ, greeting_handler, XUG
	},
	[LDMSD_LOG_STATUS_REQ] = {
		LDMSD_LOG_STATUS_REQ, log_status_handler, XUG
	},
	[LDMSD_STATS_RESET_REQ] = {
		LDMSD_STATS_RESET_REQ, stats_reset_handler, XALL
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
		LDMSD_SET_DEFAULT_AUTHZ_REQ, set_default_authz_handler, XUG | MOD
	},

	[LDMSD_PROFILING_REQ] = {
		LDMSD_PROFILING_REQ, profiling_handler, XALL
	},

	/* FAILOVER user commands */
	[LDMSD_FAILOVER_CONFIG_REQ] = {
		LDMSD_FAILOVER_CONFIG_REQ, failover_config_handler, XUG | MOD,
	},
	[LDMSD_FAILOVER_PEERCFG_STOP_REQ]  = {
		LDMSD_FAILOVER_PEERCFG_STOP_REQ,
		failover_peercfg_stop_handler,
		XUG | LDMSD_PERM_FAILOVER_ALLOWED | MOD,
	},
	[LDMSD_FAILOVER_PEERCFG_START_REQ]  = {
		LDMSD_FAILOVER_PEERCFG_START_REQ,
		failover_peercfg_start_handler,
		XUG | LDMSD_PERM_FAILOVER_ALLOWED | MOD,
	},
	[LDMSD_FAILOVER_STATUS_REQ]  = {
		LDMSD_FAILOVER_STATUS_REQ, failover_status_handler,
		XUG | LDMSD_PERM_FAILOVER_ALLOWED,
	},
	[LDMSD_FAILOVER_START_REQ] = {
		LDMSD_FAILOVER_START_REQ, failover_start_handler, XUG | MOD,
	},
	[LDMSD_FAILOVER_STOP_REQ] = {
		LDMSD_FAILOVER_STOP_REQ, failover_stop_handler,
		XUG | LDMSD_PERM_FAILOVER_ALLOWED | MOD,
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
		LDMSD_SETGROUP_ADD_REQ, setgroup_add_handler, XUG | MOD,
	},
	[LDMSD_SETGROUP_MOD_REQ] = {
		LDMSD_SETGROUP_MOD_REQ, setgroup_mod_handler, XUG | MOD,
	},
	[LDMSD_SETGROUP_DEL_REQ] = {
		LDMSD_SETGROUP_DEL_REQ, setgroup_del_handler, XUG | MOD,
	},
	[LDMSD_SETGROUP_INS_REQ] = {
		LDMSD_SETGROUP_INS_REQ, setgroup_ins_handler, XUG | MOD,
	},
	[LDMSD_SETGROUP_RM_REQ] = {
		LDMSD_SETGROUP_RM_REQ, setgroup_rm_handler, XUG | MOD,
	},

	/* STREAM */
	[LDMSD_STREAM_PUBLISH_REQ] = {
		LDMSD_STREAM_PUBLISH_REQ, stream_publish_handler, XALL
	},
	[LDMSD_STREAM_SUBSCRIBE_REQ] = {
		LDMSD_STREAM_SUBSCRIBE_REQ, stream_subscribe_handler, XUG | MOD
	},
	[LDMSD_STREAM_UNSUBSCRIBE_REQ] = {
		LDMSD_STREAM_UNSUBSCRIBE_REQ, stream_unsubscribe_handler, XUG | MOD
	},
	[LDMSD_STREAM_CLIENT_DUMP_REQ] = {
		LDMSD_STREAM_CLIENT_DUMP_REQ, stream_client_dump_handler, XUG
	},
	[LDMSD_STREAM_NEW_REQ] = {
		LDMSD_STREAM_NEW_REQ, stream_new_handler, XUG | MOD
	},
	[LDMSD_STREAM_STATUS_REQ] = {
		LDMSD_STREAM_STATUS_REQ, stream_status_handler, XALL
	},
	[LDMSD_STREAM_DISABLE_REQ] = {
		LDMSD_STREAM_DISABLE_REQ, stream_disable_handler, XUG | MOD
	},

	/* MSG */
	[LDMSD_MSG_STATS_REQ] = {
		LDMSD_MSG_STATS_REQ, msg_stats_handler, XALL
	},
	[LDMSD_MSG_CLIENT_STATS_REQ] = {
		LDMSD_MSG_CLIENT_STATS_REQ, msg_client_stats_handler, XALL
	},
	[LDMSD_MSG_DISABLE_REQ] = {
		LDMSD_MSG_DISABLE_REQ, msg_disable_handler, XUG | MOD
	},

	/* LISTEN */
	[LDMSD_LISTEN_REQ] = {
		LDMSD_LISTEN_REQ, listen_handler, XUG | MOD,
	},

	/* AUTH */
	[LDMSD_AUTH_ADD_REQ] = {
		LDMSD_AUTH_ADD_REQ, auth_add_handler, XUG | MOD
	},
	[LDMSD_AUTH_DEL_REQ] = {
		LDMSD_AUTH_DEL_REQ, auth_del_handler, XUG
	},

	/* CMD-LINE options */
	[LDMSD_CMDLINE_OPTIONS_SET_REQ] = {
		LDMSD_CMDLINE_OPTIONS_SET_REQ, cmd_line_arg_set_handler, XUG
	},
	[LDMSD_DEFAULT_AUTH_REQ] = {
		LDMSD_DEFAULT_AUTH_REQ, default_auth_handler, XUG
	},
	[LDMSD_MEMORY_REQ] = {
		LDMSD_MEMORY_REQ, set_memory_handler, XUG
	},
	[LDMSD_LOG_FILE_REQ] = {
		LDMSD_LOG_FILE_REQ, log_file_handler, XUG
	},
	[LDMSD_PUBLISH_KERNEL_REQ] = {
		LDMSD_PUBLISH_KERNEL_REQ, publish_kernel_handler, XUG
	},
	[LDMSD_DAEMON_NAME_SET_REQ] = {
		LDMSD_DAEMON_NAME_SET_REQ, daemon_name_set_handler, XUG
	},
	[LDMSD_WORKER_THR_SET_REQ] = {
		LDMSD_WORKER_THR_SET_REQ, worker_threads_set_handler, XUG
	},
	[LDMSD_DEFAULT_QUOTA_REQ] = {
		LDMSD_DEFAULT_QUOTA_REQ, default_quota_set_handler, XUG
	},
	[LDMSD_PID_FILE_REQ] = {
		LDMSD_PID_FILE_REQ, pid_file_handler, XUG
	},
	[LDMSD_BANNER_MODE_REQ] = {
		LDMSD_BANNER_MODE_REQ, banner_mode_handler, XUG
	},

	/* Sampler Discovery */
	[LDMSD_ADVERTISER_ADD_REQ] = {
		LDMSD_ADVERTISER_ADD_REQ, advertiser_add_handler, XUG
	},
	[LDMSD_ADVERTISER_START_REQ] = {
		LDMSD_ADVERTISER_START_REQ, advertiser_start_handler, XUG
	},
	[LDMSD_ADVERTISER_STOP_REQ] = {
		LDMSD_ADVERTISER_STOP_REQ, advertiser_stop_handler, XUG
	},
	[LDMSD_ADVERTISER_DEL_REQ] = {
		LDMSD_ADVERTISER_DEL_REQ, advertiser_del_handler, XUG
	},
	[LDMSD_PRDCR_LISTEN_ADD_REQ] = {
		LDMSD_PRDCR_LISTEN_ADD_REQ, prdcr_listen_add_handler, XUG
	},
	[LDMSD_PRDCR_LISTEN_DEL_REQ] = {
		LDMSD_PRDCR_LISTEN_DEL_REQ, prdcr_listen_del_handler, XUG
	},
	[LDMSD_PRDCR_LISTEN_START_REQ] = {
		LDMSD_PRDCR_LISTEN_START_REQ, prdcr_listen_start_handler, XUG | MOD
	},
	[LDMSD_PRDCR_LISTEN_STOP_REQ] = {
		LDMSD_PRDCR_LISTEN_STOP_REQ, prdcr_listen_stop_handler, XUG | MOD
	},
	[LDMSD_PRDCR_LISTEN_STATUS_REQ] = {
		LDMSD_PRDCR_LISTEN_STATUS_REQ, prdcr_listen_status_handler, XALL
	},
	[LDMSD_ADVERTISE_REQ] = {
		LDMSD_ADVERTISE_REQ, advertise_handler, XUG
	},

	/* Quota Group (qgroup) */
	[LDMSD_QGROUP_CONFIG_REQ] = {
		LDMSD_QGROUP_CONFIG_REQ, qgroup_config_handler, XUG
	},
	[LDMSD_QGROUP_MEMBER_ADD_REQ] = {
		LDMSD_QGROUP_MEMBER_ADD_REQ, qgroup_member_add_handler, XUG
	},
	[LDMSD_QGROUP_MEMBER_DEL_REQ] = {
		LDMSD_QGROUP_MEMBER_DEL_REQ, qgroup_member_del_handler, XUG
	},
	[LDMSD_QGROUP_START_REQ] = {
		LDMSD_QGROUP_START_REQ, qgroup_start_handler, XUG
	},
	[LDMSD_QGROUP_STOP_REQ] = {
		LDMSD_QGROUP_STOP_REQ, qgroup_stop_handler, XUG
	},
	[LDMSD_QGROUP_INFO_REQ] = {
		LDMSD_QGROUP_INFO_REQ, qgroup_info_handler, XUG
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
	case LDMSD_DEFAULT_AUTH_REQ:
	case LDMSD_MEMORY_REQ:
	case LDMSD_LOG_FILE_REQ:
	case LDMSD_PUBLISH_KERNEL_REQ:
	case LDMSD_DAEMON_NAME_SET_REQ:
	case LDMSD_WORKER_THR_SET_REQ:
	case LDMSD_DEFAULT_QUOTA_REQ:
	case LDMSD_PID_FILE_REQ:
	case LDMSD_BANNER_MODE_REQ:
	case LDMSD_STREAM_DISABLE_REQ:
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
	ldms_xprt_put(xprt->ldms.ldms, "cfg_xprt");
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

static int is_stream_request(int id)
{
	return (id >= LDMSD_STREAM_PUBLISH_REQ
		&& id <= LDMSD_STREAM_STATUS_REQ);
}

int ldmsd_handle_request(ldmsd_req_ctxt_t reqc)
{
	struct request_handler_entry *ent;
	ldmsd_req_hdr_t request = (ldmsd_req_hdr_t)reqc->req_buf;
	ldms_t ldms = NULL;
	uid_t luid;
	gid_t lgid;
	mode_t mask;

	if (!stream_enabled && is_stream_request(reqc->req_id)) {
		reqc->errcode = ENOSYS;
		(void)Snprintf(&reqc->line_buf, &reqc->line_len,
			       "The stream service is DISABLED on this system.");
		ldmsd_send_req_response(reqc, reqc->line_buf);
		return 0;
	}

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
		ovis_log(config_log, OVIS_LERROR, "No response handler "
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
		ovis_log(config_log, OVIS_LERROR, "Out of memory\n");
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
				ovis_log(config_log, OVIS_LERROR, "Out of memory\n");
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
/*
 * \param req_filter is a function that returns zero if we want to process the
 *                   request, and returns non-zero otherwise.
 */
int ldmsd_process_config_request(ldmsd_cfg_xprt_t xprt, ldmsd_req_hdr_t request,
				req_filter_fn req_filter, void *filter_ctxt)
{
	struct req_ctxt_key key;
	ldmsd_req_ctxt_t reqc = NULL;
	struct request_handler_entry *ent;
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
			ovis_log(config_log, OVIS_LERROR, "The message no %" PRIu32 ":%" PRIu64
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
		ovis_log(config_log, OVIS_LERROR, "%s\n", errstr);
		ldmsd_send_error_reply(xprt, key.msg_no, rc, errstr, strlen(errstr)+1);
		goto err_out;
	}

	/* Convert the request byte order from network to host */
	ldmsd_ntoh_req_msg((ldmsd_req_hdr_t)reqc->req_buf);
	reqc->req_id = ((ldmsd_req_hdr_t)reqc->req_buf)->req_id;

	if (req_filter) {
		rc = req_filter(reqc, filter_ctxt);
		/* rc = 0, filter OK */
		if (rc == 0) {
			__dlog(DLOG_CFGOK, "# deferring line %d (%s)\n",
				reqc->key.msg_no, reqc->xprt->file.path);
			goto put_reqc;
		}
		/* rc == errno */
		if (rc > 0) {
			goto put_reqc;
		} else {
			/* rc < 0, filter not applied */
			rc = 0;
		}
	}

	rc = ldmsd_handle_request(reqc);

	if (!rc && !reqc->errcode) {
		ent = &request_handler[reqc->req_id];
		if (ent->flag & MOD)
			ldmsd_inc_cfg_cntr();
	}

put_reqc:
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
	ovis_log(config_log, OVIS_LCRITICAL, "%s\n", oom_errstr);
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
		ovis_log(config_log, OVIS_LERROR,
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
		ovis_log(config_log, OVIS_LERROR, "%s\n", errstr);
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
		ovis_log(config_log, OVIS_LERROR, "%s\n", errstr);
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

ldmsd_prdcr_t __prdcr_add_handler(ldmsd_req_ctxt_t reqc, char *verb, char *obj_name)
{
	ldmsd_prdcr_t prdcr = NULL;
	char *name, *host, *xprt, *attr_name, *type_s, *port_s, *interval_s,
	     *rail_s, *quota_s, *rx_rate_s;
	char *auth;
	enum ldmsd_prdcr_type type = -1;
	unsigned short port_no = 0;
	long interval_us = -1;
	size_t cnt;
	uid_t uid;
	gid_t gid;
	int perm;
	int64_t quota = ldmsd_quota; /* use the global quota setting by default */
	int64_t rx_rate = LDMS_UNLIMITED;
	int rail = 1;
	char *perm_s, *cache_ip_s;
	int cache_ip = 1; /* Default is 1. */
	perm_s = cache_ip_s = NULL;

	name = host = xprt = type_s = port_s = interval_s = auth = rail_s = quota_s = NULL;

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
			reqc->errcode = EINVAL;
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				       "The attribute type '%s' is invalid.",
				       type_s);
			goto out;
		}
		if (type == LDMSD_PRDCR_TYPE_LOCAL) {
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				       "%s with type 'local' is "
				       "not supported.", obj_name);
			reqc->errcode = EINVAL;
			goto out;
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
	if (type != LDMSD_PRDCR_TYPE_PASSIVE) {
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
	} else {
		if (port_s) {
			cnt = snprintf(reqc->line_buf, reqc->line_len,
					"Ignore the given port %s because "
					"the type of %s %s is passive.",
					port_s, obj_name, name);
		}
		port_no = -1;
	}

	attr_name = "reconnect";
	interval_s = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_INTERVAL);
	if (!interval_s) {
		goto einval;
	} else {
		reqc->errcode = ovis_time_str2us(interval_s, &interval_us);
		if (reqc->errcode) {
			cnt = snprintf(reqc->line_buf, reqc->line_len,
					"The given 'reconnect' is invalid.");
			goto out;
		}
		if (interval_us <= 0) {
			reqc->errcode = EINVAL;
			cnt = snprintf(reqc->line_buf, reqc->line_len,
					"The reconnect interval must be a positive number.");
			goto out;
		}
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

	rail_s = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_RAIL);
	if (rail_s) {
		rail = atoi(rail_s);
		if (rail <= 0) {
			reqc->errcode = EINVAL;
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"'rail' attribute must be a positive integer, got '%s'", rail_s);
			goto out;
		}
	}

	quota_s = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_QUOTA);
	if (quota_s) {
		quota = atol(quota_s);
		if (quota <= -2) {
			reqc->errcode = EINVAL;
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"'quota' attribute must be greater than -2, got '%s'", quota_s);
			goto out;
		}
	}

	rx_rate_s = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_RX_RATE);
	if (rx_rate_s) {
		rx_rate = atol(rx_rate_s);
		if (quota <= -2) {
			reqc->errcode = EINVAL;
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"'rx_rate' attribute must be greater than -2, got '%s'", rx_rate_s);
			goto out;
		}
	}

	cache_ip_s = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_IP);
	if (cache_ip_s) {
		if (0 == strcasecmp(cache_ip_s, "false")) {
			cache_ip = 0;
		}
	}

	prdcr = ldmsd_prdcr_new_with_auth(name, xprt, host, port_no, type,
					  interval_us, auth, uid, gid, perm,
					  rail, quota, rx_rate, cache_ip);
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

	goto out;
ebadauth:
	reqc->errcode = ENOENT;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"Authentication name not found, check the auth_add configuration.");
	goto out;
enomem:
	reqc->errcode = ENOMEM;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"Memory allocation failed.");
	goto out;
eexist:
	reqc->errcode = EEXIST;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The prdcr %s already exists.", name);
	goto out;
eafnosupport:
	reqc->errcode = EAFNOSUPPORT;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"Error resolving hostname '%s'\n", host);
	goto out;
einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The attribute '%s' is required.", attr_name);
out:
	free(name);
	free(type_s);
	free(port_s);
	free(interval_s);
	free(host);
	free(xprt);
	free(perm_s);
	free(auth);
	free(rail_s);
	free(quota_s);
	return prdcr;
}

static int prdcr_add_handler(ldmsd_req_ctxt_t reqc)
{
	ldmsd_prdcr_t prdcr;
	prdcr = __prdcr_add_handler(reqc, "prdcr_add", "producer");
	if (prdcr) {
		__dlog(DLOG_CFGOK, "prdcr_add name=%s xprt=%s host=%s port=%u type=%s "
			"reconnect=%ld auth=%s uid=%d gid=%d perm=%o\n",
			prdcr->obj.name, prdcr->xprt_name, prdcr->host_name,
			prdcr->port_no, ldmsd_prdcr_type2str(prdcr->type),
			prdcr->conn_intrvl_us, prdcr->conn_auth_dom_name,
			(int)prdcr->obj.uid, (int)prdcr->obj.gid,
			(unsigned)prdcr->obj.perm);
	}

	ldmsd_send_req_response(reqc, reqc->line_buf);
	return 0;
}

static int __prdcr_del_handler(ldmsd_req_ctxt_t reqc, const char *cmd, const char *obj_type)
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
				"The attribute '%s' is required by %s.",
							attr_name, cmd);
		goto out;
	}

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);

	reqc->errcode = ldmsd_prdcr_del(name, &sctxt);
	switch (reqc->errcode) {
	case 0:
		__dlog(DLOG_CFGOK, "%s name=%s\n", cmd, name);
		break;
	case ENOENT:
		Snprintf(&reqc->line_buf, &reqc->line_len,
				"The %s specified does not exist.", obj_type);
		break;
	case EBUSY:
		Snprintf(&reqc->line_buf, &reqc->line_len,
				"The %s is in use.", obj_type);
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
out:
	free(name);
	return 0;
}

static int prdcr_del_handler(ldmsd_req_ctxt_t reqc)
{
	int rc = __prdcr_del_handler(reqc, "prdcr_del", "producer");
	ldmsd_send_req_response(reqc, reqc->line_buf);
	return rc;
}

static int __prdcr_start_handler(ldmsd_req_ctxt_t reqc, const char *cmd, const char *obj_type)
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
				"The attribute 'name' is required by %s.", cmd);
		goto out;
	}

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);
	interval_str = ldmsd_req_attr_str_value_get_by_id(reqc,
							LDMSD_ATTR_INTERVAL);
	reqc->errcode = ldmsd_prdcr_start(name, interval_str, &sctxt);
	switch (reqc->errcode) {
	case 0:
		/* do nothing */
		break;
	case EBUSY:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The %s is already running.", obj_type);
		break;
	case ENOENT:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The %s specified does not exist.", obj_type);
		break;
	case EACCES:
		Snprintf(&reqc->line_buf, &reqc->line_len,
				"Permission denied.");
		break;
	case EINVAL:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The 'reconnect' value (%s) is invalid.", interval_str);
		break;
	case -EINVAL:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The 'reconnect' interval must be a positive interval.");
		break;
	default:
		Snprintf(&reqc->line_buf, &reqc->line_len,
				"Error: %d %s",
				reqc->errcode, ovis_errno_abbvr(reqc->errcode));
	}

out:
	free(name);
	free(interval_str);
	return 0;
}

static int prdcr_start_handler(ldmsd_req_ctxt_t reqc)
{
	int rc = __prdcr_start_handler(reqc, "prdcr_start", "producer");
	if (CONFIG_PLAYBACK_ENABLED(DLOG_CFGOK)) {
		if (!rc && !reqc->errcode) {
			char *name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
			char *interval_us = ldmsd_req_attr_str_value_get_by_id(reqc,
								LDMSD_ATTR_INTERVAL);
			if (interval_us) {
				__dlog(DLOG_CFGOK, "prdcr_start name=%s reconnect=%s\n",
					name, interval_us);
				free(interval_us);
			} else {
				__dlog(DLOG_CFGOK, "prdcr_start name=%s\n", name);
			}
		}
	}
	ldmsd_send_req_response(reqc, reqc->line_buf);
	return 0;
}

static int __prdcr_stop_handler(ldmsd_req_ctxt_t reqc, const char *cmd, const char *obj_type)
{
	char *name = NULL;
	size_t cnt = 0;
	struct ldmsd_sec_ctxt sctxt;

	reqc->errcode = 0;

	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!name) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The attribute 'name' is required by %s.", cmd);
		goto out;
	}

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);

	reqc->errcode = ldmsd_prdcr_stop(name, &sctxt);
	switch (reqc->errcode) {
	case 0:
		__dlog(DLOG_CFGOK, "%s name=%s\n", cmd, name);
		break;
	case EBUSY:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The %s is already stopped.", obj_type);
		break;
	case ENOENT:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The %s specified does not exist.", obj_type);
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

out:
	free(name);
	return 0;
}

static int prdcr_stop_handler(ldmsd_req_ctxt_t reqc)
{
	int rc = __prdcr_stop_handler(reqc, "prdcr_stop", "producer");
	ldmsd_send_req_response(reqc, reqc->line_buf);
	return rc;
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
			prdcr_regex, interval_str ? " reconnect=" :"",
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
	char *msg = NULL;
	char *rx_rate_s = NULL;
	size_t cnt = 0;
	struct ldmsd_sec_ctxt sctxt;
	int64_t rx_rate = LDMS_UNLIMITED;

	reqc->errcode = 0;

	rx_rate_s = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_RX_RATE);
	if (rx_rate_s) {
		rx_rate = atol(rx_rate_s);
	}

	prdcr_regex = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_REGEX);
	if (!prdcr_regex) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The attribute 'regex' is required by prdcr_stop_regex.");
		goto send_reply;
	}

	msg = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_MSG_CHAN);
	stream_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_STREAM);

	if (!stream_name && !msg) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"One of the 'stream' or `msg` attributes is required by prdcr_subscribe_regex (can specify both).");
		goto send_reply;
	}

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);
	reqc->errcode = ldmsd_prdcr_subscribe_regex(prdcr_regex,
						    stream_name,
						    msg,
						    reqc->line_buf,
						    reqc->line_len, &sctxt, rx_rate);
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
	char *msg = NULL;
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
	msg = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_MSG_CHAN);
	if (!stream_name && !msg) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"One of the 'stream' or `msg` attributes is required by prdcr_unsubscribe_regex (can specify both).");
		goto send_reply;
	}

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);
	reqc->errcode = ldmsd_prdcr_unsubscribe_regex(prdcr_regex,
						      stream_name,
						      msg,
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

struct pstream_status_regex_ctxt {
	json_entity_t stream_dict;
	struct ldmsd_str_list prdcr_list;
	pthread_mutex_t lock;
};

struct prdcr_stream_status_ctxt {
	struct ldmsd_str_ent *pname;
	struct pstream_status_regex_ctxt *base;
};

static int __process_stream_status(struct prdcr_stream_status_ctxt *ctxt, char *data, size_t data_len)
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
		pthread_mutex_lock(&ctxt->base->lock);
		ss = json_value_find(ctxt->base->stream_dict, stream_name);
		if (!ss) {
			ss = json_entity_new(JSON_DICT_VALUE);
			json_attr_add(ctxt->base->stream_dict, stream_name, ss);
		}
		pthread_mutex_unlock(&ctxt->base->lock);
		assert(!json_value_find(ss, ctxt->pname->str)); /* Receive stream_dir from this producer twice */
		json_attr_rem(s, "publishers"); /* We need to know only the overall statistic on sampler. */
		p = json_entity_copy(s);
		if (!p) {
			rc = ENOMEM;
			goto free_json;
		}
		json_attr_add(ss, ctxt->pname->str, p);
	}
free_json:
	json_entity_free(d);
	return rc;
}

static int __on_stream_status_resp(ldmsd_req_cmd_t rcmd)
{
	int rc = 0;
	struct prdcr_stream_status_ctxt *ctxt = rcmd->ctxt;
	struct pstream_status_regex_ctxt *base = ctxt->base;

	ldmsd_req_hdr_t resp = (ldmsd_req_hdr_t)(rcmd->reqc->req_buf);
	ldmsd_req_attr_t attr = ldmsd_first_attr(resp);
	assert(attr->attr_id == LDMSD_ATTR_JSON);
	rc = __process_stream_status(ctxt, (char*)attr->attr_value, attr->attr_len);
	TAILQ_REMOVE(&base->prdcr_list, ctxt->pname, entry);
	free(ctxt->pname->str);
	free(ctxt->pname);
	free(ctxt);

	if (!TAILQ_EMPTY(&base->prdcr_list))
		goto out;

	/*
	 * We have received all responses.
	 * Send the response back to the client
	 */
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
	json_entity_free(base->stream_dict);
	free(base);
out:
	return rc;
}

static int __prdcr_stream_status(ldmsd_prdcr_t prdcr, ldmsd_req_ctxt_t oreqc,
				struct pstream_status_regex_ctxt *base,
				struct ldmsd_str_ent *pname)
{
	int rc;
	ldmsd_req_cmd_t rcmd;
	struct prdcr_stream_status_ctxt *ctxt;

	ctxt = malloc(sizeof(*ctxt));
	if (!ctxt)
		return ENOMEM;
	ctxt->pname = pname;
	ctxt->base = base;

	ldmsd_prdcr_lock(prdcr);
	if (prdcr->conn_state == LDMSD_PRDCR_STATE_CONNECTED) {
		/* issue stream subscribe request right away if connected */
		rcmd = ldmsd_req_cmd_new(prdcr->xprt, LDMSD_STREAM_STATUS_REQ,
					 oreqc, __on_stream_status_resp, ctxt);
		rc = errno;
		if (!rcmd)
			goto rcmd_err;

		rc = ldmsd_req_cmd_attr_term(rcmd);
		if (rc)
			goto rcmd_err;
	}
	ldmsd_prdcr_unlock(prdcr);
	return 0;

 rcmd_err:
	ldmsd_prdcr_unlock(prdcr);
	if (rcmd)
		ldmsd_req_cmd_free(rcmd);
	return rc;
}

int prdcr_stream_status_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	char *prdcr_regex;
	size_t cnt = 0;
	struct ldmsd_sec_ctxt sctxt;
	regex_t regex;
	ldmsd_prdcr_t prdcr;
	struct pstream_status_regex_ctxt *ctxt;
	struct ldmsd_str_ent *pname, *nxt_pname;

	prdcr_regex = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_REGEX);
	if (!prdcr_regex) {
		rc = EINVAL;
		ctxt = NULL;
		cnt = snprintf(reqc->line_buf, reqc->line_len,
				"The attribute 'regex' is required by prdcr_stop_regex.");
		goto send_resp_code;
	}
	ldmsd_req_ctxt_sec_get(reqc, &sctxt);

	ctxt = calloc(1, sizeof(*ctxt));
	if (!ctxt)
		return ENOMEM;
	TAILQ_INIT(&ctxt->prdcr_list);

	ctxt->stream_dict = json_entity_new(JSON_DICT_VALUE);
	if (!ctxt->stream_dict) {
		rc = ENOMEM;
		goto send_resp_code;
	}
	pthread_mutex_init(&ctxt->lock, NULL);

	rc = ldmsd_compile_regex(&regex, prdcr_regex, reqc->line_buf, reqc->line_len);
	if (rc)
		goto send_resp_code;

	ldmsd_cfg_lock(LDMSD_CFGOBJ_PRDCR);
	/* Count the producers matched the regex */
	for (prdcr = ldmsd_prdcr_first(); prdcr; prdcr = ldmsd_prdcr_next(prdcr)) {
		rc = regexec(&regex, prdcr->obj.name, 0, NULL, 0);
		if (rc)
			continue;
		if (prdcr->conn_state != LDMSD_PRDCR_STATE_CONNECTED)
			continue;
		pname = malloc(sizeof(*pname));
		if (!pname) {
			rc = ENOMEM;
			goto free_ctxt;
		}
		pname->str = strdup(prdcr->obj.name);
		if (!pname->str) {
			rc = ENOMEM;
			goto free_ctxt;
		}
		TAILQ_INSERT_TAIL(&ctxt->prdcr_list, pname, entry);
	}

	/* Forward the request to the connected producers */
	pname = TAILQ_FIRST(&ctxt->prdcr_list);
	prdcr = ldmsd_prdcr_first();
	while (pname && prdcr) {
		if (0 != strcmp(pname->str, prdcr->obj.name))
			goto next_prdcr;

		nxt_pname = TAILQ_NEXT(pname, entry);
		rc = __prdcr_stream_status(prdcr, reqc, ctxt, pname);
		if (rc) {
			/* Failed to forward the request.
			 * Remove the producer name from the list
			 */
			TAILQ_REMOVE(&ctxt->prdcr_list, pname, entry);
			free(pname->str);
			free(pname);
		}
		pname = nxt_pname;

next_prdcr:
		prdcr = ldmsd_prdcr_next(prdcr);
	}

	ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR);
	regfree(&regex);
	/* Don't reply now. LDMSD will reply when receiving the response from the producers. */
	if (TAILQ_EMPTY(&ctxt->prdcr_list)) {
		snprintf(reqc->line_buf, reqc->line_len, "No matched producers");
		reqc->errcode = ENOENT;
		rc = 0;
		goto free_ctxt;
	}
	return 0;

send_resp_code:
	reqc->errcode = rc;
free_ctxt:
	free(ctxt);
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(prdcr_regex);
	return rc;
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
			"\"auth\":\"%s\","
			"\"reconnect_us\":\"%ld\","
			"\"state\":\"%s\","
			"\"sets\": [",
			prdcr->obj.name, ldmsd_prdcr_type2str(prdcr->type),
			prdcr->host_name, prdcr->port_no, prdcr->xprt_name,
			prdcr->conn_auth_dom_name,
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
			free(name);
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
	ldmsd_prdcr_put(prdcr, "find");
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
		goto out;
	if (prdcr_name) {
		prdcr = ldmsd_prdcr_find(prdcr_name);
		if (!prdcr)
			goto close_str;
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
				goto close_str;
			}
		}
		ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR);
	}
close_str:
	rc = linebuf_printf(reqc, "]");
out:
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

static int strgp_add_handler(ldmsd_req_ctxt_t reqc)
{
	char *attr_name, *name, *plugin, *container, *schema, *interval, *regex;
	char *decomp = NULL;
	name = plugin = container = schema = interval = regex = NULL;
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


	ldmsd_cfgobj_store_t store = ldmsd_store_find_get(plugin);
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

	ldmsd_store_get(store, "strgp");
	strgp->store = store; /* cfgobj ref is released in strgp_del */
	ldmsd_cfgobj_lock(&store->cfg);
	LIST_INSERT_HEAD(&store->strgp_list, strgp, store_entry);
	ldmsd_cfgobj_unlock(&store->cfg);

	char regex_err[512] = "";
	if (regex) {
		strgp->regex_s = strdup(regex);
		if (!strgp->regex_s)
			goto enomem;
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
		strgp->decomp_path = strdup(decomp);
		if (!strgp->decomp_path)
			goto enomem;
		/* reqc->errcode, reqc->line_buf will be populated if
		 * there is an error. Protected by strgp lock */
		rc = ldmsd_decomp_config(strgp, strgp->decomp_path, reqc);
		if (rc)
			goto send_reply;
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
	free(regex);
	free(perm_s);
	free(interval);
	free(decomp);
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
		       "\"regex\":\"%s\","
		       "\"plugin\":\"%s\","
		       "\"flush\":\"%ld.%06ld\","
		       "\"state\":\"%s\","
		       "\"decomp\":\"%s\","
		       "\"producers\":[",
		       strgp->obj.name,
		       strgp->container,
		       ((strgp->schema)?strgp->schema:"-"),
		       ((strgp->regex_s)?strgp->regex_s:"-"),
		       strgp->store->plugin->name,
		       strgp->flush_interval.tv_sec,
		       (strgp->flush_interval.tv_nsec/1000),
		       ldmsd_strgp_state_str(strgp->state),
		       ((strgp->decomp_path)?strgp->decomp_path : "-"));
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
			free(name);
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
	ldmsd_strgp_put(strgp, "find");
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
		reqc->errcode = ovis_time_str2us(interval_str, &interval);
		if (reqc->errcode) {
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The given update interval value (%s) is invalid.",
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
		reqc->errcode = ovis_time_str2us(offset_str, &offset);
		if (reqc->errcode) {
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
					"The given update offset value (%s) "
					"is invalid.", offset_str);
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
		"\"match\":[",
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
		rc = linebuf_printf(reqc, "{\"regex\":\"%s\","
					  "\"selector\":\"%s\"}",
					  cur_set->regex_str,
					  (cur_set->selector == LDMSD_NAME_MATCH_INST_NAME)?"inst":"schema");
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
			free(name);
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
		ldmsd_updtr_put(updtr, "find");
	return rc;
}

static int updtr_start_handler(ldmsd_req_ctxt_t reqc)
{
	char *updtr_name, *interval_str, *offset_str, *auto_interval;
	updtr_name = interval_str = offset_str = auto_interval = NULL;
	size_t cnt = 0;
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
		reqc->errcode = ovis_time_str2us(offset_str, &offset);
		if (reqc->errcode) {
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
					"The given update offset value (%s) "
					"is invalid.", offset_str);
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
		reqc->errcode = ovis_time_str2us(interval_str, &interval);
		if (reqc->errcode) {
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The given update interval value (%s) is invalid.",
				interval_str);
			goto send_reply;
		} else {
			if (0 >= interval) {
				reqc->errcode = EINVAL;
				cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
						"The update interval must "
						"be a positive interval. (%ld)", interval);
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
	free(auto_interval);
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
						int updtr_cnt, int reset)
{
	int rc;
	ldmsd_prdcr_ref_t ref;
	ldmsd_prdcr_t prdcr;
	int prdcr_count;
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
	char *name, *reset_s;
	int updtr_cnt, reset;
	ldmsd_updtr_t updtr = NULL;
	reset = 0;
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
			free(name);
			return 0;
		}
	}

	reset_s = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_RESET);
	if (reset_s) {
		if (0 != strcasecmp(reset_s, "false"))
			reset = 1;
		free(reset_s);
	}

	/* Construct the json object of the updater(s) */
	if (updtr) {
		rc = __updtr_status_json_obj(reqc, updtr, 0, reset);
		if (rc)
			goto out;
	} else {
		updtr_cnt = 0;
		ldmsd_cfg_lock(LDMSD_CFGOBJ_UPDTR);
		for (updtr = ldmsd_updtr_first(); updtr;
				updtr = ldmsd_updtr_next(updtr)) {
			rc = __updtr_status_json_obj(reqc, updtr, updtr_cnt, reset);
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
	ldmsd_updtr_put(updtr, "find");
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
			reqc->errcode = ENOENT;
			cnt = snprintf(reqc->line_buf, reqc->line_len, "updtr '%s' not found", name);
			ldmsd_send_req_response(reqc, reqc->line_buf);
			free(name);
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
	reqc->errcode = rc;
	ldmsd_send_req_response(reqc, "Internal error.");
out:
	free(name);
	if (updtr)
		ldmsd_updtr_put(updtr, "find");
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
			free(name);
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
	reqc->errcode = rc;
	ldmsd_send_req_response(reqc, "Internal error");
out:
	free(name);
	ldmsd_prdcr_put(prdcr, "find");
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

static char *plugin_type_str(enum ldmsd_plugin_type type)
{
	static char *type_str[] = {
		[LDMSD_PLUGIN_OTHER] = "other",
		[LDMSD_PLUGIN_SAMPLER] = "sampler",
		[LDMSD_PLUGIN_STORE] = "store",
		[LDMSD_PLUGIN_AUTH] = "auth",
		[LDMSD_PLUGIN_DECOMP] = "decomp"

	};

	if (type <= sizeof(type_str) / sizeof(type_str[0]))
		return type_str[type];
	return "unknown";
}

extern int ldmsd_load_plugin(char *instance_name, char *plugin_name, char *errstr, size_t errlen);
extern int ldmsd_term_plugin(char *instance_name);
static int plugn_start_handler(ldmsd_req_ctxt_t reqc)
{
	char *instance_name = NULL;
	char *interval_us = NULL;
	char *offset = NULL;
	char *attr_name;
	char *exclusive_thread;
	size_t cnt = 0;

	attr_name = "name";
	instance_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!instance_name)
		goto einval;
	attr_name = "interval";
	interval_us = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_INTERVAL);
	if (!interval_us)
		goto einval;

	exclusive_thread = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_XTHREAD);

	offset = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_OFFSET);

	reqc->errcode = ldmsd_sampler_start(instance_name, interval_us, offset,
					    exclusive_thread);
	if (reqc->errcode == 0) {
		__dlog(DLOG_CFGOK, "start name=%s%s%s%s%s\n", instance_name,
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
				"Sampler instance '%s' not found.", instance_name);
	} else if (reqc->errcode == EBUSY) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Sampler instance '%s' is already running.", instance_name);
	} else if (reqc->errcode == EDOM) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The given 'offset' (%s) is invalid.", offset);
	} else if (reqc->errcode == -EDOM) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Sampler parameters interval and offset are "
				"incompatible.");
	} else {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Failed to start the sampler instance '%s'.", instance_name);
	}
	goto send_reply;

einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The attribute '%s' is required by start.", attr_name);
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(instance_name);
	free(interval_us);
	free(offset);
	return 0;
}

static int plugn_stop_handler(ldmsd_req_ctxt_t reqc)
{
	char *instance_name;
	char *attr_name;
	size_t cnt = 0;

	attr_name = "name";
	instance_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!instance_name)
		goto einval;

	reqc->errcode = ldmsd_sampler_stop(instance_name);
	if (reqc->errcode == 0) {
		__dlog(DLOG_CFGOK, "stop name=%s\n", instance_name);
		goto send_reply;
	} else if (reqc->errcode == ENOENT) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Sampler instance '%s' not found.", instance_name);
	} else if (reqc->errcode == EINVAL) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The plugin instance '%s' is not a sampler.",
				instance_name);
	} else if (reqc->errcode == -EBUSY) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The sampler instance '%s' is not running.", instance_name);
	} else {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Failed to stop sampler instance '%s'", instance_name);
	}
	goto send_reply;

einval:
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The attribute '%s' is required by stop.", attr_name);
	reqc->errcode = EINVAL;
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(instance_name);
	return 0;
}

int __plugn_status_json_obj(ldmsd_req_ctxt_t reqc)
{
	int rc, count;
	ldmsd_cfgobj_sampler_t samp;
	ldmsd_cfgobj_store_t store;
	reqc->errcode = 0;

	rc = linebuf_printf(reqc, "[");
	if (rc)
		return rc;
	count = 0;
	ldmsd_cfg_lock(LDMSD_CFGOBJ_SAMPLER);
	for (samp = ldmsd_sampler_first(); samp; samp = ldmsd_sampler_next(samp)) {
		if (count) {
			rc = linebuf_printf(reqc, ",\n");
			if (rc) {
				ldmsd_cfg_unlock(LDMSD_CFGOBJ_SAMPLER);
				goto err;
			}
		}
		count++;
		rc = linebuf_printf(reqc,
			       "{\"name\":\"%s\",\"plugin\":\"%s\",\"type\":\"%s\","
			       "\"libpath\":\"%s\"}",
			       samp->cfg.name,
			       samp->plugin->name,
			       plugin_type_str(samp->api->base.type),
			       samp->plugin->libpath);
		if (rc) {
			ldmsd_cfg_unlock(LDMSD_CFGOBJ_SAMPLER);
			goto err;
		}
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_SAMPLER);
	ldmsd_cfg_lock(LDMSD_CFGOBJ_STORE);
	for (store = ldmsd_store_first(); store; store = ldmsd_store_next(store)) {
		if (count) {
			rc = linebuf_printf(reqc, ",\n");
			if (rc) {
				ldmsd_cfg_unlock(LDMSD_CFGOBJ_STORE);
				goto err;
			}
		}
		count++;
		rc = linebuf_printf(reqc,
				    "{\"name\":\"%s\",\"plugin\":\"%s\",\"type\":\"%s\","
				    "\"libpath\":\"%s\"}",
				    store->cfg.name,
				    store->plugin->name,
				    plugin_type_str(store->api->base.type),
				    store->plugin->libpath);
		if (rc) {
			ldmsd_cfg_unlock(LDMSD_CFGOBJ_STORE);
			goto err;
		}
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_STORE);
	return linebuf_printf(reqc, "]");
err:
	return rc;
}

static int plugn_status_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	struct ldmsd_req_attr_s attr;

	__dlog(DLOG_QUERY, "plugn_status\n");
	rc = __plugn_status_json_obj(reqc);
	if (rc) {
		reqc->errcode = rc;
		reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
					"Error %d preparing plugin status.", rc);
		return rc;
	}

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
	char *inst = NULL;
	char *plugn = NULL;
	char *attr_name;
	size_t cnt = 0;

	attr_name = "name";
	inst = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!inst) {
		ovis_log(config_log, OVIS_LERROR,
			 "load plugin called without name= parameter");
		goto einval;
	}

	attr_name = "plugin";
	plugn = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_PLUGIN);
	if (!plugn)
		plugn = inst;
	reqc->errcode = ldmsd_load_plugin(inst, plugn,
					reqc->line_buf,
					reqc->line_len);
	if (reqc->errcode)
		cnt = strlen(reqc->line_buf) + 1;
	else
		__dlog(DLOG_CFGOK, "load name=%s plugin=%s\n", inst, plugn);
	goto send_reply;

einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The attribute '%s' is required by load.", attr_name);
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(inst);
	if (plugn != inst)
		free(plugn);
	return 0;
}

static int plugn_term_handler(ldmsd_req_ctxt_t reqc)
{
	char *instance_name = NULL;
	char *attr_name;
	size_t cnt = 0;

	attr_name = "name";
	instance_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!instance_name)
		goto einval;

	reqc->errcode = ldmsd_term_plugin(instance_name);
	if (reqc->errcode == 0) {
		__dlog(DLOG_CFGOK, "term name=%s\n", instance_name);
		goto send_reply;
	} else if (reqc->errcode == ENOENT) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"plugin instance '%s' not found.", instance_name);
	} else if (reqc->errcode == EINVAL) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The specified plugin instance '%s' has "
				"active users and cannot be terminated.",
				instance_name);
	} else {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Failed to terminate the plugin instance '%s'.",
				instance_name);
	}
	goto send_reply;

einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The attribute '%s' is required by term.", attr_name);
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(instance_name);
	return 0;
}

static int plugn_config_handler(ldmsd_req_ctxt_t reqc)
{
	char *instance_name;
	char *config_attr = NULL;
	char *attr_name;
	char *exclusive_thread;
	struct attr_value_list *av_list = NULL;
	struct attr_value_list *kw_list = NULL;
	ldmsd_cfgobj_t cfg;
	char *attr_copy = NULL;
	size_t cnt = 0;
	int multi_config;
	ldmsd_cfgobj_sampler_t sampler;
	ldmsd_cfgobj_store_t store;

	reqc->errcode = 0;

	attr_name = "name";
	instance_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!instance_name)
		goto einval;

	sampler = ldmsd_sampler_find_get(instance_name);
	if (!sampler) {
		store = ldmsd_store_find_get(instance_name);
		if (!store) {
			/* See if there is a */
			reqc->errcode = ENOENT;
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
					"The specified plugin instance '%s' does not exist.",
					instance_name);
			goto send_reply;
		}
		multi_config = store->api->base.flags & LDMSD_PLUGIN_MULTI_INSTANCE;
		if (!multi_config && store->configured) {
			reqc->errcode = EINVAL;
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				       "The store '%s' does not support multiple "
				       "configurations.\n",
				       instance_name);
			goto send_reply;
		}
		cfg = &store->cfg;
		ldmsd_store_find_put(store);
	} else {
		multi_config = sampler->api->base.flags & LDMSD_PLUGIN_MULTI_INSTANCE;
		if (!multi_config && sampler->configured) {
			reqc->errcode = EINVAL;
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				       "The sampler '%s' does not support multiple "
				       "configurations.\n",
				       instance_name);
			goto send_reply;
		}
		cfg = &sampler->cfg;
		ldmsd_sampler_find_put(sampler);
	}

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
		goto send_reply;
	}

	reqc->errcode = tokenize(config_attr, kw_list, av_list);
	if (reqc->errcode) {
		ovis_log(config_log, OVIS_LERROR, "Memory allocation failure "
				"processing '%s'\n", config_attr);
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Out of memory");
		reqc->errcode = ENOMEM;
		goto send_reply;
	}

	free(cfg->avl_str);
	free(cfg->kvl_str);
	cfg->avl_str = av_to_string(av_list, 0);
	cfg->kvl_str = (kw_list->count)?av_to_string(kw_list, 0):NULL;

	exclusive_thread = av_value(av_list, "exclusive_thread");
	if (exclusive_thread && sampler)
		sampler->use_xthread = atoi(exclusive_thread);

	if (sampler) {
		reqc->errcode = sampler->api->base.config((ldmsd_cfgobj_t)sampler, kw_list, av_list);
		if (!reqc->errcode) {
			sampler->configured = 1;
		}
	} else {
		reqc->errcode = store->api->base.config((ldmsd_cfgobj_t)store, kw_list, av_list);
		if (!reqc->errcode) {
			store->configured = 1;
		}
	}
	if (reqc->errcode) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Error %d configuring plugin instance '%s'.",
				reqc->errcode, instance_name);
	} else {
		__dlog(DLOG_CFGOK, "config name=%s %s\n", instance_name,
			attr_copy);
	}
	goto send_reply;

einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The attribute '%s' is required by config.",
			attr_name);
	goto send_reply;
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(instance_name);
	free(config_attr);
	av_free(kw_list);
	av_free(av_list);
	free(attr_copy);
	return 0;
}

static int __plugn_usage_string(ldmsd_req_ctxt_t reqc)
{
	char *name = NULL;
	int rc, count = 0;
	ldmsd_cfgobj_sampler_t samp;
	ldmsd_cfgobj_store_t store;
	rc = 0;

	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);

	ldmsd_cfg_lock(LDMSD_CFGOBJ_SAMPLER);
	for (samp = ldmsd_sampler_first(); samp;
			samp = ldmsd_sampler_next(samp)) {
		if (name && (0 != strcmp(name, samp->cfg.name)))
			continue;

		if (samp->api->base.usage) {
			rc = linebuf_printf(reqc, "%s\n%s",
					    samp->cfg.name, samp->api->base.usage(&samp->cfg));
		} else {
			rc = linebuf_printf(reqc, "%s\n", samp->cfg.name);
		}
		if (rc)
			goto out;
		count++;
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_SAMPLER);
	if (name && (0 == count)) {
		reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
				"Plugin '%s' not loaded.", name);
		reqc->errcode = ENOENT;
	}
	ldmsd_cfg_lock(LDMSD_CFGOBJ_STORE);
	for (store = ldmsd_store_first(); samp;
			store = ldmsd_store_next(store)) {
		if (name && (0 != strcmp(name, store->cfg.name)))
			continue;

		if (store->api->base.usage) {
			rc = linebuf_printf(reqc, "%s\n%s",
					store->cfg.name, store->api->base.usage(&store->cfg));
		} else {
			rc = linebuf_printf(reqc, "%s\n", store->cfg.name);
		}
		if (rc)
			goto out;
		count++;
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_STORE);
	if (name && (0 == count)) {
		reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
				"Plugin '%s' not loaded.", name);
		reqc->errcode = ENOENT;
	}
out:
	free(name);
	return rc;
}

static int plugn_usage_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	struct ldmsd_req_attr_s attr;

	__dlog(DLOG_QUERY, "usage\n");
	rc = __plugn_usage_string(reqc);
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
static int sampler_sets_json_obj(ldmsd_req_ctxt_t reqc, ldmsd_cfgobj_sampler_t samp)
{
	ldmsd_sampler_set_t set;
	int rc, set_count;
	rc = linebuf_printf(reqc,
			"{"
			"\"plugin\":\"%s\","
			"\"sets\":[",
			samp->cfg.name);
	if (rc)
		return rc;
	set_count = 0;
	LIST_FOREACH(set, &samp->set_list, entry) {
		if (set_count) {
			rc = linebuf_printf(reqc, ",");
			if (rc)
				return rc;
		}
		rc = linebuf_printf(reqc, "\"%s\"",
			ldms_set_instance_name_get(set->set));
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
	char *cfg_name;
	ldmsd_cfgobj_sampler_t samp = NULL;
	int comma = 0;

	cfg_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (cfg_name)
		samp = ldmsd_sampler_find_get(cfg_name);
	else
		samp = ldmsd_sampler_first();
	free(cfg_name);
	while (samp) {
		if (comma) {
			rc = linebuf_printf(reqc, ",");
			if (rc)
				goto err;
		}
		rc = sampler_sets_json_obj(reqc, samp);
		if (rc) {
			goto err;
		}
		comma = 1;
		if (cfg_name)
			break;
		samp = ldmsd_sampler_next(samp);
	}
	cnt = reqc->line_off + 2; /* +2 for '[' and ']'*/

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
	if (reqc->line_off) {
		rc = ldmsd_append_reply(reqc, reqc->line_buf, reqc->line_off, 0);
		if (rc)
			goto err;
	}
	rc = ldmsd_append_reply(reqc, "]", 1, 0);
	if (rc)
		goto err;
	attr.discrim = 0;
	return ldmsd_append_reply(reqc, (char *)&attr.discrim,
				sizeof(uint32_t), LDMSD_REQ_EOM_F);
err:
	reqc->errcode = rc;
	ldmsd_send_req_response(reqc, "Internal error.");
	return rc;
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

static int set_sec_mod_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	char *value, *regex_s, *endptr;
	regex_t regex;

	uid_t uid;
	gid_t gid;
	mode_t perm;
	int set_flags = 0;
	value = regex_s = NULL;
	uid = gid = perm = 0;

	regex_s = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_REGEX);
	if (!regex_s) {
		rc = EINVAL;
		reqc->errcode = EINVAL;
		(void) snprintf(reqc->line_buf, reqc->line_len,
				"'regex' is required.");
		goto out;
	}
	rc = regcomp(&regex, regex_s, REG_EXTENDED | REG_NOSUB);
	if (rc) {
		reqc->errcode = EINVAL;
		rc = snprintf(reqc->line_buf, reqc->line_len, "The regex string is invalid. ");
		rc++;
		(void) regerror(rc, &regex, &reqc->line_buf[rc], reqc->line_len - rc);
		goto out;
	}

	value = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_UID);
	if (value) {
		if (isdigit(value[0])) {
			uid = strtol(value, &endptr, 0);
			if (uid < 0) {
				reqc->errcode = EINVAL;
				(void) snprintf(reqc->line_buf, reqc->line_len,
						"The given UID '%s' is invalid.",
						value);
				free(value);
				goto free_regex;
			}
		} else {
			struct passwd *pwd = getpwnam(value);
			if (!pwd) {
				reqc->errcode = EINVAL;
				(void)snprintf(reqc->line_buf, reqc->line_len,
						"Unknown user '%s'", value);
				free(value);
				goto free_regex;
			}
			uid = pwd->pw_uid;
		}
		set_flags |= DEFAULT_AUTHZ_SET_UID;
		free(value);
	}

	value = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_GID);
	if (value) {
		if (isdigit(value[0])) {
			gid = strtol(value, &endptr, 0);
			if (gid < 0) {
				reqc->errcode = EINVAL;
				(void) snprintf(reqc->line_buf, reqc->line_len,
						"The given GID '%s' is invalid.",
						value);
				free(value);
				goto free_regex;
			}
		} else {
			struct group *grp = getgrnam(value);
			if (!grp) {
				reqc->errcode = EINVAL;
				(void) snprintf(reqc->line_buf, reqc->line_len,
						"Unknown group '%s'", value);
				free(value);
				goto free_regex;
			}
			gid = grp->gr_gid;
		}
		set_flags |= DEFAULT_AUTHZ_SET_GID;
		free(value);
	}

	value = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_PERM);
	if (value) {
		perm = strtoul(value, &endptr, 8);
		if (*endptr != '\0') {
			reqc->errcode = EINVAL;
			(void) snprintf(reqc->line_buf, reqc->line_len,
					"String to permission bits conversion failed.");
			free(value);
			goto free_regex;
		} else if (perm > 0777) {
			reqc->errcode = EINVAL;
			(void) snprintf(reqc->line_buf, reqc->line_len,
					"Permission value is out of range.");
			free(value);
			goto free_regex;
		}
		set_flags |= DEFAULT_AUTHZ_SET_PERM;
		free(value);
	}

	rc = ldms_set_regex_sec_set(regex, uid, gid, perm, set_flags);
	if (rc) {
		reqc->errcode = rc;
		(void) snprintf(reqc->line_buf, reqc->line_len,
				"Failed to set the security parameters.");
	}
free_regex:
	regfree(&regex);
out:
	free(regex_s);
	ldmsd_send_req_response(reqc, reqc->line_buf);
	return rc;
}

static int verbosity_change_handler(ldmsd_req_ctxt_t reqc)
{
	char *level_s = NULL;
	char *subsys = NULL;
	char *regex_s = NULL;
	size_t cnt = 0;
	int is_test = 0;
	int level;
	int rc;

	level_s = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_LEVEL);
	if (!level_s) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The attribute 'level' is required.");
		goto out;
	}
	subsys = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	regex_s = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_REGEX);

	if (0 == strcasecmp(level_s, "default") || (0 == strcasecmp(level_s, "reset"))) {
		level = OVIS_LDEFAULT;
	} else {
		level = ovis_log_str_to_level(level_s);
		if (level < 0) {
			reqc->errcode = EINVAL;
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
					"The given level %s is invalid.", level_s);
			goto out;
		}
	}

	if (regex_s) {
		rc = ovis_log_set_level_by_regex(regex_s, level);
		if (rc == EINVAL) {
			reqc->errcode = rc;
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
					"The regular expression '%s' is invalid.",
									regex_s);
		} else if (rc == ENOENT) {
			reqc->errcode = rc;
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
					"The regular expression doesn't match any logs.");
		}
	} else {
		rc = ovis_log_set_level_by_name(subsys, level);
		if (rc) {
			reqc->errcode = rc;
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
					"The given log %s does not exist.",
								subsys);
		}
	}

	if (rc)
		goto out;

	if (ldmsd_req_attr_keyword_exist_by_id(reqc->req_buf, LDMSD_ATTR_TEST))
		is_test = 1;

	__dlog(DLOG_CFGOK, "log_level level=%s%s%s%s%s%s\n", level_s,
		is_test ? " test" : "",
		subsys ? " name=" : "",
		subsys ? subsys : NULL,
		regex_s ? " regex=" : "",
		regex_s ? regex_s : "");
	if (is_test) {
		ovis_log(config_log, OVIS_LDEBUG, "TEST DEBUG\n");
		ovis_log(config_log, OVIS_LINFO, "TEST INFO\n");
		ovis_log(config_log, OVIS_LWARNING, "TEST WARNING\n");
		ovis_log(config_log, OVIS_LERROR, "TEST ERROR\n");
		ovis_log(config_log, OVIS_LCRITICAL, "TEST CRITICAL\n");
		ovis_log(config_log, OVIS_LALWAYS, "TEST ALWAYS\n");
	}

out:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(level_s);
	free(subsys);
	free(regex_s);
	return 0;
}

int log_status_handler(ldmsd_req_ctxt_t reqc)
{
	int rc = 0;
	char *subsys;
	char *result;
	size_t cnt;
	subsys = result = NULL;

	subsys = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	result = ovis_log_list(subsys);
	if (!result) {
		reqc->errcode = errno;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Failed to get the list of log information.");
		ldmsd_send_req_response(reqc, reqc->line_buf);
		goto out;
	}

	struct ldmsd_req_attr_s attr;
	attr.discrim = 1;
	attr.attr_len = strlen(result);
	attr.attr_id = LDMSD_ATTR_JSON;
	ldmsd_hton_req_attr(&attr);
	rc = ldmsd_append_reply(reqc, (char *)&attr, sizeof(attr), LDMSD_REQ_SOM_F);
	if (rc)
		goto out;
	rc = ldmsd_append_reply(reqc, result, strlen(result), 0);
	if (rc)
		goto out;
	/* send the terminating attribute */
	attr.discrim = 0;
	rc = ldmsd_append_reply(reqc, (char *)&attr.discrim, sizeof(attr.discrim), LDMSD_REQ_EOM_F);
out:
	free(subsys);
	free(result);
	return rc;
}

int __daemon_status_json_obj(ldmsd_req_ctxt_t reqc)
{
	int rc;
	char *thread_stats = NULL;
	char *json_s;
	size_t json_sz;

	rc = linebuf_printf(reqc, "{\"state\":\"ready\"");
	if (rc)
		return rc;
	thread_stats = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_STRING);
	if (!thread_stats) {
		rc = linebuf_printf(reqc, "}");
		return rc;
	}
	free(thread_stats);
	json_s = __thread_stats_as_json(&json_sz);
	if (!json_s)
		return ENOMEM;
	rc = linebuf_printf(reqc, ",\n\"thread_stats\":%s\n", json_s);
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
	ovis_log(config_log, OVIS_LINFO, "User requested exit.\n");
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
	size_t len;
	server_attr = ldmsd_first_attr((ldmsd_req_hdr_t)rcmd->reqc->req_buf);
	my_attr.discrim = 1;
	my_attr.attr_id = LDMSD_ATTR_STRING;
	/* +1 for : */
	len = my_attr.attr_len = server_attr->attr_len + strlen((char *)rcmd->ctxt) + 1;
	path = calloc(1, len);
	if (!path) {
		rcmd->org_reqc->errcode = ENOMEM;
		ldmsd_send_req_response(rcmd->org_reqc, "Out of memory");
		return 0;
	}
	ldmsd_hton_req_attr(&my_attr);
	ldmsd_append_reply(rcmd->org_reqc, (char *)&my_attr, sizeof(my_attr), LDMSD_REQ_SOM_F);
	snprintf(path, len, "%s:%s", server_attr->attr_value, (char*)rcmd->ctxt);
	ldmsd_append_reply(rcmd->org_reqc, path, len, 0);
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
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR);
	char *myself;
	myself = (char *)ldmsd_myname_get();
	if (!myself || (myself[0] == '\0'))
		myself = "ldmsd";
	if (!prdcr) {
		linebuf_printf(reqc, "%s", myself);
		attr.discrim = 1;
		attr.attr_id = LDMSD_ATTR_STRING;
		attr.attr_len = reqc->line_off + 1;
		ldmsd_hton_req_attr(&attr);
		ldmsd_append_reply(reqc, (char *)&attr, sizeof(attr), LDMSD_REQ_SOM_F);
		ldmsd_append_reply(reqc, reqc->line_buf, reqc->line_off + 1, 0);
		attr.discrim = 0;
		ldmsd_append_reply(reqc, (char *)&attr.discrim, sizeof(attr.discrim), LDMSD_REQ_EOM_F);
	} else {
		ldmsd_prdcr_lock(prdcr);
		char *ctxt = strdup(myself);
		if (!ctxt) {
			ovis_log(config_log, OVIS_LCRITICAL, "Out of memory\n");
			return ENOMEM;
		}
		rcmd = alloc_req_cmd_ctxt(prdcr->xprt, ldms_xprt_msg_max(prdcr->xprt),
						LDMSD_GREETING_REQ, reqc,
						__greeting_path_resp_handler, ctxt);
		ldmsd_prdcr_unlock(prdcr);
		if (!rcmd) {
			reqc->errcode = ENOMEM;
			ldmsd_send_req_response(reqc, "Out of Memory");
			free(ctxt);
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

static int ldmsd_cfg_cntr_handler(ldmsd_req_ctxt_t reqc)
{
	int ldmsd_cfg_cnt;
	ldmsd_cfg_cnt = ldmsd_cfg_cntr_get();
	__dlog(DLOG_QUERY, "cfg_cntr\n");

	snprintf(reqc->line_buf, reqc->line_len,
		"%d", ldmsd_cfg_cnt);
	ldmsd_send_req_response(reqc, reqc->line_buf);
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
		ovis_log(config_log, OVIS_LDEBUG, "strlen(name)=%zu. %s\n", strlen(str), str);
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
	free(str);
	return 0;
}

extern char *logfile;
extern int log_level_thr;
extern char *max_mem_sz_str;
extern char *pidfile;
extern int banner;
extern int do_kernel;
extern char *setfile;
extern int ev_thread_count;
static int dump_cfg_handler(ldmsd_req_ctxt_t reqc)
{
	FILE *fp = NULL;
	char *filename = NULL;
	int rc;
	int i;
	char hostname[128], port_no[32];
	rc = ldms_xprt_names(reqc->xprt->ldms.ldms, hostname, sizeof(hostname), port_no, sizeof(port_no),
				NULL, 0, NULL, 0, NI_NAMEREQD | NI_NUMERICSERV);
	reqc->errcode = 0;
	filename = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_PATH);
	if (!filename || strlen(filename) == 0) {
		Snprintf(&reqc->line_buf, &reqc->line_len,
				"Invalid path argument. Please specify valid directory path");
		goto err0;
	}
	char fullpath[sizeof(filename)+sizeof(hostname)+sizeof(port_no)];
	snprintf(fullpath, sizeof(fullpath), "%s/%s-%s.conf", filename, hostname, port_no);
	fp = fopen(fullpath, "w+");
	if (!fp) {
		Snprintf(&reqc->line_buf, &reqc->line_len,
				"Unable to write configuration file at path %s.", fullpath);
		goto err0;
	}
	fprintf(fp, "# This configuration file assumes ldmsd will be started with\n"
			"# no command line arguments.\n"
			"# e.g. ldmsd -c %s\n\n", fullpath);

	/* Miscellaneous, e.g., logfile, log verbosity */
	fprintf(fp, "option");
	if (logfile) {
		fprintf(fp, " -l %s", logfile);
	}
	fprintf(fp, " -v %s", ovis_log_level_to_str(log_level_thr));
	if (max_mem_sz_str)
		fprintf(fp, " -m %s", max_mem_sz_str);
	if (pidfile)
		fprintf(fp, " -r %s", pidfile);
	if (banner != -1)
		fprintf(fp, " -B %d", banner);
	if (do_kernel) {
		fprintf(fp, " -k");
		if (setfile)
			fprintf(fp, " -s %s", setfile);
	}
	fprintf(fp, "\n");

	/* Daemon name */
	const char *_name = ldmsd_myname_get();
	if (_name[0] != '\0') {
		fprintf(fp, "daemon_name name=%s\n", _name);
	}

	/* Worker threads */
	fprintf(fp, "worker_threads num=%d\n", ev_thread_count);

	/* Default credits */
	fprintf(fp, "default_credits credits=%d\n", ldmsd_quota);

	/* Auth */
	ldmsd_auth_t auth;
	ldmsd_cfg_lock(LDMSD_CFGOBJ_AUTH);
	for (auth = (ldmsd_auth_t)ldmsd_cfgobj_first(LDMSD_CFGOBJ_AUTH); auth;
			auth = (ldmsd_auth_t)ldmsd_cfgobj_next(&auth->obj)) {
		fprintf(fp, "auth_add name=%s plugin=%s", auth->obj.name, auth->plugin);
		if (auth->attrs) {
			for (i = 0; i < auth->attrs->count; i++) {
				struct attr_value *v = &auth->attrs->list[i];
				fprintf(fp, " %s=%s", v->name, v->value);
			}
		}
		fprintf(fp, "\n");
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_AUTH);
	/* Listeners */
	ldmsd_listen_t listen;
	ldmsd_cfg_lock(LDMSD_CFGOBJ_LISTEN);
	for (listen = (ldmsd_listen_t)ldmsd_cfgobj_first(LDMSD_CFGOBJ_LISTEN); listen;
			listen = (ldmsd_listen_t)ldmsd_cfgobj_next(&listen->obj)) {
		fprintf(fp, "listen xprt=%s port=%d",
			listen->xprt,
			listen->port_no);
		if (listen->host)
			fprintf(fp, " host=%s", listen->host);
		if (listen->auth_name) {
			if (listen->auth_dom_name)
				fprintf(fp, " auth=%s", listen->auth_dom_name);
			else
				fprintf(fp, " auth=DEFAULT");
		}
		fprintf(fp, "\n");
	}

	ldmsd_cfg_unlock(LDMSD_CFGOBJ_LISTEN);
	/* Producers */
	ldmsd_prdcr_t prdcr = NULL;

	ldmsd_cfg_lock(LDMSD_CFGOBJ_PRDCR);
	for (prdcr = ldmsd_prdcr_first(); prdcr;
			prdcr = ldmsd_prdcr_next(prdcr)) {
		ldmsd_prdcr_stream_t s;

		ldmsd_prdcr_lock(prdcr);
		fprintf(fp, "prdcr_add name=%s host=%s port=%d xprt=%s type=%s interval=%ld auth=%s uid=%d gid=%d\n",
			prdcr->obj.name, prdcr->host_name,
			prdcr->port_no, prdcr->xprt_name,
			ldmsd_prdcr_type2str(prdcr->type),
			prdcr->conn_intrvl_us,
			prdcr->conn_auth_dom_name,
			prdcr->obj.uid, prdcr->obj.gid);
		if (prdcr->conn_state == LDMSD_PRDCR_STATE_CONNECTED)
			fprintf(fp, "prdcr_start name=%s\n", prdcr->obj.name);
		/* Streams */
		LIST_FOREACH(s, &prdcr->stream_list, entry) {
			fprintf(fp, "prdcr_subscribe regex=^%s$ stream=%s\n", prdcr->obj.name, s->name);
		}
		ldmsd_prdcr_unlock(prdcr);
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR);
	/* Plugins */
	ldmsd_cfgobj_store_t store;
	ldmsd_cfg_lock(LDMSD_CFGOBJ_STORE);
	for (store = ldmsd_store_first(LDMSD_CFGOBJ_STORE); store;
			store = ldmsd_store_next(store)) {
		fprintf(fp, "load name=%s plugin=%s\n", store->cfg.name, store->plugin->name);
		fprintf(fp, "load name=%s plugin=%s\n", store->cfg.name, store->plugin->name);
		if (store->cfg.avl_str || store->cfg.kvl_str)
			fprintf(fp, "config name=%s %s %s\n",
				store->cfg.name,
				store->cfg.avl_str ? store->cfg.avl_str : "",
				store->cfg.kvl_str ? store->cfg.kvl_str : "");
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_STORE);
	ldmsd_cfg_lock(LDMSD_CFGOBJ_SAMPLER);
	ldmsd_cfgobj_sampler_t samp;
	for (samp = ldmsd_sampler_first(); samp;
			samp = ldmsd_sampler_next(samp)) {
		fprintf(fp, "load name=%s plugin=%s\n", samp->cfg.name, samp->plugin->name);
		fprintf(fp, "load name=%s plugin=%s\n", samp->cfg.name, samp->plugin->name);
		if (samp->cfg.avl_str || samp->cfg.kvl_str)
			fprintf(fp, "config name=%s %s %s\n",
				samp->cfg.name,
				samp->cfg.avl_str ? samp->cfg.avl_str : "",
				samp->cfg.kvl_str ? samp->cfg.kvl_str : "");
		if (samp->thread_id >= 0) {
			/* Plugin is running. */
			fprintf(fp, "start name=%s interval=%ld offset=%ld\n",
				samp->cfg.name,
				samp->sample_interval_us,
				samp->sample_offset_us);
		}
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_SAMPLER);
	/*  Updaters */
	ldmsd_name_match_t match;
	ldmsd_updtr_t updtr;
	char *sel_str;
	char *updtr_mode = NULL;

	ldmsd_cfg_lock(LDMSD_CFGOBJ_UPDTR);
	for (updtr = ldmsd_updtr_first(); updtr;
			updtr = ldmsd_updtr_next(updtr)) {
		ldmsd_updtr_lock(updtr);
		/* Initial updater configuration */
		fprintf(fp, "updtr_add name=%s", updtr->obj.name);
		if (updtr->is_auto_task)
			updtr_mode = "auto_interval=true";
		else if (updtr->push_flags & LDMSD_UPDTR_F_PUSH)
			updtr_mode = "push=true";
		else if (updtr->push_flags & LDMSD_UPDTR_F_PUSH_CHANGE)
			updtr_mode = "push=onchange";
		if (updtr_mode)
			fprintf(fp, " %s", updtr_mode);
		fprintf(fp, " interval=%ld", updtr->default_task.task.sched_us);
		if (updtr->default_task.task_flags & LDMSD_TASK_F_SYNCHRONOUS)
		    fprintf(fp, " offset=%ld\n", updtr->default_task.task.offset_us);
		else
			fprintf(fp, "\n");
		/* Add producers to updater */
		ldmsd_prdcr_ref_t ref;
		for (ref = ldmsd_updtr_prdcr_first(updtr); ref;
				ref = ldmsd_updtr_prdcr_next(ref)) {
			ldmsd_prdcr_lock(ref->prdcr);
			fprintf(fp, "updtr_prdcr_add name=%s regex=^%s$\n", updtr->obj.name, ref->prdcr->obj.name);
			ldmsd_prdcr_unlock(ref->prdcr);
		}
		/* Add match sets if there are any */
		if (!LIST_EMPTY(&updtr->match_list)) {
			LIST_FOREACH(match, &updtr->match_list, entry) {
				if (match->selector == LDMSD_NAME_MATCH_INST_NAME)
					sel_str = "inst";
				else
					sel_str = "schema";
				fprintf(fp, "updtr_match_add name=%s match=%s regex=^%s$\n",
					updtr->obj.name, sel_str, match->regex_str);
			}
		}
		/* Check updater status */
		if (updtr->state == LDMSD_UPDTR_STATE_RUNNING)
			fprintf(fp, "updtr_start name=%s\n", updtr->obj.name);
		ldmsd_updtr_unlock(updtr);
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_UPDTR);
	/* Storage Policies */
	ldmsd_strgp_t strgp;

	ldmsd_cfg_lock(LDMSD_CFGOBJ_STRGP);
	for (strgp = ldmsd_strgp_first(); strgp;
		strgp = ldmsd_strgp_next(strgp)) {
		fprintf(fp, "strgp_add name=%s "
			"plugin=%s "
			"container=%s "
			"flush=%ld "
			"perm=%d",
			strgp->obj.name,
			strgp->store->plugin->name,
			strgp->container,
			strgp->flush_interval.tv_sec,
			strgp->obj.perm);
		if (strgp->regex_s)
			fprintf(fp, " regex=%s", strgp->regex_s);
		else
			fprintf(fp, " schema=%s", strgp->schema);
		if (strgp->decomp)
			fprintf(fp, " decomposition=%s", strgp->decomp_path);
		fprintf(fp, "\n");
		LIST_FOREACH(match, &strgp->prdcr_list, entry) {
			fprintf(fp, "strgp_prdcr_add name=%s regex=%s\n",
					strgp->obj.name,
					match->regex_str);
		}
		if (strgp->state == LDMSD_STRGP_STATE_RUNNING)
			fprintf(fp, "strgp_start name=%s\n", strgp->obj.name);
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_STRGP);
	goto send_reply;
err0:
	rc = EINVAL;
	reqc->errcode = EINVAL;
	goto send_reply;
send_reply:
	if (fp)
		fclose(fp);
	free(filename);
	ldmsd_send_req_response(reqc, reqc->line_buf);
	return rc;
}

static int __on_republish_resp(ldmsd_req_cmd_t rcmd)
{
	ldmsd_req_attr_t attr;
	ldmsd_req_hdr_t resp = (ldmsd_req_hdr_t)(rcmd->reqc->req_buf);
	attr = ldmsd_first_attr(resp);
	ovis_log(config_log, OVIS_LDEBUG, "%s: %s\n", __func__, (char *)attr->attr_value);
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
		ovis_log(config_log, OVIS_LCRITICAL, "ldmsd is out of memory\n");
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
	if (rc)
		goto out;

	rc = ldmsd_client_stream_pubstats_update(c, data_len);
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
	ldms_xprt_get(xprt, "RSE");
	return ent;
}

	static inline
void __RSE_free(__RSE_t ent)
{
	ldms_xprt_put(ent->key.xprt, "RSE");
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
	struct ldms_xprt_stats_s *op_min_ep;
	// struct ldms_xprt *op_min_xprt;
	uint64_t op_max_us;
	struct ldms_xprt_stats_s *op_max_ep;
	// struct ldms_xprt *op_max_xprt;
	uint64_t op_mean_us;
};

#define __APPEND_SZ 4096
#define __APPEND(...) do {					\
	int len = snprintf(s, sz, __VA_ARGS__);			\
	if (len >= sz) {					\
		uint64_t off = (uint64_t)s - (uint64_t)buff;	\
		uint64_t bump = LDMS_ROUNDUP(len-sz, __APPEND_SZ);  \
		if (bump == 0)					\
			bump = __APPEND_SZ;			\
		sz += bump;					\
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

/*
 *       {
 *          "level": [,
 *          "rails": [
 *              {
 *                  "remote_host": <hostname>:<port>
 *                  "state": <LISTEN | CONNECTING | CONNECT | CLOSE>,
 *                  "n_eps": 2,
 *                  "endpoints": {
 *                      "<hostname>:<port>": {
 *                              "sq_sz": 10
 *                      }
 *                  }
 *              }, ....
 *          ]
 *          "compute_time_us": 1234,
 *          "connect_rate_s": 0.5,
 *          "connect_request_rate_s": 1.0,
 *          "disconnect_rate_s": 0.2,
 *          "reject_rate_s": 0.0,
 *          "auth_fail_rate_s": 0.0,
 *          "xprt_count": 1,
 *          "connect_count": 1,
 *          "connecting_count": 0,
 *          "listen_count": 0,
 *          "close_count": 0,
 *          "duration": 10.5,
 *          "op_stats": {
 *              "LOOKUP": {
 *                  "count": 100,
 *                  "total_us": 5000,
 *                  "min_us": 10,
 *                  "min_peer": "192.168.1.1:1234",
 *                  "min_peer_type": "tcp",
 *                  "max_us": 100,
 *                  "max_peer": "192.168.1.2:5678",
 *                  "max_peer_type": "tcp",
 *                  "mean_us": 50
 *              },
 *              'UPDATE': {...},
 *              'PUBLISH': {...},
 *              'SET_DELETE': {...},
 *              'DIR_REQ': {...},
 *              'DIR_REP': {...},
 *              'SEND': {...},
 *              'RECV': {...},
 *              'STREAM_PUBLISH': {...},
 *              'STREAM_SUBSCRIBE': {...},
 *              'STREAM_UNSUBSCRIBE': {...}
 *          }
 *      }
 */

static char *__xprt_stats_as_json(size_t *json_sz, int reset, int level)
{
	char *buff;
	char *s;
	size_t sz = __APPEND_SZ;
	struct op_summary op_sum[LDMS_XPRT_OP_COUNT];
	enum ldms_xprt_ops_e op_e;
	int rail_count = 0;
	int xprt_connect_count = 0;
	int xprt_connecting_count = 0;
	int xprt_listen_count = 0;
	int xprt_close_count = 0;
	struct timespec start, end;
	struct sockaddr_in *sin;
	char ip_str[32];
	char xprt_type[16];
	struct ldms_xprt_rate_data rate_data;
	int rc, first_rail = 1, first_ep;
	struct ldms_xprt_stats_result *res;
	struct ldms_xprt_stats_s *rent, *ep_res;
	int i;


	xprt_type[sizeof(xprt_type)-1] = 0; /* NULL-terminate at the end */

	(void)clock_gettime(CLOCK_REALTIME, &start);

	buff = malloc(sz);
	if (!buff)
		return NULL;
	s = buff;

	memset(op_sum, 0, sizeof(op_sum));
	for (op_e = 0; op_e < LDMS_XPRT_OP_COUNT; op_e++)
		op_sum[op_e].op_min_us = LLONG_MAX;

	__APPEND("{");
	__APPEND(" \"level\" : %d,", level);
	__APPEND(" \"rails\":[");

	/* Compute summary statistics across all of the transports */
	res = ldms_xprt_stats_result_get(LDMS_PERF_M_STATS, reset);
	if (!res) {
		rc = errno;
		ovis_log(config_log, OVIS_LERROR, "Failed to obtain transport statistics. Error %d.\n", rc);
		free(buff);
		return NULL;
	}

	LIST_FOREACH(rent, res, ent) {
		__APPEND("  %s{", ((!first_rail)?",":""));
		first_rail = 0;
		__APPEND("   \"state\":\"%s\",", ldms_xprt_stats_state(rent->state));
		if (rent->state == LDMS_XPRT_STATS_S_CONNECT) {
			__APPEND("  \"remote_host\":\"%s:%s\",", rent->rhostname, rent->rport_no);
		}

		__APPEND("   \"n_eps\":%d,", rent->rail.n_eps);

		switch (rent->state) {
		case LDMS_XPRT_STATS_S_LISTEN:
			xprt_listen_count += 1;
			break;
		case LDMS_XPRT_STATS_S_CONNECTING:
			xprt_connecting_count += 1;
			break;
		case LDMS_XPRT_STATS_S_CONNECT:
			xprt_connect_count += 1;
			break;
		case LDMS_XPRT_STATS_S_CLOSE:
			xprt_close_count += 1;
		}

		__APPEND("   \"endpoints\":{");
		rail_count += 1;
		first_ep = 1;
		for (i = 0; i < rent->rail.n_eps; i++) {
			ldms_stats_entry_t op;

			ep_res = &rent->rail.eps_stats[i];
			if (ep_res->state == LDMS_XPRT_STATS_S_CONNECT) {
				__APPEND("    %s\"%s:%s\":{", ((!first_ep)?",":""),
						ep_res->rhostname, ep_res->rport_no);
				__APPEND("     \"sq_sz\":%ld", ep_res->ep.sq_sz);
				__APPEND("  }");
				first_ep = 0;
			}

			for (op_e = 0; op_e < LDMS_XPRT_OP_COUNT; op_e++) {
				op = &ep_res->ep.ops[op_e];
				if (!op->count)
					continue;
				op_sum[op_e].op_total_us += op->total_us;
				op_sum[op_e].op_count += op->count;
				if (op->min_us < op_sum[op_e].op_min_us) {
					op_sum[op_e].op_min_us = op->min_us;
					op_sum[op_e].op_min_ep = ep_res;
				}
				if (op->max_us > op_sum[op_e].op_max_us) {
					op_sum[op_e].op_max_us = op->max_us;
					op_sum[op_e].op_max_ep = ep_res;
				}
			}
		}
		__APPEND("  } }");
	}

	__APPEND("],\n");

	for (op_e = 0; op_e < LDMS_XPRT_OP_COUNT; op_e++) {
		if (op_sum[op_e].op_count) {
			op_sum[op_e].op_mean_us =
				op_sum[op_e].op_total_us / op_sum[op_e].op_count;
		}
	}
	ldms_xprt_rate_data(&rate_data, reset);
	(void)clock_gettime(CLOCK_REALTIME, &end);
	uint64_t compute_time = ldms_timespec_diff_us(&start, &end);

	__APPEND(" \"compute_time_us\": %ld,\n", compute_time);
	__APPEND(" \"connect_rate_s\": %f,\n", rate_data.connect_rate_s);
	__APPEND(" \"connect_request_rate_s\": %f,\n", rate_data.connect_request_rate_s);
	__APPEND(" \"disconnect_rate_s\": %f,\n", rate_data.disconnect_rate_s);
	__APPEND(" \"reject_rate_s\": %f,\n", rate_data.reject_rate_s);
	__APPEND(" \"auth_fail_rate_s\": %f,\n", rate_data.auth_fail_rate_s);
	__APPEND(" \"xprt_count\": %d,\n", rail_count);
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

		strncpy(ip_str, "0.0.0.0:0", sizeof(ip_str));
		strncpy(xprt_type, "????", sizeof(xprt_type));
		if (op->op_min_ep) {
			memccpy(xprt_type, op->op_min_ep->name, 0, sizeof(xprt_type)-1);
			sin = (struct sockaddr_in *)&op->op_min_ep->ep.ss_remote;
			inet_ntop(sin->sin_family, &sin->sin_addr, ip_str, sizeof(ip_str));
			__APPEND("    \"min_peer\": \"%s:%hu\",\n", ip_str, ntohs(sin->sin_port));
		} else {
			__APPEND("    \"min_peer\": \"%s:%hu\",\n", ip_str, 0);
		}
		__APPEND("    \"min_peer_type\": \"%s\",\n", xprt_type);

		__APPEND("    \"max_us\": %ld,\n", (op->op_count ? op->op_max_us : 0));

		strncpy(ip_str, "0.0.0.0:0", sizeof(ip_str));
		strncpy(xprt_type, "????", sizeof(xprt_type));

		if (op->op_max_ep) {
			sin = (struct sockaddr_in *)&op->op_max_ep->ep.ss_remote;
			memccpy(xprt_type, op->op_max_ep->name, 0, sizeof(xprt_type)-1);
			inet_ntop(sin->sin_family, &sin->sin_addr, ip_str, sizeof(ip_str));
			__APPEND("    \"max_peer\": \"%s:%hu\",\n", ip_str, ntohs(sin->sin_port));
		} else {
			__APPEND("    \"max_peer\": \"%s:%hu\",\n", ip_str, 0);
		}

		__APPEND("    \"max_peer_type\": \"%s\",\n", xprt_type);
		__APPEND("    \"mean_us\": %ld\n", op->op_mean_us);
		if (op_e < LDMS_XPRT_OP_COUNT - 1)
			__APPEND(" },\n");
		else
			__APPEND(" }\n");
	}
	__APPEND(" }\n"); /* op_stats */
	__APPEND("}");
	*json_sz = s - buff + 1;
	ldms_xprt_stats_result_free(res);
	return buff;
__APPEND_ERR:
	if (res)
		ldms_xprt_stats_result_free(res);
	return NULL;
}

static int xprt_stats_handler(ldmsd_req_ctxt_t req)
{
	char *s, *json_s;
	size_t json_sz;
	int reset = 0;
	int level = 0;
	struct ldmsd_req_attr_s attr;

	s = ldmsd_req_attr_str_value_get_by_id(req, LDMSD_ATTR_RESET);
	__dlog(DLOG_QUERY, "xprt_stats%s%s\n", s ? " reset=" : "", s? s : "");
	if (s) {
		if (0 != strcasecmp(s, "false"))
			reset = 1;
		free(s);
	}

	s = ldmsd_req_attr_str_value_get_by_id(req, LDMSD_ATTR_LEVEL);
	if (s)
		level = atoi(s);

	json_s = __xprt_stats_as_json(&json_sz, reset, level);
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

double __ts2double(struct timespec ts)
{
	return ts.tv_sec + ((double)ts.tv_nsec)/1000000000.0;
}

json_t *__ldms_op_profiling_as_json(struct ldms_op_ctxt *xc, enum ldms_xprt_ops_e op_e)
{
	json_t *stat;
	stat = json_object();
	switch (op_e) {
	case LDMS_XPRT_OP_LOOKUP:
		json_object_set_new(stat, "app_req",
				json_real(__ts2double(xc->lookup_profile.app_req_ts)));
		json_object_set_new(stat, "req_send",
				json_real(__ts2double(xc->lookup_profile.req_send_ts)));
		json_object_set_new(stat, "req_recv",
				json_real(__ts2double(xc->lookup_profile.req_recv_ts)));
		json_object_set_new(stat, "share",
				json_real(__ts2double(xc->lookup_profile.share_ts)));
		json_object_set_new(stat, "rendzv",
				json_real(__ts2double(xc->lookup_profile.rendzv_ts)));
		json_object_set_new(stat, "read",
				json_real(__ts2double(xc->lookup_profile.read_ts)));
		json_object_set_new(stat, "complete",
				json_real(__ts2double(xc->lookup_profile.complete_ts)));
		json_object_set_new(stat, "deliver",
				json_real(__ts2double(xc->lookup_profile.deliver_ts)));
		break;
	case LDMS_XPRT_OP_UPDATE:
		json_object_set_new(stat, "app_req",
				json_real(__ts2double(xc->update_profile.app_req_ts)));
		json_object_set_new(stat, "read_start",
				json_real(__ts2double(xc->update_profile.read_ts)));
		json_object_set_new(stat, "read_complete",
				json_real(__ts2double(xc->update_profile.read_complete_ts)));
		json_object_set_new(stat, "deliver",
				json_real(__ts2double(xc->update_profile.deliver_ts)));
		break;
	case LDMS_XPRT_OP_SEND:
		json_object_set_new(stat, "app_req",
				json_real(__ts2double(xc->send_profile.app_req_ts)));
		json_object_set_new(stat, "send",
				json_real(__ts2double(xc->send_profile.send_ts)));
		json_object_set_new(stat, "complete",
				json_real(__ts2double(xc->send_profile.complete_ts)));
		json_object_set_new(stat, "deliver",
				json_real(__ts2double(xc->send_profile.deliver_ts)));
		break;
	case LDMS_XPRT_OP_SET_DELETE:
		json_object_set_new(stat, "send",
				json_real(__ts2double(xc->set_del_profile.send_ts)));
		json_object_set_new(stat, "recv",
				json_real(__ts2double(xc->set_del_profile.recv_ts)));
		json_object_set_new(stat, "acknowledge",
				json_real(__ts2double(xc->set_del_profile.ack_ts)));
		break;
	case LDMS_XPRT_OP_MSG_PUBLISH:
		json_object_set_new(stat, "hop_cnt",
				json_integer(xc->msg_pub_profile.hop_num));
		json_object_set_new(stat, "recv",
				json_real(__ts2double(xc->msg_pub_profile.recv_ts)));
		json_object_set_new(stat, "send",
				json_real(__ts2double(xc->msg_pub_profile.send_ts)));
		break;
	default:
		break;
	}
	return stat;
}

int __stream_profiling_as_json(json_t **_jobj, int is_reset) {
	json_t *jobj, *strm_jobj, *src_jobj, *hop_jobj, *prf_array, *prf_jobj;
	struct ldms_msg_ch_stats_tq_s *tq;
	struct ldms_msg_ch_stats_s *ss;
	struct ldms_msg_src_stats_s *strm_src;
	struct ldms_msg_profile_ent *prf;
	struct ldms_addr addr;
	char addr_buf[128] = "";
	struct rbn *rbn;
	int i, rc = 0;

	jobj = json_object();
	tq = ldms_msg_ch_stats_tq_get(NULL, 0, is_reset);
	if (!tq) {
		/* no stream ... nothing to do here. */
		goto out;
	}
	TAILQ_FOREACH(ss, tq, entry) {
		strm_jobj = json_object();

		RBT_FOREACH(rbn, &ss->src_stats_rbt) {
			src_jobj = json_array();

			strm_src = container_of(rbn, struct ldms_msg_src_stats_s, rbn);
			addr = strm_src->src;
			ldms_addr_ntop(&addr, addr_buf, sizeof(addr_buf));
			TAILQ_FOREACH(prf, &strm_src->profiles, ent) {
				hop_jobj = json_object();
				json_object_set_new(hop_jobj, "hop_count", json_integer(prf->profiles.hop_cnt));
				prf_array = json_array();
				json_object_set_new(hop_jobj, "profile", prf_array);
				for (i = 0; i < prf->profiles.hop_cnt; i++) {
					prf_jobj = json_object();
					json_object_set_new(prf_jobj, "recv",
						json_real(__ts2double(prf->profiles.hops[i].recv_ts)));
					json_object_set_new(prf_jobj, "deliver",
						json_real(__ts2double(prf->profiles.hops[i].send_ts)));
					json_array_append_new(prf_array, prf_jobj);
				}
				json_array_append_new(src_jobj, hop_jobj);
			}
			json_object_set_new(strm_jobj, addr_buf, src_jobj);
		}
		json_object_set_new(jobj, ss->name, strm_jobj);
	}
 out:
	*_jobj = jobj;
	return rc;
}

int __xprt_profiling_as_json(json_t **_obj, int is_reset)
{
	json_t *obj, *ep_prf, *op_prf;
	struct ldms_op_ctxt *xc;
	int rc;
	enum ldms_xprt_ops_e op_e;
	struct ldms_xprt_stats_result *res;
	struct ldms_xprt_stats_s *rent, *ep_res;
	char name[161];
	int i;

	obj = json_object();
	if (!obj) {
		ovis_log(config_log, OVIS_LCRIT, "Memory allocation failure\n");
		return ENOMEM;
	}

	res = ldms_xprt_stats_result_get(LDMS_PERF_M_PROFILNG, is_reset);
	if (!res) {
		rc = errno;
		ovis_log(config_log, OVIS_LERROR, "Failed to get transport statistics. Error %d.\n", rc);
		return rc;
	}

	LIST_FOREACH(rent, res, ent) {
		if (rent->state != LDMS_XPRT_STATS_S_CONNECT)
			continue;

		snprintf(name, 160, "%s:%s", rent->rhostname, rent->rport_no);
		ep_prf = json_object();
		for (i = 0; i < rent->rail.n_eps; i++) {
			ep_res = &rent->rail.eps_stats[i];
			for (op_e = 0; op_e < LDMS_XPRT_OP_COUNT; op_e++) {
				op_prf = json_array();
				TAILQ_FOREACH(xc, &ep_res->ep.op_ctxt_lists[op_e], ent) {
					json_array_append_new(op_prf, __ldms_op_profiling_as_json(xc, op_e));
				}
				json_object_set_new(ep_prf, ldms_xprt_op_names[op_e], op_prf);
			}
		}
		json_object_set_new(obj, name, ep_prf);
	}
	*_obj = obj;
	ldms_xprt_stats_result_free(res);
	return 0;
}

static int profiling_handler(ldmsd_req_ctxt_t req)
{
	json_t *obj, *xprt_prf, *strm_prf;
	char *json_as_str;
	int rc = 0;
	struct ldmsd_req_attr_s attr;
	size_t str_len;
	char *enable_str, *reset_str;
	int is_enable = -1; /* -1 means only getting the profile data, don't enable/disable */
	int is_reset = 0;

	enable_str = ldmsd_req_attr_str_value_get_by_id(req, LDMSD_ATTR_TYPE);
	if (enable_str) {
		is_enable = 1;
		if (0 == strcasecmp(enable_str, "false"))
			is_enable = 0; /* disable */
	}
	reset_str = ldmsd_req_attr_str_value_get_by_id(req, LDMSD_ATTR_RESET);
	if (reset_str) {
		is_reset = 1;
		if (0 == strcasecmp(reset_str, "false"))
			is_reset = 0;
	}

	if (is_enable == 1) {
		ldms_profiling_enable(-1, NULL, NULL);
	} else if (is_enable == 0) {
		ldms_profiling_disable(-1, NULL, NULL);
	}

	/*
	 * The output JSON object looks like this:
	 *
	 * {
	 *  "xprt": {
	 *	<xprt name> : {
	 *		"lookup": <profile>,
	 *		"update": <profile>,
	 *		"send": <profile>
	 *		},
	 *	...
	 *	},
	 *  "stream" : {
	 *	<stream name> : <profile>,
	 *	...
	 *	}
	 * }
	 */
	obj = json_object();
	(void)__xprt_profiling_as_json(&xprt_prf, is_reset);
	json_object_set_new(obj, "xprt", xprt_prf);

	(void)__stream_profiling_as_json(&strm_prf, is_reset);
	json_object_set_new(obj, "stream", strm_prf);

	json_as_str = json_dumps(obj, JSON_INDENT(0));
	str_len = strlen(json_as_str) + 1; /* +1 for \0 */

	attr.discrim = 1;
	attr.attr_id = LDMSD_ATTR_JSON;
	attr.attr_len = str_len;
	ldmsd_hton_req_attr(&attr);

	if (ldmsd_append_reply(req, (const char *)&attr, sizeof(attr), LDMSD_REQ_SOM_F))
		goto err;

	if (ldmsd_append_reply(req, json_as_str, str_len, 0))
		goto err;

	attr.discrim = 0;
	if (ldmsd_append_reply(req, (const char *)&attr.discrim, sizeof(attr.discrim), LDMSD_REQ_EOM_F))
		goto err;

	free(obj);
	free(json_as_str);
	return 0;
err:
	free(obj);
	free(json_as_str);
	req->errcode = rc;
	ldmsd_send_req_response(req, "Failed to get ldms_xprt's probe data");
	return ENOMEM;
}

struct store_time_thread {
	pid_t tid;
	uint64_t store_time;
	struct rbn rbn;
};

int __store_time_thread_cmp(void *tree_key, const void *key)
{
	const pid_t a = (pid_t)(uint64_t)tree_key;
	pid_t b = (pid_t)(uint64_t)key;
	return a - b;
}

static int __store_time_thread_tree(struct rbt *tree)
{
	ldmsd_prdcr_t prdcr;
	struct rbn *prdset_rbn, *rbn;
	ldmsd_prdcr_set_t prdset;
	struct store_time_thread *ent;
	pid_t tid;
	int rc = 0;

	for (prdcr = ldmsd_prdcr_first(); prdcr; prdcr = ldmsd_prdcr_next(prdcr)) {
		RBT_FOREACH(prdset_rbn, &prdcr->set_tree) {
			prdset = container_of(prdset_rbn, struct ldmsd_prdcr_set, rbn);
			if (!prdset->set)
				continue;
			tid = ldms_set_thread_id_get(prdset->set);
			rbn = rbt_find(tree, (void*)(uint64_t)tid);
			if (!rbn) {
				ent = calloc(1, sizeof(*ent));
				if (!ent) {
					ovis_log(config_log, OVIS_LCRITICAL,
							"Memory Allocation Failure.");
					rc = ENOMEM;
					goto out;
				}
				rbn_init(&ent->rbn, (void*)(uint64_t)tid);
				rbt_ins(tree, &ent->rbn);
			} else {
				ent = container_of(rbn, struct store_time_thread, rbn);
			}
			ent->store_time += (uint64_t)(prdset->store_stat.avg * prdset->store_stat.count);
		}
	}
out:
	return rc;
}

void ldmsd_prdcr_set_stats_reset(ldmsd_prdcr_set_t prdset, struct timespec *now, int flags);
static void __prdset_stats_reset(struct timespec *now, int is_update, int is_store)
{
	ldmsd_prdcr_t prdcr;
	ldmsd_prdcr_set_t prdset;
	struct rbn *rbn;
	int reset_flags = 0;

	if (is_update)
		reset_flags |= LDMSD_PRDSET_STATS_F_UPD;
	if (is_store)
		reset_flags |= LDMSD_PRDSET_STATS_F_STORE;

	for (prdcr = ldmsd_prdcr_first(); prdcr; prdcr = ldmsd_prdcr_next(prdcr)) {
		ldmsd_prdcr_lock(prdcr);
		RBT_FOREACH(rbn, &prdcr->set_tree) {
			prdset = container_of(rbn, struct ldmsd_prdcr_set, rbn);
			ldmsd_prdcr_set_stats_reset(prdset, now, reset_flags);
		}
		ldmsd_prdcr_unlock(prdcr);
	}
}

/**
 * Thread Statistics Handler
 *
 * The thread_stats handler provides information about LDMSD thread performance
 * and utilization metrics. This includes worker threads, I/O threads, and
 * sampling threads (xthreads).
 *
 * Thread statistics are collected using the ovis_thrstats library, which provides
 * both overall and time-window based utilization metrics.
 *
 * Request Format:
 * {
 *   "command": "thread_stats"
 * }
 *
 * Response Format:
 * {
 *   "count": <total thread count>,
 *   "io_threads": [
 *     {
 *       "name": "<thread name>",
 *       "tid": <Linux thread ID>,
 *       "thread_id": "<pthread ID as hex string>",
 *       "type": "io_thread",
 *       "utilization": <utilization ratio 0.0-1.0>,
 *       "sq_sz": <send queue size>,
 *       "n_eps": <number of endpoints>,
 *       "idle": <idle ratio 0.0-1.0>,
 *       "active": <active ratio 0.0-1.0>,
 *       "interval": <analysis interval in microseconds>,
 *       "ldms_xprt": {
 *         "Idle": <idle time in microseconds>,
 *         "Zap": <zap time in microseconds>,
 *         "<operation name>": <time in microseconds>,
 *         ...
 *       }
 *     },
 *     ...
 *   ],
 *   "worker_threads": [
 *     {
 *       "name": "<thread name>",
 *       "tid": <Linux thread ID>,
 *       "thread_id": "<pthread ID as hex string>",
 *       "idle_pc": <idle percentage over total runtime>,
 *       "active_pc": <active percentage over total runtime>,
 *       "total_us": <total runtime in microseconds>,
 *       "utilization": <utilization ratio 0.0-1.0>,
 *       "interval_us": <analysis interval in microseconds>,
 *       "ev_cnt": <event count>
 *     },
 *     ...
 *   ],
 *   "xthreads": [
 *     {
 *       "name": "<thread name>",
 *       "tid": <Linux thread ID>,
 *       "thread_id": "<pthread ID as hex string>",
 *       "idle_pc": <idle percentage over total runtime>,
 *       "active_pc": <active percentage over total runtime>,
 *       "total_us": <total runtime in microseconds>,
 *       "ev_cnt": <event count>
 *     },
 *     ...
 *   ],
 *   "duration": <statistics collection duration in microseconds>
 * }
 *
 * Field Descriptions:
 * -------------------
 * count           - Total number of threads reported
 * duration        - Duration of the statistics collection in microseconds
 * name            - Thread name
 * tid             - Linux thread ID (from gettid() system call)
 * thread_id       - Thread ID from pthread_self() as hex string
 *
 * utilization     - Thread utilization ratio (0.0-1.0) within the analysis interval
 *                   A value of -1 indicates insufficient data for calculation
 * sq_sz           - Send queue size (number of pending send operations)
 * n_eps           - Number of endpoints handled by this thread
 * idle            - Idle ratio within the last 3 seconds
 * active          - Active ratio within the last 3 seconds
 * interval        - Duration of the analysis interval in microseconds (3 seconds)
 * ldms_xprt       - LDMS transport operation statistics
 * ldms_xprt.Idle  - Total time spent idle in microseconds since last reset
 * Zap             - Time spent in Zap transport layer in microseconds
 * <operation>     - Time spent in specific operations (Lookup, Update, etc.)
 * idle_pc         - Idle percentage over since last reset (0-100)
 * active_pc       - Active percentage over since last reset (0-100)
 * total_us        - Total runtime in microseconds since start/reset
 * ev_cnt          - Number of events processed by the thread
 * interval_us     - Duration of the analysis interval in microseconds
 *
 * Notes:
 * ------
 * - The reported utilization is typically based on a 3-second window by default
 * - idle_pc and active_pc are based on the entire runtime since thread start/reset
 * - idle and active ratios are based on the analysis interval (typically 3 seconds)
 * - The "worker_threads" represent LDMSD event processing threads
 * - The "io_threads" represent Zap network I/O threads
 * - The "xthreads" represent sampler execution threads
 */
extern void ldmsd_worker_thrstat_free(struct ldmsd_worker_thrstat_result *res);
extern struct ldmsd_worker_thrstat_result *ldmsd_worker_thrstat_get();
extern struct ldmsd_worker_thrstat_result *ldmsd_xthrstat_get();
static char * __thread_stats_as_json(size_t *json_sz)
{
	char *buff, *s;
	size_t sz = __APPEND_SZ;
	int i, j;
	int rc;
	struct timespec start, end;
	struct ldms_thrstat_result *lres = NULL;
	struct zap_thrstat_result_entry *zthr;
	struct rbt store_time_tree;
	struct rbn *rbn;
	struct store_time_thread *stime_ent;
	struct ldmsd_worker_thrstat_result *wres = NULL;
	struct ldmsd_worker_thrstat_result *xres = NULL;
	struct ovis_scheduler_thrstats *wthr;
	struct ovis_thrstats_result *res;

	double utilization;

	/*
	 * Get the thread utilization in the last 3 seconds.
	 * The 3 seconds is adopted as it is the default refresh rate of `top`
	 */
	uint64_t interval_s = 3;

	s = buff = NULL;

	(void)clock_gettime(CLOCK_REALTIME, &start);

	rbt_init(&store_time_tree, __store_time_thread_cmp);
	rc = __store_time_thread_tree(&store_time_tree);
	if (rc) {
		goto __APPEND_ERR;
	}

	lres = ldms_thrstat_result_get(interval_s);
	if (!lres)
		goto __APPEND_ERR;

	wres = ldmsd_worker_thrstat_get(interval_s);
	if (!wres)
		goto __APPEND_ERR;

	xres = ldmsd_xthrstat_get();
	if (!xres && errno != ENOENT)
		goto __APPEND_ERR;

	buff = malloc(sz);
	if (!buff)
		goto __APPEND_ERR;
	s = buff;

	__APPEND("{");
	__APPEND(" \"count\": %d,\n", lres->count);
	__APPEND(" \"io_threads\": [\n");
	for (i = 0; i < lres->count; i++) {
		zthr = lres->entries[i].zap_res;
		res = &zthr->res;
		if (res->interval_us == 0) {
			utilization = -1;
		} else {
			utilization = (double)res->active_us / (double)res->interval_us;
		}
		__APPEND("  {\n");
		__APPEND("   \"name\": \"%s\",\n", res->name);
		__APPEND("   \"tid\": %d,\n", res->tid);
		__APPEND("   \"thread_id\": \"%p\",\n", (void*)res->thread_id);
		__APPEND("   \"idle_tot\" : %ld,\n", res->idle_tot);
		__APPEND("   \"active_tot\" : %ld,\n", res->active_tot);
		__APPEND("   \"total_us\" : %ld,\n", res->dur_tot);
		__APPEND("   \"idle_us\": %ld,\n", ((res->interval_us)?res->idle_us:0));
		__APPEND("   \"active_us\": %ld,\n", ((res->interval_us)?res->active_us:0));
		__APPEND("   \"refresh_us\": %ld,\n", res->interval_us);
		__APPEND("   \"utilization\": %g,\n", utilization);
		__APPEND("   \"sq_sz\": %lu,\n", zthr->sq_sz);
		__APPEND("   \"n_eps\": %lu,\n", zthr->n_eps);
		__APPEND("   \"ldms_xprt\": {\n");
		__APPEND("     \"Idle\": %ld,\n", lres->entries[i].idle);
		__APPEND("     \"Zap\": %ld,\n", lres->entries[i].zap_time);
		for (j = 0; j < LDMS_THRSTAT_OP_COUNT; j++) {
			if (j > 0)
				__APPEND(",\n");
			if (j == LDMS_THRSTAT_OP_UPDATE_REPLY) {
				/* Substract the store_time from the total update time */
				rbn = rbt_find(&store_time_tree, (void*)(uint64_t)res->tid);
				if (rbn) {
					stime_ent = container_of(rbn, struct store_time_thread, rbn);
					__APPEND("     \"%s\": %ld,\n", ldms_thrstat_op_str(j),
						lres->entries[i].ops[j] - stime_ent->store_time);
					__APPEND("     \"Storing Data\": %ld",
							stime_ent->store_time);
				} else {
					__APPEND("     \"%s\": %ld,\n", ldms_thrstat_op_str(j),
								lres->entries[i].ops[j]);
					__APPEND("     \"Storing Data\": 0");
				}
			} else {
				__APPEND("     \"%s\": %ld", ldms_thrstat_op_str(j),
							lres->entries[i].ops[j]);
			}
		}
		__APPEND("      }");
		__APPEND("   ");
		if (i < lres->count - 1)
			__APPEND("  },\n");
		else
			__APPEND("  }\n");
	}
	__APPEND(" ],\n"); /* end of entries array */
	__APPEND(" \"worker_threads\": [\n");
	for (i = 0; i < wres->count; i++) {
		wthr = wres->entries[i];
		res = &wthr->stats;
		if (res->interval_us == 0) {
			utilization = -1;
		} else {
			utilization = (double)res->active_us / (double)res->interval_us;
		}
		__APPEND("  {\n");
		__APPEND("   \"name\": \"%s\",\n", res->name);
		__APPEND("   \"tid\": %d,\n", res->tid);
		__APPEND("   \"thread_id\": \"%p\",\n", (void*)res->thread_id);
		__APPEND("   \"idle_tot\" : %ld,\n", res->idle_tot);
		__APPEND("   \"active_tot\" : %ld,\n", res->active_tot);
		__APPEND("   \"total_us\" : %ld,\n", res->dur_tot);
		__APPEND("   \"utilization\" : %lf,\n", utilization);
		__APPEND("   \"idle_us\": %ld,\n", ((res->interval_us)?res->idle_us:0));
		__APPEND("   \"active_us\": %ld,\n", ((res->interval_us)?res->active_us:0));
		__APPEND("   \"refresh_us\": %ld,\n", res->interval_us);
		__APPEND("   \"ev_cnt\" : %ld\n", wthr->ev_cnt);
		if (i < wres->count - 1)
			__APPEND("   },\n");
		else
			__APPEND("   }\n");
	}
	__APPEND(" ],\n"); /* end of worker threads */
	__APPEND(" \"xthreads\": [\n");
	for (i = 0; xres && i < xres->count; i++) {
		wthr = xres->entries[i];
		res = &wthr->stats;
		if (res->interval_us == 0) {
			utilization = -1;
		} else {
			utilization = (double)res->active_us / (double)res->interval_us;
		}
		__APPEND("  {\n");
		__APPEND("   \"name\": \"%s\",\n", res->name);
		__APPEND("   \"tid\": %d,\n", res->tid);
		__APPEND("   \"thread_id\": \"%p\",\n", (void*)res->thread_id);
		__APPEND("   \"idle_tot\" : %ld,\n", res->idle_tot);
		__APPEND("   \"active_tot\" : %ld,\n", res->active_tot);
		__APPEND("   \"total_us\" : %ld,\n", res->dur_tot);
		__APPEND("   \"utilization\" : %lf,\n", utilization);
		__APPEND("   \"idle_us\": %ld,\n", ((res->interval_us)?res->idle_us:0));
		__APPEND("   \"active_us\": %ld,\n", ((res->interval_us)?res->active_us:0));
		__APPEND("   \"refresh_us\": %ld,\n", res->interval_us);
		__APPEND("   \"ev_cnt\" : %ld\n", wthr->ev_cnt);
		if (i < xres->count - 1)
			__APPEND("   },\n");
		else
			__APPEND("   }\n");
	}
	__APPEND(" ],\n"); /* end of worker threads */
	(void)clock_gettime(CLOCK_REALTIME, &end);
	uint64_t compute_time = ldms_timespec_diff_us(&start, &end);

	__APPEND(" \"compute_time\": %ld\n", compute_time);
	__APPEND("}"); /* end */

	*json_sz = s - buff + 1;
	ldms_thrstat_result_free(lres);
	ldmsd_worker_thrstat_free(wres);
	while ((rbn = rbt_min(&store_time_tree))) {
		rbt_del(&store_time_tree, rbn);
		stime_ent = container_of(rbn, struct store_time_thread, rbn);
		free(stime_ent);
	}
	return buff;
__APPEND_ERR:
	ldms_thrstat_result_free(lres);
	ldmsd_worker_thrstat_free(wres);
	if (xres)
		ldmsd_worker_thrstat_free(xres);
	while ((rbn = rbt_min(&store_time_tree))) {
		rbt_del(&store_time_tree, rbn);
		stime_ent = container_of(rbn, struct store_time_thread, rbn);
		free(stime_ent);
	}
	free(buff);
	return NULL;
}

extern void ldmsd_worker_thrstats_reset(struct timespec *now);
extern void ldmsd_xthrstat_reset(struct timespec *now);
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
	if (reset) {
		struct timespec now;
		clock_gettime(CLOCK_REALTIME, &now);
		zap_thrstat_reset_all(&now);
		ldmsd_worker_thrstats_reset(&now);
		ldmsd_xthrstat_reset(&now);
		__prdset_stats_reset(&now, 1, 1);
	}

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
 *	 "connected" : <int>,
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
		set_count = 0, standby_count = 0;

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
		case LDMSD_PRDCR_STATE_STANDBY:
			standby_count++;
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
	__APPEND(" \"standby_count\": %d,\n", standby_count);
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
static char * __set_stats_as_json(size_t *json_sz, int is_summary)
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
	buff = malloc(sz);
	if (!buff)
		goto __APPEND_ERR;
	s = buff;

	if (is_summary > 0) {
		/*
		 * Only report the active count and deleting count.
		 */
		goto do_json;
	}

	mm_stats(&stats);

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
						if (prd_set->updt_interval)
							freq = 1000000.0 / (double)prd_set->updt_interval;
						else
							freq = 0.0;
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
					if (prd_set->updt_interval)
						freq = 1000000.0 / (double)prd_set->updt_interval;
					else
						freq = 0.0;
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

do_json:
	__APPEND("{");
	__APPEND(" \"active_count\": %d,\n", ldms_set_count());
	__APPEND(" \"deleting_count\": %d,\n", ldms_set_deleting_count());
	if (is_summary == 1) {
		__APPEND(" \"summary\": \"true\",\n");
		goto done_json;
	}
	__APPEND(" \"mem_total_kb\": %g,\n", (double)stats.size / 1024.0);
	__APPEND(" \"mem_free_kb\": %g,\n", (double)(stats.bytes * stats.grain) / 1024.0);
	__APPEND(" \"mem_used_kb\": %g,\n", (double)(stats.size - (stats.bytes * stats.grain)) / 1024.0);
	__APPEND(" \"set_load\": %g,\n", set_load);
done_json:
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
	char *value;
	int is_summary = 0;
	size_t json_sz;
	struct ldmsd_req_attr_s attr;

	__dlog(DLOG_QUERY, "set_stats\n");

	value = ldmsd_req_attr_str_value_get_by_id(req, LDMSD_ATTR_SUMMARY);
	if (0 == strcasecmp(value, "true"))
		is_summary = 1;

	json_s = __set_stats_as_json(&json_sz, is_summary);
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
	ldmsd_xprt_ctxt_t ctxt = ldms_xprt_ctxt_get(x);
	if (!ctxt)
		return NULL;
	return ctxt->name;
}

static int stream_publish_handler(ldmsd_req_ctxt_t reqc)
{
	char *stream_name = NULL;
	ldmsd_stream_type_t stream_type = LDMSD_STREAM_STRING;
	ldmsd_req_attr_t attr;
	int cnt;
	char *p_name;

	stream_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!stream_name) {
		reqc->errcode = EINVAL;
		ovis_log(config_log, OVIS_LERROR, "%s: The stream name is missing "
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
			    (char*)attr->attr_value,
			    attr->attr_len, NULL, p_name);
out_0:
	free(stream_name);
	return 0;
err_reply:
	free(stream_name);
	ldmsd_send_req_response(reqc, reqc->line_buf);
	return 0;
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
	char *s, *reset_s;
	size_t len;
	struct ldmsd_req_attr_s attr;
	int reset = 0;

	reset_s = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_RESET);
	if (reset_s && (0 != strcasecmp(reset_s, "false"))) {
		reset = 1;
		free(reset_s);
	}

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
	if (rc)
		goto out;

	if (reset)
		ldmsd_stream_stats_reset_all();
out:
	free(s);
	return rc;
}

static int stream_new_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	char *name;

	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!name) {
		ovis_log(config_log, OVIS_LERROR, "Received %s without the stream name\n",
				ldmsd_req_id2str(reqc->req_id));
		return 0;
	}
	rc = ldmsd_stream_new(name);
	if (rc) {
		ovis_log(config_log, OVIS_LERROR, "Error %d: failed to create stream %s\n",
				rc, name);
		free(name);
	}
	return 0;
}

static int stream_disable_handler(ldmsd_req_ctxt_t reqc)
{
	stream_enabled = 0;
	reqc->errcode = 0;
	(void)Snprintf(&reqc->line_buf, &reqc->line_len, "OK");
	ldmsd_send_req_response(reqc, reqc->line_buf);
	return 0;
}

static int stream_status_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	char *s, *reset_s;
	size_t len;
	struct ldmsd_req_attr_s attr;
	int reset = 0;

	reset_s = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_RESET);
	if (reset_s && (0 != strcasecmp(reset_s, "false"))) {
		reset = 1;
		free(reset_s);
	}

	s = ldmsd_stream_dir_dump();
	if (!s) {
		reqc->errcode = errno;
		(void)Snprintf(&reqc->line_buf, &reqc->line_len,
				"Failed to get collect stream status information.");
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
	if (rc)
		goto out;

	if (reset)
		ldmsd_stream_stats_reset_all();
out:
	free(s);
	return rc;
}

/*
 * command format:
 *     stream_stats [regex=<REGEX>] [stream=STREAM_NAME]
 *
 * If `regex` and `stream` are not given, get stats from all streams. `regex`
 * precedes `stream`.
 */
static int msg_stats_handler(ldmsd_req_ctxt_t reqc)
{
	char *regex = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_REGEX);
	char *stream = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_STREAM);
	char *reset_s = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_RESET);
	const char *match = NULL;
	int is_regex = 0;
	char buff[128];
	char *s;
	int rc = 0;
	int is_reset = 0;
	size_t len;
	struct ldmsd_req_attr_s attr;

	if (regex) {
		match = regex;
		is_regex = 1;
	} else if (stream) {
		match = stream;
	}

	if (reset_s && (0 == strcasecmp(reset_s, "true")))
		is_reset = 1;

	s = ldms_msg_stats_str(match, is_regex, is_reset);
	if (!s) {
		reqc->errcode = errno;
		snprintf(buff, sizeof(buff), "ldms_msg_stats_str() error: %d",
				errno);
		ldmsd_send_req_response(reqc, buff);
		rc = 0;
		goto out;
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
	free(regex);
	free(stream);
	free(s);
	return rc;
}

/*
 * command format:
 *     stream_client_stats
 *
 * This command takes no options.
 */
static int msg_client_stats_handler(ldmsd_req_ctxt_t reqc)
{
	char *s;
	char buff[128];
	int rc = 0;
	size_t len;
	struct ldmsd_req_attr_s attr;
	char *reset_s = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_RESET);
	int is_reset = 0;

	if (reset_s && (0 == strcasecmp(reset_s, "true")))
		is_reset = 1;

	s = ldms_msg_client_stats_str(is_reset);
	if (!s) {
		reqc->errcode = errno;
		snprintf(buff, sizeof(buff), "ldms_msg_client_stats_str() error: %d",
				errno);
		ldmsd_send_req_response(reqc, buff);
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

static int msg_disable_handler(ldmsd_req_ctxt_t reqc)
{
	ldms_msg_disable();
	reqc->errcode = 0;
	(void)Snprintf(&reqc->line_buf, &reqc->line_len, "OK");
	ldmsd_send_req_response(reqc, reqc->line_buf);
	return 0;
}

void ldmsd_xprt_term(ldms_t x)
{
	struct rbn *rbn;

	__RSE_t ent;
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
		ovis_log(config_log, OVIS_LERROR, "Too many auth options\n");
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
	char *xprt, *port, *host, *auth, *attr_name, *quota, *rx_limit;
	unsigned short port_no = -1;
	xprt = port = host = auth = quota = rx_limit = NULL;

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
	quota = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_QUOTA);
	rx_limit = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_RX_RATE);

	listen = ldmsd_listen_new(xprt, port, host, auth, quota, rx_limit);
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
	ovis_log(config_log, OVIS_LCRITICAL, "Out of memroy\n");
	rc = ENOMEM;
	goto send_reply;
}

static int
__prdset_upd_time_stats_json_obj(ldmsd_req_ctxt_t reqc, ldmsd_updtr_t updtr,
				ldmsd_prdcr_t prdcr, ldmsd_name_match_t match,
				int prd_cnt, int reset)
{
	int rc;
	ldmsd_prdcr_set_t prdset;
	int cnt = 0;
	const char *str;

	ldmsd_prdcr_lock(prdcr);
	if (prdcr->conn_state != LDMSD_PRDCR_STATE_CONNECTED || prdcr->xprt->disconnected) {
		rc = ENOTCONN; /* unconnected producers not shown */
		goto unlock;
	}
	if (prd_cnt) {
		rc = linebuf_printf(reqc, ",");
		if (rc)
			goto unlock;
	}
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
				"\"cnt\":%d,"
				"\"skipped_cnt\":%d,"
				"\"oversampled_cnt\":%d"
				"}",
				(cnt?",":""),
				prdset->inst_name,
				prdset->updt_stat.min,
				prdset->updt_stat.max,
				prdset->updt_stat.avg,
				prdset->updt_stat.count,
				prdset->skipped_upd_cnt,
				prdset->oversampled_cnt);
		if (reset)
			memset(&prdset->updt_stat, 0, sizeof(prdset->updt_stat));
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
__upd_time_stats_json_obj(ldmsd_req_ctxt_t reqc, ldmsd_updtr_t updtr, int reset)
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
				rc = __prdset_upd_time_stats_json_obj(reqc, updtr,
						ref->prdcr, match, cnt, reset);
				if (rc && rc != ENOTCONN)
					goto out;
				else if (!rc)
					cnt++;
			}
		}
	} else {
		ldmsd_prdcr_ref_t ref;
		for (ref = updtr_prdcr_ref_first(updtr); ref;
				ref = updtr_prdcr_ref_next(ref)) {
			rc = __prdset_upd_time_stats_json_obj(reqc, updtr,
						   ref->prdcr, NULL, cnt, reset);
			if (rc && rc != ENOTCONN)
				goto out;
			else if (!rc)
				cnt++;
		}
	}
	rc = linebuf_printf(reqc, "}");
out:
	return rc;
}

static int update_time_stats_handler(ldmsd_req_ctxt_t reqc)
{
	int rc = 0;
	ldmsd_updtr_t updtr;
	char *name = NULL;
	char *reset_s = NULL;
	int cnt = 0;
	int reset = 0;

	reset_s = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_RESET);
	if (reset_s) {
		if (0 != strcasecmp(reset_s, "false"))
			reset = 1;
		free(reset_s);
	}


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
			goto err;
		}
		rc = __upd_time_stats_json_obj(reqc, updtr, reset);
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
			rc = __upd_time_stats_json_obj(reqc, updtr, reset);
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

static json_entity_t __ldmsd_stat2dict(struct ldmsd_stat *stat)
{
	double start_ts = stat->start.tv_sec + stat->start.tv_nsec/1000000.0;
	double end_ts = stat->end.tv_sec + stat->end.tv_nsec/1000000.0;
	double min_ts = stat->min_ts.tv_sec + stat->min_ts.tv_nsec/1000000.0;
	double max_ts = stat->max_ts.tv_sec + stat->max_ts.tv_nsec/1000000.0;
	json_entity_t d = json_dict_build(NULL,
				JSON_FLOAT_VALUE, "min", stat->min,
				JSON_FLOAT_VALUE, "min_ts", min_ts,
				JSON_FLOAT_VALUE, "max", stat->max,
				JSON_FLOAT_VALUE, "max_ts", max_ts,
				JSON_FLOAT_VALUE, "avg", stat->avg,
				JSON_INT_VALUE, "count", stat->count,
				JSON_FLOAT_VALUE, "start_ts", start_ts,
				JSON_FLOAT_VALUE, "end_ts", end_ts,
				-1);
	return d;
}

static int
__store_time_stats_strgp(json_entity_t strgp_dict, ldmsd_strgp_t strgp, int reset)
{
	int rc = 0;
	ldmsd_prdcr_t prdcr;
	ldmsd_prdcr_set_t prdset;
	ldmsd_name_match_t match;
	struct rbn *rbn;
	pid_t tid;
	char tid_s[128];
	json_entity_t strgp_stats, set_stats ;
	json_entity_t producers, threads, schemas, sets;
	json_entity_t prdcr_json, thr_json, sch_json, set_json;

	strgp_stats = json_dict_build(NULL,
				JSON_DICT_VALUE, "producers", -2,
				JSON_DICT_VALUE, "threads", -2,
				JSON_DICT_VALUE, "schemas", -2,
				JSON_DICT_VALUE, "sets", -2,
				-1);
	if (!strgp_stats) {
		ovis_log(config_log, OVIS_LCRIT, "Out of memory.\n");
		rc = ENOMEM;
		goto out;
	}
	producers = json_attr_value(json_attr_find(strgp_stats, "producers"));
	threads = json_attr_value(json_attr_find(strgp_stats, "threads"));
	schemas = json_attr_value(json_attr_find(strgp_stats, "schemas"));
	sets = json_attr_value(json_attr_find(strgp_stats, "sets"));

	for (prdcr = ldmsd_prdcr_first(); prdcr; prdcr = ldmsd_prdcr_next(prdcr)) {
		match = ldmsd_strgp_prdcr_first(strgp);
		for (rc = 0; match; match = ldmsd_strgp_prdcr_next(match)) {
			rc = regexec(&match->regex, prdcr->obj.name, 0, NULL, 0);
			if (!rc)
				break;
		}
		for (rbn = rbt_min(&prdcr->set_tree); rbn; rbn = rbn_succ(rbn)) {

			prdset = container_of(rbn, struct ldmsd_prdcr_set, rbn);
			if (strgp->schema) {
				if (0 != strcmp(strgp->schema, prdset->schema_name))
					continue;
			} else {
				rc = regexec(&strgp->schema_regex, prdset->schema_name, 0, NULL, 0);
				if (rc)
					continue;
			}

			prdcr_json = json_attr_find(producers, prdcr->obj.name);
			if (!prdcr_json) {
				/*
				 * The dictionary may be extended to contain
				 * producer's statistics in the future.
				 */
				prdcr_json = json_entity_new(JSON_DICT_VALUE);
				if (!prdcr_json)
					goto oom;
				rc = json_attr_add(producers, prdcr->obj.name, prdcr_json);
				if (rc)
					goto json_error;
			}

			tid = ldms_set_thread_id_get(prdset->set);
			snprintf(tid_s, 127, "%d", tid);
			thr_json = json_attr_find(threads, tid_s);
			if (!thr_json) {
				/*
				 * The dictionary may be extended to contain
				 * thread's statistics in the future.
				 */
				thr_json = json_entity_new(JSON_DICT_VALUE);
				if (!thr_json)
					goto oom;
				rc = json_attr_add(threads, tid_s, thr_json);
				if (rc)
					goto json_error;
			}

			sch_json = json_attr_find(schemas, prdset->schema_name);
			if (!sch_json) {
				/*
				 * The dictionary may be extended to contain
				 * schema's statistics in the future.
				 */
				sch_json = json_entity_new(JSON_DICT_VALUE);
				if (!sch_json)
					goto oom;
				rc = json_attr_add(schemas, prdset->schema_name, sch_json);
				if (rc)
					goto json_error;
			}

			set_json = json_dict_build(NULL,
					JSON_STRING_VALUE, "producer", prdcr->obj.name,
					JSON_STRING_VALUE, "schema", prdset->schema_name,
					JSON_STRING_VALUE, "thread_id", tid_s,
					-1);
			set_stats = __ldmsd_stat2dict(&prdset->store_stat);
			if (!set_json || !set_stats)
				goto oom;
			rc = json_attr_add(set_json, "stats", set_stats);
			if (rc)
				goto json_error;
			rc = json_attr_add(sets, prdset->inst_name, set_json);
			if (rc)
				goto json_error;
			if (reset) {
				memset(&prdset->store_stat, 0, sizeof(prdset->store_stat));
				clock_gettime(CLOCK_REALTIME, &prdset->store_stat.start);
			}
		}
	}
	rc = json_attr_add(strgp_dict, strgp->obj.name, strgp_stats);
	if (rc)
		goto json_error;
	return 0;
free_stats:
	json_entity_free(strgp_stats);
out:
	return rc;
oom:
	ovis_log(config_log, OVIS_LCRIT, "Out of memory.\n");
	rc = ENOMEM;
	goto free_stats;
json_error:
	ovis_log(config_log, OVIS_LERROR, "Error creating the response "
				"of a store_time request. Error %d\n", rc);
	goto free_stats;
}

static int store_time_stats_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	char *name = NULL;
	char *reset_s = NULL;
	ldmsd_strgp_t strgp;
	int reset = 0;
	json_entity_t strgp_dict;

	reset_s = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_RESET);
	if (reset_s) {
		if (0 != strcasecmp(reset_s, "false"))
			reset = 1;
		free(reset_s);
	}

	strgp_dict = json_entity_new(JSON_DICT_VALUE);
	if (!strgp_dict) {
		ovis_log(config_log, OVIS_LCRIT, "Out of memory.\n");
		rc = ENOMEM;
		goto out;
	}

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
		rc = __store_time_stats_strgp(strgp_dict, strgp, reset);
		if (rc)
			goto err;
	} else {
		ldmsd_cfg_lock(LDMSD_CFGOBJ_STRGP);
		for (strgp = ldmsd_strgp_first(); strgp;
				strgp = ldmsd_strgp_next(strgp)) {
			ldmsd_strgp_lock(strgp);
			rc = __store_time_stats_strgp(strgp_dict, strgp, reset);
			if (rc) {
				ldmsd_strgp_unlock(strgp);
				ldmsd_cfg_unlock(LDMSD_CFGOBJ_STRGP);
				goto err;
			}
			ldmsd_strgp_unlock(strgp);
		}
		ldmsd_cfg_unlock(LDMSD_CFGOBJ_STRGP);
	}

	jbuf_t jbuf = json_entity_dump(NULL, strgp_dict);
	ldmsd_send_req_response(reqc, jbuf->buf);
	goto out;
err:
	snprintf(reqc->line_buf, reqc->line_len, "Failed to query the store "
						 "time stats. Error %d.", rc);
	reqc->errcode = rc;
	ldmsd_send_req_response(reqc, reqc->line_buf);
out:
	free(name);
	json_entity_free(strgp_dict);
	return rc;
}

static int stats_reset_handler(ldmsd_req_ctxt_t reqc)
{
	struct timespec now;
	int rc = 0;
	char *s;
	char *tmp, *tok, *ptr;
	int is_update;
	int is_store;
	int is_thread;
	int is_xprt;
	int is_stream;
	is_update = is_store = is_thread = is_xprt = is_stream = 0;

	s = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_STRING);
	if (s) {
		tmp = strdup(s);
		if (!tmp) {
			ovis_log(config_log, OVIS_LCRIT, "Memory allocation failure\n");
			(void) Snprintf(&reqc->line_buf, &reqc->line_len, "Memory allocation failed.");
			rc = ENOMEM;
			goto out;
		}

		tok = strtok_r(tmp, ",", &ptr);
		while (tok) {
			if (0 == strcasecmp(tok, "update"))
				is_update = 1;
			else if (0 == strcasecmp(tok, "store"))
				is_store = 1;
			else if (0 == strcasecmp(tok, "thread"))
				is_thread = 1;
			else if (0 == strcasecmp(tok, "xprt"))
				is_xprt = 1;
			else if (0 == strcasecmp(tok, "stream"))
				is_stream = 1;
			tok = strtok_r(NULL, ",", &ptr);
		}

	} else {
		is_update = is_store = is_thread = is_xprt = is_stream = 1;
	}

	clock_gettime(CLOCK_REALTIME, &now);
	if (is_thread || is_xprt) {
		zap_thrstat_reset_all(&now);
		ldmsd_worker_thrstats_reset(&now);
		ldmsd_xthrstat_reset(&now);
		__prdset_stats_reset(&now, is_update, is_store);
	}

	if (is_xprt) {
		ldms_xprt_rate_data(NULL, 1);
		__prdset_stats_reset(&now, is_update, is_store);
	}

	if (is_stream)
		ldms_msg_stats_reset();
out:
	free(s);
	ldmsd_send_req_response(reqc, reqc->line_buf);
	return rc;
}

static int default_auth_handler(ldmsd_req_ctxt_t reqc)
{
	int rc = 0;
	char *plugin_name = NULL;
	char *auth_attr = NULL;

	reqc->errcode = 0;

	plugin_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_PLUGIN);
	if (!plugin_name) {
		reqc->errcode = EINVAL;
		reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
					  "The attribute 'plugin' is missing.");
		goto send_reply;
	}

	auth_attr = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_STRING);

	reqc->errcode = ldmsd_process_cmd_line_arg('a', plugin_name);
	if (reqc->errcode) {
		reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
					  "Failed to process 'default_auth'.");
		goto send_reply;
	}

	if (auth_attr) {
		reqc->errcode = ldmsd_process_cmd_line_arg('A', auth_attr);
		if (reqc->errcode) {
			reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
						  "Failed to process the default auth attributes.");
			goto send_reply;
		}
	}

send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(plugin_name);
	free(auth_attr);
	return rc;
}

static int set_memory_handler(ldmsd_req_ctxt_t reqc)
{
	int rc = 0;
	char *value = NULL;

	value = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_SIZE);
	if (!value) {
		reqc->errcode = EINVAL;
		reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
					  "The attribute 'size' is missing.");
		goto send_reply;
	}

	reqc->errcode = ldmsd_process_cmd_line_arg('m', value);
	if (reqc->errcode) {
		snprintf(reqc->line_buf, reqc->line_len,
				"The given value '%s' is invalid.",
				value);
		goto send_reply;
	}

send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(value);
	return rc;
}

static int log_file_handler(ldmsd_req_ctxt_t reqc)
{
	int rc = 0;
	char *path = NULL;

	path = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_PATH);
	if (!path) {
		reqc->errcode = EINVAL;
		reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
					  "The attribute 'path' is missing.");
		goto send_reply;
	}

	reqc->errcode = ldmsd_process_cmd_line_arg('l', path);
	if (reqc->errcode) {
		reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
					  "Failed to open the log file '%s'.", path);
		goto send_reply;
	}

send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(path);
	return rc;
}

static int publish_kernel_handler(ldmsd_req_ctxt_t reqc)
{
	int rc = 0;
	char *path = NULL;

	path = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_PATH);
	if (path) {
		/*
		 * The kernel setfile will be opened later.
		 * The process will exit if it fails to open the setfile.
		 * See k_proc().
		 */
		reqc->errcode = ldmsd_process_cmd_line_arg('s', path);
		if (reqc->errcode) {
			reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
						  "Failed to process the kernel set file path '%s'.", path);
			goto send_reply;
		}
	}
	reqc->errcode = ldmsd_process_cmd_line_arg('k', NULL);
	if (reqc->errcode) {
		reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
					  "Failed to config LDMSD to publish the kernel metrics.");
		goto send_reply;
	}

send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(path);
	return rc;
}

static int daemon_name_set_handler(ldmsd_req_ctxt_t reqc)
{
	int rc = 0;
	char *name = NULL;

	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!name) {
		reqc->errcode = EINVAL;
		reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
					 "The attribute 'name' is missing.");
		goto send_reply;
	}
	reqc->errcode = ldmsd_process_cmd_line_arg('n', name);
	if (reqc->errcode) {
		reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
					  "Failed to process the `daemon_name` command.");
		goto send_reply;
	}

send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(name);
	return rc;
}

static int worker_threads_set_handler(ldmsd_req_ctxt_t reqc)
{
	int rc = 0;
	char *value = NULL;

	value = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_SIZE);
	if (!value) {
		reqc->errcode = EINVAL;
		reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
					  "The attribute 'num' is missing.");
		goto send_reply;
	}
	reqc->errcode = ldmsd_process_cmd_line_arg('P', value);
	if (reqc->errcode) {
		reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
					  "Failed to process the 'worker_threads' command");
		goto send_reply;
	}
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(value);
	return rc;
}

static int default_quota_set_handler(ldmsd_req_ctxt_t reqc)
{
	int rc = 0;
	char *value = NULL;

	value = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_QUOTA);
	if (!value) {
		reqc->errcode = EINVAL;
		reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
					  "The attribute 'quota' is missing.");
		goto send_reply;
	}
	reqc->errcode = ldmsd_process_cmd_line_arg('C', value);
	if (reqc->errcode) {
		reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
					  "Failed to process the 'default_quota' command");
		goto send_reply;
	}
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(value);
	return rc;
}

static int pid_file_handler(ldmsd_req_ctxt_t reqc)
{
	int rc = 0;
	char *path = NULL;

	path = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_PATH);
	if (!path) {
		reqc->errcode = EINVAL;
		reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
					  "The attribute 'path' is missing.");
		goto send_reply;
	}

	reqc->errcode = ldmsd_process_cmd_line_arg('r', path);
	if (reqc->errcode) {
		reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
					  "Failed to open the log file '%s'.", path);
		goto send_reply;
	}

send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(path);
	return rc;
}

static int banner_mode_handler(ldmsd_req_ctxt_t reqc)
{
	int rc = 0;
	char *mode_s = NULL;
	mode_s = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_LEVEL);
	if (!mode_s) {
		reqc->errcode = EINVAL;
		reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
					  "The attribute 'mode' is missing.");
		goto send_reply;
	}
	reqc->errcode = ldmsd_process_cmd_line_arg('B', mode_s);
	if (reqc->errcode) {
		reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
					  "The given banner mode '%s' is invalid.", mode_s);
		goto send_reply;
	}
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(mode_s);
	return rc;
}

/* Sampler Discovery */

/* *2 for the two hex digits needed for each 16-bit value, * 8 for the 8 groups of values, + 7 for the 7 colons */
#define MAX_IPV6_STR_LEN (sizeof(uint16_t) * 2 * 8 + 7)
static int __cidr2addr6(const char *cdir_str, struct ldms_addr *addr, int *prefix_len)
{
	int rc;
	int is_ipv6 = 0;
	char netaddr_str[MAX_IPV6_STR_LEN];
	int _prefix_len;
	struct ldms_addr s6 = {
		.addr = {0,0,0,0,0,0,0,0,0,0,0xff,0xff,0,0,0,0}
	};
	if (strchr(cdir_str, ':') != NULL)
		is_ipv6 = 1;

	rc = sscanf(cdir_str, "%[^/]/%d", netaddr_str, &_prefix_len);
	if (rc != 2) {
		return EINVAL;
	}

	if (prefix_len)
		*prefix_len = _prefix_len;

	if (addr) {
		if (is_ipv6) {
			rc = inet_pton(AF_INET6, netaddr_str, &addr->addr);
		} else {
			rc = inet_pton(AF_INET, netaddr_str, &addr->addr);
		}
	}

	if (rc != 1)
		return rc;
	if (!is_ipv6) {
		/* Make the ipv4-mapped ipv6 format */
		memcpy(&s6.addr[12], &addr->addr, 4);
		memcpy(&addr->addr, &s6.addr, 16);
		*prefix_len += 96;
	}
	addr->sa_family = AF_INET6;
	return 0;
}


/* Aggregator */
/* The implementation is in ldmsd_prdcr.c */
extern int prdcr_ref_cmp(void *a, const void *b);

static void prdcr_listen___del(ldmsd_cfgobj_t obj)
{
	ldmsd_prdcr_listen_t pl = (ldmsd_prdcr_listen_t)obj;
	if (pl->cidr_str)
		free((char*)pl->cidr_str);
	if (pl->hostname_regex_s) {
		regfree(&pl->regex);
		free((char*)pl->hostname_regex_s);
	}
	ldmsd_cfgobj___del(obj);
}

static int prdcr_listen_add_handler(ldmsd_req_ctxt_t reqc)
{
	int rc = 0;
	char *name;
	char *regex_str;
	char *cidr_str;
	char *reconnect_str;
	char *disabled_start;
	char *attr_name;
	char *quota;
	char *rx_rate;
	char *prdcr_type;
	char *rail_s;
	char *advtr_xprt;
	char *advtr_port;
	char *advtr_auth;
	char *endptr = NULL;
	ldmsd_prdcr_listen_t pl;

	name = regex_str = reconnect_str = cidr_str = disabled_start = NULL;
	quota = rx_rate = rail_s = NULL;
	prdcr_type = advtr_xprt = advtr_port = advtr_auth = NULL;

	attr_name = "name";
	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!name)
		goto einval;

	regex_str = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_REGEX);
	cidr_str = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_IP);
	disabled_start = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_AUTO_INTERVAL);
	quota = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_QUOTA);
	rx_rate = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_RX_RATE);
	rail_s = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_RAIL);
	prdcr_type = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_TYPE);
	advtr_xprt = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_XPRT);
	advtr_port = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_PORT);
	advtr_auth = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_AUTH);
	reconnect_str = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_INTERVAL);

	pl = (ldmsd_prdcr_listen_t)
		ldmsd_cfgobj_new_with_auth(name, LDMSD_CFGOBJ_PRDCR_LISTEN,
						sizeof(*pl), prdcr_listen___del,
						0, 0, 0);
	if (!pl) {
		if (errno == EEXIST)
			goto eexist;
		else
			goto enomem;
	}

	pl->auto_start = 1;
	if (disabled_start) {
		if ((0 == strcmp(disabled_start, "1")) ||
				(0 == strcasecmp(disabled_start, "true"))) {
			pl->auto_start = 0;
		}
	}

	if (regex_str) {
		pl->hostname_regex_s = strdup(regex_str);
		if (!pl->hostname_regex_s) {
			goto enomem;
		}

		rc = ldmsd_compile_regex(&pl->regex, regex_str, reqc->line_buf, reqc->line_len);
		if (rc) {
			reqc->errcode = EINVAL;
			reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
						"The regular expression string "
						"'%s' is invalid.", regex_str);
			goto err;
		}
	}

	if (cidr_str) {
		pl->cidr_str = strdup(cidr_str);
		if (!pl->cidr_str) {
			reqc->errcode = ENOMEM;
			reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
						    "Memory allocation failure.");
			goto enomem;
		}
		rc = __cidr2addr6(cidr_str, &pl->net_addr, &pl->prefix_len);
		if (rc) {
			reqc->errcode = EINVAL;
			reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
						"The given CIDR string '%s' "
						"is invalid.", cidr_str);
			goto err;
		}
	} else {
		pl->quota = 0; /* 0 means inherit quota from the listen xprt */
	}

	if (rx_rate) {
		pl->rx_rate = ovis_get_mem_size(rx_rate);
		if (!pl->rx_rate) {
			reqc->errcode = EINVAL;
			reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
						"The given rx_rate '%s' "
						"is invalid.", rx_rate);
			goto err;
		}
	} else {
		pl->rx_rate = 0; /* 0 means inherit rx_rate from the listen xprt */
	}

	if (prdcr_type) {
		pl->prdcr_type = ldmsd_prdcr_str2type(prdcr_type);
		if (pl->prdcr_type == LDMSD_PRDCR_TYPE_PASSIVE) {
			pl->prdcr_type = LDMSD_PRDCR_TYPE_ADVERTISED_PASSIVE;
		} else if (pl->prdcr_type == LDMSD_PRDCR_TYPE_ACTIVE) {
			pl->prdcr_type = LDMSD_PRDCR_TYPE_ADVERTISED_ACTIVE;
		} else {
			reqc->errcode = EINVAL;
			reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
						"The type value '%s' is invalid.",
						prdcr_type);
			reqc->errcode = EINVAL;
			goto err;
		}
	} else {
		pl->prdcr_type = LDMSD_PRDCR_TYPE_ADVERTISED_PASSIVE;
	}

	if (pl->prdcr_type == LDMSD_PRDCR_TYPE_ADVERTISED_PASSIVE) {
		pl->reconnect = INT_MAX;

		if (rail_s) {
			ovis_log(config_log, OVIS_LINFO,
					"prdcr_listen_add '%s' is passive. " \
					"Ignore the given 'rail' value.", name);
		}
		if (advtr_xprt) {
			ovis_log(config_log, OVIS_LINFO,
					"prdcr_listen_add '%s' is passive. " \
					"Ignore the given 'advertiser_xprt' value.", name);
		}
		if (advtr_auth) {
			ovis_log(config_log, OVIS_LINFO,
					"prdcr_listen_add '%s' is passive. " \
					"Ignore the given 'advertiser_auth' value.", name);
		}
		if (advtr_port) {
			ovis_log(config_log, OVIS_LINFO,
					"prdcr_listen_add '%s' is passive. " \
					"Ignore the given 'advertiser_port' value.", name);
		}
		if (reconnect_str) {
			ovis_log(config_log, OVIS_LINFO,
					"prdcr_listen_add '%s' is passive. " \
					"Ignore the given 'reconnect' value.", name);
		}

		goto update_prdcr_tree;
	}

	/* Active */
	if (rail_s) {
		pl->rail = strtol(rail_s, &endptr, 0);
		if (!endptr) {
			reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
						"Rail value '%s' is invalid.", rail_s);
			reqc->errcode = EINVAL;
			goto err;
		}
	} else {
		pl->rail = 1;
	}

	if (advtr_port) {
		endptr = NULL;
		pl->advtr_port = strtol(advtr_port, &endptr, 0);
		if ((pl->advtr_port < 1) || (pl->advtr_port > USHRT_MAX)) {
			reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
						  "The port value '%s' is invalid.",
						  advtr_port);
			reqc->errcode = EINVAL;
			goto err;
		}
	} else {
		attr_name = "advertiser_port";
		goto einval_active;
	}

	if (reconnect_str) {
		rc = ovis_time_str2us(reconnect_str, &pl->reconnect);
		if (rc) {
			reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
						  "The reconnect value '%s' is invalid.",
						  reconnect_str);
			reqc->errcode = EINVAL;
			goto err;
		}
	} else {
		attr_name = "reconnect";
		goto einval_active;
	}

	if (!advtr_xprt) {
		attr_name = "advertiser_xprt";
		goto einval_active;
	}

	pl->auth = advtr_auth;
	pl->advtr_xprt = advtr_xprt;

update_prdcr_tree:
	rbt_init(&pl->prdcr_tree, prdcr_ref_cmp);
	ldmsd_cfgobj_unlock(&pl->obj);

send_reply:
	free(name);
	free(regex_str);
	free(cidr_str);
	free(reconnect_str);
	free(disabled_start);
	free(rx_rate);
	free(quota);
	free(advtr_port);
	free(rail_s);
	free(prdcr_type);

	ldmsd_send_req_response(reqc, reqc->line_buf);
	return rc;
eexist:
	reqc->errcode = EEXIST;
	(void)snprintf(reqc->line_buf, reqc->line_len,
			"The prdcr listener %s already exists.", name);
	/* We won't remove the existing prdcr_listen in this case, so goto send_reply. */
	goto send_reply;
enomem:
	reqc->errcode = ENOMEM;
	(void)snprintf(reqc->line_buf, reqc->line_len,
			"Memory allocation failed.");
	goto err;
einval:
	reqc->errcode = EINVAL;
	(void) snprintf(reqc->line_buf, reqc->line_len,
			"The attribute '%s' is required.", attr_name);
	goto err;
einval_active:
	reqc->errcode = EINVAL;
	(void) snprintf(reqc->line_buf, reqc->line_len,
			"The attribute '%s' is required for the 'active' mode.", attr_name);
	goto err;
err:
	ldmsd_cfgobj_unlock(&pl->obj);
	ldmsd_cfgobj_rm(&pl->obj);
	ldmsd_cfgobj_put(&pl->obj, "init");
	goto send_reply;
}

/* This is implemented in ldmsd_cfgobj.c */
extern struct rbt *cfgobj_trees[];
static int prdcr_listen_del_handler(ldmsd_req_ctxt_t reqc)
{
	int rc = 0;
	char *name = NULL;
	struct ldmsd_sec_ctxt sctxt;
	ldmsd_prdcr_listen_t pl = NULL;

	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!name) {
		reqc->errcode = EINVAL;
		reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
					"The attribute 'name' is required,");
		goto send_reply;
	}

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);

	ldmsd_cfg_lock(LDMSD_CFGOBJ_PRDCR_LISTEN);
	for (pl = (ldmsd_prdcr_listen_t)ldmsd_cfgobj_first(LDMSD_CFGOBJ_PRDCR_LISTEN); pl;
			pl = (ldmsd_prdcr_listen_t)ldmsd_cfgobj_next(&pl->obj)) {
		if (0 != strcmp(name, pl->obj.name))
			continue;

		ldmsd_cfgobj_lock(&pl->obj);
		rc = ldmsd_cfgobj_access_check(&pl->obj, 0222, &sctxt);
		if (rc) {
			ldmsd_cfgobj_unlock(&pl->obj);
			ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR_LISTEN);
			reqc->errcode = EACCES;
			reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
						"Permission denied");
			goto send_reply;
		}

		if (pl->state != LDMSD_PRDCR_LISTEN_STATE_STOPPED) {
			ldmsd_cfgobj_unlock(&pl->obj);
			ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR_LISTEN);
			reqc->errcode = EBUSY;
			reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
						"The producer listen '%s' is in use.\n",
						name);
			goto send_reply;
		}

		if (ldmsd_cfgobj_refcount(&pl->obj) > 2) {
			ldmsd_cfgobj_unlock(&pl->obj);
			ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR_LISTEN);
			reqc->errcode = EBUSY;
			reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
						"The producer listen '%s' is in use.\n",
						name);
			goto send_reply;
		}

		rbt_del(cfgobj_trees[LDMSD_CFGOBJ_PRDCR_LISTEN], &pl->obj.rbn);
		ldmsd_cfgobj_put(&pl->obj, "cfgobj_tree"); /* Put back the reference from the tree */
		ldmsd_cfgobj_unlock(&pl->obj);
		goto unlock_tree;
	}

	if (!pl) {
		ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR_LISTEN);
		reqc->errcode = ENOENT;
		reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
					"The producer listen '%s' does not exist.\n",
					name);
		goto send_reply;
	}

unlock_tree:
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR_LISTEN);

send_reply:
	if (pl)
		ldmsd_cfgobj_put(&pl->obj, "iter"); /* Put back the 'first' or 'next' reference */
	ldmsd_send_req_response(reqc, reqc->line_buf);
	return rc;
}

static int prdcr_listen_start_handler(ldmsd_req_ctxt_t reqc)
{
	int rc = 0;
	char *name = NULL;
	struct ldmsd_sec_ctxt sctxt;
	ldmsd_prdcr_listen_t pl;

	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!name) {
		reqc->errcode = EINVAL;
		reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
					"The attribute 'name' is required,");
		goto send_reply;
	}

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);

	pl = (ldmsd_prdcr_listen_t)ldmsd_cfgobj_find_get(name, LDMSD_CFGOBJ_PRDCR_LISTEN);
	if (!pl) {
		reqc->errcode = ENOENT;
		reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
					"The listen_producer '%s' does not exist.",
					name);
		goto send_reply;
	}

	ldmsd_cfgobj_lock(&pl->obj);
	rc = ldmsd_cfgobj_access_check(&pl->obj, 0222, &sctxt);
	if (rc) {
		ldmsd_cfgobj_unlock(&pl->obj);
		reqc->errcode = EACCES;
		reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
					"Permission denied");
		goto send_reply;
	}

	pl->obj.perm |= LDMSD_PERM_DSTART;
	pl->state = LDMSD_PRDCR_LISTEN_STATE_RUNNING;
	ldmsd_cfgobj_find_put(&pl->obj);
	ldmsd_cfgobj_unlock(&pl->obj);

send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	return rc;
}

static int prdcr_listen_stop_handler(ldmsd_req_ctxt_t reqc)
{
	int rc = 0;
	char *name = NULL;
	struct ldmsd_sec_ctxt sctxt;
	ldmsd_prdcr_listen_t pl;

	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!name) {
		reqc->errcode = EINVAL;
		reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
					"The attribute 'name' is required,");
		goto send_reply;
	}

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);

	pl = (ldmsd_prdcr_listen_t)ldmsd_cfgobj_find_get(name, LDMSD_CFGOBJ_PRDCR_LISTEN);
	if (!pl) {
		reqc->errcode = ENOENT;
		reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
					"The listen_producer '%s' does not exist.",
					name);
		goto send_reply;
	}

	ldmsd_cfgobj_lock(&pl->obj);
	rc = ldmsd_cfgobj_access_check(&pl->obj, 0222, &sctxt);
	if (rc) {
		reqc->errcode = EACCES;
		reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
					"Permission denied");
		goto out;
	}

	if (pl->state == LDMSD_PRDCR_LISTEN_STATE_STOPPED)
		goto out; /* already stopped, return as stop succeeds. */

	pl->obj.perm &= ~LDMSD_PERM_DSTART;
	pl->state = LDMSD_PRDCR_LISTEN_STATE_STOPPED;
out:
	ldmsd_cfgobj_find_put(&pl->obj);
	ldmsd_cfgobj_unlock(&pl->obj);

send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	return rc;
}

static int prdcr_listen_status_handler(ldmsd_req_ctxt_t reqc)
{
	int rc = 0;
	int cnt = 0;
	int prdcr_cnt = 0;
	ldmsd_prdcr_listen_t pl;
	ldmsd_prdcr_ref_t pref;
	struct rbn *rbn;
	struct ldmsd_req_attr_s attr;

	ldmsd_cfg_lock(LDMSD_CFGOBJ_PRDCR_LISTEN);
	for (pl = (ldmsd_prdcr_listen_t)ldmsd_cfgobj_first(LDMSD_CFGOBJ_PRDCR_LISTEN); pl;
			pl = (ldmsd_prdcr_listen_t)ldmsd_cfgobj_next(&pl->obj)) {
		if (cnt) {
			if ((rc = linebuf_printf(reqc, ",")))
				goto err;
		}
		rc = linebuf_printf(reqc,
				"{\"name\":\"%s\","
				 "\"state\":\"%s\","
				 "\"regex\":\"%s\","
				 "\"IP range\":\"%s\","
				 "\"quota\":\"%ld\","
				 "\"rx_rate\":\"%ld\",",
				pl->obj.name,
				((pl->state==LDMSD_PRDCR_LISTEN_STATE_RUNNING)?("running"):("stopped")),
				(pl->hostname_regex_s?pl->hostname_regex_s:"-"),
				(pl->cidr_str?pl->cidr_str:"-"),
				(pl->quota?pl->quota:ldmsd_quota),
				(pl->rx_rate?pl->rx_rate:LDMS_UNLIMITED));
		if (rc)
			goto err;
		if (pl->prdcr_type == LDMSD_PRDCR_TYPE_ADVERTISED_PASSIVE) {
			rc = linebuf_printf(reqc, "\"type\":\"passive\",");
			if (rc)
				goto err;
		} else {
			char reconnect_str[16];
			ovis_time_us2str(pl->reconnect, reconnect_str, sizeof(reconnect_str));
			rc = linebuf_printf(reqc,
				"\"type\":\"active\","
				"\"xprt\":\"%s\","
				"\"port\":\"%d\","
				"\"auth_dom\":\"%s\","
				"\"rail_sz\":\"%d\","
				"\"reconnect\":\"%s\",",
				pl->advtr_xprt,
				pl->advtr_port,
				(pl->auth?pl->auth:"_DEFAULT_"),
				pl->rail,
				reconnect_str);
			if (rc)
				goto err;

		}
		rc = linebuf_printf(reqc, "\"producers\":[");
		if (rc)
			goto err;
		prdcr_cnt = 0;
		RBT_FOREACH(rbn, &pl->prdcr_tree) {
			pref = container_of(rbn, struct ldmsd_prdcr_ref, rbn);
			if (prdcr_cnt) {
				if ((rc = linebuf_printf(reqc, ",")))
					goto err;
			}
			if ((rc = linebuf_printf(reqc, "\"%s\"", pref->prdcr->obj.name)))
				goto err;
			prdcr_cnt++;
		}
		if ((rc = linebuf_printf(reqc, "]}")))
			goto err;
		cnt++;
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR_LISTEN);
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
	return rc;
err:
	if (pl)
		ldmsd_cfgobj_put(&pl->obj, "iter");
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR_LISTEN);
	reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
				"Error getting the status: Error %d.", rc);
	reqc->errcode = EIO;
	ldmsd_send_req_response(reqc, reqc->line_buf);
	return rc;
}

enum ldmsd_prdcr_type __prdcr_listen_prdcr_type(ldmsd_prdcr_listen_t pl)
{
	if (pl->rail)
		return LDMSD_PRDCR_TYPE_ADVERTISED_ACTIVE;
	return LDMSD_PRDCR_TYPE_ADVERTISED_PASSIVE;
}

ldmsd_prdcr_t __advertised_prdcr_new(ldmsd_req_ctxt_t reqc, ldmsd_prdcr_listen_t pl,
				     const char *prdcr_name)
{
	int rc;
	char *attr_name;
	char *advtr_name;
	char *adv_hostname;
	char *advtr_port_s;
	int adv_port;

	struct ldmsd_sec_ctxt sctxt;
	char *xprt_s;
	struct ldms_addr rem_addr = {0};
	ldms_t x;

	ldmsd_prdcr_t prdcr;

	/* Get daemon's UID and GID */
	ldmsd_sec_ctxt_get(&sctxt);

	advtr_name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);

	x = ldms_xprt_get(reqc->xprt->ldms.ldms, "advertised_prdcr");
	xprt_s = (char *)ldms_xprt_type_name(x);

	attr_name = "hostname";
	adv_hostname = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_HOST);
	if (!adv_hostname)
		goto einval;

	advtr_port_s = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_PORT);
	if (pl->prdcr_type == LDMSD_PRDCR_TYPE_ADVERTISED_PASSIVE) {
		rc = ldms_xprt_addr(x, NULL, &rem_addr);
		if (rc) {
			ovis_log(NULL, OVIS_LERROR, "Failed to get the hostname " \
					"of advertiser '%s'\n", advtr_name);
			goto err;
		}
		adv_port = rem_addr.sin_port;
	} else {
		adv_port = pl->advtr_port;



		if (!pl->advtr_port) {
			char *endptr;
			adv_port = strtol(advtr_port_s, &endptr, 0);
		} else {
			adv_port = pl->advtr_port;
		}
	}

	errno = 0;
	prdcr = ldmsd_prdcr_new_with_auth(prdcr_name,
			xprt_s, adv_hostname, adv_port,
			pl->prdcr_type, pl->reconnect, pl->auth,
			sctxt.crd.uid, sctxt.crd.gid, 0700,
			pl->rail, pl->quota, pl->rx_rate, 1);
	if (!prdcr) {
		rc = errno;
		ovis_log(NULL, OVIS_LERROR, "Error %d: Failed to create an " \
					    "advertised producer corresponding " \
					    "to advertiser '%s' on hostname '%s'\n",
					    rc, advtr_name, adv_hostname);
		goto err;
	}
	if (pl->quota) {
		ldms_xprt_rail_recv_quota_set(x, pl->quota);
	}
	if (pl->rx_rate) {
		ldms_xprt_rail_recv_rate_limit_set(x, pl->rx_rate);
	}
	ldms_xprt_put(x, "advertised_prdcr"); /* Put back the reference at the beginning of the funciton */
	return prdcr;

einval:
	ovis_log(NULL, OVIS_LERROR,
			"The '%s' attribute is missing from " \
			"an advert advtr_nameise request from advertiser %s on hostname '%s'\n",
			attr_name, advtr_name, adv_hostname);
	/*
	 * It is intended to not provide detail information in the response message
	 * to prevent providing information to ill-intended communication.
	 */
	reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
				"Invalid advertisement message");
	reqc->errcode = errno = EINVAL;
	return NULL;
err:
	reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
				"ldmsd failed to create producers.");
	reqc->errcode = EINTR;
	errno = rc;
	return NULL;
}

/* The implementation is in ldmsd_updtr.c */
extern int __ldmsd_updtr_prdcr_add(ldmsd_updtr_t updtr, ldmsd_prdcr_t prdcr);
/* The implementations are in ldmsd_prdcr.c */
extern ldmsd_prdcr_ref_t prdcr_ref_new(ldmsd_prdcr_t prdcr);
extern void prdcr_connect_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg);
static int __process_advertisement(ldmsd_req_ctxt_t reqc, ldmsd_prdcr_listen_t pl, struct ldms_addr *rem_addr)
{
	int rc = 0;
	char *xprt_s;
	char *adv_hostname;
	char *adv_port;
	char *attr_name;
	char prdcr_name[NI_MAXHOST + NI_MAXSERV + 1];
	ldmsd_prdcr_t prdcr;
	ldmsd_prdcr_ref_t pl_pref, updtr_pref;
	struct rbn *rbn;
	struct ldmsd_sec_ctxt sctxt;
	uid_t uid;
	gid_t gid;
	int is_new_prdcr = 0;
	struct ldms_xprt_event conn_ev;
	ldms_t x = ldms_xprt_get(reqc->xprt->ldms.ldms, "process_advertisement");

	xprt_s = adv_hostname = adv_port = NULL;

	attr_name = "hostname";
	adv_hostname = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_HOST);
	if (!adv_hostname)
		goto einval;

	if (!pl->advtr_port) {
		attr_name = "port";
		adv_port = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_PORT);
		if (!adv_port)
			goto einval;
	} else {
		adv_port = malloc(NI_MAXSERV+1);
		if (!adv_port) {
			goto enomem;
		}
		snprintf(adv_port, NI_MAXSERV, "%d", pl->advtr_port);
	}

	snprintf(prdcr_name, 32, "%s:%s", adv_hostname, adv_port);;

	xprt_s = (char *)ldms_xprt_type_name(x);

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);
	uid = sctxt.crd.uid;
	gid = sctxt.crd.gid;

	prdcr = ldmsd_prdcr_find(prdcr_name);
	if (prdcr) {
		ldmsd_prdcr_lock(prdcr);
		if (prdcr->type == LDMSD_PRDCR_TYPE_ADVERTISED_PASSIVE) {
			switch (prdcr->conn_state) {
			case LDMSD_PRDCR_STATE_CONNECTED:
			case LDMSD_PRDCR_STATE_STANDBY:
				ovis_log(NULL, OVIS_LERROR,
					"Received a duplicate advertisement for producer '%s'. "
					"LDMSD ignores the advertisement.\n", prdcr_name);
				rc = EBUSY;
				ldmsd_prdcr_unlock(prdcr);
				goto out;
			case LDMSD_PRDCR_STATE_STOPPING:
				/*
				* The producer was manually stopped but
				* the aggregator hasn't received the 'DISCONNECTED' event yet.
				* This is a race condition between a disconnected event and
				* an advertisement notification. We reject any advertisements
				* of this producer until the transport has been completely torn down.
				*
				* Let the sampler daemon retry again.
				*/
				rc = EAGAIN;
				ldmsd_prdcr_unlock(prdcr);
				goto out;
			case LDMSD_PRDCR_STATE_STOPPED:
				prdcr->xprt = ldms_xprt_get(x, "prdcr");
				ldms_xprt_event_cb_set(prdcr->xprt, prdcr_connect_cb, prdcr);
				prdcr->conn_state = LDMSD_PRDCR_STATE_STANDBY;
				break;
			case LDMSD_PRDCR_STATE_DISCONNECTED:
				prdcr->xprt = ldms_xprt_get(reqc->xprt->ldms.ldms, "prdcr");
				ldms_xprt_event_cb_set(prdcr->xprt, prdcr_connect_cb, prdcr);
				/* Move the producer state to CONNECTED here */
				conn_ev.type = LDMS_XPRT_EVENT_CONNECTED;
				ldmsd_prdcr_unlock(prdcr);
				prdcr_connect_cb(prdcr->xprt, &conn_ev, prdcr);
				ldmsd_prdcr_lock(prdcr);
				break;
			default:
				ovis_log(NULL, OVIS_LERROR, "Reach an unexpected state (%s) of " \
						"a generated producer %s.\n",
						prdcr_state_str(prdcr->conn_state),
						prdcr->obj.name);
				rc = EINVAL;
				ldmsd_prdcr_unlock(prdcr);
				goto out;
			}
		} else if (prdcr->type == LDMSD_PRDCR_TYPE_ADVERTISED_ACTIVE) {
			/*
			 * Do nothing;
			 * The producer is an active producer,
			 * it tries to reconnect until a connection is established.
			 */
		} else {
			ovis_log(NULL, OVIS_LERROR, "Received an advertisement " \
						    "for producer %s, but " \
						    "the producer was manually added.\n",
						    prdcr_name);
			ldmsd_prdcr_unlock(prdcr);
			rc = EEXIST;
			goto out;
		}
		ldmsd_prdcr_unlock(prdcr);
	} else {
		prdcr = __advertised_prdcr_new(reqc, pl, prdcr_name);
		if (!prdcr) {
			rc = errno;
			goto out;
		}
		is_new_prdcr = 1;
		rbn = rbt_find(&pl->prdcr_tree, prdcr_name);
		if (rbn) {
			ovis_log(NULL, OVIS_LINFO, "Producer %s does not exist, but " \
						    "it is unexpectedly " \
						    "in the producer list of " \
						    "producer_listen '%s'. \n",
						    prdcr_name, pl->obj.name);
			assert((rbn == NULL) && ("Node (rbn) unexpected in the tree"));
			/* Handle the case when assert() is disabled at compile time. */
			rbt_del(&pl->prdcr_tree, rbn);
			pl_pref = (ldmsd_prdcr_ref_t)container_of(rbn, struct ldmsd_prdcr_ref, rbn);
			free(pl_pref); /* No need to put back prdcr. It has disappeared from the cfgobj_tree */
		}
		pl_pref = prdcr_ref_new(prdcr);
		if (!pl_pref) {
			/* Completely remove producer as it was just created. */
			ldmsd_cfgobj_del(&prdcr->obj);
			goto enomem;
		}
		rbt_ins(&pl->prdcr_tree, &pl_pref->rbn);
	}
	/* Add the producer to any updaters that the producer matches */
	ldmsd_updtr_t updtr;
	ldmsd_name_match_t match;
	ldmsd_cfg_lock(LDMSD_CFGOBJ_UPDTR);
	for (updtr = ldmsd_updtr_first(); updtr; updtr = ldmsd_updtr_next(updtr)) {
		updtr_pref = ldmsd_updtr_prdcr_find(updtr, prdcr->obj.name);
		if (updtr_pref)
			continue;

		LIST_FOREACH(match, &updtr->prdcr_filter, entry) {
			if (0 == regexec(&match->regex, prdcr->obj.name, 0, NULL, 0)) {
				(void) __ldmsd_updtr_prdcr_add(updtr, prdcr);
				/*
				 * No need to handle errors.
				 * The call is to make sure that the producer
				 * has been added to an updater according to configuration.
				 */
				break;
			}
		}
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_UPDTR);

	if (pl->auto_start && is_new_prdcr) {
		if (prdcr->type == LDMSD_PRDCR_TYPE_ADVERTISED_PASSIVE) {
			prdcr->xprt = ldms_xprt_get(x, "prdcr");
			ldms_xprt_event_cb_set(prdcr->xprt, prdcr_connect_cb, prdcr);
			prdcr->conn_state = LDMSD_PRDCR_STATE_STANDBY;
		}
		rc = ldmsd_prdcr_start(prdcr_name, NULL, &sctxt);
		if (rc) {
			ovis_log(NULL, OVIS_LERROR, "failed to start the " \
						    "advertised producer %s. Error %d.\n",
						    prdcr_name, rc);
		}
	}
out:
	free(adv_hostname);
	free(adv_port);
	ldms_xprt_put(x, "process_advertisement");
	return rc;
einval:
	ovis_log(NULL, OVIS_LERROR,
			"The '%s' attribute is missing from "
			"an advertise request.\n", attr_name);
	reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
				"The attribute '%s' is missing from "
				"an advertise request to an aggregator.", attr_name);
	reqc->errcode = rc = EINVAL;
	goto out;
enomem:
	reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
				  "Aggregator has a memory allocation failure.");
	ovis_log(NULL, OVIS_LCRIT, "Memory allocation failure.\n");
	reqc->errcode = rc = ENOMEM;
	goto out;
}

/*
 * If the producer listen contains both hostname regex and CIDR IP address range,
 * the advertiser matches only when its hostname and IP address are matched
 * the prdcr_listen's hostname regex and IP range.
 */
int __is_advertiser_matched(ldmsd_prdcr_listen_t pl, struct ldms_addr *advts_addr,
						 const char *advts_hostname)
{
	int is_host_matched = 1;
	int is_ip_matched = 1;

	if (pl->hostname_regex_s) {
		if (0 != regexec(&pl->regex, advts_hostname, 0, NULL, 0))
			is_host_matched = 0;
	}

	if (pl->prefix_len) {
		if (advts_addr->sa_family == AF_INET) {
			struct ldms_addr s6 = {
				.addr ={0,0,0,0,0,0,0,0,0,0,0xff,0xff,0,0,0,0}
			};
			memcpy(&s6.addr[12], &advts_addr->addr, 4);
			memcpy(&advts_addr->addr, &s6.addr, 16);
			advts_addr->sa_family = AF_INET6;
		}
		/* A CIDR IP address was given. */
		if (0 == ldms_addr_in_network_addr(advts_addr, &pl->net_addr, pl->prefix_len))
			is_ip_matched = 0;
	}

	if (is_host_matched && is_ip_matched)
		return 1;
	else
		return 0;
}

static int advertise_handler(ldmsd_req_ctxt_t reqc)
{
	int rc = 0;
	ldmsd_prdcr_listen_t pl;
	char *hostname;
	char *name;
	hostname = name = NULL;
	struct ldms_addr rem_addr = {0};

	rc = ldms_xprt_addr(reqc->xprt->ldms.ldms, NULL, &rem_addr);
	if (rc) {
		reqc->errcode = rc;
		reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
						"An error %d occurred on the aggregator "
						"while processing the advertisement.", rc);
		goto send_reply;
	}

	hostname = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_HOST);
	if (!hostname) {
		reqc->errcode = EINVAL;
		reqc->line_off = snprintf(reqc->line_buf, reqc->line_len,
						"The attribute 'hostname' is required.");
		goto send_reply;
	}
	for (pl = (ldmsd_prdcr_listen_t)ldmsd_cfgobj_first(LDMSD_CFGOBJ_PRDCR_LISTEN);
			pl; pl = (ldmsd_prdcr_listen_t)ldmsd_cfgobj_next(&pl->obj))
	{
		if (pl->state != LDMSD_PRDCR_LISTEN_STATE_RUNNING)
			continue;
		if (__is_advertiser_matched(pl, &rem_addr, hostname)) {
			/* The hostname matches the regular expression. */
			reqc->errcode = __process_advertisement(reqc, pl, &rem_addr);
			if (reqc->errcode) {
				if (reqc->errcode == EBUSY) {
					snprintf(reqc->line_buf, reqc->line_len,
						"The client already has a running "
						"producer with the given name.");
				} else {
					snprintf(reqc->line_buf, reqc->line_len,
						"An error '%d' occurred on the peer.", reqc->errcode);
				}
			}
			ldmsd_cfgobj_put(&pl->obj, "iter"); /* Put back the 'first' or 'next' reference */
			goto send_reply;
		}
	}
	/*
	 * The advertisement doesn't match any listening producers
	 */
	reqc->errcode = ENOENT;
	snprintf(reqc->line_buf, reqc->line_len,
			"The given hostname '%s' doesn't match "
			"any `prdcr_listen`'s regex.", hostname);
	ovis_log(NULL, OVIS_LERROR, "Received a producer advertisement "
			"with hostname '%s', which isn't matched any listening producers. "
			"Stop the advertisement, update its configuration, and then restart.\n",
			hostname);
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	return rc;
}

static int advertiser_add_handler(ldmsd_req_ctxt_t reqc)
{
	ldmsd_prdcr_t prdcr;
	prdcr = __prdcr_add_handler(reqc, "advertiser_add", "advertiser");
	if (prdcr) {
		__dlog(DLOG_CFGOK, "advertiser_add name=%s xprt=%s host=%s port=%u "
			"auth=%s uid=%d gid=%d perm=%o\n",
			prdcr->obj.name, prdcr->xprt_name, prdcr->host_name,
			prdcr->port_no, prdcr->conn_auth_dom_name,
			(int)prdcr->obj.uid, (int)prdcr->obj.gid,
			(unsigned)prdcr->obj.perm);
	}

	ldmsd_send_req_response(reqc, reqc->line_buf);
	return 0;
}

static int advertiser_start_handler(ldmsd_req_ctxt_t reqc)
{
	int rc = 0;
	ldmsd_prdcr_t prdcr;
	char *name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);

	prdcr = ldmsd_prdcr_find(name);
	if (!prdcr) {
		prdcr = __prdcr_add_handler(reqc, "advertiser_start", "advertiser");
		if (!prdcr) {
			/*
			 * Failed to create the producer.
			 * The error message was prepared in __prdcr_add_handler()
			 */
			goto send_reply;
		}
	} else {
		ldmsd_prdcr_put(prdcr, "find");
	}

	rc = __prdcr_start_handler(reqc, "advertiser_start", "advertiser");
	if (CONFIG_PLAYBACK_ENABLED(DLOG_CFGOK)) {
		if (!rc && !reqc->errcode) {
			__dlog(DLOG_CFGOK, "advertiser_start "
				"name=%s xprt=%s host=%s port=%u "
				"reconnect=%ld auth=%s uid=%d gid=%d perm=%o\n",
				prdcr->obj.name, prdcr->xprt_name, prdcr->host_name,
				prdcr->port_no, prdcr->conn_intrvl_us, prdcr->conn_auth_dom_name,
				(int)prdcr->obj.uid, (int)prdcr->obj.gid,
				(unsigned)prdcr->obj.perm);
		}
	}

send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	return rc;
}

static int advertiser_stop_handler(ldmsd_req_ctxt_t reqc)
{
	int rc = __prdcr_stop_handler(reqc, "advertiser_stop", "advertiser");
	ldmsd_send_req_response(reqc, reqc->line_buf);
	return rc;
}

static int advertiser_del_handler(ldmsd_req_ctxt_t reqc)
{
	int rc = __prdcr_del_handler(reqc, "advertiser_del", "advertiser");
	ldmsd_send_req_response(reqc, reqc->line_buf);
	return rc;
}


/* -------------------- */
/* Quota Group (qgroup) */
/* -------------------- */

static int qgroup_config_handler(ldmsd_req_ctxt_t reqc)
{
	char *quota = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_QUOTA);
	char *reset_interval = ldmsd_req_attr_str_value_get_by_id(reqc,
						LDMSD_ATTR_RESET_INTERVAL);
	char *ask_interval = ldmsd_req_attr_str_value_get_by_id(reqc,
						LDMSD_ATTR_ASK_INTERVAL);
	char *ask_amount = ldmsd_req_attr_str_value_get_by_id(reqc,
						LDMSD_ATTR_ASK_AMOUNT);
	char *ask_mark = ldmsd_req_attr_str_value_get_by_id(reqc,
						LDMSD_ATTR_ASK_MARK);
	int rc = 0;
	uint64_t u64;
	int64_t s64;

	if (quota) {
		u64 = ovis_get_mem_size(quota);
		rc = ldms_qgroup_cfg_quota_set(u64);
		if (rc) {
			linebuf_printf(reqc,
				"qgroup quota set failed, "
				"set value: \"%s\", rc: %d", quota, rc);
			goto out;
		}
	}

	if (reset_interval) {
		rc = ovis_time_str2us(reset_interval, &s64);
		if (rc) {
			linebuf_printf(reqc,
				"Bad time format, set value: \"%s\", rc: %d",
				reset_interval, rc);
			goto out;
		}
		rc = ldms_qgroup_cfg_reset_usec_set(s64);
		if (rc) {
			linebuf_printf(reqc,
				"qgroup reset_interval set failed, "
				"set value: \"%s\", rc: %d",
				reset_interval, rc);
			goto out;
		}
	}

	if (ask_interval) {
		rc = ovis_time_str2us(ask_interval, &s64);
		if (rc) {
			linebuf_printf(reqc,
				"Bad time format, set value: \"%s\", rc: %d",
				ask_interval, rc);
			goto out;
		}
		rc = ldms_qgroup_cfg_ask_usec_set(s64);
		if (rc) {
			linebuf_printf(reqc,
				"qgroup ask_interval set failed, "
				"set value: \"%s\", rc: %d",
				ask_interval, rc);
			goto out;
		}
	}

	if (ask_amount) {
		s64 = ovis_get_mem_size(ask_amount);
		rc = ldms_qgroup_cfg_ask_amount_set(s64);
		if (rc) {
			linebuf_printf(reqc,
				"qgroup ask_amount set failed, "
				"set value: \"%s\", rc: %d",
				ask_amount, rc);
			goto out;
		}
	}

	if (ask_mark) {
		s64 = ovis_get_mem_size(ask_mark);
		rc = ldms_qgroup_cfg_ask_mark_set(s64);
		if (rc) {
			linebuf_printf(reqc,
				"qgroup ask_mark set failed, "
				"set value: \"%s\", rc: %d",
				ask_mark, rc);
			goto out;
		}
	}

	rc = 0;

 out:
	reqc->errcode = rc;
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(quota);
	free(reset_interval);
	free(ask_interval);
	free(ask_amount);
	free(ask_mark);
	return rc;
}


static int qgroup_member_add_handler(ldmsd_req_ctxt_t reqc)
{
	char *a_host = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_HOST);
	char *a_port = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_PORT);
	char *a_xprt = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_XPRT);
	char *a_auth = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_AUTH);
	ldmsd_auth_t auth = NULL;
	int rc;

	/* ldms_qgroup_member_add(); */

	/* host */
	if (!a_host) {
		rc = EINVAL;
		linebuf_printf(reqc, "Missing 'host' attribute.");
		goto out;
	}

	/* port is optional */

	/* xprt */
	if (!a_xprt) {
		rc = EINVAL;
		linebuf_printf(reqc, "Missing 'xprt' attribute.");
		goto out;
	}

	/* auth */
	if (a_auth) {
		auth = ldmsd_auth_find(a_auth);
		if (!auth) {
			rc = ENOENT;
			linebuf_printf(reqc,
				"Authentication domain '%s' not found, check"
				" the auth_add configuration.", a_auth);
			goto out;
		}
	} else {
		/* use default auth */
		auth = ldmsd_auth_default_get();
		assert(auth);
	}

	rc = ldms_qgroup_member_add(a_xprt, a_host, a_port,
				    auth->plugin, auth->attrs);
	switch (rc) {
	case 0:
		/* no-op */
		break;
	case EEXIST:
		linebuf_printf(reqc, "qgroup member '%s:%s' already existed",
				a_host, a_port?a_port:"411");
		goto out;
	default:
		linebuf_printf(reqc, "qgroup member add error: %d", rc);
		goto out;
	}

 out:
	reqc->errcode = rc;
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(a_host);
	free(a_port);
	free(a_xprt);
	free(a_auth);
	ldmsd_cfgobj_put(&auth->obj, "find");
	return rc;
}


static int qgroup_member_del_handler(ldmsd_req_ctxt_t reqc)
{
	char *a_host = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_HOST);
	char *a_port = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_PORT);
	int rc;

	/* host */
	if (!a_host) {
		rc = EINVAL;
		linebuf_printf(reqc, "Missing 'host' attribute.");
		goto out;
	}

	/* port is optional */
	rc = ldms_qgroup_member_del(a_host, a_port);
	if (rc == ENOENT) {
		linebuf_printf(reqc, "qgroup member '%s:%s' not found",
				a_host, a_port?a_port:"411");
		goto out;
	}

 out:
	reqc->errcode = rc;
	ldmsd_send_req_response(reqc, reqc->line_buf);
	free(a_host);
	free(a_port);
	return rc;
}


static int qgroup_start_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	rc = ldms_qgroup_start();
	if (rc) {
		linebuf_printf(reqc, "qgroup start error: %s(%d)",
				     ovis_errno_abbvr(rc), rc);
	}
	reqc->errcode = rc;
	ldmsd_send_req_response(reqc, reqc->line_buf);
	return rc;
}


static int qgroup_stop_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	rc = ldms_qgroup_stop();
	if (rc) {
		linebuf_printf(reqc, "qgroup stop error: %s(%d)",
				     ovis_errno_abbvr(rc), rc);
	}
	reqc->errcode = rc;
	ldmsd_send_req_response(reqc, reqc->line_buf);
	return rc;
}


#define __LB_PRINTF_RC_OUT( FMT, ... ) do { \
		rc = linebuf_printf(reqc, FMT, ## __VA_ARGS__); \
		if (rc) { \
			snprintf(ebuf, sizeof(ebuf), \
				"linebuf_printf() error: %d", rc); \
			goto out; \
		} \
	} while (0)
static int qgroup_info_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	char ebuf[256];
	ldms_qgroup_info_t qinfo = NULL;
	ldms_qgroup_member_info_t minfo;
	const char *sep = "";

	qinfo = ldms_qgroup_info_get();
	if (!qinfo) {
		rc = errno;
		snprintf(ebuf, sizeof(ebuf),
			 "ldms_qgroup_info_get() error: %s(%d)",
			 ovis_errno_abbvr(rc), rc);
		goto out;
	}

	__LB_PRINTF_RC_OUT("{");
	__LB_PRINTF_RC_OUT("\"state\":\"%s\"",
					ldms_qgroup_state_str(qinfo->state));
	__LB_PRINTF_RC_OUT(",\"quota\":%lu", qinfo->quota);
	__LB_PRINTF_RC_OUT(",\"config\":{");
		__LB_PRINTF_RC_OUT("\"quota\":%lu", qinfo->cfg.quota);
		__LB_PRINTF_RC_OUT(",\"ask_mark\":%lu", qinfo->cfg.ask_mark);
		__LB_PRINTF_RC_OUT(",\"ask_amount\":%lu", qinfo->cfg.ask_amount);
		__LB_PRINTF_RC_OUT(",\"ask_usec\":%lu", qinfo->cfg.ask_usec);
		__LB_PRINTF_RC_OUT(",\"reset_usec\":%lu", qinfo->cfg.reset_usec);
	__LB_PRINTF_RC_OUT("}"); /* config */
	__LB_PRINTF_RC_OUT(",\"members\":[");
	STAILQ_FOREACH(minfo, &qinfo->member_stq, entry) {
		__LB_PRINTF_RC_OUT("%s{", sep);
		__LB_PRINTF_RC_OUT("\"state\":\"%s\"",
				ldms_qgroup_member_state_str(minfo->state));
		__LB_PRINTF_RC_OUT(",\"host\":\"%s\"", minfo->c_host);
		__LB_PRINTF_RC_OUT(",\"port\":\"%s\"", minfo->c_port);
		__LB_PRINTF_RC_OUT(",\"xprt\":\"%s\"", minfo->c_xprt);
		__LB_PRINTF_RC_OUT(",\"auth\":\"%s\"", minfo->c_auth);
		if (minfo->c_auth_av_list) {
			int i;
			static struct attr_value *av;
			__LB_PRINTF_RC_OUT(",\"auth_options\":{");
			for (i = 0; i < minfo->c_auth_av_list->count; i++) {
				av = &minfo->c_auth_av_list->list[i];
				__LB_PRINTF_RC_OUT("%s\"%s\":\"%s\"",
						i?",":"", av->name, av->value);
			}
			__LB_PRINTF_RC_OUT("}");
		}
		__LB_PRINTF_RC_OUT("}");
		sep = ",";
	}
	__LB_PRINTF_RC_OUT("]"); /* members */
	__LB_PRINTF_RC_OUT("}"); /* doc */

	rc = 0;

 out:
	if (qinfo)
		ldms_qgroup_info_free(qinfo);
	reqc->errcode = rc;
	if (rc) {
		ldmsd_send_req_response(reqc, ebuf);
	} else {
		ldmsd_send_req_response(reqc, reqc->line_buf);
	}
	return rc;
}
