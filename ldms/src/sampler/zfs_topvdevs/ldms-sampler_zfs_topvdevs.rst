.. _zfs_topvdevs:

===================
zfs_topvdevs
===================

------------------------------------------
Man page for the LDMS zfs_topvdevs plugin
------------------------------------------

:Date:   19 Apr 2023
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| config name=zfs_topvdevs

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. The zfs_topvdevs plugin uses LDMS_V_LIST and
LDMS_V_RECORD to provide top level zfs virtual devices info using
libzfs.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The zfs_topvdevs plugin uses the sampler_base base class. This man page
covers only the configuration attributes, or those with default values,
specific to the this plugin; see :ref:`ldms_sampler_base(7) <ldms_sampler_base>` for the
attributes of the base class.

**config**
   | name=<plugin_name>
   | configuration line

   name=<plugin_name>
      |
      | This MUST be zfs_topvdevs.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=procnetdev
   config name=zfs_topvdevs producer=${HOSTNAME} instance=${HOSTNAME}/zfs_topvdevs
   start name=zfs_topvdevs interval=1000000 offset=0

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`,
:ref:`zfs_topvdevs(7) <zfs_topvdevs>`
