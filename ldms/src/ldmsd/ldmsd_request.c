/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015-2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2015-2016 Sandia Corporation. All rights reserved.
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
typedef struct msg_key {
	uint32_t msg_no;
	uint32_t sock_fd;
} *msg_key_t;

static int msg_comparator(void *a, const void *b)
{
	msg_key_t ak = (msg_key_t)a;
	msg_key_t bk = (msg_key_t)b;
	int rc;

	rc = ak->sock_fd - bk->sock_fd;
	if (rc)
		return rc;
	return ak->msg_no - bk->msg_no;
}
struct rbt msg_tree = RBT_INITIALIZER(msg_comparator);
#define LINE_BUF_LEN 1024
#define REQ_BUF_LEN 4096
#define REP_BUF_LEN REQ_BUF_LEN
typedef struct req_msg {
	struct msg_key key;
	struct rbn rbn;
	struct ldmsd_req_hdr_s rh;
	struct msghdr *mh;
	int rec_no;
	size_t line_len;
	char *line_buf;
	size_t req_len;
	size_t req_off;
	char *req_buf;
	size_t rep_len;
	size_t rep_off;
	char *rep_buf;
} *req_msg_t;

typedef int
(*ldmsd_request_handler_t)(int sock, req_msg_t rm);
struct request_handler_entry {
	int req_id;
	ldmsd_request_handler_t handler;
};

static int cli_handler(int sock, req_msg_t rm);
static int example_handler(int sock, req_msg_t rm);
static int prdcr_add_handler(int sock, req_msg_t rm);
static int prdcr_del_handler(int sock, req_msg_t rm);
static int prdcr_start_handler(int sock, req_msg_t rm);
static int prdcr_stop_handler(int sock, req_msg_t rm);
static int prdcr_start_regex_handler(int sock, req_msg_t rm);
static int prdcr_stop_regex_handler(int sock, req_msg_t rm);
static int prdcr_status_handler(int sock, req_msg_t rm);
static int prdcr_set_handler(int sock, req_msg_t rm);
static int strgp_add_handler(int sock, req_msg_t rm);
static int strgp_del_handler(int sock, req_msg_t rm);
static int strgp_start_handler(int sock, req_msg_t rm);
static int strgp_stop_handler(int sock, req_msg_t rm);
static int strgp_prdcr_add_handler(int sock, req_msg_t rm);
static int strgp_prdcr_del_handler(int sock, req_msg_t rm);
static int strgp_metric_add_handler(int sock, req_msg_t rm);
static int strgp_metric_del_handler(int sock, req_msg_t rm);
static int strgp_status_handler(int sock, req_msg_t rm);
static int updtr_add_handler(int sock, req_msg_t rm);
static int updtr_del_handler(int sock, req_msg_t rm);
static int updtr_prdcr_add_handler(int sock, req_msg_t rm);
static int updtr_prdcr_del_handler(int sock, req_msg_t rm);
static int updtr_match_add_handler(int sock, req_msg_t rm);
static int updtr_match_del_handler(int sock, req_msg_t rm);
static int updtr_start_handler(int sock, req_msg_t rm);
static int updtr_stop_handler(int sock, req_msg_t rm);
static int updtr_status_handler(int sock, req_msg_t rm);
static int plugn_status_handler(int sock, req_msg_t rm);
static int unimplemented_handler(int sock, req_msg_t rm);

static struct request_handler_entry request_handler[] = {
	[LDMSD_CLI_REQ]          = { LDMSD_CLI_REQ, cli_handler },
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
	[LDMSD_PLUGN_STATUS_REQ] = { LDMSD_PLUGN_STATUS_REQ, plugn_status_handler },
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
static req_msg_t find_msg(struct msg_key *key)
{
	req_msg_t rm = NULL;
	struct rbn *rbn = rbt_find(&msg_tree, key);
	if (rbn)
		rm = container_of(rbn, struct req_msg, rbn);
	return rm;
}

static void free_msg(req_msg_t rm)
{
	rbt_del(&msg_tree, &rm->rbn);
	if (rm->line_buf)
		free(rm->line_buf);
	if (rm->req_buf)
		free(rm->req_buf);
	if (rm->rep_buf)
		free(rm->rep_buf);
	free(rm);
}

static req_msg_t alloc_msg(struct msg_key *key)
{
	req_msg_t rm;
	rm = calloc(1, sizeof *rm);
	if (!rm)
		return NULL;
	rm->line_len = LINE_BUF_LEN;
	rm->line_buf = malloc(LINE_BUF_LEN);
	if (!rm->line_buf)
		goto err;
	rm->req_len = REQ_BUF_LEN;
	rm->req_off = 0;
	rm->req_buf = malloc(REQ_BUF_LEN);
	if (!rm->req_buf)
		goto err;
	rm->req_buf[0] = '\0';
	rm->rep_len = REP_BUF_LEN;
	rm->rep_off = 0;
	rm->rep_buf = malloc(REP_BUF_LEN);
	if (!rm->rep_buf)
		goto err;
	rm->rep_buf[0] = '\0';
	rm->key = *key;
	rbn_init(&rm->rbn, &rm->key);
	rbt_ins(&msg_tree, &rm->rbn);
	return rm;
 err:
	free_msg(rm);
	return NULL;
}

static int gather_data(req_msg_t rm, struct msghdr *msg, size_t msg_len)
{
	int i;
	size_t copylen;
	size_t copyoff;
	ldmsd_req_hdr_t request = msg->msg_iov[0].iov_base;
	size_t remaining = rm->req_len - rm->req_off;
	if (msg_len > remaining) {
		size_t new_size = msg_len + REQ_BUF_LEN;
		if (new_size < rm->req_len + msg_len)
			new_size = rm->req_len + msg_len;
		rm->req_buf = realloc(rm->req_buf, new_size);
		if (!rm->req_buf)
			return ENOMEM;
		rm->req_len = new_size;
	}
	copyoff = sizeof(*request);
	for (i = 0; i < msg->msg_iovlen; i++) {
		if (msg->msg_iov[i].iov_len <= copyoff) {
			copyoff -= msg->msg_iov[i].iov_len;
			continue;
		}
		copylen = msg->msg_iov[i].iov_len - copyoff;
		memcpy(&rm->req_buf[rm->req_off],
		       msg->msg_iov[i].iov_base + copyoff,
		       copylen);
		rm->req_off += copylen;
		copyoff = 0;
	}
	rm->req_buf[rm->req_off] = '\0';
	return 0;
}

int process_request(int fd, struct msghdr *msg, size_t msg_len)
{
	ldmsd_req_hdr_t request = msg->msg_iov[0].iov_base;
	struct msg_key key;
	req_msg_t rm;
	int rc;

	key.msg_no = request->msg_no;
	key.sock_fd = fd;

	pthread_mutex_lock(&msg_tree_lock);
	if (request->flags & LDMSD_REQ_SOM_F) {
		/* Ensure that we don't already have this message in
		 * the tree */
		rm = find_msg(&key);
		if (rm) {
			ldmsd_log(LDMSD_LERROR,
				  "Duplicate message number %d:%d received\n",
				  key.msg_no, key.sock_fd);
			goto err_out;
		}
		rm = alloc_msg(&key);
		if (rm)
			memcpy(&rm->rh, request, sizeof(*request));
	} else {
		rm = find_msg(&key);
		if (!rm)
			ldmsd_log(LDMSD_LERROR,
				  "The message no %d:%d was not found\n",
				  key.msg_no, key.sock_fd);
	}
	if (!rm)
		goto err_out;

	if (gather_data(rm, msg, msg_len))
		goto err_out;

	pthread_mutex_unlock(&msg_tree_lock);
	if (0 == (request->flags & LDMSD_REQ_EOM_F))
		return 0;

	rm->mh = msg;

	/* Check for request id outside of range */
	if (request->cmd_id < 0 ||
	    request->cmd_id >= (sizeof(request_handler)/sizeof(request_handler[0])))
		rc = unimplemented_handler(fd, rm);
	else if (!request_handler[request->cmd_id].handler)
		/* Check for unimplemented request */
		rc = unimplemented_handler(fd, rm);
	else
		rc = request_handler[request->cmd_id].handler(fd, rm);
	pthread_mutex_lock(&msg_tree_lock);
	free_msg(rm);
	pthread_mutex_unlock(&msg_tree_lock);
	return rc;
 err_out:
	pthread_mutex_unlock(&msg_tree_lock);
	return -1;
}

static int
send_request_reply(int sock, req_msg_t rm,
		   char *data, size_t data_len,
		   int msg_flags)
{
	struct ldmsd_req_hdr_s req_reply;
	struct msghdr reply_msg;
	struct iovec iov[2];
	size_t remaining;

	do {
		remaining = rm->rep_len - rm->rep_off;
		if (data_len < remaining)
			remaining = data_len;

		if (remaining && data) {
			memcpy(&rm->rep_buf[rm->rep_off], data, remaining);
			rm->rep_off += remaining;
			data_len -= remaining;
			data += remaining;
		}

		if ((remaining == 0) ||
		    ((data_len == 0) && (msg_flags & LDMSD_REQ_EOM_F))) {
			ldmsd_log(LDMSD_LERROR, "remaining %d data_len %d\n", remaining, data_len);
			msg_flags =
				(rm->rec_no == 0?LDMSD_REQ_SOM_F:0)
				| (msg_flags & LDMSD_REQ_EOM_F);
			/* Record is full, send it on it's way */
			memcpy(&req_reply, &rm->rh, sizeof rm->rh);
			req_reply.flags = msg_flags;
			req_reply.rec_len = rm->rep_off + sizeof(req_reply);

			reply_msg.msg_name = rm->mh->msg_name;
			reply_msg.msg_namelen = rm->mh->msg_namelen;
			iov[0].iov_base = &req_reply;
			iov[0].iov_len = sizeof(req_reply);
			iov[1].iov_base = rm->rep_buf;
			iov[1].iov_len = rm->rep_off,
			reply_msg.msg_iov = iov;
			reply_msg.msg_iovlen = 2;
			reply_msg.msg_control = NULL;
			reply_msg.msg_controllen = 0;
			reply_msg.msg_flags = 0;
			if (sendmsg(sock, &reply_msg, 0) < 0)
				return errno;
			rm->rec_no ++;
			rm->rep_off = 0;
			rm->rep_buf[0] = '\0';
		}
	} while (data_len);

	return 0;
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

#define Snprintf_error(dst, len, fmt, ...) \
	Snprintf(dst, len, "{\"error\":\""fmt"\"}", ##__VA_ARGS__)

/* kw_table must ordered in strcasecmp order, for use with bsearch. */
struct kw {
	char *token_str;
	int token_id;
} kw_table[] = {
	{ "CONFIG", LDMSCTL_CFG_PLUGIN },
	{ "ENV", LDMSCTL_ENV },
	{ "EXIT", LDMSCTL_EXIT_DAEMON },
	{ "INCLUDE", LDMSCTL_INCLUDE },
	{ "INFO", LDMSCTL_INFO_DAEMON },
	{ "LIST", LDMSCTL_LIST_PLUGINS },
	{ "LOAD", LDMSCTL_LOAD_PLUGIN },
	{ "ONESHOT_SAMPLE", LDMSCTL_ONESHOT_SAMPLE },
	{ "PRDCR_ADD", LDMSCTL_PRDCR_ADD },
	{ "PRDCR_DEL", LDMSCTL_PRDCR_DEL },
	{ "PRDCR_START", LDMSCTL_PRDCR_START },
	{ "PRDCR_START_REGEX", LDMSCTL_PRDCR_START_REGEX },
	{ "PRDCR_STOP", LDMSCTL_PRDCR_STOP },
	{ "PRDCR_STOP_REGEX", LDMSCTL_PRDCR_STOP_REGEX },
	{ "SET_UDATA", LDMSCTL_SET_UDATA },
	{ "SET_UDATA_REGEX", LDMSCTL_SET_UDATA_REGEX },
	{ "START", LDMSCTL_START_SAMPLER },
	{ "STOP", LDMSCTL_STOP_SAMPLER },
	{ "STRGP_ADD", LDMSCTL_STRGP_ADD },
	{ "STRGP_DEL", LDMSCTL_STRGP_DEL },
	{ "STRGP_METRIC_ADD", LDMSCTL_STRGP_METRIC_ADD },
	{ "STRGP_METRIC_DEL", LDMSCTL_STRGP_METRIC_DEL },
	{ "STRGP_PRDCR_ADD", LDMSCTL_STRGP_PRDCR_ADD },
	{ "STRGP_PRDCR_DEL", LDMSCTL_STRGP_PRDCR_DEL },
	{ "STRGP_START", LDMSCTL_STRGP_START },
	{ "STRGP_STOP", LDMSCTL_STRGP_STOP },
	{ "TERM", LDMSCTL_TERM_PLUGIN },
	{ "UPDTR_ADD", LDMSCTL_UPDTR_ADD },
	{ "UPDTR_DEL", LDMSCTL_UPDTR_DEL },
	{ "UPDTR_MATCH_ADD", LDMSCTL_UPDTR_MATCH_ADD },
	{ "UPDTR_MATCH_DEL", LDMSCTL_UPDTR_MATCH_DEL },
	{ "UPDTR_PRDCR_ADD", LDMSCTL_UPDTR_PRDCR_ADD },
	{ "UPDTR_PRDCR_DEL", LDMSCTL_UPDTR_PRDCR_DEL },
	{ "UPDTR_START", LDMSCTL_UPDTR_START },
	{ "UPDTR_STOP", LDMSCTL_UPDTR_STOP },
	{ "VERBOSE", LDMSCTL_VERBOSE },
	{ "VERSION", LDMSCTL_VERSION },
};

static int kw_cmp(const void *a, const void *b)
{
	struct kw *_a = (struct kw *)a;
	struct kw *_b = (struct kw *)b;
	return strcasecmp(_a->token_str, _b->token_str);
}

static int cli_handler(int sock, req_msg_t rm)
{
	char *cmd_s;
	long cmd_id;
	struct attr_value_list *av_list;
	struct attr_value_list *kw_list;
	int tokens, rc;

	/*
	 * Count the number of spaces. That's the maximum number of
	 * tokens that could be present
	 */
	for (tokens = 0, cmd_s = rm->req_buf; cmd_s[0] != '\0';) {
		tokens++;
		/* find whitespace */
		while (cmd_s[0] != '\0' && !isspace(cmd_s[0]))
			cmd_s++;
		/* Now skip whitepace to next token */
		while (cmd_s[0] != '\0' && isspace(cmd_s[0]))
			cmd_s++;
	}
	rc = ENOMEM;
	av_list = av_new(tokens);
	kw_list = av_new(tokens);
	if (!av_list || !kw_list)
		goto out;
	rc = tokenize(rm->req_buf, kw_list, av_list);
	if (rc) {
		ldmsd_log(LDMSD_LERROR, "ENOMEM processing %d byte request",
			  rm->req_off);
		rc = ENOMEM;
		goto out;
	}

	cmd_s = av_name(kw_list, 0);
	if (!cmd_s) {
		ldmsd_log(LDMSD_LERROR, "EINVAL request is missing request Id\n");
		rc = EINVAL;
		goto out;
	}

	if (isalpha(cmd_s[0])) {
		struct kw key;
		key.token_str = cmd_s;
		struct kw *kw = bsearch(&key, kw_table,
					sizeof(kw_table) / sizeof(kw_table[0]),
					sizeof(kw_table[0]),
					kw_cmp);
		if (!kw) {
			ldmsd_log(LDMSD_LERROR, "Unrecognized keyword %s\n", cmd_s);
			rc = EINVAL;
			goto out;
		}
		cmd_id = kw->token_id;
	} else
		cmd_id = strtoul(cmd_s, NULL, 0);
	if (cmd_id >= 0 && cmd_id <= LDMSCTL_LAST_COMMAND) {
		if (cmd_table[cmd_id]) {
			rc = cmd_table[cmd_id](rm->rep_buf, av_list, kw_list);
			goto out;
		}
	}
	rc = send_request_reply(sock, rm, "22Invalid command", 18,
				LDMSD_REQ_SOM_F|LDMSD_REQ_EOM_F);
	free(kw_list);
	free(av_list);
	return rc;
 out:
	rm->rep_off = strlen(rm->rep_buf) + 1;
	rc = send_request_reply(sock, rm, NULL, 0, LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	if (kw_list)
		av_free(kw_list);
	if (av_list)
		av_free(av_list);
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
static int example_handler(int sock, req_msg_t rm)
{
	size_t cnt;
	int rc, count = 0;
	ldmsd_req_attr_t attr = (ldmsd_req_attr_t)rm->req_buf;
	rc = send_request_reply(sock, rm, "[", 1, LDMSD_REQ_SOM_F);
	while (attr->discrim) {
		if (count)
			rc = send_request_reply(sock, rm, ",\n", 2, 0);

		cnt = Snprintf(&rm->line_buf, &rm->line_len,
			       "{ \"attr_len\":%d,"
			       "\"attr_id\":%d,"
			       "\"attr_value\": \"%s\" }",
			       attr->attr_len,
			       attr->attr_id,
			       (char *)attr->attr_value);
		rc = send_request_reply(sock, rm, rm->line_buf, cnt, 0);
		count++;
		attr = (ldmsd_req_attr_t)&attr->attr_value[attr->attr_len];
	}
	rc = send_request_reply(sock, rm, "]", 1, LDMSD_REQ_EOM_F);
	return rc;
}

static int prdcr_add_handler(int sock, req_msg_t rm)
{
	ldmsd_prdcr_t prdcr;
	char *name, *host, *xprt, *attr_name;
	name = host = xprt = NULL;
	enum ldmsd_prdcr_type type = -1;
	short port_no = -1;
	int interval_us = -1;
	size_t cnt;

	ldmsd_req_attr_t attr;
	attr = (ldmsd_req_attr_t)rm->req_buf;
	while (attr->discrim) {
		switch (attr->attr_id) {
		case LDMSD_ATTR_NAME:
			name = (char *)attr->attr_value;
			break;
		case LDMSD_ATTR_TYPE:
			type = ldmsd_prdcr_str2type((char *)attr->attr_value);
			break;
		case LDMSD_ATTR_XPRT:
			xprt = (char *)attr->attr_value;
			break;
		case LDMSD_ATTR_HOST:
			host = (char *)attr->attr_value;
			break;
		case LDMSD_ATTR_PORT:
			port_no = strtol((char *)attr->attr_value, NULL, 0);
			break;
		case LDMSD_ATTR_INTERVAL:
			interval_us = strtol((char *)attr->attr_value, NULL, 0);
			break;
		default:
			break;
		}
		attr = (ldmsd_req_attr_t)&attr->attr_value[attr->attr_len];
	}
	if (!name) {
		attr_name = "name";
		goto einval;
	}
	if (type < 0) {
		attr_name = "type";
		goto einval;
	}
	if (type == EINVAL) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len, "%dThe attribute "
					"type '%s' is invalid.", EINVAL, type);
		goto send_reply;
	}
	if (type == LDMSD_PRDCR_TYPE_LOCAL)
		goto out;

	if (!xprt) {
		attr_name = "xprt";
		goto einval;
	}
	if (!host) {
		attr_name = "host";
		goto einval;
	}
	if (port_no < 0) {
		attr_name = "port";
		goto einval;
	}
	if (interval_us < 0) {
		attr_name = "interval";
		goto einval;
	}
out:
	prdcr = ldmsd_prdcr_new(name, xprt, host, port_no, type, interval_us);
	if (!prdcr) {
		if (errno == EEXIST)
			goto eexist;
		else
			goto enomem;
	}
	cnt = Snprintf(&rm->line_buf, &rm->line_len, "0");
	goto send_reply;
enomem:
	cnt = Snprintf_error(&rm->line_buf, &rm->line_len, "%dMemory allocation "
							"failed.", ENOMEM);
	goto send_reply;
eexist:
	cnt = Snprintf_error(&rm->line_buf, &rm->line_len, "%dThe prdcr %s already "
						"exists.", EEXIST, name);
	goto send_reply;
einval:
	cnt = Snprintf_error(&rm->line_buf, &rm->line_len, "%dThis attribute '%s' "
					"is required.", EINVAL, attr_name);
send_reply:
	(void) send_request_reply(sock, rm, rm->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	return 0;
}

static int prdcr_del_handler(int sock, req_msg_t rm)
{
	char *name = NULL, *attr_name;
	size_t cnt;
	int rc;

	ldmsd_req_attr_t attr;
	attr = (ldmsd_req_attr_t)rm->req_buf;
	while (attr->discrim) {
		switch (attr->attr_id) {
		case LDMSD_ATTR_NAME:
			name = (char *)attr->attr_value;
			break;
		default:
			break;
		}
		attr = (ldmsd_req_attr_t)&attr->attr_value[attr->attr_len];
	}
	if (!name) {
		attr_name = "name";
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len, "%dThis attribute "
					"'%s' is required.", EINVAL, attr_name);
		goto send_reply;
	}

	rc = ldmsd_prdcr_del(name);
	if (rc == ENOENT) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len, "%dThe producer "
				"specified does not exist.", ENOENT);
	} else if (rc == EBUSY) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len, "%dThe producer "
						"is in use.", EBUSY);
	} else {
		cnt = Snprintf(&rm->line_buf, &rm->line_len, "0");
	}

send_reply:
	(void) send_request_reply(sock, rm, rm->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	return 0;
}

static int prdcr_start_handler(int sock, req_msg_t rm)
{
	char *name, *interval_str;
	name = interval_str = NULL;
	size_t cnt;
	ldmsd_req_attr_t attr;

	attr = (ldmsd_req_attr_t)rm->req_buf;
	while (attr->discrim) {
		switch (attr->attr_id) {
		case LDMSD_ATTR_NAME:
			name = (char *)attr->attr_value;
			break;
		case LDMSD_ATTR_INTERVAL:
			interval_str = (char *)attr->attr_value;
			break;
		default:
			break;
		}
		attr = (ldmsd_req_attr_t)&attr->attr_value[attr->attr_len];
	}
	if (!name) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len, "%dThis attribute "
						"'name' is required.", EINVAL);
		goto send_reply;
	}

	int rc = ldmsd_prdcr_start(name, interval_str);
	if (rc == EBUSY) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len, "%dThe producer "
						"is already running.", EBUSY);
	} else if (rc == ENOENT) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len, "%dThe producer "
					"specified does not exist.", ENOENT);
	} else {
		cnt = Snprintf(&rm->line_buf, &rm->line_len, "0");
	}

send_reply:
	(void) send_request_reply(sock, rm, rm->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	return 0;
}

static int prdcr_stop_handler(int sock, req_msg_t rm)
{
	char *name = NULL;
	size_t cnt;
	ldmsd_req_attr_t attr;

	attr = (ldmsd_req_attr_t)rm->req_buf;
	while (attr->discrim) {
		switch (attr->attr_id) {
		case LDMSD_ATTR_NAME:
			name = (char *)attr->attr_value;
			break;
		default:
			break;
		}
		attr = (ldmsd_req_attr_t)&attr->attr_value[attr->attr_len];
	}
	if (!name) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len, "%dThis attribute "
						"'name' is required.", EINVAL);
		goto send_reply;
	}

	int rc = ldmsd_prdcr_stop(name);
	if (rc == EBUSY) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len, "%dThe producer "
						"is already stopped.", EBUSY);
	} else if (rc == ENOENT) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len, "%dThe producer "
					"specified does not exist.", ENOENT);
	} else {
		cnt = Snprintf(&rm->line_buf, &rm->line_len, "0");
	}

send_reply:
	(void) send_request_reply(sock, rm, rm->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	return 0;
}

static int prdcr_start_regex_handler(int sock, req_msg_t rm)
{
	char *prdcr_regex, *interval_str;
	prdcr_regex = interval_str = NULL;
	size_t cnt;
	ldmsd_req_attr_t attr;

	attr = (ldmsd_req_attr_t)rm->req_buf;
	while (attr->discrim) {
		switch (attr->attr_id) {
		case LDMSD_ATTR_REGEX:
			prdcr_regex = (char *)attr->attr_value;
			break;
		case LDMSD_ATTR_INTERVAL:
			interval_str = (char *)attr->attr_value;
			break;
		default:
			break;
		}
		attr = (ldmsd_req_attr_t)&attr->attr_value[attr->attr_len];
	}
	if (!prdcr_regex) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len, "%dThis attribute "
						"'regex' is required.", EINVAL);
		goto send_reply;
	}
	int rc = ldmsd_prdcr_start_regex(prdcr_regex, interval_str,
					rm->line_buf, rm->line_len);
	if (rc)
		cnt = sizeof(rm->line_buf);
	else
		cnt = Snprintf(&rm->line_buf, &rm->line_len, "0");

send_reply:
	(void) send_request_reply(sock, rm, rm->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	return 0;
}

static int prdcr_stop_regex_handler(int sock, req_msg_t rm)
{
	char *prdcr_regex = NULL;
	size_t cnt;
	ldmsd_req_attr_t attr;

	attr = (ldmsd_req_attr_t)rm->req_buf;
	while (attr->discrim) {
		switch (attr->attr_id) {
		case LDMSD_ATTR_REGEX:
			prdcr_regex = (char *)attr->attr_value;
			break;
		default:
			break;
		}
		attr = (ldmsd_req_attr_t)&attr->attr_value[attr->attr_len];
	}
	if (!prdcr_regex) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len, "%dThis attribute "
						"'regex' is required.", EINVAL);
		goto send_reply;
	}
	int rc = ldmsd_prdcr_stop_regex(prdcr_regex, rm->line_buf, rm->line_len);
	if (rc)
		cnt = sizeof(rm->line_buf);
	else
		cnt = Snprintf(&rm->line_buf, &rm->line_len, "0");

send_reply:
	(void) send_request_reply(sock, rm, rm->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	return 0;
}

static int prdcr_status_handler(int sock, req_msg_t rm)
{
	extern const char *prdcr_state_str(enum ldmsd_prdcr_state state);
	ldmsd_prdcr_t prdcr;
	size_t cnt;
	int rc, count = 0;

	ldmsd_cfg_lock(LDMSD_CFGOBJ_PRDCR);
	rc = send_request_reply(sock, rm, "[", 1, LDMSD_REQ_SOM_F);
	for (prdcr = ldmsd_prdcr_first(); prdcr;
	     prdcr = ldmsd_prdcr_next(prdcr)) {

		if (count)
			rc = send_request_reply(sock, rm, ",\n", 2, 0);

		cnt = Snprintf(&rm->line_buf, &rm->line_len,
			       "{ \"name\":\"%s\","
			       "\"host\":\"%s\","
			       "\"port\":%hd,"
			       "\"transport\":\"%s\","
			       "\"state\":\"%s\","
			       "\"sets\": [",
			       prdcr->obj.name,
			       prdcr->host_name, prdcr->port_no, prdcr->xprt_name,
			       prdcr_state_str(prdcr->conn_state));
		rc = send_request_reply(sock, rm, rm->line_buf, cnt, 0);

		ldmsd_prdcr_lock(prdcr);
		ldmsd_prdcr_set_t prv_set;
		int set_count = 0;
		for (prv_set = ldmsd_prdcr_set_first(prdcr); prv_set;
		     prv_set = ldmsd_prdcr_set_next(prv_set)) {

			if (set_count)
				rc = send_request_reply(sock, rm, ",\n", 2, 0);

			cnt = Snprintf(&rm->line_buf, &rm->line_len,
				       "{ \"inst_name\":\"%s\","
				       "\"schema_name\":\"%s\","
				       "\"state\":\"%s\"}",
				       prv_set->inst_name, prv_set->schema_name,
				       ldmsd_prdcr_set_state_str(prv_set->state));
			rc = send_request_reply(sock, rm, rm->line_buf, cnt, 0);
			set_count++;
		}
		ldmsd_prdcr_unlock(prdcr);
		rc = send_request_reply(sock, rm, "]}", 2, 0);
		count++;
	}
	rc = send_request_reply(sock, rm, "]", 1, LDMSD_REQ_EOM_F);
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR);
	return rc;
}

static int prdcr_set_handler(int sock, req_msg_t rm)
{
	ldmsd_prdcr_t prdcr;
	ldmsd_req_attr_t attr = (ldmsd_req_attr_t)rm->req_buf;
	size_t cnt;
	int rc, count = 0;

	rc = send_request_reply(sock, rm, "[", 1, LDMSD_REQ_SOM_F);
	prdcr = ldmsd_prdcr_find((char *)attr->attr_value);
	if (!prdcr) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len,
				"Producer %s does not exist.",
				attr->attr_value);
		rc = send_request_reply(sock, rm, rm->line_buf, cnt, 0);
		goto out;
	}
	ldmsd_prdcr_lock(prdcr);
	ldmsd_prdcr_set_t met_set;
	for (met_set = ldmsd_prdcr_set_first(prdcr); met_set;
		met_set = ldmsd_prdcr_set_next(met_set)) {
		if (count)
			rc = send_request_reply(sock, rm, ",\n", 2, 0);
		cnt = Snprintf(&rm->line_buf, &rm->line_len,
				"{ \"inst_name\":\"%s\","
				"\"schema_name\":\"%s\","
				"\"state\":\"%s\"}",
				met_set->inst_name, met_set->schema_name,
				ldmsd_prdcr_set_state_str(met_set->state));
		rc = send_request_reply(sock, rm, rm->line_buf, cnt, 0);
		count++;
	}
	ldmsd_prdcr_unlock(prdcr);
out:
	rc = send_request_reply(sock, rm, "]", 1, LDMSD_REQ_EOM_F);
	return rc;
}

static int strgp_add_handler(int sock, req_msg_t rm)
{
	char *attr_name, *name, *plugin, *container, *schema;
	name = plugin = container = schema = NULL;
	ldmsd_req_attr_t attr;
	size_t cnt;

	attr = (ldmsd_req_attr_t)rm->req_buf;
	while (attr->discrim) {
		switch (attr->attr_id) {
		case LDMSD_ATTR_NAME:
			name = (char *)attr->attr_value;
			break;
		case LDMSD_ATTR_PLUGIN:
			plugin = (char *)attr->attr_value;
			break;
		case LDMSD_ATTR_CONTAINER:
			container = (char *)attr->attr_value;
			break;
		case LDMSD_ATTR_SCHEMA:
			schema = (char *)attr->attr_value;
			break;
		default:
			break;
		}
		attr = (ldmsd_req_attr_t)&attr->attr_value[attr->attr_len];
	}

	if (!name) {
		attr_name = "name";
		goto einval;
	}
	if (!plugin) {
		attr_name = "plugin";
		goto einval;
	}
	if (!container) {
		attr_name = "container";
		goto einval;
	}
	if (!schema) {
		attr_name = "schema";
		goto einval;
	}

	struct ldmsd_plugin_cfg *store;
	store = ldmsd_get_plugin(plugin);
	if (!store) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len, "%dThe plugin "
						"does not exist.\n", EINVAL);
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

	cnt = Snprintf(&rm->line_buf, &rm->line_len, "0");
	goto send_reply;

enomem_3:
	free(strgp->schema);
enomem_2:
	free(strgp->plugin_name);
enomem_1:
	free(strgp);
enomem:
	cnt = Snprintf_error(&rm->line_buf, &rm->line_len, "%dMemory allocation "
							"failed.", ENOMEM);
	goto send_reply;
eexist:
	cnt = Snprintf_error(&rm->line_buf, &rm->line_len, "%dThe prdcr %s already "
						"exists.", EEXIST, name);
	goto send_reply;
einval:
	cnt = Snprintf_error(&rm->line_buf, &rm->line_len, "%dThis attribute '%s' "
					"is required.", EINVAL, attr_name);
send_reply:
	(void) send_request_reply(sock, rm, rm->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	return 0;
}

static int strgp_del_handler(int sock, req_msg_t rm)
{
	char *name = NULL;
	ldmsd_req_attr_t attr;
	size_t cnt;

	attr = (ldmsd_req_attr_t)rm->req_buf;
	while (attr->discrim) {
		switch (attr->attr_id) {
		case LDMSD_ATTR_NAME:
			name = (char *)attr->attr_value;
			break;
		default:
			break;
		}
		attr = (ldmsd_req_attr_t)&attr->attr_value[attr->attr_len];
	}

	if (!name) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len, "%dThis "
			"attribute 'name' is required.", EINVAL);
		goto send_reply;
	}

	int rc = ldmsd_strgp_del(name);
	if (rc == ENOENT) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len, "%dThe storage "
				"policy specified does not exist.", ENOENT);
	} else if (rc == EBUSY) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len, "%dThe storage "
				"policy is in use.", EBUSY);
	} else {
		cnt = Snprintf(&rm->line_buf, &rm->line_len, "0");
	}

send_reply:
	(void) send_request_reply(sock, rm, rm->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	return 0;
}

static int strgp_prdcr_add_handler(int sock, req_msg_t rm)
{
	char *name, *regex_str, *attr_name;
	name = regex_str = NULL;
	ldmsd_req_attr_t attr;
	size_t cnt = 0;

	attr = (ldmsd_req_attr_t)rm->req_buf;
	while (attr->discrim) {
		switch (attr->attr_id) {
		case LDMSD_ATTR_NAME:
			name = (char *)attr->attr_value;
			break;
		case LDMSD_ATTR_REGEX:
			regex_str = (char *)attr->attr_value;
			break;
		default:
			break;
		}
		attr = (ldmsd_req_attr_t)&attr->attr_value[attr->attr_len];
	}

	if (!name) {
		attr_name = "name";
		goto einval;
	}
	if (!regex_str) {
		attr_name = "regex";
		goto einval;
	}

	int rc = ldmsd_strgp_prdcr_add(name, regex_str,
				rm->line_buf, rm->line_len);
	if (rc) {
		if (rc == ENOENT) {
			cnt = Snprintf_error(&rm->line_buf, &rm->line_len,
					"%dThe storage policy specified "
					"does not exist\n", ENOENT);
		} else if (rc == EBUSY) {
			cnt = Snprintf_error(&rm->line_buf, &rm->line_len,
				"%dConfiguration changes cannot be made "
				"while the storage policy is running\n", EBUSY);
		} else if (rc == ENOMEM) {
			cnt = Snprintf_error(&rm->line_buf, &rm->line_len,
					"%dOut of memory.\n", ENOMEM);
		}
	} else {
		cnt = Snprintf(&rm->line_buf, &rm->line_len, "0");
	}
	goto send_reply;
einval:
	cnt = Snprintf_error(&rm->line_buf, &rm->line_len, "%dThis attribute '%s' "
					"is required.", EINVAL, attr_name);
send_reply:
	(void) send_request_reply(sock, rm, rm->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	return 0;
}

static int strgp_prdcr_del_handler(int sock, req_msg_t rm)
{
	char *name, *regex_str, *attr_name;
	name = regex_str = NULL;
	ldmsd_req_attr_t attr;
	size_t cnt = 0;

	attr = (ldmsd_req_attr_t)rm->req_buf;
	while (attr->discrim) {
		switch (attr->attr_id) {
		case LDMSD_ATTR_NAME:
			name = (char *)attr->attr_value;
			break;
		case LDMSD_ATTR_REGEX:
			regex_str = (char *)attr->attr_value;
			break;
		default:
			break;
		}
		attr = (ldmsd_req_attr_t)&attr->attr_value[attr->attr_len];
	}

	if (!name) {
		attr_name = "name";
		goto einval;
	}
	if (!regex_str) {
		attr_name = "regex";
		goto einval;
	}

	int rc = ldmsd_strgp_prdcr_add(name, regex_str,
				rm->line_buf, rm->line_len);
	if (rc == ENOENT) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len,
				"%dThe storage policy specified "
				"does not exist\n", ENOENT);
	} else if (rc == EBUSY) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len,
			"%dConfiguration changes cannot be made "
			"while the storage policy is running\n", EBUSY);
	} else if (rc == EEXIST) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len,
				"%dThe specified regex does not match "
				"any condition\n", ENOENT);
	} else {
		cnt = Snprintf(&rm->line_buf, &rm->line_len, "0");
	}
	goto send_reply;
einval:
	cnt = Snprintf_error(&rm->line_buf, &rm->line_len, "%dThis attribute '%s' "
					"is required.", EINVAL, attr_name);
send_reply:
	(void) send_request_reply(sock, rm, rm->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	return 0;
}

static int strgp_metric_add_handler(int sock, req_msg_t rm)
{
	char *name, *metric_name, *attr_name;
	name = metric_name = NULL;
	ldmsd_req_attr_t attr;
	size_t cnt;

	attr = (ldmsd_req_attr_t)rm->req_buf;
	while (attr->discrim) {
		switch (attr->attr_id) {
		case LDMSD_ATTR_NAME:
			name = (char *)attr->attr_value;
			break;
		case LDMSD_ATTR_METRIC:
			metric_name = (char *)attr->attr_value;
			break;
		default:
			break;
		}
		attr = (ldmsd_req_attr_t)&attr->attr_value[attr->attr_len];
	}

	if (!name) {
		attr_name = "name";
		goto einval;
	}
	if (!metric_name) {
		attr_name = "metric";
		goto einval;
	}

	int rc = ldmsd_strgp_metric_add(name, metric_name);
	if (rc == ENOENT) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len,
				"%dThe storage policy specified "
				"does not exist\n", ENOENT);
	} else if (rc == EBUSY) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len,
			"%dConfiguration changes cannot be made "
			"while the storage policy is running\n", EBUSY);
	} else if (rc == EEXIST) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len,
				"%dThe specified metric is already "
				"present.\n", EEXIST);
	} else if (rc == ENOMEM) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len,
				"%dMemory allocation failure.\n", ENOMEM);
	} else {
		cnt = Snprintf(&rm->line_buf, &rm->line_len, "0");
	}
	goto send_reply;
einval:
	cnt = Snprintf_error(&rm->line_buf, &rm->line_len, "%dThis attribute '%s' "
					"is required.", EINVAL, attr_name);
send_reply:
	(void) send_request_reply(sock, rm, rm->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	return 0;
}

static int strgp_metric_del_handler(int sock, req_msg_t rm)
{
	char *name, *metric_name, *attr_name;
	name = metric_name = NULL;
	ldmsd_req_attr_t attr;
	size_t cnt;

	attr = (ldmsd_req_attr_t)rm->req_buf;
	while (attr->discrim) {
		switch (attr->attr_id) {
		case LDMSD_ATTR_NAME:
			name = (char *)attr->attr_value;
			break;
		case LDMSD_ATTR_METRIC:
			metric_name = (char *)attr->attr_value;
			break;
		default:
			break;
		}
		attr = (ldmsd_req_attr_t)&attr->attr_value[attr->attr_len];
	}

	if (!name) {
		attr_name = "name";
		goto einval;
	}
	if (!metric_name) {
		attr_name = "metric";
		goto einval;
	}

	int rc = ldmsd_strgp_metric_del(name, metric_name);
	if (rc == ENOENT) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len,
				"%dThe storage policy specified "
				"does not exist\n", ENOENT);
	} else if (rc == EBUSY) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len,
			"%dConfiguration changes cannot be made "
			"while the storage policy is running\n", EBUSY);
	} else if (rc == EEXIST) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len,
				"%dThe specified metric was not found.\n",
				EEXIST);
	} else {
		cnt = Snprintf(&rm->line_buf, &rm->line_len, "0");
	}
	goto send_reply;
einval:
	cnt = Snprintf_error(&rm->line_buf, &rm->line_len, "%dThis attribute '%s' "
					"is required.", EINVAL, attr_name);
send_reply:
	(void) send_request_reply(sock, rm, rm->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	return 0;
}

static int strgp_start_handler(int sock, req_msg_t rm)
{
	char *name, *attr_name;
	name = NULL;
	ldmsd_req_attr_t attr;
	size_t cnt;

	attr = (ldmsd_req_attr_t)rm->req_buf;
	while (attr->discrim) {
		switch (attr->attr_id) {
		case LDMSD_ATTR_NAME:
			name = (char *)attr->attr_value;
			break;
		default:
			break;
		}
		attr = (ldmsd_req_attr_t)&attr->attr_value[attr->attr_len];
	}

	if (!name) {
		attr_name = "name";
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len,
				"%dThis attribute '%s' is required.",
				EINVAL, attr_name);
		goto send_reply;
	}

	ldmsd_strgp_t strgp = ldmsd_strgp_find(name);
	if (!strgp) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len,
			"%dThe storage policy does not exist.\n", ENOENT);
		goto out_1;
	}
	ldmsd_strgp_lock(strgp);
	if (strgp->state != LDMSD_STRGP_STATE_STOPPED) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len,
			"%dThe storage policy is already running\n", EBUSY);
		goto out_1;
	}
	strgp->state = LDMSD_STRGP_STATE_RUNNING;
	/* Update all the producers of our changed state */
	ldmsd_prdcr_update(strgp);
	cnt = Snprintf(&rm->line_buf, &rm->line_len, "0\n");

out_1:
	ldmsd_strgp_unlock(strgp);
	ldmsd_strgp_put(strgp);
send_reply:
	(void) send_request_reply(sock, rm, rm->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	return 0;
}

static int strgp_stop_handler(int sock, req_msg_t rm)
{
	char *name, *attr_name;
	name = NULL;
	ldmsd_req_attr_t attr;
	size_t cnt;

	attr = (ldmsd_req_attr_t)rm->req_buf;
	while (attr->discrim) {
		switch (attr->attr_id) {
		case LDMSD_ATTR_NAME:
			name = (char *)attr->attr_value;
			break;
		default:
			break;
		}
		attr = (ldmsd_req_attr_t)&attr->attr_value[attr->attr_len];
	}

	if (!name) {
		attr_name = "name";
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len,
				"%dThis attribute '%s' is required.",
				EINVAL, attr_name);
		goto send_reply;
	}

	int rc = ldmsd_strgp_stop(name);
	if (rc == ENOENT) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len,
			"%dThe storage policy does not exist.", ENOENT);
	} else if (rc == EBUSY) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len,
			"%dThe storage policy is not running.", EBUSY);
	} else {
		cnt = Snprintf(&rm->line_buf, &rm->line_len, "0");
	}
send_reply:
	(void) send_request_reply(sock, rm, rm->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	return 0;
}

static int strgp_status_handler(int sock, req_msg_t rm)
{
	ldmsd_strgp_t strgp;
	size_t cnt;
	int rc, metric_count, match_count, count = 0;
	ldmsd_name_match_t match;
	ldmsd_strgp_metric_t metric;

	ldmsd_cfg_lock(LDMSD_CFGOBJ_STRGP);
	rc = send_request_reply(sock, rm, "[", 1, 0);
	for (strgp = ldmsd_strgp_first(); strgp; strgp = ldmsd_strgp_next(strgp)) {
		if (count)
			rc = send_request_reply(sock, rm, ",\n", 2, 0);
		ldmsd_strgp_lock(strgp);
		cnt = Snprintf(&rm->line_buf, &rm->line_len,
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
		rc = send_request_reply(sock, rm, rm->line_buf, cnt, 0);
		match_count = 0;
		for (match = ldmsd_strgp_prdcr_first(strgp); match;
		     match = ldmsd_strgp_prdcr_next(match)) {
			if (match_count)
				rc = send_request_reply(sock, rm, ",", 1, 0);
			match_count++;
			cnt = Snprintf(&rm->line_buf, &rm->line_len,
				       "\"%s\"", match->regex_str);
			rc = send_request_reply(sock, rm, rm->line_buf, cnt, 0);
		}
		rc = send_request_reply(sock, rm, "],\"metrics\":[", 13, 0);
		metric_count = 0;
		for (metric = ldmsd_strgp_metric_first(strgp); metric;
		     metric = ldmsd_strgp_metric_next(metric)) {
			if (metric_count)
				rc = send_request_reply(sock, rm, ",", 1, 0);
			metric_count++;
			cnt = Snprintf(&rm->line_buf, &rm->line_len,
				       "\"%s\"", metric->name);
			rc = send_request_reply(sock, rm, rm->line_buf, cnt, 0);
		}
		rc = send_request_reply(sock, rm, "]}", 2, 0);
		ldmsd_strgp_unlock(strgp);
		count++;
	}
	rc = send_request_reply(sock, rm, "]", 1, LDMSD_REQ_EOM_F);
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_STRGP);
	return rc;
}

static int updtr_add_handler(int sock, req_msg_t rm)
{
	char *name, *offset_str, *interval_str, *attr_name;
	name = offset_str = interval_str = NULL;
	size_t cnt;
	ldmsd_req_attr_t attr;

	attr = (ldmsd_req_attr_t)rm->req_buf;
	while (attr->discrim) {
		switch (attr->attr_id) {
		case LDMSD_ATTR_NAME:
			name = (char *)attr->attr_value;
			break;
		case LDMSD_ATTR_INTERVAL:
			interval_str = (char *)attr->attr_value;
			break;
		case LDMSD_ATTR_OFFSET:
			offset_str = (char *)attr->attr_value;
			break;
		default:
			break;
		}
		attr = (ldmsd_req_attr_t)&attr->attr_value[attr->attr_len];
	}
	if (!name) {
		attr_name = "name";
		goto einval;
	}
	if (!interval_str) {
		attr_name = "interval";
		goto einval;
	}

	ldmsd_updtr_t updtr = ldmsd_updtr_new(name);
	if (!updtr) {
		if (errno == EEXIST)
			goto eexist;
		else if (errno == ENOMEM)
			goto enomem;
		else
			goto out;
	}

	updtr->updt_intrvl_us = strtol(interval_str, NULL, 0);
	if (offset_str) {
		updtr->updt_offset_us = strtol(offset_str, NULL, 0);
		updtr->updt_task_flags = LDMSD_TASK_F_SYNCHRONOUS;
	} else {
		updtr->updt_task_flags = 0;
	}
	goto out;

einval:
	cnt = Snprintf_error(&rm->line_buf, &rm->line_len, "%dThis attribute '%s' "
					"is required.", EINVAL, attr_name);
	goto send_reply;
enomem:
	cnt = Snprintf_error(&rm->line_buf, &rm->line_len,
			"%dOut of memory\n", ENOMEM);
	goto send_reply;
eexist:
	cnt = Snprintf_error(&rm->line_buf, &rm->line_len, "%dThe updtr %s already "
						"exists.", EEXIST, name);
	goto send_reply;
out:
	cnt = Snprintf(&rm->line_buf, &rm->line_len, "0");
send_reply:
	(void) send_request_reply(sock, rm, rm->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	return 0;
}

static int updtr_del_handler(int sock, req_msg_t rm)
{
	char *name;
	name = NULL;
	size_t cnt;
	ldmsd_req_attr_t attr;

	attr = (ldmsd_req_attr_t)rm->req_buf;
	while (attr->discrim) {
		switch (attr->attr_id) {
		case LDMSD_ATTR_NAME:
			name = (char *)attr->attr_value;
			break;
		default:
			break;
		}
		attr = (ldmsd_req_attr_t)&attr->attr_value[attr->attr_len];
	}
	if (!name)
		goto einval;

	int rc = ldmsd_updtr_del(name);
	if (rc == ENOENT) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len, "%dThe updater "
				"specified does not exist.", ENOENT);
	} else if (rc == EBUSY) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len, "%dThe updater "
				"is in use.", EBUSY);
	} else {
		cnt = Snprintf(&rm->line_buf, &rm->line_len, "0");
	}

	goto send_reply;

einval:
	cnt = Snprintf_error(&rm->line_buf, &rm->line_len, "%dThis attribute "
						"'name' is required.", EINVAL);
	goto send_reply;
send_reply:
	(void) send_request_reply(sock, rm, rm->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	return 0;
}

static int updtr_prdcr_add_handler(int sock, req_msg_t rm)
{
	char *updtr_name, *prdcr_regex, *attr_name;
	updtr_name = prdcr_regex = NULL;
	int rc;
	size_t cnt;
	ldmsd_req_attr_t attr;

	attr = (ldmsd_req_attr_t)rm->req_buf;
	while (attr->discrim) {
		switch (attr->attr_id) {
		case LDMSD_ATTR_NAME:
			updtr_name = (char *)attr->attr_value;
			break;
		case LDMSD_ATTR_REGEX:
			prdcr_regex = (char *)attr->attr_value;
			break;
		default:
			break;
		}
		attr = (ldmsd_req_attr_t)&attr->attr_value[attr->attr_len];
	}

	if (!updtr_name) {
		attr_name = "name";
		goto einval;
	}
	if (!prdcr_regex) {
		attr_name = "regex";
		goto einval;
	}

	rc = ldmsd_updtr_prdcr_add(updtr_name, prdcr_regex,
				rm->line_buf, rm->line_len);
	if (rc) {
		if (rc == ENOENT) {
			cnt = Snprintf_error(&rm->line_buf, &rm->line_len,
					"%dThe updater specified "
					"does not exist.", ENOENT);
		} else if (rc == EBUSY) {
			cnt = Snprintf_error(&rm->line_buf, &rm->line_len,""
				"%dConfiguration changes cannot be "
				"made while the updater is running.",
				EBUSY);
		} else if (rc == ENOMEM) {
			cnt = Snprintf_error(&rm->line_buf, &rm->line_len,
				"%dMemory allocation failure.",
				ENOMEM);
		} else {
			cnt = strlen(rm->line_buf);
		}
	} else {
		cnt = Snprintf(&rm->line_buf, &rm->line_len, "0");
	}
	goto send_reply;

einval:
	cnt = Snprintf_error(&rm->line_buf, &rm->line_len, "%dThis attribute '%s' "
					"is required.", attr_name, EINVAL);
send_reply:
	(void) send_request_reply(sock, rm, rm->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	return 0;
}

static int updtr_prdcr_del_handler(int sock, req_msg_t rm)
{
	char *updtr_name, *prdcr_regex, *attr_name;
	updtr_name = prdcr_regex = NULL;
	int rc;
	size_t cnt;
	ldmsd_req_attr_t attr;

	attr = (ldmsd_req_attr_t)rm->req_buf;
	while (attr->discrim) {
		switch (attr->attr_id) {
		case LDMSD_ATTR_NAME:
			updtr_name = (char *)attr->attr_value;
			break;
		case LDMSD_ATTR_REGEX:
			prdcr_regex = (char *)attr->attr_value;
			break;
		default:
			break;
		}
		attr = (ldmsd_req_attr_t)&attr->attr_value[attr->attr_len];
	}

	if (!updtr_name) {
		attr_name = "name";
		goto einval;
	}
	if (!prdcr_regex) {
		attr_name = "regex";
		goto einval;
	}

	rc = ldmsd_updtr_prdcr_del(updtr_name, prdcr_regex,
			rm->line_buf, rm->line_len);
	if (rc) {
		if (rc == ENOMEM) {
			cnt = Snprintf_error(&rm->line_buf, &rm->line_len,
					"%dThe updater specified does not "
					"exist.", ENOENT);
		} else if (rc == EBUSY) {
			cnt = Snprintf_error(&rm->line_buf, &rm->line_len,
					"%dConfiguration changes cannot be "
					"made while the updater is running,",
					EBUSY);
		} else if (rc == ENOENT) {
			cnt = Snprintf_error(&rm->line_buf, &rm->line_len,
					"%dThe updater specified does not "
					"exist.", ENOENT);
		} else {
			cnt = strlen(rm->line_buf);
		}
	} else {
		cnt = Snprintf(&rm->line_buf, &rm->line_len, "0");
	}

	goto send_reply;
einval:
	cnt = Snprintf_error(&rm->line_buf, &rm->line_len, "%dThis attribute '%s' "
					"is required.", attr_name, EINVAL);
send_reply:
	(void) send_request_reply(sock, rm, rm->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	return 0;
}

static int updtr_match_add_handler(int sock, req_msg_t rm)
{
	char *updtr_name, *regex_str, *match_str, *attr_name;
	updtr_name = regex_str = match_str = NULL;
	int rc;
	size_t cnt = 0;
	ldmsd_req_attr_t attr;

	attr = (ldmsd_req_attr_t)rm->req_buf;
	while (attr->discrim) {
		switch (attr->attr_id) {
		case LDMSD_ATTR_NAME:
			updtr_name = (char *)attr->attr_value;
			break;
		case LDMSD_ATTR_REGEX:
			regex_str = (char *)attr->attr_value;
			break;
		case LDMSD_ATTR_MATCH:
			match_str = (char *)attr->attr_value;
			break;
		default:
			break;
		}
		attr = (ldmsd_req_attr_t)&attr->attr_value[attr->attr_len];
	}

	if (!updtr_name) {
		attr_name = "name";
		goto einval;
	}
	if (!regex_str) {
		attr_name = "regex";
		goto einval;
	}

	rc = ldmsd_updtr_match_add(updtr_name, regex_str, match_str,
			rm->line_buf, rm->line_len);
	if (!rc) {
		cnt = Snprintf(&rm->line_buf, &rm->line_len, "0");
	} else if (rc == ENOENT) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len,
				"%dThe updater specified does not exist.", ENOENT);
	} else if (rc == EBUSY) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len,
				"%dConfiguration changes cannot be made "
				"while the updater is running\n", EBUSY);
	} else if (rc == ENOMEM) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len,
				"%dOut of memory.\n", ENOMEM);
	} else if (rc == EINVAL) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len,
				"%dThe value '%s' for match= is invalid.\n",
				EINVAL, match_str);
	}
	goto send_reply;
einval:
	cnt = Snprintf_error(&rm->line_buf, &rm->line_len, "%dThis attribute '%s' "
					"is required.", attr_name, EINVAL);
send_reply:
	(void) send_request_reply(sock, rm, rm->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	return 0;
}

static int updtr_match_del_handler(int sock, req_msg_t rm)
{
	char *updtr_name, *regex_str, *match_str, *attr_name;
	updtr_name = regex_str = match_str = NULL;
	size_t cnt;
	ldmsd_req_attr_t attr;

	attr = (ldmsd_req_attr_t)rm->req_buf;
	while (attr->discrim) {
		switch (attr->attr_id) {
		case LDMSD_ATTR_NAME:
			updtr_name = (char *)attr->attr_value;
			break;
		case LDMSD_ATTR_REGEX:
			regex_str = (char *)attr->attr_value;
			break;
		case LDMSD_ATTR_MATCH:
			match_str = (char *)attr->attr_value;
			break;
		default:
			break;
		}
		attr = (ldmsd_req_attr_t)&attr->attr_value[attr->attr_len];
	}

	if (!updtr_name) {
		attr_name = "name";
		goto einval;
	}
	if (!regex_str) {
		attr_name = "regex";
		goto einval;
	}

	int rc = ldmsd_updtr_match_del(updtr_name, regex_str, match_str);
	if (rc == ENOENT) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len,
			"%dThe updater specified does not exist.", ENOENT);
	} else if (rc == EBUSY) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len,
				"%dConfiguration changes cannot be made "
				"while the updater is running.", EBUSY);
	} else if (rc == -ENOENT) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len,
			"%dThe specified regex does not match any condition.",
			ENOENT);
	} else if (rc == EINVAL) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len,
			"%dUnrecognized match type '%s'", EINVAL, match_str);
	} else {
		cnt = Snprintf(&rm->line_buf, &rm->line_len, "0");
	}

	goto send_reply;
einval:
	cnt = Snprintf_error(&rm->line_buf, &rm->line_len, "%dThis attribute '%s' "
					"is required.", attr_name, EINVAL);
send_reply:
	(void) send_request_reply(sock, rm, rm->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	return 0;
}

static int updtr_start_handler(int sock, req_msg_t rm)
{
	char *updtr_name, *interval_str, *offset_str;
	updtr_name = interval_str = offset_str = NULL;
	int rc;
	size_t cnt;
	ldmsd_req_attr_t attr;

	attr = (ldmsd_req_attr_t)rm->req_buf;
	while (attr->discrim) {
		switch (attr->attr_id) {
		case LDMSD_ATTR_NAME:
			updtr_name = (char *)attr->attr_value;
			break;
		case LDMSD_ATTR_INTERVAL:
			interval_str = (char *)attr->attr_value;
			break;
		case LDMSD_ATTR_OFFSET:
			offset_str = (char *)attr->attr_value;
			break;
		default:
			break;
		}
		attr = (ldmsd_req_attr_t)&attr->attr_value[attr->attr_len];
	}

	if (!updtr_name) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len,
				"%dThe updater name must be specified.", EINVAL);
		goto send_reply;
	}

	rc = ldmsd_updtr_start(updtr_name, interval_str, offset_str);
	if (rc == ENOENT) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len, "%dThe updater "
				"specified does not exist.", ENOENT);
	} else if (rc == EBUSY) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len,
				"%dThe updater is already running.", EBUSY);
	} else {
		cnt = Snprintf(&rm->line_buf, &rm->line_len, "0");
	}

send_reply:
	(void) send_request_reply(sock, rm, rm->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	return 0;
}

static int updtr_stop_handler(int sock, req_msg_t rm)
{
	char *updtr_name;
	updtr_name = NULL;
	int rc;
	size_t cnt;
	ldmsd_req_attr_t attr;

	attr = (ldmsd_req_attr_t)rm->req_buf;
	while (attr->discrim) {
		switch (attr->attr_id) {
		case LDMSD_ATTR_NAME:
			updtr_name = (char *)attr->attr_value;
			break;
		default:
			break;
		}
		attr = (ldmsd_req_attr_t)&attr->attr_value[attr->attr_len];
	}

	if (!updtr_name) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len,
				"%dThe updater name must be specified.", EINVAL);
		goto send_reply;
	}

	rc = ldmsd_updtr_stop(updtr_name);
	if (rc == ENOENT) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len, "%dThe updater "
				"specified does not exist.", ENOENT);
	} else if (rc == EBUSY) {
		cnt = Snprintf_error(&rm->line_buf, &rm->line_len,
				"%dThe updater is already stopped.", EBUSY);
	} else {
		cnt = Snprintf(&rm->line_buf, &rm->line_len, "0");
	}

send_reply:
	(void) send_request_reply(sock, rm, rm->line_buf, cnt,
				LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	return 0;
}

static int updtr_status_handler(int sock, req_msg_t rm)
{
	ldmsd_updtr_t updtr;
	size_t cnt;
	int rc, count, prdcr_count;
	ldmsd_prdcr_ref_t ref;
	ldmsd_prdcr_t prdcr;
	const char *prdcr_state_str(enum ldmsd_prdcr_state state);

	ldmsd_cfg_lock(LDMSD_CFGOBJ_UPDTR);
	rc = send_request_reply(sock, rm, "[", 1, 0);
	count = 0;
	for (updtr = ldmsd_updtr_first(); updtr; updtr = ldmsd_updtr_next(updtr)) {
		if (count)
			rc = send_request_reply(sock, rm, ",\n", 2, 0);
		count++;
		cnt = Snprintf(&rm->line_buf, &rm->line_len,
			       "{\"name\":\"%s\","
			       "\"interval\":\"%d\","
			       "\"offset\":%d,"
			       "\"state\":\"%s\","
			       "\"producers\":[",
			       updtr->obj.name,
			       updtr->updt_intrvl_us,
			       updtr->updt_offset_us,
			       ldmsd_updtr_state_str(updtr->state));
		rc = send_request_reply(sock, rm, rm->line_buf, cnt, 0);
		prdcr_count = 0;
		for (ref = ldmsd_updtr_prdcr_first(updtr); ref;
		     ref = ldmsd_updtr_prdcr_next(ref)) {
			if (prdcr_count)
				rc = send_request_reply(sock, rm, ",\n", 2, 0);
			prdcr_count++;
			prdcr = ref->prdcr;
			cnt = Snprintf(&rm->line_buf, &rm->line_len,
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
			rc = send_request_reply(sock, rm, rm->line_buf, cnt, 0);
		}
		rc = send_request_reply(sock, rm, "]}", 2, 0);
	}
	rc = send_request_reply(sock, rm, "]", 1, LDMSD_REQ_EOM_F);
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_UPDTR);
	return rc;
}

struct plugin_list {
	struct ldmsd_plugin_cfg *lh_first;
};

static char *plugn_state_str(enum ldmsd_plugin_type type)
{
	static char *state_str[] = { "sampler", "store" };
	if (type <= LDMSD_PLUGIN_STORE)
		return state_str[type];
	return "unknown";
}

static int plugn_status_handler(int sock, req_msg_t rm)
{
	extern struct plugin_list plugin_list;
	struct ldmsd_plugin_cfg *p;
	size_t cnt;
	int rc, count;
	rc = send_request_reply(sock, rm, "[", 1, 0);
	count = 0;
	LIST_FOREACH(p, &plugin_list, entry) {
		if (count)
			rc = send_request_reply(sock, rm, ",\n", 2, 0);
		count++;
		cnt = Snprintf(&rm->line_buf, &rm->line_len,
			       "{\"name\":\"%s\",\"type\":\"%s\","
			       "\"sample_interval_us\":%d,"
			       "\"sample_offset_us\":%d,"
			       "\"libpath\":\"%s\"}",
			       p->plugin->name,
			       plugn_state_str(p->plugin->type),
			       p->sample_interval_us, p->sample_offset_us,
			       p->libpath);
		rc = send_request_reply(sock, rm, rm->line_buf, cnt, 0);
	}
	rc = send_request_reply(sock, rm, "]", 1, LDMSD_REQ_EOM_F);
	return rc;
}

static int unimplemented_handler(int sock, req_msg_t rm)
{
	size_t cnt;
	int rc;

	rc = send_request_reply(sock, rm, "[", 1, 0);
	cnt = Snprintf_error(&rm->line_buf, &rm->line_len,
			"The request is not implemented");
	rc = send_request_reply(sock, rm, rm->line_buf, cnt, 0);
	rc = send_request_reply(sock, rm, "]", 1, LDMSD_REQ_EOM_F);
	return rc;
}
