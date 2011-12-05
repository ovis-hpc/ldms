/*
 * Copyright (c) 2010 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2010 Sandia Corporation. All rights reserved.
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
#include <sys/errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include "ldms.h"
#include "ldms_xprt.h"

static void local_xprt_cleanup(void)
{
}

static int local_xprt_connect(struct ldms_xprt *x, struct sockaddr *sa, socklen_t sa_len)
{
	return 0;
}

static int local_xprt_listen(struct ldms_xprt *x, struct sockaddr *sa, socklen_t sa_len)
{
	return ENOSYS;
}

static void sock_xprt_destroy(struct ldms_xprt *x)
{
	free(x);
}

static int sock_xprt_send(struct ldms_xprt *x, void *buf, size_t len)
{
	x->recv_cb(x, buf);
	return 0;
}

struct ldms_rbuf_desc *local_rbuf_alloc(struct ldms_xprt *x,
				       struct ldms_set *set,
				       enum ldms_rbuf_type type,
				       void *xprt_data,
				       size_t xprt_data_len)
{
	return NULL;
}

void local_rbuf_free(struct ldms_xprt *x, struct ldms_rbuf_desc *desc)
{
}

static int sock_read_meta_start(struct ldms_xprt *x, ldms_set_t s, size_t len, void *context)
{
	return 0;
}

static int sock_read_data_start(struct ldms_xprt *x, ldms_set_t s, size_t len, void *context)
{
	return 0;
}

struct ldms_xprt local_transport = {
	.name = "local",
	.init = local_xprt_init,
};

struct ldms_xprt *xprt_get(int (*cb)(struct ldms_xprt *, void *)) {
	struct ldms_xprt *x;
	x = malloc(sizeof (*x));
	if (!x) {
		errno = ENOMEM;
		return NULL;
	}
	x->connect = local_xprt_connect;
	x->listen = local_xprt_listen;
	x->destroy = local_xprt_destroy;
	x->send = local_xprt_send;
	x->read_meta_start = local_read_meta_start;
	x->read_data_start = local_read_data_start;
	x->read_complete_cb = cb;
	x->alloc = local_rbuf_alloc;
	x->free = local_rbuf_free;
	x->private = NULL;
	return x;
}
