#ifndef simple_lps_h
#define simple_lps_h
/*
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
#include <ovis_json/ovis_json.h>


/*
 * This is a simple wrapper for the ldms publishing service (slps) for sending
 * messages ('events' or 'buffers') to LDMSD daemons (external processes)
 * that may come and go during the life of the application client.
 * It should not be used for publishing from ldmsd plug-ins running in the
 * daemon, as these require no network connection.
 *
 * It is an abstraction layer which enables application writers to avoid
 * directly using anything except the ovis_json.h header and the required
 * libraries and without cloning boilerplate environment
 * and pthreaded event handling code.
 *
 * Included is an option for a single slps handle to target multiple
 * daemons for message injection slps_create_from_argv.
 *
 * It includes a workaround for event deliveries to the publisher callback
 * that can lead to use-after-free bugs after the stream has been closed.
 */

typedef void (*slps_msg_log_f)(int level, const char *fmt, ...);

struct slps_target;
LIST_HEAD(slps_target_list, slps_target);

/*
 * Initialize the slps library.
 */
void slps_init();

/*
 * Clean up slps library and close all ldms_xprt.
 */
void slps_finalize();

#define SLPS_CONN_DEFAULTS "sock", "127.0.0.1", 411, "ovis", 60, 1, NULL
#define SLPS_CONN_DEFAULTS2 "sock", "127.0.0.1", 412, "munge", 60, 1, NULL

#define SLPS_NONBLOCKING 0
#define SLPS_BLOCKING 1
#define SLPS_TRY_ONCE -1 /*< value of reconnect to suppress repeated attempts */
/*
 * Create a single-target publisher, using C arguments.
 * slps_create_from_env offers more runtime flexibility
 * \param stream The string tag for the stream published.
 * \param blocking 1 to wait for connection, 0 to run in non-blocking mode.
 * 	In non-blocking mode, messages attempted while the connection is absent
 * 	are dropped. In blocking mode, the application may be delayed.
 *
 * \param debug_log function pointer may be NULL to suppress all logging.
 * If not NULL, the function must be thread-safe.
 * \param xprt name of the LDMS transport type (sock, rdma, etc).
 * \param host name of the host (name or ipv4/ipv6 address).
 * \param auth name of the LDMS authentication method (munge, ovis, etc)
 * 	If 'ovis' is used, the environment variable LDMS_AUTH_FILE must
 * 	be set or one of the default locations for that file must be accessible.
 * \param reconnect Delay to wait before retrying a failed connection (seconds)
 * \param timeout Time to wait during a connection attempt before declaring
 * 	it failed.
 * \param send_log If not NULL, message send attempts (including content)
 *        and acks are appended to the named file.
 *
 * Example:
 * slps = slps_create("kokkos-sampler", SLPS_NONBLOCKING, log_f, SLPS_CONN_DEFAULTS);
 */
struct slps *slps_create(const char *stream, int blocking,
	slps_msg_log_f debug_log,
	const char *xprt, const char *host, int port, const char *auth,
	int reconnect, int timeout, const char *send_log);

/* Destroy slps handle, closing (with timeout) any connections.
 */
int slps_destroy(struct slps *slps);

/*
 * Create a connection using the environment parameters, or the
 * defaults supplied in the argument list for any variable not defined.
 * Where the supplied default value is 0 or NULL, and no environment value
 * is supplied, the fallback value will be taken from:
 * {sock, localhost, 411, munge, 60, 1, NULL}
 * Non-environment settings
 * \param stream tag of ldmsd stream to publish with, if not overridden.
 * \param blocking 0 if connection is non-blocking, nonzero for blocking.
 * \param debug_log function to log library internal messages.
 * Environment:
 * ${env_prefix}_LDMS_STREAM	the stream tag to override param 'stream'.
 *                              Normally set only for debugging.
 * ${env_prefix}_LDMS_XPRT	the transport type to use.
 * ${env_prefix}_LDMS_HOST	host name to connect.
 * ${env_prefix}_LDMS_PORT	host port to connect.
 * ${env_prefix}_LDMS_AUTH	authentication type for the connection.
 * ${env_prefix}_LDMS_RECONNECT	interval to retry failed connections.
 * ${env_prefix}_LDMS_TIMEOUT	wait time before declaring a failed connection.
 * ${env_prefix}_LDMS_SEND_LOG	optional file name for appending send attempts.
 * Example (using connection defaults):
 * s = slps_create_from_env("KOKKOS_", "kokkos-sampler", 0, log_f,
 *	SLPS_CONN_DEFAULTS2);
 */
struct slps *slps_create_from_env(const char *env_prefix,
	const char *stream,
	int blocking, slps_msg_log_f debug_log,
	const char *def_xprt, const char *def_host, int def_port,
	const char *def_auth, int def_reconnect, int def_timeout,
	const char *def_send_log);

/* return number of targets configured, but not necessarily operating. */
int slps_target_count_get(struct slps *l);

struct slps_send_result {
	int publish_count; /* count of successful stream_publish events */
	int rc; /*< errno if send fails completely. */
};

#define LN_NULL_RESULT { 0,0 }

/* Publish json event to all targets in list, and attempt updating target
 * connections that are absent longer than the reconnect interval.
 * Messages can be delayed by the product of the timeout and the number of
 * targets every reconnect seconds. Messages may be dropped, as can be
 * determined from the return value.
 * If any of the connections is defined as blocking, this function may block
 * indefinitely.
 * \return struct value of the number of targets to which the message was
 * successfully published to and an errno value if all failed.
 * This call may be made on the same slps object by multiple threads.
 */
struct slps_send_result slps_send_event(struct slps *slps, jbuf_t jb);

/* Publish buffer to all targets in list, and attempt updating target
 * connections that is absent longer than the reconnect interval.
 * Messages can be delayed by the product of the timeout and the number of
 * targets every reconnect seconds. Messages may be dropped, as can be
 * determined from the return value.
 * \param buf must be nul-terminated, but may contain internal nul characters.
 * \return struct value of the number of targets to which the message was
 * successfully published to and an errno value if all failed.
 * This call may be made on the same slps object by multiple threads.
 */
struct slps_send_result slps_send_string(struct slps *slps, size_t len, const char *str);

#if 0
/* Publish buffer to all targets in list, and attempt updating target connections
 * that are absent longer than the reconnect interval.
 * Messages can be delayed by the product of the timeout and the number of
 * targets every reconnect seconds. Messages may be dropped, as can be
 * determined from the return value.
 * \param buf may contain anything; (This function is not yet supported by the
 * underlying ldmsd_stream type). It is not required to be nul-terminated.
 * \return tuple of the number of targets to which the message was successfully published
 * etc.
 */
struct slps_send_result slps_send_buf(struct slps *slps, size_t buf_len, const char *buf);
#endif

/* Create a multiple publish target configuration using
 * details from argv. Clients defined may not
 * yet be connected on return unless blocking is specified.
 * Strings in argv are interpreted as follows:
 * timeout=$connect_seconds
 * stream=$stream_name
 * blocking=1
 * send_log=/path/file
 * delivery=$max_publication_per_message (0 --> all targets)
 * target=$xprt:$port:$auth:$reconnect_seconds:$host
 *
 * There must be only one option of the format opt=val per argv element.
 *
 * All entries are optional. Missing entries have defaults as follows:
 * target= may be repeated for multiple targets.
 * For repeated elements other than target, the last value seen
 * applies to all targets and others are ignored.
 *
 * xprt - sock
 * port - 411
 * auth - munge
 * reconnect - 600
 * host - localhost
 * blocking - 0 (non-blocking)
 * send_log - NULL (no send log file appended)
 * delivery - 1 (send to first connected target)
 *
 * Therefore:
 *
 * target=:::: or omitting target= entirely is equivalent to:
 * target=sock:411:munge:600:localhost
 *
 * Independent of specific targets are the following:
 * timeout=1
 * stream=slurm
 * blocking=1
 * send_log=/path/file
 * delivery=0
 * If send_log is not defined in argv, a message log is not produced.
 * Otherwise, info on messages is logged at the sender side to the
 * named file.
 *
 * \param debug_log may be NULL to suppress syslog-style logging.
 * If not NULL, the function must be thread-safe.
 */
struct slps *slps_create_from_argv(int argc, const char *argv[], slps_msg_log_f debug_log);
#endif /* simple_lps_h */
