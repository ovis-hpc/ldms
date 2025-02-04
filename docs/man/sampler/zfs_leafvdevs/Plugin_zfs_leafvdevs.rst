====================
Plugin_zfs_leafvdevs
====================

:Date:   19 Apr 2023

NAME
====

Plugin_zfs_leafvdevs - man page for the LDMS zfs_leafvdevs plugin

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
specific to the this plugin; see ldms_sampler_base.man for the
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

ldmsd(8), ldms_quickstart(7), ldmsd_controller(8), ldms_sampler_base(7),
Plugin_leafvdevs(7)
