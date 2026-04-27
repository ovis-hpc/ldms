.. _ldms_xprt_threading_model:

==============================================
LDMS Transport Threading Model and Callback Safety
==============================================

A developer-facing reference for the threading model and callback safety rules
of LDMS transport (``ldms_xprt``) and rails.

.. contents:: Table of Contents
   :local:
   :depth: 2

Section 1: Overview
====================

Terminology
-----------

- **Rail bundle** — the transport handle (``ldms_t``). Internally
  ``ldms_rail_s``. This is the only transport object the application creates,
  connects, and operates on. It bundles *n* parallel connections to the same
  peer.
- **Rail endpoint** — one connection inside a rail bundle
  (``ldms_rail_ep_s`` internally). The application never creates or holds rail
  endpoints directly; they are managed by the rail bundle.
- **Event callback** — the ``ldms_event_cb_t`` function registered when calling
  ``ldms_xprt_connect()`` or ``ldms_xprt_listen()``. The rail bundle calls this
  function to notify the application of connection and data events.
- **Terminal event** — an event after which no further events will be delivered
  on that rail bundle. The terminal events are ``LDMS_XPRT_EVENT_DISCONNECTED``,
  ``LDMS_XPRT_EVENT_REJECTED``, and ``LDMS_XPRT_EVENT_ERROR``. After a terminal
  event, the rail bundle handle cannot be used for data operations or
  reconnection. See `Terminal Events and Cleanup`_ for details.

What is a Rail Bundle?
----------------------

A rail bundle consists of *n* parallel connections to the same peer over the
same transport type (e.g., Socket, RDMA), and *n* can be 1. All connections
are established and torn down together. To the application, the rail bundle
behaves as a single transport handle — it is created with one call, connected
with one call, and delivers all events through one callback function.

``ldms_xprt_new()`` and ``ldms_xprt_new_with_auth()`` are wrappers around
``ldms_xprt_rail_new()`` with *n* = 1. There is no way to create a bare
``ldms_xprt`` — the rail bundle is the only transport object applications see.

The threading model described in this document applies regardless of the number
of endpoints, including a single endpoint (*n* = 1).

Section 2: Threading Guarantees
================================

Each rail endpoint runs on an I/O thread. Within the same rail bundle, each
endpoint is assigned to a different thread. A thread may, however, be shared
across endpoints belonging to different rail bundles.

The consequences for callback delivery:

- **n > 1**: Two events of the same rail bundle may arrive concurrently on
  different threads. The application must protect any shared state accessed in
  the callback path.
- **n = 1**: Event callbacks are delivered serially on a single I/O thread.

The application can discover which threads are assigned to a rail's endpoints by
calling ``ldms_xprt_get_threads()``.

Section 3: Creating and Connecting
====================================

Creation
--------

.. code-block:: c

   /* Multi-endpoint rail */
   ldms_t x = ldms_xprt_rail_new("sock", 4,
                                  LDMS_UNLIMITED, LDMS_UNLIMITED,
                                  "munge", NULL);

Parameters:

- ``"sock"`` — transport type name (e.g., ``"sock"``, ``"rdma"``,
  ``"ugni"``).
- ``4`` — number of rail endpoints (*n*).
- ``LDMS_UNLIMITED`` (first) — send quota; ``LDMS_UNLIMITED`` means no send
  quota limit.
- ``LDMS_UNLIMITED`` (second) — receive rate limit; ``LDMS_UNLIMITED`` means
  no receive rate limit.
- ``"munge"`` — authentication plugin name.
- ``NULL`` — authentication plugin parameters (NULL for defaults).

.. code-block:: c

   /* Single-endpoint rail */
   ldms_t x = ldms_xprt_new_with_auth("sock", "munge", NULL);

``ldms_xprt_new_with_auth(name, auth, auth_args)`` calls
``ldms_xprt_rail_new()`` with *n* = 1 and both the send quota and receive
rate limit set to ``LDMS_UNLIMITED``. ``ldms_xprt_new(name)`` is equivalent
to ``ldms_xprt_new_with_auth(name, "none", NULL)``.

All creation functions return a handle with one internal reference tagged
``"rail_ref"``. Application must call ``ldms_xprt_put(x, "rail_ref")``, where
``x`` is the transport handle, in the cleanup path.

Connecting
----------

Two functions connect a rail bundle to a peer:

- ``ldms_xprt_connect_by_name(x, host, port, cb, cb_arg)`` — resolves the
  host by name.
- ``ldms_xprt_connect(x, sa, sa_len, cb, cb_arg)`` — takes a
  ``struct sockaddr`` directly.

Both support asynchronous and synchronous modes, described below.

Synchronous Connect
~~~~~~~~~~~~~~~~~~~~

When ``cb`` is ``NULL``, the connect function blocks until the connection
succeeds or fails and returns a zero or non-zero error code, respectively.

.. code-block:: c

   int rc = ldms_xprt_connect_by_name(x, host, port, NULL, NULL);
   if (rc) {
       /* connection failed */
       ldms_xprt_put(x, "rail_ref");
   }

Asynchronous Connect
~~~~~~~~~~~~~~~~~~~~~

When ``cb`` is not ``NULL``, the connect function submits the connection
requests and returns immediately. A zero return value means all connection
requests were submitted successfully, but the connection is not yet
established. The application must wait for the ``LDMS_XPRT_EVENT_CONNECTED``
event before using the rail bundle for data operations. If the connection
fails, a terminal event (``LDMS_XPRT_EVENT_ERROR``,
``LDMS_XPRT_EVENT_REJECTED``, or ``LDMS_XPRT_EVENT_DISCONNECTED``) is delivered
instead.

.. code-block:: c

   int rc = ldms_xprt_connect_by_name(x, host, port, my_event_cb, my_arg);
   if (rc) {
       /*
        * synchronous failure — no callback will fire.
        * Release the creation reference to eventually free the handle.
        */
       ldms_xprt_put(x, "rail_ref");
       return rc;
   }
   /*
    * Wait for LDMS_XPRT_EVENT_CONNECTED or a terminal event
    * (LDMS_XPRT_EVENT_REJECTED, LDMS_XPRT_EVENT_ERROR) via the callback.
    */

On synchronous failure (non-zero return), no connection was established and no
callback will fire. The application must release the handle with
``ldms_xprt_put(x, "rail_ref")`` as shown above.

On success (zero return), LDMS manages its own internal references to keep the
rail bundle alive until a terminal event fires. The application does not need to
take an additional reference for normal use (see
`Section 6: Reference Counting`_).

Handle Is Not Reconnectable
----------------------------

After a terminal event, the ``ldms_t`` handle cannot be reused. Calling
``ldms_xprt_connect()`` or ``ldms_xprt_connect_by_name()`` on it returns
``EINVAL``. To reconnect, create a new rail bundle.

Section 4: Passive Side (Listener)
====================================

Listening
---------

A listening rail bundle is created with ``ldms_xprt_new_with_auth()`` or
``ldms_xprt_rail_new()`` and then bound to an address with one of the listen
functions. Only one rail endpoint is created for the listener regardless of the
value of *n* passed to the creation function.

.. code-block:: c

   ldms_t listener = ldms_xprt_new_with_auth("sock", "munge", NULL);
   int rc = ldms_xprt_listen_by_name(listener, host, port,
                                      my_event_cb, my_arg);

Two functions start listening:

- ``ldms_xprt_listen_by_name(x, host, port, cb, cb_arg)`` — resolves the host
  by name.
- ``ldms_xprt_listen(x, sa, sa_len, cb, cb_arg)`` — takes a
  ``struct sockaddr`` directly.

Changing the Callback
---------------------

``ldms_xprt_event_cb_set(x, new_cb, new_arg)`` replaces the callback and its
argument on a rail bundle. The assignment is not lock-protected. If events are
being delivered concurrently during the assignment, some events may be delivered
to the old callback and some to the new callback.

Incoming Connections
--------------------

When the application accepts a connection request, LDMS creates a new rail
bundle for that connection. The number of rail endpoints in the new handle
follows the connecting peer's number of rail endpoints. The application's
callback receives ``LDMS_XPRT_EVENT_CONNECTED`` with:

- ``x`` set to the new rail bundle handle (not the listener handle).
- ``cb_arg`` inherited from the listener's ``cb_arg``.

Legacy Peer Interoperability
-----------------------------

When a rail-based listener receives a connection from a legacy (pre-rail) LDMS
peer, LDMS wraps the incoming connection in a single-endpoint rail
transparently. The application does not need to handle this case differently.

Section 5: The Event Callback
==============================

Signature
---------

.. code-block:: c

   void event_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg);

- ``x`` — the rail bundle handle. Always the same handle that was passed to
  ``ldms_xprt_connect()``, or the new handle created for an incoming connection
  on the passive side. Never a rail endpoint.
- ``e`` — the event (see table below).
- ``cb_arg`` — the application-supplied argument passed at connect/listen time.

No LDMS locks are held when the callback fires. The application is free to call
LDMS API functions from within the callback. After receiving a terminal event,
the application must not perform any LDMS operations on the handle (see
`Terminal Events and Cleanup`_).

Blocking in the callback is not forbidden, but it delays delivery of subsequent
events on that rail endpoint. Keep the callback short and defer heavy work to
another thread if needed.

Event Types
-----------

The following table lists all event types delivered through the callback. Each
event type is described in detail in the subsections that follow.

.. list-table::
   :header-rows: 1
   :widths: 25 10 65

   * - Event type
     - ``e->data``
     - Meaning
   * - ``LDMS_XPRT_EVENT_CONNECTED``
     - ``NULL``
     - Rail bundle is fully connected and ready.
   * - ``LDMS_XPRT_EVENT_REJECTED``
     - ``NULL``
     - Connection was rejected by the peer. Terminal event.
   * - ``LDMS_XPRT_EVENT_ERROR``
     - ``NULL``
     - A connection attempt failed. Terminal event.
   * - ``LDMS_XPRT_EVENT_DISCONNECTED``
     - ``NULL``
     - Connection is disconnected. Terminal event.
   * - ``LDMS_XPRT_EVENT_RECV``
     - message bytes
     - A message sent by the peer via ``ldms_xprt_send()`` arrived.
   * - ``LDMS_XPRT_EVENT_SEND_COMPLETE``
     - ``NULL``
     - A send operation completed.
   * - ``LDMS_XPRT_EVENT_SET_DELETE``
     - ``e->set_delete``
     - A looked-up set was deleted at the peer.
   * - ``LDMS_XPRT_EVENT_SEND_QUOTA_DEPOSITED``
     - ``e->quota``
     - The peer returned send quota.

LDMS_XPRT_EVENT_CONNECTED
~~~~~~~~~~~~~~~~~~~~~~~~~~

Fires only after all *n* endpoints in the rail bundle have connected
successfully. ``e->data`` is ``NULL``. The application may begin data
operations (send, lookup, etc.) after receiving this event.

LDMS_XPRT_EVENT_RECV
~~~~~~~~~~~~~~~~~~~~~

A message sent by the peer via ``ldms_xprt_send()`` arrived. ``e->data``
contains the message bytes and ``e->data_len`` gives the length.

- **Lifetime**: ``e->data`` is owned by the LDMS transport and is freed when
  the callback returns. The application must copy the data in the callback if
  it needs the data after the callback returns.
- **Ordering**: ``LDMS_XPRT_EVENT_RECV`` events are delivered in the same order
  as the peer sent the corresponding messages.

LDMS_XPRT_EVENT_SEND_COMPLETE
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A previous ``ldms_xprt_send()`` has been submitted to the underlying transport.
This does not guarantee that the data has arrived on the peer. ``e->data`` is
``NULL``.

LDMS_XPRT_EVENT_SET_DELETE
~~~~~~~~~~~~~~~~~~~~~~~~~~~

A metric set that the application previously looked up has been deleted at the
peer. ``e->set_delete`` contains the local set handle and name. The
application should release its reference to the set and stop updating it.

LDMS_XPRT_EVENT_SEND_QUOTA_DEPOSITED
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The peer returned send quota. ``e->quota`` contains the current quota value,
endpoint index, and return code. Applications that do not use quota-aware send
logic can safely ignore this event.

Terminal Events and Cleanup
----------------------------

``LDMS_XPRT_EVENT_DISCONNECTED``, ``LDMS_XPRT_EVENT_REJECTED``, and
``LDMS_XPRT_EVENT_ERROR`` are terminal events. After the application receives a
terminal event, no further events will be delivered on that rail bundle.

``LDMS_XPRT_EVENT_DISCONNECTED`` indicates the connection was closed, either by
the peer or by a call to ``ldms_xprt_close()`` on the active side.

``LDMS_XPRT_EVENT_REJECTED`` indicates the peer refused the connection.

``LDMS_XPRT_EVENT_ERROR`` indicates a connection attempt failed. This event is
delivered only before the connection is established. After
``LDMS_XPRT_EVENT_CONNECTED`` has fired, transport-level errors result in
``LDMS_XPRT_EVENT_DISCONNECTED``, not ``LDMS_XPRT_EVENT_ERROR``.

When the application receives a terminal event, it should:

1. Clean up application resources associated with the rail bundle (e.g, free
   ``cb_arg`` structures, notify other threads, etc.).
2. Release any additional references the application holds by calling
   ``ldms_xprt_put()`` with the matching name for each ``ldms_xprt_get()``
   the application previously called (see `Section 6: Reference Counting`_).
3. Not perform any LDMS operations on the handle. Internal resources are
   already being released.
4. Release the creation reference by calling ``ldms_xprt_put(x, "rail"ref")``,
   where ``x`` is the handle returned by calling ``ldms_xprt_rail_new()``,
   ``ldms_xprt_new()``, or ``ldms_xprt_new_with_auth()``.
5. Create a new rail bundle if reconnection is needed.

Section 6: Reference Counting
================================

``ldms_xprt_get()`` and ``ldms_xprt_put()`` manage reference-counted references
on the rail bundle. They are safe to call from any thread, including from within
the event callback.

LDMS keeps the rail bundle alive internally from connect time until the terminal
event fires. For normal use, the application does not need to call
``ldms_xprt_get()`` after connect or listen.

When to Take an Extra Reference
---------------------------------

If the application stores the ``ldms_t`` handle in a data structure or passes it
to another context where the handle may be used beyond the lifetime guaranteed by
LDMS (i.e., after the terminal event), it should take a reference with
``ldms_xprt_get()`` to prevent the handle from being freed while the application
still holds a pointer to it.

Each call to ``ldms_xprt_get(x, reason)`` must be paired with a corresponding
``ldms_xprt_put(x, reason)`` using the same reason string. The ``reason``
parameter is a diagnostic tag used for debugging reference leaks and does not
affect behavior.

.. code-block:: c

   /* Taking a reference when storing the handle */
   ldms_xprt_get(x, "my_context");
   my_struct->xprt = x;

   /* Releasing when done — e.g., on terminal event or teardown */
   my_struct->xprt = NULL;
   ldms_xprt_put(x, "my_context");

An unmatched ``ldms_xprt_put()`` will corrupt the reference count and likely
cause a use-after-free or double-free.

Section 7: Lifecycle Sketch
==============================

Active Side (Client)
--------------------

.. code-block:: c

   void my_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg)
   {
       struct my_ctxt *ctxt = cb_arg;

       pthread_mutex_lock(&ctxt->lock);
       switch (e->type) {
       case LDMS_XPRT_EVENT_CONNECTED:
           ctxt->connected = 1;
           break;

       case LDMS_XPRT_EVENT_RECV:
           /* Copy e->data if needed beyond this return */
           handle_message(e->data, e->data_len);
           break;

       case LDMS_XPRT_EVENT_DISCONNECTED:
       case LDMS_XPRT_EVENT_REJECTED:
       case LDMS_XPRT_EVENT_ERROR:
           /* Terminal — clean up application resources. */
           ctxt->connected = 0;
           ldms_xprt_put(x, "rail_ref");
           break;

       default:
           break;
       }
       pthread_mutex_unlock(&ctxt->lock);
   }

   /* --- setup sketch --- */

   struct my_ctxt ctxt = { .lock = PTHREAD_MUTEX_INITIALIZER };

   ldms_t x = ldms_xprt_rail_new("sock", 4,
                                  LDMS_UNLIMITED, LDMS_UNLIMITED,
                                  "munge", NULL);

   int rc = ldms_xprt_connect_by_name(x, "host", "411",
                                      my_cb, &ctxt);
   if (rc) {
       /* synchronous failure — release creation reference to free handle */
       ldms_xprt_put(x, "rail_ref");
       /* handle error */
   }

   /* ... application main loop ... */

   /* Application-initiated teardown (if still connected): */
   pthread_mutex_lock(&ctxt.lock);
   if (ctxt.connected) {
       ldms_xprt_close(x);
       /* A DISCONNECTED callback will follow. */
   }
   pthread_mutex_unlock(&ctxt.lock);

Passive Side (Server)
-----------------------

.. code-block:: c

   void listener_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg)
   {
       switch (e->type) {
       case LDMS_XPRT_EVENT_CONNECTED:
           /* x is a NEW rail bundle for this incoming connection. */
           register_conn(x);
           break;
       case LDMS_XPRT_EVENT_DISCONNECTED:
       case LDMS_XPRT_EVENT_REJECTED:
       case LDMS_XPRT_EVENT_ERROR:
           /* Terminal event for a specific connection. */
           unregister_conn(x);
           ldms_xprt_put(x, "rail_ref");
           break;
       default:
           break;
       }
   }

   /* --- setup sketch --- */

   ldms_t listener = ldms_xprt_new_with_auth("sock", "munge", NULL);
   int rc = ldms_xprt_listen_by_name(listener, "0.0.0.0", "411",
                                     listener_cb, NULL);
   if (rc) {
       ldms_xprt_put(x, "rail_ref");
       return rc;
   }

Section 8: Summary of Safety Rules
=====================================

.. list-table::
   :header-rows: 1
   :widths: 65 35

   * - Rule
     - Applies when
   * - Protect shared ``cb_arg`` state with a mutex
     - *n* > 1
   * - Copy ``e->data`` before the callback returns
     - Always (``LDMS_XPRT_EVENT_RECV``)
   * - Do not perform any LDMS operations after a terminal event
     - Always
   * - Call ``ldms_xprt_put(x, "rail_ref")``
       when the transport handle will not be used again
     - Always
   * - Match each ``ldms_xprt_get()`` with ``ldms_xprt_put()``
     - Always
   * - Keep callbacks short; defer heavy work
     - Recommended

SEE ALSO
=========

:ref:`ldms_msg(7) <ldms_msg>` :ref:`ldms_msg_chan(7) <ldms_msg_chan>`
