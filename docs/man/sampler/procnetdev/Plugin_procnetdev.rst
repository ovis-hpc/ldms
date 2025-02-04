=================
Plugin_procnetdev
=================

:Date:   10 Dec 2018

NAME
====

Plugin_procnetdev - man page for the LDMS procnetdev plugin

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| config name=procnetdev [ <attr> = <value> ]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. The procnetdev plugin provides network info from
/proc/net/dev.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The procnetdev plugin uses the sampler_base base class. This man page
covers only the configuration attributes, or those with default values,
specific to the this plugin; see ldms_sampler_base.man for the
attributes of the base class.

**config**
   | name=<plugin_name> ifaces=<ifs>
   | configuration line

   name=<plugin_name>
      |
      | This MUST be procnetdev.

   ifaces=<ifs>
      |
      | CSV list of ifaces. Order matters. Non-existent ifaces will be
        included and default to 0-value data.

   schema=<schema>
      |
      | Optional schema name. It is intended that the same sampler on
        different nodes with different metrics or ifaces have a
        different schema. If not specified, will default to
        \`procnetdev`.

BUGS
====

Interfaces list is limited to 20.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=procnetdev
   config name=procnetdev producer=vm1_1 instance=vm1_1/procnetdev iface=eth0,eth1
   start name=procnetdev interval=1000000

SEE ALSO
========

ldmsd(8), ldms_quickstart(7), ldmsd_controller(8), ldms_sampler_base(7)
