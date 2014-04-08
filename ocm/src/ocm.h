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
/** * \file ocm.h
 * \defgroup OCM
 * \{
 * \brief OVIS Configuration Management Library.
 *
 * OVIS Configuration Management Library, or libocm, is an implementation of a
 * simple configuration protocol to configure multiple program instances over
 * the network. libocm is event-based and based on libzap. There are
 * configuration provider side and configuration receiver side. They're called
 * the provider and the receiver for short. The provider is the initiator side.
 * Application can add a receiver using function \c ocm_add_receiver(). The
 * provider will then actively connects to the receiver. On the receiver side,
 * the application can register itself to the service with function \c
 * ocm_register(). Then, the application should call \c ocm_enable() to let \c
 * ocm knows that it is ready.  When the receiver and the provider are connected
 * (without application knowing), the receiver side send requests using \c key
 * povided by the application to the provider. The callback function \c
 * (ocm_cb_fn_t) on the provider side will be called with an event that contain
 * request information. The provider side application can use \c key to get or
 * create configuration for this request. The configuration can be build using
 * \c ocm_cfg_buff (with functions \c ocm_cfg_buff_new() , \c
 * ocm_cfg_buff_add_verb() and \c ocm_cfg_buff_add_av()). Then, the application
 * on the provider side can respond the request event with function \c
 * ocm_event_resp_err() or \c ocm_event_resp_cfg().
 *
 * When the connection goes away, the provider will try to reconnect and the
 * same process is repeated.
 *
 * When the provider comes up (e.g. from the crash), it will reconnect and send
 * the configuration to the receivers. In this event, ocm library in the
 * receiver side will automatically drop the configuration that has been
 * received.
 *
 */
#ifndef __OCM_H
#define __OCM_H
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <zap/zap.h>
#include <errno.h>

#define OCM_DEFAULT_PORT 54321

typedef struct ocm * ocm_t;
typedef struct ocm_err * ocm_err_t;
typedef struct ocm_cfg_cmd * ocm_cfg_cmd_t;
typedef struct ocm_cfg * ocm_cfg_t;
typedef struct ocm_cfg_req * ocm_cfg_req_t;

/**
 * Get \c key from error \c e.
 */
const char *ocm_err_key(const ocm_err_t e);

/**
 * Get \c msg from error \c e.
 */
const char *ocm_err_msg(const ocm_err_t e);

/**
 * Get error code from error \c e.
 */
int ocm_err_code(const ocm_err_t e);

/**
 * OCM String.
 * For convenience, \c str will also contain the null-terminated '\\0' at the
 * end of the string, and \c len is \c strlen(str) + 1.
 */
struct ocm_str {
	int len;
	char str[0];
};

/**
 * ::ocm_str constructor.
 */
static struct ocm_str *ocm_str_create(const char *s)
{
	int len = strlen(s) + 1;
	struct ocm_str *str;
	str = calloc(1, sizeof(*str) + len);
	if (!str)
		return NULL;
	strcpy(str->str, s);
	return str;
}

/**
 * ::ocm_str size calculation.
 */
static size_t ocm_str_size(struct ocm_str *str)
{
	return sizeof(*str) + str->len;
}

typedef enum ocm_value_type {
	OCM_VALUE_INT8,
	OCM_VALUE_INT16,
	OCM_VALUE_INT32,
	OCM_VALUE_INT64,
	OCM_VALUE_UINT8,
	OCM_VALUE_UINT16,
	OCM_VALUE_UINT32,
	OCM_VALUE_UINT64,
	OCM_VALUE_FLOAT,
	OCM_VALUE_DOUBLE,
	OCM_VALUE_STR,
	OCM_VALUE_CMD, /** verb,av as value */
	OCM_VALUE_LAST
} ocm_value_type_t;

/**
 * Value part (in attribute-value pair).
 */
struct ocm_value {
	ocm_value_type_t type;
	union {
		int8_t i8;
		int16_t i16;
		int32_t i32;
		int64_t i64;
		uint8_t u8;
		uint16_t u16;
		uint32_t u32;
		uint64_t u64;
		float f;
		double d;
		struct ocm_str s;
		ocm_cfg_cmd_t cmd;
	};
};

void ocm_value_set_s(struct ocm_value *v, const char *s);

void ocm_value_set(struct ocm_value *v, ocm_value_type_t type, ...);

/**
 * Get verb from the command.
 */
const char *ocm_cfg_cmd_verb(ocm_cfg_cmd_t cmd);

/**
 * Attribute-value iterator
 */
struct ocm_av_iter {
	struct ocm_cfg_cmd *cmd;
	void* next_av; /**< Point to next \c av in \c cmd->data */
};

/**
 * Initializes the pre-allocated \c iter with \c cmd.
 * \returns 0 on success.
 * \returns Error number on error.
 */
int ocm_av_iter_init(struct ocm_av_iter* iter, ocm_cfg_cmd_t cmd);

/**
 * Reset iterator \c iter.
 */
void ocm_av_iter_reset(struct ocm_av_iter *iter);

const struct ocm_value *ocm_av_get_value(ocm_cfg_cmd_t cmd, const char *attr);

/**
 * Iterator for ::ocm_cfg_cmd inside ::ocm_cfg.
 */
struct ocm_cfg_cmd_iter {
	ocm_cfg_t cfg;
	/**
	 * Pointer to next cmd in cfg->data.
	 */
	void* next_cmd;
};

/**
 * Next attribute-value.
 * \c *attr and \c *v will be set to point to attribute and its value
 * respectively if the iterator is not at its end. If the iterator is at its
 * end, \c *attr and \c *v are left untouched.
 * \return 0 on success.
 * \returns -1 if there are no more attribute-value.
 */
int ocm_av_iter_next(struct ocm_av_iter *iter, const char **attr,
		const struct ocm_value **v);

/* The following functions are similar to those of ::ocm_av_iter */
void ocm_cfg_cmd_iter_init(struct ocm_cfg_cmd_iter *iter, ocm_cfg_t cfg);
void ocm_cfg_cmd_iter_reset(struct ocm_cfg_cmd_iter *iter);
/**
 * \param iter The iterator handle.
 * \param[out] cmd The ::ocm_cfg_cmd handle.
 * \returns 0 on success and \c *cmd is set.
 * \returns -1 if there are no more commands and \c *cmd is untouched.
 */
int ocm_cfg_cmd_iter_next(struct ocm_cfg_cmd_iter *iter,
					ocm_cfg_cmd_t *cmd);

typedef enum ocm_event_type {
	OCM_EVENT_ERROR,
	OCM_EVENT_CFG_REQUESTED,
	OCM_EVENT_CFG_RECEIVED,
	OCM_EVENT_LAST
} ocm_event_type_t;

/**
 * OCM event structure.
 */
struct ocm_event {
	ocm_t ocm;
	zap_ep_t ep;
	ocm_event_type_t type;
	union {
		ocm_cfg_req_t req;
		ocm_cfg_t cfg;
		ocm_err_t err;
	};
};

/**
 * Get the key of the request \c cfg.
 */
const char *ocm_cfg_req_key(ocm_cfg_req_t req);

/**
 * Get the key of the configuration \c cfg.
 */
const char *ocm_cfg_key(ocm_cfg_t cfg);

/**
 * Buffer to help building ::ocm_cfg.
 */
struct ocm_cfg_buff {
	ocm_cfg_t buff; /**< Dynamic buffer for configuration, buffer can be expanded if needed */
	size_t buff_len; /**< Total buffer length */
	uint64_t cmd_offset; /**< Indexed to the current command in buff */
	uint64_t current_offset; /**< Indexed to the place that will be written next in buff */
};

/**
 * ::ocm_cfg_buff constructor.
 * Please note that \c init_buff_size is just an initial size of the
 * configuration buffer. The buffer will dynamically grow if needed.
 * \param init_buff_size The initial size of the buffer.
 * \param key The key of the configuration.
 * \returns The pointer to ocm_cfg_buff on success.
 * \returns NULL on error.
 */
struct ocm_cfg_buff *ocm_cfg_buff_new(size_t init_buff_size, const char *key);

/**
 * Reset the buffer with \c new_key.
 * \param buff Buffer handle.
 * \param new_key The new key for configuration buffer.
 *
 * \returns 0 on success.
 * \returns Error code on error.
 */
int ocm_cfg_buff_reset(struct ocm_cfg_buff *buff, const char *new_key);

/**
 * Add a \c verb into the buffer.
 * Adding new verb means that the previous working command is finished.
 *
 * \returns 0 on success.
 * \returns Error code on error.
 */
int ocm_cfg_buff_add_verb(struct ocm_cfg_buff *buff, const char *verb);

/**
 * Add attribute-value pair into the current command in the buffer.
 *
 * \returns 0 on success.
 * \returns Error code on error.
 */
int ocm_cfg_buff_add_av(struct ocm_cfg_buff *buff, const char *attr,
						struct ocm_value *value);

/**
 * Same as ::ocm_cfg_buff_add_av, except that the value is \c cmd. This allows
 * nested attribute-values list.
 */
int ocm_cfg_buff_add_cmd_as_av(struct ocm_cfg_buff *buff, const char *attr,
		ocm_cfg_cmd_t cmd);

/**
 * Get current pointer in the buffer.
 *
 * \param buff The buffer.
 *
 * \returns current pointer in the buffer.
 */
void *ocm_cfg_buff_curr_ptr(struct ocm_cfg_buff *buff);

/**
 * Get current ::ocm_cfg_cmd in the buffer.
 *
 * \param buff The buffer.
 *
 * \returns current command in the buffer.
 */
struct ocm_cfg_cmd *ocm_cfg_buff_curr_cmd(struct ocm_cfg_buff *buff);

/**
 * Free the buffer.
 */
void ocm_cfg_buff_free(struct ocm_cfg_buff *buff);


/**
 * Callback function definition for \c ocm.
 */
typedef int (*ocm_cb_fn_t)(struct ocm_event *e);

/**
 * Create an OCM handle with transport \c xprt.  If \c xprt is NULL, it will be
 * determined from environment variable \c OCM_XPRT. If none is set, "sock" will
 * be used.
 *
 * OCM will call the \c request_cb function when it receive a request.
 *
 * \param xprt The transport to be used.
 * \param port The port number to listen to remote configurator.
 * \param request_cb The request callback function.
 * \param log_fn The log function. If NULL, ocm will use its default log
 *               function ::__ocm_default_log().
 *
 * \returns an OCM handle if success.
 * \returns NULL on error.
 */
ocm_t ocm_create(const char *xprt, uint16_t port, ocm_cb_fn_t request_cb,
		 void (*log_fn)(const char *fmt, ...));

/**
 * Add a receiver.
 */
int ocm_add_receiver(ocm_t ocm, struct sockaddr *sa, socklen_t sa_len);

/**
 * Remove a receiver.
 *
 * \returns 0 on success.
 * \returns Error code on error.
 */
int ocm_remove_receiver(ocm_t ocm, struct sockaddr *sa, socklen_t sa_len);

/**
 * Configuration regeisteration for the side that request configuration info.
 * \c cb will be called when the configuration for \c key is received.
 */
int ocm_register(ocm_t ocm, const char *key, ocm_cb_fn_t cb);

/**
 * Deregister the configuration for \c key.
 */
int ocm_deregister(ocm_t ocm, const char *key);

/**
 * Enable the \c ocm.
 *
 * \returns 0 on success.
 * \returns Error code on error.
 */
int ocm_enable(ocm_t ocm);

/**
 * Respond an error to an OCM event \c e.
 *
 * Application should call this if it cannot serve OCM request in
 * the event \c e.
 *
 * \returns 0 on success.
 * \returns zap error code on error.
 */
int ocm_event_resp_err(struct ocm_event *e, int code, const char *key,
			const char *emsg);

/**
 * Respond a configuration to an OCM event \c e.
 *
 * Application call this function to serve configuration \c cfg to the request
 * in event \c e.
 *
 */
int ocm_event_resp_cfg(struct ocm_event *e, ocm_cfg_t cfg);

/**
 * Close and destroy the OCM handle.
 * ::ocm_close() should be called when nobody uses it anymore.
 */
int ocm_close(ocm_t ocm);

#endif /* __OCM_H */
/** \} */
