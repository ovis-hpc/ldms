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

#include <sys/syscall.h>

#include "ovis_util/os_util.h"
#include "coll/rbt.h"
#include "mmalloc/mmalloc.h"

#include "zap_ugni.h"

#ifdef EP_LOG_ENABLED
#define EP_LOG(EP, FMT, ...) do { \
	struct timespec __t; \
	pid_t _tid = syscall(SYS_gettid); \
	clock_gettime(CLOCK_REALTIME, &__t); \
	fprintf((EP)->log, "[%ld.%09ld] (%d) line:%d " FMT, \
			__t.tv_sec, __t.tv_nsec, _tid, \
			__LINE__, ## __VA_ARGS__); \
} while (0)
#else
#define EP_LOG(EP, FMT, ...) /* no-op */
#endif


/*
 * NOTE on `zap_ugni`
 * ==================
 *
 * TCP sockets are used for handling connections (listen/connect/accept) and
 * messaging. GNI is only used for read and write operations.
 *
 *
 * Threads and endpoints
 * ---------------------
 *
 * `io_thread_proc()` is the thread procedure for all `zap_ugni` zap threads.
 * The threads use `epoll` to manage events from 1) sockets, 2) the GNI
 * Completion Queue (CQ) via Completion Channel, and 3) the `zap_ugni` event
 * queue `zq` for timeout events and connection events.
 *
 * Currently, when an endpoint is assigned to a thread, it stays there
 * until it is destroyed (no thread migration).
 *
 * Regarding GNI resources, each thread has 1 completion channel, and 1 local
 * CQ (scq). They are shared among the endpoints assigned to the thread.
 *
 * The completion channel has a file descriptor that can be used with epoll. The
 * CQ is attached to the completion channel. After the CQ is armed, they will
 * notify the completion channel when a completion entry is available to be
 * processed. This makes completion channel file descriptor ready to be read and
 * epoll wakes the thread up to process the completion events.
 *
 * The `zq` is an additional event queue that manages timeout events and
 * connection events (CONNECTED, CONN_ERROR and DISCONNECTED). The connection
 * events could result in a recursive application callback or calling
 * application callback from a thread other than the associated io thread. The
 * connection events are put into `zq` to avoid such situations. An example
 * would be a synchronous error of send/read/write that may be called from other
 * theads or from within the application callback path. The synchronous error
 * immediately put the endpoint in error state, then resulted in either
 * DISCONNECT or CONN_ERROR events in `zq`.
 *
 *
 * Unsuccessful tries with SMSG
 * ----------------------------
 *
 * `zap_ugni` used to rely on TCP socket for messaging while the GNI endpoint
 * only handled RDMA operation. When the multiple-zap-io-thread model was
 * introduced, it was a good time to also modify `zap_ugni` to not rely on TCP
 * socket for send/recv and use pure GNI utilities.
 *
 * At first, we tried SMSG with 1 remote cq to handle recv, and 1 local cq to
 * handle both SMSG send completion and RDMA GET/PUT (read/write) completions.
 * In the first setup, the application threads can directly call
 * `GNI_SmsgSend()` and `GNI_PostRdma()` (but the calls are protected by the
 * api mutex). This setup however quickly resulted in a `GNI_RC_INVALID_PARAM`
 * returned by `GNI_PostRdma()`.
 *
 * So, we moved the `GNI_SmsgSend()` and `GNI_PostRdma()` calls into the IO
 * thread, so that we limit the cq's access to only a single thread (still
 * protected by the api mutex). It delayed the error, but eventually we still
 * get `GNI_RC_INVALID_PARAM` from `GNI_PostRdma()`. Some other times, the test
 * program stalled. The posted RDMA requests were never completed (even when we
 * desperately try to reap CQ every 100 ms).
 *
 * The next setup was having 1 remote cq for SMSG recv, 1 local smsg send cq,
 * and 1 local rdma cq. This yielded the same results as the previous setup.
 *
 * So, finally, we tried implementing zap_ugni messaging using RDMA PUT instead.
 *
 *
 * Unsuccessful tries with messaging over RDMA PUT
 * -----------------------------------------------
 * We were not successful with RDMA PUT for messaging either. With 5000
 * conections, we eventually ended up with RDMA PUT requests stuck in the CQ and
 * they were never completed. Please see the DEPRECATED Documentation below for
 * more information about messaging over RDMA PUT.
 *
 *
 *
 *//*-----------------------------------------------------------------------*//*

 *
 * DEPRECATED Documentations
 * =========================
 *
 *
 * Messaging over RDMA PUT (DEPRECATED)
 * ------------------------------------
 *
 * The `dst_cq_hndl` parameter in `GNI_MemRegister()` is the destination
 * completion queue that will be notified when a remote pee RDMA or FMA PUT data
 * into the memory region. So, we use this mechanism to implement `zap_ugni`
 * message send/recv.
 *
 * Let's first laid out resources related to send/recv.
 *
 * * Each IO thread handles multiple endpoints. When an endpoint is assigned to
 *   an IO thread, it stays there (no thread migration).
 * * When and enpoint is assigned to a thread, an endpoint index (`ep_idx`) is
 *   also assigned to it. The thread can use `ep_idx` to get to the endpoint,
 *   and thread-provided resources assoicated to the endpoint.
 * * Each IO thread has:
 *   * Array of message buffers (mbuf). mbuf[idx] is a send-and-recv buffer for
 *     the endpoint with index `idx`. The entire mbuf array is
 *     `GNI_MemRegister()` so that the endpoint can use the send buffer part to
 *     send messages with RDMA PUT, and the recv buffer part to receive the RDMA
 *     PUT from the peer.
 *   * 1 recv cq (rcq). With `GNI_MemRegister(mbuf, rcq)`, GNI system will
 *     put a CQ entry into `rcq` when the peer RDMA PUT data into mbuf.
 *   * 1 local cq (scq) -- a local submission completion queue. Note that scq is
 *     used for `zap_read` (RDMA GET), `zap_write` (RDMA PUT), `zap_send` (RDMA
 *     PUT) and other `zap_ugni`-specific message send (RDMA PUT).
 * * The mbuf for an endpoint contains:
 *   * mbuf.rbuf[N] : N recv buffer (see struct z_ugni_msg_buf_ent).
 *   * mbuf.sbuf[N] : N send buffer (see struct z_ugni_msg_buf_ent).
 *     * our sbuf[i] will RDMA PUT to peer's rbuf[i] and vice-versa.
 *     * sbuf and rbuf are `struct z_ugni_msg_buf_ent`, which is a chunk of
 *       memory described as follows:
 *       * msg ( MAX_LEN - 8 bytes )
 *       * status ( last 8 bytes ) : currently only have 1 bitfield:
 *         * processed
 *           * For send buffer, processed being 1 means the send has completed
 *           * For recv buffer, processed being 1 means the buffer has already
 *             been processed.
 *   * mbuf.curr_rbuf_idx : the index of current rbuf to be processed
 *   * mbuf.curr_sbuf_idx : the index of current sbuf to be used for RDMA PUT
 *   * mbuf.peer_rbuf_status[N] : an array of peer's rbuf status (availability).
 *                                The peer RDMA PUT into this memory to let us
 *                                know about its recv buffer status so that we
 *                                won't RDMA PUT peer's rbuf when it is not
 *                                ready.
 *   * mbuf.our_rbuf_status[N] : an array of our rbuf status. This is actually
 *                               the source to RDMA PUT to update peer's
 *                               mbuf.peer_rbuf_status[N] so that the peer knows
 *                               the status of our recv buffer. Copying
 *                               our_rbuf_status[i] to peer's
 *                               peer_rbuf_status[i] is basically an ACK,
 *                               letting the peer knows that our rbuf[i] has
 *                               been received, processed and ready for peer to
 *                               RDMA PUT new data into our rbuf[i].
 *     * rbuf_status currently has only 1 bitfield:
 *       * avail : 1 iff rbuf is availble
 *   * mbuf.ack_status[N] : track the status of RDMA PUT for ACK.
 *     * ack_status currently has 2 bitfields:
 *       * outstanding : 1 if ACK submitted but not completed
 *       * pending : 1 if we received a message but still has an outstanding
 *                   ACK. This can happen by the peer receiving our first ACK
 *                   and send another message into our rbuf before our ACK has
 *                   completed. Since our previous ACK is still not completed,
 *                   we will mark the pending bit to 1, and the ACK of the
 *                   latest receive will be sent when the previous ACK
 *                   completed.
 *//*
 *
 * Initially, everything in `mbuf` is set to 0.
 *
 * Sending side:
 * - A zap_ugni send work request (send WR) is created.
 * - If the `pending_msg_wrq` is not empty,
 *   or ep->mbuf->sbuf[ep->mbuf->curr_sbuf_idx].status.processed == 0, (send not completed)
 *   or ep->mbuf->peer_rbuf_status[ep->mbuf->curr_sbuf_idx].avail == 0 (corresponding peer's rbuf not available)
 *   (in other words, if there are prior pending WR or our sbuf is not ready or peer's rbuf is not ready)
 *   then ==> append the WR to endpoint's pending_msg_wrq. The send mechanism
 *   ends here in this case. The pending WR in the endpoint's pending_msg_wrq
 *   will be processed later when the outstanding sbuf has completed and the
 *   corresponding remote rbuf is available.
 * - otherwise, take the current sbuf[i], set `sbuf[i].status.processed = 0`,
 *   set `peer_rbuf_status[i].avail = 0`, and assigned sbuf[i] to the WR. Then,
 *   modulo-increase `ep->mbuf->curr_sbuf_idx` (the next sbuf for the next
 *   send). Then append the WR to thread's `thr->pending_msg_wrq` and notify the
 *   thread.
 * - The thread wakes up and process the send WR by:
 *   - `GNI_RdmaPost()` RDMA PUT our WR sbuf[i] to the corresponding peer's
 *     rbuf[i]. Notice that our sbuf[i].status.processed being 0 will make
 *     peer's rbuf[i].status.processed being 0 too. Then, the peer will know
 *     that rbuf[i] is a new message.
 * - When the RDMA PUT completed, the thread wakes up again and process the
 *   srq completion entry. The WR sbuf[i].status.processed is set to 1 to mark
 *   that the sbuf[i] is ready to use (doesn't mean that the peer's rbuf[i] is
 *   ready to receive though). Note that sbuf[i] may or may not be the current
 *   sbuf to be assigned for the next RDMA PUT. At this point, the pending send
 *   WR in the endpoint `ep->pending_msg_wrq` will have a chance to try to
 *   obtain the sbuf[curr_sbuf_idx].
 *   - If sbuf[curr_sbuf_idx].status.processed == 1, and
 *        peer_rbuf_status[curr_sbuf_idx].avail == 1:
 *     then, sbuf[curr_sbuf_idx] can be assigned to the WR (and modulo-increase
 *     curr_sbuf_idx). Similar to above, the WR is moved into
 *     `thr->pending_msg_wrq` and will be processed in the same way (but we
 *     don't have to notify the thread since we're already in the thread).
 * - When the peer finished processing the rbuf[i], the peer will write to our
 *   `peer_rbuf_status[i]`. This will wake up the io thread with rcq completion
 *   event. From the event, we will get the endpoint and will try to process the
 *   pending WR in the endpoint like the send completion case above.
 *
 * - NOTE1: msg WR in the endpoint pending queue is not ready for
 *          `GNI_PostRdma()` as it does not have sbuf[i] assigned to it yet.
 *          If it were to be put into the thread pending queue, it will block
 *          other entries.
 *
 * - NOTE2: The msg WRs in the thread's `thr->pending_msg_wrq` were ready for
 *          `GNI_PostRdma()`. However, we want to control the number of
 *          outstanding GNI posts to avoid overwhelming GNI resources. Thus, the
 *          `thr->pending_msg_wrq` and `thr->msg_post_credit` are used to
 *          control the outstanding posts regarding messaging. Originally, this
 *          was not separated from RDMA WR queue. But, when we have so many RDMA
 *          requests, the msg WR could not compete because it has to obtain the
 *          sbuf before moving into the thread WR queue. This resulted in
 *          message processing starvation. Separating the message and RDMA
 *          queues solved the starvation issue.
 *//*
 * Receiving side:
 * - When our endpoint's rbuf[i] is written, the io thread wakes up and process
 *   the CQ entry in the rcq. The recv CQ entry only contain a reference to the
 *   endpoint and nothing more (it is actually the endpoint ID value we sent to
 *   the remote socket at the beginning of the connection and the remote peer
 *   set the `remote_value` in the peer's `GNI_EpSetEventData()` with the value
 *   we sent). In other words, when the io thread wakes up by the rcq entry, we
 *   don't know which rbuf or which peer_rbuf_status got written. We just know
 *   that the witten data is in rbuf or peer_rbuf_status of the endpoint
 *   identified by the endpoint ID from the rcq entry. Hence, the
 *   `curr_rbuf_idx` tracks which rbuf[i] shall be processed next. Note that the
 *   RDMA PUT may not arrive in order. Hence, io thread may wake up to find out
 *   that the current rbuf is not ready to process yet. Thus, when the io thread
 *   process rcq, it will try to consume as many endpoint rbufs as possible to
 *   process the possible out-of-order RDMA PUT delivery.
 * - If rbuf[curr_rbuf_idx].status.processed is 0 (note that the peer RDMA PUT
 *   peer's sbuf[i] with sbuf[i].status.processed = 0), the rbuf[curr_rbuf_idx]
 *   is ready to be processed.
 *   - After the rbuf[curr_rbuf_idx] is processed,
 *     rbuf[curr_rbuf_idx].status.processed is set to 1.
 *   - Now, the rbuf[curr_rbuf_idx] become available and we need to let the peer
 *     know by RDMA PUT to peer's peer_rbuf_status[curr_rbuf_idx]. This is
 *     basically a recv acknowledgement and has ACK WR to handle it. But, before
 *     we create the ACK WR, we must check if we have an outstanding ACK
 *     (ack_status[curr_rbuf_idx].outstanding == 1).
 *     - If we have an outstanding, simply:
 *       - set ack_status[curr_rbuf_idx].pending = 1, and
 *       - modulo-increase `curr_rbuf_idx` and try to process the next rbuf.
 *       - In this case, the pending ACK will be executed when the outstanding
 *         RDMA PUT of the outstanding ACK is completed.
 *     - If we don't have outstanding ACK:
 *       - set `our_rbuf_status[curr_rbuf_idx].avail = 1`.
 *       - set `ack_status[curr_rbuf_idx].outstanding = 1`.
 *       - create ACK WR for curr_rbuf_idx. It will use
 *         `our_rbuf_status[curr_rbuf_idx]` to RDMA PUT to peer's
 *         `peer_rbuf_status[curr_rbuf_idx]`. Put ACK WR into the thead
 *         pending_ack_wrq. It will be processed/submitted after the thread
 *         finishes processing completions in scq/rcq.
 * - When the RDMA PUT for the ACK WR is completed, check if
 *   ack_status[i].pending is 1. If so, submit another ACK WR for the rbuf[i]
 *   and reset ack_stats[i].pending to 0. Note that we have at most 1 ACK
 *   pending because the peer won't be able to write to our rbuf[i] if we didn't
 *   ACK (except for the first send).
 * - At the end of rcq entry processing, we also try to submit the send WR
 *   pending in the endpoint.
 *
 * - NOTE-1: We put rbuf[i].status at the end because RDMA PUT seems to copy
 *   data from low-byte to high-byte. When we place rbuf[i].status at the
 *   beginning, we expereinced receiving message with old garbage data. The
 *   speculation was that RDMA PUT was working on the *NEXT* rbuf around the
 *   time that current rbuf was completed and notified. Then, the
 *   `rbuf[next].status` being at the beginning was copied first and hence the
 *   io thread misunderstood that the next rbuf was ready and process it while
 *   it was not .. resulting in garbage message. The problem has not shown up
 *   anymore after we move the status to the back of the buffer (hence the
 *   speculation above).
 *
 *//*
 *
 * (DEPRECATED) Connecting mechanism with RDMA-PUT messaging
 * ---------------------------------------------------------
 *
 * REMARK: This section is kept for historical reason. It is deprecated.
 *
 * The following describes interactions between the active side and the passive
 * side of `zap_ugni` in connecting mechanism.
 *
 * - [passive] listens on a TCP socket (host/port).
 * - [active] creates an endpoint and TCP `connect()` to the passive side.
 * - [passive] creates an endpoint and TCP `accept()` the TCP connection.
 * - [active] becomes TCP-connected and send a `z_ugni_sock_msg_conn_req`
 *            message over the socket. The message contains information needed
 *            by the passive side to bind GNI endpoint and set endpoint event
 *            data.
 * - [passive] becomes TCP-connected, and receives `z_ugni_sock_msg_conn_req`
 *             message over the socket. If the protocol version does not match
 *             or other errors occur, terminates the TCP connection. If the
 *             protocol version matched, bind the GNI endpoint and set endpoint
 *             event data according to the information in the received message,
 *             and replies with `z_ugni_sock_msg_conn_accept` message over the
 *             socket that contain similar data needed to bind the GNI endpoint
 *             and set the endpoint event data on the active side.  Next, close
 *             the TCP socket as it is not needed anymore. The communication
 *             from this point will use messaging over RDMA PUT described in the
 *             section above.
 * - [active] receives `z_ugni_sock_msg_conn_accept` and bind the GNI endpoint
 *            and set the endpoint event data. Then, the active side closes the
 *            TCP socket as GNI endpoint is established.
 * - [NOTE] At this point both sides can use messaging over RDMA PUT to
 *          send/recv messages. The TCP socket part may be replaced with
 *          `GNI_EpPostData()` mechanisms in the future. The application data
 *          supplied in `zap_connect()`, `zap_accept()`, or `zap_reject()`
 *          happened over RDMA PUT messaging. The connection procedure continues
 *          as follows.
 * - [active] Send the `ZAP_UGNI_MSG_CONNECT` message over GNI RDMA PUT
 *            containing application connect data. A timeout event is also added
 *            into `zq`.  If the connection procedure could not complete
 *            (rejected, accepted or error) within the timeout limit, connection
 *            timeout will be processed and `CONN_ERROR` is delivered to the
 *            application.
 * - [passive] Receive the `ZAP_UGNI_MSG_CONNECT` message over the GNI messaging
 *             and notify the application about connection request with the
 *             data. The application may `zap_accept()` or `zap_reject()` the
 *             connection request, which results in the passive side
 *             GNI sending `ZAP_UGNI_MSG_ACCEPTED` with application-supplied
 *             data or `ZAP_UGNI_MSG_REJECTED` respectively. In the case of
 *             ACCEPTED, also notify the application the CONENCTED event.
 * - [active] GNI-recv either `ZAP_UGNI_MSG_ACCEPTED` or `ZAP_UGNI_MSG_REJECTED`
 *            and notifies the application accordingly (CONNECTED or REJECTED
 *            event).
 * - [NOTE] After this point, both active and passive sides can `zap_send()`,
 *          `zap_read()`, and `zap_write()`.
 *
 *
 * (DEPRECATED) Disconnecting mechanism over GNI messaging over RDMA PUT
 * --------------------------------------------------------
 *
 * REMARK: This section is kept for historical reason. It is deprecated.
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
 *           GNI-send `ZAP_UGNI_MSG_TERM(0)` to peer1. A timeout event is also
 *           added to zq to force-terminate the connection when the timeout
 *           occurs before `ZAP_UGNI_MSG_ACK_TERM(0)` is received.
 * - [peer1] GNI-recv `ZAP_UGNI_MSG_TERM(0)`. The ep state is changed to
 *           PEER_CLOSE (to prevent future application send/read/write
 *           requests). Since peer1 has never sent `ZAP_UGNI_MSG_TERM(1)`,
 *           it send the message to peer0, expecting a
 *           `ZAP_UGNI_MSG_ACK_TERM(1)` back (with timeout). Then,
 *           `ZAP_UGNI_MSG_ACK_TERM(0)` is GNI-sent to peer0 to acknowledge the
 *           TERM peer1 received from peer0. No messages will be sent any
 *           further from peer1.
 * - [peer0] GNI-recv `ZAP_UGNI_MSG_TERM(1)`. peer0 does not send another
 *           `ZAP_UGNI_MSG_TERM(0)` because it knows that it has already sent its
 *           TERM message. Then, peer0 GNI-sends `ZAP_UGNI_MSG_ACK_TERM(1)` to
 *           peer1.
 * - [peer0+peer1] GNI-recv `ZAP_UGNI_MSG_ACK_TERM(0)` and
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
	struct sockaddr_storage la = {0};
	struct sockaddr_storage ra = {0};
	char addr_str[INET_ADDRSTRLEN];
	struct sockaddr_in *l = (struct sockaddr_in *)&la;
	struct sockaddr_in *r = (struct sockaddr_in *)&ra;
	socklen_t sa_len = sizeof(la);
	size_t sz;

	sa_len = sizeof(la);
	(void) zap_get_name(ep, (struct sockaddr *)&la,
				(struct sockaddr *)&ra, &sa_len);
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
		uep->ep.z->log_fn("zap_ugni:%d %s " fmt, __LINE__, name, ##__VA_ARGS__); \
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
#define DLLOG(FMT, ...) LLOG(FMT, ## __VA_ARGS__)
#else
#define DLLOG(FMT, ...) /* no-op */
#endif

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

#define __get_ep(ep, name) zap_get_ep(ep, name, __func__, __LINE__)
#define __put_ep(ep, name) zap_put_ep(ep, name, __func__, __LINE__)

#ifdef CONN_DEBUG
#define CONN_LOG(FMT, ...) do { \
	struct timespec __t; \
	clock_gettime(CLOCK_REALTIME, &__t); \
	LLOG( "[CONN_LOG] [%ld.%09ld] " FMT, __t.tv_sec, __t.tv_nsec, ## __VA_ARGS__); \
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
/* Z_GNI_API_LOCK_TYPE:
 *   0: no lock
 *   1: lock with global mutex (ugni_lock)
 *   2: lock with thread mutex
 */
#define Z_GNI_API_LOCK_TYPE 1
#if Z_GNI_API_LOCK_TYPE == 0 /* no GNI API lock */
#  define Z_GNI_API_LOCK(thr)
#  define Z_GNI_API_UNLOCK(thr)
#elif Z_GNI_API_LOCK_TYPE == 1 /* use global ugni_lock */
#  define Z_GNI_API_LOCK(thr)   pthread_mutex_lock(&ugni_lock)
#  define Z_GNI_API_UNLOCK(thr) pthread_mutex_unlock(&ugni_lock)
#elif Z_GNI_API_LOCK_TYPE == 2 /* use io_thread lock */
#  define Z_GNI_API_LOCK(thr)   pthread_mutex_lock(&(thr)->mutex)
#  define Z_GNI_API_UNLOCK(thr) pthread_mutex_unlock(&(thr)->mutex)
#endif

#define EP_THR_LOCK(ep) pthread_mutex_lock(&((zap_ep_t)(ep))->thread->mutex)
#define EP_THR_UNLOCK(ep) pthread_mutex_unlock(&((zap_ep_t)(ep))->thread->mutex)

#define EP_LOCK(ep) pthread_mutex_lock(&((zap_ep_t)(ep))->lock)
#define EP_UNLOCK(ep) pthread_mutex_unlock(&((zap_ep_t)(ep))->lock)

#define THR_LOCK(thr) pthread_mutex_lock(&((zap_io_thread_t)(thr))->mutex)
#define THR_UNLOCK(thr) pthread_mutex_unlock(&((zap_io_thread_t)(thr))->mutex)

/* Round up V to be multiple of R. R must be 2^K. */
#define ROUNDUP(V, R) ( (((V)-1)|((R)-1)) + 1 )

int init_complete = 0;

static void zap_ugni_default_log(const char *fmt, ...);
static zap_log_fn_t zap_ugni_log = zap_ugni_default_log;
zap_mem_info_fn_t __mem_info_fn = NULL;


/* some debugging statistics */
#define ATOMIC_INC(PTR, V) __atomic_add_fetch(PTR, V, __ATOMIC_SEQ_CST)

static struct z_ugni_debug_stat {
	int send_submitted;
	int send_completed;
	int rdma_submitted;
	int rdma_completed;
	int ack_submitted;
	int ack_completed;
	int recv_success;
	int rcq_success;
	int rcq_rc_not_done;
	int active_send;
	int active_rdma;
	int active_ack;
	int pending_ack;
} z_ugni_stat = {0};
static int z_ugni_stat_log_enabled = 0;

int zap_ugni_cq_depth = ZAP_UGNI_CQ_DEPTH;


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

static pthread_mutex_t ugni_mh_lock;
gni_mem_handle_t global_mh;
int global_mh_initialized = 0;
static gni_mem_handle_t * ugni_get_mh();

static pthread_t error_thread;

static void *error_thread_proc(void *arg);

static zap_err_t z_ugni_msg_send(struct z_ugni_ep *uep, zap_ugni_msg_t msg,
			     const char *data, size_t data_len, void *ctxt);

static zap_err_t z_ugni_close(zap_ep_t ep);
static int z_ugni_enable_sock(struct z_ugni_ep *uep);
static int z_ugni_disable_sock(struct z_ugni_ep *uep);
static int __enable_epoll_out(struct z_ugni_ep *uep);

static int __on_wr_send_comp(struct z_ugni_wr *wr);

static int __get_nodeid(struct sockaddr *sa, socklen_t sa_len);
static int __check_node_state(int node_id);

static void z_ugni_destroy(zap_ep_t ep);
static int z_ugni_setup_conn(struct z_ugni_ep *uep, struct z_ugni_ep_desc *ep_desc);
static int z_ugni_sock_send_conn_accept(struct z_ugni_ep *uep, const char *data, size_t data_len);
static void z_ugni_sock_send(z_ugni_ep_t uep);
static void z_ugni_process_sock_send_comp(z_ugni_ep_t uep);

static LIST_HEAD(, z_ugni_ep) z_ugni_list = LIST_HEAD_INITIALIZER(0);
static pthread_mutex_t z_ugni_list_mutex = PTHREAD_MUTEX_INITIALIZER;

#define ZAP_UGNI_STALLED_TIMEOUT	60 /* 1 minute */
static int zap_ugni_stalled_timeout;

#ifdef DEBUG
static uint32_t ugni_io_count = 0;
#endif /* DEBUG */
static uint64_t ugni_post_id  = 1;

static pthread_mutex_t ugni_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t inst_id_cond = PTHREAD_COND_INITIALIZER;

static int zap_ugni_dom_initialized = 0;
static struct zap_ugni_dom {
	zap_ugni_type_t type;
	uid_t euid;
	uint8_t ptag;
	uint32_t cookie;
	uint32_t pe_addr;
	union z_ugni_inst_id_u inst_id; /* id for RDMA endpoint */
	gni_job_limits_t limits;
	gni_cdm_handle_t cdm;
	gni_nic_handle_t nic;
} _dom = {0};

static void z_ugni_flush(struct z_ugni_ep *uep, struct z_ugni_io_thread *thr);
static void z_ugni_zq_post(struct z_ugni_io_thread *thr, struct z_ugni_ev *uev);
static void z_ugni_zq_rm(struct z_ugni_io_thread *thr, struct z_ugni_ev *uev);
static void z_ugni_ep_release(struct z_ugni_ep *uep);
static void z_ugni_zq_try_post(struct z_ugni_ep *uep, uint64_t ts_msec, int type, int status);

/* Move wr's from uep that are in thr->pending_wrq into uep->flushed_wrq */
static void z_ugni_thr_ep_flush(struct z_ugni_io_thread *thr, struct z_ugni_ep *uep);

static void z_ugni_io_thread_wakeup(struct z_ugni_io_thread *thr);

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

const char *gni_ev_str(int ev_type)
{
	switch (ev_type) {
	case GNI_CQ_EVENT_TYPE_POST:
		return "GNI_CQ_EVENT_TYPE_POST";
	case GNI_CQ_EVENT_TYPE_SMSG:
		return "GNI_CQ_EVENT_TYPE_SMSG";
	case GNI_CQ_EVENT_TYPE_MSGQ:
		return "GNI_CQ_EVENT_TYPE_MSGQ";
	case GNI_CQ_EVENT_TYPE_DMAPP:
		return "GNI_CQ_EVENT_TYPE_DMAPP";
	default:
		return "UNKNOWN";
	}
}

/* ugni_lock is held */
static union z_ugni_inst_id_u __get_inst_id()
{
	int rc;
	rs_node_t node;
	union z_ugni_inst_id_u id;
	rc = rca_get_nodeid(&node);
	if (rc) {
		id.u32 = -1;
	} else {
		pid_t pid = getpid();
		id.pid = pid;
		id.node_id = node.rs_node_s._node_id;
	}
	return id;
}

/* THR_LOCK must be held */
static inline int z_ugni_get_post_credit(struct z_ugni_io_thread *thr, struct z_ugni_wr *wr)
{
	int *credit;
	switch (wr->type) {
	case Z_UGNI_WR_RDMA:
		credit = &thr->rdma_post_credit;
		break;
	default:
		assert(0 == "BAD TYPE");
	}
	assert((*credit) >= 0);
	if (*credit) {
		(*credit)--;
		return 1;
	}
	return 0;
}

/* THR_LOCK must be held */
static inline void z_ugni_put_post_credit(struct z_ugni_io_thread *thr, struct z_ugni_wr *wr)
{
	int *credit;
	switch (wr->type) {
	case Z_UGNI_WR_RDMA:
		credit = &thr->rdma_post_credit;
		break;
	default:
		assert(0 == "BAD TYPE");
	}
	(*credit)++;
}

static zap_err_t __node_state_check(struct z_ugni_ep *uep)
{
	/* node state validation */
	if (!_node_state.check_state)
		return ZAP_ERR_OK;
	if (uep->node_id == -1) {
		struct sockaddr_storage lsa, sa;
		socklen_t sa_len;
		zap_err_t zerr;

		sa_len = sizeof(lsa);
		zerr = zap_get_name(&uep->ep, (struct sockaddr *)&lsa,
					(struct sockaddr *)&sa, &sa_len);
		if (zerr) {
			DLOG("zap_get_name() error: %d\n", zerr);
			return ZAP_ERR_ENDPOINT;
		}
		uep->node_id = __get_nodeid((struct sockaddr* )&sa, sa_len);
	}
	if (uep->node_id != -1) {
		if (__check_node_state(uep->node_id)) {
			DLOG("Node %d is in a bad state\n", uep->node_id);
			z_ugni_close(&uep->ep);
			return ZAP_ERR_ENDPOINT;
		}
	}
	return ZAP_ERR_OK;
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

static void __cache_sockaddr(struct z_ugni_ep *uep)
{
	uep->addr_len = sizeof(uep->local_addr);
	getsockname(uep->sock, (void*)&uep->local_addr, &uep->addr_len);
	getpeername(uep->sock, (void*)&uep->remote_addr, &uep->addr_len);
}

static struct z_ugni_wr *z_ugni_alloc_send_wr(struct z_ugni_ep *uep,
					 zap_ugni_msg_t msg,
					 const char *data, size_t data_len,
					 void *ctxt)
{
	int hdr_sz;
	int alloc_data = 1;
	int type = Z_UGNI_WR_SEND;

	switch (ntohs(msg->hdr.msg_type)) {
	case ZAP_UGNI_MSG_CONNECT:
		hdr_sz = sizeof(msg->connect);
		msg->connect.data_len = htonl(data_len);
		break;
	case ZAP_UGNI_MSG_RENDEZVOUS:
		hdr_sz = sizeof(msg->rendezvous);
		break;
	case ZAP_UGNI_MSG_ACCEPTED:
		hdr_sz = sizeof(msg->accept);
		msg->accept.data_len = htonl(data_len);
		break;
	case ZAP_UGNI_MSG_SEND_MAPPED: /* use `regular` format */
		type = Z_UGNI_WR_SEND_MAPPED;
		alloc_data = 0;
		/* let-through */
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

	struct z_ugni_wr *wr;
	wr = calloc(1, sizeof(*wr) + sizeof(*wr->send_wr) + hdr_sz + alloc_data*data_len);
	if (!wr)
		return NULL;
	wr->uep = uep;
	wr->type = type;
	wr->state = Z_UGNI_WR_INIT;
	wr->send_wr->msg_len = hdr_sz + data_len;
	wr->send_wr->hdr_len = hdr_sz;
	wr->send_wr->data_len = data_len;
	wr->send_wr->ctxt = ctxt;
	memcpy(wr->send_wr->msg, msg, hdr_sz);
	if (data && alloc_data && data_len) {
		wr->send_wr->data = ((void*)wr->send_wr->msg) + hdr_sz;
		memcpy(wr->send_wr->data, data, data_len);
	} else {
		wr->send_wr->data = (void*)data; /* this is SEND_MAPPED */
	}
	return wr;
}

static struct z_ugni_wr *z_ugni_alloc_post_desc(struct z_ugni_ep *uep)
{
	struct zap_ugni_rdma_wr *d;
	struct z_ugni_wr *wr = calloc(1, sizeof(*wr) + sizeof(*wr->rdma_wr));
	if (!wr)
		return NULL;
	wr->uep = uep;
	wr->type = Z_UGNI_WR_RDMA;
	wr->state = Z_UGNI_WR_INIT;
	d = wr->rdma_wr;
	format_4tuple(&uep->ep, d->ep_name, ZAP_UGNI_EP_NAME_SZ);
	return wr;
}

static void z_ugni_free_wr(struct z_ugni_wr *wr)
{
	free(wr);
}

static gni_mem_handle_t * ugni_get_mh()
{
	gni_return_t grc = GNI_RC_SUCCESS;
	zap_mem_info_t mmi;
	gni_mem_handle_t *__mh = NULL;

	if (__builtin_expect(global_mh_initialized, 1)) {
		return &global_mh;
	}

	pthread_mutex_lock(&ugni_mh_lock);
	if (global_mh_initialized) {
		/* the other thread won the race */
		goto out;
	}
	mmi = __mem_info_fn();
	grc = GNI_MemRegister(_dom.nic, (uint64_t)mmi->start, mmi->len, NULL,
			      GNI_MEM_READWRITE | GNI_MEM_RELAXED_PI_ORDERING,
			      -1, &global_mh);
	if (grc != GNI_RC_SUCCESS)
		goto err;
	global_mh_initialized = 1;
 out:
	__mh = &global_mh;
 err:
	pthread_mutex_unlock(&ugni_mh_lock);
	return __mh;
}

gni_mem_handle_t *map_mh(zap_map_t map)
{
	if (map->mr[ZAP_UGNI])
		return map->mr[ZAP_UGNI];
	if (map->type == ZAP_MAP_LOCAL)
		return map->mr[ZAP_UGNI] = ugni_get_mh();
	return NULL;
}

/* The caller must hold the endpoint lock */
static void z_ugni_ep_error(struct z_ugni_ep *uep)
{
	z_ugni_disable_sock(uep);
	z_ugni_ep_release(uep);
	switch (uep->ep.state) {
	case ZAP_EP_CONNECTED:
		z_ugni_zq_try_post(uep, 0, ZAP_EVENT_DISCONNECTED, ZAP_ERR_ENDPOINT);
		break;
	case ZAP_EP_CLOSE:
	case ZAP_EP_PEER_CLOSE:
		z_ugni_zq_try_post(uep, 0, uep->zap_connected?ZAP_EVENT_DISCONNECTED:ZAP_EVENT_CONNECT_ERROR, ZAP_ERR_ENDPOINT);
		break;
	case ZAP_EP_CONNECTING:
		z_ugni_zq_try_post(uep, 0, ZAP_EVENT_CONNECT_ERROR, ZAP_ERR_ENDPOINT);
		break;
	case ZAP_EP_ACCEPTING:
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
}

static void z_ugni_ep_release(struct z_ugni_ep *uep)
{
	gni_return_t grc;
	if (uep->gni_ep) {
		Z_GNI_API_LOCK(thr);
		grc = GNI_EpDestroy(uep->gni_ep);
		Z_GNI_API_UNLOCK(thr);
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

static zap_err_t z_ugni_close(zap_ep_t ep)
{
	pthread_t self = pthread_self();
	struct z_ugni_ep *uep = (struct z_ugni_ep *)ep;

	DLOG_(uep, "Closing xprt: %p, state: %s\n", uep,
			__zap_ep_state_str(uep->ep.state));
	EP_LOCK(uep);
	if (ep->thread && self != ep->thread->thread) {
		while (!TAILQ_EMPTY(&uep->send_wrq) && uep->active_wr) {
			pthread_cond_wait(&uep->sq_cond, &uep->ep.lock);
		}
	}
	switch (uep->ep.state) {
	case ZAP_EP_LISTENING:
		z_ugni_disable_sock(uep);
		uep->ep.state = ZAP_EP_CLOSE;
		break;
	case ZAP_EP_CONNECTED:
	case ZAP_EP_PEER_CLOSE:
		shutdown(uep->sock, SHUT_RDWR);
		uep->ep.state = ZAP_EP_CLOSE;
		break;
	case ZAP_EP_CONNECTING:
	case ZAP_EP_ACCEPTING:
	case ZAP_EP_ERROR:
		shutdown(uep->sock, SHUT_RDWR);
		/* don't change state */
		break;
	case ZAP_EP_CLOSE:
		/* don't change state */
		break;
	default:
		ZAP_ASSERT(0, ep, "%s: Unexpected state '%s'\n",
				__func__, __zap_ep_state_str(ep->state));
	}
	EP_UNLOCK(uep);
	return ZAP_ERR_OK;
}

static zap_err_t z_get_name(zap_ep_t ep, struct sockaddr *local_sa,
			    struct sockaddr *remote_sa, socklen_t *sa_len)
{
	struct z_ugni_ep *uep = (void*)ep;
	if (*sa_len < uep->addr_len) {
		/* not enough space. Set the required length and return */
		*sa_len = uep->addr_len;
		return ZAP_ERR_RESOURCE;
	}
	*sa_len = uep->addr_len;
	memcpy(local_sa, &uep->local_addr, uep->addr_len);
	memcpy(remote_sa, &uep->remote_addr, uep->addr_len);
	return ZAP_ERR_OK;
}

static zap_err_t z_ugni_connect(zap_ep_t ep,
				struct sockaddr *sa, socklen_t sa_len,
				char *data, size_t data_len)
{
	int rc;
	zap_err_t zerr;
	struct z_ugni_ep *uep = (void*)ep;

	if (data_len > ZAP_UGNI_MAX_MSG)
		return ZAP_ERR_PARAMETER;

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
	ref_get(&uep->ep.ref, "accept/connect");
	rc = connect(uep->sock, sa, sa_len);
	if (rc && errno != EINPROGRESS) {
		zerr = ZAP_ERR_CONNECT;
		goto err_1;
	}

	uep->sock_epoll_ctxt.type = Z_UGNI_SOCK_EVENT;
	uep->ev.events = EPOLLIN|EPOLLOUT;
	uep->ev.data.ptr = &uep->sock_epoll_ctxt;

	zerr = zap_io_thread_ep_assign(&uep->ep);
	if (zerr)
		goto err_1;

	return 0;
 err_1:
	free(uep->conn_data);
	uep->conn_data = NULL;
	uep->conn_data_len = 0;
	ref_put(&uep->ep.ref, "accept/connect");
 err_0:
	if (uep->sock >= 0) {
		close(uep->sock);
		uep->sock = -1;
	}
	return zerr;
}

/* EP_LOCK is NOT held */
static void process_uep_msg_unknown(struct z_ugni_ep *uep)
{
	LOG_(uep, "zap_ugni: Unknown zap message.\n");
	EP_LOCK(uep);
	z_ugni_ep_error(uep);
	EP_UNLOCK(uep);
}

/* EP_LOCK is NOT held */
static void process_uep_msg_regular(struct z_ugni_ep *uep)
{
	struct zap_ugni_msg_regular *msg = &uep->mbuf.msg.regular;
	struct zap_event ev = {
			.ep = &uep->ep,
			.type = ZAP_EVENT_RECV_COMPLETE,
	};

	ev.data = (void*)msg->data;
	ev.data_len = ntohl(msg->data_len);
	uep->ep.cb(&uep->ep, &ev);
	return;
}

/* EP_LOCK is NOT held */
static void process_uep_msg_rendezvous(struct z_ugni_ep *uep)
{
	struct zap_ugni_msg_rendezvous *msg = &uep->mbuf.msg.rendezvous;
	struct zap_map *map;
	zap_err_t zerr;

	msg->map_addr = be64toh(msg->map_addr);
	msg->acc = ntohl(msg->acc);
	msg->map_len = ntohl(msg->map_len);
	msg->gni_mh.qword1 = be64toh(msg->gni_mh.qword1);
	msg->gni_mh.qword2 = be64toh(msg->gni_mh.qword2);

	zerr = zap_map(&map, (void*)msg->map_addr, msg->map_len, msg->acc);
	if (zerr) {
		LOG_(uep, "%s:%d: Failed to create a map in %s (%s)\n",
			__FILE__, __LINE__, __func__, __zap_err_str[zerr]);
		goto err0;
	}
	map->type = ZAP_MAP_REMOTE;
	ref_get(&uep->ep.ref, "zap_map/rendezvous");
	map->ep = (void*)uep;
	map->mr[ZAP_UGNI] = &msg->gni_mh;

	char *amsg = NULL;
	size_t amsg_len = msg->hdr.msg_len - sizeof(*msg);
	if (amsg_len) {
		amsg = msg->msg; /* attached message from rendezvous */
	}
	struct zap_event ev = {
		.type = ZAP_EVENT_RENDEZVOUS,
		.map = map,
		.data_len = amsg_len,
		.data = (void*)amsg
	};

	uep->ep.cb((void*)uep, &ev);
	return;

err0:
	return;
}

/*
 * msg is in NETWORK byte order.
 * The data length and the message lenght are conveniently set by this function.
 */
static zap_err_t z_ugni_msg_send(struct z_ugni_ep *uep, zap_ugni_msg_t msg,
			     const char *data, size_t data_len, void *ctxt)
{
	struct z_ugni_wr *wr;

	/* need to keep send buf until send completion */
	wr = z_ugni_alloc_send_wr(uep, msg, data, data_len, ctxt);
	if (!wr)
		goto err;

	EP_THR_LOCK(uep);
	wr->state = Z_UGNI_WR_PENDING;
	TAILQ_INSERT_TAIL(&uep->send_wrq, wr, entry);
	__atomic_fetch_add(&uep->ep.sq_sz, 1, __ATOMIC_SEQ_CST);
	if (uep->ep.thread)
		__atomic_fetch_add(&uep->ep.thread->stat->sq_sz, 1, __ATOMIC_SEQ_CST);
	ATOMIC_INC(&z_ugni_stat.active_send, 1);
	z_ugni_sock_send(uep);
	EP_THR_UNLOCK(uep);
	return ZAP_ERR_OK;
 err:
	return ZAP_ERR_ENDPOINT;
}

/* EP_LOCK is NOT held */
static void process_uep_msg_accepted(struct z_ugni_ep *uep)
{
	struct zap_ugni_msg_accept *msg = &uep->mbuf.msg.accept;
	struct zap_ugni_msg_regular ack;
	int rc, len;

	if (uep->ep.state != ZAP_EP_CONNECTING) {
		LOG_(uep, "Get z_ugni_sock_msg_conn_accept message "
			  "while endpoint in %s state\n",
			  __zap_ep_state_str(uep->ep.state));
		goto err;
	}

	msg->ep_desc.inst_id = ntohl(msg->ep_desc.inst_id);
	msg->ep_desc.pe_addr = ntohl(msg->ep_desc.pe_addr);
	msg->data_len = ntohl(msg->data_len);

	CONN_LOG("%p sock-recv conn_accept\n", uep);
	rc = z_ugni_setup_conn(uep, &msg->ep_desc);
	if (rc)
		goto err;

	DLOG_(uep, "ACCEPTED received: pe_addr: %#x, inst_id: %#x\n",
			msg->ep_desc.pe_addr, msg->ep_desc.inst_id);

	/* ack the accept message */
	memset(&ack, 0, sizeof(ack));
	ack.data_len = 0;
	ack.hdr.msg_len = htonl(sizeof(ack));
	ack.hdr.msg_type = htons(ZAP_UGNI_MSG_ACK_ACCEPTED);
	len = write(uep->sock, &ack, sizeof(ack));
	if (len != sizeof(ack)) {
		/* REASON: msg is super small and it is the only message to be
		 *         sent on the socket. So, we expect it to successfully
		 *         copied over to the kernel buffer in one go. */
		goto err;
	}
	rc = zap_ep_change_state(&uep->ep, ZAP_EP_CONNECTING, ZAP_EP_CONNECTED);
	assert( rc == 0 );
	if (rc)
		goto err;
	uep->zap_connected = 1;
	struct zap_event ev = {
		.ep = &uep->ep,
		.type = ZAP_EVENT_CONNECTED,
		.data_len = msg->data_len,
		.data = (msg->data_len ? (void*)msg->data : NULL)
	};
	uep->ep.cb((void*)uep, &ev);
	return;
err:
	EP_LOCK(uep);
	uep->ep.state = ZAP_EP_ERROR;
	z_ugni_disable_sock(uep);
	EP_UNLOCK(uep);
	/* post CONN_ERR event to zq */
	z_ugni_zq_try_post(uep, 0, ZAP_EVENT_CONNECT_ERROR, ZAP_ERR_ENDPOINT);
	return;
}

/* EP_LOCK is NOT held */
static void process_uep_msg_connect(struct z_ugni_ep *uep)
{
	struct zap_ugni_msg_connect *msg = &uep->mbuf.msg.connect;

	/* validate version and signature */
	if (!ZAP_VERSION_EQUAL(msg->ver)) {
		LOG_(uep, "zap_ugni: Receive conn request "
			  "from an unsupported version "
			  "%hhu.%hhu.%hhu.%hhu\n",
			  msg->ver.major,
			  msg->ver.minor,
			  msg->ver.patch,
			  msg->ver.flags);
		goto err;
	}
	if (memcmp(msg->sig, ZAP_UGNI_SIG, sizeof(ZAP_UGNI_SIG))) {
		LOG_(uep, "Expecting sig '%s', but got '%.*s'.\n",
			  ZAP_UGNI_SIG, sizeof(msg->sig),
			  msg->sig);
		goto err;
	}
	/* ntoh, the hdr part has already been converted */
	msg->data_len = ntohl(msg->data_len);
	msg->ep_desc.inst_id = ntohl(msg->ep_desc.inst_id);
	msg->ep_desc.pe_addr = ntohl(msg->ep_desc.pe_addr);
	if (uep->ep.state != ZAP_EP_ACCEPTING) {
		LOG_(uep, "Get z_ugni_sock_msg_conn_req message while "
			  "endpoint in %s state\n",
			  __zap_ep_state_str(uep->ep.state));
		goto err;
	}
	CONN_LOG("%p sock-recv conn_msg: pe_addr: %#x, inst_id: %#x\n",
			uep, msg->ep_desc.pe_addr, msg->ep_desc.inst_id);

	struct zap_event ev = {
		.ep = &uep->ep,
		.type = ZAP_EVENT_CONNECT_REQUEST,
		.data_len = msg->data_len,
		.data = (msg->data_len)?((void*)msg->data):(NULL)
	};
	uep->ep.cb(&uep->ep, &ev);

	return;

 err:
	/* no need for EP_LOCK here as we're the only thread accessing it */
	z_ugni_disable_sock(uep);
	switch (uep->ep.state) {
	case ZAP_EP_ACCEPTING:
		/* application does not know about this endpoint yet */
		uep->ep.state = ZAP_EP_ERROR;
		zap_free(&uep->ep); /* b/c zap_new() in conn_req */
		break;
	default:
		assert(0 == "Unexpected endpoint state");
	}
}

/* EP_LOCK is NOT held */
static void process_uep_msg_rejected(struct z_ugni_ep *uep)
{
	struct zap_ugni_msg_regular *msg = &uep->mbuf.msg.regular;
	int rc;
	size_t data_len;

	data_len = ntohl(msg->data_len);
	struct zap_event ev = {
		.ep = &uep->ep,
		.type = ZAP_EVENT_REJECTED,
		.data_len = data_len,
		.data = (data_len ? (void *)msg->data : NULL)
	};
	rc = zap_ep_change_state(&uep->ep, ZAP_EP_CONNECTING, ZAP_EP_ERROR);
	if (rc != ZAP_ERR_OK) {
		return;
	}
	zap_event_deliver(&ev);
	return;
}

/* EP_LOCK is NOT held */
static void process_uep_msg_ack_accepted(struct z_ugni_ep *uep)
{
	int rc;

	rc = zap_ep_change_state(&uep->ep, ZAP_EP_ACCEPTING, ZAP_EP_CONNECTED);
	if (rc != ZAP_ERR_OK) {
		return;
	}
	uep->zap_connected = 1;
	struct zap_event ev = {
		.type = ZAP_EVENT_CONNECTED
	};
	uep->ep.cb(&uep->ep, &ev);
	return;
}

typedef void(*process_uep_msg_fn_t)(struct z_ugni_ep*);
process_uep_msg_fn_t process_uep_msg_fns[] = {
	[ZAP_UGNI_MSG_REGULAR]      =  process_uep_msg_regular,
	[ZAP_UGNI_MSG_SEND_MAPPED]  =  process_uep_msg_regular, /* SEND_MAPPED recv process the same way as REGULAR */
	[ZAP_UGNI_MSG_RENDEZVOUS]   =  process_uep_msg_rendezvous,
	[ZAP_UGNI_MSG_ACCEPTED]     =  process_uep_msg_accepted,
	[ZAP_UGNI_MSG_CONNECT]      =  process_uep_msg_connect,
	[ZAP_UGNI_MSG_REJECTED]     =  process_uep_msg_rejected,
	[ZAP_UGNI_MSG_ACK_ACCEPTED] =  process_uep_msg_ack_accepted,
};

#define min_t(t, x, y) (t)((t)x < (t)y?(t)x:(t)y)

int zap_ugni_err_handler(gni_cq_handle_t cq, gni_cq_entry_t cqe,
			 struct zap_ugni_rdma_wr *desc)
{
	uint32_t recoverable = 0;
	char errbuf[512];
	gni_return_t grc = GNI_CqErrorRecoverable(cqe, &recoverable);
	if (grc) {
		LOG("GNI_CqErrorRecoverable returned %d\n", grc);
		recoverable = 0;
	}
	grc = GNI_CqErrorStr(cqe, errbuf, sizeof(errbuf));
	if (grc) {
		LOG("GNI_CqErrorStr returned %d\n", grc);
	} else {
		LOG("GNI cqe Error : %s\n", errbuf);
	}
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
		LLOG("FAIL:GNI_SubscribeErrors returned error %s\n", gni_err_str[status]);
	}
	while (1) {
		memset(&ev, 0, sizeof(ev));
		status = GNI_WaitErrorEvents(err_hndl,&ev,1,0,&num);
		if (status != GNI_RC_SUCCESS || (num != 1)) {
			LLOG("FAIL:GNI_WaitErrorEvents returned error %d %s\n",
				num, gni_err_str[status]);
			continue;
		}
		LLOG("gni error event: %u %u %u %u %lu %lu %lu %lu %lu\n",
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

	uep->sock_epoll_ctxt.type = Z_UGNI_SOCK_EVENT;
	uep->ev.events = EPOLLIN;
	uep->ev.data.ptr = &uep->sock_epoll_ctxt;

	zerr = zap_io_thread_ep_assign(&uep->ep);
	if (zerr)
		goto err_1;

	return ZAP_ERR_OK;

 err_1:
	close(uep->sock);
 err_0:
	return zerr;
}

static zap_err_t
z_ugni_send_mapped(zap_ep_t ep, zap_map_t map, void *buf, size_t len,
		   void *context)
{
	struct z_ugni_ep *uep = (void*)ep;
	zap_err_t zerr;
	struct zap_ugni_msg msg;

	if (len > ZAP_UGNI_MAX_MSG)
		return ZAP_ERR_PARAMETER;

	/* map validation */
	if (map->type != ZAP_MAP_LOCAL)
		return ZAP_ERR_INVALID_MAP_TYPE;
	if (z_map_access_validate(map, buf, len, 0))
		return ZAP_ERR_LOCAL_LEN;

	/* node state validation */
	zerr = __node_state_check(uep);
	if (zerr)
		return zerr;

	EP_LOCK(uep);
	if (ep->state != ZAP_EP_CONNECTED) {
		EP_UNLOCK(uep);
		return ZAP_ERR_NOT_CONNECTED;
	}
	EP_UNLOCK(uep);
	memset(&msg, 0, sizeof(msg));
	msg.hdr.msg_type = htons(ZAP_UGNI_MSG_SEND_MAPPED);
	zerr = z_ugni_msg_send(uep, &msg, buf, len, context);
	return zerr;
}

static zap_err_t z_ugni_send(zap_ep_t ep, char *buf, size_t len)
{
	struct z_ugni_ep *uep = (void*)ep;
	zap_err_t zerr;
	struct zap_ugni_msg msg;

	if (len > ZAP_UGNI_MAX_MSG)
		return ZAP_ERR_PARAMETER;

	/* node state validation */
	zerr = __node_state_check(uep);
	if (zerr)
		return zerr;

	memset(&msg, 0, sizeof(msg));
	EP_LOCK(uep);
	if (ep->state != ZAP_EP_CONNECTED) {
		EP_UNLOCK(uep);
		return ZAP_ERR_NOT_CONNECTED;
	}
	memset(&msg, 0, sizeof(msg));
	msg.hdr.msg_type = htons(ZAP_UGNI_MSG_REGULAR);
	zerr = z_ugni_msg_send(uep, &msg, buf, len, NULL);
	EP_UNLOCK(uep);
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
		LOG("Number of nodes %d exceeds ZAP_UGNI_MAX_NUM_NODE "
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
			LOG("node_id %d exceeds ZAP_UGNI_MAX_NUM_NODE "
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
	int rc;
	struct ovis_event_s ns_ev;

	/* Initialize the inst_id here. */
	pthread_mutex_lock(&ugni_lock);
	_dom.inst_id = __get_inst_id();
	pthread_cond_signal(&inst_id_cond);
	pthread_mutex_unlock(&ugni_lock);
	if (_dom.inst_id.u32 == -1)
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
		grc = GNI_CdmCreate(_dom.inst_id.u32, _dom.ptag, _dom.cookie,
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

	bzero(&z_ugni_stat, sizeof(z_ugni_stat));

	zap_ugni_cq_depth = ZAP_ENV_INT(ZAP_UGNI_CQ_DEPTH);

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
		/*
		 * The node_state_thread isn't created, so the nodeid isn't
		 * initilized. Do it here.
		 */
		_dom.inst_id = __get_inst_id();
		if (_dom.inst_id.u32 == -1) {
			pthread_mutex_unlock(&ugni_lock);
			goto err;
		}
	} else {
		/*
		 * The node_state_thread is created and the node id will be
		 * initialized in there. Wait until it is done.
		 */
		while (_dom.inst_id.u32 == 0) {
			pthread_cond_wait(&inst_id_cond, &ugni_lock);
			if (_dom.inst_id.u32 == 0)
				continue;

			if (_dom.inst_id.u32 == -1) {
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
	uep->node_id = -1;
	uep->sock_enabled = 0;

	TAILQ_INIT(&uep->send_wrq);
	TAILQ_INIT(&uep->send_comp_wrq);
	TAILQ_INIT(&uep->flushed_wrq);

#ifdef EP_LOG_ENABLED
	char hostname[256];
	char path[4096];
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	gethostname(hostname, sizeof(hostname));
	snprintf(path, sizeof(path), "log/%s.%d-%p.%ld.%ld.log", hostname,
			getpid(), uep, ts.tv_sec, ts.tv_nsec);
	uep->log = fopen(path, "w");
	if (!uep->log) {
		free(uep);
		goto err_0;
	}
	setbuf(uep->log, NULL); /* no buffer */
#endif

	pthread_mutex_lock(&z_ugni_list_mutex);
	LIST_INSERT_HEAD(&z_ugni_list, uep, link);
	pthread_mutex_unlock(&z_ugni_list_mutex);
	CONN_LOG("new endpoint: %p\n", uep);
	return (zap_ep_t)uep;

 err_0:
	return NULL;
}

static void z_ugni_destroy(zap_ep_t ep)
{
	struct z_ugni_ep *uep = (void*)ep;
	CONN_LOG("destroying endpoint %p\n", uep);
	pthread_mutex_lock(&z_ugni_list_mutex);
	ZUGNI_LIST_REMOVE(uep, link);
	pthread_mutex_unlock(&z_ugni_list_mutex);

	if (uep->conn_data) {
		free(uep->conn_data);
		uep->conn_data = 0;
	}
	if (uep->sock > -1) {
		close(uep->sock);
		uep->sock = -1;
	}

#ifdef EP_LOG_ENABLED
	if (uep->log) {
		fclose(uep->log);
	}
#endif

	z_ugni_ep_release(uep);
	free(ep);
}

zap_err_t z_ugni_accept(zap_ep_t ep, zap_cb_fn_t cb, char *data, size_t data_len)
{
	/* ep is the newly created ep from __z_ugni_conn_request */
	struct z_ugni_ep *uep = (struct z_ugni_ep *)ep;
	zap_err_t zerr = ZAP_ERR_RESOURCE;
	int rc;

	if (data_len > ZAP_UGNI_MAX_MSG)
		return ZAP_ERR_PARAMETER;

	EP_LOCK(uep);

	if (uep->ep.state != ZAP_EP_ACCEPTING) {
		zerr = ZAP_ERR_ENDPOINT;
		EP_UNLOCK(uep);
		goto out;
	}

	uep->ep.cb = cb;
	uep->app_accepted = 1;
	rc = z_ugni_setup_conn(uep, &uep->mbuf.msg.connect.ep_desc);
	if (rc)
		goto out;
	rc = z_ugni_sock_send_conn_accept(uep, data, data_len);
	if (rc)
		goto out;
	__get_ep(&uep->ep, "accept/connect"); /* will be put down when disconnected/conn_error */
	EP_UNLOCK(uep);
	zerr = ZAP_ERR_OK;
	CONN_LOG("%p zap-accepted\n", uep);
	/* let through */
out:
	return zerr;
}

static zap_err_t z_ugni_reject(zap_ep_t ep, char *data, size_t data_len)
{
	struct z_ugni_ep *uep = (struct z_ugni_ep *)ep;
	struct zap_ugni_msg msg;
	zap_err_t zerr;

	if (data_len > ZAP_UGNI_MAX_MSG)
		return ZAP_ERR_PARAMETER;

	memset(&msg, 0, sizeof(msg));
	msg.hdr.msg_type = htons(ZAP_UGNI_MSG_REJECTED);
	EP_LOCK(uep);
	uep->ep.state = ZAP_EP_ERROR;
	zerr = z_ugni_msg_send(uep, &msg, data, data_len, NULL);
	EP_UNLOCK(uep);
	if (zerr)
		goto err;
	return ZAP_ERR_OK;
err:
	return zerr;
}

static zap_err_t z_ugni_unmap(zap_map_t map)
{
	if ((map->type == ZAP_MAP_REMOTE) && map->ep)
		__put_ep(map->ep, "zap_map/rendezvous");
	return ZAP_ERR_OK;
}

static zap_err_t z_ugni_share(zap_ep_t ep, zap_map_t map,
				const char *msg, size_t msg_len)
{
	zap_err_t rc;
	struct z_ugni_ep *uep = (void*) ep;
	gni_mem_handle_t *mh;

	if (msg_len > ZAP_UGNI_MAX_MSG)
		return ZAP_ERR_PARAMETER;

	/* validate */
	if (map->type != ZAP_MAP_LOCAL)
		return ZAP_ERR_INVALID_MAP_TYPE;

	mh = map_mh(map);
	if (!mh)
		return ZAP_ERR_RESOURCE;

	/* node state validation */
	rc = __node_state_check(uep);
	if (rc)
		return rc;

	EP_LOCK(uep);
	if (ep->state != ZAP_EP_CONNECTED) {
		EP_UNLOCK(uep);
		return ZAP_ERR_NOT_CONNECTED;
	}

	/* prepare message */
	struct zap_ugni_msg _msg = {
		.rendezvous = {
			.hdr = {
				.msg_type = htons(ZAP_UGNI_MSG_RENDEZVOUS),
			},
			.gni_mh = {
				.qword1 = htobe64(mh->qword1),
				.qword2 = htobe64(mh->qword2),
			},
			.map_addr = htobe64((uint64_t)map->addr),
			.map_len = htonl(map->len),
			.acc = htonl(map->acc),
		}
	};

	rc = z_ugni_msg_send(uep, &_msg, msg, msg_len, NULL);
	EP_UNLOCK(uep);
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

	gni_mem_handle_t *src_mh = map_mh(src_map);
	gni_mem_handle_t *dst_mh = map_mh(dst_map);
	if (!src_mh || !dst_mh)
		return ZAP_ERR_RESOURCE;

	struct z_ugni_ep *uep = (struct z_ugni_ep *)ep;
	struct z_ugni_io_thread *thr = (void*)ep->thread;

	/* node state validation */
	zerr = __node_state_check(uep);
	if (zerr)
		return zerr;

	EP_LOCK(ep);
	if (!uep->gni_ep || ep->state != ZAP_EP_CONNECTED) {
		zerr = ZAP_ERR_NOT_CONNECTED;
		EP_UNLOCK(ep);
		goto out;
	}
	EP_UNLOCK(ep);

	struct z_ugni_wr *wr = z_ugni_alloc_post_desc(uep);
	if (!wr) {
		zerr = ZAP_ERR_RESOURCE;
		goto out;
	}
	struct zap_ugni_rdma_wr *desc = wr->rdma_wr;

	desc->post.type = GNI_POST_RDMA_GET;
	desc->post.cq_mode = GNI_CQMODE_GLOBAL_EVENT;
	desc->post.dlvr_mode = GNI_DLVMODE_IN_ORDER;
	desc->post.local_addr = (uint64_t)dst;
	desc->post.local_mem_hndl = *dst_mh;
	desc->post.remote_addr = (uint64_t)src;
	desc->post.remote_mem_hndl = *src_mh;
	desc->post.length = sz;
	desc->post.src_cq_hndl = thr->scq;

	desc->context = context;
#ifdef DEBUG
	__sync_fetch_and_add(&ugni_io_count, 1);
#endif /* DEBUG */

	THR_LOCK(thr);
	wr->state = Z_UGNI_WR_PENDING;
	TAILQ_INSERT_TAIL(&thr->pending_rdma_wrq, wr, entry);
	wr->seq = thr->wr_seq++;
	z_ugni_io_thread_wakeup(thr);
	THR_UNLOCK(thr);
	/* no need to copy data to wr like send b/c the application won't touch
	 * it until it get completion event */
	zerr = ZAP_ERR_OK;
 out:
	return zerr;
}

static zap_err_t z_ugni_write(zap_ep_t ep, zap_map_t src_map, char *src,
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

	if (z_map_access_validate(src_map, src, sz, ZAP_ACCESS_NONE) != 0)
		return ZAP_ERR_LOCAL_LEN;
	if (z_map_access_validate(dst_map, dst, sz, ZAP_ACCESS_WRITE) != 0)
		return ZAP_ERR_REMOTE_PERMISSION;

	gni_mem_handle_t *src_mh = map_mh(src_map);
	gni_mem_handle_t *dst_mh = map_mh(dst_map);
	if (!src_mh || !dst_mh)
		return ZAP_ERR_RESOURCE;

	struct z_ugni_ep *uep = (void*)ep;
	struct z_ugni_io_thread *thr = (void*)ep->thread;

	/* node state validation */
	zerr = __node_state_check(uep);
	if (zerr)
		return zerr;

	EP_LOCK(ep);
	if (!uep->gni_ep || ep->state != ZAP_EP_CONNECTED) {
		zerr = ZAP_ERR_NOT_CONNECTED;
		EP_UNLOCK(ep);
		goto out;
	}
	EP_UNLOCK(ep);

	struct z_ugni_wr *wr = z_ugni_alloc_post_desc(uep);
	if (!wr) {
		zerr = ZAP_ERR_RESOURCE;
		goto out;
	}
	struct zap_ugni_rdma_wr *desc = wr->rdma_wr;

	desc->post.type = GNI_POST_RDMA_PUT;
	desc->post.cq_mode = GNI_CQMODE_GLOBAL_EVENT;
	desc->post.dlvr_mode = GNI_DLVMODE_PERFORMANCE;
	desc->post.local_addr = (uint64_t)src;
	desc->post.local_mem_hndl = *src_mh;
	desc->post.remote_addr = (uint64_t)dst;
	desc->post.remote_mem_hndl = *dst_mh;
	desc->post.length = sz;
	desc->context = context;

#ifdef DEBUG
	__sync_fetch_and_add(&ugni_io_count, 1);
#endif /* DEBUG */
	EP_THR_LOCK(uep);
	wr->state = Z_UGNI_WR_PENDING;
	TAILQ_INSERT_TAIL(&thr->pending_rdma_wrq, wr, entry);
	wr->seq = thr->wr_seq++;
	z_ugni_io_thread_wakeup(thr);
	EP_THR_UNLOCK(uep);
	zerr = ZAP_ERR_OK;
out:
	return zerr;
}

void z_ugni_io_thread_cleanup(void *arg)
{
	struct z_ugni_io_thread *thr = arg;
	if (thr->cch)
		GNI_CompChanDestroy(thr->cch);
	if (thr->scq)
		GNI_CqDestroy(thr->scq);
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
	int rc;
	if (uep->sock < 0) {
		LOG("ERROR: %s:%d socket closed.\n", __func__, __LINE__);
		return EINVAL;
	}
	__get_ep(&uep->ep, "sock");
	/* uep->ev is already prepared */
	rc = epoll_ctl(thr->efd, EPOLL_CTL_ADD, uep->sock, &uep->ev);
	if (rc) {
		__put_ep(&uep->ep, "sock");
		LOG("ERROR: %s:%d epoll ADD error: %d.\n", __func__, __LINE__, errno);
		return errno;
	}
	uep->sock_enabled = 1;
	return 0;
}

/* Must hold EP_LOCK */
static int z_ugni_disable_sock(struct z_ugni_ep *uep)
{
	struct z_ugni_io_thread *thr = (void*)uep->ep.thread;
	struct epoll_event ignore;
	if (!uep->sock_enabled)
		return 0;
	assert(uep->sock >= 0);
	if (uep->sock < 0)
		return EINVAL;
	CONN_LOG("%p disabling socket\n", uep);
	epoll_ctl(thr->efd, EPOLL_CTL_DEL, uep->sock, &ignore);
	close(uep->sock);
	uep->sock = -1;
	uep->sock_enabled = 0;
	__put_ep(&uep->ep, "sock");
	return 0;
}

/* Must hold EP_LOCK */
static int __enable_epoll_out(struct z_ugni_ep *uep)
{
	int rc;
	z_ugni_io_thread_t thr = (z_ugni_io_thread_t)uep->ep.thread;
	if (uep->ev.events & EPOLLOUT)
		return 0; /* already enabled */
	if (uep->sock < 0)
		return EINVAL;
	DLOG("ep: %p, Enabling EPOLLOUT\n", uep);
	uep->ev.events = EPOLLIN|EPOLLOUT;
	rc = epoll_ctl(thr->efd, EPOLL_CTL_MOD, uep->sock, &uep->ev);
	return rc;
}

/* Must hold EP_LOCK */
static int __disable_epoll_out(struct z_ugni_ep *uep)
{
	int rc;
	z_ugni_io_thread_t thr = (z_ugni_io_thread_t)uep->ep.thread;
	if ((uep->ev.events & EPOLLOUT) == 0)
		return 0; /* already disabled */
	if (uep->sock < 0)
		return EINVAL;
	DLOG("ep: %p, Disabling EPOLLOUT\n", uep);
	uep->ev.events = EPOLLIN;
	rc = epoll_ctl(thr->efd, EPOLL_CTL_MOD, uep->sock, &uep->ev);
	return rc;
}
static int z_ugni_thr_submit_pending(struct z_ugni_io_thread *thr,
				     struct z_ugni_wrq *wrq)
{
	int rc;
	gni_return_t grc;
	struct z_ugni_wr *wr;
	struct z_ugni_ep *uep;

	THR_LOCK(thr);
 next:
	wr = TAILQ_FIRST(wrq);
	if (!wr)
		goto out;
	if (!z_ugni_get_post_credit(thr, wr))
		goto out;
	uep = wr->uep;
	Z_GNI_API_LOCK(uep->ep.thread);
#ifdef EP_LOG_ENABLED
	const char *op = "UNKNOWN";
	if (wr->rdma_wr->post.type == GNI_POST_RDMA_GET) {
		op = "read";
	} else if (wr->rdma_wr->post.type == GNI_POST_RDMA_PUT) {
		op = "write";
		switch (wr->type) {
		case Z_UGNI_WR_SEND:
			op = "send";
			break;
		case Z_UGNI_WR_SEND_MAPPED:
			op = "send_mapped";
			break;
		case Z_UGNI_WR_RDMA:
			op = "write";
			break;
		default:
			assert(0 == "BAD TYPE");
		}
	}
#endif
	EP_LOG(uep, "resume %s %p\n", op, wr);
#ifdef DEBUG
	if (wr->type == Z_UGNI_WR_RDMA) {
		LLOG("%p posting RDMA read/write %p\n", uep, wr);
	} else {
		int msg_type = ntohs(wr->send_wr->msg->hdr.msg_type);
		LLOG("%p posting SEND over RDMA PUT %s (%d) %p\n",
		     uep, zap_ugni_msg_type_str(msg_type), msg_type, wr);
	}
#endif
	wr->rdma_wr->post.post_id = __sync_fetch_and_add(&ugni_post_id, 1);
	grc = GNI_PostRdma(uep->gni_ep, wr->post);
	Z_GNI_API_UNLOCK(uep->ep.thread);
	if (grc != GNI_RC_SUCCESS) {
		z_ugni_put_post_credit(thr, wr);
		THR_UNLOCK(thr);
		EP_LOG(uep, "error %s %p\n", op, wr);
		LLOG("GNI_PostRdma() error: %d, uep: %p\n", grc, uep);
		rc = EIO;
		EP_LOCK(uep);
		z_ugni_ep_error(uep);
		EP_UNLOCK(uep);
		THR_LOCK(thr);
		z_ugni_thr_ep_flush(thr, uep);
		THR_UNLOCK(thr);
		goto err;
	}
	switch (wr->type) {
	case Z_UGNI_WR_RDMA:
		ATOMIC_INC(&z_ugni_stat.rdma_submitted, 1);
		ATOMIC_INC(&z_ugni_stat.active_rdma, 1);
		break;
	default:
		assert(0 == "BAD TYPE");
	}
	EP_LOG(uep, "post %s %p\n", op, wr);
	assert(wr->state == Z_UGNI_WR_PENDING);
	TAILQ_REMOVE(wrq, wr, entry);
	TAILQ_INSERT_TAIL(&thr->submitted_wrq, wr, entry);
	wr->state = Z_UGNI_WR_SUBMITTED;
	goto next;
 out:
	THR_UNLOCK(thr);
	return 0;
 err:
	return rc;
}

/* EP_LOCK must NOT be held */
static int __on_wr_send_comp(struct z_ugni_wr *wr)
{
	struct z_ugni_ep *uep = wr->uep;
	struct zap_event zev = {0};
	ATOMIC_INC(&z_ugni_stat.send_completed, 1);
	ATOMIC_INC(&z_ugni_stat.active_send, -1);
	zev.status = wr->send_wr->status;
	DLLOG("Send completed\n");
	switch (wr->type) {
	case Z_UGNI_WR_SEND_MAPPED:
		zev.context = wr->send_wr->ctxt;
		zev.type = ZAP_EVENT_SEND_MAPPED_COMPLETE;
		break;
	case Z_UGNI_WR_SEND:
		zev.type = ZAP_EVENT_SEND_COMPLETE;
		break;
	default:
		assert(0 == "BAD TYPE");
		return EINVAL;
	}
	uep->ep.cb(&uep->ep, &zev);
	return 0;
}

/* must hold THR_LOCK */
static void z_ugni_release_wr(struct z_ugni_io_thread *thr, struct z_ugni_wr *wr)
{
	z_ugni_put_post_credit(thr, wr);
	z_ugni_free_wr(wr);
}

static int __on_wr_rdma_comp(struct z_ugni_io_thread *thr,struct z_ugni_wr *wr, gni_return_t grc)
{
	struct zap_event zev = {0};
	gni_post_descriptor_t *post;

	ATOMIC_INC(&z_ugni_stat.rdma_completed, 1);
	ATOMIC_INC(&z_ugni_stat.active_rdma, -1);
	#ifdef EP_LOG_ENABLED
	const char *op = "UNKNOWN";
	if (wr->rdma_wr->post.type == GNI_POST_RDMA_GET) {
		op = "read";
	} else if (wr->rdma_wr->post.type == GNI_POST_RDMA_PUT) {
		op = "write";
	}
	#endif
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
		TAILQ_REMOVE(&thr->stalled_wrq, wr, entry);
		z_ugni_release_wr(thr, wr);
		goto out;
	}
	post = &wr->rdma_wr->post;
	struct z_ugni_ep *uep = wr->uep;
	if (!uep) {
		/*
		 * This should not happen. The code is put in to prevent
		 * the segmentation fault and to record the situation.
		 */
		LOG("%s: %s: desc->uep = NULL. Drop the descriptor.\n", __func__,
			wr->rdma_wr->ep_name);
		goto out;
	}
	EP_LOG(uep, "complete %s %p (grc: %d)\n", op, wr, grc);
	zev.ep = &uep->ep;
	switch (post->type) {
	case GNI_POST_RDMA_GET:
		/* zap read completion */
		DLOG_(uep, "RDMA_GET: Read complete %p with %s\n", post, gni_ret_str(grc));
		if (grc) {
			zev.status = ZAP_ERR_RESOURCE;
			LOG_(uep, "RDMA_GET: completing "
				"with error %s.\n",
				gni_ret_str(grc));
		} else {
			zev.status = ZAP_ERR_OK;
		}
		zev.type = ZAP_EVENT_READ_COMPLETE;
		zev.context = wr->rdma_wr->context;
		break;
	case GNI_POST_RDMA_PUT:
		/* could be zap_write, zap_send or zap_send_map completions */
		DLOG_(uep, "RDMA_PUT: Write complete %p with %s\n",
					post, gni_ret_str(grc));
		if (grc) {
			zev.status = ZAP_ERR_RESOURCE;
			DLOG_(uep, "RDMA_PUT: completing "
				"with error %s.\n",
				gni_ret_str(grc));
		} else {
			zev.status = ZAP_ERR_OK;
		}
		zev.type = ZAP_EVENT_WRITE_COMPLETE;
		zev.context = wr->rdma_wr->context;
		break;
	default:
		LOG_(uep, "Unknown completion type %d.\n",
				 post->type);
		EP_LOCK(uep);
		z_ugni_ep_error(uep);
		EP_UNLOCK(uep);
	}
	uep->ep.cb(&uep->ep, &zev);
	EP_LOCK(uep);
	THR_LOCK(thr);
	uep->active_wr--;
	if (0 == uep->active_wr && TAILQ_EMPTY(&uep->send_wrq))
		pthread_cond_signal(&uep->sq_cond);
	TAILQ_REMOVE(&thr->submitted_wrq, wr, entry);
	z_ugni_release_wr(thr, wr);
	THR_UNLOCK(thr);
	EP_UNLOCK(uep);
 out:
	return 0;
}

static void z_ugni_handle_scq_events(struct z_ugni_io_thread *thr,
				     gni_cq_handle_t cq,
				     int cqe_type)
{
	gni_cq_entry_t cqe;
	gni_return_t grc;
	uint64_t ev_type;
#ifdef CONN_DEBUG
	uint64_t ev_source;
	uint64_t ev_status;
	uint64_t ev_overrun;
#endif
	struct z_ugni_wr *wr;
	gni_post_descriptor_t *post;

 next:
	cqe = 0;
	Z_GNI_API_LOCK(&thr->zap_io_thread);
	grc = GNI_CqGetEvent(cq, &cqe);
	Z_GNI_API_UNLOCK(&thr->zap_io_thread);
	switch (grc) {
	case GNI_RC_SUCCESS:
	case GNI_RC_TRANSACTION_ERROR:
		break;
	case GNI_RC_ERROR_RESOURCE:
		LOG("GNI_CqGetEvent() error: %s(%d)\n", gni_err_str[grc], grc);
		goto out;
	case GNI_RC_NOT_DONE:
		goto out;
	default:
		LOG("GNI_CqGetEvent() unexpected rc: %s(%d)\n", gni_err_str[grc], grc);
		assert(0 == "Unexpected return code");
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
	assert(ev_type == cqe_type);
	if (ev_type != GNI_CQ_EVENT_TYPE_POST) {
		LOG("Unexpected cq event: %d\n", ev_type);
		assert(0 == "Unexpected cq event.");
		goto next;
	}
	post = NULL;
	Z_GNI_API_LOCK(&thr->zap_io_thread);
	grc = GNI_GetCompleted(thr->scq, cqe, &post);
	Z_GNI_API_UNLOCK(&thr->zap_io_thread);
	if (grc != GNI_RC_SUCCESS) {
		LOG("GNI_GetCompleted() error: %d\n", grc);
		goto next;
	}
	grc = GNI_CQ_GET_STATUS(cqe);
	assert( grc == 0 );
	wr = __container_of(post, struct z_ugni_wr, rdma_wr);

	switch (wr->type) {
	case Z_UGNI_WR_RDMA:
		if (grc)
			LLOG("Z_UGNI_WR_RDMA error: %d\n", grc);
		__on_wr_rdma_comp(thr, wr, grc);
		break;
	case Z_UGNI_WR_SEND:
	case Z_UGNI_WR_SEND_MAPPED:
		LLOG("Unexpected WR type: %d\n", wr->type);
		assert(0);
		break;
	}
	assert(GNI_CQ_OVERRUN(cqe) == 0); /* rcq OVERRUN */
	goto next;
 out:
	return ;
}

static void z_ugni_handle_cq_event(struct z_ugni_io_thread *thr)
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

	/* re-arm */
	Z_GNI_API_LOCK(&thr->zap_io_thread);
	grc = GNI_CqArmCompChan(&cq, 1);
	Z_GNI_API_UNLOCK(&thr->zap_io_thread);
	if (grc != GNI_RC_SUCCESS) {
		LOG("Unexpected error from GNI_CqArmCompChan(): %d\n", grc);
		assert(0 == "Unexpected error from GNI_CompChanGenEvent()");
	}

	assert( cq == thr->scq );
	z_ugni_handle_scq_events(thr, cq, GNI_CQ_EVENT_TYPE_POST);
}

/* EP_LOCK is NOT held */
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

	new_uep->sock_connected = 1;

	__cache_sockaddr(new_uep);

	/* prep epoll ev */
	new_uep->sock_epoll_ctxt.type = Z_UGNI_SOCK_EVENT;
	new_uep->ev.events = EPOLLIN;
	new_uep->ev.data.ptr = &new_uep->sock_epoll_ctxt;

	rc = zap_io_thread_ep_assign(new_ep);
	if (rc) {
		LOG_(new_uep, "thread assignment error: %d\n", rc);
		zap_free(new_ep);
		return;
	}

	/*
	 * NOTE: At this point, the connection is socket-connected. The next
	 * step would be setting up GNI EP and messaging resources. The active
	 * side will send z_ugni_sock_send_conn_req message over socket to
	 * initiate the setup.
	 */

	return;
}

/* Fill `zev->type` and `zev->context`. Returns 0 if OK. */
static int wr_zap_event(struct z_ugni_wr *wr, struct zap_event *zev)
{
	switch (wr->type) {
	case Z_UGNI_WR_SEND:
		zev->type = ZAP_EVENT_SEND_COMPLETE;
		return 0;
	case Z_UGNI_WR_SEND_MAPPED:
		zev->type = ZAP_EVENT_SEND_MAPPED_COMPLETE;
		zev->context = wr->send_wr->ctxt;
		return 0;
	case Z_UGNI_WR_RDMA:
		switch (wr->post->type) {
		case GNI_POST_RDMA_GET:
			zev->type = ZAP_EVENT_READ_COMPLETE;
			zev->context = wr->rdma_wr->context;
			return 0;
		case GNI_POST_RDMA_PUT:
			zev->type = ZAP_EVENT_WRITE_COMPLETE;
			zev->context = wr->rdma_wr->context;
			return 0;
		default:
			assert(0 == "BAD TYPE");
			return EINVAL;
		}
		break;
	}
	assert(0 == "BAD TYPE");
	return EINVAL;
}

/* Must hold thr lock */
static void z_ugni_thr_ep_flush(struct z_ugni_io_thread *thr, struct z_ugni_ep *uep)
{
	struct z_ugni_wr *wr, *next_wr;
	struct z_ugni_wrq *h = &thr->pending_rdma_wrq;

	wr = TAILQ_FIRST(h);
	while (wr) {
		next_wr = TAILQ_NEXT(wr, entry);
		if (wr->uep != uep)
			goto next;
		TAILQ_REMOVE(h, wr, entry);
		TAILQ_INSERT_TAIL(&uep->flushed_wrq, wr, entry);
	next:
		wr = next_wr;
	}
}

/* uep->ep.lock MUST be held, THR_LOCK must NOT be held */
static void z_ugni_flush(struct z_ugni_ep *uep, struct z_ugni_io_thread *thr)
{
	/* This function is only called from the io thread */
	struct zap_event zev = { .status = ZAP_ERR_FLUSH, .ep = &uep->ep };
	struct z_ugni_wr *wr, *next_wr;
	int rc;

	/* Deliver left-over send completions */
	z_ugni_process_sock_send_comp(uep);

	THR_LOCK(thr);

	z_ugni_thr_ep_flush(thr, uep);

	wr = TAILQ_FIRST(&thr->submitted_wrq);
	while (wr) {
		next_wr = TAILQ_NEXT(wr, entry);
		if (wr->uep != uep) /* not our WR */
			goto next_sub;
		TAILQ_REMOVE(&thr->submitted_wrq, wr, entry);
		TAILQ_INSERT_TAIL(&thr->stalled_wrq, wr, entry);
		wr->uep = NULL;
		wr->state = Z_UGNI_WR_STALLED;
		z_ugni_put_post_credit(thr, wr);
		rc = wr_zap_event(wr, &zev);
		if (rc == 0) {
			/* deliver flush event */
			THR_UNLOCK(thr);
			EP_UNLOCK(uep);
			zap_event_deliver(&zev);
			EP_LOCK(uep);
			THR_LOCK(thr);
		}
	next_sub:
		wr = next_wr;
	}

	THR_UNLOCK(thr);

	/* Go through endpoint's flushed wrq */
	while ( (wr = TAILQ_FIRST(&uep->flushed_wrq)) ) {
		TAILQ_REMOVE(&uep->flushed_wrq, wr, entry);
		rc = wr_zap_event(wr, &zev);
		if (rc == 0) {
			EP_UNLOCK(uep);
			zap_event_deliver(&zev);
			EP_LOCK(uep);
		}
		/* wr has not been submitted */
		z_ugni_free_wr(wr);
	}

	/* Now, go through endpoint's pending wr queue. */
	while ( (wr = TAILQ_FIRST(&uep->send_wrq)) ) {
		TAILQ_REMOVE(&uep->send_wrq, wr, entry);
		__atomic_fetch_sub(&uep->ep.sq_sz, 1, __ATOMIC_SEQ_CST);
		/* uep->ep.thread->stat->sq_sz has been handled since
		 * thread_ep_release */
		if (wr->send_wr->hdr_off)
			ATOMIC_INC(&z_ugni_stat.active_send, -1);
		rc = wr_zap_event(wr, &zev);
		if (rc == 0) {
			EP_UNLOCK(uep);
			zap_event_deliver(&zev);
			EP_LOCK(uep);
		}
		/* wr has not been submitted */
		z_ugni_free_wr(wr);
	}

}

/* EP_LOCK is held */
static int z_ugni_sock_send_conn_req(struct z_ugni_ep *uep)
{
	int n;
	struct zap_ugni_msg_buf mbuf;
	int msg_len = sizeof(struct zap_ugni_msg_connect) + uep->conn_data_len;

	assert(msg_len <= ZAP_UGNI_BUFSZ);

	CONN_LOG("%p sock-sending conn_req\n", uep);

	assert(uep->ep.state == ZAP_EP_CONNECTING);

	memset(&mbuf, 0, sizeof(mbuf));
	mbuf.msg.hdr.msg_len = htonl(msg_len);
	mbuf.msg.hdr.msg_type = htons(ZAP_UGNI_MSG_CONNECT);

	mbuf.msg.connect.ep_desc.inst_id = htonl(_dom.inst_id.u32);
	mbuf.msg.connect.ep_desc.pe_addr = htonl(_dom.pe_addr);
	memcpy(mbuf.msg.connect.sig, ZAP_UGNI_SIG, sizeof(ZAP_UGNI_SIG));
	ZAP_VERSION_SET(mbuf.msg.connect.ver);

	mbuf.msg.connect.data_len = htonl(uep->conn_data_len);

	memcpy(mbuf.msg.connect.data, uep->conn_data, uep->conn_data_len);

	n = write(uep->sock, &mbuf, msg_len);
	if (n != msg_len) {
		z_ugni_ep_error(uep);
		return EIO;
	}
	return 0;
}

/* EP_LOCK is held */
static int z_ugni_sock_send_conn_accept(struct z_ugni_ep *uep, const char *data, size_t data_len)
{
	/* NOTE: This is not the application accept message. It is a socket
	 *       message agreeing to establish GNI communication. */
	int n;
	struct zap_ugni_msg_buf mbuf;
	int msg_len = sizeof(struct zap_ugni_msg_accept) + data_len;

	CONN_LOG("%p sock-sending conn_accept\n", uep);

	if (msg_len > sizeof(mbuf)) {
		CONN_LOG("%p data too long: %d\n", data_len);
		return ENOSPC;
	}

	assert(uep->ep.state == ZAP_EP_ACCEPTING);

	memset(&mbuf, 0, sizeof(mbuf));
	mbuf.msg.hdr.msg_len = htonl(msg_len);
	mbuf.msg.hdr.msg_type = htons(ZAP_UGNI_MSG_ACCEPTED);
	mbuf.msg.accept.ep_desc.inst_id = htonl(_dom.inst_id.u32);
	mbuf.msg.accept.ep_desc.pe_addr = htonl(_dom.pe_addr);
	mbuf.msg.accept.data_len = htonl(data_len);
	memcpy(mbuf.msg.accept.data, data, data_len);
	n = write(uep->sock, &mbuf, msg_len);
	if (n != msg_len) {
		assert(0 == "cannot write");
		/* REASON: msg is super small and it is the only message to be
		 *         sent on the socket. So, we expect it to successfully
		 *         copied over to the kernel buffer in one go. */
		uep->ep.state = ZAP_EP_ERROR;
		z_ugni_disable_sock(uep);
		/* This results in `accept()` synchronous error, do not post
		 * connect error event */
		return EIO;
	}
	return 0;
}

static int z_ugni_setup_conn(struct z_ugni_ep *uep, struct z_ugni_ep_desc *ep_desc)
{
	gni_return_t grc;
	CONN_LOG("%p setting up GNI connection\n");
	uep->peer_ep_desc = *ep_desc;
	/* bind remote endpoint (RDMA) */
	Z_GNI_API_LOCK(uep->ep.thread);
	grc = GNI_EpBind(uep->gni_ep, ep_desc->pe_addr, ep_desc->inst_id);
	Z_GNI_API_UNLOCK(uep->ep.thread);
	if (grc != GNI_RC_SUCCESS) {
		LOG_(uep, "GNI_EpBind() error: %d\n", grc);
		goto out;
	}
	CONN_LOG("%p GNI endpoint bound\n");
	uep->ugni_ep_bound = 1;
 out:
	return gni_rc_to_errno(grc);
}

/* EP_LOCK is NOT held */
static void z_ugni_sock_recv(struct z_ugni_ep *uep)
{
	int n, mlen;
	struct zap_ugni_msg *msg;

	/* read full header first */
	while (uep->sock_off < sizeof(struct zap_ugni_msg_hdr)) {
		/* need to get the entire header to know the full msg len */
		n = sizeof(struct zap_ugni_msg_hdr) - uep->sock_off;
		n = read(uep->sock, uep->mbuf.buf + uep->sock_off, n);
		if (n < 0) {
			if (errno == EAGAIN) /* this is OK */
				return;
			/* Otherwise, read error */
			goto err;
		}
		if (n == 0) {
			errno = ECOMM;
			goto err;
		}
		uep->sock_off += n;
	}
	mlen = ntohl(uep->mbuf.msg.hdr.msg_len);
	/* read entire message */
	while (uep->sock_off < mlen) {
		n = read(uep->sock, uep->mbuf.buf + uep->sock_off,
				    mlen - uep->sock_off);
		if (n < 0) {
			if (errno == EAGAIN) /* this is OK */
				return;
			/* Otherwise, read error */
			goto err;
		}
		if (n == 0) {
			errno = ECOMM;
			goto err;
		}
		uep->sock_off += n;
	}
	/* message read completed, reset offset */
	uep->sock_off = 0;

	msg = &uep->mbuf.msg;

	/* network-to-host */
	msg->hdr.msg_len = ntohl(msg->hdr.msg_len);
	msg->hdr.msg_type = ntohs(msg->hdr.msg_type);

	if (ZAP_UGNI_MSG_NONE < msg->hdr.msg_type && msg->hdr.msg_type < ZAP_UGNI_MSG_TYPE_LAST) {
		process_uep_msg_fns[msg->hdr.msg_type](uep);
	} else {
		/* unknown message */
		process_uep_msg_unknown(uep);
	}

	return;
 err:
	/* EOF or read error occur duing message aggregation */
	EP_LOCK(uep);
	if (uep->ep.state == ZAP_EP_ACCEPTING) {
		z_ugni_ep_error(uep);
		EP_UNLOCK(uep);
		zap_free(&uep->ep); /* b/c zap_new() in conn_req */
	} else {
		z_ugni_ep_error(uep);
		EP_UNLOCK(uep);
	}
}

/* EP_LOCK is held */
static void z_ugni_sock_hup(struct z_ugni_ep *uep)
{
	z_ugni_disable_sock(uep);
	switch (uep->ep.state) {
	case ZAP_EP_CONNECTING:
		uep->ep.state = ZAP_EP_ERROR;
		z_ugni_zq_try_post(uep, 0, ZAP_EVENT_CONNECT_ERROR, ZAP_ERR_ENDPOINT);
		break;
	case ZAP_EP_ACCEPTING:
		/* application does not know about this endpoint yet */
		uep->ep.state = ZAP_EP_ERROR;
		zap_free(&uep->ep); /* b/c zap_new() in conn_req */
		break;
	case ZAP_EP_CONNECTED:
		/* peer closed the connection */
		uep->ep.state = ZAP_EP_PEER_CLOSE;
		z_ugni_zq_try_post(uep, 0, ZAP_EVENT_DISCONNECTED, ZAP_ERR_OK);
		break;
	case ZAP_EP_CLOSE:
		/* app call close */
		z_ugni_zq_try_post(uep, 0, ZAP_EVENT_DISCONNECTED, ZAP_ERR_OK);
		break;
	case ZAP_EP_ERROR:
		/* no-op */
		break;
	case ZAP_EP_PEER_CLOSE:
	case ZAP_EP_INIT:
	case ZAP_EP_LISTENING:
		assert(0 == "Unexpected endpoint state");
	}
}

/* Must hold EP_LOCK.
 * This function could be called from the io thread, or the application thread
 * (subsequent call from zap_send() or zap_send_mapped()). */
static void z_ugni_sock_send(z_ugni_ep_t uep)
{
	/* Send messages in the pending message queue */
	struct z_ugni_wr *wr;
	struct zap_ugni_send_wr *send_wr;
	int len;
 next:
	wr = TAILQ_FIRST(&uep->send_wrq);
	if (!wr) {
		if (TAILQ_EMPTY(&uep->send_comp_wrq)) {
			/* no send WR, no completion */
			__disable_epoll_out(uep);
		} else {
			/* has completion to process by uep->thr */
			__enable_epoll_out(uep);
		}
		goto out;
	}

	/* We need to write the fixed-length message body and the
	 * variable-length data separately because the data from
	 * `zap_send_mappped()` is not located next to the message body. */

	send_wr = wr->send_wr;

	/* writing fixed-length message body */
	while (send_wr->hdr_off < send_wr->hdr_len) {
		len = send(uep->sock, send_wr->msg->bytes + send_wr->hdr_off,
			   send_wr->hdr_len - send_wr->hdr_off, MSG_NOSIGNAL);
		if (len < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				__enable_epoll_out(uep);
				goto out;
			}
			/* otherwise, bad error */
			goto err;
		}
		if (len == 0) {
			/* peer hang up */
			goto err;
		}
		send_wr->hdr_off += len;
	}

	/* writing the variable-length data */
	while (send_wr->data_off < send_wr->data_len) {
		len = send(uep->sock, send_wr->data + send_wr->data_off,
			   send_wr->data_len - send_wr->data_off, MSG_NOSIGNAL);
		if (len < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				__enable_epoll_out(uep);
				goto out;
			}
			/* otherwise, bad error */
			goto err;
		}
		if (len == 0) {
			/* peer hang up */
			goto err;
		}
		send_wr->data_off += len;
	}
	/* move wr into send completion queue */
	TAILQ_REMOVE(&uep->send_wrq, wr, entry);
	__atomic_fetch_sub(&uep->ep.sq_sz, 1, __ATOMIC_SEQ_CST);
	if (uep->ep.thread)
		__atomic_fetch_sub(&uep->ep.thread->stat->sq_sz, 1, __ATOMIC_SEQ_CST);
	TAILQ_INSERT_TAIL(&uep->send_comp_wrq, wr, entry);

	goto next;
 out:
	return;

 err:
	z_ugni_ep_error(uep);
}

/* Must hold EP_LOCK */
static void z_ugni_process_sock_send_comp(z_ugni_ep_t uep)
{
	struct z_ugni_wr *wr;
	while ((wr = TAILQ_FIRST(&uep->send_comp_wrq))) {
		TAILQ_REMOVE(&uep->send_comp_wrq, wr, entry);
		EP_UNLOCK(uep);
		__on_wr_send_comp(wr);
		z_ugni_free_wr(wr);
		EP_LOCK(uep);
	}
	if (TAILQ_EMPTY(&uep->send_wrq)) {
		__disable_epoll_out(uep);
	}
}

static void z_ugni_handle_sock_event(struct z_ugni_ep *uep, int events)
{
	__get_ep(&uep->ep, "sock_event");
	EP_LOCK(uep);
	if (uep->ep.state == ZAP_EP_LISTENING) {
		/* This is a listening endpoint */
		EP_UNLOCK(uep);
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
		EP_UNLOCK(uep);
		goto out;
	}
	if (events & EPOLLOUT) {
		if (uep->sock_connected) {
			/* sock-connected, ready to send */
			z_ugni_sock_send(uep);
			z_ugni_process_sock_send_comp(uep);
		} else if (uep->ep.state == ZAP_EP_CONNECTING) {
			/* just become sock-connected */
			uep->sock_connected = 1;
			__cache_sockaddr(uep);
			CONN_LOG("uep %p becoming sock-connected\n", uep);
			__disable_epoll_out(uep);
			/* send connect message */
			z_ugni_sock_send_conn_req(uep);
		} else {
			/* bad state */
			assert(0 == "BAD STATE");
			EP_UNLOCK(uep);
			goto out;
		}
	}
	EP_UNLOCK(uep);
	if ((events & EPOLLIN) && uep->sock >= 0) {
		/* NOTE: sock may be disabled by z_ugni_sock_XXX(uep) above */
		z_ugni_sock_recv(uep);
	}
 out:
	__put_ep(&uep->ep, "sock_event");
}

static void z_ugni_io_thread_wakeup(struct z_ugni_io_thread *thr)
{
	static const char c = 1;
	write(thr->zq_fd[1], &c, 1);
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

	THR_LOCK(thr);
	rbn = rbt_min(&thr->zq);
	if (rbn) {
		ts_min = ((struct z_ugni_ev*)rbn)->ts_msec;
	}
	rbt_ins(&thr->zq, &uev->rbn);
	uev->in_zq = 1;
	if (uev->ts_msec < ts_min) /* notify the thread if wait time changed */
		write(thr->zq_fd[1], &c, 1);
	THR_UNLOCK(thr);
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
	__get_ep(&uep->ep, "uev"); /* will be put when uev is processed */
	uep->uev.ts_msec = ts_msec;
	rbn_init(&uep->uev.rbn, &uep->uev.ts_msec);
	uep->uev.zev.ep = &uep->ep;
	uep->uev.zev.type = type;
	uep->uev.zev.status = status;
	z_ugni_zq_post((void*)uep->ep.thread, &uep->uev);
}

__attribute__((unused))
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
	THR_LOCK(thr);
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
	THR_UNLOCK(thr);
	__put_ep(&uep->ep, "uev"); /* taken in __post_zq() */
}

/* return timeout in msec to be used by epoll_wait(). */
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
	THR_LOCK(thr);
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

		uep = (void*)uev->zev.ep;
		switch (uev->zev.type) {
		case ZAP_EVENT_CONNECT_ERROR:
		case ZAP_EVENT_DISCONNECTED:
			THR_UNLOCK(thr);
			zap_io_thread_ep_release(&uep->ep);
			EP_LOCK(uep);
			z_ugni_flush(uep, thr);
			EP_UNLOCK(uep);
			zap_event_deliver(&uev->zev);
			__put_ep(&uep->ep, "accept/connect"); /* taken in z_ugni_connect()/z_ugni_accept() */
			THR_LOCK(thr);
			break;
		default:
			LLOG("Unexpected event in zq: %s(%d)\n",
				zap_event_str(uev->zev.type),
				uev->zev.type);
			assert(0 == "Unexpected event in zq");
		}

		__put_ep(&uep->ep, "uev"); /* taken in __post_zq() */
	}
	THR_UNLOCK(thr);
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
	int was_not_empty;

	pthread_cleanup_push(z_ugni_io_thread_cleanup, thr);

 loop:
	timeout = z_ugni_handle_zq_events(thr, 0);
	if (timeout < 0 || timeout > 250) {
		/* max timeout; see 'desperately reaping CQ' note below. */
		timeout = 250;
	}
	zap_thrstat_wait_start(thr->zap_io_thread.stat);
	n = epoll_wait(thr->efd, ev, N_EV, timeout);
	zap_thrstat_wait_end(thr->zap_io_thread.stat);
	was_not_empty = !TAILQ_EMPTY(&thr->submitted_wrq)    ||
			!TAILQ_EMPTY(&thr->pending_rdma_wrq) ;
	for (i = 0; i < n; i++) {
		ctxt = ev[i].data.ptr;
		switch (ctxt->type) {
		case Z_UGNI_CQ_EVENT:
			z_ugni_handle_cq_event(thr);
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
	/* NOTE: desperately reaping CQ as sometimes the completion channel does
	 *       not deliver the CompChanEvent.
	 */
	z_ugni_handle_scq_events(thr, thr->scq, GNI_CQ_EVENT_TYPE_POST);

	/*
	 * NOTE: zap_ugni need to submit pending entry here because when it
	 * submitted the pending entry right after a CQ entry has been
	 * processed, the rcq (receive CQ / remote CQ) got OVERRUN error.
	 * Draining all completion entries and submiting the pending entries
	 * later (here) made the rcq OVERRUN error disappear.
	 */
	z_ugni_thr_submit_pending(thr, &thr->pending_rdma_wrq);
	THR_LOCK(thr);
	if (z_ugni_stat_log_enabled && was_not_empty
			&& TAILQ_EMPTY(&thr->submitted_wrq)
			&& TAILQ_EMPTY(&thr->pending_rdma_wrq)) {
		LLOG("send queue empty, z_ugni_stat: \n"
		     "  z_ugni_stat.send_submitted: %d\n"
		     "  z_ugni_stat.send_completed: %d\n"
		     "  z_ugni_stat.rdma_submitted: %d\n"
		     "  z_ugni_stat.rdma_completed: %d\n"
		     "  z_ugni_stat.ack_submitted: %d\n"
		     "  z_ugni_stat.ack_completed: %d\n"
		     "  z_ugni_stat.recv_success: %d\n"
		     "  z_ugni_stat.rcq_success: %d\n"
		     "  z_ugni_stat.rcq_rc_not_done: %d\n"
		     "  z_ugni_stat.active_rdma: %d\n"
		     "  z_ugni_stat.active_send: %d\n"
		     "  z_ugni_stat.active_ack: %d\n" ,
		     z_ugni_stat.send_submitted,
		     z_ugni_stat.send_completed,
		     z_ugni_stat.rdma_submitted,
		     z_ugni_stat.rdma_completed,
		     z_ugni_stat.ack_submitted,
		     z_ugni_stat.ack_completed,
		     z_ugni_stat.recv_success,
		     z_ugni_stat.rcq_success,
		     z_ugni_stat.rcq_rc_not_done,
		     z_ugni_stat.active_rdma,
		     z_ugni_stat.active_send,
		     z_ugni_stat.active_ack);
	}
	THR_UNLOCK(thr);

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
	TAILQ_INIT(&thr->stalled_wrq);
	rbt_init(&thr->zq, zqe_cmp);

	thr->rdma_post_credit = zap_ugni_cq_depth/2;
	TAILQ_INIT(&thr->pending_rdma_wrq);
	TAILQ_INIT(&thr->submitted_wrq);
	TAILQ_INIT(&thr->stalled_wrq);

	CONN_LOG("zap thread initializing ...\n");
	rc = zap_io_thread_init(&thr->zap_io_thread, z, "zap_ugni_io",
			ZAP_ENV_INT(ZAP_THRSTAT_WINDOW));
	CONN_LOG("zap thread initialized\n");
	if (rc)
		goto err_1;
	thr->efd = epoll_create1(O_CLOEXEC);
	if (thr->efd < 0)
		goto err_2;
	/*
	 * NOTE on GNI_CQ_BLOCKING
	 * In order to use Completion Channel, GNI_CQ_BLOCKING is required.
	 * `GNI_CqGetEvent()` is still a non-blocking call.
	 */

	/* For RDMA local/source completions (posts) */
	Z_GNI_API_LOCK(&thr->zap_io_thread);
	CONN_LOG("RDMA CqCreate ...\n");
	rc = GNI_CqCreate(_dom.nic, zap_ugni_cq_depth, 0, GNI_CQ_BLOCKING, NULL, NULL, &thr->scq);
	CONN_LOG("RDMA CqCreate ... done.\n");
	Z_GNI_API_UNLOCK(&thr->zap_io_thread);
	if (rc != GNI_RC_SUCCESS) {
		LLOG("GNI_CqCreate() failed: %s(%d)\n", gni_ret_str(rc), rc);
		goto err_3;
	}

	Z_GNI_API_LOCK(&thr->zap_io_thread);
	CONN_LOG("CompChanCreate ...\n");
	rc = GNI_CompChanCreate(_dom.nic, &thr->cch);
	CONN_LOG("CompChanCreate ... done\n");
	Z_GNI_API_UNLOCK(&thr->zap_io_thread);
	if (rc) {
		LLOG("GNI_CompChanCreate() failed: %s(%d)\n", gni_ret_str(rc), rc);
		goto err_5;
	}
	Z_GNI_API_LOCK(&thr->zap_io_thread);
	CONN_LOG("Get CompChanFd ...\n");
	rc = GNI_CompChanFd(thr->cch, &thr->cch_fd);
	CONN_LOG("Get CompChanFd ... done\n");
	Z_GNI_API_UNLOCK(&thr->zap_io_thread);
	if (rc) {
		LLOG("GNI_CompChanFd() failed: %s(%d)\n", gni_ret_str(rc), rc);
		goto err_6;
	}
	Z_GNI_API_LOCK(&thr->zap_io_thread);
	CONN_LOG("RDMA-CQ CqAttachCompChan ...\n");
	rc = GNI_CqAttachCompChan(thr->scq, thr->cch);
	CONN_LOG("RDMA-CQ CqAttachCompChan ... done\n");
	Z_GNI_API_UNLOCK(&thr->zap_io_thread);
	if (rc) {
		LLOG("GNI_CqAttachCompChan() failed: %s(%d)\n", gni_ret_str(rc), rc);
		goto err_6;
	}
	Z_GNI_API_LOCK(&thr->zap_io_thread);
	CONN_LOG("Arming RDMA-CQ ...\n");
	rc = GNI_CqArmCompChan(&thr->scq, 1);
	CONN_LOG("Arming RDMA-CQ ... done\n");
	Z_GNI_API_UNLOCK(&thr->zap_io_thread);
	if (rc) {
		LLOG("GNI_CqArmCompChan() failed: %s(%d)\n", gni_ret_str(rc), rc);
		goto err_6;
	}
	CONN_LOG("Creating zq notification pipe ...\n");
	rc = pipe2(thr->zq_fd, O_NONBLOCK|O_CLOEXEC);
	CONN_LOG("Creating zq notification pipe ... done\n");
	if (rc < 0) {
		LLOG("pipe2() failed, errno: %d\n", errno);
		goto err_7;
	}

	/* cq-epoll */
	ev.events = EPOLLIN;
	thr->cq_epoll_ctxt.type = Z_UGNI_CQ_EVENT;
	ev.data.ptr = &thr->cq_epoll_ctxt;
	CONN_LOG("Adding CompChanFd to epoll\n");
	rc = epoll_ctl(thr->efd, EPOLL_CTL_ADD, thr->cch_fd, &ev);
	if (rc)
		goto err_7;

	/* zq-epoll */
	ev.events = EPOLLIN;
	thr->zq_epoll_ctxt.type = Z_UGNI_ZQ_EVENT;
	ev.data.ptr = &thr->zq_epoll_ctxt;
	CONN_LOG("Adding zq fd to epoll\n");
	rc = epoll_ctl(thr->efd, EPOLL_CTL_ADD, thr->zq_fd[0], &ev);
	if (rc)
		goto err_7;

	CONN_LOG("Creating pthread\n");
	rc = pthread_create(&thr->zap_io_thread.thread, NULL,
			    z_ugni_io_thread_proc, thr);
	if (rc)
		goto err_7;
	pthread_mutex_unlock(&ugni_lock);
	pthread_setname_np(thr->zap_io_thread.thread, "zap_ugni_io");
	CONN_LOG("returning.\n");
	return &thr->zap_io_thread;

 err_7:
	close(thr->zq_fd[0]);
	close(thr->zq_fd[1]);
 err_6:
	Z_GNI_API_LOCK(&thr->zap_io_thread);
	GNI_CompChanDestroy(thr->cch);
	Z_GNI_API_UNLOCK(&thr->zap_io_thread);
 err_5:
	Z_GNI_API_LOCK(&thr->zap_io_thread);
	GNI_CqDestroy(thr->scq);
	Z_GNI_API_UNLOCK(&thr->zap_io_thread);
 err_3:
	close(thr->efd);
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
		thr->scq = 0;
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

	EP_LOCK(uep);

	/* allocate GNI ednpoint. We need to do it here instead of zap_new()
	 * because we don't know which cq to attached to yet. */

	THR_LOCK(t);
	Z_GNI_API_LOCK(&thr->zap_io_thread);
	grc = GNI_EpCreate(_dom.nic, thr->scq, &uep->gni_ep);
	Z_GNI_API_UNLOCK(&thr->zap_io_thread);
	THR_UNLOCK(t);

	if (grc) {
		LOG("GNI_EpCreate(gni_ep) failed: %s\n", gni_ret_str(grc));
		zerr = ZAP_ERR_RESOURCE;
		goto out;
	}
	CONN_LOG("%p created gni_ep %p\n", uep, uep->gni_ep);
	rc = z_ugni_enable_sock(uep);
	if (rc) {
		zerr = ZAP_ERR_RESOURCE;
		goto err_1;
	}

	zerr = ZAP_ERR_OK;
	goto out;

 err_1:
	Z_GNI_API_LOCK(&thr->zap_io_thread);
	GNI_EpDestroy(uep->gni_ep);
	uep->gni_ep = NULL;
	Z_GNI_API_UNLOCK(&thr->zap_io_thread);
 out:
	EP_UNLOCK(uep);
	return zerr;
}

zap_err_t z_ugni_io_thread_ep_release(zap_io_thread_t t, zap_ep_t ep)
{
	/* release ep_idx and mbox */
	z_ugni_ep_t uep = (void*)ep;

	EP_LOCK(uep);
	z_ugni_disable_sock(uep);
	z_ugni_ep_release((void*)ep);
	EP_UNLOCK(uep);
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

	__mem_info_fn = mem_info_fn;
	/* max_msg is the limit of application message size (which is the data
	 * part of zap message). */
	z->max_msg = ZAP_UGNI_MAX_MSG;
	z->new = z_ugni_new;
	z->destroy = z_ugni_destroy;
	z->connect = z_ugni_connect;
	z->accept = z_ugni_accept;
	z->reject = z_ugni_reject;
	z->listen = z_ugni_listen;
	z->close = z_ugni_close;
	z->send = z_ugni_send;
	z->send_mapped = z_ugni_send_mapped;
	z->read = z_ugni_read;
	z->write = z_ugni_write;
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
