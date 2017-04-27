/*
 * ldmsd_request.h
 *
 *  Created on: Jun 24, 2016
 *      Author: nichamon
 */

#include <inttypes.h>
#include "coll/rbt.h"

#ifndef LDMS_SRC_LDMSD_LDMSD_REQUEST_H_
#define LDMS_SRC_LDMSD_LDMSD_REQUEST_H_

enum ldmsd_request {
	LDMSD_CLI_REQ = 0x1,
	LDMSD_EXAMPLE_REQ,
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
