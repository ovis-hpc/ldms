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

#ifndef LDMS_SRC_LDMSD_LDMSD_REQUEST_H_
#define LDMS_SRC_LDMSD_LDMSD_REQUEST_H_

enum ldmsd_request {
	LDMSD_EXAMPLE_REQ = 0x1,
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
	LDMSD_ATTR_LAST,
};

#define LDMSD_RECORD_MARKER 0xffffffff

#define LDMSD_REQ_SOM_F	1
#define LDMSD_REQ_EOM_F	2

typedef struct ldmsd_req_hdr_s {
	uint32_t marker;	/* Always has the value 0xff */
	uint32_t flags;		/* EOM==1 means this is the last record for this message */
	uint32_t msg_no;	/* Unique for each request */
	uint32_t code;		/* For command req, it is the unique command id. For command response, it is the error code. */
	uint32_t rec_len;	/* Record length in bytes including this header */
} *ldmsd_req_hdr_t;

typedef struct req_ctxt_key {
	uint32_t msg_no;
	uint32_t sock_fd;
} *msg_key_t;

struct ldmsd_req_ctxt;
/** Function to handle the response */
typedef int (*ldmsd_req_resp_handler_t)(struct ldmsd_req_ctxt *ctxt,
				char *data, size_t data_len, int msg_flags);
typedef struct ldmsd_req_ctxt {
	struct req_ctxt_key key;
	struct rbn rbn;
	struct ldmsd_req_hdr_s rh;
	struct msghdr *mh;
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
	int dest_fd; /* Where to respond back to */
	ldmsd_req_resp_handler_t resp_handler;
} *ldmsd_req_ctxt_t;

typedef struct ldmsd_req_attr_s {
	uint32_t discrim;	/* If 0, end of attr_list */
	uint32_t attr_id;	/* Attribute identifier, unique per ldmsd_req_hdr_s.cmd_id */
	uint32_t attr_len;	/* Size of value in bytes */
	uint8_t attr_value[0];	/* Size is attr_len */
} *ldmsd_req_attr_t;

typedef struct ldmsd_req_hdr_s *ldmsd_req_hdr_t;

/**
 * \brief Process a configuration line and prepare the buffer of ldmsd_req_attr
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
 * \param request	request header to be set.
 * \param cfg
 * \param buf
 * \param bufoffset
 * \param buflen
 *
 * \return 0 on success. An error code is returned on failure.
 */
int ldmsd_process_cfg_str(ldmsd_req_hdr_t request, const char *cfg,
			char **buf, size_t *bufoffset, size_t *buflen);

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
uint32_t ldmsd_req_attr_str2id(const char *name);

#endif /* LDMS_SRC_LDMSD_LDMSD_REQUEST_H_ */
