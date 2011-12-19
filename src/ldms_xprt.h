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
#ifndef __LDMS_XPRT_H__
#define __LDMS_XPRT_H__

#include <semaphore.h>
#include <sys/queue.h>

#include "ldms_config.h"

#ifndef HAVE_SPINLOCK_T
#  define pthread_spinlock_t pthread_mutex_t
#  define pthread_spin_lock pthread_mutex_lock
#  define pthread_spin_unlock pthread_mutex_unlock
#endif // ldms_HAVE_SPINLOCK_T

enum ldms_rbuf_type {
	LDMS_RBUF_LOCAL,	/* This is a buffer being served to remote peers */
	LDMS_RBUF_REMOTE,	/* This buffer is a data sink for a remote buffer */
};

#pragma pack(4)
struct ldms_rbuf_desc {
	struct ldms_xprt *xprt;
	struct ldms_set *set;
	enum ldms_rbuf_type type;
	uint32_t flags;
	uint64_t xid;
	LIST_ENTRY(ldms_rbuf_desc) set_link; /* list of RBD for a set */
	LIST_ENTRY(ldms_rbuf_desc) xprt_link; /* list of RBD for a transport */
	uint32_t xprt_data_len;	/* The length of the transport private data in bytes */
	void *xprt_data;	/* The transport private data section */
	void *lcl_data;		/* Pointer to the local buffer. */
};

enum ldms_request_cmd {
	LDMS_CMD_DIR = 0,
	LDMS_CMD_DIR_CANCEL,
	LDMS_CMD_LOOKUP,
	LDMS_CMD_UPDATE,
	LDMS_CMD_REPLY,
	LDMS_CMD_DIR_REPLY,
	LDMS_CMD_LOOKUP_REPLY,
	/* Transport private requests set bit 32 */
	LDMS_CMD_XPRT_PRIVATE = 0x80000000,
};

struct ldms_hello_cmd_param {
	uint32_t msg_len;
	char msg[0];
};

struct ldms_lookup_cmd_param {
	uint32_t path_len;
	char path[LDMS_LOOKUP_PATH_MAX+1];
};

struct ldms_update_cmd_param {
	uint64_t set_id;
};

struct ldms_dir_cmd_param {
	uint32_t flags;
};

struct ldms_request_hdr {
	uint64_t xid;
	uint32_t cmd;
	uint32_t len;
};
struct ldms_request {
	struct ldms_request_hdr hdr;
	union {
		struct ldms_dir_cmd_param dir;
		struct ldms_lookup_cmd_param lookup;
		struct ldms_update_cmd_param update;
	};
};

struct ldms_lookup_reply {
	uint32_t meta_len;
	uint32_t data_len;
	uint32_t xprt_data_len;
	char xprt_data[0];
};

struct ldms_dir_reply {
	uint32_t set_count;
	uint32_t set_list_len;
	char set_list[0];
};

struct ldms_reply_hdr {
	uint64_t xid;
	uint32_t cmd;
	uint32_t len;
	uint32_t rc;
};
struct ldms_reply {
	struct ldms_reply_hdr hdr;
	union {
		struct ldms_lookup_reply lookup;
		struct ldms_dir_reply dir;
	};
};
#pragma pack()

struct ldms_context {
	sem_t sem;
	int rc;
	union {
		struct {
			int set_count;
			char *set_list;
			size_t set_list_len;
			uint32_t flags;
			ldms_dir_cb_t cb;
			void *cb_arg;
		} dir;
		struct {
			char *path;
			ldms_lookup_cb_t cb;
			void *cb_arg;
			struct ldms_set *set;
		} lookup;
		struct {
			ldms_set_t s;
			ldms_update_cb_t cb;
			void *arg;
		} update;
	};
};

#define LDMS_MAX_TRANSPORT_NAME_LEN 16

struct ldms_xprt {
	char name[LDMS_MAX_TRANSPORT_NAME_LEN];
	int ref_count;
	struct sockaddr_storage ss;
	socklen_t ss_len;
	pthread_spinlock_t lock;
	int connected;
	int closed;
	uint64_t local_dir_xid;
	uint64_t remote_dir_xid;
	int io_wait;
	struct ldms_context io_ctxt;

	LIST_HEAD(xprt_rbd_list, ldms_rbuf_desc) rbd_list;
	LIST_ENTRY(ldms_xprt) xprt_link;

	/** Request a connection with a server */
	int (*connect)(struct ldms_xprt *, struct sockaddr *sa, socklen_t sa_len);
	/** Listen for incoming connection requests */
	int (*listen)(struct ldms_xprt *, struct sockaddr *sa, socklen_t sa_len);
	/** Close the connection */
	void (*close)(struct ldms_xprt *);
	/** Destroy the transport instance */
	void (*destroy)(struct ldms_xprt *);
	/** Send a request/reply */
	int (*send)(struct ldms_xprt *, void *, size_t);
	/** Read remote data buffer */
	int (*read_data_start)(struct ldms_xprt *, ldms_set_t, size_t, void *);
	/** Read remote metadata buffer */
	int (*read_meta_start)(struct ldms_xprt *, ldms_set_t, size_t, void *);

	/** User callback routine invoked when the read completes. */
	int (*read_complete_cb)(struct ldms_xprt *, void *);
	/** User callback routine called when data arrives on the transport. */
	int (*recv_cb)(struct ldms_xprt *, void *);
	/** User callback invoked when ldms_dir completes */
	ldms_dir_cb_t *dir_cb;
	void *dir_cb_arg;

	/** Allocate a remote buffer */
	struct ldms_rbuf_desc *(*alloc)(struct ldms_xprt *,
					struct ldms_set *s,
					enum ldms_rbuf_type type,
					void *xprt_data, size_t xprt_data_len);
	/** Free a remote buffer */
	void (*free)(struct ldms_xprt *, struct ldms_rbuf_desc *);

	/** Pointer to the transport's private data */
	void *private;
};
typedef struct ldms_xprt *(*ldms_xprt_get_t)
	(
	 int (*recv_cb)(struct ldms_xprt *, void *),
	 int (*read_complete_cb)(struct ldms_xprt *, void *)
	 );

#define ldms_ptr_(_t, _p, _o) (_t *)&((char *)_p)[_o]

extern struct ldms_rbuf_desc *ldms_alloc_rbd(struct ldms_xprt *,
					     struct ldms_set *s,
					     enum ldms_rbuf_type type,
					     void *xprt_data, size_t xprt_data_len);

extern void ldms_free_rbd(struct ldms_rbuf_desc *);

extern struct ldms_rbuf_desc *ldms_lookup_rbd(struct ldms_xprt *, struct ldms_set *);

extern struct ldms_set *ldms_find_local_set(const char *path);

#endif
