.. _blob_stream_writer:

=========================
blob_stream_writer
=========================

-----------------------------------------------
Man page for the LDMS blob_stream_writer plugin
-----------------------------------------------

:Date:   15 Jun 2021
:Manual section: 7
:Manual group: LDMS sampler


SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| config name=blob_stream_writer [ <attr>=<value> ]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. The blob_stream_writer plugin writes out raw stream
messages and offsets of the messages in separate files. Messages are not
appended with ' or ' '. Multiple streams may be specified.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

**config**
   | name=blob_stream_writer path=<path> container=<container>
     stream=<stream> debug=1
   | configuration line

   name=<plugin_name>
      |
      | This MUST be blob_stream_writer.

   path=<path>
      |
      | path to the directory of the output files

   container=<container>
      |
      | directory of the output file

   stream=<stream>
      |
      | stream to which to subscribe. This argument may be repeated.
        Each stream will be written in a separate file pair.

   debug=1
      |
      | Enable logging of messages stored to the log file.

   timing=1
      |
      | Enable writing timestamps to a separate file.

   spool=1
      |
      | Move closed files to the directory <path>/<container>/spool/.

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
           is suppressed if no data at all has been written and
           rollempty=0.

      2
         |
         | wake daily at rollover seconds after midnight (>=0) and roll.
           Rollover is suppressed if no data at all has been written and
           rollempty=0.

      3
         |
         | roll after approximately rollover records are written.

      4
         roll after approximately rollover bytes are written.

      5
         |
         | wake at rollover seconds after midnight (>=0) and roll, then
           repeat every rollagain (> rollover) seconds during the day.
           For example "rollagain=3600 rollover=0 rolltype=5" rolls
           files hourly. Rollover is suppressed if no data at all has
           been written and rollempty=0.

   rollover=<rollover>
      |
      | Rollover value controls the frequency of rollover (e.g., number
        of bytes, number of records, time interval, seconds after
        midnight). Note that these values are estimates.

   rollempty=0
      |
      | Turn off rollover of empty files. Default value is 1 (create
        extra empty files).

OUTPUT FORMAT
=============

There is no requirement that any message must the same format as any
other.

The writer writes all messages received to a file pair:
$path/$container/$stream.OFFSET.$create_time
$path/$container/$stream.DAT.$create_time where OFFSET is the byte
offsets into the corresponding .DAT of the messages seen on the stream.

Each byte offset is written as a little-endian 64 bit number. Data read
from .OFFSET should be converted to host order with le64toh.

Both DAT and OFFSET files begin with an 8 byte magic number: blobdat\\0
and bloboff\\0, respectively.

Optionally (if timing=1 given) the additional file
$path/$container/$stream.TIMING.$create_time is created containing
binary timestamps corresponding to the messages. The TIMING file begins
with an 8 byte magic number: blobtim\\0. Each time is the delivery time
to the plugin performing the blob storage. Each timestamp is written to
the .TIMING file as a binary pair (tv_sec, tv_usec) with each value
stored as a little-endian 64 bit value which should be read and then
converted with le64toh.

NOTES
=====

This writer is in development and may be changed at any time.

Cannot support stream=.\* as there is no corresponding regex
subscription policy currently available in the C stream API.

The config operation may called at any time or repeated, though the use
of rollover policies is recommended instead. Repeated configuration of
rollover is silently ignored. The start and stop operations will start
and stop storage of all streams.

The plugin appears in C code as a sampler plugin, since the storage
policy and store plugin interfaces are set-oriented and no sets are
involved here.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=blob_stream_writer
   config name=blob_stream_writer path=/writer/streams container=${CLUSTER} stream=foo stream=slurm stream=kokkos
   start name=name=blob_stream_writer

Examining offsets in a shell:

::

   od od -A d -t u8 -j 8 -w8 slurm.OFFSET.1624033344 |sed -e 's/[0-9,A-F,a-f]* *//'

Examining timestamps in a shell:

::

   od -A d -j 8 -t u8

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, le64toh(3), fseek(3), od(1)
