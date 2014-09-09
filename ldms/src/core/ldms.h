/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2010 Sandia Corporation. All rights reserved.
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
#include <netinet/in.h>
#include <coll/rbt.h>
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif
#if 0
}
#endif
typedef void *ldms_t;

typedef void *ldms_rbuf_t;
typedef void *ldms_set_t;
typedef void *ldms_value_t;
typedef void *ldms_metric_t;

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
 * \li \b ldms_create_xprt() Create a transport instance.
 * \li \b ldms_listen() Create a listening endpoint and respond to
 * queries from peers.
 * \li \b ldms_connect() Request a connection with a remote peer.
 * \li \b ldms_xprt_close() Close a connection with a remote peer.
 *
 * \section metric_sets Creating Metric Sets
 *
 * A Metric Set is defined and maintained by a compute node
 * server. Metric Sets are created and updated in local memory. The
 * principle functions for creating and destroying local metric sets are the
 * following:
 *
 * \li \b ldms_create_set() Create a new metric set of the specified
 * size.
 * \li \b ldms_mmap_set() Use the memory specified as the contents of a
 * metric set.
 * \li \b ldms_destroy_set() Destroy a metric set.
 * \li \b ldms_add_metric() Add a metric to a metric set.
 *
 * \section query Querying Metric Sets
 *
 * A Metric Set consumer uses these function to query the server for
 * the publisehd Metric Sets:
 *
 * \li \b ldms_dir() Return a list of published metric set names.''
 * \li \b ldms_lookup() Lookup and gather the detail of a particular
 * metric set.
 * \li \b ldms_update() Update the contents of a previously looked-up
 * metric set.
 *
 * \section metrics Setting and Getting Metric Values.
 *
 * A Metric Set producer sets the values of the metrics after creating
 * the metric set and periodically thereafter as required. The client
 * updates the metric set and gets the values of the metrics as
 * required. The functions for doing this are as follows:
 *
 * \li \b ldms_get_metric() Get a metric handle.
 * \li \b ldms_set_metric() Set the value of a metric.
 * \li \b ldms_get_XXX() Get the value of a metric where the XXX
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
 * \brief Metric value descriptor
 *
 * This structure describes a metric value in the metric set. Metrics
 * are self describing.
 */
#pragma pack(4)
struct ldms_value_desc {
	uint64_t user_data;	/*! User defined meta-data */
	uint32_t next_offset;	/*! Offset of next descriptor */
	uint32_t data_offset;	/*! Offset of the value in ldms_data_hdr */
	uint32_t type;		/*! The type of the value, enum ldms_value_type */
	uint8_t name_len;	/*! The length of the metric name in bytes*/
	char name[FLEXIBLE_ARRAY_MEMBER];		/*! The metric name */
};
#pragma pack()

/**
 * \brief Metric value union
 *
 * A generic union that encapsulates all of the LDMS value types.
 */
union ldms_value {
	uint8_t v_u8;
	int8_t v_s8;
	uint16_t v_u16;
	int16_t v_s16;
	uint32_t v_u32;
	int32_t v_s32;
	uint64_t v_u64;
	int64_t v_s64;
	float v_f;
	double v_d;
	long double v_ld;
};

/**
 * \brief LDMS value type enumeration
 */
enum ldms_value_type {
	LDMS_V_NONE,
	LDMS_V_U8,
	LDMS_V_S8,
	LDMS_V_U16,
	LDMS_V_S16,
	LDMS_V_U32,
	LDMS_V_S32,
	LDMS_V_U64,
	LDMS_V_S64,
	LDMS_V_F,
	LDMS_V_D,
	LDMS_V_LD,
};

/**
 * \brief Metric value iterator
 */
struct ldms_iterator {
	struct ldms_set *set;
	struct ldms_value_desc *curr_desc;
	union ldms_value *curr_value;
	uint32_t curr_off;
};

/**
 * \brief Initialize LDMS
 *
 *  Pre-allocate a memory region for metric sets
 *  \param max_size The maximum size of the pre-allocated memory
 *  \return 0 on success
 */
int ldms_init(size_t max_size);

/**
 * \brief Description of this build.
 *
 *  Return a string describing this build suitable for humans.
 *  \return 0 on success
 */
const char * ldms_pedigree();

/**
 * \brief Take a reference on a transport
 *
 * \param x 	The transport handle
 * \returns	The transport handle
 */
ldms_t ldms_xprt_get(ldms_t x);

/**
 * \brief Find a transport that matches the specified address.
 *
 * \param sin 	Specifies the address that should match the remote peer's ip address.
 * \returns	The matching transport endpoint or NULL if no match was found.
 */
ldms_t ldms_xprt_find(struct sockaddr_in *sin);

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

/**
 * \brief Check if an endpoint is connected.
 *
 * \param l	The endpoint handle.
 * \returns	!0 if the endpoint is connected.
 */
int ldms_xprt_connected(ldms_t);

/**
 * \brief Check if an endpoint is authenticated.
 *
 * \param l	The endpoint handle.
 * \returns	!0 if the endpoint is authenticated.
 */
int ldms_xprt_authenticated(ldms_t);

/**
 * \brief Return value at iterator
 *
 * \param i	Pointer to the iterator.
 * \returns	Pointer to the ldms_value at the current position.
 */
static inline union ldms_value *ldms_iter_value(struct ldms_iterator *i)
{
	return i->curr_value;
}

/**
 * \brief Return description of value at the iterator
 *
 * \param i	Pointer to the iterator.
 * \returns	Pointer to the ldms_value_desc structure describing the metric value.
 */
static inline struct ldms_value_desc *ldms_iter_desc(struct ldms_iterator *i)
{
	return i->curr_desc;
}

#define LDMS_SETH_F_BE		0x0001
#define LDMS_SETH_F_LE		0x0002

#if __BYTE_ORDER__ == __BIG_ENDIAN
#define LDMS_SETH_F_LCLBYTEORDER	LDMS_SETH_F_BE
#else
#define LDMS_SETH_F_LCLBYTEORDER	LDMS_SETH_F_LE
#endif

#define LDMS_VERSION 0x02020000	/* 2.2.1.0 */
#define LDMS_SET_NAME_MAX 256
struct ldms_set_hdr {
	uint64_t meta_gn;	/* Meta-data generation number */
	uint32_t version;	/* LDMS version number */
	uint32_t flags;		/* Set format flags */
	uint32_t card;		/* Number of values in this set. */
	uint32_t meta_size;	/* size of meta data in bytes */
	uint32_t data_size;	/* size of metric values in bytes */
	uint64_t values;	/* Local handle for values */
	uint32_t head_off;	/* offset of first descriptor */
	uint32_t tail_off;	/* offset of last descriptor */
	char name[LDMS_SET_NAME_MAX];
};

enum ldms_transaction_flags {
	LDMS_TRANSACTION_NONE = 0,
	LDMS_TRANSACTION_BEGIN = 1,
	LDMS_TRANSACTION_END = 2
};

struct ldms_timestamp  {
	uint32_t sec;
	uint32_t usec;
};

struct ldms_transaction {
	struct ldms_timestamp ts;
	uint32_t flags;
};

struct ldms_data_hdr {
	struct ldms_transaction trans;
	uint32_t pad;
	uint64_t gn;		/* Metric-value generation number */
	uint64_t size;		/* Max size of data */
	uint64_t meta_gn;	/* Meta-data generation number */
	uint32_t head_off;	/* Offset of first value */
	uint32_t tail_off;	/* Offset of last value */
};

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
 * \param s	 The metric set handle.
 * \param cb_arg The callback argument specified in the call to \c ldms_lookup.
 */
typedef void (*ldms_lookup_cb_t)(ldms_t t, enum ldms_lookup_status status,
				 ldms_set_t s, void *arg);

/*
 * Values for the flags field in the ldms_set structure below.
 */
#define LDMS_SET_F_MEMMAP	0x0001
#define LDMS_SET_F_FILEMAP	0x0002
#define LDMS_SET_F_LOCAL	0x0004
#define LDMS_SET_F_REMOTE	0x0008
#define LDMS_SET_F_COHERENT	0x0010
#define LDMS_SET_F_DIRTY	0x1000
#define LDMS_SET_ID_DATA	0x1000000

struct ldms_set {
	unsigned long flags;
	struct ldms_set_hdr *meta;
	struct ldms_data_hdr *data;
	struct rbn rb_node;
	LIST_HEAD(rbd_list, ldms_rbuf_desc) rbd_list;
};

struct ldms_set_desc {
	struct ldms_rbuf_desc *rbd;
	struct ldms_set *set;
};

struct ldms_metric {
	struct ldms_set *set;
	struct ldms_value_desc *desc;
	union ldms_value *value;
};

struct ldms_mvec {
	int count;
	ldms_metric_t v[];
};

typedef struct ldms_mvec *ldms_mvec_t;

/**
 * \brief Create ::ldms_mvec.
 * \param count The number of metrics in the vector.
 * \return NULL on error.
 * \return A pointer to ::ldms_mvec on success.
 */
ldms_mvec_t ldms_mvec_create(int count);

/**
 * \brief Equivalent to \c free(mvec).
 * \param mvec The pointer to the ::ldms_mvec.
 */
void ldms_mvec_destroy(ldms_mvec_t mvec);

/**
 * \brief Get the number of metrics in \c mvec
 * \param mvec The pointer to the ::ldms_mvec_t.
 * \return The number of metrics in mvec
 */
static inline int ldms_mvec_get_count(ldms_mvec_t mvec)
{
	return mvec->count;
}

/**
 * \brief Get the array of the metrics in \c mvec
 * \param mvec The pointer to the ::ldms_mvec_t.
 * \return The array of the metrics in \c mvec.
 */
static inline ldms_metric_t *ldms_mvec_get_metrics(ldms_mvec_t mvec)
{
	return mvec->v;
}

/**
 * \addtogroup ldms_conn_mgmt LDMS Connection Management
 *
 * These functions initiate, terminate and manage connections with
 * LDMS peers.
 * \{
 */

/**
 * \brief Log levels
 */
enum {
	LDMS_LDEBUG = 0,
	LDMS_LINFO,
	LDMS_LERROR,
	LDMS_LCRITICAL,
	LDMS_LALWAYS,
};

int ldms_str_to_level(const char *level_s);

typedef void (*ldms_log_fn_t)(int level, const char *fmt, ...);

/**
 * \brief Create a transport handle
 *
 * Metric sets are exported on the network through a transport. A
 * transport handle is required to communicate on the network.
 *
 * \param name	The name of the transport type to create.
 * \param log_fn An optional function to call when logging transport messages
 * \returns	A transport handle on success.
 * \returns	0 If the transport could not be created.
 */
extern ldms_t ldms_create_xprt(const char *name, ldms_log_fn_t log_fn);

/**
 * \brief Release a reference on a transport handle
 *
 * Drop a reference on the transport handle. When all references have been
 * dropped, free all the resources associated with the transport.
 *
 * \param x	The transport handle to free.
 */
extern void ldms_release_xprt(ldms_t x);

/**
 * \brief Return the name of a transport
 *
 * Return a character string representing the transport.
 *
 * \param x	The transport handle
 * \returns	A character string representing the local interface
 * address on which the transport is communicating.
 * \returns	0 if the transport handle is invalid.
 */
extern const char *ldms_get_xprt_name(ldms_t x);

/**
 * \brief Request a connection to an LDMS host.
 *
 * \param x	The transport handle
 * \param sa	Socket address specifying the host address and port.
 * \param sa_len The length of the socket address.
 * \returns	0 if the connection was established.
 * \returns	An error indicating why the connection failed.
 */
extern int ldms_connect(ldms_t x, struct sockaddr *sa, socklen_t sa_len);

/**
 * \brief Compute the correct response to an authentication challenge.
 *
 * The string returned should be freed by the caller.
 *
 * \param n	The random number needed to prevent replay attacks.
 * \returns	NULL if the response cannot be computed. See errno.
 * \returns	The answer to expect/provide.
 */
extern char *ldms_get_auth_string(uint64_t n);

/**
 * \brief Listen for connection requests from LDMS peers.
 *
 * \param x	The transport handle
 * \param sa	Socket address specifying the host address and port.
 * \param sa_len The length of the socket address.
 * \returns	0 if a listening endpoint was successfully created.
 * \returns	An error indicating why the listen failed.
 */
extern int ldms_listen(ldms_t x, struct sockaddr *sa, socklen_t sa_len);
/**
 * \brief Close a connection to an LDMS host.
 *
 * \param x	The transport handle
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
 * This function is called in response to a call to ldms_dir and
 * thereafter whenever there is an update to the set directory. That
 * is, this function may be called repeatedly until the transport is
 * closed.
 *
 * The application should call \c ldms_dir_release when it is finished
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
	char *set_names[FLEXIBLE_ARRAY_MEMBER];
} *ldms_dir_t;

/**
 * \brief The \c ldms_dir callback function
 *
 * This function is called when the directory request has been
 * returned by the queried host.
 *
 * \param t		The transport handle
 * \param status	The status of the request
 * \param dir		Pointer to the returned directory data
 * \param cb_arg	The callback argument specified to the ldms_dir
 *			function.
 */
typedef void (*ldms_dir_cb_t)(ldms_t t, int status, ldms_dir_t dir, void *cb_arg);

/**
 * \brief Release the resources consumed by a directory
 *
 * This function is called by the application to release the resources
 * used by an ldms_dir_t.
 *
 * \param t	 The transport handle
 * \param dir	 Pointer to an ldms_dir_s structure to be released.
 */
void ldms_dir_release(ldms_t t, ldms_dir_t dir);

/**
 * \brief Cancel LDMS directory updates
 *
 * This function cancels updates to the LDMS directory initiated by a
 * call to ldms_dir.
 *
 * \param t	 The transport handle
 */
void ldms_dir_cancel(ldms_t t);

/**
 * \brief Query the sets published by a host.
 *
 * This function queries the peer for the set of published metric sets.
 * The caller specifies the callback function to invoke when the
 * directory has been returned by the peer. If the caller specifies
 * the \c LDMS_DIR_F_NOTIFY flag, the callback function will be
 * invoked whenever the peer updates its set of published metrics.
 *
 * See the ldms_dir_cancel function to cancel directory updates.
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
extern int ldms_dir(ldms_t x, ldms_dir_cb_t cb, void *cb_arg, uint32_t flags);

/* PLUGINDIR comes from automake */
#define LDMS_XPRT_LIBPATH_DEFAULT PLUGINDIR
#define LDMS_DEFAULT_PORT	LDMSDPORT
#define LDMS_LOOKUP_PATH_MAX	511
#define LDMS_PASSWORD_MAX	128


/**
 * \brief Query the contents of a metric set.
 *
 * This function queries the peer for the detail of the metric set \c
 * path. If the query is successful, the function puts the set handle
 * in the pointer provided by the \c s parameter. This function takes
 * a reference on the metric set. The memory for the metric set will
 * not be released until all references have been removed. The
 * ldms_release() function should be called when this metric set is no
 * longer needed.
 *
 * See the \c ldms_dir() function for detail on how to query a host for
 * the list of published metric sets.
 *
 * \param t	 The transport handle
 * \param name   The set name.
 * \param cb	 The callback function to invoke when the lookup has
 *		 completed.
 * \param cb_arg A user context that will be provided as a parameter
 *		 to the \c cb function.
 * \returns	 0 if the query was submitted successfully.
 */
extern int ldms_lookup(ldms_t t, const char *name,
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
extern int ldms_update(ldms_set_t s, ldms_update_cb_t update_cb, void *arg);

/**
 * \brief Create a Metric set
 *
 * Create a metric set on the local host. The metric set is added to
 * the data base of metric sets exported by this host. The \c set_name
 * parameter specifies the logical name of the metric set. This name
 * is returned to the client when queried with the \c ldms_dir
 * function.
 *
 * \param set_name	The name of the metric set.
 * \param meta_sz	The maximum meta data size.
 * \param data_sz	The maximum data size.
 * \param s		Pointer to ldms_set_t handle that will be set to the new handle.
 * \returns		0 on success
 */
extern int ldms_create_set(const char *set_name,
			   size_t meta_sz, size_t data_sz, ldms_set_t *s);

/**
 * \brief Map a metric set for remote access
 *
 * This service is used to map a local metric set for access on the
 * network. The \c addr parameter specifies the address of the memory
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
 * \brief Destroy a Metric set
 *
 * Called to destroy a local metric set created with
 * ldms_create_set. This also invalidates any handles that remote
 * users may have to this metric set.
 *
 * \param s		The ldms_set_t handle to destroy.
 */
extern void ldms_destroy_set(ldms_set_t s);

/**
 * \brief Get the set name.
 *
 * Return the name of the metric set.
 *
 * \param s	The ldms_set_t handle.
 * \returns	The metric set name as a character string.
 */
extern const char *ldms_get_set_name(ldms_set_t s);

/**
 * \brief Get the number of metrics in the metric set.
 * \param s	The ldms_set_t handle.
 * \return The number of metrics in the metric set. -1 otherwise.
 */
extern uint32_t ldms_get_set_card(ldms_set_t s);

/**
 * \brief Get a set by name.
 *
 * Find a local metric set by name.
 *
 * \param set_name	The set name.
 * \returns		The ldms_set_t handle or 0 if not found.
 */
extern ldms_set_t ldms_get_set(const char *set_name);

/**
 * \brief Get the metric schema generation number.
 *
 * A metric set has a \c generation number that chnages when metrics
 * are added or removed from the metric set.
 *
 * \param s	The ldms_set_t handle.
 * \returns	The 64bit meta data generation number.
 */
uint64_t ldms_get_meta_gn(ldms_set_t s);

/**
 * \brief Get the metric data generation number.
 *
 * A metric set has a \c generation number that chnages when metric
 * values are modified.
 *
 * \param s	The ldms_set_t handle.
 * \returns	The 64bit data generation number.
 */
uint64_t ldms_get_data_gn(ldms_set_t s);

/**
 * \brief Return the maxmimum size of the metric set
 *
 * The maximum size of a metric set is specified when it is
 * creates. The library may make this larger, but will never make it
 * smaller than specified. The size of the metric set determines how
 * many metrics the set can hold.
 *
 * \param s	The ldms_set_t handle.
 * \returns	The maximum size of the metric set.
 */
extern uint32_t ldms_get_max_size(ldms_set_t s);

/**
 * \brief Return the current size of the metric set
 *
 * The current size of a metric set depends on how many metrics have
 * been created in the set.
 *
 * \param s	The ldms_set_t handle.
 * \returns	The current size of the metric set.
 */
extern uint32_t ldms_get_size(ldms_set_t s);

/**
 * \brief Return the number of metrics in the set
 *
 * Returns the number of metrics in the set.
 *
 * \param s	The ldms_set_t handle.
 * \returns	The number of metrics in the set.
 */
extern uint32_t ldms_get_cardinality(ldms_set_t s);
/** \} */

/**
 * \addtogroup ldms_metric LDMS Metric Manaegment
 *
 * These functions are used to create and destroy local metrics
 * and to get and set the value of a metric.
 * \{
 */

/**
 * \brief Start an LDMS transaction
 *
 * Metric sets are updated one metric at a time. A metric set provider
 * may provide information to the peer that the metric set is
 * consistent by updating metrics inside a transaction. In this way,
 * the peer can determine if the metric set updated was in the middle
 * of a transaction and therefore contains potentially inconsistent
 * data.
 *
 * \param s     The ldms_set_t handle.
 * \returns 0   If the transaction was started.
 * \returns !0  If the specified metric set is invalid.
 */
extern int ldms_start_transaction(ldms_set_t s);

/**
 * \brief Begin an LDMS transaction
 *
 * A metric set provider may provide information to the peer that the
 * metric set is consistent by updating metrics inside a
 * transaction. In this way, the peer can determine if the metric set
 * updated was in the middle of a transaction and therefore contains
 * potentially inconsistent data.
 *
 * \param s     The ldms_set_t handle.
 * \returns 0   If the transaction was started.
 * \returns !0  If the specified metric set is invalid.
 */
extern int ldms_begin_transaction(ldms_set_t s);

/**
 * \brief End an LDMS transaction
 *
 * Marks the metric set as consistent and time-stamps the data.
 *
 * \param s     The ldms_set_t handle.
 * \returns 0   If the transaction was started.
 * \returns !0  If the specified metric set is invalid.
 */
extern int ldms_end_transaction(ldms_set_t s);

/**
 * \brief Get the time the set was last modified.
 *
 * Returns an ldms_timestamp structure that specifies when
 * ldms_end_transaction was last called by the metric set provider. If
 * the metric set provider does not update it's metric sets inside
 * transactions, then this value is invalid. This value is undefined
 * if the metric set is not consistent, see \c ldms_is_set_consistent.
 *
 * \param s     The metric set handle
 * \returns ts  A pointer to a timestamp structure.
 */
extern struct ldms_timestamp const *ldms_get_timestamp(ldms_set_t s);

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
extern int ldms_is_set_consistent(ldms_set_t s);

/**
 * \brief Add a metric to the set
 *
 * Adds a metric to the metric set. The \c name of the metric must be
 * unique.
 *
 * \param s	The ldms_set_t handle.
 * \param name	The name of the metric.
 * \param t	The type of the metric.
 * \returns	A handle to the newly created metric if successful.
 * \returns	0 if an error occured.
 */
extern ldms_metric_t ldms_add_metric(ldms_set_t s, const char *name, enum ldms_value_type t);

/**
 * \brief Return the storage needed for metric
 *
 * Given a metric name and a metric type, return the number of bytes
 * the metric would consume in a metric set. This function is provided
 * as a means to estimate the required size of the containing metric
 * set given the names and types of the metrics it will contain.
 *
 * \param name	The name of the metric.
 * \param t	The LDMS metric type.
 * \param meta_sz Pointer to the variable to receive the meta data size
 * \param data_sz Pointer to the variable to receive the data size
 * \returns	0 on success or EINVAL if \c t is an unrecognized type.
 */
extern int ldms_get_metric_size(const char *name, enum ldms_value_type t,
				     size_t *meta_sz, size_t *data_sz);

/**
 * \brief Get the metric handle for a metric
 *
 * Returns the metric handle for the metric with the specified
 * name. This handle can then be used with the ldms_get_XXX functions
 * to return the value of the metric.
 *
 * \param s	The metric set handle
 * \param name	The name of the metric.
 * \returns	The metric set handle or 0 if there is none was found.
 */
extern ldms_metric_t ldms_get_metric(ldms_set_t s, const char *name);

/**
 * \brief Create a metric handle from a value descriptor.
 *
 * Returns the metric handle for the metric with the specified
 * name. This handle can then be used with the ldms_get_XXX functions
 * to return the value of the metric.
 *
 * \param s	The metric set handle
 * \param vd	The value descriptor.
 * \returns	A new metric handle or NULL if memory was not available.
 */
extern ldms_metric_t ldms_make_metric(ldms_set_t s, struct ldms_value_desc *vd);

/**
 * \brief Release a reference on the metric.
 *
 * \param m	The metric set handle
 */
extern void ldms_metric_release(ldms_metric_t m);

/**
 * \brief Returns the name of a metric.
 *
 * Returns the name of the metric specified by the handle.
 *
 * \param m	The metric handle
 * \returns	A character string containing the name of the metric.
 */
extern const char *ldms_get_metric_name(ldms_metric_t m);

/**
 * \brief Returns the type of a metric.
 *
 * Returns the type of the metric specified by the handle.
 *
 * \param m	The metric handle
 * \returns	The ldms_value_type for the metric.
 */
extern enum ldms_value_type ldms_get_metric_type(ldms_metric_t m);

/**
 * \brief Get a metric set type as a string.
 *
 * Returns a string representing the data type.
 *
 * \param t	The metric value type.
 * \returns	A character string representing the metric value type.
 */
extern const char *ldms_type_to_str(enum ldms_value_type t);

/**
 * \brief Convert a string to an ldms_value_type
 *
 * \param name	Character string representing the type name.
 * \returns	The associated ldms_value_type.
 */
extern enum ldms_value_type ldms_str_to_type(const char *name);

/**
 * \brief LDMS Visit Callback Function
 *
 * A function of the type ldms_vist_cb_t is passed as an argument to
 * ldms_visit_metrics(). The ldms_vist_metrics() function calls the
 * specified \c cb function once for each metric in the set.
 *
 * The parameters to the callback function are a pointer to an
 * ldms_value_desc structure, a pointer ot the value itself, and a
 * pointer to a callback parameter argument specified in teh
 * ldms_visit_metrics() call.
 *
 * \param vd	Pointer to an ldms_value_desc structure containing the
 * name and type of the metric.
 * \param v	Pointer to an ldms_value union.
 * \param arg	Callback argument provided to the ldms_visit_metrics() function.
 */
typedef void (*ldms_visit_cb_t)(struct ldms_value_desc *vd, union ldms_value *v, void *arg);

/**
 * \brief Iterate over the Metric Values in a Metric set.
 *
 * The function of the type ldms_vist_cb_t is passed as an argument to
 * ldms_visit_metrics(). The ldms_vist_metrics() function calls the
 * specified \c cb function once for each metric in the set.
 *
 * \param s	The metric set handle.
 * \param cb	The callback function to call.
 * \param arg	The \c arg parameter to provide to the callback function.
 */
extern void ldms_visit_metrics(ldms_set_t s, ldms_visit_cb_t cb, void *arg);

/**
 * \brief Get the first metric in the metric set.
 *
 * These functions are used to iterate over the metrics in a metric
 * set. The struct ldms_iterator is used to keep track of the current
 * position in the set and must be provided in the call the
 * ldms_next().
 *
 * \param i	Pointer to ldms_iterator structure.
 * \param set	The metric set handle.
 * \returns	An ldms_value_desc for the first metric in the metric
 * set.
 * \returns	Null if the set is empty or invalid.
 */
extern struct ldms_value_desc *ldms_first(struct ldms_iterator *i, ldms_set_t set);

/**
 * \brief Get the next metric in the metric set.
 *
 * Returns the ldms_value_desc for the next metric in the metric
 * set.
 *
 * \param i	Pointer to ldms_iterator structure.
 * \returns	An ldms_value_desc for the next metric in the metric
 * set.
 * \returns	Null if the set is empty or invalid.
 */
extern struct ldms_value_desc *ldms_next(struct ldms_iterator *i);

/**
 * \brief Set the user-data associated with a metric
 *
 * Sets the user-defined meta data associated with a metric.
 *
 * \param m     The metric handle.
 * \param u     An 8B user-data value.
 */
static inline void ldms_set_user_data(ldms_metric_t m, uint64_t u)
{
	struct ldms_metric *_m = (struct ldms_metric *)m;
	_m->desc->user_data = u;
}

/**
 * \brief Get the user-data associated with a metric
 *
 * Returns the user-defined metric meta-data set with
 * \c ldms_set_user_data.
 *
 * \param m     The metric handle.
 * \returns u   The user-defined metric meta data value.
 */
static inline uint64_t ldms_get_user_data(ldms_metric_t m)
{
	struct ldms_metric *_m = (struct ldms_metric *)m;
	return _m->desc->user_data;
}

/**
 * \brief Set the value of a metric.
 *
 * Set the specified metric \c m to the value contained in the union
 * \c v. The value type in the union must match the value type used to
 * create the metric. See the ldms_get_metric_type() for information
 * on how to discover the type of a metric.
 *
 * \param _m	The metric handle.
 * \param v	An ldms_value union specifying the value.
 */
static inline void ldms_set_metric(ldms_metric_t _m, union ldms_value *v)
{
	struct ldms_metric *m = (struct ldms_metric *)_m;

	switch (m->desc->type) {
	case LDMS_V_U8:
	case LDMS_V_S8:
		m->value->v_u8 = v->v_u8;
		break;
	case LDMS_V_U16:
	case LDMS_V_S16:
		m->value->v_u16 = v->v_u16;
		break;
	case LDMS_V_U32:
	case LDMS_V_S32:
		m->value->v_u32 = v->v_u32;
		break;
	case LDMS_V_U64:
	case LDMS_V_S64:
		m->value->v_u64 = v->v_u64;
		break;
	case LDMS_V_F:
		m->value->v_f = v->v_f;
		break;
	case LDMS_V_D:
		m->value->v_d = v->v_d;
		break;
	case LDMS_V_LD:
		m->value->v_ld = v->v_ld;
		break;
	default:
		return;
	}
	m->set->data->gn++;
}

/**
 * \brief Set the value of a metric.
 *
 * Set the specified metric \c m to the unsigned char value specified
 * by \c v.
 *
 * \param _m	The metric handle.
 * \param v	The value.
 */
static inline void ldms_set_u8(ldms_metric_t _m, uint8_t v) {
	struct ldms_metric *m = (struct ldms_metric *)_m;
	m->value->v_u8 = v;
	m->set->data->gn++;
}

/**
 * \brief Set the value of a metric.
 *
 * Set the specified metric \c m to the unsigned short value specified
 * by \c v.
 *
 * \param _m	The metric handle.
 * \param v	The value.
 */
static inline void ldms_set_u16(ldms_metric_t _m, uint16_t v) {
	struct ldms_metric *m = (struct ldms_metric *)_m;
	m->value->v_u16 = v;
	m->set->data->gn++;
}

/**
 * \brief Set the value of a metric.
 *
 * Set the specified metric \c m to the unsigned int value specified
 * by \c v.
 *
 * \param _m	The metric handle.
 * \param v	The value.
 */
static inline void ldms_set_u32(ldms_metric_t _m, uint32_t v) {
	struct ldms_metric *m = (struct ldms_metric *)_m;
	m->value->v_u32 = v;
	m->set->data->gn++;
}

/**
 * \brief Set the value of a metric.
 *
 * Set the specified metric \c m to the unsigned 64b value specified
 * by \c v.
 *
 * \param _m	The metric handle.
 * \param v	The value.
 */
static inline void ldms_set_u64(ldms_metric_t _m, uint64_t v) {
	struct ldms_metric *m = (struct ldms_metric *)_m;
	m->value->v_u64 = v;
	m->set->data->gn++;
}

/**
 * \brief Set the value of a metric.
 *
 * Set the specified metric \c m to the signed char value specified
 * by \c v.
 *
 * \param _m	The metric handle.
 * \param v	The value.
 */
static inline void ldms_set_s8(ldms_metric_t _m, int8_t v) {
	struct ldms_metric *m = (struct ldms_metric *)_m;
	m->value->v_s8 = v;
	m->set->data->gn++;
}

/**
 * \brief Set the value of a metric.
 *
 * Set the specified metric \c m to the signed short value specified
 * by \c v.
 *
 * \param _m	The metric handle.
 * \param v	The value.
 */
static inline void ldms_set_s16(ldms_metric_t _m, int16_t v) {
	struct ldms_metric *m = (struct ldms_metric *)_m;
	m->value->v_s16 = v;
	m->set->data->gn++;
}

/**
 * \brief Set the value of a metric.
 *
 * Set the specified metric \c m to the signed int value specified
 * by \c v.
 *
 * \param _m	The metric handle.
 * \param v	The value.
 */
static inline void ldms_set_s32(ldms_metric_t _m, int32_t v) {
	struct ldms_metric *m = (struct ldms_metric *)_m;
	m->value->v_s32 = v;
	m->set->data->gn++;
}

/**
 * \brief Set the value of a metric.
 *
 * Set the specified metric \c m to the signed 64b value specified
 * by \c v.
 *
 * \param _m	The metric handle.
 * \param v	The value.
 */
static inline void ldms_set_s64(ldms_metric_t _m, int64_t v) {
	struct ldms_metric *m = (struct ldms_metric *)_m;
	m->value->v_s64 = v;
	m->set->data->gn++;
}

/**
 * \brief Set the value of a metric.
 *
 * Set the specified metric \c m to the single precision floating
 * point value specified by \c v.
 *
 * \param _m	The metric handle.
 * \param v	The value.
 */
static inline void ldms_set_float(ldms_metric_t _m, float v) {
	struct ldms_metric *m = (struct ldms_metric *)_m;
	m->value->v_f = v;
	m->set->data->gn++;
}

/**
 * \brief Set the value of a metric.
 *
 * Set the specified metric \c m to the double precision floating
 * point value specified by \c v.
 *
 * \param _m	The metric handle.
 * \param v	The value.
 */
static inline void ldms_set_double(ldms_metric_t _m, double v) {
	struct ldms_metric *m = (struct ldms_metric *)_m;
	m->value->v_d = v;
	m->set->data->gn++;
}

/**
 * \brief Set the value of a metric.
 *
 * Set the specified metric \c m to the long double precision floating
 * point value specified by \c v.
 *
 * \param _m	The metric handle.
 * \param v	The value.
 */
static inline void ldms_set_long_double(ldms_metric_t _m, long double v) {
	struct ldms_metric *m = (struct ldms_metric *)_m;
	m->value->v_ld = v;
	m->set->data->gn++;
}

/**
 * \brief Get the value of a metric.
 *
 * Get the specified metric \c m as a void pointer to the value
 *
 * \param _m	The metric handle.
 * \returns	Unsigned byte value from the metric.
 */
static inline void *ldms_get_value_ptr(ldms_metric_t _m) {
	return ((struct ldms_metric *)_m)->value;
}

/**
 * \brief Get the value of a metric.
 *
 * Get the specified metric \c m as an unsigned byte.
 *
 * \param _m	The metric handle.
 * \returns	Unsigned byte value from the metric.
 */
static inline uint8_t ldms_get_u8(ldms_metric_t _m) {
	return ((struct ldms_metric *)_m)->value->v_u8;
}

/**
 * \brief Get the value of a metric.
 *
 * Get the specified metric \c m as an unsigned byte.
 *
 * \param _m	The metric handle.
 * \returns	Unsigned byte value from the metric.
 */
static inline uint16_t ldms_get_u16(ldms_metric_t _m) {
	return ((struct ldms_metric *)_m)->value->v_u16;
}

/**
 * \brief Get the value of a metric.
 *
 * Get the specified metric \c m as an unsigned byte.
 *
 * \param _m	The metric handle.
 * \returns	Unsigned byte value from the metric.
 */
static inline uint32_t ldms_get_u32(ldms_metric_t _m) {
	return ((struct ldms_metric *)_m)->value->v_u32;
}

/**
 * \brief Get the value of a metric.
 *
 * Get the specified metric \c m as an unsigned byte.
 *
 * \param _m	The metric handle.
 * \returns	Unsigned byte value from the metric.
 */
static inline uint64_t ldms_get_u64(ldms_metric_t _m) {
	return ((struct ldms_metric *)_m)->value->v_u64;
}

/**
 * \brief Get the value of a metric.
 *
 * Get the specified metric \c m as an signed byte.
 *
 * \param _m	The metric handle.
 * \returns	Unsigned byte value from the metric.
 */
static inline int8_t ldms_get_s8(ldms_metric_t _m) {
	return ((struct ldms_metric *)_m)->value->v_s8;
}

/**
 * \brief Get the value of a metric.
 *
 * Get the specified metric \c m as an signed short.
 *
 * \param _m	The metric handle.
 * \returns	Signed 16b value from the metric.
 */
static inline int16_t ldms_get_s16(ldms_metric_t _m) {
	return ((struct ldms_metric *)_m)->value->v_s16;
}

/**
 * \brief Get the value of a metric.
 *
 * Get the specified metric \c m as a signed int.
 *
 * \param _m	The metric handle.
 * \returns	Signed 32b value from the metric.
 */
static inline int32_t ldms_get_s32(ldms_metric_t _m) {
	return ((struct ldms_metric *)_m)->value->v_s32;
}

/**
 * \brief Get the value of a metric.
 *
 * Get the specified metric \c m as an signed 64b value.
 *
 * \param _m	The metric handle.
 * \returns	Signed 64b value from the metric.
 */
static inline int64_t ldms_get_s64(ldms_metric_t _m) {
	return ((struct ldms_metric *)_m)->value->v_s64;
}
/**
 * \brief Get the value of a metric.
 *
 * Get the specified metric \c m as a single precision floating point value.
 *
 * \param _m	The metric handle.
 * \returns	Float value from the metric.
 */
static inline float ldms_get_float(ldms_metric_t _m) {
	return ((struct ldms_metric *)_m)->value->v_f;
}
/**
 * \brief Get the value of a metric.
 *
 * Get the specified metric \c m as double precision floating point
 * value.
 *
 * \param _m	The metric handle.
 * \returns	Double value from the metric.
 */
static inline double ldms_get_double(ldms_metric_t _m) {
	return ((struct ldms_metric *)_m)->value->v_d;
}
/**
 * \brief Get the value of a metric.
 *
 * Get the specified metric \c m as long double precision floating
 * point value.
 *
 * \param _m	The metric handle.
 * \returns	Double value from the metric.
 */
static inline double ldms_get_long_double(ldms_metric_t _m) {
	return ((struct ldms_metric *)_m)->value->v_ld;
}
extern void ldms_print_set_metrics(ldms_set_t _set);
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
typedef struct ldms_notify_event_s {
	enum ldms_notify_event_type {
		LDMS_SET_MODIFIED = 1,
		LDMS_USER_DATA = 2,
	} type;			/*! Specifies the type of event  */
	size_t len;		/*! The size of the event in bytes */
	union { /* flex array usage in unions is outside the standards. */
		unsigned char u_data[0];/*! User-data for the LDMS_USER_DATA
					  type */
	};
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
