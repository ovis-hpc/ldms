/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2014-2017,2019-2021 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2014-2017,2019-2021 Open Grid Computing, Inc. All rights
 * reserved.
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
#define _GNU_SOURCE
#include <sys/errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <assert.h>
#include <endian.h>
#include <signal.h>
#include <errno.h>
#include <stdarg.h>
#include <limits.h>

#include "ovis_util/os_util.h"
#include "coll/rbt.h"
#include "mmalloc/mmalloc.h"

#include "zap_ugni.h"

/*
 * NOTE on `zap_ugni`
 * ==================
 *
 * The TCP sockets are used to initiate connections, and to listen. In a near
 * future, the sockets will be removed entirely.
 *
 *
 * Threads and endpoints
 * ---------------------
 *
 * `io_thread_proc()` is the thread procedure of all zap thread for `zap_ugni`.
 * The thread uses `epoll` to manage events from 1) sockets, 2) the GNI
 * Completion Queue (CQ) via Completion Channel, and 3) the `zap_ugni` event
 * queue `zq` for timeout events and connection events.
 *
 * Currently, when an endpoint is assigned to a thread, it stays there
 * until it is destroyed (no thread migration).
 *
 * Regarding GNI resources, each thread has 1 completion channel, 1 local CQ,
 * and 1 remote (smsg recv) CQ which are shared among the endpoints assigned to
 * the thread. We need to use 2 CQs because the documentation of
 * `GNI_CqCreate()` said so. We also tried it out with 1 CQ and found out that
 * the remote completion entry cannot be used to determine the type of the
 * completion (GNI_CQ_GET_TYPE()). It reported `GNI_CQ_EVENT_TYPE_POST` instead
 * of `GNI_CQ_EVENT_TYPE_SMSG`.
 *
 * The completion channel has a file descriptor that can be used with epoll. The
 * CQs are attached to the completion channel. After the CQs are armed, they
 * will notify the completion channel when a completion entry is available to be
 * processed. This makes completion channel file descriptor ready to be read and
 * epoll wakes the thread up to process the completion events.
 *
 * `zq` is an additional event queue for each thread that manages timeout events
 * and connection events (CONNECTED, CONN_ERROR and DISCONNECTED). The
 * connection events could result in a recursive application callback
 * or calling application callback from a thread other than the associated io
 * thread are put into `zq` to avoid such situations. An example would be a
 * synchronous error of send/read/write that may be called from other theads or
 * from within the application callback path. The synchronous error immediately
 * put the endpoint in error state, then result in either DISCONNECT or
 * CONN_ERROR events in `zq`.
 *
 *
 * Connecting mechanism
 * --------------------
 *
 * The following describes interactions between the active side and the passive
 * side of `zap_ugni` in connecting mechanism.
 *
 * - [passive] listens on a TCP socket (host/port).
 * - [active] creates an endpoint and TCP `connect()` to the passive side.
 * - [passive] creates an endpoint and TCP `accept()` the TCP connection.
 * - [active] becomes TCP-connected and send a `z_ugni_sock_msg_conn_req`
 *            message over the socket. The message contains information needed
 *            by the passive side to setup the GNI SMSG.
 * - [passive] becomes TCP-connected, and receives `z_ugni_sock_msg_conn_req`
 *             message over the socket. Then, if the protocol version matched,
 *             setup GNI SMSG (also bind GNI endpoint) according to the
 *             information in the received message, and replies with
 *             `z_ugni_sock_msg_conn_accept` message over the socket that
 *             contain similar data needed to setup the SMSG on the active side.
 *             Next, close the TCP socket as it is not needed anymore. The
 *             communication from this point will use GNI SMSG. If the protocol
 *             version does not match or other errors occur, terminates the TCP
 *             connection.
 * - [active] receives `z_ugni_sock_msg_conn_accept` and setup GNI SMSG. Then,
 *            the active side closes the TCP socket as GNI SMSG is established.
 * - [NOTE] At this point both sides can use GNI SMSG to send/recv messages.
 *          The TCP socket part will soon be replaced with `GNI_EpPostData()`
 *          mechanisms. The application data supplied in `zap_connect()`,
 *          `zap_accept()`, or `zap_reject()` happened over GNI SMSG. The
 *          GNI-SMSG-based connection procedure continues as follows.
 * - [active] SMSG-sends the `ZAP_UGNI_MSG_CONNECT` message containing
 *            application connect data. A timeout event is also added into `zq`.
 *            If the connection procedure could not complete (rejected, accepted
 *            or error) within the timeout limit, connection timeout will be
 *            processed and `CONN_ERROR` is delivered to the application.
 * - [passive] SMSG-recv the `ZAP_UGNI_MSG_CONNECT` message and notify the
 *             application about connection request with the data. The
 *             application may `zap_accept()` or `zap_reject()` the connection
 *             request, which results in the passive side SMSG-sending
 *             `ZAP_UGNI_MSG_ACCEPTED` with application-supplied data or
 *             `ZAP_UGNI_MSG_REJECTED` respectively. In the case of ACCEPTED,
 *             also notify the application the CONENCTED event.
 * - [active] SMSG-recv either `ZAP_UGNI_MSG_ACCEPTED` or
 *            `ZAP_UGNI_MSG_REJECTED` and notifies the application accordingly
 *            (CONNECTED or REJECTED event).
 * - [NOTE] After this point, both active and passive sides can `zap_send()`,
 *          `zap_read()`, and `zap_write()`.
 *
 *
 * Disconnecting mechanism over GNI SMSG
 * -------------------------------------
 *
 * `ZAP_UGNI_MSG_TERM` is a message to notify the peer that the local process
 * wants to terminate the connection. The local process shall not send any more
 * messages other than `ZAP_UGNI_MSG_ACK_TERM` to acknowledge the
 * `ZAP_UGNI_MSG_TERM` that the peer may also send. When the peer replies with
 * `ZAP_UGNI_MSG_ACK_TERM`, the peer will not to send any further messages and
 * the local process can deem the endpoint DISCONNECTED. In the case that one
 * side actively close the connection, it plays out as follows.
 *
 * ```
 *                   .-------.                     .-------.
 *                   | peer0 |                     | peer1 |
 *                   '-------'                     '-------'
 *                       |                             |
 *          Send TERM(0) |            TERM(0)          |
 *                       |---------------------------->|--.
 *                       |                             |  | Process TERM(0)
 *                       |            TERM(1)          |  | -Send TERM(1)
 *                    .--|<----------------------------|<-' -Send ACK_TERM(0)
 *                    |  |                             |
 *  Process TERM(1)   |  |          ACK_TERM(0)        |
 *  -Send ACK_TERM(1) |  |<----------------------------|
 *                    |  |                             |
 *                    |  |                             |
 *                    |  |                             |
 *                    '->|---------------------------->|--.
 *                       |          ACK_TERM(1)        |  | Process ACK_TERM(1)
 *  Process ACK_TERM(0)  |                             |<-' -DISCONNECT
 *  -DISCONNECT       .--|                             |
 *                    |  |                             |
 *                    '->|                             |
 *                       |                             |
 *                       v                             v
 * ```
 *
 * - [peer0] calls `zap_close()`. The endpoint state is changed to CLOSE and
 *           SMSG-send `ZAP_UGNI_MSG_TERM(0)` to peer1. A timeout event is also
 *           added to zq to force-terminate the connection when the timeout
 *           occurs before `ZAP_UGNI_MSG_ACK_TERM(0)` is received.
 * - [peer1] SMSG-recv `ZAP_UGNI_MSG_TERM(0)`. The ep state is changed to
 *           PEER_CLOSE (to prevent future application send/read/write
 *           requests). Since peer1 has never sent `ZAP_UGNI_MSG_TERM(1)`,
 *           it send the message to peer0, expecting a
 *           `ZAP_UGNI_MSG_ACK_TERM(1)` back (with timeout). Then,
 *           `ZAP_UGNI_MSG_ACK_TERM(0)` is SMSG-sent to peer0 to acknowledge the
 *           TERM peer1 received from peer0. No messages will be sent any
 *           further from peer1.
 * - [peer0] SMSG-recv `ZAP_UGNI_MSG_TERM(1)`. peer0 does not send another
 *           `ZAP_UGNI_MSG_TERM(0)` because it knows that it has already sent its
 *           TERM message. Then, peer0 GNI-sends `ZAP_UGNI_MSG_ACK_TERM(1)` to
 *           peer1.
 * - [peer0+peer1] SMSG-recv `ZAP_UGNI_MSG_ACK_TERM(0)` and
 *                 `ZAP_UGNI_MSG_ACK_TERM(1)` respectively. The DISCONNECTED
 *                 event is then delivered to the application.
 *
 * In the case of both peers terminate the connection simultaneously, the
 * mechanism works the same way, except that both peers know that they have
 * alread sent TERM and won't have to send it when they receive TERM from peer.
 *
 *
 * ```
 *                   .-------.                     .-------.
 *                   | peer0 |                     | peer1 |
 *                   '-------'                     '-------'
 *                       |    TERM(0)        TERM(1)   |
 *          Send TERM(0) |--------.              .-----| Send TERM(1)
 *                       |         \            /      |
 *                       |          '----------/------>|--.
 *                       |<-------------------'        |  | Process TERM(0)
 *                    .--|                             |  | -Send ACK_TERM(0)
 *                    |  |                             |  |
 *  Process TERM(1)   |  |          ACK_TERM(0)        |  |
 *  -Send ACK_TERM(1) |  |<----------------------------|<-'
 *                    |  |                             |
 *                    |  |                             |
 *                    |  |                             |
 *                    '->|---------------------------->|--.
 *                       |          ACK_TERM(1)        |  | Process ACK_TERM(1)
 *  Process ACK_TERM(0)  |                             |<-' -DISCONNECT
 *  -DISCONNECT       .--|                             |
 *                    |  |                             |
 *                    '->|                             |
 *                       |                             |
 *                       v                             v
 * ```
 *
 * REMARK: GNI SMSG guarantees the order to process messages. The messages on
 *         the wire may arrive out of order, but `GNI_SmsgGetNext()` guarantees
 *         the order. For example, if `msgN+1` arrives but `msgN` has not
 *         arrived yet, `GNI_SmsgGetNext()` will return `GNI_RC_NOT_DONE`. When
 *         `msgN` arrives, the first call to `GNI_SmsgGetNext()` yields `msgN`,
 *         and the next call yields `msgN+1`. In `zap_ugni`,
 *         `z_ugni_handle_rcq_smsg()` keeps calling `GNI_SmsgGetNext()` and
 *         process the message until it returns `GNI_RC_NOT_DONE`.
 *
 *
 * NOTE on slow connection: `GNI_MemRegister()` with recv CQ for SMSG mbox took
 * 0.6-0.7 sec. Fortunately this occurs only once in each io thread. Short-lived
 * zap application like `ldms_ls` would see the effect of this slow connection
 * the most.
 *
 */

#define VERSION_FILE "/proc/version"

#define ZUGNI_LIST_REMOVE(elm, link) do { \
	LIST_REMOVE((elm), link); \
	(elm)->link.le_next = 0; \
	(elm)->link.le_prev = 0; \
} while(0)

#define __container_of(ptr, type, field) (((void*)(ptr)) - offsetof(type, field))

static char *format_4tuple(struct zap_ep *ep, char *str, size_t len)
{
	struct sockaddr la = {0};
	struct sockaddr ra = {0};
	char addr_str[INET_ADDRSTRLEN];
	struct sockaddr_in *l = (struct sockaddr_in *)&la;
	struct sockaddr_in *r = (struct sockaddr_in *)&ra;
	socklen_t sa_len = sizeof(la);
	size_t sz;

	(void) zap_get_name(ep, &la, &ra, &sa_len);
	sz = snprintf(str, len, "lcl=%s:%hu <--> ",
		inet_ntop(AF_INET, &l->sin_addr, addr_str, INET_ADDRSTRLEN),
		ntohs(l->sin_port));
	if (sz + 1 > len)
		return NULL;
	len -= sz;
	sz = snprintf(&str[sz], len, "rem=%s:%hu",
		inet_ntop(AF_INET, &r->sin_addr, addr_str, INET_ADDRSTRLEN),
		ntohs(r->sin_port));
	if (sz + 1 > len)
		return NULL;
	return str;
}

#define LOG_(uep, fmt, ...) do { \
	if ((uep) && (uep)->ep.z && (uep)->ep.z->log_fn) { \
		char name[ZAP_UGNI_EP_NAME_SZ]; \
		format_4tuple(&(uep)->ep, name, ZAP_UGNI_EP_NAME_SZ); \
		uep->ep.z->log_fn("zap_ugni: %s " fmt, name, ##__VA_ARGS__); \
	} \
} while(0);

#define LOG(...) do { \
	zap_ugni_log("zap_ugni: " __VA_ARGS__); \
} while(0);

/* log with file, function name, and line number */
#define LLOG(FMT, ...) do { \
	zap_ugni_log("zap_ugni: %s():%d " FMT, __func__, __LINE__, ##__VA_ARGS__); \
} while(0)

#ifdef DEBUG
#define DLOG_(uep, fmt, ...) do { \
	if ((uep) && (uep)->ep.z && (uep)->ep.z->log_fn) { \
		char name[ZAP_UGNI_EP_NAME_SZ]; \
		format_4tuple(&(uep)->ep, name, ZAP_UGNI_EP_NAME_SZ); \
		uep->ep.z->log_fn("zap_ugni [DEBUG]: %s " fmt, name, ##__VA_ARGS__); \
	} \
} while(0);

#define DLOG(...) do { \
	zap_ugni_log("zap_ugni [DEBUG]: " __VA_ARGS__); \
} while(0);
#else
#define DLOG_(UEP, ...)
#define DLOG(...)
#endif

#if 0
#define CONN_DEBUG
#endif
#ifdef CONN_DEBUG
#define CONN_LOG(FMT, ...) do { \
	struct timespec __t; \
	clock_gettime(CLOCK_REALTIME, &__t); \
	LLOG( "[CONN_LOG] [%ld.%09ld] " FMT, __t.tv_sec, __t.tv_nsec, ## __VA_ARGS__); \
} while (0)
#define zap_get_ep(ep) do { \
	LLOG("zap_get_ep(%p), ref_count: %d\n", ep, (ep)->ref_count); \
	zap_get_ep(ep); \
} while (0)

#define zap_put_ep(ep) do { \
	LLOG("zap_put_ep(%p), ref_count: %d\n", ep, (ep)->ref_count); \
	zap_put_ep(ep); \
} while (0)
#else
#define CONN_LOG(...)
#endif

/* For LOCK/UNLOCK GNI API calls.
 *
 * NOTE:
 * - When using no GNI API lock, ldmsd aggregator run for a little while and got
 *   SIGSEGV or SIGABRT, even with just 1 zap_io_thread. Seems like ldmsd
 *   updtr thread (posting RDMA read) and zap_io_thread race to modify the
 *   completion queue.
 * - When using Z_GNI_API_THR_LOCK which use zap_io_thread->mutex to protect GNI
 *   API calls, the case of 1 zap_io_thread works. However, when we have
 *   multiple zap_io_threads (1 local CQ per thread), the aggregator hangs in
 *   GNII_DlaDrain(). Since all CQs share the same nic handle, even though the
 *   CQ is protected by our API lock, the resources in the nic handle would not
 *   be protected by them (two io threads can take their own locks and
 *   access/modify nic handle resources at the same time). And, it looks like
 *   the nic handle also needs mutex protection.
 * - When using Z_GNI_API_GLOBAL_LOCK, i.e. using our global `ugni_lock` to
 *   protect all GNI calls, everything seems to be working fine with multiple io
 *   threads. At the least, it apssed the following setup: 32 samps ->
 *   agg11 + agg12 -> agg2. When the samplers were killed/restarted, agg2 still
 *   get the updated data. When agg11+agg12 were killed/restarted, agg2 also get
 *   the updated data.
 */
#define Z_GNI_API_GLOBAL_LOCK
#if defined Z_GNI_API_THR_LOCK
/* use io_thread lock */
#define Z_GNI_API_LOCK(thr) pthread_mutex_lock(&(thr)->mutex)
#define Z_GNI_API_UNLOCK(thr) pthread_mutex_unlock(&(thr)->mutex)
#elif defined Z_GNI_API_GLOBAL_LOCK
/* use global ugni_lock */
#define Z_GNI_API_LOCK(thr) pthread_mutex_lock(&ugni_lock)
#define Z_GNI_API_UNLOCK(thr) pthread_mutex_unlock(&ugni_lock)
#else
/* no GNI API lock */
#define Z_GNI_API_LOCK(thr)
#define Z_GNI_API_UNLOCK(thr)
#endif

int init_complete = 0;

static void zap_ugni_default_log(const char *fmt, ...);
static zap_log_fn_t zap_ugni_log = zap_ugni_default_log;

/* 100000 because the Cray node names have only 5 digits, e.g, nid00000  */
#define ZAP_UGNI_MAX_NUM_NODE 100000

/* objects for checking node states */
#define ZAP_UGNI_NODE_GOOD 7
static ovis_scheduler_t node_state_sched;
static pthread_t node_state_thread;

#ifdef DEBUG
#define ZAP_UGNI_RCA_LOG_THS 1
#else
#define ZAP_UGNI_RCA_LOG_THS 1
#endif /* DEBUG */
struct zap_ugni_node_state {
	unsigned long state_interval_us;
	unsigned long state_offset_us;
	int state_ready;
	int check_state;
	int rca_log_thresh;
	int rca_get_failed;
	int *node_state;
} _node_state = {0};

struct zap_ugni_defer_disconn_ev {
	struct z_ugni_ep *uep;
	struct event *disconn_ev;
	struct timeval retry_count; /* Retry unbind counter */
};

#define ZAP_UGNI_DISCONNECT_TIMEOUT	10
static int zap_ugni_disconnect_timeout;

/*
 * Maximum number of endpoints zap_ugni will handle
 */
#define ZAP_UGNI_MAX_NUM_EP 32000
static int zap_ugni_max_num_ep;
static uint32_t *zap_ugni_ep_id;

static pthread_mutex_t ugni_mh_lock;
gni_mem_handle_t global_mh;
int global_mh_initialized = 0;
static gni_return_t ugni_get_mh(zap_t z, gni_mem_handle_t *mh);

static pthread_t error_thread;

static void *error_thread_proc(void *arg);

static zap_err_t z_ugni_smsg_send(struct z_ugni_ep *uep, zap_ugni_msg_t msg,
			     const char *data, size_t data_len);

static int z_ugni_enable_sock(struct z_ugni_ep *uep);
static int z_ugni_disable_sock(struct z_ugni_ep *uep);

static int __get_nodeid(struct sockaddr *sa, socklen_t sa_len);
static int __check_node_state(int node_id);

static void z_ugni_destroy(zap_ep_t ep);

static LIST_HEAD(, z_ugni_ep) z_ugni_list = LIST_HEAD_INITIALIZER(0);
static pthread_mutex_t z_ugni_list_mutex = PTHREAD_MUTEX_INITIALIZER;

#define ZAP_UGNI_STALLED_TIMEOUT	60 /* 1 minute */
static int zap_ugni_stalled_timeout;

#ifdef DEBUG
static LIST_HEAD(, z_ugni_ep) deferred_list = LIST_HEAD_INITIALIZER(0);
static pthread_mutex_t deferred_list_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint32_t ugni_io_count = 0;
#endif /* DEBUG */
static uint32_t ugni_post_id __attribute__((unused));

static pthread_mutex_t ugni_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t inst_id_cond = PTHREAD_COND_INITIALIZER;

static int zap_ugni_dom_initialized = 0;
static struct zap_ugni_dom {
	zap_ugni_type_t type;
	uid_t euid;
	uint8_t ptag;
	uint32_t cookie;
	uint32_t pe_addr;
	uint32_t inst_id;
	gni_job_limits_t limits;
	gni_cdm_handle_t cdm;
	gni_nic_handle_t nic;
} _dom = {0};

static void z_ugni_flush(struct z_ugni_ep *uep);
static void z_ugni_zq_post(struct z_ugni_io_thread *thr, struct z_ugni_ev *uev);
static void z_ugni_zq_rm(struct z_ugni_io_thread *thr, struct z_ugni_ev *uev);
static void z_ugni_deliver_conn_error(struct z_ugni_ep *uep);
static void z_ugni_ep_release(struct z_ugni_ep *uep);
static void z_ugni_zq_try_post(struct z_ugni_ep *uep, uint64_t ts_msec, int type, int status);

static void zap_ugni_default_log(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

int z_rbn_cmp(void *a, void *b)
{
	uint32_t x = (uint32_t)(uint64_t)a;
	uint32_t y = (uint32_t)(uint64_t)b;
	return x - y;
}

const char *zap_ugni_msg_type_str(zap_ugni_msg_type_t type)
{
	if (type < ZAP_UGNI_MSG_NONE || ZAP_UGNI_MSG_TYPE_LAST < type)
		return "ZAP_UGNI_MSG_OUT_OF_RANGE";
	return __zap_ugni_msg_type_str[type];
}

const char *zap_ugni_type_str(zap_ugni_type_t type)
{
	if (type < ZAP_UGNI_TYPE_NONE || ZAP_UGNI_TYPE_LAST < type)
		return "ZAP_UGNI_TYPE_OUT_OF_RANGE";
	return __zap_ugni_type_str[type];
}

int gni_rc_to_errno(gni_return_t grc)
{
	switch (grc) {
	case GNI_RC_SUCCESS: return 0;
        case GNI_RC_NOT_DONE: return EAGAIN;
        case GNI_RC_INVALID_PARAM: return EINVAL;
        case GNI_RC_ERROR_RESOURCE: return ENOMEM;
        case GNI_RC_TIMEOUT: return ETIMEDOUT;
        case GNI_RC_PERMISSION_ERROR: return EACCES;
        case GNI_RC_DESCRIPTOR_ERROR: return EBADF;
        case GNI_RC_ALIGNMENT_ERROR: return EINVAL;
        case GNI_RC_INVALID_STATE: return EINVAL;
        case GNI_RC_NO_MATCH: return ENOENT;
        case GNI_RC_SIZE_ERROR: return EINVAL;
        case GNI_RC_TRANSACTION_ERROR: return EIO;
        case GNI_RC_ILLEGAL_OP: return ENOTSUP;
        case GNI_RC_ERROR_NOMEM: return ENOMEM;
	}
	return EINVAL;
}

static void z_ugni_ep_idx_init(struct z_ugni_io_thread *thr)
{
	struct z_ugni_ep_idx *ep_idx = thr->ep_idx;
	int i;
	for (i = 0; i < ZAP_UGNI_THREAD_EP_MAX; i++) {
		ep_idx[i].idx = i;
		ep_idx[i].next_idx = i+1;
		ep_idx[i].uep = NULL;
	}
	ep_idx[ZAP_UGNI_THREAD_EP_MAX-1].next_idx = 0;
	thr->ep_idx_head = &ep_idx[0];
	thr->ep_idx_tail = &ep_idx[ZAP_UGNI_THREAD_EP_MAX-1];
}

/* assign a free ep_idx to uep. Must hold thr->zap_io_thread.mutex */
static int z_ugni_ep_idx_assign(struct z_ugni_ep *uep)
{
	struct z_ugni_io_thread *thr = (void*)uep->ep.thread;
	struct z_ugni_ep_idx *ep_idx = thr->ep_idx;
	int idx;
	struct z_ugni_ep_idx *curr, *next;
	if (uep->ep_idx) {
		/* should not happen */
		LOG("%s() warning: uep->ep_idx is not NULL\n", __func__);
		assert(0 == "uep->ep_idx is NOT NULL");
		return 0;
	}
	idx = thr->ep_idx_head->next_idx;
	if (!idx)
		return ENOMEM;
	curr = &ep_idx[idx];
	next = &ep_idx[curr->next_idx];

	/* remove curr from the free list */
	thr->ep_idx_head->next_idx = next->idx;
	curr->next_idx = 0;

	if (next->idx == 0) { /* also reset tail if list depleted */
		thr->ep_idx_tail = thr->ep_idx_head;
	}

	/* assign curr to uep */
	zap_get_ep(&uep->ep);
	curr->uep = uep;
	uep->ep_idx = curr;

	CONN_LOG("%p got ep_idx %d (%p)\n", uep, curr->idx, curr);

	return 0;
}

/* release the ep_idx assigned to the uep, put it back into the free list.
 * Caller must hold thr->zap_io_thread.mutex */
static void z_ugni_ep_idx_release(struct z_ugni_ep *uep)
{
	struct z_ugni_io_thread *thr = (void*)uep->ep.thread;
	if (!uep->ep_idx) {
		LOG("%s(): warning: uep->ep_idx is NULL\n", __func__);
		assert(0 == "uep->ep_idx is NULL");
		return;
	}

	/* insert tail */
	thr->ep_idx_tail->next_idx = uep->ep_idx->idx;
	uep->ep_idx->next_idx = 0;
	/* update tail */
	thr->ep_idx_tail = uep->ep_idx;

	/* release uep/ep_idx */
	uep->ep_idx->uep = NULL;
	uep->ep_idx = NULL;

	zap_put_ep(&uep->ep);
}

/* uep->ep.lock must be held */
static int z_ugni_get_post_credit(struct z_ugni_ep *uep)
{
	if (STAILQ_EMPTY(&uep->pending_wrq) && uep->post_credit) {
		uep->post_credit--;
		return 1;
	}
	return 0;
}

/* uep->ep.lock must be held */
static void z_ugni_put_post_credit(struct z_ugni_ep *uep)
{
	uep->post_credit++;
}

/*
 * Use KEEP-ALIVE packets to shut down a connection if the remote peer fails
 * to respond for 10 minutes
 */
#define ZAP_SOCK_KEEPCNT	3	/* Give up after 3 failed probes */
#define ZAP_SOCK_KEEPIDLE	10	/* Start probing after 10s of inactivity */
#define ZAP_SOCK_KEEPINTVL	2	/* Probe couple seconds after idle */

static int __set_nonblock(int fd)
{
	int fl;
	int rc;
	fl = fcntl(fd, F_GETFL);
	if (fl == -1)
		return errno;
	rc = fcntl(fd, F_SETFL, fl | O_NONBLOCK);
	if (rc)
		return errno;
	return 0;
}

static int __set_sock_opts(int fd)
{
	int rc;

	/* nonblock */
	rc = __set_nonblock(fd);
	if (rc)
		return rc;

	return 0;
}

uint32_t zap_ugni_get_ep_gn(int id)
{
	return zap_ugni_ep_id[id];
}

int zap_ugni_is_ep_gn_matched(int id, uint32_t gn)
{
	if (zap_ugni_ep_id[id] == gn)
		return 1;
	return 0;
}

/* setting up mailboxes for GNI Smsg */
int z_ugni_io_thread_mbox_setup(struct z_ugni_io_thread *thr)
{
	gni_return_t grc;
	uint32_t sz;
	struct gni_smsg_attr attr;

	attr.msg_type = GNI_SMSG_TYPE_MBOX_AUTO_RETRANSMIT;
	attr.mbox_maxcredit = ZAP_UGNI_RCQ_DEPTH;
	attr.msg_maxsize = ZAP_UGNI_MSG_SZ_MAX;

	/* size of mbox/ep */
        grc = GNI_SmsgBufferSizeNeeded(&attr, &sz);
	if (grc) {
		LLOG("GNI_SmsgBufferSizeNeeded() error: %d\n", grc);
		errno = gni_rc_to_errno(grc);
		goto err_0;
	}
	thr->mbox_sz = ((sz - 1)|0x3f) + 1; /* align to cache line: 64 bytes */
	/* allocate mailboxes serving endpoints in this thread */
	sz = ZAP_UGNI_THREAD_EP_MAX * thr->mbox_sz;
	thr->mbox = malloc(sz);
	if (!thr->mbox) {
		LLOG("malloc() error: %d\n", errno);
		goto err_0;
	}

	return 0;

 err_0:
	return errno;
}

/* releasing resources for GNI SMSG mailbox */
void z_ugni_mbox_release(struct z_ugni_ep *uep)
{
}

/*
 * Caller must hold the z_ugni_list_mutex lock;
 */
int zap_ugni_get_ep_id()
{
	static uint32_t current_gn = 0;
	static int idx = -1;
	int count = -1;
	do {
		++count;
		if (count == zap_ugni_max_num_ep) {
			/*
			 * All slots have been occupied.
			 */
			LOG("Not enough endpoint slots. "
				"Considering setting the ZAP_UGNI_MAX_NUM_EP"
				"environment variable to a larger number.\n");
			return -1;
		}

		++idx;
		if (idx >= zap_ugni_max_num_ep)
			idx = 0;
	} while (zap_ugni_ep_id[idx]);
	zap_ugni_ep_id[idx] = ++current_gn;
	return idx;
}

/* Must be called with the endpoint lock held */
static struct z_ugni_wr *z_ugni_alloc_send_wr(struct z_ugni_ep *uep,
					 zap_ugni_msg_t msg,
					 const char *data, size_t data_len)
{
	int hdr_sz;

	switch (ntohs(msg->hdr.msg_type)) {
	case ZAP_UGNI_MSG_CONNECT:
		hdr_sz = sizeof(msg->connect);
		msg->connect.data_len = htonl(data_len);
		break;
	case ZAP_UGNI_MSG_RENDEZVOUS:
		hdr_sz = sizeof(msg->rendezvous);
		break;
	case ZAP_UGNI_MSG_ACCEPTED:
		hdr_sz = sizeof(msg->accepted);
		msg->accepted.data_len = htonl(data_len);
		break;
	case ZAP_UGNI_MSG_ACK_ACCEPTED: /* use `regular` format */
	case ZAP_UGNI_MSG_REJECTED: /* use `regular` format */
	case ZAP_UGNI_MSG_TERM: /* use `regular` format */
	case ZAP_UGNI_MSG_ACK_TERM: /* use `regular` format */
	case ZAP_UGNI_MSG_REGULAR:
		hdr_sz = sizeof(msg->regular);
		msg->regular.data_len = htonl(data_len);
		break;
	default:
		LLOG("WARNING: Invalid send message.\n");
		errno = EINVAL;
		return NULL;
	}
	msg->hdr.msg_len = htonl(hdr_sz + data_len);

	struct z_ugni_wr *wr = malloc(sizeof(*wr) +
				      sizeof(*wr->send_wr) +
				      hdr_sz + data_len);
	if (!wr)
		return NULL;
	wr->type = Z_UGNI_WR_SMSG;
	wr->state = Z_UGNI_WR_INIT;
	wr->send_wr->msg_len = hdr_sz + data_len;
	memcpy(wr->send_wr->msg, msg, hdr_sz);
	if (data && data_len)
		memcpy(((void*)wr->send_wr->msg) + hdr_sz, data, data_len);
	wr->send_wr->msg_id = (uep->ep_idx->idx << 16)|(uep->next_msg_seq++);
	return wr;
}

static void z_ugni_free_send_wr(struct z_ugni_wr *wr)
{
	free(wr);
}

/* Must be called with the endpoint lock held */
static struct z_ugni_wr *z_ugni_alloc_post_desc(struct z_ugni_ep *uep)
{
	struct zap_ugni_post_desc *d;
	struct z_ugni_wr *wr = calloc(1, sizeof(*wr) + sizeof(*wr->post_desc));
	if (!wr)
		return NULL;
	wr->type = Z_UGNI_WR_RDMA;
	wr->state = Z_UGNI_WR_INIT;
	d = wr->post_desc;
	d->uep = uep;
	//zap_get_ep(&uep->ep);
#ifdef DEBUG
	d->ep_gn = zap_ugni_get_ep_gn(uep->ep_id);
#endif /* DEBUG */
	format_4tuple(&uep->ep, d->ep_name, ZAP_UGNI_EP_NAME_SZ);
	//LIST_INSERT_HEAD(&uep->post_desc_list, d, ep_link);
	return wr;
}

static void z_ugni_free_post_desc(struct z_ugni_wr *wr)
{
	assert(wr->type == Z_UGNI_WR_RDMA);
	free(wr);
}

static gni_return_t ugni_get_mh(zap_t z, gni_mem_handle_t *mh)
{
	gni_return_t grc = GNI_RC_SUCCESS;
	zap_mem_info_t mmi;

	if (__builtin_expect(global_mh_initialized, 1)) {
		*mh = global_mh;
		return GNI_RC_SUCCESS;
	}

	pthread_mutex_lock(&ugni_mh_lock);
	if (global_mh_initialized) {
		/* the other thread won the race */
		goto out;
	}
	mmi = z->mem_info_fn();
	grc = GNI_MemRegister(_dom.nic, (uint64_t)mmi->start, mmi->len,
			      NULL,
			      GNI_MEM_READWRITE | GNI_MEM_RELAXED_PI_ORDERING,
			      -1, &global_mh);
	if (grc != GNI_RC_SUCCESS)
		goto err;
	global_mh_initialized = 1;
 out:
	*mh = global_mh;
 err:
	pthread_mutex_unlock(&ugni_mh_lock);
	return grc;
}

/* The caller must hold the endpoint lock */
static void z_ugni_ep_error(struct z_ugni_ep *uep)
{
	if (uep->sock >= 0)
		shutdown(uep->sock, SHUT_RDWR);
	z_ugni_ep_release(uep);
	switch (uep->ep.state) {
	case ZAP_EP_CONNECTED:
		z_ugni_zq_try_post(uep, 0, ZAP_EVENT_DISCONNECTED, ZAP_ERR_ENDPOINT);
	case ZAP_EP_CLOSE:
	case ZAP_EP_PEER_CLOSE:
		z_ugni_zq_try_post(uep, 0, uep->zap_connected?ZAP_EVENT_DISCONNECTED:ZAP_EVENT_CONNECT_ERROR, ZAP_ERR_ENDPOINT);
		break;
	case ZAP_EP_CONNECTING:
		/* need to remove the connect timeout event first */
		z_ugni_zq_rm((void*)uep->ep.thread, &uep->uev);
		/* let through */
	case ZAP_EP_ACCEPTING:
		z_ugni_zq_try_post(uep, 0, ZAP_EVENT_CONNECT_ERROR, ZAP_ERR_ENDPOINT);
		break;
	default:
		break;
	}
	uep->ep.state = ZAP_EP_ERROR;
}

void z_ugni_cleanup(void)
{
	if (node_state_sched)
		ovis_scheduler_term(node_state_sched);

	if (node_state_thread) {
		pthread_cancel(node_state_thread);
		pthread_join(node_state_thread, NULL);
	}

	if (node_state_sched) {
		ovis_scheduler_free(node_state_sched);
		node_state_sched = NULL;
	}

	if (_node_state.node_state)
		free(_node_state.node_state);

	if (zap_ugni_ep_id)
		free(zap_ugni_ep_id);
}

static void z_ugni_ep_release(struct z_ugni_ep *uep)
{
	gni_return_t grc;
	if (uep->gni_ep) {
#if 0
		grc = GNI_EpUnbind(uep->gni_ep);
		if (grc)
			LLOG("GNI_EpUnbind() error: %s\n", gni_ret_str(grc));
#endif
		Z_GNI_API_LOCK(uep->ep.thread);
		grc = GNI_EpDestroy(uep->gni_ep);
		Z_GNI_API_UNLOCK(uep->ep.thread);
		if (grc != GNI_RC_SUCCESS)
			LLOG("GNI_EpDestroy() error: %s\n", gni_ret_str(grc));
		uep->gni_ep = NULL;
	}
}

uint64_t __ts_msec(uint64_t delay_msec)
{
	struct timespec ts;
	uint64_t ts_msec;
	clock_gettime(CLOCK_REALTIME, &ts);
	ts_msec = ts.tv_sec*1000 + ts.tv_nsec/1000000 + delay_msec;
	return ts_msec;
}

/*
 * Telling peer that we're terminating the connection. When peer ACK_TERM, we
 * can safely release the mbox (peer won't send anything after ACK_TERM).
 *
 * If peer won't ACK within TIMEOUT ... the connection will be force-terminated.
 */
static zap_err_t z_ugni_send_term(struct z_ugni_ep *uep)
{
	struct zap_ugni_msg msg = {.hdr.msg_type = htons(ZAP_UGNI_MSG_TERM)};
	uint64_t ts_msec;
	zap_err_t zerr = ZAP_ERR_OK;
	if (uep->ugni_term_sent) {
		goto out;
	}
	uep->ugni_term_sent = 1;
	zerr = z_ugni_smsg_send(uep, &msg, NULL, 0);
	if (zerr == ZAP_ERR_OK) {
		/* timeout */
		ts_msec = __ts_msec(zap_ugni_disconnect_timeout*1000);
		if (uep->ep.state == ZAP_EP_CONNECTED) {
			z_ugni_zq_try_post(uep, ts_msec, ZAP_EVENT_DISCONNECTED, 0);
		} else {
			z_ugni_zq_try_post(uep, ts_msec, ZAP_EVENT_CONNECT_ERROR,
					ZAP_ERR_ENDPOINT);
		}
	} else {
		/* could not send, immediate disconnect/conn_error */
		z_ugni_ep_error(uep);
	}
 out:
	return zerr;
}

static zap_err_t z_ugni_send_ack_term(struct z_ugni_ep *uep)
{
	struct zap_ugni_msg msg = {.hdr.msg_type = htons(ZAP_UGNI_MSG_ACK_TERM)};
	zap_err_t zerr;
	if (uep->ugni_ack_term_sent) {
		LLOG("WARNING: Multiple sends of ACK_TERM message.\n");
		goto out;
	}
	zerr = z_ugni_smsg_send(uep, &msg, NULL, 0);
	if (zerr != ZAP_ERR_OK)
		return zerr;
	uep->ugni_ack_term_sent = 1;
 out:
	return ZAP_ERR_OK;
}

static zap_err_t z_ugni_close(zap_ep_t ep)
{
	struct z_ugni_ep *uep = (struct z_ugni_ep *)ep;

	DLOG_(uep, "Closing xprt: %p, state: %s\n", uep,
			__zap_ep_state_str(uep->ep.state));
	pthread_mutex_lock(&uep->ep.lock);
	switch (uep->ep.state) {
	case ZAP_EP_LISTENING:
		z_ugni_disable_sock(uep);
		uep->ep.state = ZAP_EP_CLOSE;
		break;
	case ZAP_EP_CONNECTED:
		z_ugni_send_term(uep);
		uep->ep.state = ZAP_EP_CLOSE;
		break;
	case ZAP_EP_CONNECTING:
	case ZAP_EP_ACCEPTING:
	case ZAP_EP_PEER_CLOSE:
		if (uep->ugni_ep_bound) {
			z_ugni_send_term(uep);
		} else {
			z_ugni_disable_sock(uep);
		}
		uep->ep.state = ZAP_EP_CLOSE;
		break;
	case ZAP_EP_ERROR:
	case ZAP_EP_CLOSE:
		/* don't change state */
		break;
	default:
		ZAP_ASSERT(0, ep, "%s: Unexpected state '%s'\n",
				__func__, __zap_ep_state_str(ep->state));
	}
	uep->ep.state = ZAP_EP_CLOSE;
	pthread_mutex_unlock(&uep->ep.lock);
	return ZAP_ERR_OK;
}

static zap_err_t z_get_name(zap_ep_t ep, struct sockaddr *local_sa,
			    struct sockaddr *remote_sa, socklen_t *sa_len)
{
	struct z_ugni_ep *uep = (void*)ep;
	int rc;
	*sa_len = sizeof(struct sockaddr_in);
	rc = getsockname(uep->sock, local_sa, sa_len);
	if (rc)
		goto err;
	rc = getpeername(uep->sock, remote_sa, sa_len);
	if (rc)
		goto err;
	return ZAP_ERR_OK;
err:
	return zap_errno2zerr(errno);
}

static zap_err_t z_ugni_connect(zap_ep_t ep,
				struct sockaddr *sa, socklen_t sa_len,
				char *data, size_t data_len)
{
	int rc;
	zap_err_t zerr;
	struct z_ugni_ep *uep = (void*)ep;
	zerr = zap_ep_change_state(&uep->ep, ZAP_EP_INIT, ZAP_EP_CONNECTING);
	if (zerr)
		goto err_0;

	if (_node_state.check_state) {
		if (uep->node_id == -1)
			uep->node_id = __get_nodeid(sa, sa_len);
		if (uep->node_id != -1) {
			if (__check_node_state(uep->node_id)) {
				DLOG("Node %d is in a bad state\n", uep->node_id);
				zerr = ZAP_ERR_CONNECT;
				goto err_0;
			}
		}
	}

	uep->sock = socket(sa->sa_family, SOCK_STREAM, 0);
	if (uep->sock == -1) {
		zerr = ZAP_ERR_RESOURCE;
		goto err_0;
	}

	if (__set_sock_opts(uep->sock)) {
		zerr = ZAP_ERR_TRANSPORT;
		goto err_0;
	}

	if (data_len) {
		uep->conn_data = malloc(data_len);
		if (uep->conn_data) {
			memcpy(uep->conn_data, data, data_len);
		} else {
			zerr = ZAP_ERR_RESOURCE;
			goto err_0;
		}
		uep->conn_data_len = data_len;
	}
	zap_get_ep(&uep->ep); /* Release when disconnect/conn_error/rejected */
	rc = connect(uep->sock, sa, sa_len);
	if (rc && errno != EINPROGRESS) {
		zerr = ZAP_ERR_CONNECT;
		goto err_1;
	}

	zerr = zap_io_thread_ep_assign(&uep->ep);
	if (zerr)
		goto err_1;
	rc = z_ugni_enable_sock(uep);
	if (rc) {
		zerr = zap_errno2zerr(errno);
		goto err_2;
	}

	return 0;
 err_2:
	zap_io_thread_ep_release(&uep->ep);
 err_1:
	free(uep->conn_data);
	uep->conn_data = NULL;
	uep->conn_data_len = 0;
	zap_put_ep(&uep->ep);
 err_0:
	if (uep->sock >= 0) {
		close(uep->sock);
		uep->sock = -1;
	}
	return zerr;
}

/**
 * Process an unknown message in the end point.
 */
static void process_uep_msg_unknown(struct z_ugni_ep *uep)
{
	LOG_(uep, "zap_ugni: Unknown zap message.\n");
	pthread_mutex_lock(&uep->ep.lock);
	z_ugni_ep_error(uep);
	pthread_mutex_unlock(&uep->ep.lock);
}

/**
 * Receiving a regular message.
 */
static void process_uep_msg_regular(struct z_ugni_ep *uep)
{
	struct zap_ugni_msg_regular *msg;
	struct zap_event ev = {
			.ep = &uep->ep,
			.type = ZAP_EVENT_RECV_COMPLETE,
	};

	msg = (void*)uep->rmsg;
	ev.data = (void*)msg->data;
	ev.data_len = ntohl(msg->data_len);
	uep->ep.cb(&uep->ep, &ev);
	return;
}

/**
 * Receiving a rendezvous (share) message.
 */
static void process_uep_msg_rendezvous(struct z_ugni_ep *uep)
{
	struct zap_ugni_msg_rendezvous *msg;

	msg = (void*)uep->rmsg;

	msg->hdr.msg_len = ntohl(msg->hdr.msg_len);
	msg->hdr.reserved = 0;
	msg->hdr.msg_type = ntohs(msg->hdr.msg_type);
	msg->map_addr = be64toh(msg->map_addr);
	msg->acc = ntohl(msg->acc);
	msg->map_len = ntohl(msg->map_len);
	msg->gni_mh.qword1 = be64toh(msg->gni_mh.qword1);
	msg->gni_mh.qword2 = be64toh(msg->gni_mh.qword2);

	struct zap_ugni_map *map = calloc(1, sizeof(*map));
	if (!map) {
		LOG_(uep, "ENOMEM in %s at %s:%d\n",
				__func__, __FILE__, __LINE__);
		goto err0;
	}

	char *amsg = NULL;
	size_t amsg_len = msg->hdr.msg_len - sizeof(msg);
	if (amsg_len) {
		amsg = msg->msg; /* attached message from rendezvous */
	}

	map->map.ref_count = 1;
	map->map.ep = (void*)uep;
	map->map.acc = msg->acc;
	map->map.type = ZAP_MAP_REMOTE;
	map->map.addr = (void*)msg->map_addr;
	map->map.len = msg->map_len;
	map->gni_mh = msg->gni_mh;

	zap_get_ep(&uep->ep);
	pthread_mutex_lock(&uep->ep.lock);
	LIST_INSERT_HEAD(&uep->ep.map_list, &map->map, link);
	pthread_mutex_unlock(&uep->ep.lock);

	struct zap_event ev = {
		.type = ZAP_EVENT_RENDEZVOUS,
		.map = (void*)map,
		.data_len = amsg_len,
		.data = (void*)amsg
	};

	uep->ep.cb((void*)uep, &ev);
	return;

err0:
	return;
}

/* uep->ep.lock must be held.
 * msg is in NETWORK byte order.
 * The data length and the message lenght are conveniently set by this function.
 */
static zap_err_t z_ugni_smsg_send(struct z_ugni_ep *uep, zap_ugni_msg_t msg,
			     const char *data, size_t data_len)
{
	gni_return_t grc;
	struct z_ugni_wr *wr;

	/* need to keep send buf until send completion */
	wr = z_ugni_alloc_send_wr(uep, msg, data, data_len);
	if (!wr)
		goto err;

	if (!z_ugni_get_post_credit(uep))
		goto pending;

	Z_GNI_API_LOCK(uep->ep.thread);
	grc = GNI_SmsgSend(uep->gni_ep, wr->send_wr->msg, wr->send_wr->msg_len,
			NULL, 0, wr->send_wr->msg_id);
	Z_GNI_API_UNLOCK(uep->ep.thread);
	switch (grc) {
	case GNI_RC_SUCCESS:
		wr->state = Z_UGNI_WR_SUBMITTED;
		STAILQ_INSERT_TAIL(&uep->submitted_wrq, wr, entry);
		CONN_LOG("%p smsg sent %s\n", uep, zap_ugni_msg_type_str(ntohs(msg->hdr.msg_type)));
		goto out;
	case GNI_RC_NOT_DONE: /* not enough smsg credit */
		z_ugni_put_post_credit(uep);
		goto pending;
	default:
		goto err;
	}

 pending:
	wr->state = Z_UGNI_WR_PENDING;
	STAILQ_INSERT_TAIL(&uep->pending_wrq, wr, entry);
	CONN_LOG("%p pending smsg %s\n", uep, zap_ugni_msg_type_str(ntohs(msg->hdr.msg_type)));
 out:
	return ZAP_ERR_OK;
 err:
	return ZAP_ERR_ENDPOINT;
}

/* uep->ep.lock is held */
static zap_err_t z_ugni_send_connect(struct z_ugni_ep *uep)
{
	struct zap_ugni_msg msg;
	zap_err_t zerr;
	uint64_t ts_msec;

	msg.hdr.msg_type = htons(ZAP_UGNI_MSG_CONNECT);
	msg.connect.ep_desc.inst_id = htonl(_dom.inst_id);
	msg.connect.ep_desc.pe_addr = htonl(_dom.pe_addr);
	msg.connect.ep_desc.smsg_attr = uep->local_smsg_attr;
	memcpy(msg.connect.sig, ZAP_UGNI_SIG, sizeof(ZAP_UGNI_SIG));
	ZAP_VERSION_SET(msg.connect.ver);

	zerr = z_ugni_smsg_send(uep, &msg, uep->conn_data, uep->conn_data_len);
	if (zerr)
		return zerr;
	/* Post the connect timeout event. The event is removed when the
	 * connection is established */
	ts_msec = __ts_msec(zap_ugni_disconnect_timeout*1000);
	z_ugni_zq_try_post(uep, ts_msec, ZAP_EVENT_CONNECT_ERROR, ZAP_ERR_ENDPOINT);
	return ZAP_ERR_OK;
}

static void process_uep_msg_accepted(struct z_ugni_ep *uep)
{
	ZAP_ASSERT(uep->ep.state == ZAP_EP_CONNECTING, &uep->ep,
			"%s: Unexpected state '%s'. "
			"Expected state 'ZAP_EP_CONNECTING'\n",
			__func__, __zap_ep_state_str(uep->ep.state));
	struct zap_ugni_msg_accepted *msg;

	msg = (void*)uep->rmsg;

	msg->hdr.msg_len = ntohl(msg->hdr.msg_len);
	msg->hdr.reserved = 0;
	msg->hdr.msg_type = ntohs(msg->hdr.msg_type);
	msg->data_len = ntohl(msg->data_len);
	msg->ep_desc.inst_id = ntohl(msg->ep_desc.inst_id);
	msg->ep_desc.pe_addr = ntohl(msg->ep_desc.pe_addr);

	DLOG_(uep, "ACCEPTED received: pe_addr: %#x, inst_id: %#x\n",
			msg->ep_desc.pe_addr, msg->ep_desc.inst_id);

#ifdef ZAP_UGNI_DEBUG
	char *is_exit = getenv("ZAP_UGNI_CONN_EST_BEFORE_ACK_N_BINDING_TEST");
	if (is_exit)
		exit(0);
#endif /* ZAP_UGNI_DEBUG */

#ifdef ZAP_UGNI_DEBUG
	is_exit = getenv("ZAP_UGNI_CONN_EST_BEFORE_ACK_AFTER_BINDING_TEST");
	if (is_exit)
		exit(0);
#endif /* ZAP_UGNI_DEBUG */

	struct zap_event ev = {
		.ep = &uep->ep,
		.type = ZAP_EVENT_CONNECTED,
		.data_len = msg->data_len,
		.data = (msg->data_len ? (void*)msg->data : NULL)
	};
	/* need to remove the connect timeout event first */
	z_ugni_zq_rm((void*)uep->ep.thread, &uep->uev);
	if (!zap_ep_change_state(&uep->ep, ZAP_EP_CONNECTING, ZAP_EP_CONNECTED)) {
		uep->ep.cb((void*)uep, &ev);
	} else {
		LOG_(uep, "'Accept' message received in unexpected state %d.\n",
		     uep->ep.state);
		goto err;
	}
	return;

err:
	return;
}

static void process_uep_msg_connect(struct z_ugni_ep *uep)
{
	struct zap_ugni_msg_connect *msg;

	msg = (void*)uep->rmsg;

	pthread_mutex_lock(&uep->ep.lock);

	if (!ZAP_VERSION_EQUAL(msg->ver)) {
		LOG_(uep, "zap_ugni: Receive conn request "
				"from an unsupported version "
				"%hhu.%hhu.%hhu.%hhu\n",
				msg->ver.major, msg->ver.minor,
				msg->ver.patch, msg->ver.flags);
		goto err1;
	}

	if (memcmp(msg->sig, ZAP_UGNI_SIG, sizeof(msg->sig))) {
		LOG_(uep, "Expecting sig '%s', but got '%.*s'.\n",
				ZAP_UGNI_SIG, sizeof(msg->sig), msg->sig);
		goto err1;

	}

	msg->hdr.msg_len = ntohl(msg->hdr.msg_len);
	msg->hdr.reserved = 0;
	msg->hdr.msg_type = ntohs(msg->hdr.msg_type);
	msg->data_len = ntohl(msg->data_len);
	msg->ep_desc.inst_id = ntohl(msg->ep_desc.inst_id);
	msg->ep_desc.pe_addr = ntohl(msg->ep_desc.pe_addr);
	msg->ep_desc.remote_event = ntohl(msg->ep_desc.remote_event);

	DLOG_(uep, "CONN_REQ received: pe_addr: %#x, inst_id: %#x\n",
			msg->ep_desc.pe_addr, msg->ep_desc.inst_id);
	uep->app_owned = 1;
	pthread_mutex_unlock(&uep->ep.lock);

	struct zap_event ev = {
		.ep = &uep->ep,
		.type = ZAP_EVENT_CONNECT_REQUEST,
		.data_len = msg->data_len,
		.data = (msg->data_len)?((void*)msg->data):(NULL)
	};
	uep->ep.cb(&uep->ep, &ev);
	if (uep->app_accepted) {
		zap_err_t zerr = zap_ep_change_state(&uep->ep, ZAP_EP_ACCEPTING, ZAP_EP_CONNECTED);
		assert(zerr == ZAP_ERR_OK);
		if (zerr == ZAP_ERR_OK) {
			CONN_LOG("%p delivering ZAP_EVENT_CONNECTED\n", uep);
		}
		ev.type = ZAP_EVENT_CONNECTED;
		ev.data_len = 0;
		ev.data = NULL;
		uep->ep.cb(&uep->ep, &ev);
	}

	return;
err1:
	pthread_mutex_unlock(&uep->ep.lock);
	return;
}

static void process_uep_msg_rejected(struct z_ugni_ep *uep)
{
	struct zap_ugni_msg_regular *msg;
	int rc;
	size_t data_len;

	msg = (void*)uep->rmsg;
	data_len = ntohl(msg->data_len);
	struct zap_event ev = {
		.ep = &uep->ep,
		.type = ZAP_EVENT_REJECTED,
		.data_len = data_len,
		.data = (data_len ? (void *)msg->data : NULL)
	};
	/* need to remove the connect timeout event first */
	z_ugni_zq_rm((void*)uep->ep.thread, &uep->uev);
	rc = zap_ep_change_state(&uep->ep, ZAP_EP_CONNECTING, ZAP_EP_ERROR);
	if (rc != ZAP_ERR_OK) {
		return;
	}
	zap_event_deliver(&ev);
	return;
}

static void process_uep_msg_ack_accepted(struct z_ugni_ep *uep)
{
	int rc;

	rc = zap_ep_change_state(&uep->ep, ZAP_EP_ACCEPTING, ZAP_EP_CONNECTED);
	if (rc != ZAP_ERR_OK) {
		return;
	}
	struct zap_event ev = {
		.type = ZAP_EVENT_CONNECTED
	};
	zap_get_ep(&uep->ep); /* Release when receive disconnect/error event */
	uep->ep.cb(&uep->ep, &ev);
	return;
}

static void process_uep_msg_term(struct z_ugni_ep *uep)
{
	zap_err_t zerr = ZAP_ERR_OK;
	pthread_mutex_lock(&uep->ep.lock);
	if (uep->ugni_term_recv) {
		LLOG("ERROR: Multiple TERM messages received\n");
		goto out;
	}
	uep->ugni_term_recv = 1;
	switch (uep->ep.state) {
	case ZAP_EP_CONNECTED:
		/* send our term, then ack peer's term */
		zerr = z_ugni_send_term(uep);
		zerr = zerr?zerr:z_ugni_send_ack_term(uep);
		uep->ep.state = ZAP_EP_PEER_CLOSE;
		break;
	case ZAP_EP_CLOSE:
		zerr = z_ugni_send_ack_term(uep);
		break;
	case ZAP_EP_CONNECTING:
		/* need to remove the connect timeout event first */
		z_ugni_zq_rm((void*)uep->ep.thread, &uep->uev);
		/* let through */
	case ZAP_EP_ACCEPTING:
		/* peer close before becoming CONNECTED */
		/* send our term, then ack peer's term */
		zerr = z_ugni_send_term(uep);
		zerr = zerr?zerr:z_ugni_send_ack_term(uep);
		uep->ep.state = ZAP_EP_ERROR;
		break;
	case ZAP_EP_ERROR:
		/* do nothing */
		break;
	default:
		LLOG("ERROR: Unexpected TERM message while endpoint "
		     "in state %s (%d)\n",
		     __zap_ep_state_str(uep->ep.state),
		     uep->ep.state);
		break;
	}
	if (zerr) {
		uep->ep.state = ZAP_EP_ERROR;
	}
 out:
	pthread_mutex_unlock(&uep->ep.lock);
}

static void process_uep_msg_ack_term(struct z_ugni_ep *uep)
{
	pthread_mutex_lock(&uep->ep.lock);
	if (!uep->uev.in_zq) {
		/* already timeout, skip the processing */
		LLOG("INFO: uev timed out ... skipping\n");
		goto err;
	}
	if (!uep->ugni_term_sent) {
		LLOG("ERROR: TERM has not been sent but received ACK_TERM\n");
		goto err;
	}
	if (uep->ugni_ack_term_recv) {
		LLOG("ERROR: Multiple ACK_TERM messages received.\n");
		goto err;
	}
	uep->ugni_ack_term_recv = 1;
	/* validate ep state */
	switch (uep->ep.state) {
	case ZAP_EP_CLOSE:
	case ZAP_EP_PEER_CLOSE:
	case ZAP_EP_ERROR:
		/* OK */
		break;
	default:
		LLOG("ERROR: Unexpected ACK_TERM message while endpoint "
		     "in state %s (%d)\n",
		     __zap_ep_state_str(uep->ep.state),
		     uep->ep.state);
		goto err;
	}
	/*
	 * NOTE: This does not race with z_ugni_handle_zq_events() since it is
	 * on the same thread. However, we still need the thread->mutex (in
	 * z_ugni_zq_rm()) to protect the tree from node insertion from the
	 * application thread (simultaneous tree modification may corrupt the
	 * tree).
	 */
	z_ugni_zq_rm((void*)uep->ep.thread, &uep->uev);
	z_ugni_flush(uep);
	pthread_mutex_unlock(&uep->ep.lock);
	zap_io_thread_ep_release(&uep->ep);
	CONN_LOG("%p delivering last event: %s\n", uep, zap_event_str(uep->uev.zev.type));
	zap_event_deliver(&uep->uev.zev);
	zap_put_ep(&uep->ep); /* taken in z_ugni_connect()/z_ugni_accept() */
	return;

 err:
	pthread_mutex_unlock(&uep->ep.lock);
}

typedef void(*process_uep_msg_fn_t)(struct z_ugni_ep*);
process_uep_msg_fn_t process_uep_msg_fns[] = {
	[ZAP_UGNI_MSG_REGULAR]      =  process_uep_msg_regular,
	[ZAP_UGNI_MSG_RENDEZVOUS]   =  process_uep_msg_rendezvous,
	[ZAP_UGNI_MSG_ACCEPTED]     =  process_uep_msg_accepted,
	[ZAP_UGNI_MSG_CONNECT]      =  process_uep_msg_connect,
	[ZAP_UGNI_MSG_REJECTED]     =  process_uep_msg_rejected,
	[ZAP_UGNI_MSG_ACK_ACCEPTED] =  process_uep_msg_ack_accepted,
	[ZAP_UGNI_MSG_TERM]         =  process_uep_msg_term,
	[ZAP_UGNI_MSG_ACK_TERM]     =  process_uep_msg_ack_term,
};

#define min_t(t, x, y) (t)((t)x < (t)y?(t)x:(t)y)

int zap_ugni_err_handler(gni_cq_handle_t cq, gni_cq_entry_t cqe,
			 struct zap_ugni_post_desc *desc)
{
	uint32_t recoverable = 0;
	char errbuf[512];
	gni_return_t grc = GNI_CqErrorRecoverable(cqe, &recoverable);
	if (grc) {
		zap_ugni_log("GNI_CqErrorRecoverable returned %d\n", grc);
		recoverable = 0;
	}
	grc = GNI_CqErrorStr(cqe, errbuf, sizeof(errbuf));
	if (grc)
		zap_ugni_log("GNI_CqErrorStr returned %d\n", grc);
	else
		zap_ugni_log("GNI cqe Error : %s\n", errbuf);
	return recoverable;
}

static zap_thrstat_t ugni_stats;
#define WAIT_5SECS 5000

static void *error_thread_proc(void *args)
{
	gni_err_handle_t err_hndl;
	gni_error_event_t ev;
	gni_return_t status;
	uint32_t num;

	gni_error_mask_t err =
		GNI_ERRMASK_CORRECTABLE_MEMORY |
		GNI_ERRMASK_CRITICAL |
		GNI_ERRMASK_TRANSACTION |
		GNI_ERRMASK_ADDRESS_TRANSLATION |
		GNI_ERRMASK_TRANSIENT |
		GNI_ERRMASK_INFORMATIONAL;

	status = GNI_SubscribeErrors(_dom.nic, 0, err, 64, &err_hndl);
	/* Test for subscription to error events */
	if (status != GNI_RC_SUCCESS) {
		zap_ugni_log("FAIL:GNI_SubscribeErrors returned error %s\n", gni_err_str[status]);
	}
	while (1) {
		memset(&ev, 0, sizeof(ev));
		status = GNI_WaitErrorEvents(err_hndl,&ev,1,0,&num);
		if (status != GNI_RC_SUCCESS || (num != 1)) {
			zap_ugni_log("FAIL:GNI_WaitErrorEvents returned error %d %s\n",
				num, gni_err_str[status]);
			continue;
		}
		zap_ugni_log("%u %u %u %u %lu %lu %lu %lu %lu\n",
			     ev.error_code, ev.error_category, ev.ptag,
			     ev.serial_number, ev.timestamp,
			     ev.info_mmrs[0], ev.info_mmrs[1], ev.info_mmrs[2],
			     ev.info_mmrs[3]);
	}
	return NULL;
}

static zap_err_t z_ugni_listen(zap_ep_t ep, struct sockaddr *sa,
				socklen_t sa_len)
{
	struct z_ugni_ep *uep = (void*)ep;
	struct epoll_event ev;
	struct z_ugni_io_thread *thr;
	zap_err_t zerr;
	int rc, flags;

	zerr = zap_ep_change_state(&uep->ep, ZAP_EP_INIT, ZAP_EP_LISTENING);
	if (zerr)
		goto err_0;

	uep->sock = socket(AF_INET, SOCK_STREAM, 0);
	if (uep->sock == -1) {
		zerr = ZAP_ERR_RESOURCE;
		goto err_0;
	}
	rc = __set_sock_opts(uep->sock);
	if (rc) {
		zerr = ZAP_ERR_RESOURCE;
		goto err_1;
	}
	flags = 1;
	rc = setsockopt(uep->sock, SOL_SOCKET, SO_REUSEADDR,
			&flags, sizeof(flags));
	if (rc == -1) {
		zerr = ZAP_ERR_RESOURCE;
		goto err_1;
	}

	/* bind - listen */
	rc = bind(uep->sock, sa, sa_len);
	if (rc) {
		if (errno == EADDRINUSE)
			zerr = ZAP_ERR_BUSY;
		else
			zerr = ZAP_ERR_RESOURCE;
		goto err_1;
	}
	rc = listen(uep->sock, 1024);
	if (rc) {
		if (errno == EADDRINUSE)
			zerr = ZAP_ERR_BUSY;
		else
			zerr = ZAP_ERR_RESOURCE;
		goto err_1;
	}

	zerr = zap_io_thread_ep_assign(&uep->ep);
	if (zerr)
		goto err_1;

	thr = (void*)uep->ep.thread;
	ev.events = EPOLLIN;
	ev.data.ptr = &uep->sock_epoll_ctxt;
	rc = epoll_ctl(thr->efd, EPOLL_CTL_ADD, uep->sock, &ev);
	if (rc) {
		zerr = zap_errno2zerr(errno);
		goto err_2;
	}

	return ZAP_ERR_OK;
 err_2:
	zap_io_thread_ep_release(ep);
 err_1:
	close(uep->sock);
 err_0:
	return zerr;
}

static zap_err_t z_ugni_send(zap_ep_t ep, char *buf, size_t len)
{
	struct z_ugni_ep *uep = (void*)ep;
	zap_err_t zerr;
	struct zap_ugni_msg msg;

	/* node state validation */
	if (_node_state.check_state) {
		if (uep->node_id == -1) {
			struct sockaddr lsa, sa;
			socklen_t sa_len;
			zap_err_t zerr;
			zerr = zap_get_name(ep, &lsa, &sa, &sa_len);
			if (zerr) {
				DLOG("zap_get_name() error: %d\n", zerr);
				return ZAP_ERR_ENDPOINT;
			}
			uep->node_id = __get_nodeid(&sa, sa_len);
		}
		if (uep->node_id != -1) {
			if (__check_node_state(uep->node_id)) {
				DLOG("Node %d is in a bad state\n", uep->node_id);
				z_ugni_close(ep);
				return ZAP_ERR_ENDPOINT;
			}
		}
	}

	pthread_mutex_lock(&uep->ep.lock);
	if (!uep->gni_ep || ep->state != ZAP_EP_CONNECTED) {
		pthread_mutex_unlock(&uep->ep.lock);
		return ZAP_ERR_NOT_CONNECTED;
	}

	msg.hdr.msg_type = htons(ZAP_UGNI_MSG_REGULAR);
	zerr = z_ugni_smsg_send(uep, &msg, buf, len);
	pthread_mutex_unlock(&uep->ep.lock);
	return zerr;
}

static uint8_t __get_ptag()
{
	const char *tag = getenv("ZAP_UGNI_PTAG");
	if (!tag)
		return 0;
	return atoi(tag);
}

static uint32_t __get_cookie()
{
	const char *str = getenv("ZAP_UGNI_COOKIE");
	if (!str)
		return 0;
	return strtoul(str, NULL, 0);
}

static int __get_disconnect_timeout()
{
	const char *str = getenv("ZAP_UGNI_DISCONNECT_TIMEOUT");
	if (!str) {
		str = getenv("ZAP_UGNI_UNBIND_TIMEOUT");
		if (!str)
			return ZAP_UGNI_DISCONNECT_TIMEOUT;
	}
	return atoi(str);
}

static int __get_stalled_timeout()
{
	const char *str = getenv("ZAP_UGNI_STALLED_TIMEOUT");
	if (!str)
		return ZAP_UGNI_STALLED_TIMEOUT;
	return atoi(str);
}

static int __get_max_num_ep()
{
	const char *str = getenv("ZAP_UGNI_MAX_NUM_EP");
	if (!str)
		return ZAP_UGNI_MAX_NUM_EP;
	return atoi(str);
}

#define UGNI_NODE_PREFIX "nid"
static int __get_nodeid(struct sockaddr *sa, socklen_t sa_len)
{
	int rc = 0;
	char host[HOST_NAME_MAX];
	rc = getnameinfo(sa, sa_len, host, HOST_NAME_MAX,
					NULL, 0, NI_NAMEREQD);
	if (rc)
		return -1;

	char *ptr = strstr(host, UGNI_NODE_PREFIX);
	if (!ptr) {
		return -1;
	}
	ptr = 0;
	int id = strtol(host + strlen(UGNI_NODE_PREFIX), &ptr, 10);
	if (ptr[0] != '\0') {
		return -1;
	}
	return id;
}

static int __get_node_state()
{
	int i, node_id;
	rs_node_array_t nodelist;
	if (rca_get_sysnodes(&nodelist)) {
		_node_state.rca_get_failed++;
		if ((_node_state.rca_get_failed %
				_node_state.rca_log_thresh) == 0) {
			LOG("ugni: rca_get_sysnodes failed.\n");
		}

		for (i = 0; i < ZAP_UGNI_MAX_NUM_NODE; i++)
			_node_state.node_state[i] = ZAP_UGNI_NODE_GOOD;

		_node_state.state_ready = -1;
		return -1;
	}

	_node_state.rca_get_failed = 0;
	if (nodelist.na_len >= ZAP_UGNI_MAX_NUM_NODE) {
		zap_ugni_log("Number of nodes %d exceeds ZAP_UGNI_MAX_NUM_NODE "
				"%d.\n", nodelist.na_len, ZAP_UGNI_MAX_NUM_NODE);
	}
	for (i = 0; i < nodelist.na_len && i < ZAP_UGNI_MAX_NUM_NODE; i++) {
		node_id = nodelist.na_ids[i].rs_node_s._node_id;
		_node_state.node_state[node_id] =
			nodelist.na_ids[i].rs_node_s._node_state;
	}
	free(nodelist.na_ids);
	_node_state.state_ready = 1;
	return 0;
}

/*
 * return 0 if the state is good. Otherwise, 1 is returned.
 */
static int __check_node_state(int node_id)
{
	while (_node_state.state_ready != 1) {
		/* wait for the state to be populated. */
		if (_node_state.state_ready == -1) {
			/*
			 * XXX: FIXME: Handle this case
			 * For now, when rca_get_sysnodes fails,
			 * the node states are set to UGNI_NODE_GOOD.
			 */
			break;
		}
	}

	if (node_id != -1){
		if (node_id >= ZAP_UGNI_MAX_NUM_NODE) {
			zap_ugni_log("node_id %d exceeds ZAP_UGNI_MAX_NUM_NODE "
					"%d.\n", node_id, ZAP_UGNI_MAX_NUM_NODE);
			return 1;
		}
		if (_node_state.node_state[node_id] != ZAP_UGNI_NODE_GOOD)
			return 1; /* not good */
	}

	return 0; /* good */
}

void ugni_node_state_cb(ovis_event_t ev)
{
	__get_node_state(); /* FIXME: what if this fails? */
}

void *node_state_proc(void *args)
{
	rs_node_t node;
	int rc;
	struct ovis_event_s ns_ev;

	/* Initialize the inst_id here. */
	pthread_mutex_lock(&ugni_lock);
	rc = rca_get_nodeid(&node);
	if (rc) {
		_dom.inst_id = -1;
	} else {
		_dom.inst_id = (node.rs_node_s._node_id << 16) | (uint32_t)getpid();
	}
	pthread_cond_signal(&inst_id_cond);
	pthread_mutex_unlock(&ugni_lock);
	if (rc)
		return NULL;

	OVIS_EVENT_INIT(&ns_ev);
	ns_ev.param.type = OVIS_EVENT_PERIODIC;
	ns_ev.param.cb_fn = ugni_node_state_cb;
	ns_ev.param.periodic.period_us = _node_state.state_interval_us;
	ns_ev.param.periodic.phase_us = _node_state.state_offset_us;

	__get_node_state(); /* FIXME: what if this fails? */

	rc = ovis_scheduler_event_add(node_state_sched, &ns_ev);
	assert(rc == 0);

	rc = ovis_scheduler_loop(node_state_sched, 0);
	DLOG("Exiting the node state thread, rc: %d\n", rc);

	return NULL;
}

int __get_state_interval()
{
	int interval, offset;
	char *thr = getenv("ZAP_UGNI_STATE_INTERVAL");
	if (!thr) {
		DLOG("Note: no envvar ZAP_UGNI_STATE_INTERVAL.\n");
		goto err;
	}

	char *ptr;
	int tmp = strtol(thr, &ptr, 10);
	if (ptr[0] != '\0') {
		LOG("Invalid ZAP_UGNI_STATE_INTERVAL value (%s)\n", thr);
		goto err;
	}
	if (tmp < 100000) {
		LOG("Invalid ZAP_UGNI_STATE_INTERVAL value (%s). "
				"Using 100ms.\n", thr);
		interval = 100000;
	} else {
		interval = tmp;
	}

	thr = getenv("ZAP_UGNI_STATE_OFFSET");
	if (!thr) {
		DLOG("Note: no envvar ZAP_UGNI_STATE_OFFSET.\n");
		offset = 0;
		goto out;
	}

	tmp = strtol(thr, &ptr, 10);
	if (ptr[0] != '\0') {
		LOG("Invalid ZAP_UGNI_STATE_OFFSET value (%s)\n", thr);
		goto err;
	}

	offset = tmp;
	if (!(interval >= labs(offset) * 2)){ /* FIXME: What should this check be ? */
		LOG("Invalid ZAP_UGNI_STATE_OFFSET value (%s)."
				" Using 0ms.\n", thr);
		offset = 0;
	}
out:
	_node_state.state_interval_us = interval;
	_node_state.state_offset_us = offset;
	_node_state.check_state = 1;
	return 0;
err:
	_node_state.state_interval_us = 0;
	_node_state.state_offset_us = 0;
	_node_state.check_state = 0;
	return -1;
}

static int ugni_node_state_thread_init()
{
	int rc = 0;
	rc = __get_state_interval();
	if (rc) {
		/* Don't check node states if failed to get the interval */
		return 0;
	}

	_node_state.state_ready = 0;
	_node_state.rca_get_failed = 0;
	_node_state.rca_log_thresh = ZAP_UGNI_RCA_LOG_THS;
	_node_state.rca_get_failed = 0;

	_node_state.node_state = malloc(ZAP_UGNI_MAX_NUM_NODE * sizeof(int));
	if (!_node_state.node_state) {
		LOG("Failed to create node state array. Out of memory\n");
		errno = ENOMEM;
		return -1;
	}
	if (!node_state_sched) {
		node_state_sched = ovis_scheduler_new();
		if (!node_state_sched)
			return errno;
	}
	rc = pthread_create(&node_state_thread, NULL, node_state_proc, NULL);
	if (rc)
		return rc;
	pthread_setname_np(node_state_thread, "ugni:node_state");
	return 0;
}

static int z_ugni_init()
{
	int rc = 0;
	gni_return_t grc;
	static char buff[256];
	int fd;
	ssize_t rdsz;

	pthread_mutex_lock(&ugni_lock);
	if (zap_ugni_dom_initialized)
		goto out;

	fd = open(VERSION_FILE, O_RDONLY);
	if (fd < 0) {
		LOG("ERROR: Cannot open version file: %s\n",
				VERSION_FILE);
		rc = errno;
		goto out;
	}
	rdsz = read(fd, buff, sizeof(buff) - 1);
	if (rdsz < 0) {
		LOG("version file read error (errno %d): %m\n", errno);
		close(fd);
		rc = errno;
		goto out;
	}
	buff[rdsz] = 0;
	close(fd);

	if (strstr(buff, "cray_ari")) {
		_dom.type = ZAP_UGNI_TYPE_ARIES;
	}

	if (strstr(buff, "cray_gem")) {
		_dom.type = ZAP_UGNI_TYPE_GEMINI;
	}

	if (_dom.type == ZAP_UGNI_TYPE_NONE) {
		LOG("ERROR: cannot determine ugni type\n");
		rc = EINVAL;
		goto out;
	}

	_dom.euid = geteuid();
	_dom.cookie = __get_cookie();
	DLOG("cookie: %#x\n", _dom.cookie);

	switch (_dom.type) {
	case ZAP_UGNI_TYPE_ARIES:
#ifdef GNI_FIND_ALLOC_PTAG
		_dom.ptag = GNI_FIND_ALLOC_PTAG;
		DLOG("ugni_type: aries\n");
#else
		DLOG("ERROR: This library has not been compiled"
			" with ARIES support\n");
		rc = EINVAL;
		goto out;
#endif
		break;
	case ZAP_UGNI_TYPE_GEMINI:
		_dom.ptag = __get_ptag();
		DLOG("ugni_type: gemini\n");
		break;
	default:
		rc = EINVAL;
		goto out;
	}

	DLOG("ptag: %#hhx\n", _dom.ptag);

	_dom.limits.mdd_limit = GNI_JOB_INVALID_LIMIT;
	_dom.limits.a.mrt_limit = GNI_JOB_INVALID_LIMIT;
	_dom.limits.b.gart_limit = GNI_JOB_INVALID_LIMIT;
	_dom.limits.fma_limit = GNI_JOB_INVALID_LIMIT;
	_dom.limits.bte_limit = GNI_JOB_INVALID_LIMIT;
	_dom.limits.cq_limit = GNI_JOB_INVALID_LIMIT;

	_dom.limits.ntt_ctrl = GNI_JOB_CTRL_NTT_CLEANUP;

	_dom.limits.ntt_size = 0;
	_dom.limits.ntt_base = 0;

	if (!_dom.cdm) {
		if (((int)_dom.euid == 0) ||
			(_dom.type == ZAP_UGNI_TYPE_GEMINI)) {
			/* Do this if run as root or of type Gemini */
			grc = GNI_ConfigureJob(0, 0, _dom.ptag, _dom.cookie,
							&_dom.limits);
			if (grc) {
				LOG("ERROR: GNI_ConfigureJob() failed: %s\n",
						gni_ret_str(grc));
				rc = grc;
				goto out;
			}
		}
		uint32_t fma_mode;
		int dedicated;
		char *dedicated_s = getenv("ZAP_UGNI_FMA_DEDICATED");
		if (dedicated_s)
			dedicated = atoi(dedicated_s);
		else
			dedicated = 0;
		if (dedicated)
			fma_mode = GNI_CDM_MODE_FMA_DEDICATED;
		else
			fma_mode = GNI_CDM_MODE_FMA_SHARED;
		grc = GNI_CdmCreate(_dom.inst_id, _dom.ptag, _dom.cookie,
				    fma_mode, &_dom.cdm);
		if (grc) {
			LOG("ERROR: GNI_CdmCreate() failed: %s\n",
					gni_ret_str(grc));
			rc = grc;
			goto out;
		}
	}

	if (!_dom.nic) {
		grc = GNI_CdmAttach(_dom.cdm, 0, &_dom.pe_addr, &_dom.nic);
		if (grc) {
			LOG("ERROR: GNI_CdmAttach() failed: %s\n",
					gni_ret_str(grc));
			rc = grc;
			goto out;
		}
	}
	zap_ugni_dom_initialized = 1;
	rc = pthread_create(&error_thread, NULL, error_thread_proc, NULL);
	if (rc) {
		LOG("ERROR: pthread_create() failed: %d\n", rc);
	}
	pthread_setname_np(error_thread, "ugni:error");
out:
	pthread_mutex_unlock(&ugni_lock);
	return rc;
}

int init_once()
{
	int rc = ENOMEM;

	ugni_stats = zap_thrstat_new("ugni:cq_proc", ZAP_ENV_INT(ZAP_THRSTAT_WINDOW));
	rc = ugni_node_state_thread_init();
	if (rc)
		return rc;

	/*
	 * We cannot call the rca APIs from different threads.
	 * The node_state_thread calls rca_get_sysnodes to get the node states.
	 * To construct a unique ID to attach CM, rca_get_nodeid is called.
	 */
	pthread_mutex_lock(&ugni_lock);
	if (!_node_state.check_state) {
		rs_node_t node;
		/*
		 * The node_state_thread isn't created, so the nodeid isn't
		 * initilized. Do it here.
		 */
		rc = rca_get_nodeid(&node);
		if (rc) {
			pthread_mutex_unlock(&ugni_lock);
			goto err;
		}

		_dom.inst_id = (node.rs_node_s._node_id << 16) | (uint32_t)getpid();
	} else {
		/*
		 * The node_state_thread is created and the node id will be
		 * initialized in there. Wait until it is done.
		 */
		while (_dom.inst_id == 0) {
			pthread_cond_wait(&inst_id_cond, &ugni_lock);
			if (_dom.inst_id == 0)
				continue;

			if (_dom.inst_id == -1) {
				/* Error getting the node ID */
				pthread_mutex_unlock(&ugni_lock);
				goto err;
			}
		}
	}

	zap_ugni_disconnect_timeout = __get_disconnect_timeout();
	zap_ugni_stalled_timeout = __get_stalled_timeout();

	/*
	 * Get the number of maximum number of endpoints zap_ugni will handle.
	 */
	zap_ugni_max_num_ep = __get_max_num_ep();
#ifdef DEBUG
	zap_ugni_ep_id = calloc(zap_ugni_max_num_ep, sizeof(uint32_t));
	if (!zap_ugni_ep_id)
		goto err;
#endif /* DEBUG */
	pthread_mutex_unlock(&ugni_lock);

	rc = z_ugni_init();
	if (rc)
		goto err;

	init_complete = 1;

	return 0;
err:
	z_ugni_cleanup();
	return rc;
}

zap_ep_t z_ugni_new(zap_t z, zap_cb_fn_t cb)
{
	struct z_ugni_ep *uep = calloc(1, sizeof(*uep));
	DLOG("Creating ep: %p\n", uep);
	if (!uep) {
		errno = ZAP_ERR_RESOURCE;
		goto err_0;
	}
	uep->sock = -1;
	uep->ep_id = -1;
	uep->node_id = -1;
	uep->app_owned = 1;
	uep->post_credit = ZAP_UGNI_EP_SQ_DEPTH;
	STAILQ_INIT(&uep->pending_wrq);
	STAILQ_INIT(&uep->submitted_wrq);
	pthread_mutex_lock(&z_ugni_list_mutex);
#ifdef DEBUG
	uep->ep_id = zap_ugni_get_ep_id();
	if (uep->ep_id < 0) {
		LOG_(uep, "%s: %p: Failed to get the zap endpoint ID\n",
				__func__, uep);
		errno = ZAP_ERR_RESOURCE;
		goto err_1;
	}
#endif /* DEBUG */
	LIST_INSERT_HEAD(&z_ugni_list, uep, link);
	pthread_mutex_unlock(&z_ugni_list_mutex);
	CONN_LOG("new endpoint: %p\n", uep);
	return (zap_ep_t)uep;

#ifdef DEBUG
 err_1:
	pthread_mutex_unlock(&z_ugni_list_mutex);
	free(uep);
#endif /* DEBUG */
 err_0:
	return NULL;
}

static void z_ugni_destroy(zap_ep_t ep)
{
	struct z_ugni_ep *uep = (void*)ep;
	DLOG_(uep, "destroying endpoint %p\n", uep);
	CONN_LOG("destroying endpoint %p\n", uep);
	pthread_mutex_lock(&z_ugni_list_mutex);
	ZUGNI_LIST_REMOVE(uep, link);
#ifdef DEBUG
	if (uep->ep_id >= 0)
		zap_ugni_ep_id[uep->ep_id] = 0;
#endif /* DEBUG */
	pthread_mutex_unlock(&z_ugni_list_mutex);

	if (uep->conn_data) {
		free(uep->conn_data);
		uep->conn_data = 0;
	}
	if (uep->sock > -1) {
		close(uep->sock);
		uep->sock = -1;
	}
	z_ugni_ep_release(uep);
	free(ep);
}

zap_err_t z_ugni_accept(zap_ep_t ep, zap_cb_fn_t cb, char *data, size_t data_len)
{
	/* ep is the newly created ep from __z_ugni_conn_request */
	struct z_ugni_ep *uep = (struct z_ugni_ep *)ep;
	struct zap_ugni_msg msg;
	zap_err_t zerr;

	pthread_mutex_lock(&uep->ep.lock);

	if (uep->ep.state != ZAP_EP_ACCEPTING) {
		zerr = ZAP_ERR_ENDPOINT;
		goto out;
	}

	zap_get_ep(&uep->ep); /* will be put down when disconnected/conn_error */
	uep->ep.cb = cb;
	uep->app_accepted = 1;

	msg.hdr.msg_type = htons(ZAP_UGNI_MSG_ACCEPTED);
	msg.accepted.ep_desc.inst_id = htonl(_dom.inst_id);
	msg.accepted.ep_desc.pe_addr = htonl(_dom.pe_addr);
	msg.accepted.ep_desc.smsg_attr = uep->local_smsg_attr;
	zerr = z_ugni_smsg_send(uep, &msg, data, data_len);
	if (zerr)
		goto out;
	zerr = ZAP_ERR_OK;
	CONN_LOG("%p zap-accepted\n", uep);
	/* let through */
out:
	pthread_mutex_unlock(&uep->ep.lock);
	return zerr;
}

static zap_err_t z_ugni_reject(zap_ep_t ep, char *data, size_t data_len)
{
	struct z_ugni_ep *uep = (struct z_ugni_ep *)ep;
	struct zap_ugni_msg msg;
	zap_err_t zerr;

	msg.hdr.msg_type = htons(ZAP_UGNI_MSG_REJECTED);
	pthread_mutex_lock(&uep->ep.lock);
	uep->ep.state = ZAP_EP_ERROR;
	zerr = z_ugni_smsg_send(uep, &msg, data, data_len);
	if (zerr)
		goto err;
	pthread_mutex_unlock(&uep->ep.lock);
	return ZAP_ERR_OK;
err:
	pthread_mutex_unlock(&uep->ep.lock);
	return zerr;
}

static zap_err_t
z_ugni_map(zap_ep_t ep, zap_map_t *pm, void *buf, size_t len, zap_access_t acc)
{
	struct zap_ugni_map *map = calloc(1, sizeof(*map));
	gni_return_t grc;
	zap_err_t zerr = ZAP_ERR_OK;
	if (!map) {
		zerr = ZAP_ERR_RESOURCE;
		goto err0;
	}

	grc = ugni_get_mh(ep->z, &map->gni_mh);
	if (grc) {
		zerr = ZAP_ERR_RESOURCE;
		goto err1;
	}

	*pm = (void*)map;
	goto out;
err1:
	free(map);
err0:
out:
	return zerr;
}

static zap_err_t z_ugni_unmap(zap_ep_t ep, zap_map_t map)
{
	struct zap_ugni_map *m = (void*) map;
	free(m);
	return ZAP_ERR_OK;
}

static zap_err_t z_ugni_share(zap_ep_t ep, zap_map_t map,
				const char *msg, size_t msg_len)
{
	zap_err_t rc;
	struct z_ugni_ep *uep = (void*) ep;

	/* validate */
	if (map->type != ZAP_MAP_LOCAL)
		return ZAP_ERR_INVALID_MAP_TYPE;

	/* node state validation */
	if (_node_state.check_state) {
		if (uep->node_id == -1) {
			struct sockaddr lsa, sa;
			socklen_t sa_len;
			zap_err_t zerr;
			zerr = zap_get_name(ep, &lsa, &sa, &sa_len);
			if (zerr) {
				DLOG("zap_get_name() error: %d\n", zerr);
				return ZAP_ERR_ENDPOINT;
			}
			uep->node_id = __get_nodeid(&sa, sa_len);
		}
		if (uep->node_id != -1) {
			if (__check_node_state(uep->node_id)) {
				DLOG("Node %d is in a bad state\n", uep->node_id);
				z_ugni_close(ep);
				return ZAP_ERR_ENDPOINT;
			}
		}
	}

	pthread_mutex_lock(&uep->ep.lock);
	if (ep->state != ZAP_EP_CONNECTED) {
		pthread_mutex_unlock(&uep->ep.lock);
		return ZAP_ERR_NOT_CONNECTED;
	}

	/* prepare message */
	struct zap_ugni_map *smap = (struct zap_ugni_map *)map;
	struct zap_ugni_msg smsg = {
		.rendezvous = {
			.hdr = {
				.msg_type = htons(ZAP_UGNI_MSG_RENDEZVOUS),
			},
			.gni_mh = {
				.qword1 = htobe64(smap->gni_mh.qword1),
				.qword2 = htobe64(smap->gni_mh.qword2),
			},
			.map_addr = htobe64((uint64_t)map->addr),
			.map_len = htonl(map->len),
			.acc = htonl(map->acc),
		}
	};

	rc = z_ugni_smsg_send(uep, &smsg, msg, msg_len);
	pthread_mutex_unlock(&uep->ep.lock);
	return rc;
}

static zap_err_t z_ugni_read(zap_ep_t ep, zap_map_t src_map, char *src,
			     zap_map_t dst_map, char *dst, size_t sz,
			     void *context)
{
	zap_err_t zerr;

	if (((uint64_t)src) & 3)
		return ZAP_ERR_PARAMETER;
	if (((uint64_t)dst) & 3)
		return ZAP_ERR_PARAMETER;
	if (sz & 3)
		return ZAP_ERR_PARAMETER;

	if (z_map_access_validate(src_map, src, sz, ZAP_ACCESS_READ) != 0)
		return ZAP_ERR_REMOTE_PERMISSION;
	if (z_map_access_validate(dst_map, dst, sz, ZAP_ACCESS_READ) != 0)
		return ZAP_ERR_LOCAL_LEN;

	struct z_ugni_ep *uep = (struct z_ugni_ep *)ep;
	struct zap_ugni_map *smap = (struct zap_ugni_map *)src_map;
	struct zap_ugni_map *dmap = (struct zap_ugni_map *)dst_map;

	/* node state validation */
	if (_node_state.check_state) {
		if (uep->node_id == -1) {
			struct sockaddr lsa, sa;
			socklen_t sa_len;
			zap_err_t zerr;
			zerr = zap_get_name(ep, &lsa, &sa, &sa_len);
			if (zerr) {
				DLOG("zap_get_name() error: %d\n", zerr);
				return ZAP_ERR_ENDPOINT;
			}
			uep->node_id = __get_nodeid(&sa, sa_len);
		}
		if (uep->node_id != -1) {
			if (__check_node_state(uep->node_id)) {
				DLOG("Node %d is in a bad state\n", uep->node_id);
				z_ugni_close(ep);
				return ZAP_ERR_ENDPOINT;
			}
		}
	}

	pthread_mutex_lock(&ep->lock);
	if (!uep->gni_ep || ep->state != ZAP_EP_CONNECTED) {
		zerr = ZAP_ERR_NOT_CONNECTED;
		goto out;
	}

	gni_return_t grc;
	struct z_ugni_wr *wr = z_ugni_alloc_post_desc(uep);
	if (!wr) {
		zerr = ZAP_ERR_RESOURCE;
		goto out;
	}
	struct zap_ugni_post_desc *desc = wr->post_desc;

	desc->post.type = GNI_POST_RDMA_GET;
	desc->post.cq_mode = GNI_CQMODE_GLOBAL_EVENT;
	desc->post.dlvr_mode = GNI_DLVMODE_IN_ORDER;
	desc->post.local_addr = (uint64_t)dst;
	desc->post.local_mem_hndl = dmap->gni_mh;
	desc->post.remote_addr = (uint64_t)src;
	desc->post.remote_mem_hndl = smap->gni_mh;
	desc->post.length = sz;
	desc->post.post_id = __sync_fetch_and_add(&ugni_post_id, 1);

	desc->context = context;
#ifdef DEBUG
	__sync_fetch_and_add(&ugni_io_count, 1);
#endif /* DEBUG */

	if (uep->post_credit) {
		Z_GNI_API_LOCK(uep->ep.thread);
		grc = GNI_PostRdma(uep->gni_ep, &desc->post);
		Z_GNI_API_UNLOCK(uep->ep.thread);
		if (grc != GNI_RC_SUCCESS) {
			LOG_(uep, "%s: GNI_PostRdma() failed, grc: %s\n",
					__func__, gni_ret_str(grc));
			z_ugni_ep_error(uep);
#ifdef DEBUG
			__sync_sub_and_fetch(&ugni_io_count, 1);
#endif /* DEBUG */
			z_ugni_free_post_desc(wr);
			zerr = ZAP_ERR_RESOURCE;
			goto out;
		}
		uep->post_credit--;
		wr->state = Z_UGNI_WR_SUBMITTED;
		/* add to the submitted list */
		STAILQ_INSERT_TAIL(&uep->submitted_wrq, wr, entry);
	} else {
		/* no post credit, add to the pending list */
		wr->state = Z_UGNI_WR_PENDING;
		STAILQ_INSERT_TAIL(&uep->pending_wrq, wr, entry);
	}
	/* no need to copy data to wr like send b/c the application won't touch
	 * it until it get completion event */
	zerr = ZAP_ERR_OK;
 out:
	pthread_mutex_unlock(&ep->lock);
	return zerr;
}

static zap_err_t z_ugni_write(zap_ep_t ep, zap_map_t src_map, char *src,
			      zap_map_t dst_map, char *dst, size_t sz,
			      void *context)
{
	gni_return_t grc;
	zap_err_t zerr;

	if (((uint64_t)src) & 3)
		return ZAP_ERR_PARAMETER;
	if (((uint64_t)dst) & 3)
		return ZAP_ERR_PARAMETER;
	if (sz & 3)
		return ZAP_ERR_PARAMETER;

	if (z_map_access_validate(src_map, src, sz, ZAP_ACCESS_NONE) != 0)
		return ZAP_ERR_LOCAL_LEN;
	if (z_map_access_validate(dst_map, dst, sz, ZAP_ACCESS_WRITE) != 0)
		return ZAP_ERR_REMOTE_PERMISSION;

	struct z_ugni_ep *uep = (void*)ep;
	struct zap_ugni_map *smap = (void*)src_map;
	struct zap_ugni_map *dmap = (void*)dst_map;

	/* node state validation */
	if (_node_state.check_state) {
		if (uep->node_id == -1) {
			struct sockaddr lsa, sa;
			socklen_t sa_len;
			zap_err_t zerr;
			zerr = zap_get_name(ep, &lsa, &sa, &sa_len);
			if (zerr) {
				DLOG("zap_get_name() error: %d\n", zerr);
				return ZAP_ERR_ENDPOINT;
			}
			uep->node_id = __get_nodeid(&sa, sa_len);
		}
		if (uep->node_id != -1) {
			if (__check_node_state(uep->node_id)) {
				DLOG("Node %d is in a bad state\n", uep->node_id);
				z_ugni_close(ep);
				return ZAP_ERR_ENDPOINT;
			}
		}
	}

	pthread_mutex_lock(&ep->lock);
	if (!uep->gni_ep || ep->state != ZAP_EP_CONNECTED) {
		zerr = ZAP_ERR_NOT_CONNECTED;
		goto out;
	}

	struct z_ugni_wr *wr = z_ugni_alloc_post_desc(uep);
	if (!wr) {
		zerr = ZAP_ERR_RESOURCE;
		goto out;
	}
	struct zap_ugni_post_desc *desc = wr->post_desc;

	desc->post.type = GNI_POST_RDMA_PUT;
	desc->post.cq_mode = GNI_CQMODE_GLOBAL_EVENT;
	desc->post.dlvr_mode = GNI_DLVMODE_PERFORMANCE;
	desc->post.local_addr = (uint64_t)src;
	desc->post.local_mem_hndl = smap->gni_mh;
	desc->post.remote_addr = (uint64_t)dst;
	desc->post.remote_mem_hndl = dmap->gni_mh;
	desc->post.length = sz;
	desc->post.post_id = __sync_fetch_and_add(&ugni_post_id, 1);
	desc->context = context;

#ifdef DEBUG
	__sync_fetch_and_add(&ugni_io_count, 1);
#endif /* DEBUG */
	if (uep->post_credit) {
		Z_GNI_API_LOCK(uep->ep.thread);
		grc = GNI_PostRdma(uep->gni_ep, &desc->post);
		Z_GNI_API_UNLOCK(uep->ep.thread);
		if (grc != GNI_RC_SUCCESS) {
			LOG_(uep, "%s: GNI_PostRdma() failed, grc: %s\n",
					__func__, gni_ret_str(grc));
			z_ugni_ep_error(uep);
#ifdef DEBUG
			__sync_sub_and_fetch(&ugni_io_count, 1);
#endif /* DEBUG */
			z_ugni_free_post_desc(wr);
			zerr = ZAP_ERR_RESOURCE;
			goto out;
		}
		uep->post_credit--;
		wr->state = Z_UGNI_WR_SUBMITTED;
		/* add to the submitted list */
		STAILQ_INSERT_TAIL(&uep->submitted_wrq, wr, entry);
	} else {
		/* no post credit, add to the pending list */
		wr->state = Z_UGNI_WR_PENDING;
		STAILQ_INSERT_TAIL(&uep->pending_wrq, wr, entry);
	}
	zerr = ZAP_ERR_OK;
out:
	pthread_mutex_unlock(&ep->lock);
	return zerr;
}

void z_ugni_io_thread_cleanup(void *arg)
{
	struct z_ugni_io_thread *thr = arg;
	if (thr->cch)
		GNI_CompChanDestroy(thr->cch);
	if (thr->rcq)
		GNI_CqDestroy(thr->rcq);
	if (thr->scq)
		GNI_CqDestroy(thr->scq);
	if (thr->mbox_mh.qword1 || thr->mbox_mh.qword2)
		GNI_MemDeregister(_dom.nic, &thr->mbox_mh);
	if (thr->efd > -1)
		close(thr->efd);
	if (thr->zq_fd[0] > -1)
		close(thr->zq_fd[0]);
	if (thr->zq_fd[1] > -1)
		close(thr->zq_fd[1]);
	zap_io_thread_release(&thr->zap_io_thread);
	free(thr);
}

static int z_ugni_enable_sock(struct z_ugni_ep *uep)
{
	struct z_ugni_io_thread *thr = (void*)uep->ep.thread;
	struct epoll_event ev;
	int rc;
	if (uep->sock < 0) {
		LOG("ERROR: %s:%d socket closed.\n", __func__, __LINE__);
		return EINVAL;
	}
	ev.events = EPOLLIN|EPOLLOUT;
	ev.data.ptr = &uep->sock_epoll_ctxt;
	rc = epoll_ctl(thr->efd, EPOLL_CTL_ADD, uep->sock, &ev);
	if (rc) {
		LOG("ERROR: %s:%d epoll ADD error: %d.\n", __func__, __LINE__, errno);
		return errno;
	}
	zap_get_ep(&uep->ep);
	return 0;
}

static int z_ugni_disable_sock(struct z_ugni_ep *uep)
{
	/* Must hold uep->ep.lock */
	struct z_ugni_io_thread *thr = (void*)uep->ep.thread;
	struct epoll_event ignore;
	if (uep->sock < 0)
		return EINVAL;
	CONN_LOG("%p disabling socket\n", uep);
	epoll_ctl(thr->efd, EPOLL_CTL_DEL, uep->sock, &ignore);
	close(uep->sock);
	uep->sock = -1;
	zap_put_ep(&uep->ep);
	return 0;
}

/* Must hold uep->ep.lock */
static int z_ugni_submit_pending(struct z_ugni_ep *uep)
{
	int rc;
	gni_return_t grc;
	struct z_ugni_wr *wr;
 next:
	if (!uep->post_credit)
		goto out;
	wr = STAILQ_FIRST(&uep->pending_wrq);
	if (!wr)
		goto out;
	switch (wr->type) {
	case Z_UGNI_WR_RDMA:
		Z_GNI_API_LOCK(uep->ep.thread);
		grc = GNI_PostRdma(uep->gni_ep, &wr->post_desc->post);
		Z_GNI_API_UNLOCK(uep->ep.thread);
		if (grc != GNI_RC_SUCCESS) {
			LLOG("GNI_PostRdma() error: %d\n", grc);
			rc = EIO;
			goto err;
		}
		break;
	case Z_UGNI_WR_SMSG:
		Z_GNI_API_LOCK(uep->ep.thread);
		grc = GNI_SmsgSend(uep->gni_ep, wr->send_wr->msg,
				   wr->send_wr->msg_len, NULL, 0,
				   wr->send_wr->msg_id);
		Z_GNI_API_UNLOCK(uep->ep.thread);
		if (grc == GNI_RC_NOT_DONE) {
			/* no peer recv credit */
			goto out;
		}
		if (grc != GNI_RC_SUCCESS) {
			LLOG("GNI_SmsgSend() error: %d\n", grc);
			rc = EIO;
			goto err;
		}
		CONN_LOG("%p sent pending smsg %s\n", uep, zap_ugni_msg_type_str(ntohs(wr->send_wr->msg->hdr.msg_type)));
		break;
	default:
		rc = EINVAL;
		LLOG("Unexpected z_ugni_wr: %d\n", wr->type);
		goto err;
	}
	uep->post_credit--;
	assert(wr->state == Z_UGNI_WR_PENDING);
	STAILQ_REMOVE(&uep->pending_wrq, wr, z_ugni_wr, entry);
	STAILQ_INSERT_TAIL(&uep->submitted_wrq, wr, entry);
	wr->state = Z_UGNI_WR_SUBMITTED;
	goto next;
 out:
	return 0;
 err:
	return rc;
}

static int z_ugni_handle_scq_rdma(struct z_ugni_io_thread *thr, gni_cq_entry_t cqe)
{
	gni_return_t grc;
	gni_post_descriptor_t *post;
	struct z_ugni_wr *wr;
	struct zap_ugni_post_desc *desc;
	struct zap_event zev = {0};

	post = NULL;
	Z_GNI_API_LOCK(&thr->zap_io_thread);
	grc = GNI_GetCompleted(thr->scq, cqe, &post);
	Z_GNI_API_UNLOCK(&thr->zap_io_thread);
	if (grc != GNI_RC_SUCCESS) {
		LOG("GNI_GetCompleted() error: %d\n", grc);
		return -1;
	}
	desc = (void*)post;
	wr = __container_of(desc, struct z_ugni_wr, post_desc);
	if (wr->state == Z_UGNI_WR_STALLED) {
		/*
		 * The descriptor is in the stalled state.
		 *
		 * The completion corresponding to the descriptor
		 * has been flushed. The corresponding endpoint
		 * might have been freed already.
		 *
		 * desc is in thr->stalled_wrq. The thread `thr` is the only
		 * one accessing it. So, the mutex is not required.
		 */
		LOG("%s: Received a CQ event for a stalled post "
					"desc.\n", desc->ep_name);
		STAILQ_REMOVE(&thr->stalled_wrq, wr, z_ugni_wr, entry);
		z_ugni_free_post_desc(wr);
		goto out;
	}
	struct z_ugni_ep *uep = desc->uep;
	if (!uep) {
		/*
		 * This should not happen. The code is put in to prevent
		 * the segmentation fault and to record the situation.
		 */
		LOG("%s: %s: desc->uep = NULL. Drop the descriptor.\n", __func__,
			desc->ep_name);
		goto out;
	}
#ifdef DEBUG
	if (uep->deferred_link.le_prev)
		LOG_(uep, "uep %p: Doh!! I'm on the deferred list.\n", uep);
#endif /* DEBUG */
	zev.ep = &uep->ep;
	switch (desc->post.type) {
	case GNI_POST_RDMA_GET:
		DLOG_(uep, "RDMA_GET: Read complete %p with %s\n", desc, gni_ret_str(grc));
		if (grc) {
			zev.status = ZAP_ERR_RESOURCE;
			LOG_(uep, "RDMA_GET: completing "
				"with error %s.\n",
				gni_ret_str(grc));
		} else {
			zev.status = ZAP_ERR_OK;
		}
		zev.type = ZAP_EVENT_READ_COMPLETE;
		zev.context = desc->context;
		break;
	case GNI_POST_RDMA_PUT:
		DLOG_(uep, "RDMA_PUT: Write complete %p with %s\n",
					desc, gni_ret_str(grc));
		if (grc) {
			zev.status = ZAP_ERR_RESOURCE;
			DLOG_(uep, "RDMA_PUT: completing "
				"with error %s.\n",
				gni_ret_str(grc));
		} else {
			zev.status = ZAP_ERR_OK;
		}
		zev.type = ZAP_EVENT_WRITE_COMPLETE;
		zev.context = desc->context;
		break;
	default:
		LOG_(uep, "Unknown completion type %d.\n",
				 desc->post.type);
		z_ugni_ep_error(uep);
	}
	pthread_mutex_lock(&uep->ep.lock);
	STAILQ_REMOVE(&uep->submitted_wrq, wr, z_ugni_wr, entry);
	z_ugni_put_post_credit(uep);
	z_ugni_submit_pending(uep);
	pthread_mutex_unlock(&uep->ep.lock);
	z_ugni_free_post_desc(wr);
	uep->ep.cb(&uep->ep, &zev);
 out:
	return 0;
}

static int z_ugni_handle_rcq_smsg(struct z_ugni_io_thread *thr, gni_cq_entry_t cqe)
{
	/* NOTE: This is GNI "remote" completion. The cqe contains `remote_data`
	 *       we sent to peer, which is our ep_idx. */
	uint32_t ep_idx = GNI_CQ_GET_REM_INST_ID(cqe);
	struct z_ugni_ep *uep;
	gni_return_t grc;
	int msg_type;

	if (!ep_idx || ep_idx >= ZAP_UGNI_THREAD_EP_MAX) {
		LLOG("Bad ep_idx: %d\n", ep_idx);
		return EINVAL;
	}
	uep = thr->ep_idx[ep_idx].uep;
	if (!uep)
		return 0;
	zap_get_ep(&uep->ep);
 next:
	Z_GNI_API_LOCK(&thr->zap_io_thread);
	grc = GNI_SmsgGetNext(uep->gni_ep, (void*)&uep->rmsg);
	Z_GNI_API_UNLOCK(&thr->zap_io_thread);
	if (grc != GNI_RC_SUCCESS)
		goto out;
	msg_type = ntohs(uep->rmsg->hdr.msg_type);
	CONN_LOG("%p smsg recv: %s (%d)\n", uep, zap_ugni_msg_type_str(msg_type), msg_type);
	if (ZAP_UGNI_MSG_NONE < msg_type && msg_type < ZAP_UGNI_MSG_TYPE_LAST) {
		process_uep_msg_fns[msg_type](uep);
	} else {
		process_uep_msg_unknown(uep);
	}
	Z_GNI_API_LOCK(&thr->zap_io_thread);
	GNI_SmsgRelease(uep->gni_ep);
	Z_GNI_API_UNLOCK(&thr->zap_io_thread);
	goto next;
 out:
	pthread_mutex_lock(&uep->ep.lock);
	z_ugni_submit_pending(uep);
	pthread_mutex_unlock(&uep->ep.lock);
	zap_put_ep(&uep->ep);
	return 0;
}

static int z_ugni_handle_scq_smsg(struct z_ugni_io_thread *thr, gni_cq_entry_t cqe)
{
	/* NOTE: This is "local" smsg completion. The cqe contains msg_id that
	 * we supplied when calling GNI_SmsgSend(). */
	uint32_t msg_id = GNI_CQ_GET_MSG_ID(cqe);
	uint16_t ep_idx = msg_id >> 16;
	struct z_ugni_ep *uep;
	struct z_ugni_wr *wr;
	gni_return_t grc;
	int rc;

	if (!ep_idx || ep_idx >= ZAP_UGNI_THREAD_EP_MAX) {
		LLOG("scq ep_idx out of range: %hu\n", ep_idx);
		errno = EINVAL;
		goto err;
	}
	uep = thr->ep_idx[ep_idx].uep;
	pthread_mutex_lock(&uep->ep.lock);
	STAILQ_FOREACH(wr, &uep->submitted_wrq, entry) {
		if (wr->type == Z_UGNI_WR_SMSG && wr->send_wr->msg_id == msg_id)
			break;
	}
	if (!wr) {
		LLOG("msg_id not found: %u\n", msg_id);
		errno = ENOENT;
		pthread_mutex_unlock(&uep->ep.lock);
		goto err;
	}
	STAILQ_REMOVE(&uep->submitted_wrq, wr, z_ugni_wr, entry);
	z_ugni_free_send_wr(wr);
	z_ugni_put_post_credit(uep);

	grc = GNI_CQ_GET_STATUS(cqe);
	if (grc) {
		LLOG("GNI_SmsgSend completed with status: %d\n", grc);
		z_ugni_ep_error(uep);
		rc = EIO;
	} else {
		rc = z_ugni_submit_pending(uep);
	}

	pthread_mutex_unlock(&uep->ep.lock);
	return rc;
 err:
	return errno;
}

static void z_ugni_handle_rcq_events(struct z_ugni_io_thread *thr)
{
	gni_cq_entry_t cqe;
	gni_return_t grc;
 next:
	Z_GNI_API_LOCK(&thr->zap_io_thread);
	grc = GNI_CqGetEvent(thr->rcq, &cqe);
	Z_GNI_API_UNLOCK(&thr->zap_io_thread);
	if (grc == GNI_RC_NOT_DONE)
		goto out;
	if (grc != GNI_RC_SUCCESS) {
		LLOG("Unexpected error from GNI_CqGetEvent(): %d\n", grc);
		goto out;
	}
	z_ugni_handle_rcq_smsg(thr, cqe);
	goto next;
 out:
	return;
}

static struct z_ugni_ep *__cqe_uep(struct z_ugni_io_thread *thr,
				   gni_cq_entry_t cqe)
{
	int ev_type = GNI_CQ_GET_TYPE(cqe);
	uint32_t msg_id;
	uint16_t ep_idx;
	switch (ev_type) {
	case GNI_CQ_EVENT_TYPE_POST:
		ep_idx = GNI_CQ_GET_INST_ID(cqe);
		return thr->ep_idx[ep_idx].uep;
	case GNI_CQ_EVENT_TYPE_SMSG:
		msg_id = GNI_CQ_GET_MSG_ID(cqe);
		ep_idx = msg_id >> 16;
		return thr->ep_idx[ep_idx].uep;
	case GNI_CQ_EVENT_TYPE_MSGQ:
	case GNI_CQ_EVENT_TYPE_DMAPP:
	default:
		LOG("Unexpected cq event: %d\n", ev_type);
		assert(0 == "Unexpected cq event.");
		break;
	}
}

static void z_ugni_handle_scq_events(struct z_ugni_io_thread *thr)
{
	gni_cq_entry_t cqe;
	gni_return_t grc;
	uint64_t ev_type;
#ifdef CONN_DEBUG
	uint64_t ev_source;
	uint64_t ev_status;
	uint64_t ev_overrun;
#endif

 next:
	cqe = 0;
	Z_GNI_API_LOCK(&thr->zap_io_thread);
	grc = GNI_CqGetEvent(thr->scq, &cqe);
	Z_GNI_API_UNLOCK(&thr->zap_io_thread);
	if (grc == GNI_RC_NOT_DONE)
		goto out;
	if (grc != GNI_RC_SUCCESS) {
		struct z_ugni_ep *uep = __cqe_uep(thr, cqe);
		if (uep) {
			pthread_mutex_lock(&uep->ep.lock);
			z_ugni_ep_error(uep);
			pthread_mutex_unlock(&uep->ep.lock);
		}
		goto out;
	}
	ev_type = GNI_CQ_GET_TYPE(cqe);
#ifdef CONN_DEBUG
	ev_source = GNI_CQ_GET_SOURCE(cqe);
	ev_status = GNI_CQ_GET_STATUS(cqe);
	ev_overrun = GNI_CQ_OVERRUN(cqe);
	LLOG("ev_type: %ld\n", ev_type);
	LLOG("ev_source: %ld\n", ev_source);
	LLOG("ev_status: %ld\n", ev_status);
	LLOG("ev_overrun: %ld\n", ev_overrun);
#endif
	switch (ev_type) {
	case GNI_CQ_EVENT_TYPE_POST:
		z_ugni_handle_scq_rdma(thr, cqe);
		break;
	case GNI_CQ_EVENT_TYPE_SMSG:
		z_ugni_handle_scq_smsg(thr, cqe);
		break;
	case GNI_CQ_EVENT_TYPE_MSGQ:
	case GNI_CQ_EVENT_TYPE_DMAPP:
	default:
		LOG("Unexpected cq event: %d\n", ev_type);
		assert(0 == "Unexpected cq event.");
		break;
	}
	goto next;
 out:
	return ;
}

static void z_ugni_handle_cq_event(struct z_ugni_io_thread *thr, int events)
{
	gni_return_t grc;
	gni_cq_handle_t cq;

	Z_GNI_API_LOCK(&thr->zap_io_thread);
	grc = GNI_CompChanGetEvent(thr->cch, &cq);
	Z_GNI_API_UNLOCK(&thr->zap_io_thread);
	if (grc != GNI_RC_SUCCESS) {
		LOG("Unexpected error from GNI_CompChanGenEvent(): %d\n", grc);
		assert(0 == "Unexpected error from GNI_CompChanGenEvent()");
		return;
	}

	if (cq == thr->rcq)
		z_ugni_handle_rcq_events(thr);
	else if (cq == thr->scq)
		z_ugni_handle_scq_events(thr);

	/* re-arm */
	Z_GNI_API_LOCK(&thr->zap_io_thread);
	grc = GNI_CqArmCompChan(&cq, 1);
	Z_GNI_API_UNLOCK(&thr->zap_io_thread);
	if (grc != GNI_RC_SUCCESS) {
		LOG("Unexpected error from GNI_CqArmCompChan(): %d\n", grc);
		assert(0 == "Unexpected error from GNI_CompChanGenEvent()");
	}
}

static void z_ugni_sock_conn_request(struct z_ugni_ep *uep)
{
	int rc;
	zap_ep_t new_ep;
	struct z_ugni_ep *new_uep;
	zap_err_t zerr;
	int sockfd;
	struct sockaddr sa;
	socklen_t sa_len = sizeof(sa);

	CONN_LOG("handling socket connection request\n");

	sockfd = accept(uep->sock, &sa, &sa_len);
	if (sockfd < 0) {
		LOG_(uep, "accept() error %d: in %s at %s:%d\n",
				errno , __func__, __FILE__, __LINE__);
		return;
	}

	rc = __set_sock_opts(sockfd);
	if (rc) {
		close(sockfd);
		zerr = ZAP_ERR_TRANSPORT;
		LOG_(uep, "Error %d: fail to set the sockbuf sz in %s.\n",
				errno, __func__);
		return;
	}

	new_ep = zap_new(uep->ep.z, uep->ep.cb);
	if (!new_ep) {
		close(sockfd);
		zerr = errno;
		LOG_(uep, "Zap Error %d (%s): in %s at %s:%d\n",
				zerr, zap_err_str(zerr) , __func__, __FILE__,
				__LINE__);
		return;
	}

	CONN_LOG("new passive endpoint: %p\n", new_ep);

	void *uctxt = zap_get_ucontext(&uep->ep);
	zap_set_ucontext(new_ep, uctxt);
	new_uep = (void*) new_ep;
	new_uep->sock = sockfd;
	new_uep->ep.state = ZAP_EP_ACCEPTING;
	new_uep->app_owned = 0;

	new_uep->sock_epoll_ctxt.type = Z_UGNI_SOCK_EVENT;

	rc = zap_io_thread_ep_assign(new_ep);
	if (rc) {
		LOG_(new_uep, "thread assignment error: %d\n", rc);
		zap_free(new_ep);
		return;
	}

	rc = z_ugni_enable_sock(new_uep);
	if (rc) {
		zap_io_thread_ep_release(new_ep);
		zap_free(new_ep);
		return;
	}

	/*
	 * NOTE: At this point, the connection is socket-connected. The next
	 * step would be setting up GNI EP and SMSG service. The active side
	 * will send z_ugni_sock_send_conn_req message over socket to initiate
	 * the setup.
	 */

	return;
}

static void z_ugni_deliver_conn_error(struct z_ugni_ep *uep)
{
	/* uep->ep.lock must NOT be held */
	struct zap_event zev = { .type = ZAP_EVENT_CONNECT_ERROR };
	zev.ep = &uep->ep;
	zev.status = zap_errno2zerr(errno);
	zap_event_deliver(&zev);
}

/* uep->ep.lock MUST be held */
static void __flush_wrq(struct z_ugni_ep *uep, struct z_ugni_wrq *head)
{
	struct zap_event zev = { .status = ZAP_ERR_FLUSH, .ep = &uep->ep };
	struct z_ugni_wr *wr;
	while ((wr = STAILQ_FIRST(head))) {
		STAILQ_REMOVE_HEAD(head, entry);
		if (wr->type == Z_UGNI_WR_RDMA) {
			switch (wr->post_desc->post.type) {
			case GNI_POST_RDMA_GET:
				zev.type = ZAP_EVENT_READ_COMPLETE;
				break;
			case GNI_POST_RDMA_PUT:
				zev.type = ZAP_EVENT_WRITE_COMPLETE;
				break;
			default:
				assert(0 == "Unexpected post type");
				continue;
			}
			zev.context = wr->post_desc->context;
			pthread_mutex_unlock(&uep->ep.lock);
			zap_event_deliver(&zev);
			pthread_mutex_lock(&uep->ep.lock);
		}
		/* zap send/recv does not have completion events */
	}
}

/* uep->ep.lock MUST be held */
static void z_ugni_flush(struct z_ugni_ep *uep)
{
	__flush_wrq(uep, &uep->submitted_wrq);
	__flush_wrq(uep, &uep->pending_wrq);
}

static int z_ugni_sock_send_conn_req(struct z_ugni_ep *uep)
{
	/* uep->ep.lock is held */
	int n;
	struct z_ugni_sock_msg_conn_req msg;

	CONN_LOG("%p sock-sending conn_req\n", uep);

	assert(uep->ep.state == ZAP_EP_CONNECTING);

	msg.hdr.msg_len = htonl(sizeof(msg));
	msg.hdr.msg_type = htons(ZAP_UGNI_MSG_CONNECT);
	msg.ep_desc.inst_id = htonl(_dom.inst_id);
	msg.ep_desc.pe_addr = htonl(_dom.pe_addr);
	msg.ep_desc.remote_event = htonl(uep->ep_idx->idx);
	memcpy(&msg.ep_desc.smsg_attr, &uep->local_smsg_attr, sizeof(msg.ep_desc.smsg_attr));
	memcpy(msg.sig, ZAP_UGNI_SIG, sizeof(ZAP_UGNI_SIG));
	ZAP_VERSION_SET(msg.ver);
	n = write(uep->sock, &msg, sizeof(msg));
	if (n != sizeof(msg)) {
		uep->ep.state = ZAP_EP_ERROR;
		z_ugni_disable_sock(uep);
		/* post CONN_ERR event to zq */
		z_ugni_zq_try_post(uep, 0, ZAP_EVENT_CONNECT_ERROR, ZAP_ERR_ENDPOINT);
		return EIO;
	}
	return 0;
}

static int z_ugni_sock_send_conn_accept(struct z_ugni_ep *uep)
{
	/* NOTE: This is not the application accept message. It is a socket
	 *       message agreeing to establish GNI SMSG communication. */
	/* uep->ep.lock is held */
	int n;
	struct z_ugni_sock_msg_conn_accept msg;

	CONN_LOG("%p sock-sending conn_accept\n", uep);

	assert(uep->ep.state == ZAP_EP_ACCEPTING);

	msg.hdr.msg_len = htonl(sizeof(msg));
	msg.hdr.msg_type = htons(ZAP_UGNI_MSG_ACCEPTED);
	msg.ep_desc.inst_id = htonl(_dom.inst_id);
	msg.ep_desc.pe_addr = htonl(_dom.pe_addr);
	msg.ep_desc.remote_event = htonl(uep->ep_idx->idx);
	memcpy(&msg.ep_desc.smsg_attr, &uep->local_smsg_attr, sizeof(msg.ep_desc.smsg_attr));
	n = write(uep->sock, &msg, sizeof(msg));
	if (n != sizeof(msg)) {
		assert(0 == "cannot write");
		/* REASON: msg is super small and it is the only message to be
		 *         sent on the socket. So, we expect it to successfully
		 *         copied over to the kernel buffer in one go. */
		uep->ep.state = ZAP_EP_ERROR;
		z_ugni_disable_sock(uep);
		return EIO;
	}
	return 0;
}

static int z_ugni_setup_conn(struct z_ugni_ep *uep, struct z_ugni_ep_desc *ep_desc)
{
	gni_return_t grc;
	CONN_LOG("%p setting up GNI connection\n");
	/* bind remote endpoint */
	Z_GNI_API_LOCK(uep->ep.thread);
	grc = GNI_EpBind(uep->gni_ep, ep_desc->pe_addr, ep_desc->inst_id);
	Z_GNI_API_UNLOCK(uep->ep.thread);
	if (grc != GNI_RC_SUCCESS) {
		LOG_(uep, "GNI_EpBind() error: %d\n", grc);
		goto out;
	}
	/* set remote event data as remote peer requested */
	CONN_LOG("%p Setting event data, local: %x, remote: %x\n",
			uep, uep->ep_idx->idx, ep_desc->remote_event);
	Z_GNI_API_LOCK(uep->ep.thread);
	grc = GNI_EpSetEventData(uep->gni_ep, uep->ep_idx->idx, ep_desc->remote_event);
	Z_GNI_API_UNLOCK(uep->ep.thread);
	if (grc != GNI_RC_SUCCESS) {
		LOG_(uep, "GNI_EpSetEventData() error: %d\n", grc);
		goto out;
	}
	/* smsg init */
	memcpy(&uep->remote_smsg_attr, &ep_desc->smsg_attr, sizeof(ep_desc->smsg_attr));
	Z_GNI_API_LOCK(uep->ep.thread);
	grc = GNI_SmsgInit(uep->gni_ep, &uep->local_smsg_attr, &uep->remote_smsg_attr);
	Z_GNI_API_UNLOCK(uep->ep.thread);
	if (grc != GNI_RC_SUCCESS) {
		LOG_(uep, "GNI_SmsgInit() error: %d\n", grc);
		goto out;
	}
	CONN_LOG("%p GNI endpoint bound\n");
	uep->ugni_ep_bound = 1;
 out:
	return gni_rc_to_errno(grc);
}

static void z_ugni_sock_recv(struct z_ugni_ep *uep)
{
	/* uep->ep.lock is held */
	int n, mlen, rc;
	zap_err_t zerr;
	struct z_ugni_sock_msg_conn_req *conn_req;
	struct z_ugni_sock_msg_conn_accept *conn_accept;

	/* read full header first */
	while (uep->sock_off < sizeof(struct zap_ugni_msg_hdr)) {
		/* need to get the entire header to know the full msg len */
		n = sizeof(struct zap_ugni_msg_hdr) - uep->sock_off;
		n = read(uep->sock, uep->sock_buff.buff + uep->sock_off, n);
		if (n < 0) {
			if (errno == EAGAIN) /* this is OK */
				return;
			/* Otherwise, read error */
			goto err;
		}
		uep->sock_off += n;
	}
	mlen = ntohl(uep->sock_buff.hdr.msg_len);
	/* read entire message */
	while (uep->sock_off < mlen) {
		n = read(uep->sock, uep->sock_buff.buff + uep->sock_off,
				    mlen - uep->sock_off);
		if (n < 0) {
			if (errno == EAGAIN) /* this is OK */
				return;
			/* Otherwise, read error */
			goto err;
		}
		uep->sock_off += n;
	}
	/* network-to-host */
	uep->sock_buff.hdr.msg_len = ntohl(uep->sock_buff.hdr.msg_len);
	uep->sock_buff.hdr.msg_type = ntohs(uep->sock_buff.hdr.msg_type);
	switch (uep->sock_buff.hdr.msg_type) {
	case ZAP_UGNI_MSG_CONNECT:
		/* validate version and signature */
		conn_req = &uep->sock_buff.conn_req;
		if (!ZAP_VERSION_EQUAL(conn_req->ver)) {
			LOG_(uep, "zap_ugni: Receive conn request "
				  "from an unsupported version "
				  "%hhu.%hhu.%hhu.%hhu\n",
				  conn_req->ver.major, conn_req->ver.minor,
				  conn_req->ver.patch, conn_req->ver.flags);
			goto err;
		}
		if (memcmp(conn_req->sig, ZAP_UGNI_SIG, sizeof(ZAP_UGNI_SIG))) {
			LOG_(uep, "Expecting sig '%s', but got '%.*s'.\n",
				  ZAP_UGNI_SIG, sizeof(conn_req->sig),
				  conn_req->sig);
			goto err;
		}
		conn_req->ep_desc.inst_id = ntohl(conn_req->ep_desc.inst_id);
		conn_req->ep_desc.pe_addr = ntohl(conn_req->ep_desc.pe_addr);
		conn_req->ep_desc.remote_event = ntohl(conn_req->ep_desc.remote_event);
		if (uep->ep.state != ZAP_EP_ACCEPTING) {
			LOG_(uep, "Get z_ugni_sock_msg_conn_req message while "
				  "endpoint in %s state\n",
				  __zap_ep_state_str(uep->ep.state));
			goto err;
		}
		CONN_LOG("%p sock-recv conn_msg\n", uep);
		rc = z_ugni_setup_conn(uep, &conn_req->ep_desc);
		if (rc)
			goto err;
		rc = z_ugni_sock_send_conn_accept(uep);
		if (rc)
			goto err;
		/* GNI SMSG established, socket not needed anymore */
		z_ugni_disable_sock(uep);
		break;
	case ZAP_UGNI_MSG_ACCEPTED:
		conn_accept = &uep->sock_buff.conn_accept;
		conn_accept->ep_desc.inst_id = ntohl(conn_accept->ep_desc.inst_id);
		conn_accept->ep_desc.pe_addr = ntohl(conn_accept->ep_desc.pe_addr);
		conn_accept->ep_desc.remote_event = ntohl(conn_accept->ep_desc.remote_event);
		if (uep->ep.state != ZAP_EP_CONNECTING) {
			LOG_(uep, "Get z_ugni_sock_msg_conn_accept message "
				  "while endpoint in %s state\n",
				  __zap_ep_state_str(uep->ep.state));
			goto err;
		}
		CONN_LOG("%p sock-recv conn_accept\n", uep);
		rc = z_ugni_setup_conn(uep, &conn_accept->ep_desc);
		if (rc)
			goto err;
		/* GNI SMSG established, socket not needed anymore */
		z_ugni_disable_sock(uep);
		zerr = z_ugni_send_connect(uep);
		if (zerr) {
			LOG_(uep, "z_ugni_send_connect() failed: %d\n", zerr);
			goto err;
		}
		break;
	default:
		/* rogue message */
		LOG_(uep, "Get unexpected message type: %d\n", uep->sock_buff.hdr.msg_type);
		goto err;
	}
	return;
 err:
	z_ugni_disable_sock(uep);
	switch (uep->ep.state) {
	case ZAP_EP_CONNECTING:
		uep->ep.state = ZAP_EP_ERROR;
		pthread_mutex_unlock(&uep->ep.lock);
		z_ugni_deliver_conn_error(uep);
		pthread_mutex_lock(&uep->ep.lock);
		break;
	case ZAP_EP_ACCEPTING:
		/* application does not know about this endpoint yet */
		uep->ep.state = ZAP_EP_ERROR;
		zap_put_ep(&uep->ep); /* b/c zap_new() in conn_req */
		break;
	default:
		assert(0 == "Unexpected endpoint state");
	}
}

static void z_ugni_sock_hup(struct z_ugni_ep *uep)
{
	z_ugni_disable_sock(uep);
	if (uep->ugni_ep_bound) /* if gni_ep is bounded, ignore sock HUP */
		return;
	switch (uep->ep.state) {
	case ZAP_EP_CONNECTING:
		uep->ep.state = ZAP_EP_ERROR;
		pthread_mutex_unlock(&uep->ep.lock);
		z_ugni_deliver_conn_error(uep);
		pthread_mutex_lock(&uep->ep.lock);
		break;
	case ZAP_EP_ACCEPTING:
		/* application does not know about this endpoint yet */
		uep->ep.state = ZAP_EP_ERROR;
		zap_put_ep(&uep->ep); /* b/c zap_new() in conn_req */
		break;
	default:
		assert(0 == "Unexpected endpoint state");
	}
}

/* cm event over sock */
static void z_ugni_handle_sock_event(struct z_ugni_ep *uep, int events)
{
	struct z_ugni_io_thread *thr = (void*)uep->ep.thread;
	struct epoll_event ev;
	zap_get_ep(&uep->ep);
	pthread_mutex_lock(&uep->ep.lock);
	if (uep->ep.state == ZAP_EP_LISTENING) {
		/* This is a listening endpoint */
		if (events != EPOLLIN) {
			LOG("Listening endpoint expecting EPOLLIN(%d), "
			    "but got: %d\n", EPOLLIN, events);
			goto out;
		}
		z_ugni_sock_conn_request(uep);
		goto out;
	}
	if (events & EPOLLHUP) {
		z_ugni_sock_hup(uep);
		goto out;
	}
	if (events & EPOLLOUT) {
		/* just become sock-connected */
		assert(uep->sock_connected == 0);
		uep->sock_connected = 1;
		CONN_LOG("uep %p becoming sock-connected\n", uep);
		ev.events = EPOLLIN;
		ev.data.ptr = &uep->sock_epoll_ctxt;
		epoll_ctl(thr->efd, EPOLL_CTL_MOD, uep->sock, &ev);
		if (uep->ep.state == ZAP_EP_CONNECTING) {
			/* send connect message */
			z_ugni_sock_send_conn_req(uep);
		}
	}
	if ((events & EPOLLIN) && uep->sock >= 0) {
		/* NOTE: sock may be disabled by z_ugni_sock_XXX(uep) above */
		z_ugni_sock_recv(uep);
	}
 out:
	pthread_mutex_unlock(&uep->ep.lock);
	zap_put_ep(&uep->ep);
}

static void z_ugni_zq_post(struct z_ugni_io_thread *thr, struct z_ugni_ev *uev)
{
	static const char c = 1;
	struct rbn *rbn;
	uint64_t ts_min = -1;

	assert(uev->in_zq == 0);
	if (uev->in_zq) {
		LLOG("WARNING: Trying to insert zq entry that is already in zq\n");
		return;
	}

	pthread_mutex_lock(&thr->zap_io_thread.mutex);
	rbn = rbt_min(&thr->zq);
	if (rbn) {
		ts_min = ((struct z_ugni_ev*)rbn)->ts_msec;
	}
	rbt_ins(&thr->zq, &uev->rbn);
	uev->in_zq = 1;
	if (uev->ts_msec < ts_min) /* notify the thread if wait time changed */
		write(thr->zq_fd[1], &c, 1);
	pthread_mutex_unlock(&thr->zap_io_thread.mutex);
}

/*
 * type is ZAP_EVENT_CONNECTED, ZAP_EVENT_DISCONNECTED or
 * ZAP_EVENT_CONNECT_ERROR.
 */
static void z_ugni_zq_try_post(struct z_ugni_ep *uep, uint64_t ts_msec, int type, int status)
{
	/* acquire the uep->uev */
	if (__atomic_test_and_set(&uep->uev.acq, __ATOMIC_ACQUIRE)) {
		/* Failed to acquire the event. This means that the `uev` has
		 * already been used to post in zq. So, we can safely ignore
		 * this. This can happen, for example, by application thread
		 * calling `zap_close()` that races with on-going connect error
		 * handling from zap ugni thread.
		 */
		return;
	}
	zap_get_ep(&uep->ep); /* will be put when uev is processed */
	uep->uev.ts_msec = ts_msec;
	rbn_init(&uep->uev.rbn, &uep->uev.ts_msec);
	uep->uev.zev.ep = &uep->ep;
	uep->uev.zev.type = type;
	uep->uev.zev.status = status;
	z_ugni_zq_post((void*)uep->ep.thread, &uep->uev);
}

static void z_ugni_zq_rm(struct z_ugni_io_thread *thr, struct z_ugni_ev *uev)
{
	struct z_ugni_ep *uep = container_of(uev, struct z_ugni_ep, uev);
	static const char c = 1;
	struct rbn *rbn;
	uint64_t ts_min0 = -1, ts_min1 = -1;
	assert(uev->in_zq == 1);
	if (!uev->in_zq) {
		LLOG("WARNING: Trying to remove zq entry that is not in zq\n");
		return;
	}
	pthread_mutex_lock(&thr->zap_io_thread.mutex);
	rbn = rbt_min(&thr->zq);
	if (rbn) {
		ts_min0 = ((struct z_ugni_ev*)rbn)->ts_msec;
	}
	rbt_del(&thr->zq, &uev->rbn);
	uev->acq = 0;
	uev->in_zq = 0;
	rbn = rbt_min(&thr->zq);
	if (rbn) {
		ts_min1 = ((struct z_ugni_ev*)rbn)->ts_msec;
	}
	if (ts_min0 != ts_min1) /* notify the thread if wait time changed */
		write(thr->zq_fd[1], &c, 1);
	pthread_mutex_unlock(&thr->zap_io_thread.mutex);
	zap_put_ep(&uep->ep); /* taken in __post_zq() */
}

/* return timeout in msec */
static int z_ugni_handle_zq_events(struct z_ugni_io_thread *thr, int events)
{
	char c;
	struct z_ugni_ep *uep;
	struct z_ugni_ev *uev;
	struct timespec ts;
	uint64_t ts_msec;
	int timeout = -1;
	while (read(thr->zq_fd[0], &c, 1) == 1) {
		/* clear the notification channel */ ;
	}
	pthread_mutex_lock(&thr->zap_io_thread.mutex);
	while ((uev = (void*)rbt_min(&thr->zq))) {
		clock_gettime(CLOCK_REALTIME, &ts);
		ts_msec = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
		if (ts_msec < uev->ts_msec) {
			timeout = uev->ts_msec - ts_msec;
			break;
		}
		assert(uev->in_zq == 1);
		rbt_del(&thr->zq, &uev->rbn);
		uev->in_zq = 0;
		uev->acq = 0;
		pthread_mutex_unlock(&thr->zap_io_thread.mutex);

		uep = (void*)uev->zev.ep;
		switch (uev->zev.type) {
		case ZAP_EVENT_CONNECT_ERROR:
		case ZAP_EVENT_DISCONNECTED:
			zap_io_thread_ep_release(&uep->ep);
			pthread_mutex_lock(&uep->ep.lock);
			z_ugni_flush(uep);
			pthread_mutex_unlock(&uep->ep.lock);
			CONN_LOG("%p delivering last event: %s\n",
				 uep, zap_event_str(uev->zev.type));
			zap_event_deliver(&uev->zev);
			zap_put_ep(&uep->ep); /* taken in z_ugni_connect()/z_ugni_accept() */
			break;
		default:
			LLOG("Unexpected event in zq: %s(%d)\n",
				zap_event_str(uev->zev.type),
				uev->zev.type);
			assert(0 == "Unexpected event in zq");
		}

		zap_put_ep(&uep->ep); /* taken in __post_zq() */
		pthread_mutex_lock(&thr->zap_io_thread.mutex);
	}
	pthread_mutex_unlock(&thr->zap_io_thread.mutex);
	return timeout;
}

static void *z_ugni_io_thread_proc(void *arg)
{
	struct z_ugni_io_thread *thr = arg;
	static const int N_EV = 512;
	int i, n;
	int timeout;
	struct z_ugni_epoll_ctxt *ctxt;
	struct z_ugni_ep *uep;
	struct epoll_event ev[N_EV];

	pthread_cleanup_push(z_ugni_io_thread_cleanup, thr);

 loop:
	timeout = z_ugni_handle_zq_events(thr, ev[i].events);
	zap_thrstat_wait_start(thr->zap_io_thread.stat);
	n = epoll_wait(thr->efd, ev, N_EV, timeout);
	zap_thrstat_wait_end(thr->zap_io_thread.stat);
	for (i = 0; i < n; i++) {
		ctxt = ev[i].data.ptr;
		switch (ctxt->type) {
		case Z_UGNI_CQ_EVENT:
			z_ugni_handle_cq_event(thr, ev[i].events);
			break;
		case Z_UGNI_SOCK_EVENT:
			uep = container_of(ctxt, struct z_ugni_ep, sock_epoll_ctxt);
			z_ugni_handle_sock_event(uep, ev[i].events);
			break;
		case Z_UGNI_ZQ_EVENT:
			z_ugni_handle_zq_events(thr, ev[i].events);
			break;
		default:
			LOG("Unexpected type: %d\n", ctxt->type);
			assert(0 == "Bad type!");
			break;
		}
	}

	goto loop;

	pthread_cleanup_pop(1);

	return NULL;
}

static int zqe_cmp(void *tree_key, const void *key)
{
	return (int*)tree_key - (int*)key;
}

zap_io_thread_t z_ugni_io_thread_create(zap_t z)
{
	int rc;
	struct z_ugni_io_thread *thr;
	struct epoll_event ev;

	CONN_LOG("IO thread create\n");

	thr = malloc(sizeof(*thr));
	if (!thr)
		goto err_0;
	STAILQ_INIT(&thr->stalled_wrq);
	rbt_init(&thr->zq, zqe_cmp);
	CONN_LOG("zap thread initializing ...\n");
	rc = zap_io_thread_init(&thr->zap_io_thread, z, "zap_ugni_io",
			ZAP_ENV_INT(ZAP_THRSTAT_WINDOW));
	CONN_LOG("zap thread initialized\n");
	if (rc)
		goto err_1;
	CONN_LOG("ep_idx initializing ...\n");
	z_ugni_ep_idx_init(thr);
	CONN_LOG("setting up mbox ...\n");
	rc = z_ugni_io_thread_mbox_setup(thr);
	CONN_LOG("mbox setup done.\n");
	if (rc)
		goto err_2;
	thr->efd = epoll_create1(O_CLOEXEC);
	if (thr->efd < 0)
		goto err_3;
	/*
	 * NOTE on GNI_CQ_BLOCKING
	 * In order to use Completion Channel, GNI_CQ_BLOCKING is required.
	 * `GNI_CqGetEvent()` is still a non-blocking call.
	 */

	/* For local/source completions (sends/posts) */
	Z_GNI_API_LOCK(&thr->zap_io_thread);
	CONN_LOG("send CqCreate ...\n");
	rc = GNI_CqCreate(_dom.nic, ZAP_UGNI_SCQ_DEPTH, 0, GNI_CQ_BLOCKING, NULL, NULL, &thr->scq);
	CONN_LOG("send CqCreate ... done.\n");
	Z_GNI_API_UNLOCK(&thr->zap_io_thread);
	if (rc != GNI_RC_SUCCESS) {
		LLOG("GNI_CqCreate() failed: %s(%d)\n", gni_ret_str(rc), rc);
		goto err_4;
	}
	/* For remote/destination completion (recv) */
	Z_GNI_API_LOCK(&thr->zap_io_thread);
	CONN_LOG("recv CqCreate ...\n");
	rc = GNI_CqCreate(_dom.nic, ZAP_UGNI_RCQ_DEPTH, 0, GNI_CQ_BLOCKING, NULL, NULL, &thr->rcq);
	CONN_LOG("recv CqCreate ... done\n");
	Z_GNI_API_UNLOCK(&thr->zap_io_thread);
	if (rc != GNI_RC_SUCCESS) {
		LLOG("GNI_CqCreate() failed: %s(%d)\n", gni_ret_str(rc), rc);
		goto err_5;
	}
	Z_GNI_API_LOCK(&thr->zap_io_thread);
	CONN_LOG("Registering mbox ...\n");
	rc = GNI_MemRegister(_dom.nic, (uint64_t)thr->mbox,
			     ZAP_UGNI_THREAD_EP_MAX * thr->mbox_sz, thr->rcq,
			     GNI_MEM_READWRITE | GNI_MEM_RELAXED_PI_ORDERING,
			     -1, &thr->mbox_mh);
	CONN_LOG("Registering mbox ... done\n");
	Z_GNI_API_UNLOCK(&thr->zap_io_thread);
	if (rc != GNI_RC_SUCCESS) {
		LLOG("GNI_MemRegister() failed: %s(%d)\n", gni_ret_str(rc), rc);
		goto err_6;
	}
	Z_GNI_API_LOCK(&thr->zap_io_thread);
	CONN_LOG("CompChanCreate ...\n");
	rc = GNI_CompChanCreate(_dom.nic, &thr->cch);
	CONN_LOG("CompChanCreate ... done\n");
	Z_GNI_API_UNLOCK(&thr->zap_io_thread);
	if (rc) {
		LLOG("GNI_CompChanCreate() failed: %s(%d)\n", gni_ret_str(rc), rc);
		goto err_7;
	}
	Z_GNI_API_LOCK(&thr->zap_io_thread);
	CONN_LOG("Get CompChanFd ...\n");
	rc = GNI_CompChanFd(thr->cch, &thr->cch_fd);
	CONN_LOG("Get CompChanFd ... done\n");
	Z_GNI_API_UNLOCK(&thr->zap_io_thread);
	if (rc) {
		LLOG("GNI_CompChanFd() failed: %s(%d)\n", gni_ret_str(rc), rc);
		goto err_8;
	}
	rc = __set_nonblock(thr->cch_fd);
	if (rc)
		goto err_8;
	Z_GNI_API_LOCK(&thr->zap_io_thread);
	CONN_LOG("send-CQ CqAttachCompChan ...\n");
	rc = GNI_CqAttachCompChan(thr->scq, thr->cch);
	CONN_LOG("send-CQ CqAttachCompChan ... done\n");
	Z_GNI_API_UNLOCK(&thr->zap_io_thread);
	if (rc) {
		LLOG("GNI_CqAttachCompChan() failed: %s(%d)\n", gni_ret_str(rc), rc);
		goto err_8;
	}
	Z_GNI_API_LOCK(&thr->zap_io_thread);
	CONN_LOG("recv-CQ CqAttachCompChan ...\n");
	rc = GNI_CqAttachCompChan(thr->rcq, thr->cch);
	CONN_LOG("recv-CQ CqAttachCompChan ... done\n");
	Z_GNI_API_UNLOCK(&thr->zap_io_thread);
	if (rc) {
		LLOG("GNI_CqAttachCompChan() failed: %s(%d)\n", gni_ret_str(rc), rc);
		goto err_8;
	}
	Z_GNI_API_LOCK(&thr->zap_io_thread);
	CONN_LOG("Arming send-CQ ...\n");
	rc = GNI_CqArmCompChan(&thr->scq, 1);
	CONN_LOG("Arming send-CQ ... done\n");
	Z_GNI_API_UNLOCK(&thr->zap_io_thread);
	if (rc) {
		LLOG("GNI_CqArmCompChan() failed: %s(%d)\n", gni_ret_str(rc), rc);
		goto err_8;
	}
	Z_GNI_API_LOCK(&thr->zap_io_thread);
	CONN_LOG("Arming recv-CQ ...\n");
	rc = GNI_CqArmCompChan(&thr->rcq, 1);
	CONN_LOG("Arming recv-CQ ... done\n");
	Z_GNI_API_UNLOCK(&thr->zap_io_thread);
	if (rc) {
		LLOG("GNI_CqArmCompChan() failed: %s(%d)\n", gni_ret_str(rc), rc);
		goto err_8;
	}
	CONN_LOG("Creating zq notification pipe ...\n");
	rc = pipe2(thr->zq_fd, O_NONBLOCK|O_CLOEXEC);
	CONN_LOG("Creating zq notification pipe ... done\n");
	if (rc < 0) {
		LLOG("pipe2() failed, errno: %d\n", errno);
		goto err_8;
	}

	/* cq-epoll */
	ev.events = EPOLLIN;
	thr->cq_epoll_ctxt.type = Z_UGNI_CQ_EVENT;
	ev.data.ptr = &thr->cq_epoll_ctxt;
	CONN_LOG("Adding CompChanFd to epoll\n");
	rc = epoll_ctl(thr->efd, EPOLL_CTL_ADD, thr->cch_fd, &ev);
	if (rc)
		goto err_9;

	/* zq-epoll */
	ev.events = EPOLLIN;
	thr->zq_epoll_ctxt.type = Z_UGNI_ZQ_EVENT;
	ev.data.ptr = &thr->zq_epoll_ctxt;
	CONN_LOG("Adding zq fd to epoll\n");
	rc = epoll_ctl(thr->efd, EPOLL_CTL_ADD, thr->zq_fd[0], &ev);
	if (rc)
		goto err_9;

	CONN_LOG("Creating pthread\n");
	rc = pthread_create(&thr->zap_io_thread.thread, NULL,
			    z_ugni_io_thread_proc, thr);
	if (rc)
		goto err_9;
	pthread_mutex_unlock(&ugni_lock);
	pthread_setname_np(thr->zap_io_thread.thread, "zap_ugni_io");
	CONN_LOG("returning.\n");
	return &thr->zap_io_thread;

 err_9:
	close(thr->zq_fd[0]);
	close(thr->zq_fd[1]);
 err_8:
	Z_GNI_API_LOCK(&thr->zap_io_thread);
	GNI_CompChanDestroy(thr->cch);
	Z_GNI_API_UNLOCK(&thr->zap_io_thread);
 err_7:
	Z_GNI_API_LOCK(&thr->zap_io_thread);
	GNI_MemDeregister(_dom.nic, &thr->mbox_mh);
	Z_GNI_API_UNLOCK(&thr->zap_io_thread);
 err_6:
	Z_GNI_API_LOCK(&thr->zap_io_thread);
	GNI_CqDestroy(thr->rcq);
	Z_GNI_API_UNLOCK(&thr->zap_io_thread);
 err_5:
	Z_GNI_API_LOCK(&thr->zap_io_thread);
	GNI_CqDestroy(thr->scq);
	Z_GNI_API_UNLOCK(&thr->zap_io_thread);
 err_4:
	close(thr->efd);
 err_3:
	free(thr->mbox);
 err_2:
	zap_io_thread_release(&thr->zap_io_thread);
 err_1:
	free(thr);
 err_0:
	return NULL;
}

zap_err_t z_ugni_io_thread_cancel(zap_io_thread_t t)
{
	struct z_ugni_io_thread *thr = (void*)t;
	int rc;
	rc = pthread_cancel(t->thread);
	switch (rc) {
	case ESRCH: /* cleaning up structure w/o running thread b/c of fork */
		thr->cch = 0;
		thr->rcq = 0;
		thr->scq = 0;
		thr->mbox_mh.qword1 = 0;
		thr->mbox_mh.qword2 = 0;
		thr->efd = -1; /* b/c of O_CLOEXEC */
		thr->zq_fd[0] = -1; /* b/c of O_CLOEXEC */
		thr->zq_fd[1] = -1; /* b/c of O_CLOEXEC */
		z_ugni_io_thread_cleanup(thr);
	case 0:
		return ZAP_ERR_OK;
	default:
		return ZAP_ERR_LOCAL_OPERATION;
	}
}

zap_err_t z_ugni_io_thread_ep_assign(zap_io_thread_t t, zap_ep_t ep)
{
	/* assign ep_idx and mbox */
	struct z_ugni_ep *uep = (void*)ep;
	struct z_ugni_io_thread *thr = (void*)t;
	zap_err_t zerr;
	gni_return_t grc;
	int rc;

	CONN_LOG("assigning endpoint %p to thread %p\n", uep, t->thread);

	pthread_mutex_lock(&t->mutex);

	/* obtain idx */
	rc = z_ugni_ep_idx_assign(uep);
	if (rc) {
		zerr = zap_errno2zerr(rc);
		goto out;
	}

	/* setup local_smsg_attr */
	uep->local_smsg_attr.msg_type = GNI_SMSG_TYPE_MBOX_AUTO_RETRANSMIT;
	uep->local_smsg_attr.msg_buffer = thr->mbox;
	uep->local_smsg_attr.buff_size = thr->mbox_sz;
	uep->local_smsg_attr.mem_hndl = thr->mbox_mh;
	uep->local_smsg_attr.mbox_maxcredit = ZAP_UGNI_EP_RQ_DEPTH;
	uep->local_smsg_attr.msg_maxsize = ZAP_UGNI_MSG_SZ_MAX;
	uep->local_smsg_attr.mbox_offset = thr->mbox_sz * uep->ep_idx->idx;

	CONN_LOG("%p mbox_offset: %d\n", uep, uep->local_smsg_attr.mbox_offset);
	CONN_LOG("%p mbox_idx: %d\n", uep,  uep->ep_idx->idx);

	/* allocate GNI ednpoint. We need to do it here instead of zap_new()
	 * because we don't know which cq to attached to yet. */
	grc = GNI_EpCreate(_dom.nic, thr->scq, &uep->gni_ep);
	if (grc) {
		LOG("GNI_EpCreate() failed: %s\n", gni_ret_str(grc));
		zerr = ZAP_ERR_RESOURCE;
		goto out;
	}
	CONN_LOG("%p created gni_ep %p\n", uep, uep->gni_ep);
	zerr = ZAP_ERR_OK;
 out:
	pthread_mutex_unlock(&t->mutex);
	return zerr;
}

zap_err_t z_ugni_io_thread_ep_release(zap_io_thread_t t, zap_ep_t ep)
{
	/* release ep_idx and mbox */
	pthread_mutex_lock(&t->mutex);
	z_ugni_ep_idx_release((void*)ep);
	pthread_mutex_unlock(&t->mutex);
	z_ugni_ep_release((void*)ep);
	return ZAP_ERR_OK;
}

zap_err_t zap_transport_get(zap_t *pz, zap_log_fn_t log_fn,
			    zap_mem_info_fn_t mem_info_fn)
{
	zap_t z;
	if (log_fn)
		zap_ugni_log = log_fn;
	if (!init_complete && init_once())
		goto err;

	z = calloc(1, sizeof (*z));
	if (!z)
		goto err;

	z->max_msg = ZAP_UGNI_MSG_SZ_MAX - sizeof(struct zap_ugni_msg);
	z->new = z_ugni_new;
	z->destroy = z_ugni_destroy;
	z->connect = z_ugni_connect;
	z->accept = z_ugni_accept;
	z->reject = z_ugni_reject;
	z->listen = z_ugni_listen;
	z->close = z_ugni_close;
	z->send = z_ugni_send;
	z->read = z_ugni_read;
	z->write = z_ugni_write;
	z->map = z_ugni_map;
	z->unmap = z_ugni_unmap;
	z->share = z_ugni_share;
	z->get_name = z_get_name;
	z->io_thread_create = z_ugni_io_thread_create;
	z->io_thread_cancel = z_ugni_io_thread_cancel;
	z->io_thread_ep_assign = z_ugni_io_thread_ep_assign;
	z->io_thread_ep_release = z_ugni_io_thread_ep_release;

	/* is it needed? */
	z->mem_info_fn = mem_info_fn;

	*pz = z;
	return ZAP_ERR_OK;

 err:
	return ZAP_ERR_RESOURCE;
}

void z_ugni_list_dump()
{
	struct z_ugni_ep *uep;
	int n = 0;
	pthread_mutex_lock(&z_ugni_list_mutex);
	LOG("==== z_ugni_list_dump ====\n");
	LIST_FOREACH(uep, &z_ugni_list, link) {
		LOG("    uep: %p, state: %s(%d)\n", uep, __zap_ep_state_str(uep->ep.state), uep->ep.state);
		n++;
	}
	LOG("    total: %d endpoints\n", n);
	LOG("-------------------------\n");
	pthread_mutex_unlock(&z_ugni_list_mutex);
}
