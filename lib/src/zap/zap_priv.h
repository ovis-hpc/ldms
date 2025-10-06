/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-2017,2019,2020 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2013-2017,2019,2020 Open Grid Computing, Inc. All rights reserved.
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

#ifndef __ZAP_PRIV_H__
#define __ZAP_PRIV_H__
#include <inttypes.h>
#include <semaphore.h>
#include <sys/queue.h>
#include <pthread.h>
#include <errno.h>
#include "ovis_ref/ref.h"
#include "ovis-ldms-config.h"
#include "ovis_thrstats/ovis_thrstats.h"
#include "zap.h"

#include "config.h"

struct zap_version {
	uint8_t major;
	uint8_t minor;
	uint8_t patch;
	uint8_t flags;
};

#define ZAP_VERSION_MAJOR 0x01
#define ZAP_VERSION_MINOR 0x03
#define ZAP_VERSION_PATCH 0x01
#define ZAP_VERSION_FLAGS 0x00

#define ZAP_VERSION_SET(v) do { \
	(v).major = ZAP_VERSION_MAJOR; \
	(v).minor = ZAP_VERSION_MINOR; \
	(v).patch = ZAP_VERSION_PATCH; \
	(v).flags = ZAP_VERSION_FLAGS; \
} while (0)

#define ZAP_VERSION_EQUAL(v) ( \
	((v).major == ZAP_VERSION_MAJOR) && \
	((v).minor == ZAP_VERSION_MINOR) \
)

#define ZLOG(ep, fmt, ...) do { \
	ovis_log(NULL, OVIS_LERROR, fmt, ## __VA_ARGS__); \
} while(0)

extern int __zap_assert;
#define ZAP_ASSERT(_cond_, _ep_, _fmt_, ...) do { \
	if (!(_cond_)) { \
		ZLOG((_ep_), _fmt_, ##__VA_ARGS__); \
		if (__zap_assert) \
			assert(_cond_); \
	} \
} while(0)

void __zap_assert_flag(int f);

/*
 * State definitions
 *
 * ZAP_EP_INIT		The first state when an endpoint is created.
 * ZAP_EP_LISTENING	The state of the listener endpoints. An endpoint state
 * 			becomes LISTENING when application calls zap_listen().
 * 			INIT --> LISTENING
 * ZAP_EP_ACCEPTING	Passive side state. When transport receives
 * 			a connect request, it creates a new endpoint.
 * 			The new endpoint is in the ACCEPTING state until
 * 			the transport delivers a zap CONN_REQUEST event
 * 			to application upon receiving a connecting message
 * 			from the peer.
 * 			INIT --> ACCEPTING
 * ZAP_EP_CONNECTING	Active side state. An endpoint is in the CONNECTING state
 * 			when application calls zap_connect() until an accepting
 * 			or rejecting message is received.
 * 			INIT --> CONNECTING
 * ZAP_EP_CONNECTED	On the passive side, the state moves from ACCEPTING to
 * 			CONNECTED when application calls zap_accept().
 * 			On the active side, the state moves from CONNECTING to
 * 			CONNECTED when it receives the accepting message.
 * 			passive: ACCEPTING --> CONNECTED
 * 			active: CONNECTING --> CONNECTED
 * ZAP_EP_PEER_CLOSE	PEER_CLOSE state means the peer has intentionally
 * 			or unintentionally closed the connection.
 * 			An endpoint is in the PEER_CLOSE state if transport
 * 			receives a disconnect event while its state is
 * 			in CONNECTED.
 * 			CONNECTED --> PEER_CLOSE
 * ZAP_EP_CLOSE		An endpoint is in the CLOSE state when application
 * 			intentionally calls zap_close().
 * 			CONNECTED --> CLOSE
 * ZAP_EP_ERROR		An endpoint is in the ERROR state if an error occurs
 * 			on the transport.
 * 			* --> ERROR, where * can be any states except INIT.
 */
/** Return 1 if it is compatible. Otherwise, 0 is returned. */

int zap_version_check(struct zap_version *v)
{
	return ZAP_VERSION_EQUAL(*v);
}

const char *zap_ep_state_str[] = {
	[ZAP_EP_INIT]        =  "ZAP_EP_INIT",
	[ZAP_EP_LISTENING]   =  "ZAP_EP_LISTENING",
	[ZAP_EP_ACCEPTING]   =  "ZAP_EP_ACCEPTING",
	[ZAP_EP_CONNECTING]  =  "ZAP_EP_CONNECTING",
	[ZAP_EP_CONNECTED]   =  "ZAP_EP_CONNECTED",
	[ZAP_EP_PEER_CLOSE]  =  "ZAP_EP_PEER_CLOSE",
	[ZAP_EP_CLOSE]       =  "ZAP_EP_CLOSE",
	[ZAP_EP_ERROR]       =  "ZAP_EP_ERROR"
};

const char *__zap_ep_state_str(zap_ep_state_t state);

typedef struct zap_io_thread *zap_io_thread_t;

struct zap_ep {
	zap_t z;
	struct ref_s ref;
	uint32_t ref_count;
	pthread_mutex_t lock;
	zap_ep_state_t state;
	void *ucontext;
	int prio;		/* !0 for high priority endpoints */

	sem_t block_sem; /**< Semaphore to support blocking operations */

	LIST_HEAD(zap_map_list, zap_map) map_list;

	/** Event callback routine. */
	zap_cb_fn_t cb;

	/** The thread that the endpoint is assigned to. */
	zap_io_thread_t thread;

#ifdef _ZAP_EP_TRACK_
	TAILQ_ENTRY(zap_ep) ep_link;
#endif
	/** (private to libzap) for thread->ep_list */
	LIST_ENTRY(zap_ep) _entry;
	uint64_t sq_sz; /* send queue size of the endpoint */
};

#define ZAP_IO_THREAD_POOL_MAX 1024 /* 1K pools should suffice */

struct zap_io_thread_pool_s {
	int idx; /* the pool index */
	int n; /* number of threads in the pool */
	LIST_HEAD(, zap_io_thread) _io_threads;
};

struct zap {
	char name[ZAP_MAX_TRANSPORT_NAME_LEN];
	int max_msg;		/* max send message size */

	LIST_ENTRY(zap) zap_link;

	/** Create a new endpoint */
	zap_ep_t (*new)(zap_t z, zap_cb_fn_t cb);

	/** Destroy an endpoint. */
	void (*destroy)(zap_ep_t ep);

	/** Request a connection with a server */
	zap_err_t (*connect)(zap_ep_t ep, struct sockaddr *sa, socklen_t sa_len,
			     char *data, size_t data_len, int tpi);

	/** Listen for incoming connection requests */
	zap_err_t (*listen)(zap_ep_t ep, struct sockaddr *sa, socklen_t sa_len);

	/** Accept a connection request */
	zap_err_t (*accept)(zap_ep_t ep, zap_cb_fn_t cb, char *data, size_t data_len, int tpi);

	/** Reject a connection request */
	zap_err_t (*reject)(zap_ep_t ep, char *data, size_t data_len);

	/** Close the connection */
	zap_err_t (*close)(zap_ep_t ep);

	/** Send a message */
	zap_err_t (*send)(zap_ep_t ep, char *buf, size_t sz);

	/** Send a message */
	zap_err_t (*send2)(zap_ep_t ep, char *buf, size_t sz, void *cb_arg);

	/** RDMA write data to a remote buffer */
	zap_err_t (*write)(zap_ep_t ep,
			   zap_map_t src_map, char *src,
			   zap_map_t dst_map, char *dst, size_t sz,
			   void *context);

	/**  RDMA read data from a remote buffer */
	zap_err_t (*read)(zap_ep_t ep,
			  zap_map_t src_map, char *src,
			  zap_map_t dst_map, char *dst, size_t sz,
			  void *context);

	/** Free a remote buffer */
	zap_err_t (*unmap)(zap_map_t map);

	/** Share a mapping with a remote peer */
	zap_err_t (*share)(zap_ep_t ep, zap_map_t m,
			   const char *msg, size_t msg_len);


	/** Get the local and remote sockaddr for the endpoint */
	zap_err_t (*get_name)(zap_ep_t ep, struct sockaddr *local_sa,
			      struct sockaddr *remote_sa, socklen_t *sa_len);

	/** Memory information callback */
	zap_mem_info_fn_t mem_info_fn;

	/** Pointer to the transport's private data */
	void *private;

	/**
	 * \brief Send mapped data to peer.
	 *
	 * No data is copied out from the described memory region. When the
	 * operation is completed, \c ZAP_EVENT_SEND_COMPLETE shall be
	 * delivered. If the operation synchronously failed, \c
	 * ZAP_EVENT_SEND_NAPPED_COMPLETE will not be delivered. The \c context is the
	 * application context coupling with the completion event.
	 */
	zap_err_t (*send_mapped)(zap_ep_t ep, zap_map_t map, void *buf,
				 size_t len, void *context);

	/**
	 * Create and start an IO thread.
	 *
	 * This API is called when libzap determines that a new IO thread for
	 * the transport is required. The transport shall:
	 *   1) allocate a zap_io_thread structure (or an extension of it),
	 *   2) call `zap_io_thread_init()` to initialize the structure,
	 *   3) perform additional transport-specific IO thread initialization,
	 *   4) create and start a POSIX thread, and
	 *   5) returns the `zap_io_thread` handle.
	 *
	 * The purpose of an IO thread is to process the low-level events from
	 * associated endpoints, create zap events and deliver them to the
	 * application. In addition, to collect thread statistics, libzap
	 * requires the transport IO thread to call `zap_thrstat_wait_start()`
	 * before it sleeps, and to call `zap_thrstat_wait_end()` when it wakes
	 * up. The IO thread shall follow the following procedure:
	 *   1) call `zap_thrstat_wait_start()`
	 *   2) wait for an event or events from the associated endpoints,
	 *   3) wake up on events then call `zap_thrstat_wait_end()`,
	 *   4) process the events, converting them to zap events with
	 *      timestamps from \c clock_gettime(),
	 *   5) deliver zap events via \c zap_event_deliver(), and
	 *   6) repeat from 1.
	 *
	 *
	 * \retval thr  On success, the thread handle.
	 * \retval NULL On failure. \c errno is also set to describe the error.
	 */
	zap_io_thread_t (*io_thread_create)(zap_t z);

	/**
	 * Terminate the IO thread.
	 *
	 * This API is called when libzap determines that the thread is no
	 * longer needed. The transport shall terminate the thread and release
	 * its resources, including the thread handle memory allocated in the
	 * thread creation.
	 *
	 * \retval ZAP_ERR_OK On success.
	 * \retval zerr A zap error code on failure.
	 */
	zap_err_t (*io_thread_cancel)(zap_io_thread_t t);

	/**
	 * Assign an endpoint to an IO thread.
	 *
	 * libzap calls this function to assign the endpoint \c ep to the thread
	 * \c t. A thread may have multiple endpoints assigned to it.
	 *
	 * \retval ZAP_ERR_OK On success.
	 * \retval zerr A zap error code on failure.
	 */
	zap_err_t (*io_thread_ep_assign)(zap_io_thread_t t, zap_ep_t ep);

	/**
	 * Release an endpoint from an IO thread.
	 *
	 * libzap calls this function to release the endpoint \c ep from the
	 * thread \c t.
	 *
	 * \retval ZAP_ERR_OK On success.
	 * \retval zerr A zap error code on failure.
	 */
	zap_err_t (*io_thread_ep_release)(zap_io_thread_t t, zap_ep_t ep);

	struct zap_io_thread *_passive_ep_thread;

	/** Mutex for _io_threads. */
	pthread_mutex_t _io_mutex;

	/** Number of thread pools. */
	int _n_pools;

	/** The last pool that assigned the endpoint */
	int _last_pool;

	/** Thread pool handles. */
	struct zap_io_thread_pool_s _thr_pool[ZAP_IO_THREAD_POOL_MAX];

	/** Number of threads */
	//int _n_threads;

};

static inline zap_err_t
zap_ep_change_state(struct zap_ep *ep,
		    zap_ep_state_t from_state,
		    zap_ep_state_t to_state)
{
	zap_err_t err = ZAP_ERR_OK;
	pthread_mutex_lock(&ep->lock);
	if (ep->state != from_state){
		err = ZAP_ERR_BUSY;
		goto out;
	}
	ep->state = to_state;
 out:
	pthread_mutex_unlock(&ep->lock);
	return err;
}

struct zap_map {
	LIST_ENTRY(zap_map) link; /*! List of maps for an endpoint. */
	uint32_t ref_count;	  /*! # of calls to zap_map() + zap_map_get() */
	zap_map_type_t type;	  /*! Is this a local or remote map  */
	zap_ep_t ep;		  /*! The endpoint */
	zap_access_t acc;	  /*! Access rights */
	char *addr;		  /*! Address of buffer. */
	size_t len;		  /*! Length of the buffer */
	void *mr[ZAP_LAST];	  /*! xprt-specific memory registrations */
};

struct zap_event_entry {
	TAILQ_ENTRY(zap_event_entry) entry;
	zap_ep_t ep;
	void *ctxt;
};

struct zap_event_queue {
	int depth; /* number of element left */
	int ep_count; /* number of endpoint associated with the queue */
	pthread_t thread; /* worker thread */
	pthread_mutex_t mutex; /* queue mutex */
	pthread_cond_t cond_nonempty; /* nonempty */
	pthread_cond_t cond_vacant; /* vacant */
	zap_thrstat_t stats;
	TAILQ_HEAD(, zap_event_entry) queue;
	TAILQ_HEAD(, zap_event_entry) prio_q;
	TAILQ_HEAD(, zap_event_entry) free_q;
};

typedef zap_err_t (*zap_get_fn_t)(zap_t *pz, zap_mem_info_fn_t map_info_fn);

/**
 * validate map access.
 *
 * \param map The map.
 * \param p The start of the accessing memory.
 * \param sz The size of the accessing memory.
 * \param acc Access flags.
 *
 * \returns 0 for valid access.
 * \returns ERANGE For invalid range access.
 * \returns EACCES For invalid access permission.
 */
static inline
int z_map_access_validate(zap_map_t map, char *p, size_t sz, zap_access_t acc)
{
	if (p < map->addr || (map->addr + map->len) < (p + sz))
		return ERANGE;
	if ((map->acc & acc) != acc)
		return EACCES;
	return 0;
}

#define ZAP_ENV_INT(X) zap_env_int(#X, X)

/**
 * Deliver an event to the application.
 *
 * The transport shall call this function in order to deliver an event to the
 * application.
 */
zap_err_t zap_event_deliver(zap_event_t ev);

/**
 * \brief Initialize a ZAP I/O thread
 *
 * \param t The ZAP I/O thread to initialize
 * \param z The ZAP transport instance
 * \param name Thread name
 *
 * \return 0 on success, error code on failure
 */
int zap_io_thread_init(zap_io_thread_t t, zap_t z, const char *name);

/**
 * Release resources allocated in \c zap_io_thread_init().
 */
int zap_io_thread_release(zap_io_thread_t t);

/**
 * \brief Update thread identity information for a ZAP I/O thread
 *
 * This function should be called from the I/O thread itself to set
 * the proper thread identification information (thread ID, Linux thread ID).
 *
 * \param t The ZAP I/O thread
 */
void zap_io_thread_thread_id(zap_io_thread_t t);

int zap_env_int(char *name, int default_value);
#define ZAP_ENV_INT(X) zap_env_int(#X, X)

double zap_env_dbl(char *name, double default_value);
#define ZAP_ENV_DBL(X) zap_env_dbl(#X, X)

/**
 * Assign \c ep to a zap io thread.
 *
 * The transport shall call this function to assign the endpoint to an io
 * thread. libzap will select the least busy thread, or create a
 * new thread, and assign the endpoint \c ep to the thread.
 * \c zap.io_thread_ep_assign() will be called subsequently.
 *
 * \param ep  The endpoint handle.
 * \param tpi The thread pool index.
 *
 * \retval ZAP_ERR_OK
 * \retval ZAP_ERR_RESOURCE
 * \retval ZAP_ERR_BUSY
 */
zap_err_t zap_io_thread_ep_assign(zap_ep_t ep, int tpi);

/**
 * Release \c ep from the zap io thread.
 *
 * The transport shall call this function to release an endpoint from the
 * associated io thread. \c zap.io_thread_ep_release() will also be called as a
 * subsequence.
 *
 * Consequently, the endpoint will not be processed by the thread any further.
 * However, the endpoint still keeps a reference to the thread for further statistics data access.
 * The endpoint references to the thread is removed when \c zap_io_thread_ep_remove() is called.
 *
 * \param ep   A Zap endpoint
 *
 * \return ZAP_ERR_OK on success. Otherwise, a Zap error code is returned.
 *
 * \see zap_io_thread_ep_remove
 */
zap_err_t zap_io_thread_ep_release(zap_ep_t ep);

/**
 * Remove \c ep reference to the zap io thread.
 *
 * The function nullifies the endpoint reference to the thread and decrements
 * the number of endpoints associated to the thread. This results in reducing
 * the thread's load counter.
 *
 * \param ep    A Zap endpoint
 *
 * \return ZAP_ERR_OK on success. Otherwise, a Zap error code is returned.
 *
 * \see zap_io_thread_ep_release
 */
zap_err_t zap_io_thread_ep_remove(zap_ep_t ep);


/**
 * \struct zap_thrstat
 * \brief Internal structure for thread statistics tracking
 *
 * This structure maintains state for the ZAP thread utilization tracking
 * functions. It uses ovis_thrstats for the core statistics collection.
 */
struct zap_thrstat {
	/** Core thread statistics from ovis_thrstats */
	struct ovis_thrstats stats;

	/** Linked list entry for the global thread stats list */
	LIST_ENTRY(zap_thrstat) entry;

	/** Thread pool index (-1 for dedicated threads) */
	int pool_idx;

	/** Send queue size (in entries) */
	uint64_t sq_sz;

	/** Number of endpoints */
	uint64_t n_eps;
};

/**
 * A structure describing a zap IO thread.
 *
 * The transport implementation may extend this structure to store
 * transport-specific data for its IO threads.
 *
 * The fields beginning with "_" are considered private to libzap. The transport
 * shall not modify such fields.
 */
struct zap_io_thread {
	/** The thread handle.
	 *
	 *  When the transport creats a zap IO thread, it shall use this field
	 *  to create the thread.
	 */
	pthread_t thread;

	/** The transport handle. */
	zap_t zap;

	/** Used to protect the _ep_list and stat entries */
	pthread_mutex_t mutex;

	/** Thread statistics */
	struct zap_thrstat *stat;

	/** (private to libzap) for zap->_io_threads */
	LIST_ENTRY(zap_io_thread) _entry;
	/** (private to libzap) endpoint list */
	LIST_HEAD(, zap_ep) _ep_list;
	/** (private to libzap) number of associated endpoints */
	int _n_ep;

	struct zap_io_thread_pool_s *tp;
};

#endif
