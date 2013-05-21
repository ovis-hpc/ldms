/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2012 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2012 Sandia Corporation. All rights reserved.
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
 *      Neither the name of the Network Appliance, Inc. nor the names of
 *      its contributors may be used to endorse or promote products
 *      derived from this software without specific prior written
 *      permission.
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
 *
 * Author: Tom Tucker <tom@opengridcomputing.com>
 */
#ifndef __LDMS_XPRT_UGNI_H__
#define __LDMS_XPRT_UGNI_H__
#include <semaphore.h>
#include <sys/queue.h>
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>
#include <event2/thread.h>

/*
 * This structure is provided to the client in lookup and returned by
 * the client in the update request.
 */
#pragma pack(4)

struct ugni_buf_remote_data {
	uint64_t meta_buf;
	gni_mem_handle_t meta_mh;
	uint32_t meta_size;
	uint64_t data_buf;
	gni_mem_handle_t data_mh;
	uint32_t data_size;
};

struct ugni_buf_local_data {
	void *meta;
	size_t meta_size;
	gni_mem_handle_t meta_mh;
	void *data;
	size_t data_size;
	gni_mem_handle_t data_mh;
};

struct ugni_desc {
	gni_post_descriptor_t post;
	struct ldms_ugni_xprt *gxp;
	void *context;
	LIST_ENTRY(ugni_desc) link;
};

struct ugni_mh {
	unsigned long start;
	unsigned long end;
	gni_mem_handle_t mh;
	int ref_count;
	LIST_ENTRY(ugni_mh) link;
};

#define UGNI_CTRL_REQ_CMD	(LDMS_CMD_XPRT_PRIVATE | 0x1)
#define UGNI_HELLO_REQ_CMD	(LDMS_CMD_XPRT_PRIVATE | 0x2)
#define UGNI_HELLO_RPL_CMD	(LDMS_CMD_XPRT_PRIVATE | 0x3)
struct ugni_hello_req {
	struct ldms_request_hdr hdr;
	uint32_t pe_addr;
	uint32_t inst_id;
};
struct ugni_hello_rpl {
	struct ldms_request_hdr hdr;
	uint32_t pe_addr;
	uint32_t inst_id;
};
#pragma pack()

enum ugni_conn_status {
	CONN_ERROR = -1,
	CONN_IDLE = 0,
	CONN_CONNECTING,
	CONN_CONNECTED,
	CONN_CLOSING,
	CONN_CLOSED
};

/**
 * uGNI Transport private data
 */
typedef struct {
	uint8_t  ptag;
	uint32_t cookie;
	uint32_t pe_addr;
	uint32_t inst_id;
} gni_dom_info_t;

typedef struct {
        gni_dom_info_t   info;
        gni_cdm_handle_t cdm;
        gni_nic_handle_t nic;
        gni_cq_handle_t  src_cq;
} gni_dom_t;

struct ldms_ugni_xprt {
	struct ldms_xprt *xprt;
	enum ugni_conn_status conn_status;

	int sock;
	gni_dom_t dom;
	gni_ep_handle_t ugni_ep;
	uint32_t rem_pe_addr;
	uint32_t rem_inst_id;
	struct bufferevent *buf_event;
	struct evconnlistener *listen_ev;

	LIST_ENTRY(ldms_ugni_xprt) client_link;
};

extern int ugni_register(struct sockaddr *s, struct ldms_ugni_xprt *x);

#endif
