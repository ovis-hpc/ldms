.. _ipmireader:

=================
ipmireader
=================

----------------------------------------
Man page for the LDMS ipmireader plugin
----------------------------------------

:Date:   18 Feb 2019
:Manual section: 7
:Manual group: LDMS store

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| config name=ipmireader [ <attr>=<value> ]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. The ipmireader plugin provides data from the result
of the ipmitool sdr command. All data is reported out as floats.

**This sampler is currently in pre-release development in V4.2.**

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The ipmireader plugin uses the sampler_base base class. This man page
covers only the configuration attributes, or those with default values,
specific to the this plugin; see :ref:`ldms_sampler_base(7) <ldms_sampler_base>` for the
attributes of the base class.

**config**
   | name=<plugin_name> [schema=<sname>] [ <attr>=<value> ... ]
   | configuration line

   name=<plugin_name>
      |
      | This MUST be ipmireader.

   schema=<schema>
      |
      | Optional schema name. It is intended that the same sampler on
        different nodes with different metrics have a different schema.
        If not specified, will default to \`ipmireader`.

   address=<address>
      |
      | address of the host to contact. H flag in the ipmitool command.

   username=<username>
      |
      | username for the query. U flag in the ipmitool command. Defaults
        to 'admin'.

   password=<password>
      |
      | password for the query. P flag in the ipmitool command. Defaults
        to 'password'.

   sdrcache=<sdrcache>
      |
      | output for the sdr cache file, to improve performance. Optional.

   retry=<sec>
      |
      | interval to retry creating set if initially fails (host down).
        Default 600 sec.

BUGS
====

No known bugs.

NOTES
=====

-  This sampler is currently in pre-release development in V4.2.

-  Parameters in the ipmitool call are: -N1 (timeout for LAN interface)
   -R1 (number of retries for LAN interface). These are in order to
   reduce the time waiting for a non responsive node.

-  The ipmitool command appears to have less overhead than ipmi-sensors
   and so is preferred over the ipmisensors sampler for single node
   calls.

-  If the dump cache command fails, this is not reported. If the file
   does not exist after a short sleep, there is a log message. Without
   the sdr file, the sampler will continue. On one system, using the
   cached sdr information reduces the call response time by about 0.5
   seconds. This manifests itself in the timestamp of the call.

-  There is a one time occurrence of a sleep of 2 seconds (empirically
   chosen) after the dump cache command, to enable the file to be
   written by the time of the next data call. If it takes longer, but is
   in place for later sample calls, it will be used then.

-  There is currently no call to redump the file.

-  There is no way to check that a dumped file is still accurate for
   your system.

-  Currently all the data is reported as type float.

-  In case of signficant error or cannot open the file, all metrics are
   set to the FAIL value, which currently is -8888. In case of a metric
   error, like a missing fan and hence the reported value is not
   numeric, the metric is set to the ERR value, which currently is
   -9999.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=ipmireader
   config name=ipmireader producer=vm1_1 instance=vm1_1/ipmireader address=cn1-ipmi
   start name=ipmireader interval=1000000

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`,
:ref:`ipmisensors(7) <ipmisensors>`
