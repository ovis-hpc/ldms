.. _hello_sampler:

====================
hello_sampler
====================

-------------------------------------------
Man page for the LDMS hello_sampler plugin
-------------------------------------------

:Date:   21 Aug 2021
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| config name=hello_sampler [ <attr>=<value> ]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. The hello_sampler plugin does not actually sample,
but rather subscribes to an ldmsd_stream and writes the stream data to
the ldmsd logfile.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The hello_sampler plugin uses the sampler_base base class. This man page
covers only the configuration attributes, or those with default values,
specific to the this plugin; see :ref:`ldms_sampler_base(7) <ldms_sampler_base>` for the
attributes of the base class.

**config**
   | name=<plugin_name> stream=<stream>
   | configuration line

   name=<plugin_name>
      |
      | This MUST be hello_sampler.

   stream=<stream>
      |
      | Name of the stream to which to subscribe.

BUGS
====

No known bugs.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=hello_sampler
   config name=hello_sampler producer=host1 instance=host1/hello_sampler stream=foo component_id=1
   start name=hello_sampler interval=1000000 offset=0

::

   > ./hello_publisher -x sock -h localhost -p 16000 -a munge -s foo -m "foo" -t str
   The data was successfully published.
   The server responded with 0

   > ./hello_publisher -x sock -h localhost -p 16000 -a munge -s foo -m "bar" -t str
   The data was successfully published.
   The server responded with 0


   In the log file of the ldmsd:
   > cat log.txt
   Mon May 04 19:44:05 2020: CRITICAL  : stream_type: STRING, msg: "foo", msg_len: 4, entity: (nil)
   Mon May 04 19:44:24 2020: CRITICAL  : stream_type: STRING, msg: "bar", msg_len: 4, entity: (nil)

   Note that the hello_streams sampler does not do a sample, instead it subscribes to the stream with a callback and prints out what it got off the stream.

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`, :ref:`ldmsd_stream_publish(7) <ldmsd_stream_publish>`, :ref:`stream_csv_store(7) <stream_csv_store>`
