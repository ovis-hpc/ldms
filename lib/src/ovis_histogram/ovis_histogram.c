#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/errno.h>

#include "ovis_json/ovis_json.h"

#include "ovis_histogram.h"

/*
 * Small constant added before taking log() so that exact-zero or
 * near-zero duration samples (a store/update that completes in well
 * under a microsecond) don't produce log(0) or a negative argument.
 * Expressed in the same unit as the measured values (microseconds);
 * 0.001 us = 1 ns is negligible relative to any real measured duration.
 */
#define HISTOGRAM_LOG_EPSILON      0.001

int ovis_histogram_init(struct ovis_histogram *h, int n_bins, int n_warmup,
                          enum ovis_histogram_scale scale)
{
	if (!h)
		return EINVAL;

	memset(h, 0, sizeof(*h));

	pthread_mutex_init(&h->lock, NULL);
	h->bins_ready = 0;
	h->scale = (scale != 0)?scale:OVIS_HISTOGRAM_SCALE_LINEAR;

	h->n_bins   = (n_bins   > 0)?n_bins:OVIS_HISTOGRAM_DEFAULT_BINS;
	h->n_warmup = (n_warmup > 0)?n_warmup:OVIS_HISTOGRAM_DEFAULT_WARMUP;

	h->warmup_buf = calloc(h->n_warmup, sizeof(double));
	if (!h->warmup_buf)
		return ENOMEM;

	h->boundaries = calloc(h->n_bins + 1, sizeof(double));
	if (!h->boundaries) {
		free(h->warmup_buf);
		h->warmup_buf = NULL;
		return ENOMEM;
	}

	/* underflow + n_bins + overflow */
	h->counts = calloc(h->n_bins + 2, sizeof(uint64_t));
	if (!h->counts) {
		free(h->warmup_buf);
		free(h->boundaries);
		h->warmup_buf = NULL;
		h->boundaries = NULL;
		return ENOMEM;
	}

	return 0;
}

void ovis_histogram_destroy(struct ovis_histogram *h)
{
	if (!h)
		return;
	pthread_mutex_destroy(&h->lock);
	free(h->warmup_buf);
	free(h->boundaries);
	free(h->counts);
	h->warmup_buf = NULL;
	h->boundaries = NULL;
	h->counts = NULL;
}

/* qsort comparator for doubles */
static int __cmp_double(const void *a, const void *b)
{
	double da = *(const double *)a;
	double db = *(const double *)b;
	return (da > db) - (da < db);
}

static void histogram_bin_value(struct ovis_histogram *h, double value);

/*
 * Fix bin boundaries from warmup samples using Tukey's fences on the
 * interquartile range: [Q1 - 1.5*IQR, Q3 + 1.5*IQR], which captures
 * ~99.3% of a normal distribution and is robust against a few outliers
 * appearing in the warmup window.
 *
 * Scale modes:
 *   LDMSD_HISTOGRAM_SCALE_LINEAR -- Q1/Q3/IQR and fences are computed
 *     directly on the warmup sample values. Bin boundaries are spaced
 *     evenly (equal width) across [min, max]. The lower fence is clamped
 *     to 0 since measured durations cannot be negative.
 *
 *   LDMSD_HISTOGRAM_SCALE_LOG -- warmup samples are first transformed to
 *     log(x + LDMSD_HISTOGRAM_LOG_EPSILON) space. Q1/Q3/IQR and fences are
 *     computed entirely in that log space (quantiles are invariant under
 *     monotonic transforms, but IQR and the fence width are not, so the
 *     transform must happen before computing IQR, not after). The
 *     resulting log-space fences are then exponentiated back, which
 *     guarantees both bounds are positive without needing a clamp. Bin
 *     boundaries are spaced evenly in log space (i.e. geometrically /
 *     equal-ratio in linear space), giving every order of magnitude in
 *     the range comparable resolution -- useful for right-skewed,
 *     outlier-prone distributions where most values cluster near a small
 *     value and a few are much larger (e.g. store_time during startup).
 *
 * MUST be called while holding h->lock. As its last act before binning
 * the warmup samples retroactively, publishes bins_ready=1 with
 * __ATOMIC_RELEASE so that other threads observing bins_ready==1 (via
 * __ATOMIC_ACQUIRE) are guaranteed to see a fully written boundaries[]
 * array without needing the lock. Does not free warmup_buf -- it is kept
 * allocated for the histogram's lifetime so that
 * ldmsd_histogram_recalibrate() can reuse it without reallocating.
 */
static void histogram_fix_bins(struct ovis_histogram *h)
{
	double min, max, q1, q3, iqr, width;
	double log_min, log_max, log_width;
	int n, lo_mid, hi_mid, m, i;
	int is_log = (h->scale == OVIS_HISTOGRAM_SCALE_LOG);

	n = h->n_warmup;

	if (is_log) {
		/* Transform in place: warmup_buf now holds log(x + eps).
		 * Safe to overwrite -- the linear-space values are not
		 * needed again after this point (the retroactive binning
		 * below re-reads warmup_buf, but histogram_bin_value()
		 * compares against boundaries[] which are computed and
		 * stored back in linear space, so retroactive binning
		 * needs the ORIGINAL linear values, not the transformed
		 * ones -- see note below before the retroactive-binning loop). */
		for (i = 0; i < n; i++)
			h->warmup_buf[i] = log(h->warmup_buf[i] + HISTOGRAM_LOG_EPSILON);
	}

	qsort(h->warmup_buf, n, sizeof(double), __cmp_double);

	/* Median of lower half for Q1, median of upper half for Q3.
	 * Use the average of the two middle elements for even-sized halves.
	 * This operates on whatever is currently in warmup_buf -- either
	 * the original linear values, or the log-transformed values above. */
	lo_mid = (n - 1) / 2;  /* last index of lower half */
	hi_mid = n / 2;        /* first index of upper half */

	if (lo_mid % 2 == 0) {
		q1 = h->warmup_buf[lo_mid / 2];
	} else {
		q1 = (h->warmup_buf[lo_mid / 2] + h->warmup_buf[lo_mid / 2 + 1]) / 2.0;
	}

	if ((n - hi_mid) % 2 == 1) {
		q3 = h->warmup_buf[hi_mid + (n - hi_mid) / 2];
	} else {
		m = hi_mid + (n - hi_mid) / 2;
		q3 = (h->warmup_buf[m - 1] + h->warmup_buf[m]) / 2.0;
	}

	iqr = q3 - q1;

	if (iqr == 0.0) {
		/* All warmup samples identical (or identical after log
		 * transform) -- fall back to a small fixed-width range
		 * centered on Q1, in whichever space we're working in. */
		min = q1 - 0.5;
		max = q3 + 0.5;
	} else {
		/* Tukey's fences: covers ~99.3% of a normal distribution */
		min = q1 - 1.5 * iqr;
		max = q3 + 1.5 * iqr;
	}

	if (is_log) {
		/* min/max are currently log-space fence values. Exponentiate
		 * back to linear space -- exp() of any real number is always
		 * positive, so no clamp is needed here. */
		log_min = min;
		log_max = max;
		min = exp(log_min) - HISTOGRAM_LOG_EPSILON;
		max = exp(log_max) - HISTOGRAM_LOG_EPSILON;
		/* Guard against floating-point edge cases pushing min
		 * slightly below 0 after subtracting epsilon back out. */
		if (min < 0.0)
			min = 0.0;

		/* Bin boundaries spaced evenly in log space (geometric in
		 * linear space). Recompute log_min/log_max from the final
		 * (possibly clamped) min/max so boundaries[] is internally
		 * consistent with the actual min/max being used. */
		log_min = log(min + HISTOGRAM_LOG_EPSILON);
		log_max = log(max + HISTOGRAM_LOG_EPSILON);
		log_width = (log_max - log_min) / h->n_bins;
		for (i = 0; i <= h->n_bins; i++)
			h->boundaries[i] = exp(log_min + i * log_width) - HISTOGRAM_LOG_EPSILON;
	} else {
		/* Linear mode: measured durations cannot be negative. */
		if (min < 0.0)
			min = 0.0;

		width = (max - min) / h->n_bins;
		for (i = 0; i <= h->n_bins; i++)
			h->boundaries[i] = min + i * width;
	}

	for (i = 0; i < h->n_bins + 2; i++)
		h->counts[i] = 0;

	/* Publish: boundaries[] is fully written above this point. */
	__atomic_store_n(&h->bins_ready, 1, __ATOMIC_RELEASE);

	/* Retroactively bin the warmup samples themselves, so they are not
	 * silently dropped from the histogram. Safe to call histogram_bin_value
	 * here since boundaries[] is already fixed and bins_ready is already
	 * published -- concurrent updates from other threads via the lock-free
	 * fast path are safe since counts[] is only ever touched with atomic
	 * fetch-add, regardless of caller.
	 *
	 * NOTE: in log mode, warmup_buf currently holds the log-transformed
	 * values (we overwrote it in place above), but boundaries[] is in
	 * linear space. Binning log-transformed values against linear-space
	 * boundaries would be wrong. We must reverse the transform here. */
	for (i = 0; i < n; i++) {
		if (is_log)
			histogram_bin_value(h, exp(h->warmup_buf[i]) - HISTOGRAM_LOG_EPSILON);
		else
			histogram_bin_value(h, h->warmup_buf[i]);
	}
}

/*
 * Bin a single value into counts[] using boundaries[].
 * Only safe to call once bins_ready has been observed true.
 * Lock-free: counts[] is updated via __atomic_fetch_add.
 *
 * Works identically regardless of scale mode -- boundaries[] already
 * encodes the spacing (linear or log/geometric), so this function only
 * ever needs to compare against it.
 */
static void histogram_bin_value(struct ovis_histogram *h, double value)
{
	int lo, hi, mid;

	/* Underflow */
	if (value < h->boundaries[0]) {
		__atomic_fetch_add(&h->counts[0], 1, __ATOMIC_SEQ_CST);
		return;
	}

	/* Overflow */
	if (value >= h->boundaries[h->n_bins]) {
		__atomic_fetch_add(&h->counts[h->n_bins + 1], 1, __ATOMIC_SEQ_CST);
		return;
	}

	/* Binary search for the bin */
	lo = 0;
	hi = h->n_bins - 1;
	while (lo < hi) {
		mid = (lo + hi + 1) / 2;
		if (value < h->boundaries[mid])
			hi = mid - 1;
		else
			lo = mid;
	}
	__atomic_fetch_add(&h->counts[lo + 1], 1, __ATOMIC_SEQ_CST);
}

void ovis_histogram_update(struct ovis_histogram *h, double value)
{
	/* Fast path: bins already fixed. __ATOMIC_ACQUIRE pairs with the
	 * __ATOMIC_RELEASE in histogram_fix_bins(), guaranteeing boundaries[]
	 * is visible here. */
	if (__atomic_load_n(&h->bins_ready, __ATOMIC_ACQUIRE)) {
		histogram_bin_value(h, value);
		return;
	}

	/* Slow path: still warming up (or racing the transition). */
	pthread_mutex_lock(&h->lock);

	/* Re-check under the lock: another thread may have just completed
	 * the transition between our fast-path check and acquiring the lock. */
	if (h->bins_ready) {
		pthread_mutex_unlock(&h->lock);
		histogram_bin_value(h, value);
		return;
	}

	h->warmup_buf[h->warmup_count++] = value;
	if (h->warmup_count == h->n_warmup)
		histogram_fix_bins(h);  /* publishes bins_ready=1 internally */

	pthread_mutex_unlock(&h->lock);
	/* Warmup samples are not counted into bins directly here -- they are
	 * binned retroactively inside histogram_fix_bins() once boundaries
	 * are fixed. */
}

int ovis_histogram_recalibrate(struct ovis_histogram *h, int new_n_bins,
                                 int new_n_warmup, enum ovis_histogram_scale new_scale)
{
	double *new_warmup_buf = NULL;
	double *new_boundaries = NULL;
	uint64_t *new_counts = NULL;
	int want_n_bins, want_n_warmup;
	int i;

	pthread_mutex_lock(&h->lock);

	want_n_bins   = (new_n_bins   > 0) ? new_n_bins   : h->n_bins;
	want_n_warmup = (new_n_warmup > 0) ? new_n_warmup : h->n_warmup;

	/* Allocate everything needed up front. Nothing below this point
	 * touches h's live state until all allocations have succeeded, so
	 * a failure here leaves the histogram fully intact and usable. */

	if (want_n_warmup != h->n_warmup) {
		new_warmup_buf = calloc(want_n_warmup, sizeof(double));
		if (!new_warmup_buf)
			goto err_nomem;
	}

	if (want_n_bins != h->n_bins) {
		new_boundaries = calloc(want_n_bins + 1, sizeof(double));
		if (!new_boundaries)
			goto err_nomem;

		new_counts = calloc(want_n_bins + 2, sizeof(uint64_t));
		if (!new_counts)
			goto err_nomem;
	}

	/* All allocations succeeded (or were not needed). Now commit. */

	if (new_warmup_buf) {
		free(h->warmup_buf);
		h->warmup_buf = new_warmup_buf;
		h->n_warmup = want_n_warmup;
	}

	if (new_boundaries) {
		free(h->boundaries);
		h->boundaries = new_boundaries;
	}
	if (new_counts) {
		free(h->counts);
		h->counts = new_counts;
		h->n_bins = want_n_bins;
	} else {
		for (i = 0; i < h->n_bins + 2; i++)
			h->counts[i] = 0;
	}

	if (new_scale != 0)
		h->scale = new_scale;

	h->warmup_count = 0;

	/* Flip back into warmup state. histogram_bin_value() must not run
	 * again (via the lock-free fast path) until histogram_fix_bins()
	 * republishes bins_ready=1 with a freshly computed boundaries[]. */
	__atomic_store_n(&h->bins_ready, 0, __ATOMIC_RELEASE);

	pthread_mutex_unlock(&h->lock);
	return 0;

err_nomem:
	free(new_warmup_buf);
	free(new_boundaries);
	free(new_counts);
	pthread_mutex_unlock(&h->lock);
	return ENOMEM;
}

/*
 * Reset histogram counts. Boundaries are preserved. bins_ready is
 * unaffected -- a histogram that has finished warmup stays in the
 * "ready" state across resets.
 *
 * No external locking required. If bins are not yet ready (still in
 * warmup), this is a no-op since counts[] is not in use yet.
 */
void ovis_histogram_reset(struct ovis_histogram *h)
{
	int i;

	if (!__atomic_load_n(&h->bins_ready, __ATOMIC_ACQUIRE))
		return;

	for (i = 0; i < h->n_bins + 2; i++)
		__atomic_store_n(&h->counts[i], 0, __ATOMIC_SEQ_CST);
}

json_entity_t ovis_histogram2dict(json_doc_t jdoc, struct ovis_histogram *h)
{
	int i, rc;
	json_entity_t v, d, boundaries, bins;
	uint64_t c, underflow, overflow;
	const char *scale_str;
	int warmup_count_snapshot, n_warmup_snapshot;

	/* Still in warmup -- report progress instead of bins/boundaries.
	 * warmup_count and n_warmup are only otherwise touched under h->lock
	 * (by ldmsd_histogram_update()'s slow path, histogram_fix_bins(), and
	 * ldmsd_histogram_recalibrate()), so take the lock briefly here too
	 * to read a consistent pair of values. */
	if (!__atomic_load_n(&h->bins_ready, __ATOMIC_ACQUIRE)) {
		pthread_mutex_lock(&h->lock);
		warmup_count_snapshot = h->warmup_count;
		n_warmup_snapshot = h->n_warmup;
		pthread_mutex_unlock(&h->lock);

		/* Re-check after acquiring the snapshot: warmup may have
		 * completed between our first lock-free check above and
		 * actually taking the lock. If so, fall through to the
		 * ready-state path below using fresh data instead of
		 * reporting a stale "still warming up" snapshot. */
		if (__atomic_load_n(&h->bins_ready, __ATOMIC_ACQUIRE))
			goto build_ready_dict;

		d = json_dict_build(jdoc,
				"warmup_in_progress", JSON_BOOL_VALUE, 1,
				"warmup_count",       JSON_INT_VALUE,  (int64_t)warmup_count_snapshot,
				"n_warmup",           JSON_INT_VALUE,  (int64_t)n_warmup_snapshot,
				NULL);
		if (!d)
			errno = ENOMEM;
		return d;  /* NULL on allocation failure, same as other paths */
	}

build_ready_dict:
	/* Build boundaries list */
	boundaries = json_list_new(jdoc);
	if (!boundaries) {
		errno = ENOMEM;
		return NULL;
	}
	for (i = 0; i <= h->n_bins; i++) {
		v = json_float_new(jdoc, h->boundaries[i]);
		if (!v) {
			errno = ENOMEM;
			goto err_boundaries;
		}
		json_item_add(boundaries, v);
	}

	/* Build bins list. Each count is read atomically; the overall
	 * snapshot is not perfectly consistent across bins if updates are
	 * landing concurrently, which is an acceptable tradeoff for
	 * monitoring data. */
	bins = json_list_new(jdoc);
	if (!bins) {
		errno = ENOMEM;
		goto err_boundaries;
	}
	for (i = 0; i < h->n_bins; i++) {
		c = __atomic_load_n(&h->counts[i + 1], __ATOMIC_SEQ_CST);
		v = json_int_new(jdoc, (int64_t)c);
		if (!v) {
			errno = ENOMEM;
			goto err_bins;
		}
		json_item_add(bins, v);
	}

	underflow = __atomic_load_n(&h->counts[0], __ATOMIC_SEQ_CST);
	overflow  = __atomic_load_n(&h->counts[h->n_bins + 1], __ATOMIC_SEQ_CST);
	scale_str = (h->scale == OVIS_HISTOGRAM_SCALE_LOG) ? "log" : "linear";

	/* Build the dict and attach the pre-built lists via json_attr_add.
	 * json_dict_build() with JSON_LIST_VALUE expects inline elements
	 * terminated by JSON_EOL_VALUE, not a pre-built entity, so we use
	 * json_attr_add() for the list fields instead. */
	d = json_dict_build(jdoc,
			"warmup_in_progress", JSON_BOOL_VALUE,   0,
			"n_bins",             JSON_INT_VALUE,    (int64_t)h->n_bins,
			"scale",              JSON_STRING_VALUE, scale_str, strlen(scale_str),
			"underflow",          JSON_INT_VALUE,    (int64_t)underflow,
			"overflow",           JSON_INT_VALUE,    (int64_t)overflow,
			NULL);
	if (!d) {
		errno = ENOMEM;
		goto err_bins;
	}

	rc = json_attr_add(d, "boundaries", boundaries);
	if (rc) {
		errno = rc;
		goto err_dict;
	}
	rc = json_attr_add(d, "bins", bins);
	if (rc) {
		errno = rc;
		goto err_dict;
	}

	return d;

err_dict:
	json_entity_free(d);
err_bins:
	json_entity_free(bins);
err_boundaries:
	json_entity_free(boundaries);
	return NULL;
}