.. _ldms_msg:

===========
ldms_msg
===========

----------------------------------------------
publish/subscribe data in LDMS message service
----------------------------------------------

:Date: 2024-12-03
:Manual section: 7
:Manual group: LDMS
:Version: LDMS 4.5

SYNOPSIS
========

ldmsd_controller commands
-------------------------

.. parsed-literal::

   ``prdcr_subscribe`` ``regex``\ =\ `PRDCR_REGEX` ``message_channel``\ =\ `NAME_REGEX`

   ``prdcr_unsubscribe`` ``regex``\ =\ `PRDCR_REGEX` ``message_channel``\ =\ `NAME_REGEX`


C APIs
------

.. code:: c

 #include "ldms.h"

 int ldms_msg_publish(ldms_t x, const char *name,
                         ldms_msg_type_t msg_type,
                         ldms_cred_t cred,
                         uint32_t perm,
                         const char *data, size_t data_len);

 typedef int (*ldms_msg_event_cb_t)(ldms_msg_event_t ev, void *cb_arg);

 ldms_msg_client_t ldms_msg_subscribe(const char *match,
                               int is_regex, ldms_msg_event_cb_t cb_fn,
                               void *cb_arg, const char *desc);
 void ldms_msg_close(ldms_msg_client_t c);

 int ldms_msg_remote_subscribe(ldms_t x, const char *match, int is_regex,
                                  ldms_msg_event_cb_t cb_fn, void *cb_arg,
                                  int64_t rate);
 int ldms_msg_remote_unsubscribe(ldms_t x, const char *match, int is_regex,
                                    ldms_msg_event_cb_t cb_fn, void *cb_arg);

 /* See "ldms.h" for the detailed API documentation */


Python APIs
-----------

.. code:: python

 from ovis_ldms import ldms

 ldms.msg_publish(name=<str>, msg_data=<str|dict>,
             msg_type=<None|ldms.LDMS_MSG_STRING|ldms.LDMS_MSG_JSON>,
             perm=<int>)

 xprt = ldms.Xprt()
 xprt.connect(host="node0", port=411)

 xprt.msg_publish(name=<str>, msg_data=<str|dict>,
             msg_type=<None|ldms.LDMS_MSG_STRING|ldms.LDMS_MSG_JSON>,
             perm=<int>)

 xprt.msg_subscribe(match=<str>, is_regex=<bool>)

 xprt.msg_unsubscribe(match=<str>, is_regex=<bool>)

 cli = ldms.MsgClient(match=<str>, is_regex=<bool>, cb=<callable|None>,
                         cb_arg=<object|None>)
 # MsgClient callback signature
 def cb(MsgClient client, MsgData data, object cb_arg)

 data = cli.get_data()

 cli.close()

 # for more detailed description and usage
 help(ldms)


DESCRIPTION
===========

LDMS Message Service is a service in LDMS for publishing variable-length data to
LDMS proecesses, and for receiving such data from LDMS processes via message
subscription. When the published data arrive at an LDMS process the
`msg_client`'s in the process that are authorized to see data will receive the
data via the callback function `cb_fn()`. If there are remote subscribers on the
LDMS process, the data will be forwarded to them if they are allowed to see the
data.

An LDMS Daemon (``ldmsd``) has to be configured with ``prdcr_subscribe``
commands in order to receive message data from its producers (``prdcr``).
``prdcr_subscribe`` can be issued many times, e.g.

.. code:: sh

 # subscribe "s0" message channel on all producers
 prdcr_subscribe regex=.* msg=s0
 # subscribe "s1" message channel on all producers
 prdcr_subscribe regex=.* msg=s1

The ``msg`` parameter can also be regular expression, e.g.

.. code:: sh

 # subscribe message channels matching "app.*" or "sys.*"
 prdcr_subscribe regex=.* msg=app.*
 prdcr_subscribe regex=.* msg=sys.*

This is the setup for the following figure:

- ``bob_app``: an application run by ``bob``. It LDMS-connects to ``samp``.

- ``samp``: an LDMS daemon (sampler).

  - A plugin in ``samp`` has an LDMS Message Client ``cli`` that subscribes to
    all channels (regex ``.*``).

  - Another plugin ``plug0`` in ``samp`` publishes to ``s1`` channel.

- ``agg``: another LDMS daemon (aggregator). It has an LDMS connection to
  ``samp``.

  - ``agg`` subscribes ``.*`` channels on ``samp`` with the following command:

    - ``prdcr_subscribe regex=samp msg=.*``

- ``alice_app``: an application run by alice that LDMS-conencts to ``agg``.

  - ``alice_app`` subscribe for ``s0``

  - ``alice_app`` has an LDMS Message Client ``cli`` that subscribes to ``"my"``
    channel.

The ``-->`` arrows illustrate possible message data paths.

::

                   ┌──────────────┐         ┌────────┐
 ┌───────────┐     │     samp     │         │  agg   │
 │bob_app    │     ├──────────────┤         ├────────┤
 ├───────────┤     │   .----.     │         │ .----. │
 │           │  .----->|ldms|---------------->|ldms| │
 │publish(s0)│  |  │   '-+-+'<---.│         │ '----' │
 │  |        │  |  │     |       |│         └────|───┘
 │  v        │  |  │.----'       |│      .-------'
 │.----.     │  |  │| .------.   |│      | ┌────────────┐
 │|ldms|--------'  │| |cli:.*|   |│      | │ alice_app  │
 │'----'     │     │| |------|   |│      | ├────────────┤
 └───────────┘     │'>|cb_fn |   |│      | │   .----.   │
                   │  '------'   |│      '---->|ldms|--.│
                   │             |│        │   '----'  |│
                   │             |│        │           |│
                   │.-----------.|│        │           |│
                   │|  plug0    ||│        │  .------. |│
                   │|-----------||│        │  |cli:s0| |│
                   │|publish(s1)|'│        │  |------| |│
                   │'-----------' │        │  |cb_fn |<'│
                   └──────────────┘        │  '------'  │
                                           └────────────┘



``bob_app`` publishes a message by calling ``ldms_msg_publish()`` function.
Let's assume that ``bob_app`` publishes ``s0`` message over the LDMS
transport to ``samp`` with ``0400`` permission.

When ``s0`` message from ``bob_app`` arrives ``samp`` daemon, the logic in
``ldms`` library does the following:

1. **Credential check**: ``ldms`` library checks the credential in the message
   against the credential in the transport. If they are not the same, the
   message is dropped to prevent user impersonation. The exception is that
   ``root`` can impersonate any user so that ``ldmsd``'s can propagate user
   messages as user.

2. **Client iteration**: ``ldms`` library Goes through all clients that
   subscribe to ``s0`` channel (including the macthing clients that subscribe
   with regular expression).

3. **Authorization check**: Then, ``ldms`` library checks if the clients should
   be seeing the data with the credential information in the client, the
   credential and permission information in the message.

4. **Callbak**: clients' ``cb_fn()`` is called for the authorized clients.
   Examples of information availble in the msg callback event are message
   channel name, message data, original publisher's ``uid``, ``gid`` and
   address.  Currently, a user can publish data to any channel. It is up to the
   receiver side to decide what to do.

In this particular case, we will have 2 clients on ``samp``: the ``cli`` that
subscribes for all channels (regex ``.*``), and a *hidden* client for remote
subscription (remote client for short) created when ``samp`` received a
subscription request message from ``agg`` (by ``prdcr_subscribe`` command in
``agg``). The ``cb_fn()`` of the remote client is an internal function in LDMS
library that forwards the message to the subscribing peer. Note that the
credential of the remote client is the credential from the LDMS transport
authentication.

Now, ``s0`` message has reached ``agg``, which has only one remote client:
``alice_app`` subscribing to ``s0`` channel. The ``ldms`` logic in ``agg`` will
NOT forward this particular message to ``alice_app`` because ``bob_app``
the original publisher set ``0400`` permission.

If ``bob_app`` published another message on ``s0`` channel to ``samp`` with
``0444`` permission, when it reached ``agg``, it will be forwarded it to
``alice_app``. ``cb_fn()`` on ``alice_app`` will be called once the ``s0`` data
reached it.

On another path, let's consider ``publish(s1)`` in ``plug0`` plugin in ``samp``
process. When ``plug0`` publishes ``s1`` with ``NULL`` transport (publishing
locally), the ``ldms`` library in ``samp`` process does the same thing as if the
data were received from a remote peer. The ``cli`` client in another plugin that
subscribed for all channels will get the data (via ``cb_fn()``), and the remote
client to ``agg`` will also get the data if authorized.


CREDENTIALS AND PERMISSIONS
===========================

The ``ldms_msg_publish()`` function in C and the ``msg_publish()`` method in
Python both receive credential ``cred`` and permission ``perm``. If ``cred`` is
not set, the process' ``UID/GID`` are used.  If a non-root user tries to
impersonate anotehr user, the ``ldms`` library on the receiver side will drop
the message. We allow ``root`` to impersonate other ``UID/GID`` so that users'
message can be preserved when propagated. Before forwarding the message to the
remote client, the remote client credential is checked if it is allowed to see
the data from ``cred`` with ``perm``.


CODE EXAMPLES
=============

C publish example
-----------------

.. code:: c

 #include "ldms.h"

 int main(int argc, char **argv)
 {
     ldms_t x;
     int rc;
     x = ldms_xprt_new_with_auth("sock", "munge", NULL);
     /* synchronous connect for simplicity */
     rc = ldms_xprt_connect_by_name(x, "node1", "411", NULL, NULL);
     if (rc)
         return rc;

     /* publish to peer */
     rc = ldms_msg_publish(x, "s0", LDMS_MSG_STRING, NULL,
                              0400, "data", 5);

     /* publish to our process */
     rc = ldms_msg_publish(NULL, "json_channel", LDMS_MSG_JSON, NULL,
                              0400, "{\"attr\":\"value\"}", 17);
     return rc;
 }


C subscribe example
-------------------

.. code:: c

 #include <stdio.h>
 #include <unistd.h>
 #include "ldms.h"

 int cb_fn0(ldms_msg_event_t ev, void *cb_arg);
 int success_cb(ldms_msg_event_t ev, void *cb_arg);

 int main(int argc, char **argv)
 {
     int rc;
     ldms_t x;

     /* connect to an ldmsd */
     x = ldms_xprt_new_with_auth("sock", "munge", NULL);
     ldms_xprt_connect_by_name(x, "node1", "411", NULL, NULL);

     /* subscribe "s0" messages that reached us; cb_fn0 is the callback function */
     cli0 = ldms_msg_subscribe("s0", 0, cb_fn0, NULL, "s0 only");


     /* Ask ldmsd to forward "s0" messages to us;
      * There will be NO success report callback since the function is `NULL`. */
     rc = ldms_msg_remote_subscribe(x, "s0", 0, NULL, NULL, LDMS_UNLIMITED);
     if (rc)
         return rc;
     /* The non-zero `rc` is a synchronous error that can still be returned,
      * e.g. EIO, ENOMEM, ENAMETOOLONG. */

     /* ask ldmsd to forward messages with channels matching "app.*" regex to
      * us.  `success_cb()` will be called once we know the result of the
      * subscription. */
     rc = ldms_msg_remote_subscribe(x, "app.*", 1, success_cb, NULL, LDMS_UNLIMITED);
     if (rc)
         return rc;

     sleep(10); /* sleep 10 sec */

     /* Request an unsubscription to "s0" channel. Note that the `match` must
      * match the subscription request. */
     rc = ldms_msg_remote_unsubscribe(x, "s0", 0, success_cb, NULL);
     if (rc)
         return rc;

     /* Request an unsubscription to "app.*" channels. Note that the `match`
      * must match the subscription request. */
     rc = ldms_msg_remote_unsubscribe(x, "app.*", 1, success_cb, NULL);
     if (rc)
         return rc;

     ldms_msg_close(cli0);

     sleep(5); /* wait a bit so that we can see the events */

     return 0;
 }

 int cb_fn0(ldms_msg_event_t ev, void *cb_arg)
 {
     if (ev->type == LDMS_MSG_EVENT_CLOSE) {
         /*
          * The client is "closed". We can clean up resources
          * associated with it here. No more event will occur
          * on this client.
          */
         struct ldms_msg_stats_s *stat;
         stat = ldms_msg_client_get_stats(ev->close.client, 0);
         printf("client closed:\n");
         printf(" - match: %s\n", stat->match);
         printf(" - is_regex: %d\n", stat->is_regex);
         printf(" - desc: %s\n", stat->desc);
         ldms_msg_client_stats_free(stat);
         return 0;
     }
     assert(ev->type == LDMS_MSG_EVENT_RECV);
     /* we expect RECV event or CLOSE event only */
     if (ev->recv.type == LDMS_MSG_STRING) {
         printf("channel name: %s\n", ev->recv.name);
         printf("message data: %s\n", ev->recv.data);
     }
     if (ev->recv.type == LDMS_MSG_JSON) {
         /* process `ev->recv.json` */
     }
 }

 int success_cb(ldms_msg_event_t ev, void *cb_arg)
 {
     switch (ev->type) {
     case LDMS_MSG_EVENT_SUBSCRIBE_STATUS:
         printf("'%s' subscription status: %d\n", ev->status.match,
                                                         ev->status.status);
         break;
     case LDMS_MSG_EVENT_UNSUBSCRIBE_STATUS:
         printf("'%s' unsubscription status: %d\n", ev->status.match,
                                                           ev->status.status);
         break;
     default:
         printf("Unexpected event: %d\n", ev->type);
     }
     return 0;
 }


Python publish examples
-----------------------

.. code:: python

 from ovis_ldms import ldms
 x = ldms.Xprt(name="sock", auth="munge") # LDMS socket transport /w munge
 x.connect(host="node0", port=411)

 # Explicitly specify STRING type.
 x.msg_publish(name="s0", "somedata", msg_type=ldms.LDMS_MSG_STRING,
                  perm=0o400)

 # JSON; the `dict` data will be converted to JSON
 x.msg_publish(name="s0", {"attr": "value"},
                  msg_type=ldms.LDMS_MSG_JSON, perm=0o400)

 # Assumed STRING type if data is `str` or `bytes` when `msg_type` is omitted
 x.msg_publish(name="s0", "somedata", perm=0o400)

 # Assumed JSON type if data is `dict` when `msg_type` is omitted
 x.msg_publish(name="app0", {"attr": "value"}, perm=0o400)

 # We can publish to our process too
 ldms.msg_publish(name="s0", "data")


Python subscribe examples
-------------------------

.. code:: python

 import time
 from ovis_ldms import ldms

 x = ldms.Xprt(name="sock", auth="munge") # LDMS socket transport /w munge
 x.connect(host="node0", port=411)

 def msg_recv_cb(cli, sd, cb_arg):
     print(f"[{sd.name}]: {sd.data}")

 def msg_sub_status_cb(ev, cb_arg):
     print(f"'{ev.name}' subscription status: {ev.status}")

 def msg_unsub_status_cb(ev, cb_arg):
     print(f"'{ev.name}' unsubscription status: {ev.status}")

 # Subscribe to messages that reaches our process on "s0" channel.
 # `msg_recv_cb()` will be called when "s0" message reached our process.
 cli0 = ldms.MsgClient(match="s0", cb=msg_recv_cb, cb_arg=None)

 # Subscribe to messages that reaches our process on channels matching "app.*".
 # Since no `cb` is given, "app.*" data that reaches our process will be
 # stored in cli1.
 cli1 = ldms.MsgClient(match="app.*", is_regex=True)

 # Request peer for message forwarding to us only on channel "s0".
 # The status result of the subscription will be notified via
 #  `msg_sub_status_cb`.
 x.msg_subscribe("s0", cb=msg_sub_status_cb, cb_arg=None)

 # Request peer for message forwarding to us on channels matching "app.*".
 # Since no `cb` is given, this call becomes blocking, waiting for the status
 # event, and returns it.
 ev = x.msg_subscribe("app.*", is_regex=True)
 print(f"'{ev.name}' subscription status: {ev.status}")

 time.sleep(10) # wait a bit to get events

 # "s0" messages were handled by `msg_recv_cb`.

 # Data of "app.*" messges are stored in `cli1` since no `cb` was given.
 sd = cli1.get_data()
 while sd is not None:
     print(f"[{sd.name}]: {sd.data}")
     sd = cli1.get_data()

 # Cancel our "s0" subscription from peer; notify result via `cb`
 x.msg_unsubscribe("s0", cb=msg_unsub_status_cb, cb_arg=None)

 # Cancel our "app.*" subscription from peer; result via return object
 ev = x.msg_unsubscribe("app.*", is_regex=True)
 print(f"'{ev.name}' unsubscription status: {ev.status}")

 # Terminate message clients and the connection
 cli0.close()
 cli1.close()
 x.close()


SEE ALSO
========

**ldmsd_controller**\ (8)
