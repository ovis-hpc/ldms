/*
 * Copyright (c) 2010 Open Grid Computing, Inc. All rights reserved.
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
#ifndef __KLDMS_REQ_H
#define __KLDMS_REQ_H

enum kldms_req_id {
	KLDMS_REQ_HELLO = 1,
	KLDMS_REQ_PUBLISH_SET,
	KLDMS_REQ_UNPUBLISH_SET,
	KLDMS_REQ_UPDATE_SET,
};
struct kldms_req_hdr {
	enum kldms_req_id	req_id;
};

#define KLDMS_HELLO_MSG_LEN 8
struct kldms_req_hello {
	struct kldms_req_hdr	hdr;
	char			msg[KLDMS_HELLO_MSG_LEN];
};

struct kldms_req_publish_set {
	struct kldms_req_hdr	hdr;
	int			set_id;
	size_t			data_len;
};

struct kldms_req_unpublish_set {
	struct kldms_req_hdr	hdr;
	int			set_id;
};

struct kldms_req_update_set {
	struct kldms_req_hdr	hdr;
	int			set_id;
};

union kldms_req {
	struct kldms_req_hdr		hdr;
	struct kldms_req_hello		hello;
	struct kldms_req_publish_set	publish;
	struct kldms_req_unpublish_set	unpublish;
	struct kldms_req_update_set	update;
};

#endif
