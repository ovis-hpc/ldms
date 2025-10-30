.. _ldms_msg_publish:

====================
ldms_msg_publish
====================

-------------------------------------------------------------
Man page for the LDMS ldms_msg_publish executable utility
-------------------------------------------------------------

:Date:   29 Oct 2025
:Manual section: 8
:Manual group: LDMSD


SYNOPSIS
========

At the command line: ldms_msg_publish [args]

DESCRIPTION
===========

The ldms_msg_publish executable publishes to the ldms_msgs
interface of a running ldms daemon. The hello_publisher takes a file as
input and publishes it either in bulk or line by line. It reuses the
connection for all the messages by default.

COMMAND LINE SYNTAX
===================

ldms_msg_publish -x <xprt> -h <host> -p <port> -m <message_channel-name> -a <auth> -A <auth-opt> -t <data-format> [-v] [-f <file> [-l] [-r <N> [-i <M>] [-R]]]
   |

   **-x|--xprt** <xprt>
      |
      | transport of the ldms to which to connect.

   **-p|--port** <port>
      |
      | port of the ldmsd to which to connect.

   **-a|--auth** <auth>
      |
      | auth to connect to the ldmsd

   **-A|--auth_opt** <auth-opt>
      |
      | auth-opts to connect to the ldmsd

   **-m||--msg_tag** <message tag>
      |
      | The message tag to include when publishing messages.

   **-t|--type** <data-format>
      |
      | Optional data-format. Either 'string' or 'json'. Default is
        string.
        This publisher program does not validate input before sending it,
        even if -t json is given.

   **-l|--line**
      |
      | Optional line mode. Publishes file one line at a time as
        separate publish calls. In line mode, a delay of -D <nanoseconds>
        is slept between lines. If a line does
        not contain a complete message, the recipients will see
        what is sent as erroneous when the type given is 'json'.

   **-f|--file** <file>
      |
      | File that is published. If not specified, input is copied from
        STDIN, and each line is expected to be a well-formed payload.
        If -f is specified, options -l, -r, and -R are applied.

   **-r|--repeat** <N>
      |
      | Repeat the publication of the file N times, with a delay
        interval specifed by -i. Repeating is not supported unless the
        input is a file. If the -l option is given, the
        lines are replayed to individual ldms_msg_publish calls.

   **-i|--interval** <M>
      |
      | When -i is specified, sleep an interval of M microseconds between
        file publication iterations. The default is 10000000 (10 seconds).
        Use -D to delay between lines within a file.

   **-g|--giveup** <secs>
      |
      | When -g is specified, a limit of <secs> seconds is set for
        QUOTA updates from the peer. If this limit is exceeded, the
	application will exit.

   **-D|--delay** <nanoseconds>
      |
      | The amount of time to sleep between ldms_msg_publish calls when
        in line mode (-l). Must be in range [0, 999999999]. Default 0.

   **-R|--reconnect**
      |
      | If -R and -f are both given, the ldms connection is closed and reopened
        between repeated transmissions of the file.
        Unlike ldmsd_stream_publish, this option is independent of the -l option.

   **-W|--retry** <W>
      |
      | If specified, W is the period in milliseconds to wait between connection
        attempts. 0 means do not retry when a connection fails. (Default is 0).
        When used, retry is infinite except in the case of detectable authentication
        failure.

   **-v|--verbose**
      |
      | Optional verbose mode. Tracks progress, and possibly other things.


NOTES
=====

This executable is in development and may change at any time.

This supports testing:
- publishing data once to a single connection from a pipe or file.
- repeating a file to a single connection.
- repeating a file to a new connection at each iteration.
- sending data line-by-line from a file or pipe as separate messages, up to
  a maximum line length which is user specified.

When the input is a pipe and -l is not specified, the entire content
of the pipe is buffered before anything is sent.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=hello_sampler
   config name=hello_sampler producer=host1 instance=host1/hello_sampler message_channel=foo component_id=1
   start name=hello_sampler interval=1000000 offset=0

::

   > cat testdata.10.out
   { "seq": 0, "job-id" : 10364, "rank" : 1, "kokkos-perf-data" : [ {"name" : "SPARTAFOO0", "count": 0, "time": 0.0000},{"name" : "SPARTAFOO1", "count": 1, "time": 0.0001},{"name" : "SPARTAFOO2", "count": 2, "time": 0.0002},{"name" : "SPARTAFOO3", "count": 3, "time": 0.0003},{"name" : "SPARTAFOO4", "count": 4, "time": 0.0004},{"name" : "SPARTAFOO5", "count": 5, "time": 0.0005},{"name" : "SPARTAFOO6", "count": 6, "time": 0.0006},{"name" : "SPARTAFOO7", "count": 7, "time": 0.0007},{"name" : "SPARTAFOO8", "count": 8, "time": 0.0008},{"name" : "SPARTAFOO9", "count": 9, "time": 0.0009}] }

::

   > ldms_msg_publish -x sock -h localhost -p 52001 -m foo -t json -f ./testdata.10.out -a none


   In the log file of the ldmsd:
   > cat log.txt
   Sat Aug 21 18:15:27 2021: CRITICAL  : stream_type: JSON, msg: "{ "seq": 0, "job-id" : 10364, "rank" : 1, "kokkos-perf-data" : [ {"name" : "SPARTAFOO0", "count": 0, "time": 0.0000},{"name" : "SPARTAFOO1", "count": 1, "time": 0.0001},{"name" : "SPARTAFOO2", "count": 2, "time": 0.0002},{"name" : "SPARTAFOO3", "count": 3, "time": 0.0003},{"name" : "SPARTAFOO4", "count": 4, "time": 0.0004},{"name" : "SPARTAFOO5", "count": 5, "time": 0.0005},{"name" : "SPARTAFOO6", "count": 6, "time": 0.0006},{"name" : "SPARTAFOO7", "count": 7, "time": 0.0007},{"name" : "SPARTAFOO8", "count": 8, "time": 0.0008},{"name" : "SPARTAFOO9", "count": 9, "time": 0.0009},{"name" : "SPARTAFOO10", "count": 10, "time": 0.00010}] }", msg_len: 589, entity: 0x2aaab8004680

   Note that the hello_streams sampler does not do a sample, instead it subscribes to the stream with a callback and prints out what it got off the stream.

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`,
:ref:`hello_sampler(7) <hello_sampler>`, :ref:`stream_csv_store(7) <stream_csv_store>`, :ref:`blob_msg_writer(7) <blob_msg_writer>`
