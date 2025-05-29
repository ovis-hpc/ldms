.. _procnetdev2:

==================
procnetdev2
==================


-----------------------------------------
Man page for the LDMS procnetdev2 plugin
-----------------------------------------

:Date:   07 Jan 2022
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| config name=procnetdev2 [ <attr> = <value> ]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. The procnetdev2 plugin uses LDMS_V_LIST and
LDMS_V_RECORD to provide network info from /proc/net/dev.

This plugin is multi-instance capable.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The procnetdev2 plugin uses the sampler_base base class. This man page
covers only the configuration attributes, or those with default values,
specific to the this plugin; see :ref:`ldms_sampler_base(7) <ldms_sampler_base>` for the
attributes of the base class.

**config**
   | name=<plugin_name> [ifaces=<ifs>]
   | configuration line

   name=<plugin_name>
      |
      | This MUST be procnetdev2.

   ifaces=<ifs>
      |
      | (Optional) A CSV list of interfaces to sample. If not specified,
        all available interfaces in /proc/net/dev will be reported. It
        is OK to specify non-existing interfaces in the ifaces list.

   schema=<schema>
      |
      | Optional schema name. It is intended that the same sampler on
        different nodes with different metrics or ifaces have a
        different schema. If not specified, will default to
        \`procnetdev`.

BUGS
====

The maximum number of interfaces is limited to 32.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=procnetdev
   config name=procnetdev producer=vm1_1 instance=vm1_1/procnetdev2 ifaces=eth0,eth1
   start name=procnetdev interval=1000000 offset=0

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`,
:ref:`procnetdev(7) <procnetdev>`
