.. _storage_plugin_developer_documentation:

=============================================
Storage Plugin Developer Documentation
=============================================

.. contents:: Table of Contents
   :local:
   :depth: 2

Section 1: Threading Overview
==============================

Execution Context
-----------------

Storage plugin interfaces (``constructor()``, ``config()``, ``open()``, ``store()``,
``flush()``, ``commit()``, ``close()``, ``destructor()``) are not always called from
the same thread. Understanding which thread calls which interface is important because
it determines what operations are safe to perform and whether additional synchronization
is needed. ldmsd uses three categories of threads relevant to storage plugins:

**Worker threads** are created and managed by ldmsd. Their responsibilities are:

- Handle configuration commands (``load``, ``config``, ``strgp_add``, ``strgp_prdcr_add``,
  ``strgp_start``, ``strgp_stop``, ``term``) in configuration files from ``-c`` or ``-y``

**IO threads** (also referred to as transport threads) are created and managed by Zap.
Their responsibilities are:

- Handle configuration commands (``load``, ``config``, ``strgp_add``, ``strgp_prdcr_add``,
  ``strgp_start``, ``strgp_stop``, ``term``) sent by remote clients, e.g.
  (``ldmsd_controller`` and ``maestro``)
- Deliver metric set updates from producers to the update callback, which triggers the
  storage path

**Storage worker threads** are created and managed by ldmsd. Their responsibilities are:

- Dequeue storage events from the storage worker queue
- Call ``store()``, ``flush()``, and ``commit()`` on storage plugins

Section 2: Storage Paths
=========================

A storage plugin supports one or both of the following invocation paths. Which path
ldmsd uses for a given storage policy depends on whether that policy was configured
with the ``decomp=`` attribute.

Legacy Path
-----------

In the legacy path, ``open()`` is called once when the first matching producer set
update arrives, and ``store()`` is called on every subsequent update. The plugin
receives a raw ``ldms_set_t`` snapshot and an array of metric indices. This path is
appropriate for backends that can store LDMS metric sets without a fixed schema.

Decomposed Path
----------------

In the decomposed path, a decomposition layer runs in the IO thread before the
storage event is posted. It transforms the LDMS set snapshot into a list of typed
rows (``ldmsd_row_t``), each representing a flattened record with named, typed columns.
The storage worker thread then calls ``commit()`` with these rows. ``open()`` and
``store()`` are not called on this path.

New storage plugins must implement ``commit()``. The decomposed path decouples the
plugin from the LDMS schema layout, supports schema evolution through the decomposition
configuration, and enables backends that require a fixed schema (SQL tables, Avro
streams, columnar formats, etc.).

.. important::

   Do NOT free ``row_list`` inside ``commit()``. ldmsd calls
   ``decomp->release_rows()`` after ``commit()`` returns. Freeing it yourself
   causes a double-free.

Event-Based Storage
--------------------

Both paths use an event-based model that decouples storage from data collection. When
a metric set update arrives, the IO thread creates a snapshot of the set, runs
decomposition if configured, and posts a storage event to a storage worker queue, then
returns immediately. A storage worker thread dequeues the event and calls
``store()``/``flush()`` or ``commit()`` on the plugin. Because ``store()`` and
``commit()`` run asynchronously on storage worker threads, the IO thread is free to
continue processing updates without waiting for storage operations to complete. The
storage worker pool absorbs bursts and variations in storage latency — the IO thread
is only affected if all storage workers are at capacity, at which point it blocks
until a worker becomes available.

The storage worker pool is shared across all storage policies. Worker assignment is
round-robin per event — a given storage policy's events are not pinned to a specific
worker. For the architecture, configuration (``storage_threads``, ``storage_queue``),
and tuning of the storage worker pool, see ``ldmsd_event_based_storage(7)``.

Section 3: API Threading Guarantees
=====================================

Each plugin interface may be called by different threads depending on how the command
that triggers it is delivered — whether from a configuration file processed at startup
or from a remote client such as ``ldmsd_controller`` or ``maestro``. The following
describes which thread is responsible for calling each interface.

Interface-by-Interface Breakdown
---------------------------------

- ``constructor()`` is called by the thread handling the ``load`` configuration command:

  - A worker thread if the command comes from a configuration file (``-c`` or ``-y``)
  - An IO thread if the command comes from ``ldmsd_controller`` or ``maestro``

- ``usage()`` is called by the thread handling the ``usage`` configuration command:

  - A worker thread if the command comes from a configuration file (``-c`` or ``-y``)
  - An IO thread if the command comes from ``ldmsd_controller`` or ``maestro``.
    This makes most sense for users to use it in ``ldmsd_controller``
  - It is expected to return an immutable string describing the plugin and how
    to configure it. ``usage()`` is not serialized with any other operation.

- ``config()`` is called by the thread handling the ``config`` command:

  - A worker thread if the command comes from a configuration file
  - An IO thread if the command comes from ``ldmsd_controller`` or ``maestro``
  - The plugin instance's cfgobj lock is held by ldmsd when ``config()`` is called.

- ``open()`` is called by an IO thread when the first matching producer set
  update arrives and the storage policy is RUNNING but has no open container
  yet. The strgp lock for that storage policy is held when ``open()`` is
  called. This interface is only used on the legacy path (see `Legacy Path`_).

- ``store()`` is called by a storage worker thread, once per storage event. No
  lock is held by ldmsd when ``store()`` is called — neither the strgp lock nor
  the cfgobj lock.  This interface is only used on the legacy path (see `Legacy
  Path`_).

- ``flush()`` is called by the same storage worker thread that called
  ``store()`` for that event, immediately after ``store()`` returns, when the
  storage policy's ``flush_interval`` has elapsed. No lock is held.

- ``commit()`` is called by a storage worker thread, once per storage event,
  when the storage policy is configured with a decomposition (``decomp=``). It
  is called instead of ``store()`` and follows the same threading rules: no
  lock is held. See `Decomposed Path`_ for details on when ``commit()`` vs
  ``store()`` is used.

- ``close()`` is called by the thread handling ``strgp_stop``:

  - A worker thread if the command comes from a configuration file
  - An IO thread if the command comes from ``ldmsd_controller`` or ``maestro``
  - The strgp lock is held during ``strgp_stop``.

- ``destructor()`` is called by the thread that drops the last reference to the
  plugin instance — either a worker thread or an IO thread. By the time
  ``destructor()`` is called, all storage policies associated with this plugin
  instance have been stopped and closed.

What Storage Plugin Authors CAN Assume
---------------------------------------

- ``constructor()`` will be the first interface to be called.
- ``destructor()`` will be the last interface to be called.
- Plugin instance log handle is available when ``constructor()`` is called, so
  ``ldmsd_plug_log_get()`` can be called in ``constructor()``.
- The plugin instance log handle exists when ``destructor()`` is called.
- For a given storage policy, ``open()`` completes before any ``store()`` or ``flush()``
  call for that storage policy begins. ``open()`` is only called when decomposition is
  not in use.
- ``close()`` completes for all storage policies using the storage plugin instance before
  ``destructor()`` is called.
- No ``store()``, ``flush()``, or ``commit()`` calls are in flight for a given storage
  policy when ``close()`` executes for that policy.

Section 4: Concurrency Scenarios
==================================

This section describes which plugin operations can run concurrently and which are
mutually exclusive. ldmsd does not serialize ``store()``, ``flush()``, and ``commit()``
with any lock — storage plugin authors must handle concurrency explicitly.

Concurrent Operations
----------------------

- ``config()`` and ``store()`` / ``commit()`` — ldmsd does not serialize these.
  ``config()`` holds the plugin instance's cfgobj lock, but the storage worker
  holds no lock when calling ``store()`` or ``commit()``. If your plugin
  instance state can be modified by ``config()`` and read or written by
  ``store()`` or ``commit()``, that state is subject to a data race. The plugin
  must protect it.

- ``store()`` / ``commit()`` for different storage policies — Multiple
  storage workers may call into the same plugin instance simultaneously for
  different storage policies. For example, if two storage policies ``strgp1``
  and ``strgp2`` both use the same plugin instance, ``store()`` for ``strgp1``
  and ``store()`` for ``strgp2`` may run concurrently on different storage
  worker threads.

- ``open()`` for different storage policies — Each storage policy has its
  own strgp lock, so ``open()`` for different storage policies may run
  concurrently inside the same plugin instance. If ``open()`` modifies shared
  plugin instance state (such as a container registry), the plugin must protect
  that state.

- Multiple ``store()`` / ``commit()`` calls for the same storage policy —
  may execute simultaneously on different storage workers if events are queued
  faster than a single storage worker processes them. Storage worker assignment
  is round-robin per event, not sticky per storage policy, so two events for
  the same storage policy may be dispatched to different storage workers and
  overlap.

- ``config()`` and ``open()`` / ``close()`` — ``config()`` holds the cfgobj
  lock; ``open()`` and ``close()`` hold the strgp lock. These are different
  locks with no ordering relationship. If ``config()``, ``open()`` and
  ``close()`` can both modify the same plugin instance state, the plugin must
  protect it.

Mutually Exclusive Operations
------------------------------

``open()`` and ``close()`` for a given storage policy do not overlap with each
other or with any ``store()``, ``flush()``, or ``commit()`` for that storage
policy.

Section 5: Synchronization Mechanism
======================================

This section describes when storage plugin authors need locks, what kind to use, and
which ldmsd-internal mechanisms are off-limits.

Guidance for Plugin Authors
----------------------------

Storage plugin authors must use locks to protect any state that can be accessed from
the concurrent operations described in `Section 4: Concurrency Scenarios`_.
The following scenarios require a mutex:

**Plugin instance state accessed from both config() and store() or commit()** — Any
plugin instance state that ``config()`` can write and ``store()`` or ``commit()`` can
read or write must be protected by a plugin mutex.

**Per-store-handle state accessed from store(), flush(), or commit()** — because
consecutive events for the same storage policy may be dispatched to different storage
worker threads, per-store-handle state modified in these functions requires a mutex. An
exception is state that is written only during ``open()`` and is strictly read-only for
the lifetime of ``store()``, ``flush()``, and ``commit()`` calls.

What Locking Primitives to Use
-------------------------------

Use standard ``pthread_mutex_t`` for protecting plugin-owned data.

Do NOT use ldmsd's internal cfgobj lock (``ldmsd_cfgobj_lock()``). It protects internal
ldmsd properties and must not be acquired by plugins.

A mutex protecting plugin instance state should be initialized in ``constructor()`` and
destroyed in ``destructor()``.

A mutex protecting per-store-handle state should be initialized in ``open()`` and
destroyed in ``close()``.

Lock Order Guideline
---------------------

If your plugin uses multiple locks, document your lock acquisition order and follow it
consistently.

Do not call ldmsd APIs from within a plugin lock. ldmsd APIs that operate on
configuration objects (storage policies) acquire internal ldmsd locks. Calling them
while holding a plugin lock risks deadlock because ldmsd may itself hold those internal
locks when it calls your plugin.

Release all plugin locks before returning from any interface.

Section 6: Plugin Lifecycle
=============================

The storage plugin does not define plugin-level states the way the sampler plugin does.
The lifecycle is instead described by the sequence of interface calls that ldmsd makes
on the plugin instance and on the storage policies attached to it.

Plugin Instance Sequence
-------------------------

::

   load      → constructor()
   config    → config()         [may repeat]
   term      → destructor()

The plugin instance has a ``configured`` flag set after the first successful ``config()``
call. There is no plugin-level RUNNING or STOPPED state — those belong to the storage
policy, not the plugin instance.

Storage Policy Sequence (per storage policy)
----------------------------------------------

::

   strgp_start → storage policy transitions to RUNNING
                 → open() called on first matching producer set update
                   → store()/flush() [legacy path only]
                 or commit() called per update (storage worker thread)
   strgp_stop  → storage policy transitions to STOPPED
                 → close() called (same thread as strgp_stop)
   strgp_del   → storage policy removed (must already be STOPPED)

A plugin instance may have zero or more storage policies attached to it at any time,
each independently in STOPPED or RUNNING state. The ``destructor()`` is only called
after all associated storage policies have been stopped and deleted.

Section 7: Using the LDMS Message Service in Storage Plugins
==============================================================

For terminology, how to check whether the message service is enabled, which API to use,
and callback threading details, see Section 6 of the Sampler Plugin Developer
Documentation. The information there applies equally to storage plugins.

This section covers only what differs for storage plugins: which message service
functions are appropriate to call from each storage plugin interface and the concurrency
implications.

Appropriateness by Plugin Interface
-------------------------------------

The following table summarizes which functions are appropriate to call from each plugin
interface. For threading details and what is safe to call from within the callback, see
``ldms_msg(7)`` and ``ldms_msg_chan(7)``.

The table below covers the plugin instance interfaces (``constructor()``, ``config()``,
``destructor()``, ``open()``, ``store()``, ``commit()``, ``close()``). ``flush()``
follows the same rules as ``store()``.

.. list-table::
   :header-rows: 1
   :widths: 28 8 8 8 12 8 10 10

   * - API
     - constructor()
     - config()
     - open()
     - store() / commit()
     - close()
     - destructor()
     - callback
   * - ``ldms_msg_subscribe()``
     - Yes
     - Yes
     - Yes
     - No [1]
     - No [1]
     - No [1]
     - Yes
   * - ``ldms_msg_client_close()``
     - No
     - Yes
     - No
     - No [1]
     - Yes
     - Yes
     - Yes
   * - ``ldms_msg_publish(NULL,...)``
     - Yes
     - Yes
     - Yes
     - Yes [3]
     - No
     - No
     - Yes [2]
   * - ``ldms_msg_chan_new()``
     - Yes
     - Yes
     - Yes
     - No [1]
     - No
     - No
     - No
   * - ``ldms_msg_chan_publish()``
     - No
     - Yes
     - No
     - Yes [4]
     - No
     - No
     - Yes
   * - ``ldms_msg_chan_close()``
     - No
     - Yes
     - No
     - No [1]
     - Yes
     - Yes
     - Yes [5]
   * - ``ldms_msg_chan_set_q_limit()``
     - Yes
     - Yes
     - Yes
     - No
     - No
     - No
     - No

**Notes:**

[1] Not a thread safety issue. These are lifecycle operations that generally
belong in ``constructor()``, ``config()``, ``open()``, ``close()``, or
``destructor()``.

[2] ``ldms_msg_publish(NULL, ...)`` invokes subscriber callbacks inline before
returning. Avoid holding plugin-internal locks across this call inside a callback — see
``ldms_msg(7)``.

[3] ``store()`` and ``commit()`` run on storage worker threads with no ldmsd lock held.
Publishing from these functions is safe but adds latency to the storage worker.

[4] ``ldms_msg_chan_publish()`` blocks until space is available in the channel's queue.
In ``store()`` / ``commit()``, a full queue will stall the storage worker thread and
increase event queue depth. Consider whether blocking is acceptable or whether the
message should be queued for a separate thread.

[5] Avoid calling ``ldms_msg_chan_close()`` with ``cancel=0`` from within the callback —
it will block the IO thread for the duration of teardown. Use ``cancel=1`` if closing
from within a callback.

Callback
---------

For the LDMS Message Bus API, the callback is invoked by whichever thread called
``ldms_msg_publish()`` for messages published on the same bus, or by an IO thread for
messages arriving from a remote peer. The ``LDMS_MSG_EVENT_CLIENT_CLOSE`` event is
delivered to signal that teardown is complete and resources may be freed.

For the message channel API, the callback is always invoked by an IO thread.
``LDMS_MSG_EVENT_CLIENT_CLOSE`` is handled internally by the channel and is not
delivered to the application callback.

For full details on callback threading, what is safe to call, and teardown patterns,
see ``ldms_msg(7)`` and ``ldms_msg_chan(7)``.

**Concurrency with storage operations:** Message callbacks run on IO threads, which are
distinct from storage worker threads. A callback may run concurrently with ``store()``
or ``commit()`` for any storage policy. If the callback and ``store()``/``commit()``
access shared plugin state, the plugin must protect that state with a mutex.

Section 8: LDMSD Internal Implementation Details (For Maintainers Only)
=========================================================================

This section documents ldmsd-internal implementation details for ldmsd maintainers.
Plugin authors do not need to read this section.

Lock Acquisition Order
-----------------------

The following describes the acquisition order for the strgp lock and related locks:

- The strgp lock is taken in ``ldmsd_strgp_update_prdcr_set()`` when ``open()`` is
  called on the first matching producer set update. It is released before the IO thread
  returns.
- The strgp lock is taken when handling ``strgp_stop`` and held during ``close()``. It
  is released before replying.
- The strgp lock is NOT held during ``store()``, ``flush()``, or ``commit()``. Storage
  events run entirely outside the strgp lock to avoid blocking the IO thread and other
  producers.
- The plugin instance's cfgobj lock is held during ``config()`` but not during
  ``store()`` or ``commit()``, so ldmsd provides no serialization between them.
- The strgp lock is taken when handling ``strgp_del``: the storage policy must be
  STOPPED (``EBUSY`` is returned otherwise), the reference count is checked (``EBUSY``
  is returned if in-flight references remain), and the plugin instance's reference to
  the storage policy is released and the storage policy is removed from the cfgobj tree.

SEE ALSO
=========

``ldmsd(8)`` ``ldmsd_controller(8)`` ``ldmsd_event_based_storage(7)``
``ldms_msg(7)`` ``ldms_msg_chan(7)``
