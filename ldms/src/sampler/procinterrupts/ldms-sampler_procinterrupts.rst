.. _procinterrupts:

=====================
procinterrupts
=====================

----------------------------------------
Man page for the LDMS interrupts plugin
----------------------------------------

:Date:   10 Feb 2018
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| config name=interrupts [ <attr>=<value> ]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. The interrupts plugin provides info from
/proc/interrupts. The metric name will be irq.<name>#CPU_NUMBER.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The procinterrupts plugin uses the sampler_base base class. This man
page covers only the configuration attributes, or those with default
values, specific to the this plugin; see :ref:`ldms_sampler_base(7) <ldms_sampler_base>` for the
attributes of the base class.

**config**
   | name=<plugin_name> [schema=<sname>]
   | configuration line

   name=<plugin_name>
      |
      | This MUST be procinterrupts.

   schema=<schema>
      |
      | Optional schema name. It is intended that the same sampler on
        different nodes with different metrics have a different schema.
        If not specified, will default to \`procinterrupts`.

BUGS
====

No known bugs.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=procinterrupts
   config name=procinterrupts producer=1 instance=vm1_1/procinterrupts
   start name=procinterrupts interval=1000000

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`
