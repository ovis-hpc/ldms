.. _darshan_stream_store:

===========================
darshan_stream_store
===========================

---------------------------------
LDMS darshan_stream_store plugin
---------------------------------

:Date:   26 September 2021
:Manual section: 7
:Manual group: LDMS store

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| config name=darshan_stream_store [ <attr>=<value> ]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. The darshan_stream_store plugin writes out a single
darshan json stream's data to SOS container. The input data produced by
the LDMS darshan plugin consist of two types of messages: "MOD" for
module data and "MET for meta data. Both messages saved into the same
SOS container.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

**config**
   | name=darshan_stream_store path=<path> stream=<stream> [mode=<mode>]
   | configuration line

   name=<plugin_name>
      |
      | This MUST be darshan_stream_store.

   path=<path>
      |
      | The path to the root of the SOS container store (should be
        created by the user)

   stream=<stream>
      |
      | stream to which to subscribe.

   mode=<mode>
      |
      | The container permission mode for create, (defaults to 0660).

INPUT JSON FORMAT
=================

The input json has a "type" field, and this type used to select the the
message type between module data and meta data.

A MOD darshan JSON example is shown below:

::

        {"schema":"N/A", "uid":12345, "exe":"N/A","job_id":21068382,"rank":1,"ProducerName":"n326","file":"N/A","record_id":4379126583041845751,"module":"POSIX","type":"MOD","max_byte":-1,"switches":-1,"flushes":-1,"cnt":1,"op":"close","seg":[{"pt_sel":-1,"irreg_hslab":-1,"reg_hslab":-1,"ndims":-1,"npoints":-1,"off":-1,"len":-1,"start":0.060127,"dur":0.000002,"total":0.000016,"timestamp":1743618559.147298}]}

A MET darshan JSON example is shown below:

Some fields are set to -1 if they don't have data for that message type.

BUGS
====

No known bugs.

NOTES
=====

This store is in development and may be changed at any time.

Only supports one stream

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=darshan_stream_store
   config name=darshan_stream_store path=/tmp/darshan_stream stream=darshanConnector

   prdcr_add name=localhost1 host=localhost type=active xprt=sock port=52001 interval=20000000
   prdcr_subscribe stream=darshanConnector regex=localhost*
   prdcr_start name=localhost1

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`,
darshan_publisher, darshan_sampler, parser.pl (has perlpod),
:ref:`darshan_cat_publisher(7) <darshan_cat_publisher>`
