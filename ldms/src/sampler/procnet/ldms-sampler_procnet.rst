.. _procnet:

==============
procnet
==============

-------------------------------------
Man page for the LDMS procnet plugin
-------------------------------------

:Date:   9 Apr 2021
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| config name=procnet [common attributes] [exclude_ports=<devs>]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. The procnet plugin provides network info from
/proc/net/dev, creating a different set for each device, reporting only
active devices, and reporting an active device only when counters
change.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The procnet plugin uses the sampler_base base class. This man page
covers only the configuration attributes, or those with default values,
specific to the this plugin; see :ref:`ldms_sampler_base(7) <ldms_sampler_base>` for the
attributes of the base class.

**config**
   | name=<plugin_name> exclude_ports=<devs>
   | configuration line

   name=<plugin_name>
      |
      | This MUST be procnet.

   exclude_ports=<devs>
      |
      | Comma separated list of ports to exclude.

   schema=<schema>
      |
      | Optional schema name. If not specified, will default to
        \`procnet`.

BUGS
====

Interfaces reported and exclude_ports lists are each limited to 20.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=procnet
   config name=procnet producer=vm1_1 instance=vm1_1/procnet exclude_ports=lo
   start name=procnet interval=1000000

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`
