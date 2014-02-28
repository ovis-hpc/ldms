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
 * \mainpage Zap
 *
 * Zap is a transport independent API for RDMA capable transports. It
 * is a thin wrapper layer that allows user-mode applications to use
 * RDMA operations without regard to network address or API
 * differences between transports. The Zap connection model is similar
 * to sockets for SOCK_STREAM connections. On the passive side, an
 * endpoint is created and the application 'listens' on a local
 * address and port. On the active side, an endpoint is created and a
 * connection is requested.
 *
 * The Connection Management API are as follows:
 *
 * \li \b zap_new() Create a new Zap endpoint.
 * \li \b zap_listen() Listen for incoming connect requests on a Zap endpoint.
 * \li \b zap_connect() Request a connection with a remote peer.
 * \li \b zap_accept() Accept a connection from a remote peer.
 * \li \b zap_close() Close a connection with a remote peer on a Zap endpoint
 */
#ifndef __ZAP_H__
#define __ZAP_H__
#include <inttypes.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>

#define ZAP_MAX_TRANSPORT_NAME_LEN 16

#define ZAP_ENUM_WRAP(X) X
#define ZAP_STR_WRAP(X) #X

typedef struct zap_ep *zap_ep_t;
typedef struct zap *zap_t;
typedef void (*zap_log_fn_t)(const char *fmt, ...);
typedef struct zap_map *zap_map_t;

#define ZAP_EVENT_LIST(ZAP_WRAP) \
	/*! An incoming connect request is ready to be accepted or rejected.  */\
	ZAP_WRAP(ZAP_EVENT_CONNECT_REQUEST), \
	/*! A connect request failed due to a network error. */ \
	ZAP_WRAP(ZAP_EVENT_CONNECT_ERROR), \
	/*! An active request initiated with \c zap_connect was accepted.  */ \
	ZAP_WRAP(ZAP_EVENT_CONNECTED), \
	/*! A connect request was rejected by the peer.  */ \
	ZAP_WRAP(ZAP_EVENT_REJECTED), \
	/*! A connection endpoint has been disconnected. */ \
	ZAP_WRAP(ZAP_EVENT_DISCONNECTED), \
	/*! Data has been received. */ \
	ZAP_WRAP(ZAP_EVENT_RECV_COMPLETE), \
	/*! An \c zap_read_start request has completed. */ \
	ZAP_WRAP(ZAP_EVENT_READ_COMPLETE), \
	/*! An \c zap_write_start request has completed. */ \
	ZAP_WRAP(ZAP_EVENT_WRITE_COMPLETE), \
	/*! An \c The peer has shared a buffer with \c zap_share. */ \
	ZAP_WRAP(ZAP_EVENT_RENDEZVOUS), \
	/*! Last event (dummy) */ \
	ZAP_WRAP(ZAP_EVENT_LAST)

typedef enum zap_event_type {
	ZAP_EVENT_LIST(ZAP_ENUM_WRAP)
} zap_event_type_t;

static char *__zap_event_str[] = {
	ZAP_EVENT_LIST(ZAP_STR_WRAP)
};

static inline
const char* zap_event_str(enum zap_event_type e)
{
	if (e < 0 || e > ZAP_EVENT_LAST)
		return "ZAP_EVENT_UNKNOWN";
	return __zap_event_str[e];
}

#define ZAP_ERR_LIST(ZAP_WRAP)                \
	ZAP_WRAP(ZAP_ERR_OK),                 \
	ZAP_WRAP(ZAP_ERR_PARAMETER),          \
	ZAP_WRAP(ZAP_ERR_TRANSPORT),          \
	ZAP_WRAP(ZAP_ERR_ENDPOINT),           \
	ZAP_WRAP(ZAP_ERR_ADDRESS),            \
	ZAP_WRAP(ZAP_ERR_ROUTE),              \
	ZAP_WRAP(ZAP_ERR_MAPPING),            \
	ZAP_WRAP(ZAP_ERR_RESOURCE),           \
	ZAP_WRAP(ZAP_ERR_BUSY),               \
	ZAP_WRAP(ZAP_ERR_NO_SPACE),           \
	ZAP_WRAP(ZAP_ERR_INVALID_MAP_TYPE),   \
	ZAP_WRAP(ZAP_ERR_CONNECT),            \
	ZAP_WRAP(ZAP_ERR_NOT_CONNECTED),      \
	ZAP_WRAP(ZAP_ERR_HOST_UNREACHABLE),   \
	ZAP_WRAP(ZAP_ERR_LOCAL_LEN),          \
	ZAP_WRAP(ZAP_ERR_LOCAL_OPERATION),    \
	ZAP_WRAP(ZAP_ERR_LOCAL_PERMISSION),   \
	ZAP_WRAP(ZAP_ERR_REMOTE_LEN),         \
	ZAP_WRAP(ZAP_ERR_REMOTE_OPERATION),   \
	ZAP_WRAP(ZAP_ERR_REMOTE_PERMISSION),  \
	ZAP_WRAP(ZAP_ERR_RETRY_EXCEEDED),     \
	ZAP_WRAP(ZAP_ERR_TIMEOUT),            \
	ZAP_WRAP(ZAP_ERR_FLUSH),              \
	ZAP_WRAP(ZAP_ERR_REMOTE_NOENTRY),     \
	ZAP_WRAP(ZAP_ERR_LAST)

typedef enum zap_err_e {
	ZAP_ERR_LIST(ZAP_ENUM_WRAP)
} zap_err_t;

static char *__zap_err_str[] = {
	ZAP_ERR_LIST(ZAP_STR_WRAP)
};

static inline
const char* zap_err_str(enum zap_err_e e)
{
	if (e < 0 || e > ZAP_ERR_LAST)
		return "ZAP_ERR_UNKNOWN";
	return __zap_err_str[e];
}

/**
 * Convert a given errno \c e into ::zap_err_e.
 * \param e The errno.
 * \returns ::zap_err_e.
 */
enum zap_err_e errno2zaperr(int e);

typedef struct zap_event {
	zap_event_type_t type;
	zap_err_t status;
	zap_map_t map;
	void *data;
	size_t data_len;
	void *context;
} *zap_event_t;

typedef void (*zap_cb_fn_t)(zap_ep_t zep, zap_event_t ev);

#define zap_ptr_(_t, _p, _o) (_t *)&((char *)_p)[_o]

typedef struct zap_mem_info {
	void *start;
	size_t len;
} *zap_mem_info_t;
typedef zap_mem_info_t (*zap_mem_info_fn_t)(void);

/**
 * \brief Get the context in the endpoint
 *
 * \param ep The handle to the endpoint which contains the ucontext
 */
void *zap_get_ucontext(zap_ep_t ep);

/**
 * \brief Set the context in the endpoint
 *
 * \param ep The handle to the endpoint in which the ucontext will be set.
 * \param context The user context
 */
void zap_set_ucontext(zap_ep_t ep, void *context);

/** \brief Get a handle to a transport.
 *
 * Get a transport handle to a transport.
 *
 * \param name	The transport name, e.g. 'rdma', 'sock', etc...
 * \param log_fn Pointer to the function to uuse for logging errors or status
 * \param pz	Pointer to the handle where the new transport handle should
 *		be stored.
 * \param mfn	Pointer to a function that returns the mapped memory info
 * \return 0	The transport was created successfully
 * \return ZAP_ERR_TRANSPORT	The transport name was not found.
 * \return ZAP_ERR_RESOURCE	There were insufficient resources to complete the request
 */
zap_err_t zap_get(const char *name, zap_t *pz, zap_log_fn_t log_fn,
		  zap_mem_info_fn_t map_info_fn);

/** \brief Returns the max send message size.
 *
 * Returns the max message size supported by the transport.
 *
 * \param t	The transport handle.
 * \return !0	The max message size. A zero value indicates a bad transport handle.
 */
size_t zap_max_msg(zap_t z);

/** \brief Create a new endpoint on a transport.
 *
 * \param z	The Zap transport handle
 * \param pep	Pointer to the handle where the new endpoint should be stored.
 * \param cb	Ponter to a function to receive asynchronous events on the endpoint.
 * \return 0	The endpoint was created successfully
 * \return ZAP_ERR_TRANSPORT	The transport handle is invalid.
 * \return ZAP_ERR_RESOURCE	There were insufficient resources to complete the request.
 */
zap_err_t zap_new(zap_t z, zap_ep_t *pep, zap_cb_fn_t cb);

/** \brief Release an endpoing.
 *
 * Relase all resources associated with the endpoint including all
 * active buffer mappings.
 *
 * \param ep	The endpoint handle to free
 * \return 0	Success.
 * \return ZAP_ERR_ENDPOINT	An invalid endpoint handle was specified
 * \return ZAP_ERR_BUSY		The endpoint is in-use
 */
zap_err_t zap_free(zap_ep_t ep);

/** \brief Request a connection with a remote peer.
 *
 * \param ep	The transport handle.
 * \param sa	Pointer to a sockaddr containing the address of the
 *		remote peer.
 * \param sa_len Size of the sockaddr in bytes.
 * \return 0	Success
 * \return !0	A Zap error code. See zap_err_t.
 */
zap_err_t zap_connect(zap_ep_t ep, struct sockaddr *sa, socklen_t sa_len);

/**
 * The blocking version of ::zap_connect()
 *
 * \param ep	The transport handle.
 * \param sa	Pointer to a sockaddr containing the address of the
 *		remote peer.
 * \param sa_len Size of the sockaddr in bytes.
 * \return 0	Success
 * \return !0	A Zap error code. See zap_err_t.
 */
zap_err_t zap_connect_block(zap_ep_t ep, struct sockaddr *sa, socklen_t sa_len);

/**
 * The blocking and easier version of ::zap_connect()
 *
 * \param ep	The transport handle.
 * \param host_port A string in the form of "host:port"
 *
 * \return 0	Success
 * \return !0	A Zap error code. See zap_err_t.
 */
zap_err_t zap_connect_ez(zap_ep_t ep, const char *host_port);

/** \brief Accept a connection request from a remote peer.
 *
 * \param ep	The endpoint handle.
 * \param cb	Ponter to a function to receive asynchronous events on the endpoint.
 * \return 0	Success
 * \return !0	A Zap error code. See zap_err_t.
 */
zap_err_t zap_accept(zap_ep_t ep, zap_cb_fn_t cb);

/** \brief Return the local and remote addresses for an endpoint.
 *
 * \param ep	The endpoint handle.
 * \return 0	Success
 * \return !0	A Zap error code. See zap_err_t.
 */
zap_err_t zap_get_name(zap_ep_t ep, struct sockaddr *local_sa,
		       struct sockaddr *remote_sa, socklen_t *sa_len);

void zap_get_ep(zap_ep_t ep);
void zap_put_ep(zap_ep_t ep);

/** \brief Reject a connection request from a remote peer.
 *
 * \param ep	The transport handle.
 * \return 0	Success
 * \return !0	A Zap error code. See zap_err_t.
 */
zap_err_t zap_reject(zap_ep_t ep);

/** \brief Listen for incoming connection requests */
zap_err_t zap_listen(zap_ep_t ep, struct sockaddr *sa, socklen_t sa_len);

/** Close the connection */
zap_err_t zap_close(zap_ep_t ep);

/** \brief Send data to the peer
 *
 * \param ep	The endpoint handle
 * \param buf	Pointer to the buffer to send
 * \param sz	The number of bytes from the buffer to send
 * \returns	ZAP_ERR_OK on success, or a zap_err_t value indicating the
 *		reason for failure.
 */
zap_err_t zap_send(zap_ep_t ep, void *buf, size_t sz);

/** \brief RDMA write data to a remote buffer */
zap_err_t zap_write(zap_ep_t t,
		    zap_map_t src_map, void *src,
		    zap_map_t dst_map, void *dst, size_t sz,
		    void *context);

/** \brief RDMA read data from a remote buffer */
zap_err_t zap_read(zap_ep_t z,
		   zap_map_t src_map, void *src,
		   zap_map_t dst_map, void *dst, size_t sz,
		   void *context);

/** \brief Zap buffer mapping access rights. */
typedef enum zap_access {
	ZAP_ACCESS_NONE = 0,	/*! Only local access is allowed */
	ZAP_ACCESS_WRITE = 1,	/*! Remote write is granted  */
	ZAP_ACCESS_READ = 2,	/*! Remote read is granted */
} zap_access_t;

typedef enum zap_map_type {
	ZAP_MAP_LOCAL = 1,	  /*! A local map acquired with zap_map */
	ZAP_MAP_REMOTE = 2,  /*! A map received from a remote peer vis zap_share  */
} zap_map_type_t;

/** \brief Allocate a remote buffer
 *
 * Map an area of local memory so that it can be used in RDMA
 * operations. This mapping can be shared with a remote peer using the
 * \c zap_share interface.
 *
 * A buffer must be mapped if it is either the data source or the data
 * sink of an RDMA WRITE request submitted with \c zap_write_start or
 * an RDMA READ request submitted with \c zap_read_start. Note that a
 * buffer does not need to be shared using the \c zap_share_map if it
 * is only used locally, i.e. it is not the target of an RDMA WRITE or
 * the source of an RDMA READ.
 *
 * \param ep	The endpoint handle
 * \param pm	Pointer to the map handle
 * \param addr	The memory address of the buffer
 * \param sz	The size in bytes of the buffer
 * \param acc	The remote access flags of the buffer
 * \return 0	Success, or a non-zero zap_err_t error code.
 */
zap_err_t zap_map(zap_ep_t ep, zap_map_t *pm,
		  void *addr, size_t sz, zap_access_t acc);

/** \brief Return the length of the mapped buffer.
 *
 * \param map	The map handle.
 * \returns !0	The size of of the mapped buffer in bytes.
 * \returns 0	The map is invalid..
 */
size_t zap_map_len(zap_map_t map);

/** \brief Return the address of the mapped buffer.
 *
 * Returns the address of the start of the mapped buffer as a void
 * pointer. The rationale for void * is to allow assignment to a
 * pointer to a data structure without an explicit cast.
 *
 * \param map	The map handle.
 * \returns !0	Pointer to the start of the mapped buffer.
 * \returns NULL The map is invalid.
 */
void* zap_map_addr(zap_map_t map);

/** \brief Unmap a buffer previously mapped with \c zap_map_buf
 *
 * Unmap a buffer previously mapped with \c zap_map_buf. The buffer
 * will no longer be accessible to the remote peer. Note that the peer is
 * not notified that the mapping has been removed.
 *
 * \param ep	The endpoint handle
 * \param map	The buffer mapping returned by a previous call to \c
 *		zap_map_buf
 * \return 0	Success, or a non-zero zap_err_t error code.
 */
zap_err_t zap_unmap(zap_ep_t ep, zap_map_t map);

/** \brief Share a mapping with a remote peer
 *
 * Send the remote peer your zap_buf_t information. If successful, the
 * peer will be able to RDMA READ and/or WRITE into the buffer. Note
 * that the access they are granted depends on the zap_access_t flags
 * provide when the buffer was mapped with \c zap_map_buf.
 *
 * \param ep	The endpoint handle
 * \param m	A Zap buffer mapping returned by \c zap_map_buf.
 * \param ctxt	A user-defined context that will be provided to the
 *		peer when it is notified of the mapping.
 * depending upon the
 */
zap_err_t zap_share(zap_ep_t ep, zap_map_t m, uint64_t ctxt);

#endif
