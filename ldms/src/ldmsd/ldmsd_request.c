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
#include "ldms.h"
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
static int greeting_handler(ldmsd_req_ctxt_t req_ctxt);
static int unimplemented_handler(ldmsd_req_ctxt_t req_ctxt);
static int eperm_handler(ldmsd_req_ctxt_t req_ctxt);

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
		LDMSD_PRDCR_STATUS_REQ, prdcr_status_handler, XALL
	},
	[LDMSD_PRDCR_SET_REQ] = {
		LDMSD_PRDCR_SET_REQ, prdcr_set_status_handler, XUG
	},
	[LDMSD_PRDCR_START_REGEX_REQ] = {
		LDMSD_PRDCR_START_REGEX_REQ, prdcr_start_regex_handler, XUG
	},
	[LDMSD_PRDCR_STOP_REGEX_REQ] = {
		LDMSD_PRDCR_STOP_REGEX_REQ, prdcr_stop_regex_handler, XUG
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
		LDMSD_STRGP_STATUS_REQ, strgp_status_handler, XALL
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
		LDMSD_UPDTR_STATUS_REQ, updtr_status_handler, XALL
	},

	/* PLUGN */
	[LDMSD_PLUGN_START_REQ] = {
		LDMSD_PLUGN_START_REQ, plugn_start_handler, XUG
	},
	[LDMSD_PLUGN_STOP_REQ] = {
		LDMSD_PLUGN_STOP_REQ, plugn_stop_handler, XUG
	},
	[LDMSD_PLUGN_STATUS_REQ] = {
		LDMSD_PLUGN_STATUS_REQ, plugn_status_handler, XALL
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
		LDMSD_DAEMON_STATUS_REQ, daemon_status_handler, XALL
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

/*
 * max_msg_len must be a positive number.
 */
ldmsd_req_ctxt_t alloc_req_ctxt(struct req_ctxt_key *key, size_t max_msg_len)
{
	ldmsd_req_ctxt_t reqc;
	size_t len;

	reqc = calloc(1, sizeof *reqc);
	if (!reqc)
		return NULL;
	/* leave one byte for terminating '\0' to accommodate string replies */
	reqc->line_len = LINE_BUF_LEN - 1;
	reqc->line_buf = malloc(LINE_BUF_LEN);
	if (!reqc->line_buf)
		goto err;
	reqc->line_buf[0] = '\0';
	reqc->req_len = REQ_BUF_LEN - 1;
	reqc->req_off = sizeof(struct ldmsd_req_hdr_s);
	reqc->req_buf = malloc(REQ_BUF_LEN);
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

int ldmsd_handle_request(ldmsd_req_ctxt_t reqc)
{
	struct request_handler_entry *ent;
	ldmsd_req_hdr_t request = (ldmsd_req_hdr_t)reqc->req_buf;
	ldms_t xprt = reqc->xprt->xprt;
	uid_t luid;
	gid_t lgid;

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
		/* NOTE: NULL xprt is a config file */
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

int ldmsd_append_reply(struct ldmsd_req_ctxt *reqc,
		       char *data, size_t data_len,
		       int msg_flags)
{
	ldmsd_req_hdr_t req_reply = (ldmsd_req_hdr_t)reqc->rep_buf;
	ldmsd_req_attr_t attr;
	size_t remaining;
	int flags;

	do {
		remaining = reqc->rep_len - reqc->rep_off - sizeof(*req_reply);
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
			req_reply->marker = LDMSD_RECORD_MARKER;
			req_reply->type = LDMSD_REQ_TYPE_CONFIG_RESP;
			req_reply->flags = flags;
			req_reply->msg_no = reqc->key.msg_no;
			req_reply->rsp_err = reqc->errcode;
			req_reply->rec_len = reqc->rep_off;
			ldmsd_hton_req_hdr(req_reply);
			reqc->xprt->send_fn(reqc->xprt, (char *)req_reply, ntohl(req_reply->rec_len));
			reqc->rec_no++;
			/* Reset the reply buffer for the next record for this message */
			reqc->rep_off = sizeof(*req_reply);
			attr = ldmsd_first_attr(req_reply);
			attr->discrim = 0;
		}
	} while (data_len);

	return 0;
}

/*
 * A convenient function that constructs a response with string attribute
 * if there is a message. Otherwise, only the terminating attribute is attached
 * to the request header.
 */
void ldmsd_send_req_response(ldmsd_req_ctxt_t reqc, char *msg)
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

int ldmsd_process_config_request(ldmsd_cfg_xprt_t xprt, ldmsd_req_hdr_t request, size_t req_len)
{
	struct req_ctxt_key key;
	ldmsd_req_ctxt_t reqc = NULL;
	size_t cnt;
	int rc = 0;

	key.msg_no = ntohl(request->msg_no);
	key.conn_id = (uint64_t)(long unsigned)xprt;

	if (ntohl(request->marker) != LDMSD_RECORD_MARKER) {
		char *msg = "Config request is missing record marker";
		ldmsd_send_error_reply(xprt, -1, EINVAL, msg, strlen(msg));
		rc = EINVAL;
		goto out;
	}

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
		if (!reqc) {
			char errstr[64];
			snprintf(errstr, 63, "ldmsd out of memory");
			ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
			rc = ENOMEM;
			ldmsd_send_error_reply(xprt, key.msg_no, rc,
						errstr, strlen(errstr));
			goto err_out;
		}
		if (reqc->req_len < req_len) {
			/* Send record length advice */
			ldmsd_send_cfg_rec_adv(xprt, key.msg_no, reqc->req_len);
			free_req_ctxt(reqc);
			goto err_out;
		}
		reqc->xprt = xprt;
		memcpy(reqc->req_buf, request, req_len);
		reqc->req_off = req_len;
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
		cnt = req_len - sizeof(*request);
		memcpy(&reqc->req_buf[reqc->req_off], request + 1, cnt);
		reqc->req_off += cnt;
	}
	req_ctxt_tree_unlock();

	if (0 == (ntohl(request->flags) & LDMSD_REQ_EOM_F))
		/* Not the end of the message */
		goto out;

	/* Convert the request byte order from network to host */
	ldmsd_ntoh_req_msg((ldmsd_req_hdr_t)reqc->req_buf);

	rc = ldmsd_handle_request(reqc);

	req_ctxt_tree_lock();
	free_req_ctxt(reqc);
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
typedef int (*action_fn)(ldmsd_req_ctxt_t reqc, char *data, size_t len, void *arg);

int __get_json_obj_len_cb(ldmsd_req_ctxt_t reqc, char *data, size_t len, void *arg)
{
	size_t *tot_cnt = (size_t *)arg;
	*tot_cnt += len;
	return 0;
}

int __append_json_obj_cb(ldmsd_req_ctxt_t reqc, char *data, size_t len, void *arg)
{
	return ldmsd_append_reply(reqc, data, len, 0);
}

int __example_json_obj(ldmsd_req_ctxt_t reqc, action_fn cb, void *arg)
{
	size_t cnt;
	int rc, count = 0;
	ldmsd_req_attr_t attr = ldmsd_first_attr((ldmsd_req_hdr_t)reqc->req_buf);
	reqc->errcode = 0;
	cnt = snprintf(reqc->line_buf, reqc->line_len, "[");
	rc = cb(reqc, reqc->line_buf, cnt, arg);
	if (rc)
		return rc;
	while (attr->discrim) {
		if (count) {
			cnt = snprintf(reqc->line_buf, reqc->line_len, ",\n");
			rc = cb(reqc, reqc->line_buf, cnt, arg);
			if (rc)
				return rc;
		}

		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "{ \"attr_len\":%d,"
			       "\"attr_id\":%d,"
			       "\"attr_value\": \"%s\" }",
			       attr->attr_len,
			       attr->attr_id,
			       (char *)attr->attr_value);
		rc = cb(reqc, reqc->line_buf, cnt, arg);
		if (rc)
			return rc;
		count++;
		attr = ldmsd_next_attr(attr);
	}
	cnt = snprintf(reqc->line_buf, reqc->line_len, "]");
	rc = cb(reqc, reqc->line_buf, cnt + 1, arg); /* +1 for '\0' */
	return rc;

}

static int example_handler(ldmsd_req_ctxt_t reqc)
{
	size_t cnt = 0;
	int flags = 0;
	struct ldmsd_req_attr_s attr;
	__example_json_obj(reqc, __get_json_obj_len_cb, (void *)&cnt);
	if (!cnt) {
		flags = LDMSD_REQ_SOM_F;
		goto endresp;
	} else {
		attr.discrim = 1;
		attr.attr_len = cnt;
		attr.attr_id = LDMSD_ATTR_JSON;
		ldmsd_hton_req_attr(&attr);
	}
	(void) ldmsd_append_reply(reqc, (char *)&attr, sizeof(attr), LDMSD_REQ_SOM_F);
	__example_json_obj(reqc, __append_json_obj_cb, NULL);
endresp:
	attr.discrim = 0;
	(void)ldmsd_append_reply(reqc, (char *)&(attr.discrim), sizeof(uint32_t),
							flags | LDMSD_REQ_EOM_F);
	return 0;
}

static int prdcr_add_handler(ldmsd_req_ctxt_t reqc)
{
	ldmsd_prdcr_t prdcr;
	char *name, *host, *xprt, *attr_name, *type_s, *port_s, *interval_s;
	name = host = xprt = type_s = port_s = interval_s = NULL;
	enum ldmsd_prdcr_type type = -1;
	unsigned short port_no = 0;
	int interval_us = -1;
	struct ldmsd_req_attr_s attr;
	size_t cnt = 0;
	uid_t uid;
	gid_t gid;
	int perm;
	char *perm_s = NULL;
	reqc->errcode = 0;

	attr_name = "name";
	name = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_NAME);
	if (!name)
		goto einval;

	attr_name = "type";
	type_s = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_TYPE);
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
	xprt = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_XPRT);
	if (!xprt)
		goto einval;

	attr_name = "host";
	host = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_HOST);
	if (!host)
		goto einval;

	attr_name = "port";
	port_s = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_PORT);
	if (!port_s) {
		goto einval;
	} else {
		port_no = strtol(port_s, NULL, 0);
	}

	attr_name = "interval";
	interval_s = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_INTERVAL);
	if (!interval_s) {
		goto einval;
	} else {
		 interval_us = strtol(interval_s, NULL, 0);
	}

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
	perm_s = ldmsd_req_attr_str_value_get_by_name(reqc->req_buf, "perm");
	if (perm_s)
		perm = strtol(perm_s, NULL, 0);

	prdcr = ldmsd_prdcr_new_with_auth(name, xprt, host, port_no, type,
					  interval_us, uid, gid, perm);
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
	name = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_NAME);
	if (!name) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"This attribute '%s' is required by prdcr_del.",
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

	name = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_NAME);
	if (!name) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"This attribute 'name' is required by prdcr_start.");
		goto send_reply;
	}

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);
	interval_str = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf,
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
	ldmsd_req_attr_t attr;
	struct ldmsd_sec_ctxt sctxt;

	reqc->errcode = 0;

	name = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_NAME);
	if (!name) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"This attribute 'name' is required by prdcr_stop.");
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
	ldmsd_req_attr_t attr;
	struct ldmsd_sec_ctxt sctxt;

	reqc->errcode = 0;

	prdcr_regex = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_REGEX);
	if (!prdcr_regex) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"This attribute 'regex' is required by prdcr_start_regex.");
		goto send_reply;
	}

	interval_str = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_INTERVAL);

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
	ldmsd_req_attr_t attr;
	struct ldmsd_sec_ctxt sctxt;

	reqc->errcode = 0;

	prdcr_regex = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_REGEX);
	if (!prdcr_regex) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"This attribute 'regex' is required by prdcr_stop_regex.");
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

int __prdcr_status_json_obj(ldmsd_req_ctxt_t reqc, action_fn cb, void *arg)
{
	extern const char *prdcr_state_str(enum ldmsd_prdcr_state state);
	ldmsd_prdcr_t prdcr;
	size_t cnt;
	int rc, count = 0;
	reqc->errcode = 0;

	ldmsd_cfg_lock(LDMSD_CFGOBJ_PRDCR);
	cnt = snprintf(reqc->line_buf, reqc->line_len, "[");
	rc = cb(reqc, reqc->line_buf, cnt, arg);
	if (rc)
		goto out;
	for (prdcr = ldmsd_prdcr_first(); prdcr;
	     prdcr = ldmsd_prdcr_next(prdcr)) {

		if (count) {
			cnt = snprintf(reqc->line_buf, reqc->line_len, ",\n");
			rc = cb(reqc, reqc->line_buf, cnt, arg);
			if (rc)
				goto out;
		}

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
		rc = cb(reqc, reqc->line_buf, cnt, arg);
		if (rc)
			goto out;

		ldmsd_prdcr_lock(prdcr);
		ldmsd_prdcr_set_t prv_set;
		int set_count = 0;
		for (prv_set = ldmsd_prdcr_set_first(prdcr); prv_set;
		     prv_set = ldmsd_prdcr_set_next(prv_set)) {
			if (set_count) {
				cnt = snprintf(reqc->line_buf, reqc->line_len, ",\n");
				rc = cb(reqc, reqc->line_buf, cnt, arg);
				if (rc) {
					ldmsd_prdcr_unlock(prdcr);
					goto out;
				}
			}

			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				       "{ \"inst_name\":\"%s\","
				       "\"schema_name\":\"%s\","
				       "\"state\":\"%s\"}",
				       prv_set->inst_name,
				       (prv_set->schema_name ? prv_set->schema_name : ""),
				       ldmsd_prdcr_set_state_str(prv_set->state));
			rc = cb(reqc, reqc->line_buf, cnt, arg);
			if (rc) {
				ldmsd_prdcr_unlock(prdcr);
				goto out;
			}
			set_count++;
		}
		ldmsd_prdcr_unlock(prdcr);
		cnt = snprintf(reqc->line_buf, reqc->line_len, "]}");
		rc = cb(reqc, reqc->line_buf, cnt, arg);
		if (rc)
			goto out;
		count++;
	}
	cnt = snprintf(reqc->line_buf, reqc->line_len, "]");
	rc = cb(reqc, reqc->line_buf, cnt, arg);
out:
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR);
	return rc;
}

static int prdcr_status_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	size_t cnt = 0;
	struct ldmsd_req_attr_s attr;

	rc = __prdcr_status_json_obj(reqc, __get_json_obj_len_cb, (void*)&cnt);
	if (rc)
		return rc;
	attr.discrim = 1;
	attr.attr_len = cnt;
	attr.attr_id = LDMSD_ATTR_JSON;
	ldmsd_hton_req_attr(&attr);
	rc = ldmsd_append_reply(reqc, (char *)&attr, sizeof(attr), LDMSD_REQ_SOM_F);
	if (rc)
		return rc;

	rc = __prdcr_status_json_obj(reqc, __append_json_obj_cb, NULL);
	if (rc)
		return rc;
	attr.discrim = 0;
	rc = ldmsd_append_reply(reqc, (char *)&attr.discrim, sizeof(uint32_t), LDMSD_REQ_EOM_F);
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
			int *count, const char *setname, const char *schema,
			action_fn cb, void *arg)
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
		if (*count) {
			cnt = snprintf(reqc->line_buf, reqc->line_len, ",\n");
			rc = cb(reqc, reqc->line_buf, cnt, arg);
			if (rc)
				return rc;
		}
		cnt = __prdcr_set_status(reqc, prd_set);
		rc = cb(reqc, reqc->line_buf, cnt, arg);
		if (rc)
			return rc;
		(*count)++;
	} else {
		for (prd_set = ldmsd_prdcr_set_first(prdcr); prd_set;
			prd_set = ldmsd_prdcr_set_next(prd_set)) {
			if (schema && (0 != strcmp(prd_set->schema_name, schema)))
				continue;

			if (*count) {
				cnt = snprintf(reqc->line_buf, reqc->line_len, ",\n");
				rc = cb(reqc, reqc->line_buf, cnt, arg);
				if (rc)
					return rc;
			}
			cnt = __prdcr_set_status(reqc, prd_set);
			rc = cb(reqc, reqc->line_buf, cnt, arg);
			if (rc)
				return rc;
			(*count)++;
		}
	}
	return rc;
}

int __prdcr_set_status_json_obj(ldmsd_req_ctxt_t reqc, action_fn cb, void *arg)
{
	char *prdcr_name, *setname, *schema;
	prdcr_name = setname = schema = NULL;
	ldmsd_prdcr_t prdcr = NULL;
	ldmsd_req_attr_t attr;
	size_t cnt = 0;
	int rc, count = 0;
	reqc->errcode = 0;

	prdcr_name = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_PRODUCER);
	setname = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_INSTANCE);
	schema = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_SCHEMA);

	cnt = snprintf(reqc->line_buf, reqc->line_len, "[");
	rc = cb(reqc, reqc->line_buf, cnt, arg);
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
				setname, schema, cb, arg);
		ldmsd_prdcr_unlock(prdcr);
	} else {
		ldmsd_cfg_lock(LDMSD_CFGOBJ_PRDCR);
		for (prdcr = ldmsd_prdcr_first(); prdcr;
				prdcr = ldmsd_prdcr_next(prdcr)) {
			ldmsd_prdcr_lock(prdcr);
			rc = __prdcr_set_status_handler(reqc, prdcr, &count,
					setname, schema, cb, arg);
			ldmsd_prdcr_unlock(prdcr);
			if (rc) {
				ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR);
				goto out;
			}
		}
		ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR);
	}

out:
	cnt = snprintf(reqc->line_buf, reqc->line_len, "]");
	rc = cb(reqc, reqc->line_buf, cnt, arg);
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
	size_t cnt = 0;
	struct ldmsd_req_attr_s attr;

	rc = __prdcr_set_status_json_obj(reqc, __get_json_obj_len_cb, (void*)&cnt);
	if (rc)
		return rc;
	attr.discrim = 1;
	attr.attr_len = cnt;
	attr.attr_id = LDMSD_ATTR_JSON;
	ldmsd_hton_req_attr(&attr);
	rc = ldmsd_append_reply(reqc, (char *)&attr, sizeof(attr), LDMSD_REQ_SOM_F);
	if (rc)
		return rc;

	rc = __prdcr_set_status_json_obj(reqc, __append_json_obj_cb, NULL);
	if (rc)
		return rc;
	attr.discrim = 0;
	rc = ldmsd_append_reply(reqc, (char *)&attr.discrim, sizeof(uint32_t), LDMSD_REQ_EOM_F);
	return rc;
}

static int strgp_add_handler(ldmsd_req_ctxt_t reqc)
{
	char *attr_name, *name, *plugin, *container, *schema;
	name = plugin = container = schema = NULL;
	ldmsd_req_attr_t attr;
	size_t cnt = 0;
	uid_t uid;
	gid_t gid;
	int perm;
	char *perm_s = NULL;

	reqc->errcode = 0;

	attr_name = "name";
	name = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_NAME);
	if (!name)
		goto einval;

	attr_name = "plugin";
	plugin = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_PLUGIN);
	if (!plugin)
		goto einval;

	attr_name = "container";
	container = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_CONTAINER);
	if (!container)
		goto einval;

	attr_name = "schema";
	schema = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_SCHEMA);
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
	perm_s = ldmsd_req_attr_str_value_get_by_name(reqc->req_buf, "perm");
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
			"This attribute '%s' is required by strgp_add.",
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
	ldmsd_req_attr_t attr;
	size_t cnt = 0;
	struct ldmsd_sec_ctxt sctxt;

	reqc->errcode = 0;

	name = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_NAME);
	if (!name) {
		reqc->errcode= EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"This attribute 'name' is required"
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
	ldmsd_req_attr_t attr;
	struct ldmsd_sec_ctxt sctxt;

	size_t cnt = 0;
	reqc->errcode = 0;

	attr_name = "name";
	name = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_NAME);
	if (!name)
		goto einval;

	attr_name = "regex";
	regex_str = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_REGEX);
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
			"This attribute '%s' is required by %s.", attr_name,
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
	ldmsd_req_attr_t attr;
	size_t cnt = 0;
	struct ldmsd_sec_ctxt sctxt;

	reqc->errcode = 0;

	attr_name = "name";
	name = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_NAME);
	if (!name)
		goto einval;

	attr_name = "regex";
	regex_str = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_REGEX);
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
	case EEXIST:
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The specified regex does not match "
				"any condition.");
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
			"This attribute '%s' is required by %s.", attr_name,
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
	ldmsd_req_attr_t attr;
	size_t cnt = 0;
	struct ldmsd_sec_ctxt sctxt;

	reqc->errcode = 0;

	attr_name = "name";
	name = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_NAME);
	if (!name)
		goto einval;

	attr_name = "metric";
	metric_name = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_METRIC);
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
			"This attribute '%s' is required by %s.", attr_name,
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
	ldmsd_req_attr_t attr;
	size_t cnt = 0;
	struct ldmsd_sec_ctxt sctxt;

	reqc->errcode = 0;

	attr_name = "name";
	name = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_NAME);
	if (!name)
		goto einval;

	attr_name = "metric";
	metric_name = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_METRIC);
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
			"This attribute '%s' is required by %s.", attr_name,
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
	ldmsd_req_attr_t attr;
	size_t cnt = 0;
	int rc;
	struct ldmsd_sec_ctxt sctxt;

	reqc->errcode = 0;

	attr_name = "name";
	name = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_NAME);
	if (!name) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"%dThis attribute '%s' is required by %s.",
				EINVAL, attr_name, "strgp_start");
		goto send_reply;
	}

	ldmsd_strgp_t strgp = ldmsd_strgp_find(name);
	if (!strgp) {
		reqc->errcode = ENOENT;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"The storage policy does not exist.");
		goto send_reply;
	}
	ldmsd_strgp_lock(strgp);
	ldmsd_req_ctxt_sec_get(reqc, &sctxt);
	reqc->errcode = ldmsd_cfgobj_access_check(&strgp->obj, 0222, &sctxt);
	if (reqc->errcode) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"Permission denied.");
		goto out_1;
	}
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
	ldmsd_send_req_response(reqc, reqc->line_buf);
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
	struct ldmsd_sec_ctxt sctxt;

	reqc->errcode = 0;

	attr_name = "name";
	name = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_NAME);
	if (!name) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"This attribute '%s' is required by %s.",
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

int __strgp_status_json_obj(ldmsd_req_ctxt_t reqc, action_fn cb, void *arg)
{
	ldmsd_strgp_t strgp;
	size_t cnt;
	int rc, metric_count, match_count, count = 0;
	ldmsd_name_match_t match;
	ldmsd_strgp_metric_t metric;
	reqc->errcode = 0;

	ldmsd_cfg_lock(LDMSD_CFGOBJ_STRGP);
	cnt = snprintf(reqc->line_buf, reqc->line_len, "[");
	rc = cb(reqc, reqc->line_buf, cnt, arg);
	if (rc)
		return rc;
	for (strgp = ldmsd_strgp_first(); strgp; strgp = ldmsd_strgp_next(strgp)) {
		if (count) {
			cnt = snprintf(reqc->line_buf, reqc->line_len, ",\n");
			rc = cb(reqc, ",\n", cnt, arg);
			if (rc)
				goto out;
		}

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
		rc = cb(reqc, reqc->line_buf, cnt, arg);
		if (rc) {
			ldmsd_strgp_unlock(strgp);
			goto out;
		}
		match_count = 0;
		for (match = ldmsd_strgp_prdcr_first(strgp); match;
		     match = ldmsd_strgp_prdcr_next(match)) {
			if (match_count) {
				cnt = snprintf(reqc->line_buf, reqc->line_len, ",");
				rc = cb(reqc, reqc->line_buf, cnt, arg);
				if (rc) {
					ldmsd_strgp_unlock(strgp);
					goto out;
				}
			}
			match_count++;
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				       "\"%s\"", match->regex_str);
			rc = cb(reqc, reqc->line_buf, cnt, arg);
			if (rc) {
				ldmsd_strgp_unlock(strgp);
				goto out;
			}
		}
		cnt = snprintf(reqc->line_buf, reqc->line_len, "],\"metrics\":[");
		rc = cb(reqc, reqc->line_buf, cnt, arg);
		if (rc) {
			ldmsd_strgp_unlock(strgp);
			goto out;
		}
		metric_count = 0;
		for (metric = ldmsd_strgp_metric_first(strgp); metric;
		     metric = ldmsd_strgp_metric_next(metric)) {
			if (metric_count) {
				cnt = snprintf(reqc->line_buf, reqc->line_len, ",");
				rc = cb(reqc, reqc->line_buf, cnt, arg);
				if (rc) {
					ldmsd_strgp_unlock(strgp);
					goto out;
				}
			}
			metric_count++;
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				       "\"%s\"", metric->name);
			rc = cb(reqc, reqc->line_buf, cnt, arg);
			if (rc) {
				ldmsd_strgp_unlock(strgp);
				goto out;
			}
		}
		cnt = snprintf(reqc->line_buf, reqc->line_len, "]}");
		rc = cb(reqc, reqc->line_buf, cnt, arg);
		ldmsd_strgp_unlock(strgp);
		if (rc)
			goto out;
		count++;
	}
	cnt = snprintf(reqc->line_buf, reqc->line_len, "]");
	rc = cb(reqc, reqc->line_buf, cnt, arg);
out:
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_STRGP);
	return rc;
}

static int strgp_status_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	size_t cnt = 0;
	struct ldmsd_req_attr_s attr;

	rc = __strgp_status_json_obj(reqc, __get_json_obj_len_cb, (void*)&cnt);
	if (rc)
		return rc;
	attr.discrim = 1;
	attr.attr_len = cnt;
	attr.attr_id = LDMSD_ATTR_JSON;
	ldmsd_hton_req_attr(&attr);
	rc = ldmsd_append_reply(reqc, (char *)&attr, sizeof(attr), LDMSD_REQ_SOM_F);
	if (rc)
		return rc;

	rc = __strgp_status_json_obj(reqc, __append_json_obj_cb, NULL);
	if (rc)
		return rc;
	attr.discrim = 0;
	rc = ldmsd_append_reply(reqc, (char *)&attr.discrim, sizeof(uint32_t), LDMSD_REQ_EOM_F);
	return rc;
}

static int updtr_add_handler(ldmsd_req_ctxt_t reqc)
{
	char *name, *offset_str, *interval_str, *push, *attr_name;
	name = offset_str = interval_str = push = NULL;
	size_t cnt = 0;
	ldmsd_req_attr_t attr;
	uid_t uid;
	gid_t gid;
	int perm;
	char *perm_s = NULL;

	reqc->errcode = 0;

	attr_name = "name";
	name = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_NAME);
	if (!name)
		goto einval;

	attr_name = "interval";
	interval_str = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_INTERVAL);
	if (!interval_str)
		goto einval;

	offset_str = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_OFFSET);
	push = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_PUSH);

	struct ldmsd_sec_ctxt sctxt;
	if (reqc->xprt->xprt) {
		ldms_xprt_cred_get(reqc->xprt->xprt, NULL, &sctxt.crd);
	} else {
		ldmsd_sec_ctxt_get(&sctxt);
	}
	uid = sctxt.crd.uid;
	gid = sctxt.crd.gid;

	perm = 0770;
	perm_s = ldmsd_req_attr_str_value_get_by_name(reqc->req_buf, "perm");
	if (perm_s)
		perm = strtoul(perm_s, NULL, 0);

	ldmsd_updtr_t updtr = ldmsd_updtr_new_with_auth(name, uid, gid, perm);
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
			"This attribute '%s' is required by %s.", attr_name,
			"updtr_add");
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
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (name)
		free(name);
	if (interval_str)
		free(interval_str);
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
	ldmsd_req_attr_t attr;
	struct ldmsd_sec_ctxt sctxt;

	reqc->errcode = 0;

	name = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_NAME);
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
			"This attribute 'name' is required by updtr_del.");
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
	ldmsd_req_attr_t attr;
	struct ldmsd_sec_ctxt sctxt;
	reqc->errcode = 0;

	attr_name = "name";
	updtr_name = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_NAME);
	if (!updtr_name)
		goto einval;

	attr_name = "regex";
	prdcr_regex = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_REGEX);
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

einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"This attribute '%s' is required by %s.", attr_name,
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
	ldmsd_req_attr_t attr;
	struct ldmsd_sec_ctxt sctxt;

	reqc->errcode = 0;

	attr_name = "name";
	updtr_name = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_NAME);
	if (!updtr_name)
		goto einval;

	attr_name = "regex";
	prdcr_regex = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_REGEX);
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
			"This attribute '%s' is required by %s.", attr_name,
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
	ldmsd_req_attr_t attr;
	struct ldmsd_sec_ctxt sctxt;

	reqc->errcode = 0;

	attr_name = "name";
	updtr_name = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_NAME);
	if (!updtr_name)
		goto einval;
	attr_name = "regex";
	regex_str = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_REGEX);
	if (!regex_str)
		goto einval;

	match_str = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_MATCH);

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
			"This attribute '%s' is required by %s.", attr_name,
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
	ldmsd_req_attr_t attr;
	struct ldmsd_sec_ctxt sctxt;

	reqc->errcode = 0;

	attr_name = "name";
	updtr_name = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_NAME);
	if (!updtr_name)
		goto einval;
	attr_name = "regex";
	regex_str = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_REGEX);
	if (!regex_str)
		goto einval;

	match_str  = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_MATCH);

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
			"This attribute '%s' is required by %s.", attr_name,
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
	char *updtr_name, *interval_str, *offset_str;
	updtr_name = interval_str = offset_str = NULL;
	size_t cnt = 0;
	ldmsd_req_attr_t attr;
	struct ldmsd_sec_ctxt sctxt;

	reqc->errcode = 0;

	updtr_name = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_NAME);
	if (!updtr_name) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"The updater name must be specified.");
		goto send_reply;
	}
	interval_str = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_INTERVAL);
	offset_str  = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_OFFSET);

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);
	reqc->errcode = ldmsd_updtr_start(updtr_name, interval_str, offset_str,
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
	ldmsd_req_attr_t attr;
	struct ldmsd_sec_ctxt sctxt;

	reqc->errcode = 0;

	updtr_name = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_NAME);
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

int __updtr_status_json_obj(ldmsd_req_ctxt_t reqc, action_fn cb, void *arg)
{
	ldmsd_updtr_t updtr;
	size_t cnt;
	int rc, count, prdcr_count;
	ldmsd_prdcr_ref_t ref;
	ldmsd_prdcr_t prdcr;
	const char *prdcr_state_str(enum ldmsd_prdcr_state state);
	reqc->errcode = 0;

	ldmsd_cfg_lock(LDMSD_CFGOBJ_UPDTR);
	cnt = snprintf(reqc->line_buf, reqc->line_len, "[");
	rc = cb(reqc, reqc->line_buf, cnt, arg);
	if (rc)
		goto out;
	count = 0;
	for (updtr = ldmsd_updtr_first(); updtr; updtr = ldmsd_updtr_next(updtr)) {
		ldmsd_updtr_lock(updtr);
		if (count) {
			cnt = snprintf(reqc->line_buf, reqc->line_len, ",\n");
			rc = cb(reqc, reqc->line_buf, cnt, arg);
			if (rc) {
				ldmsd_updtr_unlock(updtr);
				goto out;
			}
		}

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
		rc = cb(reqc, reqc->line_buf, cnt, arg);
		if (rc) {
			ldmsd_updtr_unlock(updtr);
			goto out;
		}
		prdcr_count = 0;
		for (ref = ldmsd_updtr_prdcr_first(updtr); ref;
		     ref = ldmsd_updtr_prdcr_next(ref)) {
			if (prdcr_count) {
				cnt = snprintf(reqc->line_buf, reqc->line_len, ",\n");
				rc = cb(reqc, reqc->line_buf, cnt, arg);
				if (rc) {
					ldmsd_updtr_unlock(updtr);
					goto out;
				}
			}
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
			rc = cb(reqc, reqc->line_buf, cnt, arg);
			if (rc) {
				ldmsd_updtr_unlock(updtr);
				goto out;
			}
		}
		cnt = snprintf(reqc->line_buf, reqc->line_len, "]}");
		rc = cb(reqc, reqc->line_buf, cnt, arg);
		if (rc) {
			ldmsd_updtr_unlock(updtr);
			goto out;
		}
		ldmsd_updtr_unlock(updtr);
	}
	cnt = snprintf(reqc->line_buf, reqc->line_len, "]");
	rc = cb(reqc, reqc->line_buf, cnt, arg);
out:
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_UPDTR);
	return rc;
}

static int updtr_status_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	size_t cnt = 0;
	struct ldmsd_req_attr_s attr;

	rc = __updtr_status_json_obj(reqc, __get_json_obj_len_cb, (void*)&cnt);
	if (rc)
		return rc;
	attr.discrim = 1;
	attr.attr_len = cnt;
	attr.attr_id = LDMSD_ATTR_JSON;
	ldmsd_hton_req_attr(&attr);
	rc = ldmsd_append_reply(reqc, (char *)&attr, sizeof(attr), LDMSD_REQ_SOM_F);
	if (rc)
		return rc;

	rc = __updtr_status_json_obj(reqc, __append_json_obj_cb, NULL);
	if (rc)
		return rc;
	attr.discrim = 0;
	rc = ldmsd_append_reply(reqc, (char *)&attr.discrim, sizeof(uint32_t), LDMSD_REQ_EOM_F);
	return rc;
}

struct plugin_list {
	struct ldmsd_plugin_cfg *lh_first;
};

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
	plugin_name = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_NAME);
	if (!plugin_name)
		goto einval;
	attr_name = "interval";
	interval_us = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_INTERVAL);
	if (!interval_us)
		goto einval;

	offset = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_OFFSET);

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
			"This attribute '%s' is required by start.", attr_name);
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
	plugin_name = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_NAME);
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
			"This attribute '%s' is required by stop.", attr_name);
send_reply:
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (plugin_name)
		free(plugin_name);
	return 0;
}

int __plugn_status_json_obj(ldmsd_req_ctxt_t reqc, action_fn cb, void *arg)
{
	extern struct plugin_list plugin_list;
	struct ldmsd_plugin_cfg *p;
	size_t cnt;
	int rc, count;
	reqc->errcode = 0;

	cnt = snprintf(reqc->line_buf, reqc->line_len, "[");
	rc = cb(reqc, reqc->line_buf, cnt, arg);
	if (rc)
		return rc;
	count = 0;
	LIST_FOREACH(p, &plugin_list, entry) {
		if (count) {
			cnt = snprintf(reqc->line_buf, reqc->line_len, ",\n");
			rc = cb(reqc, reqc->line_buf, cnt, arg);
			if (rc)
				return rc;
		}

		count++;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			       "{\"name\":\"%s\",\"type\":\"%s\","
			       "\"sample_interval_us\":%ld,"
			       "\"sample_offset_us\":%ld,"
			       "\"libpath\":\"%s\"}",
			       p->plugin->name,
			       plugn_state_str(p->plugin->type),
			       p->sample_interval_us, p->sample_offset_us,
			       p->libpath);
		rc = cb(reqc, reqc->line_buf, cnt, arg);
		if (rc)
			return rc;
	}
	cnt = snprintf(reqc->line_buf, reqc->line_len, "]");
	rc = cb(reqc, reqc->line_buf, cnt, arg);
	return rc;
}

static int plugn_status_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	size_t cnt = 0;
	struct ldmsd_req_attr_s attr;

	rc = __plugn_status_json_obj(reqc, __get_json_obj_len_cb, (void*)&cnt);
	if (rc)
		return rc;
	attr.discrim = 1;
	attr.attr_len = cnt;
	attr.attr_id = LDMSD_ATTR_JSON;
	ldmsd_hton_req_attr(&attr);
	rc = ldmsd_append_reply(reqc, (char *)&attr, sizeof(attr), LDMSD_REQ_SOM_F);
	if (rc)
		return rc;

	rc = __plugn_status_json_obj(reqc, __append_json_obj_cb, NULL);
	if (rc)
		return rc;
	attr.discrim = 0;
	rc = ldmsd_append_reply(reqc, (char *)&attr.discrim, sizeof(uint32_t), LDMSD_REQ_EOM_F);
	return rc;
}

static int plugn_load_handler(ldmsd_req_ctxt_t reqc)
{
	char *plugin_name, *attr_name;
	plugin_name = NULL;
	size_t cnt = 0;
	struct ldmsd_req_attr_s attr;

	attr_name = "name";
	plugin_name = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_NAME);
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
			"This attribute '%s' is required by load.", attr_name);
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
	plugin_name = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_NAME);
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
			"This attribute '%s' is required by term.", attr_name);
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
	plugin_name = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_NAME);
	if (!plugin_name)
		goto einval;
	config_attr = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_STRING);
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
			"This attribute '%s' is required by config.",
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
int __plugn_list_string(ldmsd_req_ctxt_t reqc, action_fn cb, void *arg)
{
	char *name = NULL;
	int rc, count = 0;
	size_t cnt = 0;
	struct ldmsd_plugin_cfg *p;
	rc = 0;

	name = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_NAME);

	LIST_FOREACH(p, &plugin_list, entry) {
		if (name && (0 != strcmp(name, p->name)))
			continue;

		if (p->plugin->usage) {
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len, "%s\n%s",
					p->name, p->plugin->usage(p->plugin));
		} else {
			cnt = Snprintf(&reqc->line_buf, &reqc->line_len, "%s\n", p->name);
		}
		rc = cb(reqc, reqc->line_buf, cnt, arg);
		if (rc)
			goto out;
		count++;
	}
	if (name && (0 == count)) {
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"Plugin '%s' not loaded.", name);
		reqc->errcode = ENOENT;
		rc = cb(reqc, reqc->line_buf, cnt, arg);
	}
out:
	return rc;
}

static int plugn_list_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	size_t cnt = 0;
	struct ldmsd_req_attr_s attr;

	rc = __plugn_list_string(reqc, __get_json_obj_len_cb, (void*)&cnt);
	if (rc)
		return rc;
	attr.discrim = 1;
	attr.attr_len = cnt;
	attr.attr_id = LDMSD_ATTR_STRING;
	ldmsd_hton_req_attr(&attr);
	rc = ldmsd_append_reply(reqc, (char *)&attr, sizeof(attr), LDMSD_REQ_SOM_F);
	if (rc)
		return rc;

	rc = __plugn_list_string(reqc, __append_json_obj_cb, NULL);
	if (rc)
		return rc;
	attr.discrim = 0;
	rc = ldmsd_append_reply(reqc, (char *)&attr.discrim, sizeof(uint32_t), LDMSD_REQ_EOM_F);
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
	set_name = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_INSTANCE);
	if (!set_name)
		goto einval;
	attr_name = "metric";
	metric_name = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_METRIC);
	if (!metric_name)
		goto einval;
	attr_name = "udata";
	udata = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_UDATA);
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
			"This attribute '%s' is required.", attr_name);
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
	int rc = 0;
	size_t cnt = 0;
	struct ldmsd_sec_ctxt sctxt;

	reqc->errcode = 0;

	attr_name = "instance";
	set_name = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_INSTANCE);
	if (!set_name)
		goto einval;
	attr_name = "regex";
	regex = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_REGEX);
	if (!regex)
		goto einval;
	attr_name = "base";
	base_s = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_BASE);
	if (!base_s)
		goto einval;

	inc_s = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_INCREMENT);

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);
	reqc->errcode = ldmsd_set_udata_regex(set_name, regex, base_s, inc_s,
				reqc->line_buf, reqc->line_len, &sctxt);
	goto out;
einval:
	reqc->errcode = EINVAL;
	cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
			"This attribute '%s' is required.", attr_name);
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

	level_s = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_LEVEL);
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
	ldmsd_send_req_response(reqc, reqc->line_buf);
	if (level_s)
		free(level_s);
	return 0;
}

int __daemon_status_json_obj(ldmsd_req_ctxt_t reqc, action_fn cb, void *arg)
{
	size_t cnt = 0;
	int rc = 0;

	extern int ev_thread_count;
	extern pthread_t *ev_thread;
	extern int *ev_count;
	int i;

	cnt = snprintf(reqc->line_buf, reqc->line_len, "[");
	rc = cb(reqc, reqc->line_buf, cnt, arg);
	if (rc)
		return rc;
	for (i = 0; i < ev_thread_count; i++) {
		if (i) {
			cnt = snprintf(reqc->line_buf, reqc->line_len, "\n");
			rc = cb(reqc, reqc->line_buf, cnt, arg);
			if (rc)
				return rc;
		}

		cnt = snprintf(reqc->line_buf, reqc->line_len,
				"{ \"thread\":\"%p\","
				"\"task_count\":\"%d\"}",
				(void *)ev_thread[i], ev_count[i]);
		rc = cb(reqc, reqc->line_buf, cnt, arg);
	}
	cnt = snprintf(reqc->line_buf, reqc->line_len, "]");
	rc = cb(reqc, reqc->line_buf, cnt + 1, arg);
	return rc;
}

static int daemon_status_handler(ldmsd_req_ctxt_t reqc)
{
	int rc;
	size_t cnt = 0;
	struct ldmsd_req_attr_s attr;

	rc = __daemon_status_json_obj(reqc, __get_json_obj_len_cb, (void*)&cnt);
	if (rc)
		return rc;
	attr.discrim = 1;
	attr.attr_len = cnt;
	attr.attr_id = LDMSD_ATTR_JSON;
	ldmsd_hton_req_attr(&attr);
	ldmsd_append_reply(reqc, (char *)&attr, sizeof(attr), LDMSD_REQ_SOM_F);

	rc = __daemon_status_json_obj(reqc, __append_json_obj_cb, NULL);
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
	struct ldmsd_req_attr_s attr;

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
		attr = ldmsd_next_attr(attr);
	}
	if (!path) {
		reqc->errcode = EINVAL;
		cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"This attribute 'path' is required by include.");
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

	name = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_NAME);
	if (!name) {
		attr_name = "name";
		goto einval;
	}
	time_s = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_TIME);
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
			"This attribute '%s' is required by oneshot.",
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

extern void ldmsd_exit_daemon();
static int exit_daemon_handler(ldmsd_req_ctxt_t reqc)
{
	ldmsd_exit_daemon();
	size_t cnt = Snprintf(&reqc->line_buf, &reqc->line_len,
				"exit daemon request received");
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
	int rc = 0;
	int i, msg_flag;

	rep_len_str = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_OFFSET);
	num_rec_str = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_LEVEL);
	str = ldmsd_req_attr_str_value_get_by_id(reqc->req_buf, LDMSD_ATTR_NAME);
	if (str) {
		cnt = snprintf(reqc->line_buf, reqc->line_len, "Hello '%s'", str);
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
	} else {
		ldmsd_send_req_response(reqc, NULL);
	}
out:
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
