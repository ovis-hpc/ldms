.. _lustre_client:

====================
lustre_client
====================

-------------------------------------------
Man page for the LDMS lustre_client plugin
-------------------------------------------

:Date:   1 May 2019
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| config name=lustre_client [ <attr>=<value> ]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. The lustre_client plugin provide a metric set for
each of the lustre client mounts found on a node. The schema is named
"lustre_client". The data for the metric sets is generally found in
``/proc/fs/lustre/llite/*/stats``.

This plugin currently employs zero configuration. The producer name is
set to the hostname by default, and the metric set instance names are
derived from the llite instance name. Any user-supplied configuration
values not documented here will be ignored.

This plugin should work with at least Lustre versions 2.8, 2.10, and
2.12.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

**config**
   | name=<plugin_name> [job_set=<metric set name>] [producer=<name>]
     [component_id=<u64>]
   | configuration line

   name=<plugin_name>
      |
      | This MUST be lustre_client.

   job_set=<job metric set name>
      |
      | The name of the metric set that contains the job id information
        (default=job_id)

   producer=<alternate host name>
      |
      | The default used for producer (if not provided) is the result of
        gethostname(). The set instance names will be
        $producer/$llite_name.

   component_id=<uint64_t>
      |
      | Optional (defaults to 0) number of the host where the sampler is
        running. All sets on a host will have the same value.

   perm=<octal number>
      |
      | Set the access permissions for the metric sets. (default 440).

NOTES
=====

Improperly spelled option names are not trapped as configuration errors.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=lustre_client
   config name=lustre_client
   start name=lustre_client interval=1000000

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`,
:ref:`gethostname(2) <gethostname>`
