.. _zfs_leafvdevs:

====================
zfs_leafvdevs
====================

-------------------------------------------
Man page for the LDMS zfs_leafvdevs plugin
-------------------------------------------

:Date:   19 Apr 2023
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| config name=zfs_leafvdevs

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. The zfs_leafvdevs plugin uses LDMS_V_LIST and
LDMS_V_RECORD to provide zfs leaf virtual devices using libzfs.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The zfs_leafvdevs plugin uses the sampler_base base class. This man page
covers only the configuration attributes, or those with default values,
specific to the this plugin; see :ref:`ldms_sampler_base(7) <ldms_sampler_base>` for the
attributes of the base class.

**config**
   | name=<plugin_name>
   | configuration line

   name=<plugin_name>
      |
      | This MUST be zfs_leafvdevs.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=zfs_leafvdevs
   config name=zfs_leafvdevs producer=${HOSTNAME} instance=${HOSTNAME}/zfs_leafvdevs
   start name=zfs_leafvdevs interval=1000000 offset=0

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`,
:ref:`leafvdevs(7) <leafvdevs>`
