/*
 * Copyright (c) 2016-2017 Sandia Corporation. All rights reserved.
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

#ifndef ovis_notification_h_seen
#define ovis_notification_h_seen

#include <sys/stat.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/** a structure to manage (with prune policy options)
 * messages to be sent to a specific notification destination
 * that may be a fifo or file.
 * Objects of this type are not thread-safe: the caller should
 * ensure the object is used from only one thread at a time.
 */
struct ovis_notification;

/* alias for ldmsd log function type until it moves from ldms/core/ldmsd.h to lib. */
typedef void (*ovis_notification_log_fn)(int level, const char *fmt, ...);

/** Set the time between retries for processing the message queues.
 * \param seconds length of time the io loop sleeps after attempting
 * to complete all waiting notifications.
 * The default is 10 seconds unless it is overridden by the environment variable
 * OVIS_NOTIFICATION_RETRY. 
 *
 * When notification is being used to signal a pipeline starting with 'tail', 
 * a 1 second retry may be appropriate.
 */
extern void ovis_notification_retry_set(unsigned seconds);

/**
 * Create an output queue that does not block the user much.
 * Assumes a higher scope has set handler of SIGPIPE to SIG_IGN to
 * allow nonblocking fifos to work. Calling this function is thread safe; the pointer
 * returned is for use in a single threaded fashion.
 * \param name pathname to use.
 * \param write_timeout messages in the queue longer than this will be
 * automatically dropped.
 * \param max_queue_size oldest tasks will be dropped to keep queue size smaller than max_queue_size.
 * \param retry_seconds
 * \param log output file for queue handling messages, or NULL for silence.
 * \param perm permissions mode to use for opening named output if it does not exist.
 * \param fifo create name as fifo, or if it exists warn when it is not a fifo.
 * \return queue handle or NULL if bad input or problem found on using them.
 * See errno for hint.
 */
extern struct ovis_notification * ovis_notification_open(const char *name, uint32_t write_timeout, unsigned max_queue_size, unsigned retry_seconds, ovis_notification_log_fn log, mode_t perm, bool fifo );

/** Add message to the queue.
 * This function does not support calling from multiple threads.
 * \return 0 if successful, EINVAL if bad arguments detected, ESHUTDOWN if queue
 * has disconnected. Consult log for details of disconnect. If an output
 * reports shutdown, it should be passed to ovis_notification_close().
 * \param msg null-terminated string written to queue followed by a \n. String content
 * up to application.
 */
extern int ovis_notification_add(struct ovis_notification *onp, const char *msg);

/** Stop the queue given after making one more flush attempt on any unprocessed outputs.
 * If the result of a previous add on the same queue was a shutdown, this just
 * deallocates memory without the flush attempt.
 * Calling this function is thread-safe.
 */
extern void ovis_notification_close(struct ovis_notification *onp);

#endif /* ovis_notification_h_seen */
