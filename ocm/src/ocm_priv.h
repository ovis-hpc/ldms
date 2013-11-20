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
	int is_active; /**< True if this is the active side. */
	struct sockaddr sa; /**< Socket address to connect to */
	socklen_t sa_len; /**< Length of the \c sa */
	struct event *reconn_event; /**< Reconnect event */
};


#endif
