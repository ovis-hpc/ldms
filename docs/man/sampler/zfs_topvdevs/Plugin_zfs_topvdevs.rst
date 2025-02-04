===================
Plugin_zfs_topvdevs
===================

:Date:   19 Apr 2023

NAME
====

Plugin_zfs_topvdevs - man page for the LDMS zfs_topvdevs plugin

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
specific to the this plugin; see ldms_sampler_base.man for the
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

ldmsd(8), ldms_quickstart(7), ldmsd_controller(8), ldms_sampler_base(7),
Plugin_zfs_topvdevs(7)
