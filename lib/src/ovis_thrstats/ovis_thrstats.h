/**
 * \manpage libovis_thrstats
 * \brief Thread statistics tracking and analysis for OVIS components
 *
 * The ovis_thrstats library provides functionality to collect and analyze
 * thread utilization information. It can be used to monitor thread activity,
 * track idle versus active time, and calculate utilization over specified
 * time windows.
 *
 * Statistics Collection:
 * ---------------------
 * The library collects the following statistics:
 * - Total idle time (when thread is waiting)
 * - Total active time (when thread is processing)
 * - Thread state transitions (wait start/end events)
 * - Thread identification (thread ID, Linux thread ID)
 * - Utilization over configurable time windows
 *
 * The primary data collection happens when the thread transitions between
 * active and idle states. A thread should call:
 * - ovis_thrstats_wait_start() when it begins waiting (becomes idle)
 * - ovis_thrstats_wait_end() when it stops waiting (becomes active)
 *
 * Bucket-based Statistics Storage:
 * ------------------------------
 * To track thread activity over time, the library uses a circular buffer
 * approach with fixed-size time buckets:
 *
 * 1. Each bucket represents a fixed time duration (default: 10ms)
 * 2. The library maintains two parallel arrays of buckets:
 *    - idle_bucket[]: Records idle time in each time window
 *    - active_bucket[]: Records active time in each time window
 * 3. As a thread transitions between states, the elapsed time is
 *    distributed into appropriate buckets.
 * 4. When a bucket is full (sum of idle+active equals bucket_sz),
 *    the next bucket begins to fill.
 * 5. After all buckets are used, the oldest bucket is recycled.
 *
 * This approach enables efficient calculation of utilization over
 * any time window up to the total history maintained by all buckets
 * (default: ~60 seconds with 6001 buckets of 10ms each).
 *
 * Usage Example:
 * -------------
 * // Initialize thread statistics
 * struct ovis_thrstats stats;
 * ovis_thrstats_init(&stats, "my_thread");
 * ovis_thrstats_thread_id_set(&stats); // This should be called on the thread itself
 *
 * // Track thread activity
 * while (running) {
 *     // Begin waiting for events
 *     ovis_thrstats_wait_start(&stats);
 *
 *     // Wait for some event
 *     epoll_wait(...);
 *
 *     // End waiting (begin active period)
 *     ovis_thrstats_wait_end(&stats);
 *
 *     // Process event
 *     process_event();
 * }
 *
 * // Get utilization report for last 5 seconds
 * struct ovis_thrstats_result result;
 * ovis_thrstats_result_get(&stats, 5, &result);
 * printf("Utilization: %.2f%%\n", result.utilization * 100.0);
 *
 * // Cleanup
 * ovis_thrstats_cleanup(&stats);
 */

#ifndef __OVIS_THRSTATS_H__
#define __OVIS_THRSTATS_H__

#include <inttypes.h>
#include <pthread.h>

/**
 * Default number of buckets for tracking thread statistics over time.
 * With the default bucket size of 10ms, this provides approximately
 * 60 seconds of history (6001 * 10ms = 60.01 seconds).
 */
#define OVIS_THRSTATS_NUM_BUCKET 6001
/**
 * Default bucket size in microseconds. Each bucket represents 10ms
 * of time in the circular buffer.
 */
#define OVIS_THRSTATS_BUCKET_SZ 10000 /* 10 milliseconds */

/**
 * Function signature for application-specific reset callback.
 * This is called when the thread statistics are reset.
 *
 * \param ctxt The application context
 * \param reset_ts The timestamp when reset was initiated
 */
typedef void (*ovis_thrstats_app_reset_fn)(void *ctxt, struct timespec *reset_ts);

/**
 * \struct ovis_thrstats
 * \brief The main structure for tracking thread statistics
 *
 * This structure maintains state for thread utilization tracking and
 * analysis. It tracks both overall statistics (since initialization or
 * last reset) and detailed time-series data using buckets.
 */
typedef struct ovis_thrstats {
	/**
	 * Read-write lock for thread-safe access to statistics.
	 * Writers acquire exclusive access during updates.
	 * Readers acquire shared access during queries.
	 */
	pthread_rwlock_t rwlock;

	/** Thread name for identification purposes */
	char *name;
	/** Linux thread ID (from gettid() system call) */
	pid_t tid;
	/** Thread ID as returned by pthread_self() */
	uint64_t thread_id;
	/**
	 * Timestamp when statistics were initialized or reset.
	 * All measurements are relative to this time.
	 */
	struct timespec start;
	/** Timestamp of the last transition to waiting state */
	struct timespec wait_start;

	/** Timestamp of the last transition from waiting state */
	struct timespec wait_end;
	/**
	 * Current thread state:
	 * 0 = thread is active (processing)
	 * 1 = thread is waiting (idle)
	 */
	int waiting;

	/** Number of processing periods recorded */
	uint64_t proc_count;

	/** Number of waiting periods recorded */
	uint64_t wait_count;

	/** Total idle time in microseconds since start/reset */
	uint64_t wait_tot;

	/** Total active time in microseconds since start/reset */
	uint64_t proc_tot;

	/**
	 * Application-specific context associated with this thread.
	 * This can be used to store additional statistics or state.
	 */
	void *app_ctxt;

	/**
	 * Application callback for reset events.
	 * Called when thread statistics are reset.
	 */
	ovis_thrstats_app_reset_fn app_reset_fn;

	/**
	 * Current index in the circular buffer.
	 * Points to the bucket currently being filled.
	 */
	int curr_idx;

	/** Number of buckets in the circular buffer */
	int bucket_cnt;

	/** Size of each bucket in microseconds */
	unsigned short bucket_sz;

	/**
	 * Circular buffer arrays to track thread state over fixed time windows.
	 * Each bucket represents a fixed time period (typically 10ms).
	 *
	 * idle_bucket[]:   Stores microseconds the thread spent in idle state
	 *                  during each time window.
	 *
	 * active_bucket[]: Stores microseconds the thread spent in active state
	 *                  during each time window.
	 *
	 * The sum of corresponding elements in both arrays equals the bucket
	 * size for fully populated buckets. The current position in these
	 * arrays is tracked by curr_idx, which wraps around when it reaches the
	 * end.
	 */
	unsigned short idle_bucket[OVIS_THRSTATS_NUM_BUCKET];
	unsigned short active_bucket[OVIS_THRSTATS_NUM_BUCKET];
} *ovis_thrstats_t;

/**
 * \struct ovis_thrstats_result
 * \brief Contains analysis results from thread statistics
 *
 * This structure holds both overall statistics and analysis results
 * for a specific time window, such as utilization over the last N seconds.
 */
struct ovis_thrstats_result {
	/** Thread name (copied from ovis_thrstats) */
	char *name;

	/** Linux thread ID */
	pid_t tid;

	/** Thread ID (from pthread_self()) */
	uint64_t thread_id;

	/** Whether thread was waiting at time of measurement */
	int waiting;

	/** When statistics collection started */
	struct timespec start;

	/** Last time thread began waiting */
	struct timespec wait_start;

	/** Last time thread stopped waiting */
	struct timespec wait_end;

	/** Total idle time in microseconds since start */
	uint64_t idle_tot;

	/** Total active time in microseconds since start */
	uint64_t active_tot;

	/** Total duration in microseconds (idle_tot + active_tot) */
	uint64_t dur_tot;

	/** Duration of the analyzed interval in microseconds */
	uint64_t interval_us;

	/** Idle time within the analyzed interval in microseconds */
	uint64_t idle_us;

	/** Active time within the analyzed interval in microseconds */
	uint64_t active_us;

	/** Application context pointer */
	void *app_ctxt;
    };

/**
 * Reset thread statistics to begin a new measurement period
 *
 * \param stats Pointer to thread statistics structure
 * \param now Current timestamp
 */
void ovis_thrstats_reset(ovis_thrstats_t stats, struct timespec *now);

/**
 * Initialize a thread statistics structure
 *
 * \param stats Pointer to thread statistics structure to initialize
 * \param name Optional name for this thread (can be NULL)
 * \return 0 on success, error code on failure
 */
int ovis_thrstats_init(ovis_thrstats_t stats, const char *name);

/**
 * Set the thread ID information in statistics
 * Should be called from the thread being tracked
 *
 * \param stats Pointer to thread statistics structure
 */
void ovis_thrstats_thread_id_set(ovis_thrstats_t stats);

/**
 * Get thread identification information
 *
 * \param stats Pointer to thread statistics structure
 * \param _thread_id Pointer to store pthread_t ID (can be NULL)
 * \param _tid Pointer to store Linux thread ID (can be NULL)
 */
void ovis_thrstats_thread_id_get(ovis_thrstats_t stats, uint64_t *_thread_id, pid_t *_tid);

/**
 * Set application context and reset callback
 *
 * \param stats Pointer to thread statistics structure
 * \param ctxt Application-specific context
 * \param reset_fn Function to call when statistics are reset
 * \return 0 on success, error code on failure
 */
int ovis_thrstats_app_ctxt_set(ovis_thrstats_t stats, void *ctxt, ovis_thrstats_app_reset_fn reset_fn);

/**
 * Get application context
 *
 * \param stats Pointer to thread statistics structure
 * \return Application context pointer
 */
void *ovis_thrstats_app_ctxt_get(ovis_thrstats_t stats);

/**
 * Clean up thread statistics resources
 *
 * The function does not free the memory of \c stats, but
 * it frees all the memory \c ovis_thrstats_init() allocated.
 *
 * \param stats Pointer to thread statistics structure
 *
 * \see ovis_thrstats_init
 */
void ovis_thrstats_cleanup(ovis_thrstats_t stats);

/**
 * Mark the start of a thread wait period (when thread becomes idle)
 *
 * \param stats Pointer to thread statistics structure
 */
void ovis_thrstats_wait_start(ovis_thrstats_t stats);

/**
 * Mark the end of a thread wait period (when thread becomes active)
 *
 * \param stats Pointer to thread statistics structure
 */
void ovis_thrstats_wait_end(ovis_thrstats_t stats);

/**
 * Get thread statistics results for analysis
 *
 * If \c interval_s is zero, the function will return the idle and active time
 * of the last \c stats->bucket_sz * \c stats->bucket_cnt or the maximum of the
 * time window the cache data allows.
 *
 * \param stats Pointer to thread statistics structure
 * \param interval_s Time interval to analyze in seconds (0 = use default 3s)
 * \param res Pointer to result structure (if NULL, one will be allocated)
 * \return Pointer to filled result structure
 */
struct ovis_thrstats_result *
ovis_thrstats_result_get(ovis_thrstats_t stats,
                         uint64_t interval_s,
                         struct ovis_thrstats_result *res);

/**
 * Cleanup resources associated with a thread statistics result
 *
 * The function does not free the memory of \c res, but it frees
 * the memory allocated by \c ovis_thrstats_result_get.
 *
 * \param res Pointer to result structure
 *
 * \see ovis_thrstats_result_get
 */
void ovis_thrstats_result_cleanup(struct ovis_thrstats_result *res);

/**
 * Set or update the thread name
 *
 * \param stats Pointer to thread statistics structure
 * \param name New thread name
 * \return 0 on success, error code on failure
 */
int ovis_thrstats_name_set(ovis_thrstats_t stats, const char *name);


#endif