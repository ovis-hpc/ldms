.. _ipmisensors:

==================
ipmisensors
==================

-----------------------------------------
Man page for the LDMS ipmisensors plugin
-----------------------------------------

:Date:   21 Mar 2019
:Manual section: 7
:Manual group: LDMS store

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| config name=ipmisensors [ <attr>=<value> ]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. The ipmisensors plugin provides data from the result
of the ipmi-sensors command. Specific parameters for the command
described below. All data is reported out as floats.

**This sampler is currently in pre-release development in V4.2.**

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The ipmisensors plugin uses the sampler_base base class. This man page
covers only the configuration attributes, or those with default values,
specific to the this plugin; see :ref:`ldms_sampler_base(7) <ldms_sampler_base>` for the
attributes of the base class.

**config**
   | name=<plugin_name> [schema=<sname>] [ <attr>=<value> ... ]
   | configuration line

   name=<plugin_name>
      |
      | This MUST be ipmisensors.

   schema=<schema>
      |
      | Optional schema name. It is intended that the same sampler on
        different nodes with different metrics have a different schema.
        If not specified, will default to \`ipmisensors`.

   address=<address>
      |
      | address of the host to contact. h flag in the ipmi-sensors
        command.

   username=<username>
      |
      | username for the query. u flag in the ipmi-sensors command.
        Defaults to 'admin'.

   password=<password>
      |
      | password for the query. p flag in the ipmi-sensors command.
        Defaults to 'password'.

BUGS
====

No known bugs.

NOTES
=====

-  This sampler is currently in pre-release development in V4.2.

-  The ipmi-sensors call appears to have more overhead than the ipmitool
   commandfor single node queries, and so the impireader sampler is
   preferred.

-  Specific args to the command are: --comma-separated-output
   --no-header-output --session-timeout=500 --retransmission-timeout=250
   --quiet-cache --no-sensor-type. Of note are the timeouts. These will
   limit how long the call will wait (and thus the duration of the
   sample) if a host is not responding.

-  The ipmi-sensors call can be called with a fan out. This would cause
   significant parsing in the return, so it is not used here. Also the
   return of the fan out call will wait on the return of all the
   individual calls. Thus a non-responsive node can cause a long delay,
   affecting all values, without a timeout.

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

   load name=ipmisensors
   config name=ipmisensors producer=vm1_1 instance=vm1_1/ipmisensors address=cn1-ipmi
   start name=ipmisensors interval=1000000

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`,
:ref:`ipmireader(7) <ipmireader>`
