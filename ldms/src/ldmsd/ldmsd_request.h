/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2016-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2016-2018 Open Grid Computing, Inc. All rights reserved.
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

#include <inttypes.h>
#include <json/json_util.h>
#include "coll/rbt.h"
#include "ldms.h"
#include "ldmsd.h"

#ifndef LDMS_SRC_LDMSD_LDMSD_REQUEST_H_
#define LDMS_SRC_LDMSD_LDMSD_REQUEST_H_

extern int ldmsd_req_debug;

#define LDMSD_REC_SOM_F	1 /* start of message */
#define LDMSD_REC_EOM_F	2 /* end of message */

#define LDMSD_MSG_TYPE_REQ  1
#define LDMSD_MSG_TYPE_RESP 2
#define LDMSD_MSG_TYPE_STREAM 3
/* NOTIFY does not need response */
#define LDMSD_MSG_TYPE_NOTIFY 4

/**
 * LDMSD messages are either requests or response. Each message may consist
 * of multiple records.
 *
 * The format of records is as follows:
 *
 *                 Standard Record Format
 * +---------------------------------------------------------+
 * | ldmsd_rec_hdr_s                                         |
 * +---------------------------------------------------------+
 * | payload                                                 |
 * S                                                         S
 * +---------------------------------------------------------+
 *
 *                Configuration Record Format
 * +---------------------------------------------------------+
 * | ldmsd_rec_hdr_s                                         |
 * +---------------------------------------------------------+
 * | a substring of a JSON-formatted string.                 |
 * S                                                         S
 * +---------------------------------------------------------+
 *
 *                Stream Record Format
 * +---------------------------------------------------------+
 * | ldmsd_rec_hdr_s                                         |
 * +---------------------------------------------------------+
 * | ldmsd_stream_hdr                                        |
 * +---------------------------------------------------------+
 * | stream payload                                          |
 * S                                                         S
 * +---------------------------------------------------------+
 *
 * All request handlers must respond back to the requester.
 */

#pragma pack(push, 1)
typedef struct ldmsd_msg_key {
	uint32_t msg_no;
	uint64_t conn_id;
} *ldmsd_msg_key_t;

typedef struct ldmsd_rec_hdr_s {
	uint32_t type;			/* Request type LDMSD_MSG_TYPE_REQ, LDMSD_MSG_TYPE_RESP */
	uint32_t flags;			/* EOM==1 means this is the last record for this message */
	uint32_t msg_no;		/* Unique message number of the peer */
	uint32_t rec_len;		/* Record length in bytes including this header */
} *ldmsd_rec_hdr_t;
#pragma pack(pop)

struct ldmsd_req_ctxt;

typedef struct ldmsd_cfg_ldms_s {
	ldms_t ldms;
} *ldmsd_cfg_ldms_t;

typedef struct ldmsd_cfg_file_s {
	ldmsd_plugin_inst_t t;
} *ldmsd_cfg_file_t;

typedef struct ldmsd_cfg_cli_s {
	uint64_t errcode; /* Set if there is a configuration error */
} *ldmsd_cfg_cli_t;

struct ldmsd_cfg_xprt_s;
typedef int (*ldmsd_cfg_send_fn_t)(struct ldmsd_cfg_xprt_s *xprt, char *data, size_t data_len);
typedef void (*ldmsd_cfg_cleanup_fn_t)(struct ldmsd_cfg_xprt_s *xprt);
typedef struct ldmsd_cfg_xprt_s {
	union {
		void *xprt;
		struct ldmsd_cfg_file_s file;
		struct ldmsd_cfg_ldms_s ldms;
		struct ldmsd_cfg_cli_s cli;
	};
	enum ldmsd_cfg_xprt_type {
		LDMSD_CFG_XPRT_CONFIG_FILE = 1,
		LDMSD_CFG_XPRT_LDMS,
		LDMSD_CFG_XPRT_CLI,
	} type;
	struct ref_s ref;
	size_t max_msg;
	ldmsd_cfg_send_fn_t send_fn;
	int trust; /* trust (to perform command expansion) */
} *ldmsd_cfg_xprt_t;

#define ldmsd_cfg_xprt_ref_get(_s_, _n_) _ref_get(&((_s_)->ref), (_n_), __func__, __LINE__)
#define ldmsd_cfg_xprt_ref_put(_s_, _n_) _ref_put(&((_s_)->ref), (_n_), __func__, __LINE__)

#define LDMSD_REQ_DEFER_FLAG 0x10

typedef struct ldmsd_req_buf {
	int num_rec; /* Number of records that have consumed this buffer */

	size_t len;
	size_t off;
	char *buf;
} *ldmsd_req_buf_t;

#define LDMSD_REQ_CTXT_REQ 1  /* Context to track an outstanding request from a peer */
#define LDMSD_REQ_CTXT_RSP 2  /* Created by this LDMSD when sending a request to a peer and waiting for a response */

typedef struct ldmsd_req_ctxt *ldmsd_req_ctxt_t;
typedef int (* ldmsd_req_resp_fn)(ldmsd_req_ctxt_t reqc, void *resp_args);
typedef struct ldmsd_req_ctxt {
	int type;
	/*
	 * A REQUEST message sent by a peer (LDMSD_REQ_CTXT_REQ):
	 * The msg_no in both remote and local keys are the same and
	 * it was sent by the peer with the record header.
	 * The conn_id in the remote key was sent by the peer with the record header, but
	 * the conn_id in the local key is the pointer of the ldms xprt endpoint
	 * connecting to the peer. The conn_ids sent by the two different peers
	 * may be the same, so they cannot use in the message tree keys. Thus,
	 * the peer ldms xprt pointers are used instead.
	 *
	 * A REQUEST message created and sent by this LDMSD (LDMSD_REQ_CTXT_RSP):
	 * The remote and local keys are the same. The msg_no is the message number
	 * of this LDMSD and the conn_id is the pointer of the ldms xprt endpoint
	 * connecting to the peer.
	 */
	/*
	 * The key used in the message trees
	 */
	struct ldmsd_msg_key key;
	struct rbn rbn;
	struct ref_s ref;

	ldmsd_cfg_xprt_t xprt;	/* network transport */

	json_entity_t json;

	/* Buffer to aggregate a JSON string received from single or multiple record(s) */
	ldmsd_req_buf_t recv_buf;

	/* Buffer to construct a request record to be sent */
	ldmsd_req_buf_t send_buf;

	/*
	 * In case the request needs a specific response,
	 * this is the handle of the response handler.
	 *
	 * The resp_handler is responsible for putting back the 'create' reference
	 * by calling
	 *
	 * ldmsd_req_ctxt_ref_put(reqc, "create");
	 *
	 */
	ldmsd_req_resp_fn resp_handler;
	/*
	 * The caller context to be passed to resp_handler
	 */
	void *resp_args;
} *ldmsd_req_ctxt_t;

/**
 * Convert the request header byte order from host to network
 */
void ldmsd_hton_rec_hdr(ldmsd_rec_hdr_t req);
/**
 * Convert the request header byte order from network to host
 */
void ldmsd_ntoh_rec_hdr(ldmsd_rec_hdr_t req);

/**
 * Create a new \c ldmsd_req_buf with the size \c len.
 */
ldmsd_req_buf_t ldmsd_req_buf_alloc(size_t len);
/**
 * Re-allocate the memory of an existing \c ldmsd_req_buf \c buf.
 * The existing content of buf is preserved, similar to \c realloc.
 */
ldmsd_req_buf_t ldmsd_req_buf_realloc(ldmsd_req_buf_t buf, size_t new_len);
/**
 * Reset the buffer as if it is just created.
 */
void ldmsd_req_buf_reset(ldmsd_req_buf_t buf);
/**
 * Free the memory of an existing \c ldmsd_req_buf \c buf
 */
void ldmsd_req_buf_free(ldmsd_req_buf_t buf);

__attribute__((format(printf, 2, 3)))
size_t ldmsd_req_buf_append(ldmsd_req_buf_t buf, const char *fmt, ...);

/**
 * \brief Advise the peer our configuration record length
 *
 * Send configuration record length advice
 *
 * \param xprt The configuration transport handle
 * \param msg_no The message number
 * \param rec_len The record length
 */
int ldmsd_send_err_rec_adv(ldmsd_cfg_xprt_t xprt, uint32_t msg_no, uint32_t rec_len);
typedef int (*ldmsd_req_filter_fn)(ldmsd_req_ctxt_t reqc, void *ctxt);
ldmsd_req_ctxt_t ldmsd_handle_record(ldmsd_rec_hdr_t rec, ldmsd_cfg_xprt_t xprt);
int ldmsd_process_msg_request(ldmsd_req_ctxt_t reqc);
int ldmsd_process_msg_response(ldmsd_req_ctxt_t reqc);
int ldmsd_process_msg_stream(ldmsd_req_ctxt_t reqc);
int ldmsd_process_msg_notify(ldmsd_req_ctxt_t reqc);

int ldmsd_error_send(ldmsd_cfg_xprt_t xprt, uint32_t msg_no,
			ldmsd_req_buf_t _buf, uint32_t errcode,
			char *errmsg_fmt, ...);

/**
 * \brief Create a new cfg_xprt object
 */
ldmsd_cfg_xprt_t ldmsd_cfg_xprt_new();

/**
 * \brief Create a new cfg_xprt with an ldms transport.
 */
ldmsd_cfg_xprt_t ldmsd_cfg_xprt_ldms_new(ldms_t x);

/*
 * \brief Initialize a cfg_xprt to be an LDMS.
 *
 * If the cfg_xprt is returned from \c ldmsd_cfg_xprt_ldms_new,
 * this function must not be called on the cfg_xprt.
 */
void ldmsd_cfg_xprt_ldms_init(ldmsd_cfg_xprt_t xprt, ldms_t ldms);
void ldmsd_cfg_xprt_cli_init(ldmsd_cfg_xprt_t xprt);
void ldmsd_cfg_xprt_cfgfile_init(ldmsd_cfg_xprt_t xprt, ldmsd_plugin_inst_t t);

#define ldmsd_req_ctxt_ref_get(_s_, _n_) _ref_get(&((_s_)->ref), (_n_), __func__, __LINE__)
#define ldmsd_req_ctxt_ref_put(_s_, _n_) _ref_put(&((_s_)->ref), (_n_), __func__, __LINE__)

void ldmsd_req_ctxt_tree_lock(int type);
void ldmsd_req_ctxt_tree_unlock(int type);

/**
 * \brief Get a new message key
 *
 * When this function is called, the global message number of
 * this ldmsd will be incremented by one.
 */
void ldmsd_msg_key_get(void *xprt, struct ldmsd_msg_key *key_);

/**
 * \brief Create a request context.
 *
 * The context is inserted into the response message tree.
 *
 * \param key  The message key
 * \param xprt The configuration transport endpoint
 */
ldmsd_req_ctxt_t
ldmsd_req_ctxt_alloc(struct ldmsd_msg_key *key, ldmsd_cfg_xprt_t xprt);

ldmsd_req_ctxt_t ldmsd_req_ctxt_first(int type);
ldmsd_req_ctxt_t ldmsd_req_ctxt_next(ldmsd_req_ctxt_t reqc);

/**
 * \brief Get a new message key
 */
void ldmsd_msg_key_get(void *xprt, struct ldmsd_msg_key *key_);

/*
 * \brief Send a request containing the JSON object \c req_obj
 *
 * An ldmsd_req_ctxt is created by the function. The \c resp_fn must
 * call ldmsd_req_ctxt_ref_put(reqc, "create") if the request context
 * won't be used again.
 */
int ldmsd_request_send(ldms_t ldms, json_entity_t req_obj,
			ldmsd_req_resp_fn resp_fn, void *resp_args);

typedef int (*ldmsd_msg_send_fn_t)(void *xprt, char *data, size_t data_len);
int ldmsd_append_msg_buffer(void *xprt, size_t max_msg, struct ldmsd_msg_key *key,
			ldmsd_msg_send_fn_t send_fn,
			ldmsd_req_buf_t buf,
			int msg_flags, int msg_type,
			const char *data, size_t data_len);
int ldmsd_append_msg_buffer_va(void *xprt, size_t max_msg, struct ldmsd_msg_key *key,
			ldmsd_msg_send_fn_t send_fn,
			ldmsd_req_buf_t buf,
			int msg_flags, int msg_type, const char *fmt, ...);

/**
 * \brief Append a string to the response to be sent to the peer.
 *
 * The string is constructed as in \c printf family with \c fmt and
 * the remaining parameters.
 *
 */
int ldmsd_append_response_va(ldmsd_req_ctxt_t reqc, int msg_flags,
						const char *fmt, ...);

/**
 * \brief Append data to be sent as a response to a peer
 *
 */
int ldmsd_append_response(ldmsd_req_ctxt_t reqc, int msg_flags,
				const char *data, size_t data_len);

/**
 * \brief Append a string to the request to be sent to the peer.
 *
 * The string is constructed as in \c printf family with \c fmt and
 * the remaining parameters.
 */
int ldmsd_append_request_va(ldmsd_req_ctxt_t reqc, int msg_flags,
						const char *fmt, ...);

/**
 * \brief Append data to be sent as a request to a peer.
 */
int ldmsd_append_request(ldmsd_req_ctxt_t reqc, int msg_flags,
				const char *data, size_t data_len);

/**
 * \brief Append the info_obj JSON header to the send_buf
 *
 * \c ldmsd_append_response or \c ldmsd_append_response_va can be called
 * subsequently to append all the information and send to the peer.
 *
 * \return 0 on success. Otherwise, an error code the same as the returned code
 * from \c ldmsd_append_response or \c ldmsd_append_response_va.
 *
 */
int ldmsd_append_info_obj_hdr(ldmsd_req_ctxt_t reqc, const char *info_name);

/**
 * \brief Append the cmd_obj JSON header to the send_buf as a REQUEST message.
 */
int ldmsd_append_cmd_obj_hdr(ldmsd_req_ctxt_t reqc, const char *cmd_name);

enum ldmsd_cfgobj_request_type {
	LDMSD_CFGOBJ_REQ_TYPE_CREATE = 1,
	LDMSD_CFGOBJ_REQ_TYPE_UPDATE,
	LDMSD_CFGOBJ_REQ_TYPE_DELETE,
	LDMSD_CFGOBJ_REQ_TYPE_QUERY,
	LDMSD_CFGOBJ_REQ_TYPE_EXPORT,
};

const char *ldmsd_cfgobj_req_type2str(enum ldmsd_cfgobj_request_type type);

/**
 * \brief Create a request JSON object with the mandatory attributes.
 *
 * \return A JSON dictionary { "request": \c request, "id": 0 }. NULL if ENOMEM.
 */
json_entity_t ldmsd_req_obj_new(const char *request);

/**
 * \brief Create the JSON object of a cfgobj update request.
 *
 * One of the other of \c key or \c re must be specified. If \c key is not NULL,
 * the is matched to a dictionary in \c value list. If \c re is not NULL, it uses
 * the attributes present in \c dft.
 *
 * \param id		Request ID
 * \param cfgobj_type	LDMSD cfgobj type, e.g., prdcr, auth
 * \param enabled	1 if enabled, 0 if disabled, -1 if no changes
 * \param dft		A JSON dictionary entity specifies the new default values
 * 			for object attributes or NULL if no changes
 * \param re		A JSON list entity of regular expressions matched
 * 			cfgobj names or NULL if no changes
 * \param spec		A JSON dictionary entity. The attribute names are
 * 			configuration object names. The attribute values are
 * 			the dictionaries of specific attribute values for each
 * 			configuration objects.
 */
json_entity_t
ldmsd_cfgobj_update_req_obj_new(int id, const char *cfgobj_type,
			short enabled, json_entity_t dft,
			json_entity_t re, json_entity_t spec);

/*
 * \brief Create the JSON object of a cfgobj create request.
 *
 * \param id		Request ID
 * \param cfgobj_type	LDMSD cfgobj type, e.g., prdcr, auth
 * \param enabled	1 if enabled, 0 if disabled, -1 if no changes
 * \param dft		A JSON dictionary entity specifies the new default values
 * 			for object attributes or NULL if no changes
  * \param spec		A JSON dictionary entity. The attribute names are
 * 			configuration object names. The attribute values are
 * 			the dictionaries of specific attribute values for each
 * 			configuration objects.
 */
json_entity_t
ldmsd_cfgobj_create_req_obj_new(int id, const char *cfgobj_type,
				short enabled, json_entity_t dft,
				json_entity_t spec);

#endif /* LDMS_SRC_LDMSD_LDMSD_REQUEST_H_ */
