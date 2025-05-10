.. _ldmsd_stream_publish:

====================
ldmsd_stream_publish
====================

-------------------------------------------------------------
Man page for the LDMS ldmsd_stream_publish executable utility
-------------------------------------------------------------

:Date:   21 Aug 2021
:Manual section: 8
:Manual group: LDMSD


SYNOPSIS
========

At the command line: ldmsd_stream_publish [args]

DESCRIPTION
===========

The ldmsd_stream_publish executable publishes to the ldmsd_streams
interface of a running ldms daemon. The hello_publisher takes a file as
input and publishes it either in bulk or line by line. It reuses the
connection for all the messages

COMMAND LINE SYNTAX
===================

ldmsd_stream_publish -x <xprt> -h <host> -p <port> -s <stream-name> -a <auth> -A <auth-opt> -t <data-format> -f <file> [-l]
   |

   -x <xprt>
      |
      | transport of the ldmsd to which to connect.

   -p <port>
      |
      | port of the ldmsd to which to connect.

   -a <auth>
      |
      | auth to connect to the ldmsd

   -A <auth-opt>
      |
      | auth-opts to connect to the ldmsd

   -s <stream-name>
      |
      | Name of the stream (this will be used for subscribing)

   -t <data-format>
      |
      | Optional data-format. Either 'string' or 'json'. Default is
        string.

   -l
      |
      | Optional line mode. Publishes file one line at a time as
        separate publish calls

   -f <file>
      |
      | File that is published. If not specified, input is copied from
        STDIN.

   -r N
      |
      | Repeat the publication of the file N times, with a delay
        interval specifed by -i. Repeating is not supported unless the
        input is a file. If the -l option is given, the file and
        connection are opened once and the lines are replayed to
        individual ldmsd_stream_publish calls. If -l is not given, the
        ldmsd_stream_publish_file call is used, resulting in multiple
        connection openings. -i interval_in_microseconds
      | Change the default delay (usleep(interval_in_microseconds)) used
        if repeat is specified.

BUGS
====

No known bugs.

NOTES
=====

This executable is in development and may change at any time.

The difference in repeat behavior if -l is present allows for testing
two scenarios: repeating many messages to a single connection and
repeating connection attempts to a daemon that may come and go during
publication attempts. Environment variables LDMSD_STREAM_CONN_TIMEOUT
and LDMSD_STREAM_ACK_TIMEOUT will affect the timing of the repeat loop
when -l is not given.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=hello_sampler
   config name=hello_sampler producer=host1 instance=host1/hello_sampler stream=foo component_id=1
   start name=hello_sampler interval=1000000 offset=0

::

   > cat testdata.10.out
   { "seq": 0, "job-id" : 10364, "rank" : 1, "kokkos-perf-data" : [ {"name" : "SPARTAFOO0", "count": 0, "time": 0.0000},{"name" : "SPARTAFOO1", "count": 1, "time": 0.0001},{"name" : "SPARTAFOO2", "count": 2, "time": 0.0002},{"name" : "SPARTAFOO3", "count": 3, "time": 0.0003},{"name" : "SPARTAFOO4", "count": 4, "time": 0.0004},{"name" : "SPARTAFOO5", "count": 5, "time": 0.0005},{"name" : "SPARTAFOO6", "count": 6, "time": 0.0006},{"name" : "SPARTAFOO7", "count": 7, "time": 0.0007},{"name" : "SPARTAFOO8", "count": 8, "time": 0.0008},{"name" : "SPARTAFOO9", "count": 9, "time": 0.0009}] }

::

   > ldmsd_stream_publish -x sock -h localhost -p 52001 -s foo -t json -f ./testdata.10.out -a none


   In the log file of the ldmsd:
   > cat log.txt
   Sat Aug 21 18:15:27 2021: CRITICAL  : stream_type: JSON, msg: "{ "seq": 0, "job-id" : 10364, "rank" : 1, "kokkos-perf-data" : [ {"name" : "SPARTAFOO0", "count": 0, "time": 0.0000},{"name" : "SPARTAFOO1", "count": 1, "time": 0.0001},{"name" : "SPARTAFOO2", "count": 2, "time": 0.0002},{"name" : "SPARTAFOO3", "count": 3, "time": 0.0003},{"name" : "SPARTAFOO4", "count": 4, "time": 0.0004},{"name" : "SPARTAFOO5", "count": 5, "time": 0.0005},{"name" : "SPARTAFOO6", "count": 6, "time": 0.0006},{"name" : "SPARTAFOO7", "count": 7, "time": 0.0007},{"name" : "SPARTAFOO8", "count": 8, "time": 0.0008},{"name" : "SPARTAFOO9", "count": 9, "time": 0.0009},{"name" : "SPARTAFOO10", "count": 10, "time": 0.00010}] }", msg_len: 589, entity: 0x2aaab8004680

   Note that the hello_streams sampler does not do a sample, instead it subscribes to the stream with a callback and prints out what it got off the stream.

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`,
:ref:`hello_sampler(7) <hello_sampler>`, :ref:`stream_csv_store(7) <stream_csv_store>`
