.. _sampler_plugin_developer_documentation:

==========================================
Sampler Plugin Developer Documentation
==========================================

.. contents:: Table of Contents
   :local:
   :depth: 2

Section 1: Threading Overview
==============================

Execution Context
-----------------

Plugin interfaces (``constructor()``, ``config()``, ``sample()``, ``destructor()``) are
not always called from the same thread. Understanding which thread calls which interface
is important because it determines what operations are safe to perform and whether
additional synchronization is needed. ldmsd uses three categories of threads:

**Worker threads** are created and managed by ldmsd. Their responsibilities are:

- Schedule samples
- Call ``sample()``
- Handle configuration commands (``load``, ``config``, ``start``, ``stop``, ``term``)
  in configuration files either from ``-c`` or ``-y``

**Dedicated sampling thread** — if the plugin instance is configured to use a dedicated
thread, ldmsd creates one when ``start`` is called and deletes it when ``stop`` is
called. Its sole responsibility is to call ``sample()`` for that plugin instance.

**IO threads** (also referred to as transport threads) are created and managed by Zap.
Their responsibilities are:

- Handle configuration commands (``load``, ``config``, ``start``, ``stop``, ``term``)
  sent by a remote interface (``ldmsd_controller`` and maestro)
- Deliver Stream and Message to sampler plugins

Section 2: API Threading Guarantees
=====================================

Each plugin interface may be called by different threads depending on how the command
that triggers it is delivered — whether from a configuration file processed at startup or
from a remote client such as ``ldmsd_controller`` or maestro. The following describes
which thread is responsible for calling each interface.

.. figure:: ../../images/sampler_plugin_developer/sampler_plugin_command_interface_map.png
   :alt: Diagram mapping config commands to plugin interface calls
   :align: center

   Config Commands to Sampler Plugin Interface Calls

- ``constructor()`` is called by the thread handling the load configuration command:

  - worker thread when the load command is in config files
  - IO thread when the load command is sent by a remote client such as
    ``ldmsd_controller`` or maestro

- ``usage()`` is called by the thread handling the usage configuration command:

  - IO thread typically calls ``usage()`` because it makes most sense for users to use
    it in ``ldmsd_controller``
  - ``usage()`` is expected to return an immutable string of a short description of the
    plugin and how to configure it

- ``config()`` is called by:

  - worker thread when the command is in config files
  - IO thread when the command is sent by a remote client such as ``ldmsd_controller``
    or maestro

- ``sample()`` is only called by a single thread:

  - a worker thread shared with other ldmsd's operations (e.g., other sampler plugin's
    ``sample()`` calls)
  - a dedicated thread created by ldmsd at the config time if the plugin instance is
    configured to have a dedicated sampling thread

- ``destructor()`` can be called by either a worker thread or an IO thread. It is called
  when the reference of the plugin configuration object reaches zero.

Beyond knowing which thread calls which interface, plugin authors can rely on a set of
ordering and availability guarantees that ldmsd enforces across the plugin lifecycle.
These are safe to assume regardless of which thread is active:

What Sampler Plugin Authors CAN Assume
---------------------------------------

- ``constructor()`` will be the first interface to be called
- ``destructor()`` will be the last interface to be called
- Plugin instance log handle is available when ``constructor()`` is called, so
  ``ldmsd_plug_log_get()`` can be called in ``constructor()``
- The plugin instance log handle exists when ``destructor()`` is called
- The plugin instance has been configured at least once
  (``config name=<plugin instance name> ...``) before ``sample()`` is called
- All plugin interfaces are protected by mutex, except ``usage()``, which may be called
  concurrently with ``config()`` and ``sample()``

Section 3: Concurrency Scenarios
==================================

This section describes which plugin operations can run concurrently and which
are mutually exclusive. ldmsd serializes most plugin operations to prevent race
conditions. usage() is the exception — it is not serialized because it only
returns an immutable string and therefore does not modify any plugin state.

Concurrent Operations
----------------------

``usage()`` may run concurrently with the other operations between ``constructor()`` and
``destructor()`` calls.

Mutually Exclusive Operations
------------------------------

``constructor()``, ``config()``, ``sample()``, and ``destructor()`` are mutually
exclusive. All these operations are serialized by ldmsd — only one executes at a time.

Section 4: Synchronization Mechanism
=======================================

This section describes how to write thread-safe sampler plugins. It covers two
distinct mechanisms: plugin-level locking for protecting plugin-owned data when
the plugin manages its own threads, and the LDMS set transaction API for
protecting metric consistency during updates.

Guidance for Plugin Authors
----------------------------

Sampler plugin authors do not need locks to synchronize between plugin operations
(``constructor()``, ``config()``, ``sample()``, ``destructor()``) because ldmsd
serializes all these operations — only one executes at a time. Plugin authors need to use
locks if and only if the plugin itself has multiple threads of execution.

What Locking Primitives to Use
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

- Use standard ``pthread_mutex_t`` for protecting plugin-owned data
- Do **NOT** attempt to use ldmsd's internal cfgobj lock
- Initialize mutex in ``constructor()``, destroy in ``destructor()``

Lock Order Guideline
~~~~~~~~~~~~~~~~~~~~~

- If your plugin uses multiple locks, document your own lock acquisition order
- Never call ldmsd APIs (like ``ldmsd_set_deregister``) while holding plugin locks
- Be mindful of holding locks for unnecessary duration to optimize the sampler plugin
  performance
- Release all plugin-specific locks before returning any interfaces

When Locks Are NOT Needed
~~~~~~~~~~~~~~~~~~~~~~~~~~

- Protecting data that is only accessed by plugin operations (``constructor()``,
  ``config()``, ``sample()``, ``destructor()``)
- Changing metric values in LDMS sets do not need any locks

LDMS sets do not require explicit locking by plugin authors — thread safety is
handled by the transaction API.

Thread Safety of LDMS Sets
--------------------------

- LDMS sets have built-in transaction safety via "consistent/inconsistent" flags
- ``sample()`` must call ``ldms_transaction_begin()`` before updating a metric value and
  call ``ldms_transaction_end()`` after updating all metric values
- Set readers can call ``ldms_set_is_consistent()`` to verify that a set is not
  currently being updated
- Sampler plugin authors do **NOT** need additional locks for the LDMS set data itself
- LDMS transaction APIs are only needed to enable external set readers to verify inconsistent
  metric values. For example, an ldmsd aggregator automatically checks if the set
  is inconsistent so that it will not store a set that is in the inconsistent state

Available Locks
----------------

Do not call ``ldmsd_cfgobj_lock()``. The cfgobj lock protects internal ldmsd properties.
Sampler plugin authors should not use or depend on this lock. Use ``pthread_mutex_t`` for
plugin data.

Section 5: Plugin Lifecycle
============================

This section describes the lifecycle of a sampler plugin instance. A plugin
instance transitions through a defined set of states in response to
configuration commands. The state determines which plugin interfaces ldmsd may
call and which commands are valid at that point. Note that a plugin instance
must be stopped before it can be terminated — the term command is only valid
from the INIT or CONFIGURED states.

State Diagram
--------------

.. figure:: ../../images/sampler_plugin_developer/sampler_plugin_state_diagram.png
   :alt: Plugin lifecycle state diagram
   :align: center

   Sampler Plugin Lifecycle State Diagram

State Descriptions
-------------------

The table below describes each state in the plugin lifecycle, how it is entered, and
which operations are permitted in that state.

.. list-table::
   :header-rows: 1
   :widths: 15 20 20 25 20

   * - State
     - How to Enter
     - Plugin Status
     - Allowed Operations
     - Operation Calling Thread
   * - INIT
     - load command calls ``constructor()``
     - Plugin loaded but not configured
     - ``config()``, ``usage()``
     - Worker or IO
   * - CONFIGURED
     - ``config`` command calls ``config()``, or ``stop`` command after ``start`` command
     - Plugin configured
     - ``config()`` (reconfig & multi-config lines), ``usage()``
     - Worker or IO
   * - RUNNING
     - ``start`` command initiates sampling
     - Plugin actively sampling data
     - ``sample()``, ``usage()``
     - Worker or dedicated sampling thread (for ``sample()``); IO (for ``usage()``)
   * - TERMINATING
     - ``term`` command
     - Plugin being destroyed
     - ``destructor()``
     - Worker or IO (whoever drops last reference)

Section 6: LDMSD Internal Implementation Details (For Maintainers Only)
=========================================================================

This section documents ldmsd-internal implementation details for ldmsd
maintainers. Plugin authors do not need to read this section.

Lock Acquisition Order
-----------------------

The following describes the acquisition order for the plugin cfgobj lock:

- Take when handling ``start``; release before replying back
- Take before calling ``sample()``; release after returning
- Take when handling ``stop``; release before replying back
- Take when handling ``term``:

  - Release before calling ``ldmsd_set_deregister()``
  - Re-acquire the lock
  - Release before removing the cfgobj from the tree

SEE ALSO
=========

:ref:`ldmsd(8) <ldmsd>` :ref:`ldmsd_controller(8) <ldmsd_controller>`
