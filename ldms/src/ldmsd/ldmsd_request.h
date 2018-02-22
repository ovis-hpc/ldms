/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2016-2017 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2016-2017 Sandia Corporation. All rights reserved.
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

#include <inttypes.h>
#include "coll/rbt.h"
#include "ldms.h"

#ifndef LDMS_SRC_LDMSD_LDMSD_REQUEST_H_
#define LDMS_SRC_LDMSD_LDMSD_REQUEST_H_

enum ldmsd_request {
	LDMSD_EXAMPLE_REQ = 0x1,
	LDMSD_GREETING_REQ = 0x2,
	LDMSD_PRDCR_ADD_REQ = 0x100,
	LDMSD_PRDCR_DEL_REQ,
	LDMSD_PRDCR_START_REQ,
	LDMSD_PRDCR_STOP_REQ,
	LDMSD_PRDCR_STATUS_REQ,
	LDMSD_PRDCR_START_REGEX_REQ,
	LDMSD_PRDCR_STOP_REGEX_REQ,
	LDMSD_PRDCR_SET_REQ,
	LDMSD_STRGP_ADD_REQ = 0x200,
	LDMSD_STRGP_DEL_REQ,
	LDMSD_STRGP_START_REQ,
	LDMSD_STRGP_STOP_REQ,
	LDMSD_STRGP_STATUS_REQ,
	LDMSD_STRGP_PRDCR_ADD_REQ,
	LDMSD_STRGP_PRDCR_DEL_REQ,
	LDMSD_STRGP_METRIC_ADD_REQ,
	LDMSD_STRGP_METRIC_DEL_REQ,
	LDMSD_UPDTR_ADD_REQ = 0x300,
	LDMSD_UPDTR_DEL_REQ,
	LDMSD_UPDTR_START_REQ,
	LDMSD_UPDTR_STOP_REQ,
	LDMSD_UPDTR_STATUS_REQ,
	LDMSD_UPDTR_PRDCR_ADD_REQ,
	LDMSD_UPDTR_PRDCR_DEL_REQ,
	LDMSD_UPDTR_MATCH_ADD_REQ,
	LDMSD_UPDTR_MATCH_DEL_REQ,
	LDMSD_SMPLR_ADD_REQ = 0X400,
	LDMSD_SMPLR_DEL_REQ,
	LDMSD_SMPLR_START_REQ,
	LDMSD_SMPLR_STOP_REQ,
	LDMSD_PLUGN_ADD_REQ = 0x500,
	LDMSD_PLUGN_DEL_REQ,
	LDMSD_PLUGN_START_REQ,
	LDMSD_PLUGN_STOP_REQ,
	LDMSD_PLUGN_STATUS_REQ,
	LDMSD_PLUGN_LOAD_REQ,
	LDMSD_PLUGN_TERM_REQ,
	LDMSD_PLUGN_CONFIG_REQ,
	LDMSD_PLUGN_LIST_REQ,
	LDMSD_SET_UDATA_REQ = 0x600,
	LDMSD_SET_UDATA_REGEX_REQ,
	LDMSD_VERBOSE_REQ,
	LDMSD_DAEMON_STATUS_REQ,
	LDMSD_VERSION_REQ,
	LDMSD_ENV_REQ,
	LDMSD_INCLUDE_REQ,
	LDMSD_ONESHOT_REQ,
	LDMSD_LOGROTATE_REQ,
	LDMSD_EXIT_DAEMON_REQ,
	LDMSD_RECORD_LEN_ADVICE_REQ,
	LDMSD_NOTSUPPORT_REQ,
};

enum ldmsd_request_attr {
	/* Common attribute */
	LDMSD_ATTR_NAME = 1,
	LDMSD_ATTR_INTERVAL,
	LDMSD_ATTR_OFFSET,
	LDMSD_ATTR_REGEX,
	LDMSD_ATTR_TYPE,
	LDMSD_ATTR_PRODUCER,
	LDMSD_ATTR_INSTANCE,
	LDMSD_ATTR_XPRT,
	LDMSD_ATTR_HOST,
	LDMSD_ATTR_PORT,
	LDMSD_ATTR_MATCH,
	LDMSD_ATTR_PLUGIN,
	LDMSD_ATTR_CONTAINER,
	LDMSD_ATTR_SCHEMA,
	LDMSD_ATTR_METRIC,
	LDMSD_ATTR_STRING,
	LDMSD_ATTR_UDATA,
	LDMSD_ATTR_BASE,
	LDMSD_ATTR_INCREMENT,
	LDMSD_ATTR_LEVEL,
	LDMSD_ATTR_PATH,
	LDMSD_ATTR_TIME,
	LDMSD_ATTR_PUSH,
	LDMSD_ATTR_TEST,
	LDMSD_ATTR_REC_LEN,
	LDMSD_ATTR_JSON,
	LDMSD_ATTR_PERM,
	LDMSD_ATTR_LAST,
};

#define LDMSD_RECORD_MARKER 0xffffffff

#define LDMSD_REQ_SOM_F	1
#define LDMSD_REQ_EOM_F	2

#define LDMSD_REQ_TYPE_CONFIG_CMD 1
#define LDMSD_REQ_TYPE_CONFIG_RESP 2

#pragma pack(push, 1)
typedef struct ldmsd_req_hdr_s {
	uint32_t marker;	/* Always has the value 0xff */
	uint32_t type;		/* Request type */
	uint32_t flags;		/* EOM==1 means this is the last record for this message */
	uint32_t msg_no;	/* Unique for each request */
	union {
		uint32_t req_id;	/* request id */
		uint32_t rsp_err;	/* response error  */
	};
	uint32_t rec_len;	/* Record length in bytes including this header */
} *ldmsd_req_hdr_t;
#pragma pack(pop)

/**
 * The format of requests and replies is as follows:
 *
 *                 Standard Record Format
 * +---------------------------------------------------------+
 * | ldmsd_req_hdr_s                                         |
 * +---------------------------------------------------------+
 * | attr_0, attr_discrim == 1                               |
 * +---------------------------------------------------------+
 * S                                                         |
 * +---------------------------------------------------------+
 * | attr_N, attr_discrim == 1                               |
 * +---------------------------------------------------------+
 * | attr_discrim == 0, remaining fields are not present     |
 * +---------------------------------------------------------+
 *
 *                 String Record Format
 * +---------------------------------------------------------+
 * | ldmsd_req_hdr_s                                         |
 * +---------------------------------------------------------+
 * | String bytes, i.e. attr_discrim > 1                     |
 * | length is rec_len - sizeof(ldmsd_req_hdr_s)             |
 * S                                                         S
 * +---------------------------------------------------------+
 */
typedef struct req_ctxt_key {
	uint32_t msg_no;
	uint64_t conn_id;
} *msg_key_t;

struct ldmsd_req_ctxt;


typedef struct ldmsd_cfg_ldms_s {
	ldms_t ldms;
} *ldmsd_cfg_ldms_t;

typedef struct ldmsd_cfg_sock_s {
	int fd;
	struct sockaddr_storage ss;
} *ldmsd_cfg_sock_t;

typedef struct ldmsd_cfg_file_s {
	uint32_t next_msg_no;
} *ldmsd_cfg_file_t;

struct ldmsd_cfg_xprt_s;
typedef int (*ldmsd_cfg_send_fn_t)(struct ldmsd_cfg_xprt_s *xprt, char *data, size_t data_len);
typedef void (*ldmsd_cfg_cleanup_fn_t)(struct ldmsd_cfg_xprt_s *xprt);
typedef struct ldmsd_cfg_xprt_s {
	union {
		void *xprt;
		struct ldmsd_cfg_file_s file;
		struct ldmsd_cfg_sock_s sock;
		struct ldmsd_cfg_ldms_s ldms;
	};
	size_t max_msg;
	ldmsd_cfg_send_fn_t send_fn;
	ldmsd_cfg_cleanup_fn_t cleanup_fn;
} *ldmsd_cfg_xprt_t;

#define LINE_BUF_LEN 1024
#define REQ_BUF_LEN 4096
#define REP_BUF_LEN REQ_BUF_LEN

typedef struct ldmsd_req_ctxt {
	struct req_ctxt_key key;
	struct rbn rbn;

	ldmsd_cfg_xprt_t xprt;	/* network transport */

	int rec_no;
	uint32_t errcode;

	size_t line_len;
	char *line_buf;

	size_t req_len;
	size_t req_off;
	char *req_buf;

	size_t rep_len;
	size_t rep_off;
	char *rep_buf;
} *ldmsd_req_ctxt_t;

#pragma pack(push, 1)
typedef struct ldmsd_req_attr_s {
	uint32_t discrim;	/* If 0, the remaining fields are not present */
	uint32_t attr_id;	/* Attribute identifier, unique per ldmsd_req_hdr_s.cmd_id */
	uint32_t attr_len;	/* Size of value in bytes */
	union {
		uint8_t attr_value[OVIS_FLEX_UNION];
		uint16_t attr_u16[OVIS_FLEX_UNION];
		uint32_t attr_u32[OVIS_FLEX_UNION];
		uint64_t attr_u64[OVIS_FLEX_UNION];
	};
} *ldmsd_req_attr_t;
#pragma pack(pop)

/**
 * \brief Translate a config string to a LDMSD configuration request
 *
 * The configuration line is in the format of <verb>[ <attr=value> <attr=value>..]
 * The function performs the following tasks.
 * - Set the request ID stored in \c request
 * - Parser the non-null string \c cfg to ldmsd configuration message boundary
 *   protocol and store the request data in \c *buf.
 * - \c bufoffset is set to the end of the data.
 * - If \c buf is not large enough, it is re-allocated to the necessary size.
 *   \c *buflen is updated.
 *
 * \param cfg A string containing the configuaration command text
 * \param msg_no The next unique message number
 *
 * \return The request parsed from the string
 */
ldmsd_req_hdr_t ldmsd_parse_config_str(const char *cfg, uint32_t msg_no);

/**
 * \brief Convert a command string to the request ID.
 *
 * \param verb   Configuration command string
 *
 * \return The request ID is returned on success. LDMSD_NOTSUPPORT_REQ is returned
 *         if the command does not exist.
 */
uint32_t ldmsd_req_str2id(const char *verb);

/**
 * \brief Convert a attribute name to the attribute ID
 *
 * \param name   Attribute name string
 *
 * \return The attribute ID is returned on success. -1 is returned if
 *         the attribute name is not recognized.
 */
int32_t ldmsd_req_attr_str2id(const char *name);

/**
 * \brief Return a request attribute handle of the given ID
 *
 * \param request   Buffer storing request header and the attributes
 * \param attr_id   Attribute ID
 *
 * \return attribute handle. NULL if it does not exist.
 */
ldmsd_req_attr_t ldmsd_req_attr_get_by_id(char *request, uint32_t attr_id);

/**
 * \brief Return a request attribute handle of the given name
 *
 * \param request   Buffer storing request header and the attributes
 * \param name      Attribute name
 *
 * \return attribute handle. NULL if it does not exist.
 */
ldmsd_req_attr_t ldmsd_req_attr_get_by_name(char *request, const char *name);

/**
 * \brief Return the value string of the given attribute ID
 *
 * \param request   Buffer storing request header and the attributes
 * \param attr_id   Attribute ID
 *
 * \return a string. NULL if it does not exist.
 */
char *ldmsd_req_attr_str_value_get_by_id(char *request, uint32_t attr_id);

/**
 * \brief Verify whether the given attribute/keyword ID exists or not.
 *
 * \param request   Buffer storing request header and the attributes
 * \param attr_id   ID of the keyword
 *
 * \return 1 if the keyword or attribute exists. Otherwise, 0 is returned.
 */
int ldmsd_req_attr_keyword_exist_by_id(char *request, uint32_t attr_id);

/**
 * \brief Return the value string of the given attribute name
 *
 * \param request   Buffer storing request header and the attributes
 * \param name      Attribute name
 *
 * \return a string. NULL if it does not exist.*
 */
char *ldmsd_req_attr_str_value_get_by_name(char *request, const char *name);

/**
 * \brief Verify whether the given attribute/keyword name exists or not.
 *
 * \param request   Buffer storing request header and the attributes
 * \param name      Attribute name
 *
 * \return 1 if the keyword or attribute exists. Otherwise, 0 is returned.
 */
int ldmsd_req_attr_keyword_exist_by_name(char *request, const char *name);

/**
 * Convert the request header byte order from host to network
 */
void ldmsd_hton_req_hdr(ldmsd_req_hdr_t req);
/**
 * Convert the request attribute byte order from host to network
 */
void ldmsd_hton_req_attr(ldmsd_req_attr_t attr);
/**
 * Convert the message byte order from host to network
 *
 * It assumes that the buffer \c msg contains the whole message.
 */
void ldmsd_hton_req_msg(ldmsd_req_hdr_t msg);
/**
 * Convert the request header byte order from network to host
 */
void ldmsd_ntoh_req_hdr(ldmsd_req_hdr_t req);
/**
 * Convert the request attribute byte order from network to host
 */
void ldmsd_ntoh_req_attr(ldmsd_req_attr_t attr);
/**
 * Convert the message byte order from network to host
 *
 * It assumes that the buffer \c msg contains the whole message.
 */
void ldmsd_ntoh_req_msg(ldmsd_req_hdr_t msg);

/**
 * \brief Advise the peer our configuration record length
 *
 * Send configuration record length advice
 *
 * \param xprt The configuration transport handle
 * \param rec_len The record length
 */
void ldmsd_send_cfg_rec_adv(ldmsd_cfg_xprt_t xprt, uint32_t msg_no, uint32_t rec_len);
int ldmsd_process_config_request(ldmsd_cfg_xprt_t xprt, ldmsd_req_hdr_t request, size_t req_len);
int ldmsd_append_reply(struct ldmsd_req_ctxt *reqc, char *data, size_t data_len, int msg_flags);
void ldmsd_send_error_reply(ldmsd_cfg_xprt_t xprt, uint32_t msg_no,
			    uint32_t error, char *data, size_t data_len);
int ldmsd_handle_request(ldmsd_req_ctxt_t reqc);
static inline ldmsd_req_attr_t ldmsd_first_attr(ldmsd_req_hdr_t rh)
{
	return (ldmsd_req_attr_t)(rh + 1);
}

static inline ldmsd_req_attr_t ldmsd_next_attr(ldmsd_req_attr_t attr)
{
	return (ldmsd_req_attr_t)&attr->attr_value[attr->attr_len];
}


#endif /* LDMS_SRC_LDMSD_LDMSD_REQUEST_H_ */
