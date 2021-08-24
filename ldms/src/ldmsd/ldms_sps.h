#ifndef ldms_sps_h
#define ldms_sps_h
/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2021 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2021 Open Grid Computing, Inc. All rights reserved.
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
#include <pthread.h>
#include "ldms.h"
#include <ovis_json/ovis_json.h>

#define LNOTIFY_TIMEOUT 1

/*
 * This is a simple publishing service (sps) for sending messages
 * ('events' or 'buffers') to LDMSD daemons.
 * It is an abstraction layer which enables application writers to avoid
 * directly using and tracking the ldms_xprt and ldmsd_stream interfaces.
 */

#include <ovis_json/ovis_json.h>
typedef void (*ldms_sps_msg_log_f)(int level, const char *fmt, ...);

struct sps_target;
LIST_HEAD(sps_target_list, sps_target);

struct ldms_sps {
	struct sps_target_list *cl;
	struct sps_target_list sps_target_list;
	pthread_mutex_t list_lock;
	char *stream;
	time_t io_timeout;
	int target_count;
	int blocking;
	ldms_sps_msg_log_f log;
	char *send_log;
	FILE *send_log_f;
};

/* Create sps_target list details from argv. Clients defined may not
 * yet be connected at return. Strings in argv are interpreted as follows:
 * client=$xprt:$host:$port:$auth:$retry_seconds
 * timeout=$connect_seconds
 * stream=$stream_name
 * blocking=1
 * send_log=/path/file
 *
 * All entries are optional. Missing entries have defaults as follows:
 * client= may be repeated for multiple targets.
 *
 * xprt - sock
 * host - localhost
 * port - 411
 * auth - munge
 * retry - 600
 * blocking - 0 (non-blocking)
 * send_log - NULL (no send log file appended)
 *
 * Therefore:
 *
 * client=:::: or omitting client= entirely is equivalent to:
 * client=sock:localhost:411:munge:600
 *
 * Independent of specific clients are the following:
 * timeout=1
 * stream=slurm
 * blocking=1
 * send_log=/path/file
 *
 * Logging function pointer may be NULL to suppress all logging.
 * If not NULL, the function must be thread-safe.
 * If NULL, send_log is not used, otherwise info on messages to send is
 * logged at the sender side.
 *
 * There must be only one option of the format opt=val per argv element.
 */
struct ldms_sps *ldms_sps_create(int argc, const char *argv[], ldms_sps_msg_log_f debug_log);

/*
 * Create a single-client list, using C instead of argv opt=value syntax.
 * Stream, xprt, host, port, auth, retry, timeout as described for
 * ldms_sps_create.
 *
 * \param debug_log function pointer may be NULL to suppress all logging.
 * If not NULL, the function must be thread-safe.
 * \param send_log If not NULL, message send attempts (including content)
 * and acks are appended to the named file.
 * \param blocking If non-zero, connections and sends are blocking and
 * timeout is ignored. 0 is recommended.
 *
 */
struct ldms_sps *ldms_sps_create_1(const char *stream, const char *xprt,
	const char *host, int port, const char *auth, int retry, int timeout,
	ldms_sps_msg_log_f debug_log, int blocking, const char *send_log);

/* return number of clients configured, but not necessarily operating. */
int ldms_sps_target_count_get(struct ldms_sps *l);

struct ldms_sps_send_result {
	int publish_count; /* count of successful stream_publish events */
	int rc; /*< errno if send fails completely. */
};

#define LN_NULL_RESULT { 0,0 }

/* Publish json event to all clients in list, and attempt updating client connections
 * that are absent longer than the retry interval.
 * Messages can be delayed by the product of the timeout and the number of clients
 * every retry seconds. Messages may be dropped, as can be determined from the return value.
 * If any of the connections is defined as blocking, this function may block indefinitely.
 * \return tuple of the number of clients to which the message was successfully published
 * etc.
 */
struct ldms_sps_send_result ldms_sps_send_event(struct ldms_sps *ldms_sps, jbuf_t jb);

/* Publish buffer to all clients in list, and attempt updating client connections
 * that are absent longer than the retry interval.
 * Messages can be delayed by the product of the timeout and the number of clients
 * every retry seconds. Messages may be dropped, as can be determined from the return value.
 * \param buf must be nul-terminated, but may contain internal nul characters.
 * \return tuple of the number of clients to which the message was successfully published
 * etc.
 */
struct ldms_sps_send_result ldms_sps_send_string(struct ldms_sps *ldms_sps, size_t len, const char *str);

#if 0
/* Publish buffer to all clients in list, and attempt updating client connections
 * that are absent longer than the retry interval.
 * Messages can be delayed by the product of the timeout and the number of clients
 * every retry seconds. Messages may be dropped, as can be determined from the return value.
 * \param buf may contain anything; (This function is not yet supported by the
 * underlying ldmsd_stream type).
 * \return tuple of the number of clients to which the message was successfully published
 * etc.
 */
struct ldms_sps_send_result ldms_sps_send_buf(struct ldms_sps *ldms_sps, size_t buf_len, const char *buf);
#endif

/* destroy ldms_sps list, closing (with timeout) any connections.
 */
int ldms_sps_destroy(struct ldms_sps *ldms_sps);

#endif /* ldms_sps_h */
