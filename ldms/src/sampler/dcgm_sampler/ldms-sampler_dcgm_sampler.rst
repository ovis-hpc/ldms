.. _dcgm_sampler:

===================
dcgm_sampler
===================

------------------------------------------
Man page for the LDMS dcgm_sampler plugin
------------------------------------------

:Date:   1 May 2019
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| config name=dcgm_sampler [ <attr>=<value> ] [use_base=<\*>]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. The dcgm_sampler plugin provides a metric set for
each DCGM-compatible Nvidia GPU on the system. The schema is named
"dcgm" by default.

NOTE: This sampler requires the NVidia DCGM daemon "nv-hostengine"
running before it can be configured in ldmsd.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

**config**
   | name=<plugin_name> interval=<interval(us)> [fields=<fields>]
     [schema=<schema_name>] [job_set=<metric set name>] [use_base=<\*>
     [uid=<int>] [gid=<int>] [perm=<octal>] [instance=<name>]
     [producer=<name>] [job_id=<metric name in job_set set>]]
   | configuration line

   name=<plugin_name>
      |
      | This MUST be dcgm_sampler.

   use_base=<\*>
      |
      | Any value given enables the sampler_base configuration option
        processing (see :ref:`ldms_sampler_base(7) <ldms_sampler_base>`). If not given, the options
        not listed below are ignored.

   interval=<interval(us)>
      |
      | The DCGM library sampling interval (dcgmWatchFields()
        updateFreq). This MUST be set to the same value that is set on
        the dcgm_sampler start line, otherwise behavior is undetermined.

   fields=<fields>
      |
      | <fields> is a comma-separated list of integers representing DCGM
        field identifiers that the plugin should watch. By default the
        plugin will watch fields 150,155. The field identifier meanings
        are defined in dcgm_fields.h and the DCGM Library API Reference
        Manual and may vary with DCGM release version. The plugin usage
        message provides a table of fields, subject to hardware support;
        see the output of 'ldms-plugins.sh dcgm_sampler'.

   schema=<schema_name>
      |
      | The schema name defaults to "dcgm", but it can be renamed at the
        user's choice.

   job_set=<metric set name>
      |
      | The name of the metric set that contains the job id information
        (default=job_id)

BUGS
====

No known bugs.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=dcgm_sampler
   config name=dcgm_sampler interval=1000000 fields=150,155,1001,1002,1003 schema=dcgmfav5
   start name=dcgm_sampler interval=1000000

NOTES
=====

Multiple instances of the sampler cannot run on the same server.

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`
