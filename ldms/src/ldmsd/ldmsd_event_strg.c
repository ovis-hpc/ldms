/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2025 Open Grid Computing, Inc. All rights reserved.
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

/*
 * Event-based storage worker thread pool.
 *
 * This module decouples storage operations from the data collection path by
 * processing storage on dedicated worker threads. When a metric set is updated,
 * the IO thread creates a snapshot and queues a storage event,
 * then immediately returns. Workers process storage events from their queues
 * independently.
 *
 * Design rationale:
 *
 * Asynchronous storage: Storage operations (especially to high-latency backends
 * like network storage or slow disks) can take significant time. Performing
 * storage synchronously in the update callback blocks the IO thread and delays
 * data collection. Moving storage to worker threads eliminates this bottleneck.
 *
 * Storage Worker pool: Multiple workers allow parallel storage operations
 * to different backends and better utilize multi-core systems. Workers can
 * overlap I/O operations, improving throughput when dealing with high-latency
 * storage. This also separates ldmsd configuration between producer connections
 * and storages.
 *
 * Snapshots: The live metric set continues to be updated by producers while
 * storage operations are in progress. Snapshots preserve the current state of
 * the metric set at update time, ensuring data integrity. Without snapshots,
 * storage might capture partially-updated or inconsistent data.
 *
 * Backpressure: When storage is slower than data collection, unbounded queuing
 * leads to memory exhaustion. Queue depth limits with blocking provide
 * backpressure that prevents this - when all workers are at full capacity, the IO
 * thread blocks until capacity becomes available. This trades some latency for
 * system stability and bounded memory usage.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

#include "ovis_event/ovis_event.h"
#include "ovis_log/ovis_log.h"
#include "ovis_ref/ref.h"

#include "ldms.h"
#include "ldmsd.h"

extern ovis_log_t store_log;

/*
 * Storage event context passed to worker threads.
 *
 * Contains all information needed to perform a storage operation asynchronously:
 * - snapshot: Point-in-time copy of the metric set (preserves data integrity)
 * - strgp: Storage policy defining how and where to store the data
 * - prd_set: Producer set reference (tracks data source for statistics)
 * - row_list: For decomposed storage, the rows extracted from the snapshot
 * - type: Whether to use legacy store() API or decomposed commit() API
 *
 * Timestamps enable latency breakdown by stage:
 * - start_ts: When update callback began (before decomposition)
 * - post_ts: When event was queued to worker
 * Combined with worker dequeue time and completion time, these allow us to
 * identify which stage is the bottleneck (prep, wait, queue, or commit).
 */
struct store_event_ctxt {
	ldms_set_t snapshot; /* Set Snapshot */
	ldmsd_strgp_ref_t strgp_ref;
	ldmsd_row_list_t row_list;
	int row_count;
	struct timespec start_ts;
	struct timespec post_ts;
	enum {
		STORE_T_LEGACY = 1,
		STORE_T_DECOMP = 2
	} type;
	struct strg_worker *w;
};

struct strg_worker {
	ovis_scheduler_t worker;
	pthread_t thr;
	int q_depth;
};

/*
 * Global storage worker pool.
 *
 * Each worker has its own queue (scheduler) to allow parallel processing.
 * Per-worker queue depth tracking (rather than global) enables better load
 * distribution - we can route events to less-loaded workers and check
 * individual worker capacity without lock contention.
 */
struct strg_worker_pool {
	unsigned int num_workers;
	int max_q_depth; /* unlimited if negative */
	struct strg_worker *workers;
} ldmsd_strg_worker_pool;

struct store_event_ctxt *store_event_ctxt_new(ldmsd_strgp_ref_t strgp_ref, ldms_set_t snapshot,
                                              ldmsd_prdcr_set_t prd_set,
                                              ldmsd_row_list_t row_list, int row_count,
                                              struct timespec start)
{
	struct store_event_ctxt *ctxt;

	ctxt = malloc(sizeof(*ctxt));
	if (!ctxt)
		return NULL;

	/*
	 * Take references for async processing. These are released in
	 * store_event_ctxt_free() after the worker completes the storage operation.
	 */
	ldms_set_snapshot_get(snapshot, "store_event_ctxt_new");
	ctxt->snapshot = snapshot;
	ldmsd_strgp_get(strgp_ref->strgp, "store_event_ctxt_new");
	ctxt->strgp_ref = strgp_ref;
	if (strgp_ref->strgp->decomp) {
		ctxt->type = STORE_T_DECOMP;
		ctxt->row_list = row_list;
		ctxt->row_count = row_count;
	} else {
		ctxt->type = STORE_T_LEGACY;
		ctxt->row_count = 0;
		ctxt->row_list = NULL;
	}
	ctxt->start_ts = start;
	return ctxt;
}

void store_event_ctxt_free(struct store_event_ctxt *ctxt)
{
	ovis_log(store_log, OVIS_LDEBUG, "store_ctxt_delete(%p, %p)\n", ctxt, ctxt->snapshot);
	if (ctxt->row_list) {
		ctxt->strgp_ref->strgp->decomp->release_rows(ctxt->strgp_ref->strgp, ctxt->row_list);
	}
	ldmsd_strgp_put(ctxt->strgp_ref->strgp, "store_event_ctxt_new");
	ldms_set_snapshot_put(ctxt->snapshot, "store_event_ctxt_new");
	free(ctxt);
}

/*
 * Wait condition for threads blocked waiting for an available worker.
 *
 * When all workers are at capacity, threads block on a condition variable
 * and are queued in FIFO order. When a worker completes an event, it hands
 * its slot directly to the first waiting thread. This maintains fairness
 * (FIFO).
 *
 * Locking model
 * -------------
 * `strgw_wait_list_lock` is the single mutex for the whole handoff
 * protocol. It protects the wait list AND serialises the capacity
 * decision, and it is the mutex each waiter blocks on. That is what makes
 * the following two operations atomic with respect to each other:
 *
 *   acquire:  observe "no capacity"  ->  enqueue self
 *   release:  observe "no waiter"    ->  give the slot back
 *
 * If those are not atomic, a release that lands between an acquirer's
 * failed capacity check and its enqueue is lost: the releaser sees an
 * empty wait list and returns the slot silently, the acquirer then parks
 * on an empty condvar, and -- if no further store events are posted --
 * nothing ever signals it again. That is the deadlock this protocol had.
 *
 * Slot ownership
 * --------------
 * w->q_depth counts *granted* slots: events in flight plus slots already
 * promised to a blocked waiter. strg_worker_release() therefore either
 *   (a) transfers its slot to the first waiter, leaving q_depth unchanged
 *       -- the grant is a real reservation nobody can steal from the
 *       waiter, so a woken waiter never has to re-try and never fails; or
 *   (b) gives the slot back with q_depth--.
 * Both branches run under strgw_wait_list_lock.
 */
struct strg_worker_wait_cond {
	pthread_cond_t cond;
	struct strg_worker *w;		/* set by the releaser; NULL = keep waiting */
	TAILQ_ENTRY(strg_worker_wait_cond) entry;
};

TAILQ_HEAD(strg_worker_wait_list, strg_worker_wait_cond);
/* FIFO wait queue - ensures fairness (first blocked thread gets first available worker) */
struct strg_worker_wait_list strgw_wait_list = TAILQ_HEAD_INITIALIZER(strgw_wait_list);
pthread_mutex_t strgw_wait_list_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * Block until a worker slot is handed to us.
 *
 * MUST be called with strgw_wait_list_lock held; returns with it released.
 *
 * The wait cond lives on the caller's stack: it is removed from the list
 * by the releaser before this function can return, and the releaser only
 * touches it while holding strgw_wait_list_lock, which this function does
 * not drop until it is done with it.
 *
 * Always succeeds -- the slot is reserved for us by the releaser, so
 * there is nothing left to fail at.
 */
static struct strg_worker *__strg_worker_wait_for_available(void)
{
	struct strg_worker_wait_cond wait_cond;
	struct strg_worker *w;

	pthread_cond_init(&wait_cond.cond, NULL);
	wait_cond.w = NULL;

	ovis_log(store_log, OVIS_LDEBUG, "store_event_block.enqueue(%p).\n", &wait_cond);
	TAILQ_INSERT_TAIL(&strgw_wait_list, &wait_cond, entry);

	while (wait_cond.w == NULL)
		pthread_cond_wait(&wait_cond.cond, &strgw_wait_list_lock);

	w = wait_cond.w;
	pthread_mutex_unlock(&strgw_wait_list_lock);

	ovis_log(store_log, OVIS_LDEBUG, "store_event_block.cond_wake(%p) "
					 "with worker %p.\n", &wait_cond, w);
	pthread_cond_destroy(&wait_cond.cond);
	return w;
}

/* Helper function to try acquiring a worker with capacity */
static struct strg_worker *strg_worker_try_acquire(struct strg_worker *w)
{
	__atomic_fetch_add(&w->q_depth, 1, __ATOMIC_SEQ_CST);

	/* Unlimited queue depth */
	if (ldmsd_strg_worker_pool.max_q_depth < 0) {
		ovis_log(store_log, OVIS_LDEBUG, "Acquire store worker %p having q_depth %d.\n",
		         w, w->q_depth);
		return w;
	}

	/*
	 * Check if this worker is at the max capacity.
	 *
	 * Multiple threads may race and get this worker, so
	 * the worker's queue depth may exceed max_strg_q_depth, but
	 * the worker's queue depth will be bounded by max_strg_q_depth +
	 * number of rails * rail size.
	 */
	if (w->q_depth <= ldmsd_strg_worker_pool.max_q_depth) {
		ovis_log(store_log, OVIS_LDEBUG, "Acquire store worker %p having q_depth %d.\n",
		         w, w->q_depth);
		return w;
	}

	/* Worker is full, undo the increment */
	__atomic_fetch_sub(&w->q_depth, 1, __ATOMIC_SEQ_CST);
	return NULL;
}

/*
 * Acquire an available storage worker. Blocks if all workers are at capacity.
 *
 * Selection strategy:
 *   1. Under strgw_wait_list_lock, and only if nobody is already queued
 *      (FIFO fairness), try round-robin across all workers.
 *   2. Otherwise enqueue on the wait list -- still under the same lock
 *      hold that observed "no capacity" -- and block until a releaser
 *      hands us its slot.
 *
 * Returns: worker pointer. Never returns NULL.
 */
struct strg_worker *strg_worker_acquire(void)
{
	static int worker_idx = 0;
	struct strg_worker *w = NULL;
	int attempts;
	int current_idx;

	errno = 0;

	pthread_mutex_lock(&strgw_wait_list_lock);

	/*
	 * Only jump the queue when no one is waiting; otherwise we would
	 * starve the threads already parked below.
	 */
	if (TAILQ_EMPTY(&strgw_wait_list)) {
		for (attempts = 0; attempts < ldmsd_strg_worker_pool.num_workers; attempts++) {
			/* Round-robin selection for load distribution */
			current_idx = __atomic_fetch_add(&worker_idx, 1, __ATOMIC_SEQ_CST) %
			              ldmsd_strg_worker_pool.num_workers;
			w = strg_worker_try_acquire(&ldmsd_strg_worker_pool.workers[current_idx]);
			if (w) {
				pthread_mutex_unlock(&strgw_wait_list_lock);
				return w;
			}
		}
	}

	/*
	 * All workers are at full capacity (or others are queued ahead of
	 * us). Enqueue without dropping the lock: any release that has
	 * already decremented is visible to the loop above, and any release
	 * that has not yet taken this lock will find us on the list.
	 */
	return __strg_worker_wait_for_available();
}

/*
 * Release a worker after processing an event.
 *
 * If threads are waiting, hand this worker's slot to the first one
 * (FIFO) without decrementing q_depth -- the slot is transferred, not
 * freed, so the waiter cannot lose it to a racing acquirer. Otherwise
 * return the slot to the pool.
 */
void strg_worker_release(struct strg_worker *w)
{
	struct strg_worker_wait_cond *store_wait_ev;

	pthread_mutex_lock(&strgw_wait_list_lock);
	if (!TAILQ_EMPTY(&strgw_wait_list)) {
		store_wait_ev = TAILQ_FIRST(&strgw_wait_list);
		ovis_log(store_log, OVIS_LDEBUG, "store_event.dequeue(%p).\n", store_wait_ev);
		TAILQ_REMOVE(&strgw_wait_list, store_wait_ev, entry);
		/* Transfer the slot: q_depth is intentionally NOT decremented. */
		store_wait_ev->w = w;
		pthread_cond_signal(&store_wait_ev->cond);
	} else {
		__atomic_fetch_sub(&w->q_depth, 1, __ATOMIC_SEQ_CST);
	}
	pthread_mutex_unlock(&strgw_wait_list_lock);
}

/*
 * Worker thread callback - processes a storage event.
 *
 * Calls the appropriate storage plugin API:
 *   - Legacy storage: store() API with the snapshot
 *   - Decomposed storage: commit() API with the row_list
 *
 * Updates statistics and releases all references when complete.
 */

void storage_worker_actor(struct ovis_event_s *ev)
{
	int rc;
	struct timespec start, end;
	struct store_event_ctxt *event_ctxt = ev->param.ctxt;
	ldmsd_strgp_ref_t strgp_ref = event_ctxt->strgp_ref;
	ldmsd_strgp_t strgp = strgp_ref->strgp;
	ldms_set_t set = event_ctxt->snapshot;
	ldmsd_row_list_t row_list = event_ctxt->row_list;
	int row_count = event_ctxt->row_count;
	struct ldmsd_stat *queue_stat = &(strgp_ref->store_stages_stat.queue_stat);
	struct ldmsd_stat *commit_stat = &(strgp_ref->store_stages_stat.commit_stat);
	double store_time_us;

	clock_gettime(CLOCK_REALTIME, &start); /* start is the end timestamp of the event being queued */
	queue_stat->end = start;
	ldmsd_stat_update(queue_stat, &event_ctxt->post_ts, &queue_stat->end);

	if (event_ctxt->type == STORE_T_LEGACY) {
		strgp->store->api->store((ldmsd_plug_handle_t)strgp->store,
					 strgp->store_handle, set,
					 strgp->metric_arry, strgp->metric_count);
		if (strgp->flush_interval.tv_sec || strgp->flush_interval.tv_nsec) {
			struct timespec expiry;
			struct timespec now;
			ldmsd_timespec_add(&strgp->last_flush, &strgp->flush_interval, &expiry);
			clock_gettime(CLOCK_REALTIME, &now);
			if (ldmsd_timespec_cmp(&now, &expiry) >= 0) {
				clock_gettime(CLOCK_REALTIME, &strgp->last_flush);
				strgp->store->api->flush((ldmsd_plug_handle_t)strgp->store,
							  strgp->store_handle);
			}
		}
	} else {
		rc = strgp->store->api->commit((ldmsd_plug_handle_t)strgp->store,
						strgp, set, row_list, row_count);
		if (rc) {
			ovis_log(store_log, OVIS_LERROR, "strgp row commit error: %d\n", rc);
		}
		strgp->decomp->release_rows(strgp, row_list);
	}
	strg_worker_release(event_ctxt->w);

	clock_gettime(CLOCK_REALTIME, &end);
	strgp_ref->store_stat.end = commit_stat->end = end;
	ldmsd_stat_update(&strgp_ref->store_stages_stat.commit_stat,
				&start, &end);
	/* event_ctxt->start_ts is the very begining in strgp_update_fn() */
	ldmsd_stat_update(&strgp_ref->store_stat,
				&event_ctxt->start_ts, &end);
	store_time_us = ldmsd_ts_diff_usec(&end, &event_ctxt->start_ts);
	ovis_histogram_update(&strgp->hist_store_time, store_time_us);

	store_event_ctxt_free(event_ctxt);
	free(row_list);
	free(ev);
}

/*
 * Queue a storage event to a worker thread.
 *
 * Acquires an available worker (blocking if all workers are saturated) and
 * queues the event to that worker's scheduler. The worker processes events
 * in the order they were posted (per-worker FIFO ordering).
 *
 * Returns: 0 on success, errno on failure
 */
int store_event_post(struct store_event_ctxt *ctxt)
{
	int rc;
	struct ovis_event_s *ev;
	struct strg_worker *w;
	ldmsd_strgp_ref_t strgp_ref = ctxt->strgp_ref;
	struct timespec wait_start, wait_end;
	struct ldmsd_stat *worker_wait_stat = &(strgp_ref->store_stages_stat.worker_wait_stat);

	ovis_log(store_log, OVIS_LDEBUG, "store_post(%p, %p).\n", ctxt, ctxt->snapshot);

	clock_gettime(CLOCK_REALTIME, &wait_start);
	/* strg_worker_acquire() blocks when all storage workers are at full capacity. */
	w = strg_worker_acquire();
	ctxt->w = w;
	clock_gettime(CLOCK_REALTIME, &wait_end);
	ldmsd_stat_update(worker_wait_stat, &wait_start, &wait_end);

	if (!w) {
		/*
		 * Defensive only -- strg_worker_acquire() no longer fails.
		 * Report a real error code: errno is cleared on entry to
		 * strg_worker_acquire() and clobbered by the calls above, so
		 * reading it here used to yield 0, i.e. "success", and the
		 * caller silently leaked ctxt (snapshot ref + strgp ref +
		 * row_list) and dropped the store.
		 */
		ovis_log(store_log, OVIS_LERROR,
			 "Failed to acquire a storage worker.\n");
		return EBUSY;
	}

	ev = calloc(1, sizeof(*ev));
	if (!ev) {
		strg_worker_release(w);
		ovis_log(NULL, OVIS_LCRIT, "Memory allocation failure.\n");
		return ENOMEM;
	}
	OVIS_EVENT_INIT(ev);

	ev->param.cb_fn = storage_worker_actor;
	ev->param.ctxt = (void *)ctxt;
	ev->param.type = OVIS_EVENT_ONESHOT;

	/*
	 * We use w->worker queue to queue the store context.
	 * ovis_event guarantees that it processes the posted event chrononically.
	 * Therefore, the order of events posted on a worker is preserved.
	 */
	ovis_log(store_log, OVIS_LDEBUG, "Post store_event %p to worker %p\n", ev, w);

	clock_gettime(CLOCK_REALTIME, &ctxt->post_ts);
	ldmsd_stat_update(&strgp_ref->store_stages_stat.io_thread_stat,
				  &ctxt->start_ts, &ctxt->post_ts);

	/* Enqueue the store event to the storage worker */
	rc = ovis_scheduler_event_add(w->worker, ev);
	if (rc) {
		/*
		 * The event was never queued, so storage_worker_actor() will
		 * never run for it and will never release this worker. Give
		 * the slot back here -- otherwise q_depth leaks by one, and
		 * with a small max_q_depth (e.g. 1) a single failure wedges
		 * the pool permanently.
		 */
		strg_worker_release(w);
		free(ev);
		ovis_log(store_log, OVIS_LERROR, "Failed to post a store event " \
						 "on a storage worker. Error %d\n", rc);
	}
	return rc;
}

void *strg_worker_proc(void *v)
{
	struct strg_worker *w = v;
	ovis_scheduler_loop(w->worker, 0);
	return NULL;
}

/*
 * Initialize the storage worker pool.
 *
 * Creates and starts the specified number of worker threads. Each worker
 * has its own scheduler (event queue) and processes storage events
 * independently.
 *
 * Must be called during LDMSD initialization before any storage events are
 * posted (triggered by storage_threads configuration command).
 *
 * Returns: 0 on success, errno on failure
 */
int strg_worker_init(struct strg_worker *w, const char *name)
{
	w->worker = ovis_scheduler_new();
	if (!w->worker) {
		ovis_log(NULL, OVIS_LCRIT, "Memory allocation failure.\n");
		return ENOMEM;
	}
	ovis_scheduler_name_set(w->worker, name);
	pthread_create(&w->thr, NULL, strg_worker_proc, w);
	pthread_setname_np(w->thr, name);
	w->q_depth = 0;
	return 0;
}

int strg_pool_init(unsigned int num_workers, int max_q_depth)
{
	int rc;
	short i;
	char wname[21];

	ldmsd_strg_worker_pool.num_workers = num_workers;
	ldmsd_strg_worker_pool.max_q_depth = max_q_depth;

	ldmsd_strg_worker_pool.workers = malloc(num_workers * sizeof(struct strg_worker));
	if (!ldmsd_strg_worker_pool.workers) {
		ovis_log(NULL, OVIS_LCRIT, "Memory allocation failure.\n");
		return ENOMEM;
	}

	for (i = 0; i < ldmsd_strg_worker_pool.num_workers; i++) {
		snprintf(wname, sizeof(wname), "str:wrk:%hu", (uint16_t)i);
		rc = strg_worker_init(&ldmsd_strg_worker_pool.workers[i], wname);
		if (rc) {
			return rc;
		}
	}

	return 0;
}

extern void ldmsd_worker_thrstat_free(struct ldmsd_worker_thrstat_result *res);
struct ldmsd_worker_thrstat_result *ldmsd_storage_worker_thrstat_get(int interval)
{
	int i;
	struct ldmsd_worker_thrstat_result *res;
	struct timespec now;
	int num_workers = ldmsd_strg_worker_pool.num_workers;
	struct strg_worker *w;

	clock_gettime(CLOCK_REALTIME, &now);
	res = malloc(sizeof(*res) +
			(num_workers * sizeof(struct ovis_scheduler_thrstat *)));
	if (!res) {
		ovis_log(NULL, OVIS_LCRIT, "Memory allocation failure.\n");
		return NULL;
	}
	res->count = 0;
	for (i = 0; i < num_workers; i++) {
		w = &ldmsd_strg_worker_pool.workers[i];
		res->entries[i] = ovis_scheduler_thrstats_get(w->worker, &now, interval);
		if (!res->entries[i]) {
			ovis_log(NULL, OVIS_LCRIT, "Memory allocation failure.\n");
			goto err;
		}
		res->count++;
	}
	return res;
err:
	ldmsd_worker_thrstat_free(res);
	return NULL;
}