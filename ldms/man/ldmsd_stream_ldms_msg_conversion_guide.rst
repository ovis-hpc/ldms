.. _ldmsd_stream_ldms_msg_conversion_guide:

============================
`ldmsd_stream` to `ldms_msg`
============================

----------------
conversion guide
----------------

:Date: 2025-06-23
:Manual section: 7
:Manual group: LDMS
:Version: LDMS 4.5

DESCRIPTION
===========

This is a short guide on how to publish and subscribe using **ldms_msg** in
place of **ldmsd_stream** in various scenarios.


PUBLISHING DATA OVER TRANSPORT
==============================

If you use ``ldmsd_stream_publish()`` to publish data to ``ldmsd`` similar to
the following code:

.. code:: c

   ldmsd_stream_publish(x, name, LDMSD_STREAM_STRING, data, strlen(data)+1);
   /* or */
   ldmsd_stream_publish(x, name, LDMSD_STREAM_JSON, jtxt, strlen(jtxt)+1);


You can use ``ldms_msg_publish()`` as follows:

.. code:: c

   ldms_msg_publish(x, name, LDMS_MSG_STRING, NULL, 0400, data, strlen(data)+1);
   /* or */
   ldms_msg_publish(x, name, LDMS_MSG_JSON, NULL, 0400, jtxt, strlen(jtxt)+1);

Now, message publishing has a ``perm`` field (``chmod(2)`` owner-group-other
octal permission), controlling who can read your published data.


STREAM MESSAGE DELIVERY IN LDMSD
================================

If you use ``stream_deliver()`` to deliver the stream data to the subscribers in
the process (a subscriber could be a forwarder) like the following:

.. code:: c

   ldmsd_stream_deliver(name, type, str, strlen(str)+1, NULL, NULL);
   /* or */
   ldmsd_stream_deliver(name, type, NULL, 0, json_obj, NULL);

You can use ``ldms_msg_publish()`` as follows:

.. code:: c

   ldms_msg_publish(NULL, name, type, NULL, 0400, data, strlen(data)+1)
   /* type can be LDMS_MSG_STRING or LDMS_MSG_JSON.
    * If type is LDMS_MSG_JSON, the data is expected to be a string
    * representation of a JSON object. */


SUBSCRIBE STREAM IN LDMSD
=========================

If you have code that subscribes to stream data within ldmsd (e.g. in a plugin)
like the following:

.. code:: c

   sc = ldmsd_stream_subscribe("name", cb_fn, cb_arg);

You can use ``ldms_msg_subscribe()`` in its place as follows:

.. code:: c

   mc = ldms_msg_subscribe("name", 0, cb_fn, cb_arg, "description");
   /* or subscribe using regular expression matching the name */
   mc = ldms_msg_subscribe("reg.*", 1, cb_fn, cb_arg, "description2");

With the new callback function signature, for example:

.. code:: c

   int cb_fn(ldms_msg_event_t ev, void *cb_arg)
   {
     /* cb_arg is the pointer supplied to ldms_msg_subscribe() */
     switch (ev->type) {
     case LDMS_MSG_EVENT_RECV:
       printf("name: %s\n", ev->recv.name);
       printf("data: %s\n", ev->recv.data);
       /* See `struct ldms_msg_event_s` for more information. */
       break;
     case LDMS_MSG_EVENT_CLIENT_CLOSE:
       /* This is the last event guaranteed to delivered to this client. The
        * resources application associate to this client (e.g. cb_arg) could be
        * safely freed at this point. */
       break;
     default:
       /* ignore other events */;
     }
     return 0;
   }



SUBSCRIBE FROM OUTSIDE OF LDMSD
===============================

This is a rare case. For LDMSD Stream, there is no easy way for a program that
is not an ``ldmsd`` to subscribe to a stream. The application that wish to do so
has to construct ``ldmsd_request`` (using
``ldmsd_request.h:ldmsd_req_cmd_new()``) and send the SUBSCRIBE request to
``ldmsd``. In addition, the application will have to also manually unpack the
data from the ``ldmsd_request`` message sent from ``ldmsd`` as ``ldmsd_stream``
is implemented on top of ``ldmsd_request`` protocol.


For LDMS Message, an application can subscribe to message channels as follows:

.. code:: c

   /* make local subscription, to get matching messages reaching us */
   mc = ldms_msg_subscribe("reg.*", 1, cb_fn, cb_arg, "description");

   /* request remote subscription, so that the peer forward matching messages
    * to our process. */
   ldms_msg_remote_subscribe(x, "reg.*", 1, NULL, NULL, LDMS_UNLIMITED);

For more information, please see ``ldms_msg(7)``.


NOTE ON PYTHON
==============

``ldmsd_stream`` was not available in ``ovis_ldms.ldms`` Python package. So,
Python applicaiton wishing to publish to ``ldmsd`` has to form ``ldmsd_request``
PUBLISH message and send. The application wishing to subscribe will have to do
form ``ldmsd_request`` SUBSCRIBE command and send to ``ldmsd`` and handle the
unpack of the ``ldmsd_request`` messages received from ``ldmsd``.

With LDMS Message API, one can publish data to ``ldmsd`` (or any process using
LDMS library) as follows:

.. code:: python

   from ovis_ldms import ldms
   x = ldms.Xprt("sock") # or x = ldms.Xprt("sock", "munge") for munge auth
   x.connect("somehost", 411)
   x.msg_publish("name", "data")

To subscribe for messages from ``ldmsd``:

.. code:: python

   from ovis_ldms import ldms
   x = ldms.Xprt("sock") # or x = ldms.Xprt("sock", "munge") for munge auth
   x.connect("somehost", 411)
   x.msg_subscribe("abc.*", 1) # this send a SUBSCRIBE request to peer

   mc = ldms.MsgClient("abc.*", 1) # This is our local client, buffering
                                    # matching messages that reached our
                                    # python process.

   msg = mc.get_data()
   if msg is not None : # msg is None means no data arrived yet
           print(f"{msg.name}: {msg.data}")

   # To get messages right away instead of repeatedly calling `mc.get_data()`,
   # please create `ldms.MsgClient` with ``cb_fn`` to get the matching messages
   # right away when they arrived. See callback example in ``ldms_msg(7)``.

For more examples and more detailed explanation, please see ``ldms_msg(7)``.


SEE ALSO
========

:ref:`ldms_msg(7) <ldms_msg>`
