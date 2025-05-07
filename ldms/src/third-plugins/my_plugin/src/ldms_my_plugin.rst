.. _my_plugin:

================
my_plugin
================

---------------------------------------
Man page for the LDMS my_plugin plugin
---------------------------------------

:Date:   26 Sep 2019
:Manual section: 7
:Manual group: LDMS

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| config name=my_plugin [ <attr>=<value> ]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. The my_plugin plugin provides a ticker, like the
clock plugin.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The my_plugin plugin uses the sampler_base base class. This man page
covers only the configuration attributes, or those with default values,
specific to the this plugin; see :ref:`ldms_sampler_base(7) <ldms_sampler_base>` for the
attributes of the base class.

**config**
   | name=<plugin_name> [schema=<sname>]
   | configuration line

   name=<plugin_name>
      |
      | This MUST be my_plugin.

   schema=<schema>
      |
      | Optional schema name.

BUGS
====

No known bugs.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=my_plugin
   config name=my_plugin producer=vm1_1 instance=vm1_1/my_plugin
   start name=my_plugin interval=1000000

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`
