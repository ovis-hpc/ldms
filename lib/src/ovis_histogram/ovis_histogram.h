#ifndef __OVIS_HISTOGRAM_H__
#define __OVIS_HISTOGRAM_H__

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>

#include "ovis_json/ovis_json.h"

#define OVIS_HISTOGRAM_DEFAULT_BINS 12
#define OVIS_HISTOGRAM_DEFAULT_WARMUP 10

/*
 * Scale mode for bin spacing.
 *
 * Values intentionally start at 1, not 0, so that 0 can be used as a
 * sentinel meaning "no change" in ldmsd_histogram_recalibrate(), matching
 * the same 0-means-unchanged convention used for that function's
 * new_n_bins and new_n_warmup parameters.
 */
enum ovis_histogram_scale {
	OVIS_HISTOGRAM_SCALE_LINEAR = 1,  /* equal-width bins -- default */
	OVIS_HISTOGRAM_SCALE_LOG    = 2,  /* equal-ratio (geometric) bins;
	                                       better resolution for right-skewed,
	                                       outlier-prone distributions */
};

/*
 * counts layout:
 *   counts[0]          = underflow
 *   counts[1..n_bins]  = bins
 *   counts[n_bins+1]   = overflow
 *
 * Locking model:
 *   - `lock` protects the warmup phase: appending to warmup_buf,
 *     incrementing warmup_count, the one-time boundary-fixing transition
 *     in histogram_fix_bins(), and any reallocation of warmup_buf /
 *     boundaries / counts performed by ldmsd_histogram_recalibrate().
 *   - `bins_ready` is the transition flag, updated with __atomic builtins
 *     (matching the convention used elsewhere in ldmsd, e.g.
 *     oversampled_cnt / prdset_cnt). It is written with __ATOMIC_RELEASE
 *     as the last step of histogram_fix_bins() (after boundaries[] is
 *     fully populated), and is read with __ATOMIC_ACQUIRE. Once set to 1,
 *     it stays 1 across calls to ldmsd_histogram_reset() (reset only
 *     touches counts[]). It can be explicitly flipped back to 0 by
 *     ldmsd_histogram_recalibrate(), which re-enters the warmup phase to
 *     compute fresh boundaries (optionally with a different n_bins,
 *     n_warmup, and/or scale) -- see that function's comment for details.
 *   - Once bins_ready is observed true, counts[] is updated/read with
 *     __atomic_fetch_add / __atomic_load_n only -- no lock needed.
 *     boundaries[]/counts[]/n_bins are only reallocated or changed by
 *     ldmsd_histogram_recalibrate() while holding `lock`, and bins_ready
 *     is flipped to 0 before any such change and only flipped back to 1
 *     (by the next histogram_fix_bins()) once the new arrays are fully
 *     in place -- so a fast-path caller either sees the complete old
 *     arrays (bins_ready still 1) or is routed into the locked slow path
 *     (bins_ready now 0).
 *   - The caller doesn't needs to take an external lock around calls
 *     into this struct; locking is internal to ldmsd_histogram_*().
 */
typedef struct ovis_histogram {
	pthread_mutex_t lock;     /* protects warmup_buf/warmup_count, the
	                             warmup->ready transition, and any
	                             reallocation done by recalibrate() */
	int bins_ready;           /* 0 = still warming up, 1 = boundaries fixed;
	                              accessed only via __atomic builtins */
	enum ovis_histogram_scale scale;  /* bin spacing mode (under lock) */

	int n_warmup;             /* number of samples before fixing bins;
	                              may change via ldmsd_histogram_recalibrate() */
	int warmup_count;         /* samples seen during warmup (under lock) */
	double *warmup_buf;       /* n_warmup samples; allocated in init(), and
	                              reallocated by recalibrate() only if a new
	                              n_warmup is requested (under lock) */

	int n_bins;               /* number of bins; may change via
	                              ldmsd_histogram_recalibrate() */
	double *boundaries;       /* n_bins + 1 values; written by histogram_fix_bins(),
	                              then read-only until/unless recalibrate()
	                              reallocates it (under lock) */
	uint64_t *counts;         /* n_bins + 2 entries (underflow + bins + overflow);
	                              accessed via __atomic builtins once bins_ready;
	                              may be reallocated by recalibrate() (under lock) */
} *ovis_histogram_t;

/*
 * Initialize a histogram struct.
 *
 * Caller must call \c ldmsd_histogram_destroy() when the histogram isn't used anymore.
 *
 * \param n_bins    number of bins; pass 0 for default (LDMSD_HISTOGRAM_DEFAULT_BINS)
 * \param n_warmup  number of warmup samples; pass 0 for default (LDMSD_HISTOGRAM_DEFAULT_WARMUP)
 * \param scale     LDMSD_HISTOGRAM_SCALE_LINEAR or LDMSD_HISTOGRAM_SCALE_LOG;
 *                  pass 0 for default (LDMSD_HISTOGRAM_SCALE_LINEAR)
 *
 * Returns 0 on success, errno on failure.
 */
int ovis_histogram_init(struct ovis_histogram *h, int n_bins, int n_warmup,
                          enum ovis_histogram_scale scale);

/*
 * Free the resources inside \c h
 *
 * Caller needs to free the histogram object afterward.
 */
void ovis_histogram_destroy(struct ovis_histogram *h);

void ovis_histogram_reset(struct ovis_histogram *h);

/*
 * Update a histogram with a new data point.
 *
 * No external locking required -- locking is handled internally and only
 * taken during the warmup phase (and the rare transition moment). Once
 * bins are fixed, this path is fully lock-free.
 *
 * \param h       Histogram handle
 * \param value   value
 */
void ovis_histogram_update(struct ovis_histogram *h, double value);

/*
 * Discard the current boundaries and counts, and re-enter the warmup
 * phase to recalibrate boundaries from fresh samples.
 *
 * Unlike ldmsd_histogram_reset(), which only zeroes counts and preserves
 * existing boundaries, this function throws away the calibration itself.
 * This is useful when the existing boundaries no longer fit the metric's
 * current behavior -- for example, a metric (such as store_time) whose
 * boundaries were calibrated during an atypical startup phase (opening
 * database connections, etc.) and no longer reflect steady-state values.
 *
 * \param new_n_bins    If > 0, change the number of bins. If 0, keep the
 *                      existing n_bins.
 * \param new_n_warmup  If > 0, change the number of warmup samples used
 *                      for recalibration. If 0, keep the existing n_warmup.
 * \param new_scale     If non-zero (LDMSD_HISTOGRAM_SCALE_LINEAR or
 *                      LDMSD_HISTOGRAM_SCALE_LOG), change the bin scale
 *                      mode. If 0, keep the existing scale.
 *
 * Returns 0 on success, errno on failure (e.g. ENOMEM if a requested size
 * change requires reallocation and that allocation fails).
 *
 * On failure, the histogram is left completely untouched -- still
 * bins_ready with its prior boundaries/counts/n_bins/n_warmup/scale
 * intact -- rather than partially transitioned. All necessary
 * reallocations are performed and verified to succeed before any live
 * state (bins_ready, warmup_count, counts, n_bins, n_warmup, scale, or
 * the buffer pointers themselves) is modified.
 *
 * No external locking required.
 */
int ovis_histogram_recalibrate(struct ovis_histogram *h, int new_n_bins,
                                 int new_n_warmup, enum ovis_histogram_scale new_scale);

/*
 * \brief Create a JSON dictionary from struct ldmsd_histogram.
 *
 * Reports warmup progress while still calibrating, and bins/boundaries
 * once calibration is complete -- see the two JSON shapes below.
 *
 * \param jdoc Caller-owned json_doc_t. Must not be NULL -- the caller is
 *             responsible for allocating it (e.g. via json_doc_new())
 *             before calling this function, and for freeing it afterward.
 *             This function never allocates or frees jdoc.
 * \param h    The histogram to serialize.
 *
 * Returns the JSON dict entity on success (in either shape). Returns
 * NULL if jdoc is NULL or on allocation failure.
 *
 * The returned JSON object has one of two shapes, distinguished by the
 * "warmup_in_progress" attribute:
 *
 * While still calibrating:
 * {
 *   "warmup_in_progress" : true,
 *   "warmup_count"       : <int>,   -- samples collected so far
 *   "n_warmup"           : <int>    -- samples needed before bins are fixed
 * }
 *
 * Once calibrated:
 * {
 *   "warmup_in_progress" : false,
 *   "n_bins"     : <int>,           -- number of bins (excludes underflow/overflow)
 *   "scale"      : <string>,        -- "linear" or "log"
 *   "boundaries" : [ <float>, ... ] -- n_bins+1 boundary values
 *   "underflow"  : <int>,           -- count of samples below boundaries[0]
 *   "bins"       : [ <int>, ... ]   -- n_bins counts; bins[i] covers [boundaries[i], boundaries[i+1])
 *   "overflow"   : <int>            -- count of samples >= boundaries[n_bins]
 * }
 *
 * No external locking required -- a brief internal lock is taken only to
 * read warmup_count/n_warmup consistently while still in the warmup state
 * (those two fields are not part of the lock-free atomic surface the way
 * bins_ready/counts[] are).
 */
json_entity_t ovis_histogram2dict(json_doc_t jdoc, struct ovis_histogram *h);

#endif