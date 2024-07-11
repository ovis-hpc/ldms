===================
Plugin_dcgm_sampler
===================

:Date: 1 May 2019

.. contents::
   :depth: 3
..

NAME
====================

Plugin_dcgm_sampler - man page for the LDMS dcgm_sampler plugin

SYNOPSIS
========================

| Within ldmsd_controller or a configuration file:
| config name=dcgm_sampler [ <attr>=<value> ]

DESCRIPTION
===========================

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. The dcgm_sampler plugin provides a metric set for
each DCGM-compatible Nvidia GPU on the system. The schema is named
"dcgm" by default.

CONFIGURATION ATTRIBUTE SYNTAX
==============================================

**config**
   | name=<plugin_name> interval=<interval(us)> [fields=<fields>]
     [schema=<schema_name>] [job_set=<metric set name>]
   | configuration line

   name=<plugin_name>
      | 
      | This MUST be dcgm_sampler.

   interval=<interval(us)>
      | 
      | The sampling interval. This MUST be set to the same value that
        is set on the "start" line, otherwise behavior is undetermined.

   fields=<fields>
      | 
      | <fields> is a comma-separated list of integers representing DCGM
        field numebers that the plugin should watch. By default the
        plugin will watch fields 150,155.

   schema=<schema_name>
      | 
      | The schema name defaults to "dcgm", but it can be renamed at the
        user's choice.

   job_set=<metric set name>
      | 
      | The name of the metric set that contains the job id information
        (default=job_id)

BUGS
====================

No known bugs.

EXAMPLES
========================

Within ldmsd_controller or a configuration file:

::

   load name=dcgm_sampler
   config name=dcgm_sampler interval=1000000 fields=150,155,1001,1002,1003 schema=dcgmfav5
   start name=dcgm_sampler interval=1000000

SEE ALSO
========================

ldmsd(8), ldms_quickstart(7), ldmsd_controller(8), ldms_sampler_base(7)
