.. _ldmsd_event_based_storage:

============================
LDMSD Event-Based Storage
============================

-------------------------------------------
Event-Driven Storage with Worker Thread Pool
-------------------------------------------

:Date: November 2025
:Manual section: 7
:Manual group: LDMSD


OVERVIEW
========

LDMSD's event-based storage system decouples storage operations from the data
collection path by implementing a dedicated worker thread pool. This
architecture processes storage operations asynchronously, reducing latency in
the update path and allowing storage overhead to be handled by dedicated
threads rather than impacting the data collection process.

Prior to this implementation, storage operations were performed synchronously
within the update callback, which could create bottlenecks when storage
operations were slow. The event-based approach moves storage work to separate
threads, allowing the update path to complete quickly and return the resource
to the io thread.

CONFIGURATION
==============

Storage Threads
---------------

The **storage_threads** command sets the number of worker threads in the storage thread pool.

Syntax:

::

        storage_threads num=<NUM>

Parameters:

   num=NUM
      Number of dedicated storage worker threads. Must be a positive integer.
      Default: 1

The number of storage threads must be configured **before** LDMSD is fully
initialized. Once initialization is complete, the thread pool size cannot be
changed.

Example:

::

        storage_threads num=4

Setting four storage threads allows up to four storage operations to proceed in
parallel


Storage Queue Depth
-------------------

The **storage_queue_depth** command sets the maximum number of events that can
be queued on a single storage worker thread.

Syntax:

::

        storage_queue_depth num=<MAX_DEPTH>

Parameters:

   num=MAX_DEPTH
      Maximum queue depth per worker thread. Use -1 for unlimited queue depth.
      Default: -1 (unlimited)

Behavior:

* If queue depth limit is reached, the storage worker will not accept additional events
* This prevents unbounded memory growth when storage operations cannot keep pace with set updates
* A value of 0 is invalid and will be coerced to 1 (minimum queue depth of 1 is required)
* Negative values enable unlimited queue depth

The queue depth must be configured **before** LDMSD is fully initialized.

Example:

::

        storage_queue_depth num=1000

This limits each worker to a maximum of 1000 queued events.

Configuration Ordering
^^^^^^^^^^^^^^^^^^^^^^

Both **storage_threads** and **storage_queue_depth** are initialization
commands and are processed early in the configuration sequence, before
operational commands like **strgp_add** or **updtr_add**.


PERFORMANCE CONSIDERATIONS
===========================

Choosing Thread Count
---------------------

The optimal number of storage threads/workers depends on several factors:

* **Storage backend characteristics**: For example, fast local storage benefits
  from fewer threads since contention is low. High-latency storage benefits
  from higher thread counts to overlap I/O latency.

* **Data throughput**: High-frequency updates may benefit from multiple storage
  threads to parallelize storage operations.

* **CPU availability**: Each thread consumes CPU resources. Increased thread
  count increases context switching overhead.

Queue Depth Tuning
------------------

Queue depth limits serve as a backpressure mechanism. The optimal value depends
on your deployment characteristics:

* **Unlimited queue depth** (-1): Allows the system to absorb burst workloads
  by queueing events. The trade-off is that memory usage can grow significantly
  if storage cannot keep pace with event generation.

* **Bounded queue depth** (positive integer): Provides backpressure by blocking
  the update path when the queue reaches capacity for all workers. A smaller
  depth limit reduces memory usage but may increase latency in the update path.

To determine an appropriate queue depth, monitor the storage performance
statistics reported by store_time_stats and thread_stats.

STATISTICS AND MONITORING
==========================

Storage Statistics Overview
----------------------------

LDMSD tracks detailed statistics for storage operations at multiple levels.
These statistics help identify bottlenecks and optimize configuration.

Thread Statistics (thread_stats command)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The **thread_stats** command reports statistics for storage worker threads
alongside other LDMSD threads. The output includes:

* **Utilization**: CPU utilization percentage for each storage worker
* **Idle time**: Time the worker spent idle
* **Active time**: Time the worker spent executing storage events
* **Event counts**: Number of scheduled events, excluding the already processed ones
* **Refresh window**: Interval over which statistics were measured

Storage worker statistics can be compared against IO thread statistics to
diagnose whether storage overhead is impacting the data collection path.

Storage Time Statistics (store_time_stats command)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The **store_time_stats** command provides detailed breakdown of storage
operation latency by pipeline stage. The statistics include:

* **commit**: Time spent in the worker thread executing the actual storage operation
* **decomp**: Time spent decomposing the snapshot into rows (decomposed storage only)
* **prep**: Time spent in the update callback preparing the storage event
* **queue**: Time the event spent queued in the worker's event queue
* **wait**: Time spent waiting to acquire an available worker thread

For each set schema and storage policy, the following statistics of the time
duration processing store events are reported:

* **min/max**: Minimum and maximum duration in microseconds
* **avg**: Average duration in microseconds
* **count**: Number of store operations since start/reset
* **min_ts/max_ts**: Timestamps of minimum and maximum durations
* **start_ts/end_ts**: Timestamp range over which statistics were collected

Aggregate statistics across all producer sets include operations per second,
allowing throughput assessment.

Using Statistics for Tuning
----------------------------

**High 'wait' percentages in store_time_stats**: Storage workers are saturated or unavailable. Possible solutions:

* Increase the number of storage threads to parallelize work
* Increase queue depth to buffer more events while waiting
* Check whether storage backend performance needs optimization

**High 'queue' percentages in store_time_stats**: Events are accumulating
faster than workers can process. Possible solutions:

* Increase the number of storage threads
* Optimize storage backend performance to reduce commit time

**High 'average' times of the storage threads in thread_stats**: The actual
storage operation is slow. Possible solutions:

* Optimize the storage backend
* Check storage backend logs for errors or performance issues

**High 'prep' percentages in store_time_stats**: Data preparation in the update
callback is taking significant time. This is less common but may indicate:

* Complex decomposition logic for decomposed storage
* Excessive data copying or transformation

**Increasing memory usage**: Events are accumulating in worker queues faster
than they can be processed. Possible solutions:

* Reduce queue depth to enforce stricter backpressure
* Increase the number of storage threads
* Investigate and resolve storage backend bottlenecks

Statistics Reset
^^^^^^^^^^^^^^^^

The **stats_reset** command can reset store statistics for subsequent
measurements. Specify the **store** flag to reset only storage-related
statistics, or **all** to reset everything.


COMPATIBILITY
=============

The event-based storage system works transparently with existing storage
plugins. No changes are required to:

* Legacy storage plugins using the **store()** API
* Decomposed storage plugins using the **commit()** API
* Storage policies and their configuration

Both storage types (legacy and decomposition) are supported:

* **Legacy storage**: The snapshot is passed to the **store()** API alongside metric metadata
* **Decomposition storage**: The snapshot is passed to the **decompose()**
  function, which produces rows that are then passed to **commit()**


TROUBLESHOOTING
===============

High Memory Usage
-----------------

If LDMSD's memory usage grows unexpectedly:

1. Check if storage operations are falling behind data collection
2. Set a queue depth limit to prevent unbounded buffering
3. Increase the number of storage threads to parallelize work
4. Check storage backend performance and optimize if needed

Blocked Update Path
-------------------

If the update callback takes longer than expected:

1. Check log messages for backpressure on worker acquisition
2. Increase the number of storage threads
3. Increase queue depth to buffer more events
4. Profile the storage backend for performance issues

SEE ALSO
========

:ref:`ldmsd_config_files(7) <ldmsd_config_files>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldmsd(8) <ldmsd>`
