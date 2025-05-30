#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "ovis_log/ovis_log.h"

#include "ovis_thrstats.h"

static inline int64_t __timespec_diff_us(struct timespec *start, struct timespec *end)
{
	int64_t secs_ns;
	int64_t nsecs;
	secs_ns = (end->tv_sec - start->tv_sec) * 1000000000;
	nsecs = end->tv_nsec - start->tv_nsec;
	return (secs_ns + nsecs) / 1000;
}

void ovis_thrstats_reset(ovis_thrstats_t stats, struct timespec *now) {
	pthread_rwlock_wrlock(&stats->rwlock);
	stats->start = stats->wait_start = stats->wait_end = *now;
	stats->wait_tot = stats->proc_tot = 0;
	stats->proc_count = stats->wait_count = 0;
	memset(stats->idle_bucket, 0,
	       sizeof(*stats->idle_bucket) * stats->bucket_cnt);
	memset(stats->active_bucket, 0,
	       sizeof(*stats->active_bucket) * stats->bucket_cnt);
	stats->curr_idx = 0;
	pthread_rwlock_unlock(&stats->rwlock);
	if (stats->app_reset_fn) {
		stats->app_reset_fn(stats->app_ctxt, now);
	}
}

void ovis_thrstats_thread_id_set(ovis_thrstats_t stats)
{
	stats->thread_id = (uint64_t)pthread_self();
	stats->tid = syscall(SYS_gettid);
}

void ovis_thrstats_thread_id_get(ovis_thrstats_t stats, uint64_t *_thread_id, pid_t *_tid)
{
	if (!_thread_id)
		*_thread_id = stats->thread_id;
	if (!_tid)
		*_tid = stats->tid;
}

int ovis_thrstats_init(ovis_thrstats_t stats, const char *name) {
	int rc;
	struct timespec now;
	memset(stats, 0, sizeof(*stats));
	if (name) {
		rc = ovis_thrstats_name_set(stats, name);
		if (rc)
			return rc;
	}

	stats->bucket_cnt = OVIS_THRSTATS_NUM_BUCKET;
	stats->bucket_sz = OVIS_THRSTATS_BUCKET_SZ;
	pthread_rwlock_init(&stats->rwlock, NULL);
	clock_gettime(CLOCK_REALTIME, &now);
	ovis_thrstats_reset(stats, &now);
	return 0;
}

void ovis_thrstats_cleanup(ovis_thrstats_t stats)
{
	if (stats->name)
		free(stats->name);
	pthread_rwlock_destroy(&stats->rwlock);
}

int ovis_thrstats_name_set(ovis_thrstats_t stats, const char *name)
{
	if (!name)
		return 0;
	if (stats->name)
		free(stats->name);
	stats->name = strdup(name);
	if (!stats->name)
		return ENOMEM;
	return 0;
}

/* The caller must hold the stats->rwlock */
static void __buckets_update(struct ovis_thrstats *stats, uint64_t elapsed) {
	uint64_t space;
	uint64_t remaining;
	unsigned short *buckets;

	int is_idle = (stats->waiting)
			  ? 0
			  : 1; /* _stat->waiting is 0 when epoll wakes up, so
				  the elapsed time is the idle duration. */

	space = stats->bucket_sz - (stats->active_bucket[stats->curr_idx] +
				    stats->idle_bucket[stats->curr_idx]);
	if (elapsed / stats->bucket_sz >= stats->bucket_cnt) {
		/*
		 * `elapsed` is larger than the total duration the buckets can
		 * be filled. Prevent filling each bucket at a time but filling
		 * all buckets except the last bucket at full capacity.
		 */
		if (is_idle) {
			buckets = stats->idle_bucket;
		} else {
			buckets = stats->active_bucket;
		}
		/*
		 * Fill all buckets completely (stat->bucket_sz), except the
		 * last bucket
		 */
		memset(buckets, stats->bucket_sz,
		       sizeof(*buckets) * (stats->bucket_cnt - 1));
		stats->idle_bucket[stats->bucket_cnt - 1] =
		    stats->active_bucket[stats->bucket_cnt - 1] = 0;
		/* Fill the last bucket with remainder of the remaining divided
		 * by the bucket size */
		buckets[stats->bucket_cnt - 1] = (elapsed % stats->bucket_sz);
		/* Move the current idx to the last bucket */
		stats->curr_idx = stats->bucket_cnt - 1;
	} else {
		/* Filling each bucket one by one */
		remaining = elapsed;
		while (remaining) {
			if (remaining < space) {
				if (is_idle)
					stats->idle_bucket[stats->curr_idx] +=
					    remaining;
				else
					stats->active_bucket[stats->curr_idx] +=
					    remaining;
				remaining = 0;
			} else {
				/* Fill the current bucket */
				if (is_idle)
					stats->idle_bucket[stats->curr_idx] +=
					    space;
				else
					stats->active_bucket[stats->curr_idx] +=
					    space;
				remaining -= space;

				/* Move to the next bucket */
				stats->curr_idx =
				    (stats->curr_idx + 1) % stats->bucket_cnt;
				stats->idle_bucket[stats->curr_idx] = 0;
				stats->active_bucket[stats->curr_idx] = 0;
				space = stats->bucket_sz;
			}
		}
	}
}

void ovis_thrstats_wait_start(ovis_thrstats_t stats) {
	struct timespec now;
	uint64_t elapsed;

	clock_gettime(CLOCK_REALTIME, &now);
	pthread_rwlock_wrlock(&stats->rwlock);
	stats->wait_start = now;
	stats->waiting = 1;
	elapsed = __timespec_diff_us(&stats->wait_end, &now);
	stats->proc_tot += elapsed;
	stats->proc_count += 1;
	__buckets_update(stats, elapsed);
	pthread_rwlock_unlock(&stats->rwlock);
}

void ovis_thrstats_wait_end(ovis_thrstats_t stats) {
	struct timespec now;
	uint64_t elapsed;

	clock_gettime(CLOCK_REALTIME, &now);
	pthread_rwlock_wrlock(&stats->rwlock);
	stats->wait_end = now;
	elapsed = __timespec_diff_us(&stats->wait_start, &now);
	stats->wait_tot += elapsed;
	stats->wait_count += 1;
	stats->waiting = 0;
	__buckets_update(stats, elapsed);
	pthread_rwlock_unlock(&stats->rwlock);
}

int ovis_thrstats_app_ctxt_set(ovis_thrstats_t stats, void *ctxt, ovis_thrstats_app_reset_fn reset_fn)
{
	if (stats->app_ctxt)
		return EBUSY;
	stats->app_ctxt = ctxt;
	stats->app_reset_fn = reset_fn;
	return 0;
}

void *ovis_thrstats_app_ctxt_get(ovis_thrstats_t stats)
{
	return stats->app_ctxt;
}

static void __buckets_get_usage(int is_idling, uint64_t interval_us,
			       uint64_t elapsed, uint64_t curr_idx,
			       uint64_t bucket_sz, uint64_t bucket_cnt,
			       unsigned short *idle_buckets,
			       unsigned short *active_buckets,
			       struct ovis_thrstats_result *res) {
	int needed_buckets;
	uint64_t sum;
	int i, idx;
	uint64_t remaining, space;

	/* Determine how many buckets we can examine.
	 * The current bucket isn't complete or filled yet, so
	 * the highest possible number of buckets is stat->num_buckets - 1.
	 */
	if (interval_us == 0) {
		needed_buckets = bucket_cnt - 1;
	} else {
		needed_buckets = interval_us / bucket_sz;
		if (needed_buckets > bucket_cnt - 1)
			needed_buckets = bucket_cnt - 1;
	}

	res->idle_us = res->active_us = res->interval_us = 0;

	/*
	 * Fill the buckets with the elapsed time from the last wait_start and
	 * wait_end time
	 */
	if (elapsed / bucket_sz >= needed_buckets) {
		if (is_idling) {
			res->idle_us = bucket_sz * needed_buckets;
			res->active_us = 0;
		} else {
			res->idle_us = 0;
			res->active_us = bucket_sz * needed_buckets;
		}
		res->interval_us = bucket_sz * needed_buckets;
		goto out;
	} else {
		/* Take account of the empty space in the current buckets */
		space = (bucket_sz -
			 (idle_buckets[curr_idx] + active_buckets[curr_idx]));
		if (elapsed > space) {
			remaining = elapsed - space;
			if (is_idling) {
				res->idle_us += space;
				/*
				 * Make sure that it is a multiple of
				 * stat->bucket_sz to ensure that the reported
				 * result is of a fixed interval
				 */
				res->idle_us += (remaining / bucket_sz) * bucket_sz;
			} else {
				res->active_us += space;
				res->active_us +=  (remaining / bucket_sz) * bucket_sz;
			}
			res->interval_us = (remaining / bucket_sz) * bucket_sz;
			needed_buckets -= (remaining / bucket_sz);

			/* Take account of the current bucket */
			res->idle_us += idle_buckets[curr_idx];
			res->active_us += active_buckets[curr_idx];
			res->interval_us += bucket_sz;
			needed_buckets -= 1; /* -1 for the current bucket */
			assert((int)needed_buckets >= 0);
		}

		idx = ((curr_idx == 0) ? (bucket_cnt - 1) : (curr_idx - 1));
		for (i = 0; i < needed_buckets; i++) {
			sum = idle_buckets[idx] + active_buckets[idx];
			/* This is either a complete or an empty bucket. */
			assert((sum != bucket_sz) || (sum != 0));
			/* Stop at empty bucket */
			if (sum == 0)
				goto out;

			res->interval_us += bucket_sz;
			res->idle_us += idle_buckets[idx];
			res->active_us += active_buckets[idx];

			/* Move to the previous bucket */
			idx = ((idx == 0) ? (bucket_cnt - 1) : idx - 1);
		}
	}
out:
	return;
}

struct ovis_thrstats_result *ovis_thrstats_result_get(ovis_thrstats_t stats,
						      uint64_t interval_s,
						      struct ovis_thrstats_result *res) {
	struct timespec now;
	uint64_t curr_idx;
	struct timespec last_ts;
	unsigned short idle_buckets[OVIS_THRSTATS_NUM_BUCKET];
	unsigned short active_buckets[OVIS_THRSTATS_NUM_BUCKET];
	int is_idling;
	uint64_t interval_us;
	uint64_t elapsed;

	if (!res) {
		res = calloc(1, sizeof(*res));
		if (!res) {
			ovis_log(NULL, OVIS_LCRIT,
				 "Memory allocation failure.\n");
			return NULL;
		}
	}

	if (interval_s == 0) {
		interval_us = stats->bucket_cnt * stats->bucket_sz;
	} else {
		interval_us = interval_s * 1000000;
	}

	clock_gettime(CLOCK_REALTIME, &now);

	if (stats->name)
		res->name = strdup(stats->name);
	else
		res->name = NULL;
	res->thread_id = stats->thread_id;
	res->tid = stats->tid;
	res->app_ctxt = stats->app_ctxt;

	pthread_rwlock_rdlock(&stats->rwlock);
	res->idle_tot = stats->wait_tot;
	res->active_tot = stats->proc_tot;
	res->waiting = stats->waiting;
	res->start = stats->start;
	res->wait_start = stats->wait_start;
	res->wait_end = stats->wait_end;

	/* Start with the last completed bucket */
	curr_idx = stats->curr_idx;
	memcpy(idle_buckets, stats->idle_bucket,
	       sizeof(short) * OVIS_THRSTATS_NUM_BUCKET);
	memcpy(active_buckets, stats->active_bucket,
	       sizeof(short) * OVIS_THRSTATS_NUM_BUCKET);
	if (stats->waiting) {
		is_idling = 1;
		last_ts = stats->wait_start;
	} else {
		is_idling = 0;
		last_ts = stats->wait_end;
	}
	pthread_rwlock_unlock(&stats->rwlock);
	res->dur_tot = res->idle_tot + res->active_tot;

	elapsed = __timespec_diff_us(&last_ts, &now);
	__buckets_get_usage(is_idling, interval_us, elapsed, curr_idx,
		            stats->bucket_sz, stats->bucket_cnt,
			    idle_buckets, active_buckets, res);
	if (res->interval_us < interval_us) {
		/* Not enough data points */
		res->interval_us = 0;
		res->idle_us = 0;
		res->active_us = 0;
	}
	return res;
}

void ovis_thrstats_result_cleanup(struct ovis_thrstats_result *res)
{
	if (!res)
		return;
	free(res->name);
}