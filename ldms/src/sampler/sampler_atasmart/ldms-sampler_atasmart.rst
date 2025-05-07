.. _sampler_atasmart:

=======================
sampler_atasmart
=======================

----------------------------------------------
Man page for the LDMS sampler_atasmart plugin
----------------------------------------------

:Date:   18 Feb 2018
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| config name=sampler_atasmart [ <attr>=<value> ]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. The sampler_atasmart plugin provides disk info via
sampler_atasmart.

WARNING: This sampler is unsupported.

ENVIRONMENT
===========

To build this sampler, the tasmart library must be loaded.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The sampler_atasmart plugin uses the sampler_base base class. This man
page covers only the configuration attributes, or those with default
values, specific to the this plugin; see :ref:`ldms_sampler_base(7) <ldms_sampler_base>` for the
attributes of the base class.

**config**
   | name=<plugin_name> [schema=<sname>] disks=<disks>
   | configuration line

   name=<plugin_name>
      |
      | This MUST be sampler_atasmart.

   schema=<schema>
      |
      | Optional schema name. It is intended that the same sampler on
        different nodes with different metrics have a different schema.
        If not specified, will default to \`sampler_atasmart`.

   disks
      |
      | A comma-separated list of disk names (e.g., /dev/sda,/dev/sda1)

BUGS
====

No known bugs.

NOTES
=====

-  This sampler is unsupported.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=sampler_atasmart
   config name=sampler_atasmart producer=vm1_1 instance=vm1_1/sampler_atasmart component_id=1
   start name=sampler_atasmart interval=1000000

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`
