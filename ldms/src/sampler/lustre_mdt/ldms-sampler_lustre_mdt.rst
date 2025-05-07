.. _lustre_mdt:

=================
lustre_mdt
=================

----------------------------------------
Man page for the LDMS lustre_mdt plugin
----------------------------------------

:Date:   1 May 2019
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| config name=lustre_mdt

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file.

The lustre_mdt plugin provides metric sets for two different schemas:
lustre_mdt and lustre_mdt_job_stats.

The metric sets using schema lustre_mdt will have a producer name set to
the hostname, and the instance name set to the mdt name. The data for
these metrics sets come from a combination of the data in
``/proc/fs/lustre/mdt/*/stats`` and a few other single-value files in
``/proc/fs/lustre/mdt/*/``.

The metric sets using schema lustre_mdt_job_stats will have a producer
name set to the hostname, and the instance name will be set to a
combination of the mdt name and the job_id string. The data for these
metrics sets come from ``/proc/fs/lustre/mdt/*/job_stats``.

This plugin currently employs zero configuration. Any user-supplied
configuration values will be ignored. Future versions may add
configuration options.

This plugin should work with at least Lustre versions 2.8, 2.10, and
2.12.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

**config**
   | name=<plugin_name> [producer=<name>] [component_id=<u64>]
   | configuration line

   name=<plugin_name>
      |
      | This MUST be lustre_mdt.

   producer=<alternate host name>
      |
      | The default used for producer (if not provided) is the result of
        gethostname(). The set instance names will be
        $producer/$mdt_name.

   component_id=<uint64_t>
      |
      | Optional (defaults to 0) number of the host where the sampler is
        running. All sets on a host will have the same value.

BUGS
====

No known bugs.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=lustre_mdt
   config name=lustre_mdt
   start name=lustre_mdt interval=1000000

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`
