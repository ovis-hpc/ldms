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
- ``config()`` is never called while the plugin instance is in the ``RUNNING`` state
  (see `Section 5: Plugin Lifecycle`_) — only ``sample()`` and ``usage()`` are called
  in that state
- ``ldmsd_set_register()`` and ``ldmsd_set_deregister()`` are thread safe and may be
  called from ``config()``, ``sample()``, and any thread or callback of the plugin
  (see `Registering Metric Sets with ldmsd`_)

Guarding Against Unsupported Reconfiguration
----------------------------------------------

Some plugins do not support changing their configuration once they have already
been successfully configured. ldmsd does not enforce this on the plugin's
behalf. The ``config()`` interface may be called again after an instance
reaches the ``CONFIGURED`` state.

Plugin authors are responsible for detecting and rejecting reconfiguration
attempts their plugin does not support. If ``config()`` is called on an
instance that is already configured and the plugin does not support
reconfiguration, ``config()`` must return ``EINVAL``. Because all plugin
operations are serialized by ldmsd (see `Section 3: Concurrency Scenarios`_),
this check does not require additional locking.

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

This section describes how to write thread-safe sampler plugins. It covers
plugin-level locking for protecting plugin-owned data when the plugin manages its
own threads, the LDMS set transaction API for protecting metric consistency during
updates, and the registration of metric sets with ldmsd, which is safe from any
thread.

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
- Never call ldmsd APIs (like ``ldmsd_set_register()`` and ``ldmsd_set_deregister()``)
  while holding plugin locks
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

Registering Metric Sets with ldmsd
-----------------------------------

A sampler plugin creates a metric set with ``ldms_set_new()`` and makes it visible
to remote peers with ``ldms_set_publish()``. Sampler plugins typically also register
the set with ldmsd, so that ldmsd knows which sampler plugin instance owns it. The
registration API, ``ldmsd_set_register()`` and ``ldmsd_set_deregister()``, is
documented in the comment block in ``ldmsd.h``. This section describes what
registration does and when a plugin calls these functions.

What Registration Does
~~~~~~~~~~~~~~~~~~~~~~~

``ldmsd_set_register()`` associates a metric set with a sampler plugin instance.
ldmsd uses this association to:

- Report the set in the ``plugn_sets`` configuration request for that plugin
  instance.
- Apply the update hint (sample interval and offset) to the set. A set registered
  before the plugin instance is started receives the hint when ``start`` is
  processed. A set registered while the plugin instance is running receives the hint
  at registration. This includes sets created dynamically, such as per-job sets.
- Release the set references held on behalf of the plugin instance when the instance
  is terminated.

The ``cfg_name`` argument of both functions is the name of the plugin instance, i.e.,
the ``name`` given to the ``load`` command. It is not the name of the plugin library.
A plugin obtains it with ``ldmsd_plug_cfg_name_get()``; plugins that use
``sampler_base`` can use ``base->cfg_name``.

The update hint is stored in the set's metadata and is read by aggregators. An
updater configured with ``auto_interval=true`` schedules the updates of each set
according to that set's hint, and does not update a set that has none (see
``ldmsd_controller``\(8)).

Publishing and registering are independent operations. A set that is published but
not registered is visible to peers, but ldmsd does not apply an update hint to it,
``plugn_sets`` does not report it, and ldmsd holds no reference on it. Whether a
plugin registers its sets depends on whether it needs these behaviors, in particular
whether its sets are meant to be updated by ``auto_interval`` updaters.

If in doubt, register the set. A plugin that registers sets it later deletes while
the sampler is running must deregister each of them before deleting it (see
`When to Call Register and Deregister`_).

When to Call Register and Deregister
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Call ``ldmsd_set_register()`` after the plugin creates a set. It can be called from
``config()``, from ``sample()``, from a thread created by the plugin, and from a
Message Service callback.

Call ``ldmsd_set_deregister()`` before the plugin deletes the set. The sequence is
deregister, unpublish, then delete, as ``base_set_delete()`` in ``sampler_base`` does:

.. code-block:: c

   ldmsd_set_deregister(ldms_set_instance_name_get(set), cfg_name);
   ldms_set_unpublish(set);
   ldms_set_delete(set);

This matters most for plugins that delete sets while the sampler is running, such as
plugins that create one set per job. ldmsd keeps a reference on every registered set,
so the memory of a set that was deleted without being deregistered is not released
until the plugin instance is terminated.

When the plugin instance is terminated, ldmsd releases its references on the
registered sets itself (see `Section 5: Plugin Lifecycle`_).

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

Registered Sets and Termination
--------------------------------

When a plugin instance is terminated with the ``term`` command, ldmsd releases its
own references on the registered sets. Plugin authors do not need to call
``ldmsd_set_deregister()`` in ``destructor()``. The plugin is still responsible for
deleting the sets it created.

Section 6: Using the LDMS Message Service in Sampler Plugins
=============================================================

Terminology
-----------

**LDMS Message Bus** (declared in ``ldms_msg.h``, API prefix ``ldms_msg_``)
   The pub/sub routing layer in LDMS. Each message is published with a tag name;
   subscribers register callbacks against a tag name or regex pattern to receive
   matching messages. The Message Bus manages the routing and delivery of messages
   to authorized subscribers. See ``ldms_msg``\(7).

**Message channel** (declared in ``ldms_msg_chan.h``, API prefix ``ldms_msg_chan_``)
   A resilient mechanism that sits on top of the LDMS Message Bus. A message
   channel manages multiple message buses, reconnects if a peer disconnects,
   and queues data while the peer is down. See ``ldms_msg_chan``\(7).

Checking Whether the Message Service Is Enabled
------------------------------------------------

Both APIs require the message service to be enabled. Check with
``ldms_msg_is_enabled()`` before using either API:

.. code-block:: c

   if (!ldms_msg_is_enabled()) {
       /* handle disabled case */
   }

How to handle a disabled message service depends on the plugin:

- If the plugin requires the message service to function at all, return an error
  from ``constructor()`` to prevent the plugin from loading.
- If the plugin has modes that do not use the message service, return an error
  from ``config()`` only when the user configures a mode that requires it.

Plugins must not call ``ldms_msg_enable()`` to enable the service themselves.
Whether the message service is enabled is an ldmsd instance-level decision, not a
per-plugin one.

Which API to Use
-----------------

The choice depends on whether the plugin needs to own a connection to a peer.

A plugin that relies on connections already managed by ldmsd uses the LDMS Message
Bus API (``ldms_msg_``) directly — no connection management required.

A plugin that needs its own dedicated and reliable connection to a peer — whether
connecting out to a remote peer or listening for incoming connections — uses the
message channel API (``ldms_msg_chan_``).

.. list-table::
   :widths: 50 50
   :header-rows: 1

   * - Use case
     - API to use
   * - Plugin does not need to own a connection
     - ``ldms_msg_subscribe()`` and/or ``ldms_msg_publish(NULL, ...)``
   * - Plugin needs to own a connection to a peer
     - ``ldms_msg_chan``

Key points:

- Within each case, the plugin can subscribe, publish, or both as needed.
- A single message channel can publish on any number of different tag names.
  A plugin needs at most one channel per peer.
- Without a message channel, if the peer the plugin depends on disconnects,
  there is no reconnect logic and the plugin will not recover.
- A plugin that needs to own a connection only for subscribing should be aware
  that ``ldms_msg_chan`` in subscribe mode creates a listening transport endpoint
  bound to a local port. This is appropriate for plugins that act as a passive
  listener for an incoming publisher connection.

Appropriateness by Plugin Interface
-------------------------------------

The following table summarizes which functions are appropriate to call from each
plugin interface. For threading details and what is safe to call from within the
callback, see ``ldms_msg``\(7) and ``ldms_msg_chan``\(7).

.. list-table::
   :widths: 34 13 13 13 13 14
   :header-rows: 1

   * - API
     - ``constructor()``
     - ``config()``
     - ``sample()``
     - ``destructor()``
     - callback
   * - ``ldms_msg_subscribe()``
     - Yes
     - Yes
     - Not appropriate [#msg1]_
     - Not appropriate [#msg1]_
     - Yes
   * - ``ldms_msg_client_close()``
     - No
     - Yes
     - Not appropriate [#msg1]_
     - Yes
     - Yes
   * - ``ldms_msg_publish(NULL,...)``
     - Yes
     - Yes
     - Yes
     - No
     - Yes [#msg2]_
   * - ``ldms_msg_chan_new()``
     - Yes
     - Yes
     - Not appropriate [#msg1]_
     - No
     - No
   * - ``ldms_msg_chan_publish()``
     - No
     - Yes
     - Yes [#chan1]_
     - No
     - Yes
   * - ``ldms_msg_chan_close()``
     - No
     - Yes
     - Not appropriate [#msg1]_
     - Yes
     - Yes [#chan2]_
   * - ``ldms_msg_chan_set_q_limit()``
     - Yes
     - Yes
     - No
     - No
     - No

.. rubric:: Notes

.. [#msg1] Not a thread safety issue. These are lifecycle operations that belong
   in ``constructor()`` and ``config()``, not in ``sample()`` or ``destructor()``
   per call.

.. [#msg2] ``ldms_msg_publish(NULL, ...)`` invokes subscriber callbacks inline
   before returning. Avoid holding plugin-internal locks across this call inside a
   callback — see ``ldms_msg``\(7).

.. [#chan1] If adding the message would exceed the queue limit,
   ``ldms_msg_chan_publish()`` returns ``ENOBUFS``. Adjust the queue limit with
   ``ldms_msg_chan_set_q_limit()`` if needed.

.. [#chan2] Avoid calling ``ldms_msg_chan_close()`` with ``cancel=0`` from within
   the callback — it will block the IO thread for the duration of teardown. Use
   ``cancel=1`` if closing from within a callback.

Callback
---------

For the LDMS Message Bus API, the callback is invoked by whichever thread called
``ldms_msg_publish()`` for messages published on the same bus, or by an IO thread
for messages arriving from a remote peer. The ``LDMS_MSG_EVENT_CLIENT_CLOSE``
event is delivered to signal that teardown is complete and resources may be freed.

For the message channel API, the callback is always invoked by an IO thread.
``LDMS_MSG_EVENT_CLIENT_CLOSE`` is handled internally by the channel and is not
delivered to the application callback.

For full details on callback threading, what is safe to call, and teardown
patterns, see ``ldms_msg``\(7) and ``ldms_msg_chan``\(7).

Section 7: LDMSD Internal Implementation Details (For Maintainers Only)
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

  - Set the state to ``TERMINATING``, then release the lock
  - The registered sets are released afterwards, under ``set_list_lock`` only
    (see below), without the cfgobj lock
  - The cfgobj lock is not re-acquired before the cfgobj is removed from the tree

The set list lock
~~~~~~~~~~~~~~~~~~

Each sampler cfgobj has a ``set_list_lock`` that protects its ``set_list`` and the
``set_list_closed`` flag. It is independent of the cfgobj lock and of the LDMS set
tree lock. It is taken by:

- ``ldmsd_set_register()`` and ``ldmsd_set_deregister()``, which may be called with
  or without the cfgobj lock held
- ``start``, to apply the update hint to the registered sets (the cfgobj lock is
  held)
- the ``plugn_sets`` handler, to list the sets
- ``term``, to detach the list and set ``set_list_closed``

The set references are always dropped after ``set_list_lock`` is released.
``term`` sets ``set_list_closed`` so that a plugin thread racing with ``term``
cannot register a set after the list has been drained; ``ldmsd_set_register()``
returns ``EBUSY`` in that case.

SEE ALSO
=========

:ref:`ldmsd(8) <ldmsd>` :ref:`ldmsd_controller(8) <ldmsd_controller>` :ref:`ldms_msg(7) <ldms_msg>` :ref:`ldms_msg_chan(7) <ldms_msg_chan>`
