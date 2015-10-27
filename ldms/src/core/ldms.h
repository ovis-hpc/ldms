/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010-2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2010-2015 Sandia Corporation. All rights reserved.
 *
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
#ifndef __LDMS_H__
#define __LDMS_H__
#include <limits.h>
#include <inttypes.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <string.h>
#include <netinet/in.h>
#include <byteswap.h>
#include <asm/byteorder.h>
#include "ovis-lib-config.h"
#include "ldms_core.h"
#include "coll/rbt.h"
#include "ovis_util/os_util.h"


#ifdef __cplusplus
extern "C" {
#endif
#if 0
}
#endif
typedef struct ldms_xprt *ldms_t;
typedef struct ldms_rbuf_desc *ldms_rbuf_t;
typedef struct ldms_set_desc *ldms_set_t;
typedef struct ldms_value_s *ldms_value_t;
typedef struct ldms_schema_s *ldms_schema_t;

/**
 * \mainpage LDMS
 *
 * LDMS means Lightweight Distributed Metric Service. It is an
 * Application Programming Interface for publishing and gathering
 * system metrics in a clustered environment. A metric set provider
 * publishes metric sets to remote peers. The remote peers update
 * their local copies of these metrics whenever they choose, i.e. the
 * metric set published does not push the data, it is pulled by the
 * client. Only the metric set publisher can change the contents of
 * the metric set. Although the client can change its local copy, the
 * data will be overwritten the next time the set is updated.
 *
 * LDMS supports multiple network transports. The desired transport is
 * elected when the transport handle is created. If the RDMA transport
 * is available on your platform, then updating the contents of the
 * metric set is done by the hardware via RDMA_READ and the metric set
 * publisher's CPU is unaware and uninvolved with the
 * transfer; i.e. with RDMA the metric set updates have zero
 * CPU overhead.
 *
 * A \b Metric \b Set is a named collection of \b Metric values. A
 * Metric is a named and typed value. The contents of a metric set are
 * updated as a single entity, i.e. it is not possible to fetch just
 * one value from the server.
 *
 * Metric sets have a generation number that allows the client to
 * determine if any data in the metric set has changed since the last
 * time it was updated. This is useful because the data is pulled by
 * the client, not pushed by the producer.
 *
 * \section conn_mgmt Connection Management
 *
 * The LDMS connection management model is similar to the tranditional
 * sockets API. Peers are addressed with a struct sockaddr, servers \c
 * listen for incoming connection requests and clients \c connect to servers.
 *
 * The connection management API consists of the following functions:
 *
 * \li \b ldms_xprt_create() Create a transport instance.
 * \li \b ldms_xprt_listen() Create a listening endpoint and respond to
 * queries from peers.
 * \li \b ldms_xprt_connect() Request a connection with a remote peer.
 * \li \b ldms_xprt_close() Close a connection with a remote peer.
 *
 * \section schema Defining Metric Set Schema
 * A Schema defines the metrics that comprise a metric set.  A metric
 * represents a value in the set that changes over time. It's most
 * recent value is retrieved with the ldms_xprt_update() function. A
 * meta-metric is a named value that is part of the meta-data of the
 * set and does not typically change. When it does change, it will
 * trigger a fetch of both the meta-data and the data portions of a
 * set when ldms_xprt_update() is called.
 *
 * Once a Schema is defined, it can be used to create one or more metric sets
 * of the same type. The principle functions for managing schema are the
 * following:
 *
 * \li \b ldms_schema_new() Create a Schema
 * \li \b ldms_schema_delete() Destroy a Schema
 * \li \b ldms_schema_metric_add() Add a metric to the Schema
 * \li \b ldms_schema_meta_add() Add a meta-metric to the Schema
 * \li \b ldms_schema_card_get() Return the number of Metrics in the Schema
 *
 * \section metric_sets Creating Metric Sets
 *
 * Metric Sets are created with a schema and updated in local memory. The
 * principle functions for creating and destroying local metric sets are the
 * following:
 *
 * \li \b ldms_set_new() Create a new metric set from a Scheam
 * \li \b ldms_set_delete() Destroy a metric set.
 *
 * \section query Querying Metric Sets
 *
 * A Metric Set consumer uses these function to query the server for
 * the publisehd Metric Sets:
 *
 * \li \b ldms_xprt_dir() Return a list of published metric set names.''
 * \li \b ldms_xprt_lookup() Lookup and gather the detail of a particular
 * metric set.
 * \li \b ldms_xprt_update() Update the contents of a previously looked-up
 * metric set.
 *
 * \section metrics Setting and Getting Metric Values.
 *
 * A Metric Set producer sets the values of the metrics after creating
 * the metric set and periodically thereafter as required. The client
 * updates the metric set and gets the values of the metrics.
 * The functions for doing this are as follows:
 *
 * \li \b ldms_metric_by_name() Find the index for a metric
 * \li \b ldms_metric_set() Set the value of a metric.
 * \li \b ldms_metric_get_X() Get the value of a metric where the X
 * specifies the data type
 * \li \b ldms_metric_set_S() Set the value of a metric where the X
 * specifies the data type
 *
 * \section notification Push Notifications
 *
 * Nominally, LDMS is a pull oriented transport, i.e. the Metric Set
 * consumer calls a function to pull the data from the provider and
 * update it's local copy of the data. There is, however, a way for
 * the producer to initiate communication using Notifications.
 *
 * \li \b ldms_register_notify_cb() Register a callback function to be
 * called when Notification Events are received for a particular
 * Metric Set.
 * \li \b ldms_cancel_notify() Stop receiving notification events for
 * a metric set.
 * \li \b ldms_notify() Send a notification event to all consumers
 * registered to receive events on a metric set.
 */

/**
 * \brief Initialize LDMS
 *
 *  Pre-allocate a memory region for metric sets
 *  \param max_size The maximum size of the pre-allocated memory
 *  \return 0 on success
 */
int ldms_init(size_t max_size);

/**
 * \brief Take a reference on a transport
 *
 * \param x	The transport handle
 * \returns	The transport handle
 */
ldms_t ldms_xprt_get(ldms_t x);
void ldms_xprt_put(ldms_t x);

/**
 * \brief Get the LDMS library version
 *
 * \param Pointer to an ldms_version structure
 */
void ldms_version_get(struct ldms_version *v);

/**
 * \brief Get the LDMS library version
 *
 * Compare the LDMS library version to the provided version.
 *
 * \param Pointer to an ldms_version structure
 * \retval !0 The versions match
 * \retval 0 The versions do not match
 */
int ldms_version_check(struct ldms_version *v);

/**
 * \brief Find a transport that matches the specified address.
 *
 * \param sin	Specifies the address that should match the remote peer's ip address.
 * \returns	The matching transport endpoint or NULL if no match was found.
 */
ldms_t ldms_xprt_by_remote_sin(struct sockaddr_in *sin);

/**
 * \brief Returns the 'first' transport endpoint
 *
 * The ldms_xprt_first() and ldms_xprt_next() functions are used to iterate
 * among all transport endpoints in the system. The ldms_xprt_first() function
 * returns the first endpoint and takes a reference on the handle. This reference
 * should be released by calling ldms_release_xprt() when the caller has finished
 * with the endpoint.
 *
 * \returns The first transport endpoint or NULL if there are no open transports.
 */
ldms_t ldms_xprt_first();

/**
 * \brief Returns the 'next' transport endpoint
 *
 * The ldms_xprt_first() and ldms_xprt_next() functions are used to iterate
 * among all transport endpoints in the system. The ldms_xprt_first() function
 * returns the first endpoint and takes a reference on the handle. This reference
 * should be released by calling ldms_release_xprt() when the caller has finished
 * with the endpoint.
 *
 * \returns The first transport endpoint or NULL if there are no open transports.
 */
ldms_t ldms_xprt_next(ldms_t);

enum ldms_lookup_status {
	LDMS_LOOKUP_ERROR = 1,
	LDMS_LOOKUP_OK = 0,
	LDMS_LOOKUP_NOTIFY = 1,
};

/**
 * \brief Prototype for the function called when lookup completes.
 *
 * This function is called when the lookup completes.
 *
 * \param t	 The transport endpoint.
 * \param status LDMS_LOOKUP_0 if the lookup was successful, ENOENT if the
 *		 specified set does not exist, ENOMEM if there is insufficient
 *		 memory to instantiate the set locally.
 * \param more   If the more parameter is non-zero, additional lookup
 *               results are outstanding for the request.
 * \param s	 The metric set handle.
 * \param cb_arg The callback argument specified in the call to \c ldms_lookup.
 */
typedef void (*ldms_lookup_cb_t)(ldms_t t, enum ldms_lookup_status status,
				 int more, ldms_set_t s, void *arg);

/*
 * Values for the flags field in the ldms_set structure below.
 */
#define LDMS_SET_F_MEMMAP	0x0001
#define LDMS_SET_F_FILEMAP	0x0002
#define LDMS_SET_F_LOCAL	0x0004
#define LDMS_SET_F_REMOTE	0x0008
#define LDMS_SET_ID_DATA	0x1000000

struct ldms_set;
struct ldms_set_desc {
	struct ldms_rbuf_desc *rbd;
	struct ldms_set *set;
};

/**
 * \addtogroup ldms_conn_mgmt LDMS Connection Management
 *
 * These functions initiate, terminate and manage connections with
 * LDMS peers.
 * \{
 */

/**
 * LDMS log function definition.
 *
 * Users can implememnt customized log function using this API.
 *
 * \param fmt The format of the printing string (as in \c printf).
 */
typedef void (*ldms_log_fn_t)(const char *fmt, ...);

/**
 * \brief Create a transport handle
 *
 * Metric sets are exported on the network through a transport. A
 * transport handle is required to communicate on the network.
 *
 * \param name	The name of the transport type to create without authentication.
 * \param log_fn An optional function to call when logging transport messages
 *
 * \returns	A transport handle on success.
 * \returns	0 If the transport could not be created.
 */
extern ldms_t ldms_xprt_new(const char *name, ldms_log_fn_t log_fn);

#if OVIS_LIB_HAVE_AUTH
/** The same env variable applies to all or confused inconsistency results. */
#define LDMS_AUTH_ENV "LDMS_AUTH_FILE"
/**
 * \brief Create a transport handle always with authentication
 *
 * Metric sets are exported on the network through a transport. A
 * transport handle is required to communicate on the network.
 *
 * \param name	The name of the transport type to create.
 * \param log_fn An optional function to call when logging transport messages
 * \param secretword  The shared secret word used for authentication.
 *                    If NULL is given, the call fails.
 *
 * \returns	A transport handle on success.
 * \returns	0 If the transport could not be created.
 */
extern ldms_t ldms_xprt_with_auth_new(const char *name, ldms_log_fn_t log_fn,
					const char *secretword);
/**
 * \brief Find the secretword used for encrypting the key exchange.
 *
 * All callers connecting to ldmsd via any network socket use
 * this to get their secretword needed for ldms_xprt_with_auth_new.
 * The source for the secret is as follows:
 *   The file, if given, first.
 *   The env var LDMS_AUTH_ENV, if set.
 *   ~/.ldmsauth.conf, if present.
 *   sysconfdir/ldmsauth.conf, if present.
 * First source given or present, but erroneous, results in a failure.
 * \param file The user-supplied name of a file to check.
 * \param log_fn Output sink for error messages.
 * \return NULL on failure (errno set) or the secret. Caller owns result.
 */
extern char *ldms_get_secretword(const char * file, ldms_log_fn_t log_fn);
#endif /* OVIS_LIB_HAVE_AUTH */

typedef enum ldms_conn_event {
	LDMS_CONN_EVENT_CONNECTED,
	LDMS_CONN_EVENT_REJECTED,
	LDMS_CONN_EVENT_ERROR,
	LDMS_CONN_EVENT_DISCONNECTED,
	LDMS_CONN_EVENT_LAST
} ldms_conn_event_t;

/**
 * Definition of callback function for ldms_xprt_connect.
 *
 * The caller that requests a connection will be notified through a
 * callback function if the connection is successful. The event
 * parameter <tt>e</tt> parameter indicates success or failure as
 * follows:
 * - LDMS_CONN_EVENT_CONNECTED The transport is now connected, or
 * - LDMS_CONN_EVENT_ERROR The connection attempt failed
 *
 * \param x The ldms transport handle
 * \param e The connection event
 * \param cb_arg The \c cb_arg specified when ::ldms_xprt_connect() was called
 */
typedef void (*ldms_connect_cb_t)(ldms_t x, ldms_conn_event_t e, void *cb_arg);

/**
 * \brief Request a connection to an LDMS host.
 *
 * Connect to the remote peer specified by it's host
 * name and service address (port number). If the <tt>cb</tt> function
 * is not NULL, the function will return immediately and call the
 * <tt>cb</tt> function when the connection completes with the provided
 * cb_arg as an argument. See the ldms_connect_cb_t() function for more
 * details.
 *
 * If <tt>cb</tt> is NULL, the function waits until the connection
 * completes before returning and the returned value indicates the
 * success or failure of the connection.
 *
 * \param x	The transport handle
 * \param sa	Socket address specifying the host address and port.
 * \param sa_len The length of the socket address.
 * \param cb	The callback function.
 * \param cb_arg An argument to be passed to \c cb when it is called.
 * \retval	0 if the request is posted successfully. Please note that this
 *		doesn't mean that the transport is connected.
 * \retval	An error indicating why the request failed.
 */
extern int ldms_xprt_connect(ldms_t x, struct sockaddr *sa, socklen_t sa_len,
			     ldms_connect_cb_t cb, void *cb_arg);

/**
 * \brief Connect to a hostname and service
 *
 * Connect to the remote peer specified by it's host
 * name and service address (port number). If the <tt>cb</tt> function
 * is not NULL, the function will return immediately and call the
 * <tt>cb</tt> function when the connection completes with the provided
 * cb_arg as an argument. See the ldms_connect_cb_t() function for more
 * details.
 *
 * If <tt>cb</tt> is NULL, the function waits until the connection
 * completes before returning and the returned value indicates the
 * success or failure of the connection.
 *
 * \param x	The transport handle
 * \param host  The hostname
 * \param port	The port number (service) as a string
 * \param cb    The function to call when the connection completes.
 * \param cb_arg A user-supplied argument to pass to the callback function.
 *
 * \retval 0            The connection succeeded
 * \retval ENETRESET    The peer responsed with a reset (no listener)
 * \retval EHOSTUNREACH No route to host
 */
int ldms_xprt_connect_by_name(ldms_t x, const char *host, const char *port,
			      ldms_connect_cb_t cb, void *cb_arg);
/**
 * \brief Listen for connection requests from LDMS peers.
 *
 * \param x	The transport handle
 * \param sa	Socket address specifying the host address and port.
 * \param sa_len The length of the socket address.
 * \returns	0 if a listening endpoint was successfully created.
 * \returns	An error indicating why the listen failed.
 */
extern int ldms_xprt_listen(ldms_t x, struct sockaddr *sa, socklen_t sa_len);
extern int ldms_xprt_listen_by_name(ldms_t x, const char *host, const char *port);

/**
 * \brief Close a connection to an LDMS host.
 *
 * The function will disconnect the connection and free the allocated memory
 * for the transport \c x.
 *
 * \param x	The transport handle
 *
 * \see ldms_xprt_delete
 */
extern void ldms_xprt_close(ldms_t x);

/** \} */

/**
 * \addtogroup ldms_query LDMS Query Functions
 *
 * These functions query LDMS peers for published metric sets.
 * \{
 */

/**
 * \brief ldms_dir callback function
 *
 * This function is called in response to a call to ldms_xprt_dir()
 * and, if requested, thereafter whenever there is an update to the
 * set directory.
 *
 * The application should call \c ldms_xprt_dir_free() when it is finished
 * processing the directory to free the associated resources.
 *
 * There are three different types of directory updates:
 * LDMS_DIR_LIST, LDMS_DIR_ADD, and LDMS_DIR_REPLY. An LDMS_DIR_LIST
 * contains a list of all sets published by the server.LDMS_DIR_ADD
 * contains a list of newly added sets since the callback was last
 * called, and LDMS_DIR_REM contains a list of those sets removed. The
 * user can expect a LDMS_DIR_LIST directory when the callback is
 * first called, followed by any number of LDMS_DIR_ADD and
 * LDMS_DIR_REM directory types. See the ldms_dir_s structure for more
 * information.
 *
 * \param x	 The transport handle
 * \param cb_arg The callback argument specified in the call to \c ldms_dir
 * \param status If zero, the query was successful, ENOMEM indicates
 *		 that there was insufficient memory available to build the
 *		 directory.
 * \param dir	 Pointer to an ldms_dir_s structure on success, NULL
 *		 otherwise.
 *
 */

/**
 * \brief The directory update type
 */
enum ldms_dir_type {
	LDMS_DIR_LIST,		/*! A complete list of available metric sets */
	LDMS_DIR_DEL,		/*! The listed metric sets have been deleted */
	LDMS_DIR_ADD		/*! The listed metric sets have been added */
};

/**
 * \brief The format of the directory data returned by
 * \c ldms_dir request.
 */
typedef struct ldms_dir_s {
	/** the type of update */
	enum ldms_dir_type type;

	/** !0 if this is the first of multiple updates */
	int more;

	/** count of sets in the set_name array */
	int set_count;

	/** each string is null terminated. */
	char *set_names[0];
} *ldms_dir_t;

typedef void (*ldms_dir_cb_t)(ldms_t t, int status, ldms_dir_t dir, void *cb_arg);

/**
 * \brief Free the resources consumed by a directory
 *
 * This function is called by the application to release the resources
 * used by an ldms_dir_t.
 *
 * \param t	 The transport handle
 * \param dir	 Pointer to an ldms_dir_s structure to be released.
 */
void ldms_xprt_dir_free(ldms_t t, ldms_dir_t dir);

/**
 * \brief Cancel LDMS directory updates
 *
 * This function cancels updates to the LDMS directory initiated by a
 * call to ldms_xprt_dir().
 *
 * \param t	The transport handle
 *
 * \returns	0 if the query was submitted successfully
 */
int ldms_xprt_dir_cancel(ldms_t t);

/**
 * \brief Query the sets published by a host.
 *
 * This function queries the peer for the set of published metric sets.
 *
 * If the <tt>cb</tt> function is not NULL, the function will return
 * immediately and call the <tt>cb</tt> function when the connection
 * completes with the provided cb_arg as an argument.  If the
 * <tt>LDMS_DIR_F_NOTIFY</tt> flag is specified, the callback function
 * will be invoked whenever the peer updates its set of published
 * metrics. See the ldms_connect_cb_t() function for more details.
 *
 * If <tt>cb</tt> is NULL, the function waits until the query
 * completes before returning and the returned value indicates the
 * success or failure of the query. Note that the <tt>flags</tt>
 * parameter is ignored if the <tt>cb</tt> parameter is NULL.
 *
 * See the ldms_xprt_dir_cancel() function to cancel directory updates.
 *
 * \param x	 The transport handle
 * \param cb	 The callback function to invoke when the directory is
 *		 returned by the peer.
 * \param cb_arg A user context that will be provided as a parameter
 *		 to the \c cb function.
 * \param flags	 If set to LDMS_DIR_F_NOTIFY, the specified callback
 *		 function will be invoked whenever the directory changes on the
 *		 peer.
 * \returns	0 if the query was submitted successfully
 */
#define LDMS_DIR_F_NOTIFY	1
extern int ldms_xprt_dir(ldms_t x, ldms_dir_cb_t cb, void *cb_arg, uint32_t flags);

#define LDMS_XPRT_LIBPATH_DEFAULT PLUGINDIR
#define LDMS_DEFAULT_PORT	LDMSDPORT
#define LDMS_LOOKUP_PATH_MAX	511


/**
 * \brief Query the contents of a metric set.
 *
 * This function queries the peer for the detail of the metric set \c
 * path. If the query is successful, the function puts the set handle
 * in the pointer provided by the \c s parameter.
 *
 * If the <tt>cb</tt> function is not NULL, the function will return
 * immediately and call the <tt>cb</tt> function when the lookup
 * completes. See the ldms_lookup_cb_t() function for more details.
 *
 * If <tt>cb</tt> is NULL, the function waits until the lookup
 * completes before returning and the returned value indicates the
 * success or failure of the lookup.
 *
 * The <tt>flags</tt> parameter specifies if the <tt>name</tt> is a
 * regular expression. If <tt>name</tt> is an RE, then the <tt>cb</tt> function
 * will be called for every matching metric set on the peer. The
 * <tt>cb</tt> parameter cannot be NULL if LDMS_LOOKUP_RE is set in
 * <tt>flags.</tt>
 *
 * <tt>flags</tt> is a combination of the following values:
 * - LDMS_LOOKUP_RE The name parameter is a regular expression
 * - LDMS_LOOKUP_BY_INSTANCE The <tt>name</tt> refers to the set instance
 * - LDMS_LOOKUP_BY_SCHEMA The <tt>name</tt> refers to the set schema
 *
 * See the ldms_xprt_dir() function for detail on how to query a host for
 * the list of published metric sets.
 *
 * \param t	 The transport handle
 * \param name   The name to look up. The name refers to either the schema or instance name based on the value of <tt>flags</tt>
 * \param flags  The lookup options
 * \param cb	 The callback function to invoke when the lookup has
 *		 completed.
 * \param cb_arg A user context that will be provided as a parameter
 *		 to the \c cb function.
 * \returns	 0 if the query was submitted successfully.
 */
enum ldms_lookup_flags {
	LDMS_LOOKUP_BY_INSTANCE = 0,
	LDMS_LOOKUP_BY_SCHEMA = 1,
	LDMS_LOOKUP_RE = 2,
};
extern int ldms_xprt_lookup(ldms_t t, const char *name, enum ldms_lookup_flags flags,
		       ldms_lookup_cb_t cb, void *cb_arg);

/** \} */

/**
 * \addtogroup ldms_set LDMS Metric Set Management
 *
 * These functions are used to create and destroy local metric sets
 * and to update the contents of remote metric sets.
 * \{
 */

/**
 * \brief Prototype for the function called when update completes.
 *
 * You cannot call back into the transport from this function or it will
 * deadlock.
 *
 * \param t	The transport endpoint.
 * \param s	The metric set handle updated.
 * \param rc	0 if the update was successful, or an error value.
 * \param arg	The callback argument specified in the call to \c ldms_update.
 */
typedef void (*ldms_update_cb_t)(ldms_t t, ldms_set_t s, int status, void *arg);

/**
 * \brief Update the metric set contents.
 *
 * Updates the local copy of the metric set.
 *
 * \param s	The metric set handle to update.
 * \param update_cb The function to call when the update has completed
 *		    and the metric data has been updated.
 * \param cb_arg A user defined context value to provide to the update_cb function.
 * \returns	0 on success or a non-zero value to indicate an error.
 */
extern int ldms_xprt_update(ldms_set_t s, ldms_update_cb_t update_cb, void *arg);

/**
 * \brief Create a metric set schema
 *
 * Create a metric set schema. The schema can later be used to create
 * a metric set. The schema name must be unique.
 *
 * \param name	The set schema name
 * \retval !0 The schema handle.
 * \retval ENOMEM There were insufficient resources to create the schema
 */
extern ldms_schema_t ldms_schema_new(const char *schema_name);
extern void ldms_schema_delete(ldms_schema_t schema);

/**
 * \brief Return the number of metrics in the schema
 *
 * \param schema
 * \returns The number of metrics in the schema
 */
extern int ldms_schema_metric_count_get(ldms_schema_t schema);

/**
 * \brief Create a Metric set
 *
 * Create a metric set on the local host. The metric set is added to
 * the data base of metric sets exported by this host. The <tt>instance_name</tt>
 * parameter specifies the name of the metric set as it will appear to
 * hosts who query the set dictionary with ldms_dir().
 *
 * Multiple metric sets of the same type (schema) may be created
 * provided that they have different instance names.
 *
 * \param instance_name	The metric set instance name.
 * \param schema	The metric set schema being created.
 * \param s		Pointer to ldms_set_t handle that will be set to the new handle.
 * \returns Pointer to the new metric set or NULL if there is an error. Errno will be set
 * as appropriate as follows:
 * - ENOMEM Insufficient resources
 * - EEXIST The specified instance name is already used.
 * - EINVAL A parameter or the schema itself is invalid
 */
extern ldms_set_t ldms_set_new(const char *instance_name, ldms_schema_t schema);

/**
 * \brief Delete the set reference
 *
 * Delete the set reference. The set will be deleted when all set references
 * are released.
 *
 * \param s	The metric set handle.
 */
extern void ldms_set_delete(ldms_set_t s);

/**
 * \brief Get the schema name for the set
 *
 * \param s	The set handle
 * \retval !0	Pointer to a string containing the schema name
 * \retval 0	The set handle invalid
 */
extern const char *ldms_set_schema_name_get(ldms_set_t s);

/**
 * \brief Get the instance name for the set
 *
 * \param s	The set handle
 * \retval !0	Pointer to a string containing the instance name
 * \retval 0	The set handle invalid
 */
extern const char *ldms_set_instance_name_get(ldms_set_t s);

/**
 * \brief Get the producer name for the set
 *
 * \param s	The set handle
 * \returns	The producer name for the set.
 */
extern const char *ldms_set_producer_name_get(ldms_set_t s);

/**
 * \brief Set the producer name for the set
 *
 * \param s	The set handle
 * \param id	The producer name for the set.
 *
 * \returns	0 on success
 * \returns	EINVAL if the given name including
 *		the terminating null byte is longer
 *		than the LDMS_PRODUCER_NAME_MAX.
 */
extern int ldms_set_producer_name_set(ldms_set_t s, const char *name);

/**
 * \brief Map a metric set for remote access
 *
 * This service is used to map a local metric set for access on the
 * network. The <tt>addr</tt> parameter specifies the address of the memory
 * containing the metric set. This service is typically used to export
 * metric sets that are created in the kernel.
 *
 * \param meta_addr	Address of the metric set meta data
 * \param data_addr	Address of the metric set data
 * \param s		Pointer to memory to receive handle.
 * \returns 0		Success
 */
extern int ldms_mmap_set(void *meta_addr, void *data_addr, ldms_set_t *s);

/**
 * \brief Get the number of metrics in the set.
 *
 * \param s	The ldms_set_t handle.
 * \return The number of metrics in the set
 */
extern uint32_t ldms_set_card_get(ldms_set_t s);

/**
 * \brief Get the size in bytes of the set's meta data
 * \param s	The ldms_set_t handle.
 * \return The size of the meta-data in bytes
 */
extern uint32_t ldms_set_meta_sz_get(ldms_set_t s);

/**
 * \brief Get the size in bytes of the set's data
 * \param s	The ldms_set_t handle.
 * \return The size of the set's data in bytes.
 */
extern uint32_t ldms_set_data_sz_get(ldms_set_t s);

/**
 * \brief Get a set by name.
 *
 * Find a local metric set by name. A local set is one that is in
 * local memory either through ldms_xprt_lookup() or ldms_set_new().
 *
 * \param set_name	The set name.
 * \returns		The ldms_set_t handle or 0 if not found.
 */
extern ldms_set_t ldms_set_by_name(const char *set_name);

/**
 * \brief Get the metric schema generation number.
 *
 * A metric set has a \c generation number that chnages when metrics
 * are added or removed from the metric set.
 *
 * \param s	The ldms_set_t handle.
 * \returns	The 64bit meta data generation number.
 */
uint64_t ldms_set_meta_gn_get(ldms_set_t s);

/**
 * \brief Get the metric data generation number.
 *
 * A metric set has a \c generation number that chnages when metric
 * values are modified.
 *
 * \param s	The ldms_set_t handle.
 * \returns	The 64bit data generation number.
 */
uint64_t ldms_set_data_gn_get(ldms_set_t s);
/** \} */

/**
 * \addtogroup ldms_metric LDMS Metric Manaegment
 *
 * These functions are used to create and destroy local metrics
 * and to get and set the value of a metric.
 * \{
 */

/**
 * \brief Begin an LDMS transaction
 *
 * Start a metric set update transaction. This marks the set as
 * inconsistent. A remote peer that fetches a metric set can use the
 * ldms_set_is_consistent() function to determine if the metric was in
 * the process of being updated when it was fetched.
 *
 * \param s     The ldms_set_t handle.
 * \returns 0   If the transaction was started.
 * \returns !0  If the specified metric set is invalid.
 */
extern int ldms_transaction_begin(ldms_set_t s);

/**
 * \brief Complete an LDMS transaction
 *
 * Marks the metric set as consistent and time-stamps the data.
 *
 * \param s     The ldms_set_t handle.
 * \returns 0   If the transaction was started.
 * \returns !0  If the specified metric set is invalid.
 */
extern int ldms_transaction_end(ldms_set_t s);

/**
 * \brief Get the time the transaction ended
 *
 * Returns an ldms_timestamp structure that specifies when
 * ldms_transaction_end() was last called by the metric set provider. If
 * the metric set provider does not update it's metric sets inside
 * transactions, then this value is invalid. This value is undefined
 * if the metric set is not consistent, see ldms_set_is_consistent().
 *
 * \param s     The metric set handle
 * \returns ts  A pointer to a timestamp structure.
 */
extern struct ldms_timestamp ldms_transaction_timestamp_get(ldms_set_t s);

/**
 * \brief Get the duration of the last transaction
 *
 * Returns an ldms_timestamp structure that specifies the time between
 * ldms_transaction_begin() and ldms_transaction_end(). This
 * measures how long the sampler took to update the metric set.
 *
 * \param s     The metric set handle
 * \returns ts  A pointer to a timestamp structure.
 */
extern struct ldms_timestamp ldms_transaction_duration_get(ldms_set_t s);

/**
 * \brief Returns TRUE if the metric set is consistent.
 *
 * A metric set is consistent if it is not in the middle of being
 * updated. This is indicated by the metric set provider if they are
 * using transaction boundaries on metric set updates: see \c
 * ldms_begin_transaction and \c ldms_end_transaction. Using
 * transactions to update metric sets is computatationaly inexpensive,
 * but optional.
 */
extern int ldms_set_is_consistent(ldms_set_t s);

/**
 * \brief Add a metric to schema
 *
 * Adds a metric to a metric set schema. The \c name of the metric must be
 * unique within the metric set.
 *
 * \param s	The ldms_set_t handle.
 * \param name	The name of the metric.
 * \param t	The type of the metric.
 * \retval >=0  The metric index.
 * \retval <0	Insufficient resources or duplicate name
 */
extern int ldms_schema_metric_add(ldms_schema_t s, const char *name, enum ldms_value_type t);

/**
 * \brief Add an attribute to schema
 *
 * Adds a meta-metric to a set schema. The \c name of the meta-metric must be
 * unique within the set.
 *
 * \param s	The ldms_set_t handle.
 * \param name	The name of the attribute.
 * \param t	The type of the attribute.
 * \retval >=0  The attribute index.
 * \retval <0	Insufficient resources or duplicate name
 */
extern int ldms_schema_meta_add(ldms_schema_t s,
				const char *name, enum ldms_value_type t);

/**
 * \brief Add an array metric to schema
 *
 * Adds a metric of an array type to a metric set schema.
 * The \c name of the metric must be
 * unique within the metric set.
 *
 * \param s	The ldms_set_t handle.
 * \param name	The name of the metric.
 * \param t	The type of the metric.
 * \param count The number of elements in the array
 * \retval >=0  The metric index.
 * \retval <0	Insufficient resources or duplicate name
 */
extern int ldms_schema_metric_array_add(ldms_schema_t s, const char *name,
		enum ldms_value_type t, uint32_t count);
extern int ldms_schema_meta_array_add(ldms_schema_t s, const char *name,
		enum ldms_value_type t, uint32_t count);

/**
 * \brief Get the metric index given a name
 *
 * Returns the metric index  for the metric with the specified
 * name. This index can then be used with the ldms_metric_get_type() functions
 * to return the value of the metric.
 *
 * \param s	The metric set handle
 * \param name	The name of the metric.
 * \returns	The metric set handle or 0 if there is none was found.
 */
extern int ldms_metric_by_name(ldms_set_t s, const char *name);

/**
 * \brief Returns the name of a metric.
 *
 * Returns the name of the metric specified by the handle.
 *
 * \param s	The set handle
 * \param i	The metric index
 * \returns	A character string containing the name of the metric.
 */
extern const char *ldms_metric_name_get(ldms_set_t s, int i);

/**
 * \brief Returns !0 (true) if the type is an array
 *
 * \param t	The metric type
 * \returns	TRUE(!0) if the type is an array
 */
int ldms_type_is_array(enum ldms_value_type t);

/**
 * \brief Returns the type of a metric.
 *
 * Returns the type of the metric specified by the handle.
 *
 * \param s	The set handle
 * \param i	The metric index
 * \returns	The ldms_value_type for the metric.
 */
extern enum ldms_value_type ldms_metric_type_get(ldms_set_t s, int i);
/**
 * \brief Return the metric's flags
 *
 * The metric flags specify how the metric data is
 * stored. LDMS_MDESC_F_DATA is stored in the data section and is
 * retrieved whenever ldms_xprt_update() is called. LDMS_MDESC_F_META
 * metric's data is stored in the meta-data section and is only
 * retrieved when it is modified.
 *
 * \param s The metric set handle
 * \param i The metric id
 * \returns The metric's flags
 */
int ldms_metric_flags_get(ldms_set_t s, int i);

/**
 * \brief Get a metric type as a string.
 *
 * Returns a string representing the data type.
 *
 * \param t	The metric value type.
 * \returns	A character string representing the metric value type.
 */
extern const char *ldms_metric_type_to_str(enum ldms_value_type t);

/**
 * \brief Convert a string to an ldms_value_type
 *
 * \param name	Character string representing the type name.
 * \returns	The associated ldms_value_type.
 */
extern enum ldms_value_type ldms_metric_str_to_type(const char *name);

/**
 * \brief Set the user-data associated with a metric
 *
 * Sets the user-defined meta data associated with a metric.
 *
 * \param s     The set handle.
 * \param i	The metric index
 * \param u     An 8B user-data value.
 */
void ldms_metric_user_data_set(ldms_set_t s, int i, uint64_t u);

/**
 * \brief Get the user-data associated with a metric
 *
 * Returns the user-defined metric meta-data set with
 * \c ldms_set_user_data.
 *
 * \param s     The set handle
 * \param i	The metric index
 * \returns u   The user-defined metric meta data value.
 */
uint64_t ldms_metric_user_data_get(ldms_set_t s, int i);

/**
 * \brief Set the value of a metric.
 *
 * Set the specified metric \c m to the value contained in the union
 * \c v. The value type in the union must match the value type used to
 * create the metric. See the ldms_get_metric_type() for information
 * on how to discover the type of a metric.
 *
 * \param s	The set handle.
 * \param i	The metric index
 * \param v	An ldms_value union specifying the value.
 */
void ldms_metric_set(ldms_set_t s, int i, ldms_mval_t v);

/**
 * \brief Mark a metric set as modified
 *
 * If the application wishes to modify the metric value directly, it
 * must indicate to the containing set that a change has been
 * made. This allows a peer using ldms_xprt_update() to recognize that
 * a set's contents have changed. If the metric is a meta-metric, the
 * meta-data generation number is updated. If the metric is a
 * data-metric, the data generation number is updated.
 *
 * \param s The metric set handle
 * \param i The metric index
 */
void ldms_metric_modify(ldms_set_t s, int i);

/**
 * \brief Set multiple values of an array
 *
 * \param s		The set handle.
 * \param metric_idx	The metric index
 * \param v		An ldms_value union specifying the value.
 * \param start		The first index in the array
 * \param count         The number of elements in the array to set
 */
void ldms_metric_array_set(ldms_set_t s, int metric_idx, ldms_mval_t v,
			   size_t start, size_t count);

/**
 * \brief Set the value of an element in the array metric
 *
 * \param s		The set handle.
 * \param metric_idx	The metric index
 * \param v		An ldms_value union specifying the value.
 */
void ldms_metric_array_set_val(ldms_set_t s,
			       int metric_idx, int array_idx, ldms_mval_t src);

/**
 * \brief Return the ldms_mval_t for the specified metric
 *
 * \note ldms expects the elements in the array to be little-endian.
 *
 * \param s The set handle.
 * \param i The metric Id.
 * \retval ldms_mval_t for the metric.
 */
ldms_mval_t ldms_metric_get(ldms_set_t s, int i);

/**
 * \brief Get the address of the array metric in ldms set \c s.
 *
 * \note ldms expects the elements in the array to be little-endian.
 * For per-element get/set please see ldms_aray_metric_get_*TYPE*()
 * and ldms_metric_array_set_*TYPE*() functions.
 *
 * \param s The set handle.
 * \param i The metric ID.
 * \retval ptr The pointer to the array in the set.
 */
ldms_mval_t ldms_metric_array_get(ldms_set_t s, int i);

/**
 * \brief Get length of the array metric.
 *
 * \param s The set handle.
 * \param i The metric ID.
 * \retval len The length (number of elements) of the array.
 */
uint32_t ldms_metric_array_get_len(ldms_set_t s, int i);

/**
 * \brief Set the value of a metric.
 *
 * Set the specified metric \c m to the value specified
 * by \c v.
 *
 * \param s	The set handle.
 * \param i	The metric index
 * \param v	The value.
 */
void ldms_metric_set_char(ldms_set_t s, int i, char v);
void ldms_metric_set_u8(ldms_set_t s, int i, uint8_t v);
void ldms_metric_set_u16(ldms_set_t s, int i, uint16_t v);
void ldms_metric_set_u32(ldms_set_t s, int i, uint32_t v);
void ldms_metric_set_u64(ldms_set_t s, int i, uint64_t v);
void ldms_metric_set_s8(ldms_set_t s, int i, int8_t v);
void ldms_metric_set_s16(ldms_set_t s, int i, int16_t v);
void ldms_metric_set_s32(ldms_set_t s, int i, int32_t v);
void ldms_metric_set_s64(ldms_set_t s, int i, int64_t v);
void ldms_metric_set_float(ldms_set_t s, int i, float v);
void ldms_metric_set_double(ldms_set_t s, int i, double v);

void ldms_metric_array_set_str(ldms_set_t s, int mid, const char *str);
void ldms_metric_array_set_char(ldms_set_t s, int mid, int idx, char v);
void ldms_metric_array_set_u8(ldms_set_t s, int mid, int idx, uint8_t v);
void ldms_metric_array_set_u16(ldms_set_t s, int mid, int idx, uint16_t v);
void ldms_metric_array_set_u32(ldms_set_t s, int mid, int idx, uint32_t v);
void ldms_metric_array_set_u64(ldms_set_t s, int mid, int idx, uint64_t v);
void ldms_metric_array_set_s8(ldms_set_t s, int mid, int idx, int8_t v);
void ldms_metric_array_set_s16(ldms_set_t s, int mid, int idx, int16_t v);
void ldms_metric_array_set_s32(ldms_set_t s, int mid, int idx, int32_t v);
void ldms_metric_array_set_s64(ldms_set_t s, int mid, int idx, int64_t v);
void ldms_metric_array_set_float(ldms_set_t s, int mid, int idx, float v);
void ldms_metric_array_set_double(ldms_set_t s, int mid, int idx, double v);

/**
 * \brief Get the value of a metric.
 *
 * Get the specified metric \c m
 *
 * \param s	The set handle.
 * \param i	The metric index
 * \returns	Unsigned byte value from the metric.
 */
char ldms_metric_get_char(ldms_set_t s, int i);
uint8_t ldms_metric_get_u8(ldms_set_t s, int i);
uint16_t ldms_metric_get_u16(ldms_set_t s, int i);
uint32_t ldms_metric_get_u32(ldms_set_t s, int i);
uint64_t ldms_metric_get_u64(ldms_set_t s, int i);
int8_t ldms_metric_get_s8(ldms_set_t s, int i);
int16_t ldms_metric_get_s16(ldms_set_t s, int i);
int32_t ldms_metric_get_s32(ldms_set_t s, int i);
int64_t ldms_metric_get_s64(ldms_set_t s, int i);
float ldms_metric_get_float(ldms_set_t s, int i);
double ldms_metric_get_double(ldms_set_t s, int i);

const char *ldms_metric_array_get_str(ldms_set_t s, int id);
char ldms_metric_array_get_char(ldms_set_t s, int id, int idx);
uint8_t ldms_metric_array_get_u8(ldms_set_t s, int id, int idx);
uint16_t ldms_metric_array_get_u16(ldms_set_t s, int id, int idx);
uint32_t ldms_metric_array_get_u32(ldms_set_t s, int id, int idx);
uint64_t ldms_metric_array_get_u64(ldms_set_t s, int id, int idx);
int8_t ldms_metric_array_get_s8(ldms_set_t s, int id, int idx);
int16_t ldms_metric_array_get_s16(ldms_set_t s, int id, int idx);
int32_t ldms_metric_array_get_s32(ldms_set_t s, int id, int idx);
int64_t ldms_metric_array_get_s64(ldms_set_t s, int id, int idx);
float ldms_metric_array_get_float(ldms_set_t s, int id, int idx);
double ldms_metric_array_get_double(ldms_set_t s, int id, int idx);
/** \} */

/**
 * \addtogroup notify LDMS Notifications
 *
 * These functions are used register for and deliver Notification
 * Events.
 * \{
 */

/**
 * \brief Notification event type
 *
 * The ldms_notify_event_t should be initialized using one of the
 * following functions:
 *
 * * ldms_init_notify_modified
 * * ldms_init_notify_user_data
 *
 */
typedef enum ldms_notify_event_type {
		LDMS_SET_MODIFIED = 1,
		LDMS_USER_DATA = 2,
} ldms_notify_event_type_t;
typedef struct ldms_notify_event_s {
	ldms_notify_event_type_t type;
	size_t len;		/*! The size of the event in bytes */
	unsigned char u_data[0];/*! User-data for the LDMS_USER_DATA
				  type */
} *ldms_notify_event_t;

/**
 * \brief Initialize a SET_MODIFIED notification
 *
 * \param e	Pointer to the event
 */
static inline void
ldms_init_notify_modified(ldms_notify_event_t e) {
	e->type = LDMS_SET_MODIFIED;
	e->len = sizeof(struct ldms_notify_event_s);
}

/**
 * \brief Initialize a USER_DATA notification
 *
 * \param s	The set handle
 * \param e	Pointer to the event
 * \param u_data Pointer to the user-data
 * \param sz	Length of the user-data in bytes
 */
static inline void
ldms_init_notify_user_data(ldms_notify_event_t e,
			   unsigned char *u_data, size_t sz)
{
	e->type = LDMS_USER_DATA;
	e->len = sizeof(struct ldms_notify_event_s) + sz;
	memcpy(e->u_data, u_data, sz);
}

/**
 * \brief Nofication callback handler function
 *
 * This is the function protototype for registered notification
 * handlers.
 *
 * \param x The transport endpoint.
 * \param s The metric set handle
 * \param e The notification event
 * \param arg The user-supplied argument provided when the callback
 *	      was registered.
 */
typedef void (*ldms_notify_cb_t)(ldms_t x, ldms_set_t s,
				 ldms_notify_event_t e, void *arg);

/**
 * \brief Register callback handler to receive update notifications
 *
 * If the metric set producer supports update notifications, the
 * registered callback handler is invoked when the producer calls the
 * \c ldms_notify service.
 *
 * \param x	The transport endpoint.
 * \param s	The metric set handle
 * \param flags	The events of interest. 0 means all events.
 * \param cb_fn	Pointer to the function to call to receive
 *		notifications
 * \param cb_arg User-supplied argument to the cb_fn function.
 *
 * \returns 0 on success
 * \returns !0 on failure. Refer to \c errno for error details
 */
int ldms_register_notify_cb(ldms_t x, ldms_set_t s, int flags,
			    ldms_notify_cb_t cb_fn, void *cb_arg);

/**
 * \brief Release the resources consumed by an event
 *
 * This function is called by the application to release the resources
 * used by an ldms_notify_event_t.
 *
 * \param t	 The transport handle
 * \param dir	 Pointer to an ldms_notify_event_t structure to be released.
 */
void ldms_event_release(ldms_t x, ldms_notify_event_t e);

/**
 * \brief Cancel notifications
 *
 * Cancel notifications for a metric set
 *
 * \param x	The transport endpoint.
 * \param s	The metric set handle
 * \returns 0	Success
 * \returns !0	An error was encountered. See errno for details.
 */
int ldms_cancel_notify(ldms_t x, ldms_set_t s);

/**
 * \brief Notify registered clients
 *
 * Notify clients that have registered callback handlers that the
 * metric set has been modified. Note that this will unconditionally
 * notify the clients whether or not a change has been made to the
 * metric set. The expected usage is:
 *
 *    ldms_set_metric(m0, ...);
 *    ldms_set_metric(m1, ...);
 *    ldms_set_metric(m2, ...);
 *    ...
 *    ldms_notify(s);
 *
 * \param s	The metric set handle.
 * \param e	The event
 */
void ldms_notify(ldms_set_t s, ldms_notify_event_t e);
/**
 * \}
 */

#ifdef __cplusplus
}
#endif

#endif
