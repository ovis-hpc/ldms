.. _lnet_stats:

=================
lnet_stats
=================

----------------------------------------
Man page for the LDMS lnet_stats plugin
----------------------------------------

:Date:   18 Feb 2018
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

| Within ldmsctl
| ldmsctl> config name=lnet_stats [ <attr>=<value> ]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. The lnet_stats plugin provides memory info from
/proc/sys/lnet/stats or equivalent.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The lnet_stats plugin uses the sampler_base base class. This man page
covers only the configuration attributes, or those with default values,
specific to the this plugin; see :ref:`ldms_sampler_base(7) <ldms_sampler_base>` for the
attributes of the base class.

**config**
   | name=<plugin_name> [schema=<sname> file=<stats_path>]
   | ldmsctl configuration line.

   name=<plugin_name>
      |
      | This MUST be lnet_stats.

   schema=<schema>
      |
      | Optional schema name. It is intended that the same sampler on
        different nodes with different metrics have a different schema.
        If not specified, will default to \`lnet_stats`.

   file=<stats_path>
      |
      | Optional full path name of stats file to use. If not supplied,
        the default search path described in NOTES is searched.
        Typically, this option is only used in test environments which
        may not have a real Lustre installation or in order to test
        transient disappearance of the file.

NOTES
=====

The default search path followed for LNET stats is:
/sys/kernel/debug/lnet/stats:/proc/sys/lnet/stats. Which file will
exist, if either, depends on the Lustre version and how many volumes are
currently mounted. Be aware that /sys/kernel/debug normally is only
readable by privileged users.

The stats file disappears when all mounts are unmounted or not yet
mounted. While it is missing, the data set is not updated.

This assumes the file search path as described above, instead of looking
it up from the Lustre runtime libraries. This avoids compile time
dependence on Lustre which may be upgraded independently of LDMS. This
is not considered a bug.

EXAMPLES
========

::

   Within ldmsd_controller or a configuration file:
   load name=lnet_stats
   config name=lnet_stats producer=vm1_1 instance=vm1_1/lnet_stats component_id=10
   start name=lnet_stats interval=1000000

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`
