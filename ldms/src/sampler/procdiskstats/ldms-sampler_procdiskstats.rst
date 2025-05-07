.. _procdiskstats:

====================
procdiskstats
====================

-------------------------------------------
Man page for the LDMS procdiskstats plugin
-------------------------------------------

:Date:   18 Feb 2018
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| config name=procdiskstats [ <attr>=<value> ]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. The procdiskstats plugin provides disk info.

WARNING: This sampler is unsupported.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The procdiskstats plugin uses the sampler_base base class. This man page
covers only the configuration attributes, or those with default values,
specific to the this plugin; see :ref:`ldms_sampler_base(7) <ldms_sampler_base>` for the
attributes of the base class.

**config**
   | name=<plugin_name> [schema=<sname>] device=<devices>
   | configuration line

   name=<plugin_name>
      |
      | This MUST be procdiskstats.

   schema=<schema>
      |
      | Optional schema name. It is intended that the same sampler on
        different nodes with different metrics have a different schema.
        If not specified, will default to \`procdiskstats`.

   device=<devices>
      |
      | Comma separated list of devices

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

   load name=procdiskstats
   config name=procdiskstats producer=vm1_1 instance=vm1_1/procdiskstats component_id=1
   start name=procdiskstats interval=1000000

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`
