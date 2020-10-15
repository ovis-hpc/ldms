NOTE on `zap_thread`
===================

zap handles endpoint IO via `zap_io_thread`. The following outlines the
interactions among libzap, `zap_io_thread`, zap transport plugin, and zap
endpoints.

1. The application starts and `libzap` is loaded. `zap_io_thread` is not
   created yet.
2. The application loads a transport plugin (e.g. `zap_sock`).
   `zap_transport_get()` is called, but no `zap_io_thread` is created yet.
3. The application creates a zap endpoint (`zap_new()`).
4. The application calls `zap_connect()` or `zap_listen()`. The transport plugin
   shall call `zap_io_thread_ep_assign()` in its implementation of `zap->new()`
   and `zap->listen()` respectively (libzap service function) telling
   libzap to find the least-busy `zap_io_thread` and assign the endpoint to it.
   In addition, when a new passive endpoint is created (by connection request),
   the transport plugin shall also call `zap_io_thread_ep_assign()` to assign
   the endpoint to the least-busy thread.
   - If there is no `zap_io_thread` or the least-busy thread is too busy, libzap
     asks the transport plugin to create a new thread by calling
     `zap->io_thread_create()`, and the transport plugin shall:
     - allocate a `zap_io_thread` structure (or an extension of it),
     - call `zap_io_thread_init()` to initialize the `zap_io_thread` structure,
     - perform additional transport-specific IO thread initialization,
     - create and start a thread, and
     - returns the `zap_io_thread` handle.
   - The purpose of the `zap_io_thread` is to process IO events from the
     associated endpoints.
   - `zap_io_thread` is transport-specific. For example, the `zap_io_thread`
     created by `zap_sock` won't be assigned to `zap_rdma` endpoint.
   - When libzap obtained the least-busy thread, libzap add the endpoint into
     the endpoint list in the `zap_io_thread` structure. Then, it notifies the
     transport plugin about the assignment by calling
     `zap->io_thread_ep_assign()`. The transport plugin then add the endpoint
     into its back-end notification system (e.g. `EPOLL_CTL_ADD`) which shall
     notify the associated thread when an event is ready to be processed on the
     endpoint.
5. When the `zap_io_thread` wakes up, the transport plugin shall call
   `zap_thrstat_wait_end()`. When the thread is done processing the events, it
   shall call `zap_thrstat_wait_start()` before sleeping. The timestamps from
   `zap_thrstat_wait_end()` and `zap_thrstat_wait_start()` are used to calculate
   the busy-ness of the thread.
6. REMARK: Calling `ep->cb()` directly invokes the application callback
   function. zap event application callback interposer is eliminated.
7. The transport plugin shall call `zap_io_thread_ep_release()` after the
   endpoint is no longer used, i.e. after `DISCONNECTED` or `CONNECT_ERROR` is
   delivered to the application. By calling `zap_io_thread_ep_release()`, libzap
   removes the endpoint from the thread and notifies the transport plugin by
   calling `zap->io_thread_ep_release()`. The transport plugin then removes the
   endpoint from its notification system (e.g. `EPOLL_CTL_DEL`).
8. In the current implementation, the thread is not terminated when all
   endpoints leave the thread. If we terminate the thread right away, in the
   case that an `ldmsd` aggregator collecting data from N other daemons that
   were not up yet, new threads will be created at connect time, and will all be
   destroyed on connect errors. The application will create-destroy threads in
   loop.
9. However, `zap_term(z)` call by application will terminate all
   `zap_io_threads` in the transport plugin. libzap will call transport plugin
   `zap->io_thread_cancel()` to request the thread cancellation. The transport
   shall call `zap_io_thread_release()` right before the thread exited to
   release resources allocated by `zap_io_thread_init()`.

SEE ALSO
========
- `zap_priv.h`
  - `struct zap_ep`
  - `struct zap`
  - `struct zap_io_thread`
- `zap.c`
  - `zap_io_thread_init()`
  - `zap_io_thread_release()`
  - `zap_io_thread_ep_assign()`
  - `zap_io_thread_ep_release()`
- `zap_sock.{c,h}`
  - `sock_ev_cb()`
  - `io_thread_proc()`
  - `z_sock_connect()`
  - `z_sock_listen()`
  - `__z_sock_conn_request()`
  - `z_sock_io_thread_create()`
  - `z_sock_io_thread_ep_assign()`
  - `z_sock_io_thread_ep_release()`
  - `z_sock_io_thread_cancel()`
