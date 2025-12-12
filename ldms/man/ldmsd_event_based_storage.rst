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

ARCHITECTURE
============

Worker Thread Pool
------------------

The event-based storage system maintains a configurable pool of worker threads
dedicated to storage operations. Each worker thread has its own scheduler and
event queue, allowing storage events to be processed in parallel while
maintaining ordering guarantees per worker.

Key characteristics of the worker pool include:

* **Configurable size**: The number of worker threads can be set via the
  **storage_threads** configuration command (default: 1)
* **Independent event queues**: Each worker maintains its own scheduler and
  event queue
* **Load distribution**: Storage events are distributed across workers using a
  round-robin approach * **Queue depth control**: Optional queue depth limits
  prevent unbounded memory growth under heavy load

Storage Events and Context
--------------------------

When a metric set is updated, LDMSD creates a storage event containing the
necessary context for processing:

* **Set snapshot**: A point-in-time copy of the metric set to preserve data
  integrity. The snapshot ensures that storage operations access consistent data
  even if the live set is updated during processing.
* **Storage policy reference**: Pointer to the storage policy that defines how
  data should be stored
* **Producer set reference**: Reference to the producer-consumer relationship
  tracking the source of the data
* **Row list (for decomposed storage)**: For storage plugins supporting
  row-based decomposition, a list of decomposed rows from the snapshot

The storage event context encapsulates all necessary information and is passed
to a worker thread for asynchronous processing.

Set Snapshots
^^^^^^^^^^^^^

Set snapshots preserve the state of a metric set at the time the storage event
is created. This is critical for data integrity because:

* The live metric set continues to be updated by the data collection process
* The snapshot captures the exact values that need to be stored
* Storage operations can proceed without worrying about concurrent modifications
* Reference counting ensures snapshots are not freed until all storage operations complete

Flow of Storage Operations
---------------------------

The following sequence occurs for each update that triggers a store operation:

1. **Update callback**: The updater receives new data from a producer and
  invokes the update callback

2. **Snapshot creation**: Before processing any storage policies, a snapshot of
  the metric set is created if there are active storage policies

3. **Event creation**: For each applicable storage policy, a storage event
  context is created containing the snapshot and policy information

4. **Worker acquisition**: LDMSD attempts to acquire an available worker
  thread. If all workers are at capacity (queue full), the requesting thread
  blocks on a condition variable until a worker becomes available

5. **Event queuing**: The storage event is added to the acquired worker's event
  queue

6. **Worker processing**: The worker thread processes the event from its queue:

   * Retrieves the set snapshot and storage policy details
   * For decomposed storage: calls the decomposition function to transform the snapshot into rows
   * For legacy storage: calls the store API with the snapshot data
   * Updates performance statistics
   * Releases all references (snapshot, policy, producer set)

7. **Cleanup**: The event context is freed, and statistics are updated


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
parallel, useful for systems with high-throughput storage backends or multiple
storage destinations.


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

* If queue depth limit is reached, the requesting thread blocks until space becomes available
* This prevents unbounded memory growth when storage operations cannot keep pace with event generation
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

The optimal number of storage threads depends on several factors:

* **Storage backend characteristics**: Fast local storage (SSD, in-memory)
  benefits from fewer threads since contention is low. Network storage (NFS, S3)
  benefits from higher thread counts to overlap I/O latency.

* **Data throughput**: High-frequency updates may benefit from multiple threads
  to parallelize storage operations.

* **CPU availability**: Each thread consumes CPU resources. Increased thread
  count increases context switching overhead.

* **System load**: Monitor CPU utilization and adjust accordingly.

Start with the default of 1 worker thread and adjust based on observed performance metrics.

Queue Depth Tuning
------------------

Queue depth limits serve as a backpressure mechanism. The optimal value depends
on your deployment characteristics:

* **Unlimited queue depth** (-1): Allows the system to absorb burst workloads
  by queueing events. The trade-off is that memory usage can grow significantly
  if storage cannot keep pace with event generation.

* **Bounded queue depth** (positive integer): Provides backpressure by blocking
  the update path when the queue reaches capacity. A smaller depth limit reduces
  memory usage but may increase latency in the update path.

To determine an appropriate queue depth, monitor the storage performance
statistics (see STATISTICS AND MONITORING section):

* If **worker wait time** is very high, storage workers are under stress and
  you may need to increase thread count or queue depth
* If **memory usage** is growing unbounded, reduce queue depth to enforce stricter backpressure
* If **commit time** per operation is consistently high, the storage backend may be the bottleneck

If the update path frequently blocks waiting for worker availability:

* Increase the number of storage threads to parallelize storage operations
* Increase queue depth to allow more buffering, or
* Investigate and optimize the storage backend (e.g., batch writes, indexing)


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
* **Event counts**: Number of events processed
* **Refresh window**: Interval over which statistics were measured

Storage worker statistics can be compared against IO thread statistics to
diagnose whether storage overhead is impacting the data collection path.

Storage Time Statistics (store_time_stats command)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The **store_time_stats** command provides detailed breakdown of storage
operation latency by pipeline stage. For each producer set, statistics include:

* **io_thread**: Time spent in the update callback preparing the storage event
* **decomp**: Time spent decomposing the snapshot into rows (decomposed storage only)
* **worker_wait**: Time spent waiting to acquire an available worker thread
* **queue**: Time the event spent queued in the worker's event queue
* **commit**: Time spent in the worker thread executing the actual storage operation

For each stage, the following statistics are reported:

* **min/max**: Minimum and maximum duration in microseconds
* **avg**: Average duration in microseconds
* **count**: Number of operations measured
* **min_ts/max_ts**: Timestamps of minimum and maximum durations
* **start_ts/end_ts**: Timestamp range over which statistics were collected

Aggregate statistics across all producer sets include operations per second,
allowing throughput assessment.

Using Statistics for Tuning
----------------------------

**High worker_wait times**: Storage workers are saturated or unavailable. Possible solutions:

* Increase the number of storage threads to parallelize work
* Increase queue depth to buffer more events while waiting
* Check whether storage backend performance needs optimization

**High queue times**: Events are accumulating faster than workers can process. Possible solutions:

* Increase the number of storage threads
* Optimize storage backend performance to reduce commit time

**High commit times**: The actual storage operation is slow. Possible solutions:

* Optimize the storage backend (indexing, batch writes, connection pooling)
* Check storage backend logs for errors or performance issues
* Consider using dedicated storage infrastructure (SSD, faster network)

**High io_thread times**: Data preparation in the update callback is taking
significant time. This is less common but may indicate:

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
