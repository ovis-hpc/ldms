/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015-2017 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2015-2017 Sandia Corporation. All rights reserved.
 *
 * Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
 * license for use of this work by or on behalf of the U.S. Government.
 * Export of this program may require a license from the United States
 * Government.
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
#include "ldmsd.h"
#include "ldmsd_request.h"

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
static char *cfg_resp_buf;
static size_t cfg_resp_buf_sz;

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
#define LINE_BUF_LEN 1024
#define REQ_BUF_LEN 4096
#define REP_BUF_LEN REQ_BUF_LEN

typedef int
(*ldmsd_request_handler_t)(ldmsd_req_ctxt_t req_ctxt);
struct request_handler_entry {
	int req_id;
	ldmsd_request_handler_t handler;
};

static int example_handler(ldmsd_req_ctxt_t req_ctxt);
static int prdcr_add_handler(ldmsd_req_ctxt_t req_ctxt);
static int prdcr_del_handler(ldmsd_req_ctxt_t req_ctxt);
static int prdcr_start_handler(ldmsd_req_ctxt_t req_ctxt);
static int prdcr_stop_handler(ldmsd_req_ctxt_t req_ctxt);
static int prdcr_start_regex_handler(ldmsd_req_ctxt_t req_ctxt);
static int prdcr_stop_regex_handler(ldmsd_req_ctxt_t req_ctxt);
static int prdcr_status_handler(ldmsd_req_ctxt_t req_ctxt);
static int prdcr_set_handler(ldmsd_req_ctxt_t req_ctxt);
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
static int unimplemented_handler(ldmsd_req_ctxt_t req_ctxt);

static struct request_handler_entry request_handler[] = {
	[LDMSD_EXAMPLE_REQ]      = { LDMSD_EXAMPLE_REQ, example_handler },
	[LDMSD_PRDCR_ADD_REQ]    = { LDMSD_PRDCR_ADD_REQ, prdcr_add_handler },
	[LDMSD_PRDCR_DEL_REQ]    = { LDMSD_PRDCR_DEL_REQ, prdcr_del_handler },
	[LDMSD_PRDCR_START_REQ]  = { LDMSD_PRDCR_START_REQ, prdcr_start_handler },
	[LDMSD_PRDCR_STOP_REQ]  = { LDMSD_PRDCR_STOP_REQ, prdcr_stop_handler },
	[LDMSD_PRDCR_STATUS_REQ] = { LDMSD_PRDCR_STATUS_REQ, prdcr_status_handler },
	[LDMSD_PRDCR_SET_REQ] = { LDMSD_PRDCR_SET_REQ, prdcr_set_handler },
	[LDMSD_PRDCR_START_REGEX_REQ] = { LDMSD_PRDCR_START_REGEX_REQ, prdcr_start_regex_handler },
	[LDMSD_PRDCR_STOP_REGEX_REQ]  = { LDMSD_PRDCR_STOP_REGEX_REQ, prdcr_stop_regex_handler },
	[LDMSD_STRGP_ADD_REQ]    = { LDMSD_STRGP_ADD_REQ, strgp_add_handler },
	[LDMSD_STRGP_DEL_REQ]    = { LDMSD_STRGP_DEL_REQ, strgp_del_handler },
	[LDMSD_STRGP_PRDCR_ADD_REQ]   = { LDMSD_STRGP_PRDCR_ADD_REQ, strgp_prdcr_add_handler },
	[LDMSD_STRGP_PRDCR_DEL_REQ]   = { LDMSD_STRGP_PRDCR_DEL_REQ, strgp_prdcr_del_handler },
	[LDMSD_STRGP_METRIC_ADD_REQ]  = { LDMSD_STRGP_METRIC_ADD_REQ, strgp_metric_add_handler },
	[LDMSD_STRGP_METRIC_DEL_REQ]   = { LDMSD_STRGP_METRIC_DEL_REQ, strgp_metric_del_handler },
	[LDMSD_STRGP_START_REQ]  = { LDMSD_STRGP_START_REQ, strgp_start_handler },
	[LDMSD_STRGP_STOP_REQ]   = { LDMSD_STRGP_STOP_REQ, strgp_stop_handler },
	[LDMSD_STRGP_STATUS_REQ] = { LDMSD_STRGP_STATUS_REQ, strgp_status_handler },
	[LDMSD_UPDTR_ADD_REQ]    = { LDMSD_UPDTR_ADD_REQ, updtr_add_handler },
	[LDMSD_UPDTR_DEL_REQ]    = { LDMSD_UPDTR_DEL_REQ, updtr_del_handler },
	[LDMSD_UPDTR_PRDCR_ADD_REQ]   = { LDMSD_UPDTR_PRDCR_ADD_REQ, updtr_prdcr_add_handler },
	[LDMSD_UPDTR_PRDCR_DEL_REQ]   = { LDMSD_UPDTR_PRDCR_DEL_REQ, updtr_prdcr_del_handler },
	[LDMSD_UPDTR_START_REQ]  = { LDMSD_UPDTR_START_REQ, updtr_start_handler },
	[LDMSD_UPDTR_STOP_REQ]   = { LDMSD_UPDTR_STOP_REQ, updtr_stop_handler },
	[LDMSD_UPDTR_MATCH_ADD_REQ]   = { LDMSD_UPDTR_MATCH_ADD_REQ, updtr_match_add_handler },
	[LDMSD_UPDTR_MATCH_DEL_REQ]   = { LDMSD_UPDTR_MATCH_DEL_REQ, updtr_match_del_handler },
	[LDMSD_UPDTR_STATUS_REQ] = { LDMSD_UPDTR_STATUS_REQ, updtr_status_handler },
	[LDMSD_PLUGN_START_REQ] = { LDMSD_PLUGN_START_REQ, plugn_start_handler },
	[LDMSD_PLUGN_STOP_REQ] = { LDMSD_PLUGN_STOP_REQ, plugn_stop_handler },
	[LDMSD_PLUGN_STATUS_REQ] = { LDMSD_PLUGN_STATUS_REQ, plugn_status_handler },
	[LDMSD_PLUGN_LOAD_REQ] = { LDMSD_PLUGN_LOAD_REQ, plugn_load_handler },
	[LDMSD_PLUGN_TERM_REQ] = { LDMSD_PLUGN_TERM_REQ, plugn_term_handler },
	[LDMSD_PLUGN_CONFIG_REQ] = { LDMSD_PLUGN_CONFIG_REQ, plugn_config_handler },
	[LDMSD_PLUGN_LIST_REQ] = { LDMSD_PLUGN_LIST_REQ, plugn_list_handler },
	[LDMSD_SET_UDATA_REQ] = { LDMSD_SET_UDATA_REQ, set_udata_handler },
	[LDMSD_SET_UDATA_REGEX_REQ] = { LDMSD_SET_UDATA_REGEX_REQ, set_udata_regex_handler },
	[LDMSD_VERBOSE_REQ] = { LDMSD_VERBOSE_REQ, verbosity_change_handler },
	[LDMSD_DAEMON_STATUS_REQ] = { LDMSD_DAEMON_STATUS_REQ, daemon_status_handler },
	[LDMSD_VERSION_REQ] = { LDMSD_VERSION_REQ, version_handler },
	[LDMSD_ENV_REQ] = { LDMSD_ENV_REQ, env_handler },
	[LDMSD_INCLUDE_REQ] = { LDMSD_INCLUDE_REQ, include_handler },
	[LDMSD_ONESHOT_REQ] = { LDMSD_ONESHOT_REQ, oneshot_handler },
	[LDMSD_LOGROTATE_REQ] = { LDMSD_LOGROTATE_REQ, logrotate_handler },
	[LDMSD_EXIT_DAEMON_REQ] = { LDMSD_EXIT_DAEMON_REQ, exit_daemon_handler },
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

void free_req_ctxt(ldmsd_req_ctxt_t reqc)
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

ldmsd_req_ctxt_t alloc_req_ctxt(struct req_ctxt_key *key)
{
	ldmsd_req_ctxt_t reqc;
	reqc = calloc(1, sizeof *reqc);
	if (!reqc)
		return NULL;
	reqc->line_len = LINE_BUF_LEN;
	reqc->line_buf = malloc(LINE_BUF_LEN);
	if (!reqc->line_buf)
		goto err;
	reqc->req_len = REQ_BUF_LEN;
	reqc->req_off = 0;
	reqc->req_buf = malloc(REQ_BUF_LEN);
	if (!reqc->req_buf)
		goto err;
	reqc->req_buf[0] = '\0';
	reqc->rep_len = REP_BUF_LEN;
	reqc->rep_off = 0;
	reqc->rep_buf = malloc(REP_BUF_LEN);
	if (!reqc->rep_buf)
		goto err;
	reqc->rep_buf[0] = '\0';
	reqc->key = *key;
	rbn_init(&reqc->rbn, &reqc->key);
	rbt_ins(&msg_tree, &reqc->rbn);
	return reqc;
 err:
	free_req_ctxt(reqc);
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

static int gather_data(ldmsd_req_ctxt_t reqc, struct msghdr *msg, size_t msg_len)
{
	int i;
	size_t copylen;
	size_t copyoff;
	ldmsd_req_hdr_t request = msg->msg_iov[0].iov_base;
	size_t remaining = reqc->req_len - reqc->req_off;
	if (msg_len > remaining) {
		size_t new_size = msg_len + REQ_BUF_LEN;
		if (new_size < reqc->req_len + msg_len)
			new_size = reqc->req_len + msg_len;
		reqc->req_buf = realloc(reqc->req_buf, new_size);
		if (!reqc->req_buf)
			return ENOMEM;
		reqc->req_len = new_size;
	}
	copyoff = sizeof(*request);
	for (i = 0; i < msg->msg_iovlen; i++) {
		if (msg->msg_iov[i].iov_len <= copyoff) {
			copyoff -= msg->msg_iov[i].iov_len;
			continue;
		}
		copylen = msg->msg_iov[i].iov_len - copyoff;
		memcpy(&reqc->req_buf[reqc->req_off],
		       msg->msg_iov[i].iov_base + copyoff,
		       copylen);
		reqc->req_off += copylen;
		copyoff = 0;
	}
	reqc->req_buf[reqc->req_off] = '\0';
	return 0;
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

int ldmsd_handle_request(ldmsd_req_hdr_t request, ldmsd_req_ctxt_t reqc)
{
	int rc;
	/* Check for request id outside of range */
	if (request->code < 0 ||
	    request->code >= (sizeof(request_handler)/sizeof(request_handler[0])))
		rc = unimplemented_handler(reqc);
	else if (!request_handler[request->code].handler)
		/* Check for unimplemented request */
		rc = unimplemented_handler(reqc);
	else
		rc = request_handler[request->code].handler(reqc);
	return rc;
}

size_t Snprintf(char **dst, size_t *len, char *fmt, ...);

static int
send_request_reply_outband(struct ldmsd_req_ctxt *reqc,
		   char *data, size_t data_len,
		   int msg_flags)
{
	struct ldmsd_req_hdr_s req_reply;
	struct msghdr reply_msg;
	struct iovec iov[2];
	size_t remaining;

	do {
		remaining = reqc->rep_len - reqc->rep_off;
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
			msg_flags =
				(reqc->rec_no == 0?LDMSD_REQ_SOM_F:0)
				| (msg_flags & LDMSD_REQ_EOM_F);
			/* Record is full, send it on it's way */
			memcpy(&req_reply, &reqc->rh, sizeof reqc->rh);
			req_reply.flags = msg_flags;
			req_reply.rec_len = reqc->rep_off + sizeof(req_reply);
			req_reply.code = reqc->errcode;
			req_reply.type = LDMSD_REQ_TYPE_CONFIG_RESP;

			reply_msg.msg_name = reqc->mh->msg_name;
			reply_msg.msg_namelen = reqc->mh->msg_namelen;
			iov[0].iov_base = &req_reply;
			iov[0].iov_len = sizeof(req_reply);
			iov[1].iov_base = reqc->rep_buf;
			iov[1].iov_len = reqc->rep_off,
			reply_msg.msg_iov = iov;
			reply_msg.msg_iovlen = 2;
			reply_msg.msg_control = NULL;
			reply_msg.msg_controllen = 0;
			reply_msg.msg_flags = 0;
			if (sendmsg(reqc->dest_fd, &reply_msg, 0) < 0)
				return errno;
			reqc->rec_no ++;
			reqc->rep_off = 0;
			reqc->rep_buf[0] = '\0';
		}
	} while (data_len);

	return 0;
}

int
send_request_reply_intband(struct ldmsd_req_ctxt *reqc,
			   char *data, size_t data_len,
			   int msg_flags)
{
	struct ldmsd_req_hdr_s *req_reply;
	size_t remaining, len;
	int rc;

	do {
		remaining = reqc->rep_len - reqc->rep_off;
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
			msg_flags = (reqc->rec_no == 0?LDMSD_REQ_SOM_F:0)
						| (msg_flags & LDMSD_REQ_EOM_F);
			len = sizeof(*req_reply) + reqc->rep_off;
			if (cfg_resp_buf_sz < len) {
				if (cfg_resp_buf)
					free(cfg_resp_buf);
				cfg_resp_buf = calloc(1, len);
				if (!cfg_resp_buf) {
					ldmsd_log(LDMSD_LERROR, "Out of memory\n");
					return ENOMEM;
				}
				cfg_resp_buf_sz = len;
			}
			req_reply = (ldmsd_req_hdr_t)cfg_resp_buf;
			/* Record is full, send it on it's way */
			memcpy(req_reply, &reqc->rh, sizeof reqc->rh);
			req_reply->flags = msg_flags;
			req_reply->rec_len = reqc->rep_off + sizeof(req_reply);
			req_reply->code = reqc->errcode;
			req_reply->type = LDMSD_REQ_TYPE_CONFIG_RESP;

			memcpy(cfg_resp_buf + sizeof(*req_reply),
					reqc->rep_buf, reqc->rep_off);
			rc = ldms_xprt_send(reqc->ldms, cfg_resp_buf, len);
			if (rc) {
				ldmsd_log(LDMSD_LERROR, "Failed to send "
						"the config response back\n");
				free(req_reply);
				return rc;
			}
			reqc->rec_no ++;
			reqc->rep_off = 0;
			reqc->rep_buf[0] = '\0';
		}
	} while (data_len);

	return 0;
}

int process_request_unix_domain(int fd, struct msghdr *msg, size_t msg_len)
{
	ldmsd_req_hdr_t request = msg->msg_iov[0].iov_base;
	struct req_ctxt_key key;
	ldmsd_req_ctxt_t reqc = NULL;
	int rc;
	size_t cnt = 0;

	key.msg_no = request->msg_no;
	key.conn_id = fd;

	req_ctxt_tree_lock();
	if (request->flags & LDMSD_REQ_SOM_F) {
		/* Ensure that we don't already have this message in
		 * the tree */
		reqc = find_req_ctxt(&key);
		if (reqc) {
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				  "Duplicate message number %d:%d received",
				  key.msg_no, key.conn_id);
			goto err_out;
		}
		reqc = alloc_req_ctxt(&key);
		if (reqc) {
			memcpy(&reqc->rh, request, sizeof(*request));
			reqc->resp_handler = send_request_reply_outband;
			reqc->dest_fd = fd;
		} else {
			ldmsd_log(LDMSD_LERROR, "process_request_unix_domain: out of memory\n");
		}

	} else {
		reqc = find_req_ctxt(&key);
		if (!reqc) {
			ldmsd_log(LDMSD_LERROR, "process_request_unix_domain:"
					"The message no %d:%d was not found.\n",
					key.msg_no, key.conn_id);
		}
	}
	if (!reqc)
		goto err_out;

	if (gather_data(reqc, msg, msg_len))
		goto err_out;

	req_ctxt_tree_unlock();
	if (0 == (request->flags & LDMSD_REQ_EOM_F))
		return 0;

	reqc->mh = msg;

	if (request->marker != LDMSD_RECORD_MARKER) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Received an invalid cfg request");
		goto out;
	}

	/* Check for request id outside of range */
	rc = ldmsd_handle_request(request, reqc);
err_out:
	pthread_mutex_unlock(&msg_tree_lock);
out:
	if (cnt && reqc) {
		ldmsd_log(LDMSD_LERROR, "%s\n", reqc->line_buf);
		rc = reqc->resp_handler(reqc, reqc->line_buf, cnt,
					LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	}
	if (reqc) {
		req_ctxt_tree_lock();
		free_req_ctxt(reqc);
		req_ctxt_tree_unlock();
	}
	return rc;
}

int process_request_ldms_xprt(ldms_t x, ldmsd_req_hdr_t request, char *data,
							size_t data_len)
{
	struct req_ctxt_key key;
	ldmsd_req_ctxt_t reqc = NULL;
	int rc;
	size_t cnt = 0;

	key.msg_no = request->msg_no;
	key.conn_id = (uint64_t)(long unsigned)x;

	req_ctxt_tree_lock();
	if (request->flags & LDMSD_REQ_SOM_F) {
		/* Ensure that we don't already have this message in
		 * the tree */
		reqc = find_req_ctxt(&key);
		if (reqc) {
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				  "Duplicate message number %d:%" PRIu64 "received",
				  key.msg_no, key.conn_id);
			goto err_out;
		}
		reqc = alloc_req_ctxt(&key);
		if (reqc) {
			memcpy(&reqc->rh, request, sizeof(*request));
			reqc->resp_handler = send_request_reply_intband;
			reqc->ldms = x;
		} else {
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
					"ldmsd out of memory.");
		}

	} else {
		reqc = find_req_ctxt(&key);
		if (!reqc) {
			ldmsd_log(LDMSD_LERROR, "The message no %d:%d was not found.",
				key.msg_no, key.conn_id);
		}
	}
	if (!reqc)
		goto err_out;

	if (reqc->req_len < data_len) {
		reqc->req_buf = realloc(reqc->req_buf, reqc->req_len + data_len);
		if (!reqc->req_buf) {
			free_req_ctxt(reqc);
			return ENOMEM;
		}
		reqc->req_len += data_len;
	}
	memcpy(reqc->req_buf + reqc->req_off, data, data_len);
	reqc->req_off += data_len;
	req_ctxt_tree_unlock();
	if (0 == (request->flags & LDMSD_REQ_EOM_F))
		return 0;

	if (request->marker != LDMSD_RECORD_MARKER) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Received an invalid cfg request");
		goto out;
	}

	/* Check for request id outside of range */
	rc = ldmsd_handle_request(request, reqc);
err_out:
	pthread_mutex_unlock(&msg_tree_lock);
out:
	if (cnt && reqc) {
		ldmsd_log(LDMSD_LERROR, "%s\n", reqc->line_buf);
		rc = reqc->resp_handler(reqc, reqc->line_buf, cnt,
					LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	}
	if (reqc) {
		req_ctxt_tree_lock();
		free_req_ctxt(reqc);
		req_ctxt_tree_unlock();
	}
	return rc;
}

size_t Snprintf(char **dst, size_t *len, char *fmt, ...)
{
	va_list ap;
	va_list ap_copy;
	size_t cnt;

	if (!*dst) {
		*dst = malloc(1024);
		*len = 1024;
	}

	va_start(ap, fmt);
	va_copy(ap_copy, ap);
	va_end(ap_copy);
	while (1) {
		cnt = vsnprintf(*dst, *len, fmt, ap);
		if (cnt >= *len) {
			free(*dst);
			*len = cnt * 2;
			*dst = malloc(*len);
			assert(*dst);
			continue;
		}
		break;
	}
	va_end(ap);
	return cnt;
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
static int example_handler(ldmsd_req_ctxt_t reqc)
{
	size_t cnt;
	int rc, count = 0;
	reqc->errcode = 0;
	ldmsd_req_attr_t attr = (ldmsd_req_attr_t)reqc->req_buf;
	rc = reqc->resp_handler(reqc, "[", 1, LDMSD_REQ_SOM_F);
	while (attr->discrim) {
		if (count)
			rc = reqc->resp_handler(reqc, ",\n", 2, 0);

		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "{ \"attr_len\":%d,"
			       "\"attr_id\":%d,"
			       "\"attr_value\": \"%s\" }",
			       attr->attr_len,
			       attr->attr_id,
			       (char *)attr->attr_value);
		rc = reqc->resp_handler(reqc, reqc->line_buf, cnt, 0);
		count++;
		attr = (ldmsd_req_attr_t)&attr->attr_value[attr->attr_len];
	}
	rc = reqc->resp_handler(reqc, "]", 1, LDMSD_REQ_EOM_F);
	return rc;
}

extern char *ldmsd_req_attr_value_get_by_name(char *attr_list, const char *name);

static int prdcr_add_handler(ldmsd_req_ctxt_t reqc)
{
	ldmsd_prdcr_t prdcr;
	char *name, *host, *xprt, *attr_name, *type_s, *port_s, *interval_s;
	name = host = xprt = type_s = port_s = interval_s = NULL;
	enum ldmsd_prdcr_type type = -1;
	unsigned short port_no = 0;
	int interval_us = -1;
	size_t cnt = 0;
	reqc->errcode = 0;

	attr_name = "name";
	name = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!name)
		goto einval;

	attr_name = "type";
	type_s = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!type_s) {
		goto einval;
	} else {
		type = ldmsd_prdcr_str2type(type_s);
		if (type < 0) {
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
	xprt = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!xprt)
		goto einval;

	attr_name = "host";
	host = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!host)
		goto einval;

	attr_name = "port";
	port_s = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!port_s) {
		goto einval;
	} else {
		port_no = strtol(port_s, NULL, 0);
	}

	attr_name = "interval";
	interval_s = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!interval_s) {
		goto einval;
	} else {
		 interval_us = strtol(interval_s, NULL, 0);
	}

out:
	prdcr = ldmsd_prdcr_new(name, xprt, host, port_no, type, interval_us);
	if (!prdcr) {
		if (errno == EEXIST)
			goto eexist;
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
einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"This attribute '%s' is required.", attr_name);
send_reply:
	(void) reqc->resp_handler(reqc, reqc->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);

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
	return 0;
}

static int prdcr_del_handler(ldmsd_req_ctxt_t reqc)
{
	char *name = NULL, *attr_name;
	size_t cnt = 0;
	reqc->errcode = 0;

	attr_name = "name";
	name = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!name) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"This attribute '%s' is required.", attr_name);
		goto send_reply;
	}

	reqc->errcode = ldmsd_prdcr_del(name);
	if (reqc->errcode == ENOENT) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The producer specified does not exist.");
	} else if (reqc->errcode == EBUSY) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"%dThe producer is in use.");
	}

send_reply:
	(void) reqc->resp_handler(reqc, reqc->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	if (name)
		free(name);
	return 0;
}

static int prdcr_start_handler(ldmsd_req_ctxt_t reqc)
{
	char *name, *interval_str;
	name = interval_str = NULL;
	size_t cnt = 0;
	ldmsd_req_attr_t attr;
	reqc->errcode = 0;

	name = ldmsd_req_attr_value_get_by_name(reqc->req_buf, "name");
	if (!name) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"This attribute 'name' is required.");
		goto send_reply;
	}

	interval_str = ldmsd_req_attr_value_get_by_name(reqc->req_buf, "interval");

	reqc->errcode = ldmsd_prdcr_start(name, interval_str);
	if (reqc->errcode == EBUSY) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The producer is already running.");
	} else if (reqc->errcode == ENOENT) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The producer specified does not exist.");
	}

send_reply:
	(void) reqc->resp_handler(reqc, reqc->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
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
	ldmsd_req_attr_t attr;
	reqc->errcode = 0;

	name = ldmsd_req_attr_value_get_by_name(reqc->req_buf, "name");
	if (!name) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"This attribute 'name' is required.");
		goto send_reply;
	}

	reqc->errcode = ldmsd_prdcr_stop(name);
	if (reqc->errcode == EBUSY) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The producer is already stopped.");
	} else if (reqc->errcode == ENOENT) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The producer specified does not exist.");
	}

send_reply:
	(void) reqc->resp_handler(reqc, reqc->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	if (name)
		free(name);
	return 0;
}

static int prdcr_start_regex_handler(ldmsd_req_ctxt_t reqc)
{
	char *prdcr_regex, *interval_str;
	prdcr_regex = interval_str = NULL;
	size_t cnt = 0;
	ldmsd_req_attr_t attr;
	reqc->errcode = 0;

	prdcr_regex = ldmsd_req_attr_value_get_by_name(reqc->req_buf, "regex");
	if (!prdcr_regex) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"This attribute 'regex' is required.");
		goto send_reply;
	}

	interval_str = ldmsd_req_attr_value_get_by_name(reqc->req_buf, "interval");

	reqc->errcode = ldmsd_prdcr_start_regex(prdcr_regex, interval_str,
					reqc->line_buf, reqc->line_len);
	if (reqc->errcode)
		cnt = sizeof(reqc->line_buf) + 1;

send_reply:
	(void) reqc->resp_handler(reqc, reqc->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
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
	ldmsd_req_attr_t attr;
	reqc->errcode = 0;

	prdcr_regex = ldmsd_req_attr_value_get_by_name(reqc->req_buf, "regex");
	if (!prdcr_regex) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"This attribute 'regex' is required.");
		goto send_reply;
	}
	reqc->errcode = ldmsd_prdcr_stop_regex(prdcr_regex,
				reqc->line_buf, reqc->line_len);
	if (reqc->errcode)
		cnt = sizeof(reqc->line_buf) + 1;

send_reply:
	(void) reqc->resp_handler(reqc, reqc->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	if (prdcr_regex)
		free(prdcr_regex);
	return 0;
}

static int prdcr_status_handler(ldmsd_req_ctxt_t reqc)
{
	extern const char *prdcr_state_str(enum ldmsd_prdcr_state state);
	ldmsd_prdcr_t prdcr;
	size_t cnt;
	int rc, count = 0;
	reqc->errcode = 0;

	ldmsd_cfg_lock(LDMSD_CFGOBJ_PRDCR);
	rc = reqc->resp_handler(reqc, "[", 1, LDMSD_REQ_SOM_F);
	for (prdcr = ldmsd_prdcr_first(); prdcr;
	     prdcr = ldmsd_prdcr_next(prdcr)) {

		if (count)
			rc = reqc->resp_handler(reqc, ",\n", 2, 0);

		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "{ \"name\":\"%s\","
			       "\"host\":\"%s\","
			       "\"port\":%hu,"
			       "\"transport\":\"%s\","
			       "\"state\":\"%s\","
			       "\"sets\": [",
			       prdcr->obj.name,
			       prdcr->host_name, prdcr->port_no, prdcr->xprt_name,
			       prdcr_state_str(prdcr->conn_state));
		rc = reqc->resp_handler(reqc, reqc->line_buf, cnt, 0);

		ldmsd_prdcr_lock(prdcr);
		ldmsd_prdcr_set_t prv_set;
		int set_count = 0;
		for (prv_set = ldmsd_prdcr_set_first(prdcr); prv_set;
		     prv_set = ldmsd_prdcr_set_next(prv_set)) {

			if (set_count)
				rc = reqc->resp_handler(reqc, ",\n", 2, 0);

			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				       "{ \"inst_name\":\"%s\","
				       "\"schema_name\":\"%s\","
				       "\"state\":\"%s\"}",
				       prv_set->inst_name,
				       (prv_set->schema_name ? prv_set->schema_name : ""),
				       ldmsd_prdcr_set_state_str(prv_set->state));
			rc = reqc->resp_handler(reqc, reqc->line_buf, cnt, 0);
			set_count++;
		}
		ldmsd_prdcr_unlock(prdcr);
		rc = reqc->resp_handler(reqc, "]}", 2, 0);
		count++;
	}
	rc = reqc->resp_handler(reqc, "]", 1, LDMSD_REQ_EOM_F);
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR);
	return rc;
}

size_t __prdcr_set_status(ldmsd_req_ctxt_t reqc, ldmsd_prdcr_set_t prd_set)
{
	struct ldms_timestamp ts, dur;
	ts = ldms_transaction_timestamp_get(prd_set->set);
	dur = ldms_transaction_duration_get(prd_set->set);
	return Snprintf(&reqc->line_buf, &reqc->line_len,
		"{ "
		"\"inst_name\":\"%s\","
		"\"schema_name\":\"%s\","
		"\"state\":\"%s\","
		"\"origin\":\"%s\","
		"\"producer\":\"%s\","
		"\"timestamp.sec\":\"%d\","
		"\"timestamp.usec\":\"%d\","
		"\"duration.sec\":\"%d\","
		"\"duration.usec\":\"%d\""
		"}",
		prd_set->inst_name, prd_set->schema_name,
		ldmsd_prdcr_set_state_str(prd_set->state),
		ldms_set_producer_name_get(prd_set->set),
		prd_set->prdcr->obj.name,
		ts.sec, ts.usec,
		dur.sec, dur.usec);
}

/* This function must be called with producer lock held */
int __prdcr_set_status_handler(ldmsd_req_ctxt_t reqc, ldmsd_prdcr_t prdcr,
			int *count, const char *setname, const char *schema)
{
	int rc;
	size_t cnt;
	ldmsd_prdcr_set_t prd_set;

	if (setname) {
		prd_set = ldmsd_prdcr_set_find(prdcr, setname);
		if (!prd_set)
			return 0;
		if (schema && (0 != strcmp(prd_set->schema_name, schema)))
			return 0;
		if (*count)
			rc = reqc->resp_handler(reqc, ",\n", 2, 0);
		cnt = __prdcr_set_status(reqc, prd_set);
		rc = reqc->resp_handler(reqc, reqc->line_buf, cnt, 0);
		(*count)++;
	} else {
		for (prd_set = ldmsd_prdcr_set_first(prdcr); prd_set;
			prd_set = ldmsd_prdcr_set_next(prd_set)) {
			if (schema && (0 != strcmp(prd_set->schema_name, schema)))
				continue;

			if (*count)
				rc = reqc->resp_handler(reqc, ",\n", 2, 0);
			cnt = __prdcr_set_status(reqc, prd_set);
			rc = reqc->resp_handler(reqc, reqc->line_buf, cnt, 0);
			(*count)++;
		}
	}
	return rc;
}

static int prdcr_set_handler(ldmsd_req_ctxt_t reqc)
{
	char *prdcr_name, *setname, *schema;
	prdcr_name = setname = schema = NULL;
	ldmsd_prdcr_t prdcr = NULL;
	ldmsd_req_attr_t attr;
	size_t cnt = 0;
	int rc, count = 0;
	reqc->errcode = 0;

	prdcr_name = ldmsd_req_attr_value_get_by_name(reqc->req_buf, "producer");
	setname = ldmsd_req_attr_value_get_by_name(reqc->req_buf, "instance");
	schema = ldmsd_req_attr_value_get_by_name(reqc->req_buf, "schema");

	rc = reqc->resp_handler(reqc, "[", 1, LDMSD_REQ_SOM_F);
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
	rc = reqc->resp_handler(reqc, "]", 1, LDMSD_REQ_EOM_F);
	if (prdcr_name)
		free(prdcr_name);
	if (setname)
		free(setname);
	if (schema)
		free(schema);
	return rc;
}

static int strgp_add_handler(ldmsd_req_ctxt_t reqc)
{
	char *attr_name, *name, *plugin, *container, *schema;
	name = plugin = container = schema = NULL;
	ldmsd_req_attr_t attr;
	size_t cnt = 0;
	reqc->errcode = 0;

	attr_name = "name";
	name = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!name)
		goto einval;

	attr_name = "plugin";
	plugin = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!plugin)
		goto einval;

	attr_name = "container";
	container = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!container)
		goto einval;

	attr_name = "schema";
	schema = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!schema)
		goto einval;

	struct ldmsd_plugin_cfg *store;
	store = ldmsd_get_plugin(plugin);
	if (!store) {
		reqc->errcode = ENOENT;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
					"The plugin does not exist.\n");
		goto send_reply;
	}

	ldmsd_strgp_t strgp = ldmsd_strgp_new(name);
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
			"This attribute '%s' is required.", attr_name);
send_reply:
	(void) reqc->resp_handler(reqc, reqc->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	if (name)
		free(name);
	if (plugin)
		free(plugin);
	if (container)
		free(container);
	if (schema)
		free(schema);
	return 0;
}

static int strgp_del_handler(ldmsd_req_ctxt_t reqc)
{
	char *name = NULL;
	ldmsd_req_attr_t attr;
	size_t cnt = 0;
	reqc->errcode = 0;

	name = ldmsd_req_attr_value_get_by_name(reqc->req_buf, "name");
	if (!name) {
		reqc->errcode= EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"This attribute 'name' is required.");
		goto send_reply;
	}

	reqc->errcode = ldmsd_strgp_del(name);
	if (reqc->errcode == ENOENT) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The storage policy specified does not exist.");
	} else if (reqc->errcode == EBUSY) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The storage policy is in use.");
	}
send_reply:
	(void) reqc->resp_handler(reqc, reqc->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	if (name)
		free(name);
	return 0;
}

static int strgp_prdcr_add_handler(ldmsd_req_ctxt_t reqc)
{
	char *name, *regex_str, *attr_name;
	name = regex_str = NULL;
	ldmsd_req_attr_t attr;
	size_t cnt = 0;
	reqc->errcode = 0;

	attr_name = "name";
	name = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!name)
		goto einval;

	attr_name = "regex";
	regex_str = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!regex_str)
		goto einval;

	reqc->errcode = ldmsd_strgp_prdcr_add(name, regex_str,
				reqc->line_buf, reqc->line_len);
	if (reqc->errcode) {
		if (reqc->errcode == ENOENT) {
			reqc->errcode = Snprintf(&reqc->line_buf, &reqc->line_len,
					"The storage policy specified "
					"does not exist.");
		} else if (reqc->errcode == EBUSY) {
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Configuration changes cannot be made "
				"while the storage policy is running.");
		} else if (reqc->errcode == ENOMEM) {
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
					"Out of memory");
		}
	}
	goto send_reply;
einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"This attribute '%s' is required.", attr_name);
send_reply:
	(void) reqc->resp_handler(reqc, reqc->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
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
	ldmsd_req_attr_t attr;
	size_t cnt = 0;
	reqc->errcode = 0;

	attr_name = "name";
	name = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!name)
		goto einval;

	attr_name = "regex";
	regex_str = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!regex_str)
		goto einval;

	reqc->errcode = ldmsd_strgp_prdcr_add(name, regex_str,
				reqc->line_buf, reqc->line_len);
	if (reqc->errcode == ENOENT) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The storage policy specified "
				"does not exist.");
	} else if (reqc->errcode == EBUSY) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"Configuration changes cannot be made "
			"while the storage policy is running.");
	} else if (reqc->errcode == EEXIST) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The specified regex does not match "
				"any condition.");
	}
	goto send_reply;
einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"This attribute '%s' is required.", attr_name);
send_reply:
	(void) reqc->resp_handler(reqc, reqc->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
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
	ldmsd_req_attr_t attr;
	size_t cnt = 0;
	reqc->errcode = 0;

	attr_name = "name";
	name = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!name)
		goto einval;

	attr_name = "metric";
	metric_name = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!metric_name)
		goto einval;

	reqc->errcode = ldmsd_strgp_metric_add(name, metric_name);
	if (reqc->errcode == ENOENT) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The storage policy specified "
				"does not exist.");
	} else if (reqc->errcode == EBUSY) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"Configuration changes cannot be made "
			"while the storage policy is running.");
	} else if (reqc->errcode == EEXIST) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The specified metric is already "
				"present.");
	} else if (reqc->errcode == ENOMEM) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Memory allocation failure.");
	}
	goto send_reply;
einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"This attribute '%s' is required.", attr_name);
send_reply:
	(void) reqc->resp_handler(reqc, reqc->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
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
	ldmsd_req_attr_t attr;
	size_t cnt = 0;
	reqc->errcode = 0;

	attr_name = "name";
	name = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!name)
		goto einval;

	attr_name = "metric";
	metric_name = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!metric_name)
		goto einval;

	reqc->errcode = ldmsd_strgp_metric_del(name, metric_name);
	if (reqc->errcode == ENOENT) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The storage policy specified "
				"does not exist.");
	} else if (reqc->errcode == EBUSY) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"Configuration changes cannot be made "
			"while the storage policy is running.");
	} else if (reqc->errcode == EEXIST) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The specified metric was not found.");
	}
	goto send_reply;
einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"This attribute '%s' is required.", attr_name);
send_reply:
	(void) reqc->resp_handler(reqc, reqc->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
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
	ldmsd_req_attr_t attr;
	size_t cnt = 0;
	reqc->errcode = 0;

	attr_name = "name";
	name = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!name) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"%dThis attribute '%s' is required.",
				EINVAL, attr_name);
		goto send_reply;
	}

	ldmsd_strgp_t strgp = ldmsd_strgp_find(name);
	if (!strgp) {
		reqc->errcode = ENOENT;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The storage policy does not exist.");
		goto out_1;
	}
	ldmsd_strgp_lock(strgp);
	if (strgp->state != LDMSD_STRGP_STATE_STOPPED) {
		reqc->errcode = EBUSY;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The storage policy is already running.");
		goto out_1;
	}
	strgp->state = LDMSD_STRGP_STATE_RUNNING;
	/* Update all the producers of our changed state */
	ldmsd_prdcr_update(strgp);

out_1:
	ldmsd_strgp_unlock(strgp);
	ldmsd_strgp_put(strgp);
send_reply:
	(void) reqc->resp_handler(reqc, reqc->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	if (name)
		free(name);
	return 0;
}

static int strgp_stop_handler(ldmsd_req_ctxt_t reqc)
{
	char *name, *attr_name;
	name = NULL;
	ldmsd_req_attr_t attr;
	size_t cnt = 0;
	reqc->errcode = 0;

	attr_name = "name";
	name = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!name) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"This attribute '%s' is required.", attr_name);
		goto send_reply;
	}

	reqc->errcode = ldmsd_strgp_stop(name);
	if (reqc->errcode == ENOENT) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The storage policy does not exist.");
	} else if (reqc->errcode == EBUSY) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The storage policy is not running.");
	}
send_reply:
	(void) reqc->resp_handler(reqc, reqc->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	if (name)
		free(name);
	return 0;
}

static int strgp_status_handler(ldmsd_req_ctxt_t reqc)
{
	ldmsd_strgp_t strgp;
	size_t cnt;
	int rc, metric_count, match_count, count = 0;
	ldmsd_name_match_t match;
	ldmsd_strgp_metric_t metric;
	reqc->errcode = 0;

	ldmsd_cfg_lock(LDMSD_CFGOBJ_STRGP);
	rc = reqc->resp_handler(reqc, "[", 1, 0);
	for (strgp = ldmsd_strgp_first(); strgp; strgp = ldmsd_strgp_next(strgp)) {
		if (count)
			rc = reqc->resp_handler(reqc, ",\n", 2, 0);
		ldmsd_strgp_lock(strgp);
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
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
		rc = reqc->resp_handler(reqc, reqc->line_buf, cnt, 0);
		match_count = 0;
		for (match = ldmsd_strgp_prdcr_first(strgp); match;
		     match = ldmsd_strgp_prdcr_next(match)) {
			if (match_count)
				rc = reqc->resp_handler(reqc, ",", 1, 0);
			match_count++;
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				       "\"%s\"", match->regex_str);
			rc = reqc->resp_handler(reqc, reqc->line_buf, cnt, 0);
		}
		rc = reqc->resp_handler(reqc, "],\"metrics\":[", 13, 0);
		metric_count = 0;
		for (metric = ldmsd_strgp_metric_first(strgp); metric;
		     metric = ldmsd_strgp_metric_next(metric)) {
			if (metric_count)
				rc = reqc->resp_handler(reqc, ",", 1, 0);
			metric_count++;
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				       "\"%s\"", metric->name);
			rc = reqc->resp_handler(reqc, reqc->line_buf, cnt, 0);
		}
		rc = reqc->resp_handler(reqc, "]}", 2, 0);
		ldmsd_strgp_unlock(strgp);
		count++;
	}
	rc = reqc->resp_handler(reqc, "]", 1, LDMSD_REQ_EOM_F);
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_STRGP);
	return rc;
}

static int updtr_add_handler(ldmsd_req_ctxt_t reqc)
{
	char *name, *offset_str, *interval_str, *push, *attr_name;
	name = offset_str = interval_str = push = NULL;
	size_t cnt = 0;
	ldmsd_req_attr_t attr;
	reqc->errcode = 0;

	attr_name = "name";
	name = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!name)
		goto einval;

	attr_name = "interval";
	interval_str = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!interval_str)
		goto einval;

	offset_str = ldmsd_req_attr_value_get_by_name(reqc->req_buf, "offset");
	push = ldmsd_req_attr_value_get_by_name(reqc->req_buf, "push");

	ldmsd_updtr_t updtr = ldmsd_updtr_new(name);
	if (!updtr) {
		if (errno == EEXIST)
			goto eexist;
		else if (errno == ENOMEM)
			goto enomem;
		else
			goto send_reply;
	}

	if (push) {
		if (0 == strcasecmp(push, "onchange")) {
			updtr->push_flags = LDMSD_UPDTR_F_PUSH |
						LDMSD_UPDTR_F_PUSH_CHANGE;
		} else {
			updtr->push_flags = LDMSD_UPDTR_F_PUSH;
		}
	} else {
		updtr->push_flags = 0;
	}

	updtr->updt_intrvl_us = strtol(interval_str, NULL, 0);
	if (offset_str) {
		updtr->updt_offset_us = strtol(offset_str, NULL, 0);
		updtr->updt_task_flags = LDMSD_TASK_F_SYNCHRONOUS;
	} else {
		updtr->updt_task_flags = 0;
	}
	goto send_reply;

einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"This attribute '%s' is required.", attr_name);
	goto send_reply;
enomem:
	reqc->errcode = ENOMEM;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len, "Out of memory");
	goto send_reply;
eexist:
	reqc->errcode = EEXIST;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The updtr %s already exists.", name);
send_reply:
	(void) reqc->resp_handler(reqc, reqc->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	if (name)
		free(name);
	if (interval_str)
		free(interval_str);
	if (offset_str)
		free(offset_str);
	if (push)
		free(push);
	return 0;
}

static int updtr_del_handler(ldmsd_req_ctxt_t reqc)
{
	char *name = NULL;
	size_t cnt = 0;
	ldmsd_req_attr_t attr;
	reqc->errcode = 0;

	name = ldmsd_req_attr_value_get_by_name(reqc->req_buf, "name");
	if (!name)
		goto einval;

	reqc->errcode = ldmsd_updtr_del(name);
	if (reqc->errcode == ENOENT) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The updater specified does not exist.");
	} else if (reqc->errcode == EBUSY) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The updater is in use.");
	}
	goto send_reply;

einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"This attribute 'name' is required.");
	goto send_reply;
send_reply:
	(void) reqc->resp_handler(reqc, reqc->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	if (name)
		free(name);
	return 0;
}

static int updtr_prdcr_add_handler(ldmsd_req_ctxt_t reqc)
{
	char *updtr_name, *prdcr_regex, *attr_name;
	updtr_name = prdcr_regex = NULL;
	size_t cnt = 0;
	ldmsd_req_attr_t attr;
	reqc->errcode = 0;

	attr_name = "name";
	updtr_name = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!updtr_name)
		goto einval;

	attr_name = "regex";
	prdcr_regex = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!prdcr_regex)
		goto einval;

	reqc->errcode = ldmsd_updtr_prdcr_add(updtr_name, prdcr_regex,
				reqc->line_buf, reqc->line_len);
	if (reqc->errcode) {
		if (reqc->errcode == ENOENT) {
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
					"The updater specified "
					"does not exist.");
		} else if (reqc->errcode == EBUSY) {
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,""
				"Configuration changes cannot be "
				"made while the updater is running.");
		} else if (reqc->errcode == ENOMEM) {
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Memory allocation failure.");
		} else {
			cnt = strlen(reqc->line_buf);
		}
	}
	goto send_reply;

einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"This attribute '%s' is required.", attr_name);
send_reply:
	(void) reqc->resp_handler(reqc, reqc->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
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
	ldmsd_req_attr_t attr;
	reqc->errcode = 0;

	attr_name = "name";
	updtr_name = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!updtr_name)
		goto einval;

	attr_name = "regex";
	prdcr_regex = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!prdcr_regex)
		goto einval;

	reqc->errcode = ldmsd_updtr_prdcr_del(updtr_name, prdcr_regex,
			reqc->line_buf, reqc->line_len);
	if (reqc->errcode) {
		if (reqc->errcode == ENOMEM) {
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
					"The updater specified does not exist.");
		} else if (reqc->errcode == EBUSY) {
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
					"Configuration changes cannot be "
					"made while the updater is running,");
		} else if (reqc->errcode == ENOENT) {
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
					"The updater specified does not exist.");
		} else {
			cnt = strlen(reqc->line_buf);
		}
	}

	goto send_reply;
einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"This attribute '%s' is required.", attr_name);
send_reply:
	(void) reqc->resp_handler(reqc, reqc->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
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
	ldmsd_req_attr_t attr;
	reqc->errcode = 0;

	attr_name = "name";
	updtr_name = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!updtr_name)
		goto einval;
	attr_name = "regex";
	regex_str = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!regex_str)
		goto einval;

	match_str = ldmsd_req_attr_value_get_by_name(reqc->req_buf, "match");

	reqc->errcode = ldmsd_updtr_match_add(updtr_name, regex_str, match_str,
			reqc->line_buf, reqc->line_len);
	if (reqc->errcode) {
		if (reqc->errcode == ENOENT) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The updater specified does not exist.");
		} else if (reqc->errcode == EBUSY) {
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
					"Configuration changes cannot be made "
					"while the updater is running.");
		} else if (reqc->errcode == ENOMEM) {
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
					"Out of memory.");
		} else if (reqc->errcode == EINVAL) {
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The value '%s' for match= is invalid.",
							match_str);
		}
	}
	goto send_reply;
einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"This attribute '%s' is required.", attr_name);
send_reply:
	(void) reqc->resp_handler(reqc, reqc->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
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
	ldmsd_req_attr_t attr;
	reqc->errcode = 0;

	attr_name = "name";
	updtr_name = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!updtr_name)
		goto einval;
	attr_name = "regex";
	regex_str = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!regex_str)
		goto einval;

	match_str  = ldmsd_req_attr_value_get_by_name(reqc->req_buf, "match");

	reqc->errcode = ldmsd_updtr_match_del(updtr_name, regex_str, match_str);
	if (reqc->errcode == ENOENT) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The updater specified does not exist.");
	} else if (reqc->errcode == EBUSY) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Configuration changes cannot be made "
				"while the updater is running.");
	} else if (reqc->errcode == -ENOENT) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The specified regex does not match any condition.");
	} else if (reqc->errcode == EINVAL) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"Unrecognized match type '%s'", match_str);
	}

	goto send_reply;
einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"This attribute '%s' is required.", attr_name);
send_reply:
	(void) reqc->resp_handler(reqc, reqc->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
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
	char *updtr_name, *interval_str, *offset_str;
	updtr_name = interval_str = offset_str = NULL;
	size_t cnt = 0;
	ldmsd_req_attr_t attr;
	reqc->errcode = 0;

	updtr_name = ldmsd_req_attr_value_get_by_name(reqc->req_buf, "name");
	if (!updtr_name) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The updater name must be specified.");
		goto send_reply;
	}
	interval_str = ldmsd_req_attr_value_get_by_name(reqc->req_buf, "interval");
	offset_str  = ldmsd_req_attr_value_get_by_name(reqc->req_buf, "offset");

	reqc->errcode = ldmsd_updtr_start(updtr_name, interval_str, offset_str);
	if (reqc->errcode == ENOENT) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The updater specified does not exist.");
	} else if (reqc->errcode == EBUSY) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The updater is already running.");
	}

send_reply:
	(void) reqc->resp_handler(reqc, reqc->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
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
	ldmsd_req_attr_t attr;
	reqc->errcode = 0;

	updtr_name = ldmsd_req_attr_value_get_by_name(reqc->req_buf, "name");
	if (!updtr_name) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The updater name must be specified.");
		goto send_reply;
	}

	reqc->errcode = ldmsd_updtr_stop(updtr_name);
	if (reqc->errcode == ENOENT) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The updater specified does not exist.");
	} else if (reqc->errcode == EBUSY) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The updater is already stopped.");
	}
send_reply:
	(void) reqc->resp_handler(reqc, reqc->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
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

static int updtr_status_handler(ldmsd_req_ctxt_t reqc)
{
	ldmsd_updtr_t updtr;
	size_t cnt;
	int rc, count, prdcr_count;
	ldmsd_prdcr_ref_t ref;
	ldmsd_prdcr_t prdcr;
	const char *prdcr_state_str(enum ldmsd_prdcr_state state);
	reqc->errcode = 0;

	ldmsd_cfg_lock(LDMSD_CFGOBJ_UPDTR);
	rc = reqc->resp_handler(reqc, "[", 1, 0);
	count = 0;
	for (updtr = ldmsd_updtr_first(); updtr; updtr = ldmsd_updtr_next(updtr)) {
		if (count)
			rc = reqc->resp_handler(reqc, ",\n", 2, 0);
		count++;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "{\"name\":\"%s\","
			       "\"interval\":\"%d\","
			       "\"offset\":%d,"
			       "\"mode\":\"%s\","
			       "\"state\":\"%s\","
			       "\"producers\":[",
			       updtr->obj.name,
			       updtr->updt_intrvl_us,
			       updtr->updt_offset_us,
			       update_mode(updtr->push_flags),
			       ldmsd_updtr_state_str(updtr->state));
		rc = reqc->resp_handler(reqc, reqc->line_buf, cnt, 0);
		prdcr_count = 0;
		for (ref = ldmsd_updtr_prdcr_first(updtr); ref;
		     ref = ldmsd_updtr_prdcr_next(ref)) {
			if (prdcr_count)
				rc = reqc->resp_handler(reqc, ",\n", 2, 0);
			prdcr_count++;
			prdcr = ref->prdcr;
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				       "{\"name\":\"%s\","
				       "\"host\":\"%s\","
				       "\"port\":%hd,"
				       "\"transport\":\"%s\","
				       "\"state\":\"%s\"}",
				       prdcr->obj.name,
				       prdcr->host_name,
				       prdcr->port_no,
				       prdcr->xprt_name,
				       prdcr_state_str(prdcr->conn_state));
			rc = reqc->resp_handler(reqc, reqc->line_buf, cnt, 0);
		}
		rc = reqc->resp_handler(reqc, "]}", 2, 0);
	}
	rc = reqc->resp_handler(reqc, "]", 1, LDMSD_REQ_EOM_F);
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_UPDTR);
	return rc;
}

struct plugin_list {
	struct ldmsd_plugin_cfg *lh_first;
};

static char *plugn_state_str(enum ldmsd_plugin_type type)
{
	static char *state_str[] = {
		[LDMSD_PLUGIN_OTHER] = "other",
		[LDMSD_PLUGIN_SAMPLER] = "sampler",
		[LDMSD_PLUGIN_STORE] = "store"
	};
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
	plugin_name = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!plugin_name)
		goto einval;
	attr_name = "interval";
	interval_us = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!interval_us)
		goto einval;

	offset = ldmsd_req_attr_value_get_by_name(reqc->req_buf, "offset");

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
			"This attribute '%s' is required.", attr_name);
send_reply:
	(void) reqc->resp_handler(reqc, reqc->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
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
	plugin_name = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
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
			"This attribute '%s' is required.", attr_name);
send_reply:
	(void) reqc->resp_handler(reqc, reqc->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	if (plugin_name)
		free(plugin_name);
	return 0;
}

static int plugn_status_handler(ldmsd_req_ctxt_t reqc)
{
	extern struct plugin_list plugin_list;
	struct ldmsd_plugin_cfg *p;
	size_t cnt;
	int rc, count;
	reqc->errcode = 0;

	rc = reqc->resp_handler(reqc, "[", 1, 0);
	count = 0;
	LIST_FOREACH(p, &plugin_list, entry) {
		if (count)
			rc = reqc->resp_handler(reqc, ",\n", 2, 0);
		count++;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "{\"name\":\"%s\",\"type\":\"%s\","
			       "\"sample_interval_us\":%d,"
			       "\"sample_offset_us\":%d,"
			       "\"libpath\":\"%s\"}",
			       p->plugin->name,
			       plugn_state_str(p->plugin->type),
			       p->sample_interval_us, p->sample_offset_us,
			       p->libpath);
		rc = reqc->resp_handler(reqc, reqc->line_buf, cnt, 0);
	}
	rc = reqc->resp_handler(reqc, "]", 1, LDMSD_REQ_EOM_F);
	return rc;
}

static int plugn_load_handler(ldmsd_req_ctxt_t reqc)
{
	char *plugin_name, *attr_name;
	plugin_name = NULL;
	size_t cnt = 0;
	ldmsd_req_attr_t attr;

	attr_name = "name";
	plugin_name = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!plugin_name)
		goto einval;

	reqc->errcode = ldmsd_load_plugin(plugin_name, reqc->line_buf,
							reqc->line_len);
	if (reqc->errcode)
		cnt = strlen(reqc->line_buf) + 1;
	goto send_reply;

einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"This attribute '%s' is required.", attr_name);
send_reply:
	(void) reqc->resp_handler(reqc, reqc->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
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
	plugin_name = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
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
			"This attribute '%s' is required.", attr_name);
send_reply:
	(void) reqc->resp_handler(reqc, reqc->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	if (plugin_name)
		free(plugin_name);
	return 0;
}

static int plugn_config_handler(ldmsd_req_ctxt_t reqc)
{
	char *plugin_name, *config_attr, *attr_name;
	plugin_name = config_attr = NULL;
	size_t cnt = 0;
	reqc->errcode = 0;

	attr_name = "name";
	plugin_name = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!plugin_name)
		goto einval;
	config_attr = ldmsd_req_attr_value_get_by_name(reqc->req_buf, "string");
	if (!config_attr) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"No config attributes are provided.");
		reqc->errcode = EINVAL;
		goto send_reply;
	}

	char *cmd_s;
	struct attr_value_list *av_list;
	struct attr_value_list *kw_list;
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
			"This attribute '%s' is required.", attr_name);
	goto send_reply;
err:
	if (kw_list)
		av_free(kw_list);
	if (av_list)
		av_free(av_list);
send_reply:
	(void) reqc->resp_handler(reqc, reqc->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	if (plugin_name)
		free(plugin_name);
	if (config_attr)
		free(config_attr);
	return 0;
}

extern struct plugin_list plugin_list;
static int plugn_list_handler(ldmsd_req_ctxt_t reqc)
{
	char *name = NULL;
	int rc, count = 0;
	size_t cnt = 0;
	struct ldmsd_plugin_cfg *p;

	ldmsd_req_attr_t attr;
	attr = (ldmsd_req_attr_t)reqc->req_buf;
	if (attr->attr_id == LDMSD_ATTR_NAME)
		name = (char *)attr->attr_value;

	LIST_FOREACH(p, &plugin_list, entry) {
		if (name && (0 != strcmp(name, p->name)))
			continue;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len, "%s\n", p->name);
		if (count == 0) {
			rc = reqc->resp_handler(reqc, reqc->line_buf, cnt,
					LDMSD_REQ_SOM_F);
		} else {
			rc = reqc->resp_handler(reqc, reqc->line_buf, cnt, 0);
		}
		if (rc)
			goto out;
		if (p->plugin->usage) {
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len, "%s",
					p->plugin->usage(p->plugin));
			rc = reqc->resp_handler(reqc, reqc->line_buf, cnt, 0);
			if (rc)
				goto out;
		}
		count++;
	}
	if (name && (0 == count)) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Plugin '%s' not loaded.", name);
		reqc->errcode = ENOENT;
		rc = reqc->resp_handler(reqc, reqc->line_buf, cnt,
				LDMSD_REQ_EOM_F | LDMSD_REQ_EOM_F);
		return rc;
	}
	rc = reqc->resp_handler(reqc, NULL, 0, LDMSD_REQ_EOM_F);
out:
	return rc;
}

extern int ldmsd_set_udata(const char *set_name, const char *metric_name,
						const char *udata_s);
static int set_udata_handler(ldmsd_req_ctxt_t reqc)
{
	char *set_name, *metric_name, *udata, *attr_name;
	set_name = metric_name = udata = NULL;
	size_t cnt = 0;
	reqc->errcode = 0;

	attr_name = "instance";
	set_name = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!set_name)
		goto einval;
	attr_name = "metric";
	metric_name = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!metric_name)
		goto einval;
	attr_name = "udata";
	udata = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!udata)
		goto einval;

	reqc->errcode = ldmsd_set_udata(set_name, metric_name, udata);
	if (reqc->errcode) {
		if (reqc->errcode == ENOENT) {
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
					"Set '%s' not found.", set_name);
		} else if (reqc->errcode == -ENOENT) {
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
					"Metric '%s' not found in Set '%s'.",
					metric_name, set_name);
		} else if (reqc->errcode == EINVAL) {
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
					"User data '%s' is invalid.", udata);
		}
	}
	goto out;
einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"This attribute '%s' is required.", attr_name);
out:
	(void) reqc->resp_handler(reqc, reqc->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	if (set_name)
		free(set_name);
	if (metric_name)
		free(metric_name);
	if (udata)
		free(udata);
	return 0;
}

extern int ldmsd_set_udata_regex(char *set_name, char *regex_str,
		char *base_s, char *inc_s, char *er_str, size_t errsz);
static int set_udata_regex_handler(ldmsd_req_ctxt_t reqc)
{
	char *set_name, *regex, *base_s, *inc_s, *attr_name;
	set_name = regex = base_s = inc_s = NULL;
	int rc = 0;
	size_t cnt = 0;
	reqc->errcode = 0;

	attr_name = "instance";
	set_name = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!set_name)
		goto einval;
	attr_name = "regex";
	regex = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!regex)
		goto einval;
	attr_name = "base";
	base_s = ldmsd_req_attr_value_get_by_name(reqc->req_buf, attr_name);
	if (!base_s)
		goto einval;

	inc_s = ldmsd_req_attr_value_get_by_name(reqc->req_buf, "incr");

	reqc->errcode = ldmsd_set_udata_regex(set_name, regex, base_s, inc_s,
						reqc->line_buf, reqc->line_len);
	if (reqc->errcode)
		cnt = strlen(reqc->line_buf) + 1;
	goto out;
einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"This attribute '%s' is required.", attr_name);
out:
	(void) reqc->resp_handler(reqc, reqc->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
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

	level_s = ldmsd_req_attr_value_get_by_name(reqc->req_buf, "level");
	if (!level_s) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"This attribute 'level' is required.");
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

#ifdef DEBUG
	ldmsd_log(LDMSD_LDEBUG, "TEST DEBUG\n");
	ldmsd_log(LDMSD_LINFO, "TEST INFO\n");
	ldmsd_log(LDMSD_LERROR, "TEST ERROR\n");
	ldmsd_log(LDMSD_LCRITICAL, "TEST CRITICAL\n");
	ldmsd_log(LDMSD_LALL, "TEST SUPREME\n");
#endif /* DEBUG */
out:
	(void) reqc->resp_handler(reqc, reqc->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	if (level_s)
		free(level_s);
	return 0;
}

static int daemon_status_handler(ldmsd_req_ctxt_t reqc)
{
	size_t cnt = 0;
	int rc = 0;

	extern int ev_thread_count;
	extern pthread_t *ev_thread;
	extern int *ev_count;
	int i;

	rc = reqc->resp_handler(reqc, "[", 1, LDMSD_REQ_SOM_F);
	for (i = 0; i < ev_thread_count; i++) {
		if (i)
			rc = reqc->resp_handler(reqc, ",\n", 2, 0);

		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"{ \"thread\":\"%p\","
				"\"task_count\":\"%d\"}",
				(void *)ev_thread[i], ev_count[i]);
		rc = reqc->resp_handler(reqc, reqc->line_buf, cnt, 0);
	}
	rc = reqc->resp_handler(reqc, "]", 1, LDMSD_REQ_EOM_F);
	return rc;
}

static int version_handler(ldmsd_req_ctxt_t reqc)
{
	struct ldms_version ldms_version;
	struct ldmsd_version ldmsd_version;

	ldms_version_get(&ldms_version);
	size_t cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"LDMS Version: %hhu.%hhu.%hhu.%hhu\n",
			ldms_version.major, ldms_version.minor,
			ldms_version.patch, ldms_version.flags);
	int rc = reqc->resp_handler(reqc, reqc->line_buf, cnt, LDMSD_REQ_SOM_F);
	if (rc)
		return rc;

	ldmsd_version_get(&ldmsd_version);
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"LDMSD Version: %hhu.%hhu.%hhu.%hhu",
			ldmsd_version.major, ldmsd_version.minor,
			ldmsd_version.patch, ldmsd_version.flags);
	rc = reqc->resp_handler(reqc, reqc->line_buf, cnt, LDMSD_REQ_EOM_F);
	return rc;


}

static int env_handler(ldmsd_req_ctxt_t reqc)
{
	int rc = 0;
	size_t cnt = 0;
	char *env_s = NULL;
	struct attr_value_list *av_list = NULL;
	struct attr_value_list *kw_list = NULL;

	ldmsd_req_attr_t attr;
	attr = (ldmsd_req_attr_t)reqc->req_buf;
	while (attr->discrim) {
		switch (attr->attr_id) {
		case LDMSD_ATTR_STRING:
			env_s = (char *)attr->attr_value;
			break;
		default:
			break;
		}
		attr = (ldmsd_req_attr_t)&attr->attr_value[attr->attr_len];
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
					v->name, v->value, strerror(rc));
			goto out;
		}
	}
out:
	rc = reqc->resp_handler(reqc, reqc->line_buf, cnt,
			LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	if (kw_list)
		av_free(kw_list);
	if (av_list)
		av_free(av_list);
	return rc;
}

extern int process_config_file(const char *path, int *lineno);
static int include_handler(ldmsd_req_ctxt_t reqc)
{
	char *path = NULL;
	int rc = 0;
	size_t cnt = 0;

	ldmsd_req_attr_t attr = (ldmsd_req_attr_t)reqc->req_buf;
	while (attr->discrim) {
		switch (attr->attr_id) {
		case LDMSD_ATTR_PATH:
			path = (char *)attr->attr_value;
			break;
		default:
			break;
		}
		attr = (ldmsd_req_attr_t)&attr->attr_value[attr->attr_len];
	}
	if (!path) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"This attribute 'path' is required.");
		goto out;
	}
	int lineno;
	reqc->errcode = process_config_file(path, &lineno);
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
	rc =  reqc->resp_handler(reqc, reqc->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
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

	name = ldmsd_req_attr_value_get_by_name(reqc->req_buf, "name");
	if (!name) {
		attr_name = "name";
		goto einval;
	}
	time_s = ldmsd_req_attr_value_get_by_name(reqc->req_buf, "time");
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
	rc = reqc->resp_handler(reqc, NULL, 0, LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	if (name)
		free(name);
	if (time_s)
		free(time_s);
	return rc;

einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"This attribute '%s' is required.", attr_name);

out:
	rc = reqc->resp_handler(reqc, reqc->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
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
	int rc = reqc->resp_handler(reqc, reqc->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	return rc;
}

extern void ldmsd_exit_daemon();
static int exit_daemon_handler(ldmsd_req_ctxt_t reqc)
{
	ldmsd_exit_daemon();
	size_t cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"exit daemon request received");
	int rc = reqc->resp_handler(reqc, reqc->line_buf, cnt,
			LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	return rc;
}

static int unimplemented_handler(ldmsd_req_ctxt_t reqc)
{
	size_t cnt;
	reqc->errcode = ENOSYS;

	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The request is not implemented");
	(void) reqc->resp_handler(reqc, reqc->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	return 0;
}
