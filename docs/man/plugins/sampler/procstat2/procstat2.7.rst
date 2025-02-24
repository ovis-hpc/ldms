.. _procstat2:

================
procstat2
================

:Date:   14 Jan 2022

NAME
====

procstat2 - man page for the LDMS procstat2 plugin

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| config name=procstat2 [ <attr>=<value> ]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. The procstat2 plugin provides data from /proc/stat.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The procstat2 plugin uses the sampler_base base class. This man page
covers only the configuration attributes, or those with default values,
specific to the this plugin; see :ref:`ldms_sampler_base(7) <ldms_sampler_base>` for the attributes
of the base class.

**config**
   | name=<plugin_name> [schema=<sname>]
   | configuration line

   name=<plugin_name>
      |
      | This MUST be procstat2.

   schema=<schema>
      |
      | Optional schema name. It is intended that the same sampler on
        different nodes with different metrics have a different schema.
        If not specified, will default to \`procstat2`.

   intr_max=<schema>
      |
      | (Optional). The maximum number of inerrupt numbers supported in
        intr_list. If not specified, intr_max will be the current number
        of interrupts in the intr list.

BUGS
====

No known bugs.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=procstat2
   config name=procstat2 producer=vm1_1 instance=vm1_1/procstat2
   start name=procstat2 interval=1000000

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`
