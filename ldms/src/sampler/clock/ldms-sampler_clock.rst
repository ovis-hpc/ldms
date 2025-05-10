.. _clock:

============
clock
============

-----------------------------------
Man page for the LDMS clock plugin
-----------------------------------

:Date:   18 Feb 2018
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| config name=clock [ <attr>=<value> ]

DESCRIPTION
===========

The clock plugin provides a counter of samples taken since it started.
This is of pedagogical interest and useful for detecting situations
where a sample is missed either in being taken or in transmission.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The clock plugin uses the sampler_base base class. This man page covers
only the configuration attributes, or those with default values,
specific to the this plugin; see :ref:`ldms_sampler_base(7) <ldms_sampler_base>` for the
attributes of the base class.

**config**
   | name=<plugin_name> [schema=<sname>]
   | configuration line

   name=<plugin_name>
      |
      | This MUST be clock

   schema=<schema>
      |
      | Optional schema name. It is intended that the same sampler on
        different nodes with different metrics have a different schema.
        If not specified, will default to \`clock`.

BUGS
====

No known bugs.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=clock
   config name=clock producer=vm1_1 instance=vm1_1/clock
   start name=clock interval=1000000 offset=0

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`
