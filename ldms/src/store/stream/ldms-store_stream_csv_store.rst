.. _stream_csv_store:

=======================
stream_csv_store
=======================

----------------------------------------------
Man page for the LDMS stream_csv_store plugin
----------------------------------------------

:Date:   03 Oct 2021
:Manual section: 7
:Manual group: LDMS store

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| config name=stream_csv_store [ <attr>=<value> ]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. The stream_csv_store plugin is a DEVELOPMENTAL store
that writes out either a single stream's data to csv format if the input
type is a well-known json format or writes out the raw messages if the
input type is str. Input type will be determined by the
hello_cat_publisher or similar.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

**config**
   | name=stream_csv_store path=<path> container=<container>
     stream=<stream> [flushtime=<N>] [buffer=<0/1>] [rolltype=<N>
     rollover=<N> rollagain=<N>]
   | configuration line

   name=<plugin_name>
      |
      | This MUST be stream_csv_store.

   path=<path>
      |
      | path to the directory of the csv output file

   container=<container>
      |
      | directory of the csv output file

   stream=<stream_a,stream_b>
      |
      | csv list of streams to which to subscribe.

   flushtime=<N>
      |
      | Flush any file that has not received data on its stream in the
        last N sec. This is asynchonous to any buffering or rollover
        that is occuring. Min time if enabled = 120 sec. This will occur
        again at this interval if there is still no data received.

   buffer=<0/1>
      |
      | Optional buffering of the output. 0 to disable buffering, 1 to
        enable it with autosize (default)

   rolltype=<rolltype>
      |
      | By default, the store does not rollover and the data is written
        to a continously open filehandle. Rolltype and rollover are used
        in conjunction to enable the store to manage rollover, including
        flushing before rollover. The header will be rewritten when a
        roll occurs. Valid options are:

      1
         |
         | wake approximately every rollover seconds and roll. Rollover
           is suppressed if no data at all has been written.

      2
         |
         | wake daily at rollover seconds after midnight (>=0) and roll.
           Rollover is suppressed if no data at all has been written.

      3
         |
         | roll after approximately rollover records are written.

      4
         |
         | roll after approximately rollover bytes are written.

      5
         |
         | wake at rollover seconds after midnight (>=0) and roll, then
           repeat every rollagain (> rollover) seconds during the day.
           For example "rollagain=3600 rollover=0 rolltype=5" rolls
           files hourly. Rollover is suppressed if no data at all has
           been written.

   rollover=<rollover>
      |
      | Rollover value controls the frequency of rollover (e.g., number
        of bytes, number of records, time interval, seconds after
        midnight). Note that these values are estimates due to the
        nature of thread wake-ups. Also, for rolltypes 3 and 4, there is
        a minimum delay of ROLL_LIMIT_INTERVAL seconds between rollovers
        no matter how fast the data is being received, which may lead to
        larger than expected data files for small values of rollover.

JSON FORMAT AND OUTPUT HEADER AND FORMAT
========================================

The json is expected to be something like:

::

   {"foo":1, "bar":2, "zed-data":[{"count":1, "name":"xyz"},{"count":2, "name":"abc"}]}

Note the brackets. There will be at most one list. It is expected that
each dictionary in the list will have the same item names. Everything
else must be singleton data items.

The header is generated off the first received json ever. If that first
json is missing the list, or if the list has no entries, then list data
will not appear in the header and will not be parsed in subsequent data
lines. The header values will be the singleton names (e.g., foo, bar)
and a list will be broken up into and item per dictionary item with
names listname:dictname (e.g., zed_data:count, zed_data:name).

There can be any number of dictionaries in a list. Data lines with
multiple dictionaries will be written out in the csv as separate lines,
with the singleton items repeated in each line like:

::

   #foo,bar,zed-data:count,zed-data:name
   1,2,1,xyz
   1,2,2,abc

There will be a header in every output file (can be more than 1 output
file because of rollover).

STORE OUTPUT FILENAME
=====================

The filename will be '<streamname>.<timestamp>' (e.g., foo-123456789).
The timestamp is determined when the store is started or rolledover and
the file is created. That may be considerably earlier than when data is
streamed to the store.

STORE COLUMN ORDERING
=====================

There is only column ordering for 'json' format. There is no column
ordering for 'str' format. 'str' format will always be written out, no
matter what the 'json' header keys may be. The json order is arbitrary.

TIMING INFORMATION
==================

Options for timing information are driven by #defines in the code source
right now.

TIMESTAMP_STORE
   |
   | Set by #define or #undef TIMESTAMP_STORE. This will write out an
     absolute timestamp in the file as the last item in the csv and is
     called 'store_recv_time' in the header. The timestamp is only
     gotten once, when the function is entered (e.g., if a data line has
     multiple dicts, this will result in multiple output lines each of
     which will have the same additional timestamp value). Both string
     and json are timestamped.

STREAM_CSV_DIAGNOSTICS
   |
   | Set by #define or #undef STREAM_CSV_DIAGNOSTICS. This will write
     out diagnostic info to the log when stream_cb is called.

BUGS
====

No known bugs.

NOTES
=====

This store is in development and may be changed at any time.

Supports more than 1 stream. There is currently no performance guidence
about number of streams and amount of data.

There is no way to know if a stream will actually be used or if a final
value is received. Therefore, this store will need to be restarted if
you want to use it with a new stream or if you want use the same stream
name, but with different fields in the json.

It is possible that with buffering, if a stream's sends are ended, there
still may be unflushed data to a file.

There is no way to remove a stream from the index nor to unsubscribe.
That is, there is nothing that is akin to open_store and close_store
pair as in an actual store plugin. Note that this is in development and
options are changing. For example, RESET funcationality has been removed
and flushtime functionality has changed.

Note the restrictions on the data input above. Also how that affects the
header.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=stream_csv_store
   config name=stream_csv_store path=XYZ/store container=csv stream=foo buffer=1
   # dont call anything else on the store. the store action is called by a callback triggered by the stream.

   prdcr_add name=localhost1 host=localhost type=active xprt=sock port=52001 interval=20000000
   prdcr_subscribe stream=foo regex=localhost*
   prdcr_start name=localhost1

Testdata:

::

   cat XXX/testdata.txt
   {"job-id" : 10364, "rank" : 1, "kokkos-perf-data" : [ {"name" : "SPARTAFOO0", "count": 0, "time": 0.0000},{"name" : "SPARTAFOO1", "count": 1, "time": 0.0001},{"name" : "SPARTAFOO2", "count": 2, "time": 0.0002},{"name" : "SPARTAFOO3", "count": 3, "time": 0.0003},{"name" : "SPARTAFOO4", "count": 4, "time": 0.0004},{"name" : "SPARTAFOO5", "count": 5, "time": 0.0005},{"name" : "SPARTAFOO6", "count": 6, "time": 0.0006},{"name" : "SPARTAFOO7", "count": 7, "time": 0.0007},{"name" : "SPARTAFOO8", "count": 8, "time": 0.0008},{"name" : "SPARTAFOO9", "count": 9, "time": 0.0009}] }

Publish:

::

   ldmsd_stream_publish -x sock -h localhost -p 52001 -s foo -t json -f XXX/testdata.txt -a <munge|none>



   Output:
   cat XYZ/store/csv/foo.1614306320
   rank,job-id,kokkos-perf-data:time,kokkos-perf-data:name,kokkos-perf-data:count,store_recv_time
   1,10364,0.000000,"SPARTAFOO0",0,1614306329.167736
   1,10364,0.000100,"SPARTAFOO1",1,1614306329.167736
   1,10364,0.000200,"SPARTAFOO2",2,1614306329.167736
   1,10364,0.000300,"SPARTAFOO3",3,1614306329.167736
   1,10364,0.000400,"SPARTAFOO4",4,1614306329.167736
   1,10364,0.000500,"SPARTAFOO5",5,1614306329.167736
   1,10364,0.000600,"SPARTAFOO6",6,1614306329.167736
   1,10364,0.000700,"SPARTAFOO7",7,1614306329.167736
   1,10364,0.000800,"SPARTAFOO8",8,1614306329.167736
   1,10364,0.000900,"SPARTAFOO9",9,1614306329.167736

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`,
:ref:`ldmsd_stream_publish(7) <ldmsd_stream_publish>`, :ref:`hello_sampler(7) <hello_sampler>`
