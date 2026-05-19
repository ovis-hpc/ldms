.. _ldms_python_threading_model:

======================================================
LDMS Python API Threading Model and Callback Contract
======================================================

.. contents:: Table of Contents
   :local:
   :depth: 2

Section 1: The Threading Model
================================

LDMS maintains a pool of I/O threads internally. These threads handle all
network communication — sending, receiving, and delivering events. When you
supply a callback to an LDMS Python API, that callback is called from one of
these I/O threads, not from your application thread.

This means two things:

**Your callback runs concurrently with your application.** If your callback
reads or writes data that your application thread also accesses, you need to
protect that data with a lock or use a thread-safe data structure.

**Your callback runs on a thread that LDMS needs to keep available.** If your
callback takes a long time — waiting on I/O, sleeping, or calling back into
LDMS in a way that waits for a response — you are tying up an I/O thread that
LDMS needs to deliver further events. In the worst case this leads to a
deadlock. See :ref:`two_rules`.

.. note::

   For developers familiar with the C API: the Python callbacks are called from
   the same zap I/O threads that drive the C-level event handlers. Python's GIL
   is acquired before your callback is called and released when it returns, so
   only one Python callback runs at a time across all I/O threads.

.. _two_rules:

Section 2: Two Rules for Every Callback
=========================================

These rules apply to every callback in the API without exception.

Rule 1 — Do Not Call Blocking LDMS APIs from a Callback
---------------------------------------------------------

The following methods wait internally for LDMS to deliver a response event
before returning:

.. list-table::
   :header-rows: 1
   :widths: 40 60

   * - Method
     - What it waits for
   * - ``Xprt.connect()`` (no ``cb``)
     - Connection accepted or rejected
   * - ``Xprt.accept()``
     - Incoming connection
   * - ``Xprt.dir()`` (no ``cb``)
     - Directory reply
   * - ``Xprt.lookup()`` (no ``cb``)
     - Lookup reply
   * - ``Xprt.recv()``
     - Incoming message
   * - ``Set.update()`` (no ``cb``)
     - Update complete
   * - ``Xprt.msg_subscribe()`` (no ``cb``)
     - Subscribe status
   * - ``Xprt.msg_unsubscribe()`` (no ``cb``)
     - Unsubscribe status

Calling any of these from inside a callback leads to a deadlock, because the
response event they are waiting for can only be delivered by the same I/O
thread that is currently running your callback.

Each of these methods has a non-blocking form: pass a ``cb`` argument and the
method returns immediately. The result is delivered to your callback when it is
ready. Use the non-blocking form whenever you need to make further LDMS calls
from within a callback.

Rule 2 — Do Not Rely on Exceptions to Signal Errors
-----------------------------------------------------

If your callback raises an exception, it is printed to stderr and discarded.
It does not propagate to your application and does not stop the I/O thread.
Handle errors inside the callback directly — for example, by recording the
error in a shared variable or signaling your application thread.

The one exception to this rule is the ``MessageChannel`` receive callback,
which is wrapped in error handling so that exceptions are printed and the I/O
thread always continues cleanly. All other callbacks have no such protection.

Section 3: Blocking and Non-Blocking Mode
==========================================

Most LDMS operations can be used in either blocking or non-blocking mode.

In **blocking mode**, you call the method with no callback. The call does not
return until the operation is complete. This is simpler to write and easier to
reason about, but it ties up your application thread while it waits.

.. code-block:: python

   # Blocking — waits until the directory result is ready
   sets = xprt.dir()

In **non-blocking mode**, you supply a callback. The method returns immediately
and your callback is called from an I/O thread when the result is ready.

.. code-block:: python

   # Non-blocking — returns immediately; result delivered to callback
   def on_dir(xprt, status, dir_data, arg):
       ...

   xprt.dir(cb=on_dir, cb_arg=my_arg)

The sections below describe both modes for each callback, including what
objects are returned or delivered and how long they remain valid.

Section 4: Transport API Callbacks
====================================

Transport Event Callback
--------------------------

**Registered via:** ``Xprt.connect(cb=..., cb_arg=...)`` or
``Xprt.listen(cb=..., cb_arg=...)``

Transport events cover the full lifecycle of a connection: establishment, data
receipt, and disconnection. The same callback signature is used whether you are
connecting as a client or listening as a server.

.. code-block:: python

   def my_xprt_cb(xprt, ev, arg):
       pass

- ``xprt`` — the ``Xprt`` object the event is for
- ``ev`` — an ``XprtEvent`` describing what happened. ``ev.type`` is one of:
  ``EVENT_CONNECTED``, ``EVENT_REJECTED``, ``EVENT_ERROR``,
  ``EVENT_DISCONNECTED``, ``EVENT_RECV``, ``EVENT_SEND_COMPLETE``,
  ``EVENT_SEND_QUOTA_DEPOSITED``, ``EVENT_SET_DELETE``
- ``arg`` — the ``cb_arg`` you supplied

**One behavioral difference between client and server:** On the server side
(registered via ``listen()``), the ``xprt`` delivered on ``EVENT_CONNECTED``
is the **newly accepted connection**, not the listening transport. All
subsequent events for that connection — including ``EVENT_RECV`` and
``EVENT_DISCONNECTED`` — also arrive through this same callback with the
accepted transport as ``xprt``.

**In non-blocking mode:** ``ev`` and its contents are valid for the lifetime of
your callback and safe to retain beyond it. For ``EVENT_RECV``, the received
data is available as a Python ``bytes`` object in ``ev`` and is safe to keep
after the callback returns.

**In blocking mode:** There is no single blocking call for all transport events.
Instead, individual blocking methods cover specific events:

- ``Xprt.connect()`` (no ``cb``) blocks until ``EVENT_CONNECTED``,
  ``EVENT_REJECTED``, or ``EVENT_ERROR``
- ``Xprt.accept()`` blocks until an incoming connection is ready, returning an
  ``Xprt`` object for the new connection
- ``Xprt.recv()`` blocks until ``EVENT_RECV``, returning the received data as
  ``bytes``

Directory Callback
-------------------

**Registered via:** ``Xprt.dir(cb=..., cb_arg=...)``

A directory operation asks a remote peer for the list of sets it publishes. The
result may arrive in multiple callbacks if the peer has many sets.

.. code-block:: python

   def my_dir_cb(xprt, status, dir_data, arg):
       pass

- ``xprt`` — the ``Xprt`` the dir request was issued on
- ``status`` — 0 on success, non-zero on error
- ``dir_data`` — a ``DirData`` object whose ``set_data`` field is a list of
  ``DirSet`` objects, one per published set. ``dir_data.more`` is 1 if
  additional callbacks will follow for this request
- ``arg`` — the ``cb_arg`` you supplied

**In non-blocking mode:** The ``DirSet`` objects in ``dir_data.set_data`` are
plain Python objects and are safe to retain after the callback returns. You do
not need to copy any data before returning.

**In blocking mode:** ``Xprt.dir()`` returns a Python list of ``DirSet``
objects once all results have arrived. The list is fully owned by the caller.

Lookup Callback
----------------

**Registered via:** ``Xprt.lookup(name, flags, cb=..., cb_arg=...)``

A lookup operation retrieves a handle to one or more remote sets by name,
schema, or regular expression. The result may arrive in multiple callbacks if
multiple sets match.

.. code-block:: python

   def my_lookup_cb(xprt, status, more, lset, arg):
       pass

- ``xprt`` — the ``Xprt`` the lookup was issued on
- ``status`` — 0 on success, non-zero on error
- ``more`` — 1 if more matching sets will follow; 0 if this is the last result
- ``lset`` — a ``Set`` object for the matched set, or ``None`` on error
- ``arg`` — the ``cb_arg`` you supplied

**In non-blocking mode:** ``lset`` is a Python object safe to retain beyond the
callback. To read its current metric values you still need to call
``Set.update()`` — the lookup only gives you a handle, not the data.

**In blocking mode:** ``Xprt.lookup()`` returns a single ``Set`` object, or a
list of ``Set`` objects if the lookup matched multiple sets. The returned
objects are fully owned by the caller.

Update Callback
----------------

**Registered via:** ``Set.update(cb=..., cb_arg=...)``

An update fetches the current metric values of a remote set. Multiple callbacks
may follow a single ``update()`` call if the ``UPD_F_MORE`` flag is set.

.. code-block:: python

   def my_update_cb(lset, flags, arg):
       pass

- ``lset`` — the ``Set`` whose data was updated
- ``flags`` — bitwise OR of status flags:

  - ``UPD_F_MORE`` — more update callbacks will follow for this request
  - ``UPD_F_PUSH`` — this update was triggered by a push from the remote peer
  - ``UPD_F_PUSH_LAST`` — last push update; the push stream has ended
  - Use ``LDMS_UPD_ERROR(flags)`` to extract any error code

- ``arg`` — the ``cb_arg`` you supplied

**In non-blocking mode:** ``lset``'s metric values are current and stable
during your callback. Copy any values you intend to use after the callback
returns, as a subsequent update may overwrite them.

**In blocking mode:** ``Set.update()`` returns once the update is complete. The
``Set`` object's metric values are current at the point of return and remain
stable until the next ``update()`` call.

Push Callback
--------------

**Registered via:** ``Set.subscribe_push(cb=..., cb_arg=...)``

Push delivers metric updates initiated by the remote peer rather than by your
own ``update()`` calls. There is no blocking mode for push — updates are always
delivered via callback.

.. code-block:: python

   def my_push_cb(lset, flags, arg):
       pass

- ``lset`` — the ``Set`` whose data was pushed
- ``flags`` — same meaning as in the update callback. ``UPD_F_PUSH`` is always
  set. ``UPD_F_PUSH_LAST`` means the remote peer has ended the push stream
- ``arg`` — the ``cb_arg`` you supplied

The same rules about metric value lifetime apply as in the update callback.

Section 5: Message API Callbacks
==================================

Message Client Callback
-------------------------

**Registered via:** ``MsgClient(match, is_regex, cb=..., cb_arg=...)``

A ``MsgClient`` subscribes to messages published on the message bus that match
a name or pattern. There is no blocking mode — messages are always delivered
via callback.

.. code-block:: python

   def my_msg_client_cb(client, msg_data, arg):
       pass

- ``client`` — the ``MsgClient`` that received the message
- ``msg_data`` — a ``MsgData`` object with message content, source address,
  credentials, and metadata. Safe to retain beyond the callback
- ``arg`` — the ``cb_arg`` you supplied

Your callback is only invoked for received messages. The event that signals the
client has been closed is handled internally and is not forwarded to your
callback.

Message Subscribe Status Callback
-----------------------------------

**Registered via:** ``Xprt.msg_subscribe(match, is_regex, cb=..., cb_arg=...)``

This callback confirms whether a remote subscription request succeeded or
failed. It is called at most once per ``msg_subscribe()`` call.

.. code-block:: python

   def my_msg_subscribe_cb(ev, arg):
       pass

- ``ev`` — a ``MsgStatusEvent`` with fields ``match`` (str), ``is_regex``
  (int), and ``status`` (int; 0 = success)
- ``arg`` — the ``cb_arg`` you supplied

**In non-blocking mode:** ``ev`` is a Python object safe to retain beyond the
callback.

**In blocking mode:** ``Xprt.msg_subscribe()`` blocks until the status is
received and returns normally on success, or raises ``MsgSubscribeError`` on
failure. No callback object is delivered.

MessageChannel Receive Callback
---------------------------------

**Registered via:** ``MessageChannel(..., msg_cb_fn=..., msg_cb_arg=...)``

A ``MessageChannel`` provides a resilient, bidirectional message interface that
manages its own transport connections automatically. There is no blocking mode
— messages are always delivered via callback.

.. code-block:: python

   def my_chan_cb(msg_data, arg):
       pass

- ``msg_data`` — a ``MsgData`` object with message content, source address,
  credentials, and metadata. Safe to retain beyond the callback
- ``arg`` — the ``msg_cb_arg`` you supplied

This is the only callback where an exception raised in your code is handled
safely — it is printed to stderr and the I/O thread continues. For all other
callbacks, follow :ref:`Rule 2 <two_rules>` and handle errors inside the
callback directly.

Section 6: Summary
====================

.. list-table::
   :header-rows: 1
   :widths: 25 30 20 25

   * - Callback
     - Registered via
     - Has blocking mode
     - Multiple deliveries
   * - Transport event
     - ``connect(cb=...)`` or ``listen(cb=...)``
     - Yes — ``connect()``, ``accept()``, ``recv()``
     - Yes — one per event type
   * - Directory
     - ``xprt.dir(cb=...)``
     - Yes — returns a list
     - Yes — until ``more=0``
   * - Lookup
     - ``xprt.lookup(cb=...)``
     - Yes — returns a ``Set`` or list
     - Yes — until ``more=0``
   * - Update
     - ``set.update(cb=...)``
     - Yes — blocks until complete
     - Yes — until ``UPD_F_MORE`` clear
   * - Push
     - ``set.subscribe_push(cb=...)``
     - No
     - Yes — until ``UPD_F_PUSH_LAST``
   * - Message client
     - ``MsgClient(cb=...)``
     - No
     - Yes — one per message
   * - Msg subscribe status
     - ``xprt.msg_subscribe(cb=...)``
     - Yes — blocks until status received
     - No — one-shot
   * - MessageChannel recv
     - ``MessageChannel(msg_cb_fn=...)``
     - No
     - Yes — one per message

SEE ALSO
=========

:ref:`ldms_xprt_threading_model(7) <ldms_xprt_threading_model>` :ref:`ldms_msg(7) <ldms_msg>` :ref:`ldms_msg_chan(7) <ldms_msg_chan>`
