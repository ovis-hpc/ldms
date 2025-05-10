.. _fptrans:

==============
fptrans
==============

-------------------------------------
Man page for the LDMS fptrans plugin
-------------------------------------

:Date:   18 Feb 2018
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| config name=fptrans [ <attr>=<value> ]

DESCRIPTION
===========

The fptrans plugin provides metrics that have well known values which
can be used to test transmission and storage fidelity of single and
double precision scalars and floating point arrays.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The fptrans plugin uses the sampler_base base class. This man page
covers only the configuration attributes, or those with default values,
specific to the this plugin; see :ref:`ldms_sampler_base(7) <ldms_sampler_base>` for the
attributes of the base class.

**config**
   | name=<plugin_name> [schema=<sname>]
   | configuration line

   name=<plugin_name>
      |
      | This MUST be fptrans.

   schema=<schema>
      |
      | Optional schema name. It is intended that the same sampler on
        different nodes with different metrics have a different schema.
        If not specified, it will default to \`fptrans`.

NOTES
=====

The well known values used are 0, 1, and pi as determined by C macro
M_PI.

BUGS
====

No known bugs.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=fptrans
   config name=fptrans producer=vm1_1 instance=vm1_1/fptrans
   start name=fptrans interval=1000000

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`
