.. _ldmsd_config_threading:

===================================
ldmsd Configuration Threading Model
===================================

A developer-facing reference covering how configuration commands are delivered
to ldmsd, which thread executes them, and what that means for handler authors.
Also covers the thread pool configuration commands and their constraints.

Audience: ldmsd developers writing or modifying configuration command handlers,
and developers adding new thread pool controls.

.. contents:: Table of Contents
   :local:
   :depth: 2

----

Configuration Command Pipeline
================================

Two Transport Types, One Dispatcher
-------------------------------------

ldmsd accepts configuration commands through two transport types, both
represented by the same ``ldmsd_cfg_xprt_t`` abstraction:

- ``LDMSD_CFG_TYPE_FILE`` — commands read from a configuration file,
  supplied via ``-c`` (text) or ``-y`` (YAML) at daemon startup.
- ``LDMSD_CFG_TYPE_LDMS`` — commands received over an LDMS transport
  connection from an external tool (``ldmsd_controller``, ``ldmsctl``,
  or Maestro).

Regardless of transport type, every command follows the same path after
ingress:

.. code-block:: text

    ldmsd_process_config_request()
        └── ldmsd_handle_request()
                └── request_handler[req_id].handler(reqc)

The transport type affects only two things: which thread runs the handler,
and whether command expansion is permitted (the ``trust`` flag). Everything
inside the dispatcher and the handler itself is identical.

Which Thread Runs Your Handler
-------------------------------

**File-based configuration** (``-c`` / ``-y``) commands run synchronously on
the ldmsd worker thread. The worker thread reads the file, parses each line
into a request, and calls the dispatcher inline. Execution is sequential
within the file.

**In-band configuration** commands arrive as LDMS transport messages. The
receive callback ``ldmsd_recv_msg()`` is invoked on the IO thread handling
that connection, and the dispatcher — and therefore your handler — runs on
that IO thread.

All three external tools (``ldmsd_controller``, ``ldmsctl``, and Maestro)
deliver commands over LDMS transports. From ldmsd's perspective they are
indistinguishable: every command arrives through the same receive path and
executes on the IO thread of its connection.

What the Dispatcher Does Before Calling Your Handler
------------------------------------------------------

``ldmsd_process_config_request()`` assembles multi-record messages using a
red-black tree (``msg_tree``) protected by ``msg_tree_lock``. Large commands
may arrive as multiple records; the tree holds partial messages until the
end-of-message record is received.

The lock is released before ``ldmsd_handle_request()`` is called. By the
time your handler runs, ``msg_tree_lock`` is not held.

``ldmsd_handle_request()`` then performs permission checking against the
inbound credential (for in-band commands only; file-based commands bypass
this check entirely), and dispatches to your handler.

Locking Responsibilities for Handler Authors
---------------------------------------------

Because ``msg_tree_lock`` is released before your handler runs, two
concurrent in-band commands can be in their handlers simultaneously. Your
handler is responsible for acquiring whatever cfgobj-specific locks it needs
to protect shared state.

There is no implicit serialization between:

- Two concurrent in-band config commands arriving on different connections.
- A file-based config command (worker thread) and an in-band command
  (IO thread) running at the same time.

The ``trust`` Flag
-------------------

File-based configuration sets ``trust`` according to the caller. In-band
configuration always sets ``trust = 0``, unconditionally. Trust controls
whether shell-style command expansion is permitted in configuration values.
Do not rely on ``trust`` being non-zero in a handler that may be invoked
over an LDMS transport.

----

SEE ALSO
=========

:ref:`ldmsd(8) <ldmsd>`,
:ref:`ldmsd_controller(8) <ldmsd_controller>`,
:ref:`ldmsd Configuration File Reference(7) <ldmsd_config_files>`,
:ref:`LDMS Transport Threading Model(7) <ldms_xprt_threading_model>`
