/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013 Sandia Corporation. All rights reserved.
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
/**
 * \file ocm_priv.h
 * \ingroup OCM
 * \brief Private OCM definitions.
 */
#ifndef __OCM_PRIV_H
#define __OCM_PRIV_H
#include "ocm.h"

#include <coll/str_map.h>
#include <coll/idx.h>
#include <pthread.h>
#include <event2/event.h>

#define __OCM_OFF(base, ptr) ((void*)ptr - (void*)base)
#define __OCM_PTR(base, off) ((void*)base + off)

/**
 * A registered callback structure.
 */
struct ocm_registered_cb {
	ocm_cb_fn_t cb; /**< Callback function */
	char *key; /**< Key */
	int is_called; /**< set to 1 if the cb has been called */
	LIST_ENTRY(ocm_registered_cb) entry; /**< List entry */
};

/**
 * OCM handle structure.
 */
struct ocm {
	zap_t zap;
	short port;
	zap_ep_t ep;
	/**
	 * This callback is the main callback function to handle OCM events such
	 * as OCM_EVENT_ERROR and OCM_EVENT_CFG_REQUESTED.
	 */
	ocm_cb_fn_t cb;

	/**
	 * This is a map of "key" to cb_fn used in configuration routing on the
	 * receiver side. The registered callback function (see
	 * ::ocm_register()) will be called upon the arrival of configuration
	 * with the matched key.
	 */
	str_map_t cb_map;
	LIST_HEAD(_cb_list_, ocm_registered_cb) cb_list;

	/**
	 * Index of active endpoints.
	 */
	idx_t active_idx;

	/**
	 * Mutex for OCM, used to lock cb_map and active_idx.
	 */
	pthread_mutex_t mutex;

	/**
	 * log function.
	 */
	void (*log_fn)(const char *fmt, ...);
};

struct ocm_msg_hdr {
	enum {
		OCM_MSG_UNKNOWN,
		OCM_MSG_REQ,
		OCM_MSG_CFG,
		OCM_MSG_ERR,
		OCM_MSG_LAST
	} type;
} __attribute__((packed));

struct ocm_err {
	struct ocm_msg_hdr hdr;
	int len;
	int code;
	char data[0]; /**< Format: |key|msg| */
};

/**
 * OCM client send the key to OCM server to request for its configuration.
 * The server side uses the key to lookup and respond the request.
 */
struct ocm_cfg_req {
	struct ocm_msg_hdr hdr;
	struct ocm_str key;
};

/**
 * OCM configuration command.
 */
struct ocm_cfg_cmd {
	/**
	 * Length of the entire command.
	 * In other words, sizeof(len) + data length.
	 */
	int len;
	/**
	 * Data part of the command.
	 * Data format:
	 * |verb|a1-v1|a2-v2|...|aN-vN|.
	 * \c verb is ::ocm_str
	 * \c aX is ::ocm_str
	 * \c vX is ::ocm_value.
	 *
	 * \c aI-vI can be accessed by calculating the length of \c verb and
	 * \c aJ-vJ for J < I, or just use ::ocm_av_iter to help tracking.
	 */
	char data[0];
};

/**
 * A configuration is a set of commands.
 */
struct ocm_cfg {
	/**
	 * Having message header so that ::ocm_cfg can be sent over the network.
	 */
	struct ocm_msg_hdr hdr;
	/**
	 * Length of an entire configuration.
	 */
	int len;
	/**
	 * Data part of configuration.
	 * This part contains consecutive commands for a configuration.
	 * format: |key|cmd1|cmd2|...|cmdN|.
	 * \c key is ::ocm_str
	 * \c cmdI is ::ocm_cfg_cmd
	 */
	char data[0];
};

/**
 * OCM zap endpoint context.
 */
struct ocm_ep_ctxt {
	ocm_t ocm; /**< OCM handle. */
	zap_ep_t ep; /**< The zap end point that owns the context */
	int is_active; /**< True if this is the active side. */
	struct sockaddr sa; /**< Socket address to connect to */
	socklen_t sa_len; /**< Length of the \c sa */
	struct event *reconn_event; /**< Reconnect event */
	pthread_mutex_t mutex; /**< mutex lock for the context */
};


#endif
