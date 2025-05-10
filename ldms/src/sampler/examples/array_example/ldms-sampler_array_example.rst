.. _array_example:

====================
array_example
====================

-------------------------------------------
Man page for the LDMS array_example plugin
-------------------------------------------

:Date:   10 Feb 2018
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| config name=array_example [ <attr>=<value> ]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. The array_example plugin demonstrates use of array
types in ldms.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The array_example plugin uses the sampler_base base class. This man page
covers only the configuration attributes, or those with default values,
specific to the this plugin; see :ref:`ldms_sampler_base(7) <ldms_sampler_base>` for the
attributes of the base class.

**config**
   | name=<plugin_name> [schema=<sname> num_metrics=<num_metrics>
     num_ele=<num_ele> type=<type>]
   | configuration line

   name=<plugin_name>
      |
      | This MUST be array_example.

   schema=<schema>
      |
      | Optional schema name. It is intended that the same sampler on
        different nodes with different metrics have a different schema.
        If not specified, will default to \`array_example`.

   num_metrics=<num_metrics>
      |
      | The number of metrics in the schema. Defaults to a set, one each
        for on a variety of types.

   num_ele=<num_ele>
      |
      | The number of elements in each array. All arrays have the same
        number of elements. Defaults to 10.

   type=<type>
      |
      | The type of metric arrays, e.g., U64_ARRAY, U8_ARRAY, etc.
        Defaults to a set with examples for a variety of types.

BUGS
====

No known bugs.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=array_example
   config name=array_example producer=vm1_1 instance=vm1_1/array_example
   start name=array_example interval=1000000

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`
