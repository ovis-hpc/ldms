.. _zfs_zpool:

================
zfs_zpool
================

---------------------------------------
Man page for the LDMS zfs_zpool plugin
---------------------------------------

:Date:   19 Apr 2023
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| config name=zfs_zpool [ <attr> = <value> ]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. The zfs_zpool plugin uses LDMS_V_LIST and
LDMS_V_RECORD to provide zpool info using libzfs.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The zfs_zpool plugin uses the sampler_base base class. This man page
covers only the configuration attributes, or those with default values,
specific to the this plugin; see :ref:`ldms_sampler_base(7) <ldms_sampler_base>` for the
attributes of the base class.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=zfs_zpool
   config name=zfs_zpool producer=${HOSTNAME} instance=${HOSTNAME}/zfs_zpool
   schema=zpools_stats job_set=${HOSTNAME}/zpools_stats
   start name=zfs_zpool interval=10000000 offset=15

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`,
:ref:`zfs_zpool(7) <zfs_zpool>`
