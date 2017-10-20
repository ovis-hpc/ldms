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

#define _GNU_SOURCE

#include "notification.h"
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <ctype.h>
#include <stdio.h>
#include <limits.h>
#include <pthread.h>

#include "olog.h"
#include <sys/queue.h>
#include <assert.h>

struct ovis_notification_entry {
        STAILQ_ENTRY(ovis_notification_entry) entries;
        time_t time_in;
        uint32_t last; /**< time last activity ended */
        ssize_t buf_sz;
        ssize_t buf_done;
        char buf[]; /**< \n, not nul, terminated */
};

struct ovis_notification {
	TAILQ_ENTRY(ovis_notification) entries;
        char *name;
        int fd;
        pthread_mutex_t lock;
        bool fifo;
        int quit; // 0 ok, 1 try again, 2 die
        uint32_t max_age;
        unsigned int max_entries;
        unsigned int num_entries; /**< current tailq size */
        unsigned int retry;
        ovis_log_fn_t log;
        mode_t mode;
        STAILQ_HEAD(stailhead, ovis_notification_entry) head;
};

static pthread_mutex_t output_lock = PTHREAD_MUTEX_INITIALIZER;
TAILQ_HEAD(tailhead, ovis_notification) onhead;
// struct tailhead *onheadp;

enum output_loop_status {
	on_unstarted,
	on_running,
} onstate = on_unstarted;

time_t get_sec()
{
	struct timespec tp;
	int err = clock_gettime(CLOCK_MONOTONIC, &tp);
	if (err)
		return 0;
	return tp.tv_sec;
}


// age out old stuff
// try dumping everything queued on this output.
// return errno if output is unrecoverable in some way.
static int process_notification(struct ovis_notification *onp)
{
	assert(onp != NULL);
	int rc = 0;
	if (onp->quit) {
		if (onp->log)
			onp->log(OL_DEBUG,"empty Q %s\n", onp->name);
		return rc;
	}
	pthread_mutex_lock(&onp->lock);
	time_t now = get_sec();
	if (!onp->num_entries) {
		if (onp->log)
			onp->log(OL_DEBUG,"empty Q %s\n", onp->name);
		goto out;
	}
	struct ovis_notification_entry *item, *tmp_item;
	for (item = STAILQ_FIRST(&(onp->head)); item != NULL && !onp->quit; item = tmp_item) {
		tmp_item = STAILQ_NEXT(item, entries);
		/* age out */
		if ( (onp->max_age > 0 && (now - item->time_in) > onp->max_age) ||
		     (onp->num_entries > onp->max_entries && onp->max_entries > 0) ) {
			STAILQ_REMOVE(&(onp->head), item, ovis_notification_entry, entries);
			onp->num_entries--;
			if (onp->log)
				onp->log(OL_INFO,"Dropping output from queue %s: %s",
						onp->name, item->buf);
			free(item);
			continue;
		}
		// attempt item
		bool remove = false;
		if ( (now - item->last) >= onp->retry) {
			item->last = now;
			ssize_t bytes;
			bytes = write(onp->fd, item->buf + item->buf_done,
				item->buf_sz - item->buf_done);
			if (bytes < 0) {
				switch (errno) {
				case EAGAIN:
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
				case EWOULDBLOCK:
#endif
					continue;
				case EPIPE:
					onp->quit = 1;
					break;
				default:
					onp->quit = 2;
					remove = true;
					rc = errno;
					break;
				}
			} else {
				item->buf_done += bytes;
			}

			if (item->buf_done >= item->buf_sz) {
				remove = true;
				if (onp->log)
					onp->log(OL_DEBUG,"Completed Q %s MSG %s\n", onp->name, item->buf);
			}
		}
		if (remove) {
			STAILQ_REMOVE(&(onp->head), item, ovis_notification_entry, entries);
			free(item);
		}
	}
	if (onp->quit == 1) {
		if (onp->log)
			onp->log(OL_INFO, "Waiting on disconnected output %s\n", onp->name);
		onp->quit = 0;
	}
	if (onp->quit == 2) {
		if (onp->log)
			onp->log(OL_INFO, "Error on output %s: %s\n", onp->name, strerror(rc));
	}

 out:
	pthread_mutex_unlock(&onp->lock);
	return rc;
}

static void handle_queue(void *x)
{
	(void)x;
	struct ovis_notification *item, *tmp_item;
	pthread_mutex_lock(&output_lock);
	for (item = TAILQ_FIRST(&onhead); item != NULL; item = tmp_item) {
		tmp_item = TAILQ_NEXT(item, entries);
		int err = process_notification(item);
		if (err) {
			item->log(OL_ERROR,"Unrecoverable output error. Closing %s\n", item->name);
		}
	}
	pthread_mutex_unlock(&output_lock);
}

#define RETRY_MAX 86400
#define RETRY_DEFAULT 10
static int qretry = -1;
void ovis_notification_retry_set(unsigned seconds)
{
	if (seconds > RETRY_MAX)
		seconds = RETRY_MAX;
	qretry = (int)seconds;
}

static void init_qretry()
{
	if (qretry != -1)
		return;
	const char *s = getenv("OVIS_NOTIFICATION_RETRY");
	char *end;
	if (!s) {
		qretry = 10;
		return;
	}
	unsigned long r = strtoul(s, &end, 10);
	unsigned t = UINT_MAX & r;
	if (end != s)
		ovis_notification_retry_set(t);
}


static pthread_t iothread;
/* this thread must be cancelled, as from finalize. */
static void* ioThreadInit(void* m)
{
	pthread_cleanup_push(handle_queue, NULL);
        while (1) {
                int tsleep = 1;
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
                handle_queue(NULL);
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
                sleep(tsleep);
        }
	pthread_cleanup_pop(1);
        return NULL;
}

/** Start the single thread that processes notification queues.
 * A thread per output object does not scale well in a
 * per-job object use case.
 * All calls to ovis_notification functions are ignored until this is done.
 */
static int ovis_notification_init()
{
	int rc = 0;
	pthread_mutex_lock(&output_lock);
	if (onstate != on_unstarted)
		goto out;
	init_qretry();
	TAILQ_INIT(&onhead);
	rc = pthread_create(&iothread, NULL, ioThreadInit, NULL);
	if (!rc)
		onstate = on_running;
out:
	pthread_mutex_unlock(&output_lock);
	return rc;
}

/** Tell the thread that processes output queues to quit. */
static int ovis_notification_finalize()
{
	if (onstate == on_running) {
		pthread_cancel(iothread);
		return pthread_join(iothread, NULL);
	}
	return 0;
}

// in onp context, checks err, logs args if log defined, and follows out.
// if out is empty, execution continues onward.
// the args may use e to obtain the value of errno.
#define ONPERRNO(err, args, out) \
	if (err) { \
		if (onp->log) { \
			int e = errno; \
			onp->log args; \
		} \
		out; \
	}
#define ONPMSG(err, args) \
	if (err) { \
		if (onp->log) { \
			onp->log args; \
		} \
	}

struct ovis_notification * ovis_notification_open(const char *name, uint32_t write_timeout, unsigned max_queue_size, unsigned retry_seconds, ovis_notification_log_fn log, mode_t perm, bool fifo )
{
	if (!name) {
		errno = EINVAL;
		return NULL;
	}
	if (ovis_notification_init() != 0) {
		errno = EAGAIN;
		return NULL;
	}
	struct ovis_notification *onp = calloc(1,sizeof(*onp));
	if (!onp)
		goto err;
	onp->fd = -1;
	onp->name = strdup(name);
	if (!onp->name) {
		goto err;
	}
	onp->fifo = fifo;
	onp->max_age = write_timeout;
	onp->max_entries = max_queue_size;
	onp->retry = retry_seconds;
	onp->log = (ovis_log_fn_t)log;
	onp->mode = perm;
	pthread_mutex_init(&onp->lock, NULL);
	int flags = O_CLOEXEC|O_APPEND|O_NONBLOCK;
	int err = 0;
	if (fifo) {
		// open existing fifo or create and open for write.
		onp->fd = open(name, flags | O_RDWR);
		if (onp->fd == -1) {
			ONPMSG(true,
			   	(OL_INFO,"%s: creating fifo %s\n", __FUNCTION__, name));
			err = mkfifo(name, perm);
			ONPERRNO(err,
			   	(OL_ERROR,"%s: unable to make fifo %s\n", __FUNCTION__, name, strerror(e)),
			       goto err);
			onp->fd = open(name, flags|O_CREAT|O_RDWR, perm);
			ONPERRNO((onp->fd == -1),
			   	(OL_ERROR,"%s: unable to open fifo %s\n", __FUNCTION__, name, strerror(e)),
					goto err);

		}
		// check is fifo as expected.
		struct stat buf;
		err = fstat(onp->fd, &buf);
		ONPERRNO(err,
			(OL_ERROR,"%s: unable to stat %s\n", __FUNCTION__, name, strerror(e)),
			goto err);
		if (!(buf.st_mode & S_IFIFO)) {
			int e = errno;
			log(OL_WARN,"%s: %s is not fifo as requested.\n", __FUNCTION__, name, strerror(e));
		}
	} else {
		// just open file
		flags |= O_WRONLY|O_CREAT;
		onp->fd = open(name, flags, perm );
		ONPERRNO(onp->fd == -1,
			(OL_ERROR,"%s: unable to open file %s: %s\n", __FUNCTION__, name, strerror(e)),
			goto err);
	}

	if (onp->fd == -1) {
		// should never happen.
		goto err;
	}

	STAILQ_INIT(&(onp->head));

	pthread_mutex_lock(&output_lock);
	TAILQ_INSERT_HEAD(&onhead, onp, entries);
	pthread_mutex_unlock(&output_lock);
	if (onp->log)
		onp->log(OL_DEBUG,"open Q: %s\n", onp->name);
	return onp;
 err:
	if (onp) {
		if (onp->fd != -1) {
			close(onp->fd);
		}
		pthread_mutex_destroy(&onp->lock);
		free(onp->name);
		free(onp);
	}
	if (log)
		log(OL_DEBUG,"fail open Q: %s\n", name);
	return NULL;
}

void ovis_notification_close(struct ovis_notification *onp)
{
	int remaining, removed;
	remaining = removed = 0;
	pthread_mutex_lock(&output_lock);
	if (onstate == on_unstarted)
		goto out;
	if (!onp)
		goto out;
	onp->log(OL_DEBUG,"close Q: %s\n", onp->name);

	// fixme: one last clear onp's queue here?

	pthread_mutex_destroy(&onp->lock);
	struct ovis_notification *item, *tmp_item;
	for (item = TAILQ_FIRST(&onhead); item != NULL; item = tmp_item) {
		tmp_item = TAILQ_NEXT(item, entries);
		if (item == onp) {
			TAILQ_REMOVE(&onhead, item, entries);
			removed = 1;
		} else {
			remaining++;
		}
		if (removed && remaining)
			break;
	}
	close(onp->fd);
	onp->fd = -1;
	onp->quit = 2;
	free(onp->name);
	free(onp);
out:
	pthread_mutex_unlock(&output_lock);
	if (!remaining) {
		ovis_notification_finalize();
		onstate = on_unstarted;
	}
}

int ovis_notification_add(struct ovis_notification *onp, const char *msg)
{
	if (!onp || !msg || !msg[0])
		return EINVAL;
	if (onp->quit) {
		if (onp->log)
			onp->log(OL_INFO,"Ignoring output_add on closed queue %s\n", onp->name);
		return ESHUTDOWN;
	}
	if (onp->log)
		onp->log(OL_DEBUG,"add Q: %s MSG %s\n", onp->name, msg);

	size_t len = strlen(msg)+2;
	struct ovis_notification_entry *e = malloc(sizeof(*e) + len);
	if (!e) {
		if (onp->log)
			onp->log(OL_DEBUG,"Failed(ENOMEM): add Q: %s MSG %s\n", onp->name, msg);
		return ENOMEM;
	}
	e->buf_sz = len;
	e->buf_done = 0;
	strncpy(e->buf, msg, len-2);
	e->buf[len-2] = '\n';
	e->buf[len-1] = '\0';
	e->time_in = get_sec();
	e->last = 0;
	pthread_mutex_lock(&onp->lock);
	STAILQ_INSERT_TAIL(&(onp->head), e, entries);
	onp->num_entries++;
	pthread_mutex_unlock(&onp->lock);
	return process_notification(onp);
}
