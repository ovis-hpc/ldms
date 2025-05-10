.. _ldmsd_stream_subscribe:

======================
ldmsd_stream_subscribe
======================

----------------------------------------------------
Man page for the LDMS ldmsd_stream_subscribe utility
----------------------------------------------------

:Date:   21 Aug 2021
:Manual section: 8
:Manual group: LDMSD


SYNOPSIS
========

At the command line: ldmsd_stream_subscribe [args]

DESCRIPTION
===========

The ldmsd_stream_subscribe program subscribes to a stream in place of a
full ldmsd daemon, writing received messages to a file or to stdout.

COMMAND LINE SYNTAX
===================

ldmsd_stream_subscribe -x <xprt> -h <host> -p <port> -s <stream-name> -a <auth> -A <auth-opt> -f <file> -D -i -R -q -E

|

   -x,--xprt <xprt>
      |
      | transport type on which to listen.

   -p,--port <port>
      |
      | port on which to listen.

   -h,--host <port>
      |
      | hostname or IP address of interface on which to listen.

   -a,--auth <auth>
      |
      | authentication to expect from publishers.

   -A,--auth_arg <auth-opt>
      |
      | auth options if needed (for e.g. ovis auth or munge on unusual
        port)

   -s,--stream <stream-name>
      |
      | Name of the stream to subscribe.

   -f,--file <file>
      |
      | File where messages delivered are written. If not specified,
        STDOUT.

   -E,--events-raw
      |
      | Suppress delivery envelope information in message output.

   -q,--quiet
      |
      | Suppress message output to file or stdout entirely.

   -D,--daemonize
      |
      | Put the process in the background as a daemon.

   -R,--daemon-noroot
      |
      | Prevent file system root (/) change-directory when starting the
        daemon. (Does nothing if -D is not present).

   -i,--daemon-io
      |
      | Keep the input and output file descriptors attached to the
        daemon instead of closing them. (Does nothing if -D is not
        present).

BUGS
====

No known bugs.

NOTES
=====

This program is in development and may change at any time.

Using "-a none" is insecure and should only be used with care.

EXAMPLES
========

Running in user mode as a sink to test a stream publishing program
writing to tag 'mystream':

::

   ldmsd_stream_subscribe -x sock -h 127.0.0.1 -p 20411 -s mystream -a none -f messages.out -D -R

Running in root mode and testing on port 511

::

   ldmsd_stream_subscribe -x sock -h 127.0.0.1 -p 511 -s mystream -a munge -f /var/log/ldms-stream/messages.out -D

Sending data to listening subscriber

::

   echo '{ "a": "worthless message"}' | ./ldmsd_stream_publish -x sock -h 127.0.0.1 -p 20411 -s mystream -a none -t json

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_stream_publish(8) <ldmsd_stream_publish>`,
:ref:`ldms_authentication(7) <ldms_authentication>`
