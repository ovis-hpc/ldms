.. _synthetic:

================
synthetic
================

---------------------------------------
Man page for the LDMS synthetic plugin
---------------------------------------

:Date:   18 Feb 2018
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| config name=synthetic [ <attr>=<value> ]

DESCRIPTION
===========

The synthetic plugin provides metrics yielding waves offset by
component_id

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The fptrans plugin uses the sampler_base base class. This man page
covers only the configuration attributes, or those with default values,
specific to the this plugin; see :ref:`ldms_sampler_base(7) <ldms_sampler_base>` for the
attributes of the base class.

**config**
   | name=<plugin_name> [schema=<sname>] [origin=<f> height=<f>
     period=<f>]
   | configuration line

   name=<plugin_name>
      |
      | This MUST be synthetic

   schema=<schema>
      |
      | Optional schema name. It is intended that the same sampler on
        different nodes with different metrics have a different schema.
        If not specified, it will default to \`synthetic`.

   origin=<origin>
      |
      | The zero time for periodic functions.

   height=<height>
      |
      | The amplitude of functions.

   period=<period>
      |
      | The function period.

NOTES
=====

None.

BUGS
====

No known bugs.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=synthetic
   config name=synthetic producer=vm1_1 instance=vm1_1/synthetic
   start name=synthetic interval=1000000

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`
