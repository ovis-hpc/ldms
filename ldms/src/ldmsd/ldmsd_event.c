#define _GNU_SOURCE

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>

#include "ovis_event/ovis_event.h"
#include "ovis_log/ovis_log.h"
#include "ovis_ref/ref.h"

#include "ldms.h"
#include "ldmsd.h"

extern ovis_log_t store_log;

struct store_event_ctxt {
	ldms_set_t snapshot; /* Set Snapshot */
	ldmsd_strgp_t strgp;
	ldmsd_prdcr_set_t prd_set;
	ldmsd_row_list_t row_list;
	int row_count;
	enum {
		STORE_T_LEGACY = 1,
		STORE_T_DECOMP = 2
	} type;
};

struct strg_worker {
	ovis_scheduler_t worker;
	pthread_t thr;
	int q_depth;
};

struct strg_worker_pool {
	unsigned int num_workers;
	int max_q_depth; /* unlimited if negative */
	struct strg_worker *workers;
} ldmsd_strg_worker_pool;

struct store_event_ctxt *store_event_ctxt_new(ldmsd_strgp_t strgp, ldms_set_t snapshot,
				              ldmsd_prdcr_set_t prd_set,
					      ldmsd_row_list_t row_list, int row_count)
{
	struct store_event_ctxt *ctxt;

	ctxt = malloc(sizeof(*ctxt));
	if (!ctxt)
		return NULL;

	ldms_set_snapshot_get(snapshot, "store_event_ctxt_new");
	ctxt->snapshot = snapshot;
	ldmsd_prdcr_set_ref_get(prd_set);
	ctxt->prd_set = prd_set;
	ctxt->strgp = ldmsd_strgp_get(strgp, "store_event_ctxt_new");
	if (strgp->decomp) {
		ctxt->type = STORE_T_DECOMP;
		ctxt->row_list = row_list;
		ctxt->row_count = row_count;
	} else {
		ctxt->type = STORE_T_LEGACY;
		ctxt->row_count = 0;
		ctxt->row_list = NULL;
	}
	return ctxt;
}

void store_event_ctxt_free(struct store_event_ctxt *ctxt)
{
	ovis_log(store_log, OVIS_LDEBUG, "store_ctxt_delete(%p, %p)\n", ctxt, ctxt->snapshot);
	if (ctxt->row_list) {
		ctxt->strgp->decomp->release_rows(ctxt->strgp, ctxt->row_list);
	}
	ldmsd_strgp_put(ctxt->strgp, "store_event_ctxt_new");
	ldmsd_prdcr_set_ref_put(ctxt->prd_set);
	ldms_set_snapshot_put(ctxt->snapshot, "store_event_ctxt_new");
	free(ctxt);
}

struct strg_worker_wait_cond {
	pthread_cond_t cond;
	pthread_mutex_t lock;
	int status;
	struct strg_worker *w;
	TAILQ_ENTRY(strg_worker_wait_cond) entry;
};

#define STRG_WORKER_WAIT	0
#define STRG_WORKER_SIGNAL	1

TAILQ_HEAD(strg_worker_wait_list, strg_worker_wait_cond);
struct strg_worker_wait_list strgw_wait_list = TAILQ_HEAD_INITIALIZER(strgw_wait_list);
pthread_mutex_t strgw_wait_list_lock = PTHREAD_MUTEX_INITIALIZER;

struct strg_worker *strg_worker_get(void)
{
	static int worker_idx = 0;
	struct strg_worker *w = NULL;
	int attemps = 0;
	int current_idx;
	struct strg_worker_wait_cond *store_wait_ev = NULL;
	errno = 0;

	pthread_mutex_lock(&strgw_wait_list_lock);
	if (!TAILQ_EMPTY(&strgw_wait_list)) {
		goto block;
	}
	pthread_mutex_unlock(&strgw_wait_list_lock);

	attemps = 0;
	while (attemps < ldmsd_strg_worker_pool.num_workers) {
		current_idx = __atomic_fetch_add(&worker_idx, 1,
				__ATOMIC_SEQ_CST) % ldmsd_strg_worker_pool.num_workers;
		w = &ldmsd_strg_worker_pool.workers[current_idx];

retry:
		__atomic_fetch_add(&w->q_depth, 1, __ATOMIC_SEQ_CST);
		if (ldmsd_strg_worker_pool.max_q_depth < 0)
			goto acquire;

		/* Check if this worker is at the max capacity. */
		/*
		 * Multple threads may race and get this worker, so
		 * the worker's queue depth may exceed max_strg_q_depth, but
		 * the worker's queue depth will be bounded by max_strg_q_depth + number of rails * rail size.
		 *
		 * We can modify the code to guarantee that w->q_depth won't exceed max_strg_q_depth
		 * by using __atomic_compare_exchange() and retry until no other threads
		 * acquire the worker since getting the w->q_depth value.
		 *
		 * However, this reduces the performace by repeatedly retry.
		 * The current approach was favored over the later because
		 * the possible w->q_depth value is bounded.
		 */
		if (w->q_depth <= ldmsd_strg_worker_pool.max_q_depth) {
			goto acquire;
		} else {
			__atomic_fetch_sub(&w->q_depth, 1, __ATOMIC_SEQ_CST);
		}
		attemps++;
	}
	/* All workers are at full capabity. */
	pthread_mutex_lock(&strgw_wait_list_lock);

block:
	store_wait_ev = malloc(sizeof(*store_wait_ev));
	if (!store_wait_ev) {
		ovis_log(store_log, OVIS_LCRIT, "Memory allocation failure.\n");
		errno = ENOMEM;
		return NULL;
	}
	pthread_mutex_init(&store_wait_ev->lock, NULL);
	pthread_cond_init(&store_wait_ev->cond, NULL);
	store_wait_ev->w = NULL;

	ovis_log(store_log, OVIS_LINFO, "store_event_block.enqueue(%p).\n", store_wait_ev);

	TAILQ_INSERT_TAIL(&strgw_wait_list, store_wait_ev, entry);
	pthread_mutex_unlock(&strgw_wait_list_lock);

	pthread_mutex_lock(&store_wait_ev->lock);
	while (store_wait_ev->w == NULL) {
		/* store_wait_ev->w is assigned in strg_worker_put() when a worker is available. */
		pthread_cond_wait(&store_wait_ev->cond, &store_wait_ev->lock);
		ovis_log(store_log, OVIS_LINFO, "store_event_block.cond_wake(%p) " \
			                        "with worker %p.\n", store_wait_ev, w);
	}
	w = store_wait_ev->w;
	pthread_mutex_unlock(&store_wait_ev->lock);
	pthread_mutex_destroy(&store_wait_ev->lock);
	pthread_cond_destroy(&store_wait_ev->cond);
	free(store_wait_ev);
	goto retry;

acquire:
	ovis_log(store_log, OVIS_LDEBUG, "Acquire store worker %p having q_depth %d.\n", w, w->q_depth);
	return w;
}

void strg_worker_put(struct strg_worker *w)
{
	struct strg_worker_wait_cond *store_wait_ev;

	__atomic_fetch_sub(&w->q_depth, 1, __ATOMIC_SEQ_CST);

	pthread_mutex_lock(&strgw_wait_list_lock);
	if (!TAILQ_EMPTY(&strgw_wait_list)) {
		store_wait_ev = TAILQ_FIRST(&strgw_wait_list);
		ovis_log(store_log, OVIS_LINFO, "store_event.dequeue(%p).\n", store_wait_ev);
		TAILQ_REMOVE(&strgw_wait_list, store_wait_ev, entry);
		pthread_mutex_lock(&store_wait_ev->lock);
		store_wait_ev->w = w;
		pthread_cond_signal(&store_wait_ev->cond);
		pthread_mutex_unlock(&store_wait_ev->lock);
	}
	pthread_mutex_unlock(&strgw_wait_list_lock);
}

void storage_worker_actor(struct ovis_event_s *ev)
{
	int rc;
	struct store_event_ctxt *event_ctxt = ev->param.ctxt;
	ldmsd_strgp_t strgp = event_ctxt->strgp;
	ldms_set_t set = event_ctxt->snapshot;
	ldmsd_row_list_t row_list = event_ctxt->row_list;
	int row_count = event_ctxt->row_count;

	if (event_ctxt->type == STORE_T_LEGACY) {
		strgp->store->api->store(strgp->store_handle, set,
				          strgp->metric_arry,
				          strgp->metric_count);
		if (strgp->flush_interval.tv_sec || strgp->flush_interval.tv_nsec) {
			struct timespec expiry;
			struct timespec now;
			ldmsd_timespec_add(&strgp->last_flush, &strgp->flush_interval, &expiry);
			clock_gettime(CLOCK_REALTIME, &now);
			if (ldmsd_timespec_cmp(&now, &expiry) >= 0) {
				clock_gettime(CLOCK_REALTIME, &strgp->last_flush);
				strgp->store->api->flush(strgp->store_handle);
		}
	}
	} else {
		rc = strgp->store->api->commit(strgp, set, row_list, row_count);
		if (rc) {
			ovis_log(store_log, OVIS_LERROR, "strgp row commit error: %d\n", rc);
		}
		strgp->decomp->release_rows(strgp, row_list);
	}

	store_event_ctxt_free(event_ctxt);
}

int store_event_post(struct store_event_ctxt *ctxt)
{
	int rc;
	struct ovis_event_s *ev;
	struct strg_worker *w;

	ovis_log(store_log, OVIS_LDEBUG, "store_post(%p, %p).\n", ctxt, ctxt->snapshot);

	/* strg_worker_get() block when all storage workers are at full capacity. */
	w = strg_worker_get();
	if (!w) {
		rc = errno;
		return rc;
	}

	ev = calloc(1, sizeof(*ev));
	if (!ev) {
		strg_worker_put(w);
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
	rc = ovis_scheduler_event_add(w->worker, ev);
	if (rc) {
		ovis_log(store_log, OVIS_LERROR, "Failed to post a store event " \
						 "on a storage worker. Error %d\n", rc);
	}
	return rc;
}

void *strg_worker_proc(void *v)
{
	struct strg_worker *w = v;
	ovis_scheduler_loop(w->worker, 0);
	strg_worker_put(w);
	return NULL;
}

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
	int i;
	char wname[20];

	ldmsd_strg_worker_pool.num_workers = num_workers;
	ldmsd_strg_worker_pool.max_q_depth = max_q_depth;

	ldmsd_strg_worker_pool.workers = malloc(num_workers * sizeof(struct strg_worker));
	if (!ldmsd_strg_worker_pool.workers) {
		ovis_log(NULL, OVIS_LCRIT, "Memory allocation failure.\n");
		return ENOMEM;
	}

	for (i = 0; i < ldmsd_strg_worker_pool.num_workers; i++) {
		snprintf(wname, 20, "storage_w_%d", i);
		rc = strg_worker_init(&ldmsd_strg_worker_pool.workers[i], wname);
		if (rc) {
			return rc;
		}
	}
	return 0;
}