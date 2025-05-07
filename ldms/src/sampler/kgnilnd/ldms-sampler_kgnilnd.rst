.. _kgnilnd:

==============
kgnilnd
==============

-------------------------------------
Man page for the LDMS kgnilnd plugin
-------------------------------------

:Date:   10 Feb 2018
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

| Within ldmsd_controller or in a configuration file
| config name=kgnilnd [ <attr>=<value> ]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. The kgnilnd plugin provides Cray specific info from
/proc/kgnilnd.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The kgnilnd plugin uses the sampler_base base class. This man page
covers only the configuration attributes, or those with default values,
specific to the this plugin; see :ref:`ldms_sampler_base(7) <ldms_sampler_base>` for the
attributes of the base class.

**config**
   | name=<plugin_name> [schema=<sname>]
   | configuration line

   name=<plugin_name>
      |
      | This MUST be kgnilnd.

   schema=<schema>
      |
      | Optional schema name. It is intended that the same sampler on
        different nodes with different metrics have a different schema.
        If not specified, will default to \`kgnilnd`.

BUGS
====

No known bugs.

EXAMPLES
========

Within ldmsd_controller or in a configuration file

::

   load name=kgnilnd
   config name=kgnilnd producer=vm1_1 instance=vm1_1/kgnilnd
   start name=kgnilnd interval=1000000

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`cray_system_sampler_variants(7) <cray_system_sampler_variants>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`,
:ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`
