.. _ldms_msg_publish:

====================
ldms_msg_publish
====================

-------------------------------------------------------------
Man page for the LDMS ldms_msg_publish executable utility
-------------------------------------------------------------

:Date:   21 Aug 2021
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

   **-m|-M|--msg_name|--message_channel** <message_channel_name>
      |
      | Name of the channel (this will be used for subscribing)

   **-t|--type** <data-format>
      |
      | Optional data-format. Either 'string' or 'json'. Default is
        string.

   **-w|--max_wait** <W>
      |
      | Maximum number of cumulative retries to allow if the receiving daemon
        signals to wait for more transmission credits. Default 0.
        Each wait is 0.1 second.

   **-l|--line**
      |
      | Optional line mode. Publishes file one line at a time as
        separate publish calls

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
      | When -i is specified, use a delay of M microseconds between iterations.
        The default is 10000000 (10 seconds)

   **-R|--reconnect**
      |
      | If -R and -f are both given, the ldms connection is closed and reopened
        between repeated transmissions of the file.
        Unlike ldmsd_stream_publish, this option is independent of the -l option.

   **-v|--verbose**
      |
      | Optional verbose mode. Tracks progress, and possibly other things.


BUGS
====

Unterminated lines exactly 4094 bytes long at the end of a file or pipe
will not be published when line-mode (-l) is active.

NOTES
=====

This executable is in development and may change at any time.

This supports testing:
- publishing data once to a single connection from a pipe or file.
- repeating a file to a single connection.
- repeating a file to a new connection at each iteration.
- sending data line-by-line from a file or pipe as separate messages, when each line is a well-formed message.

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
