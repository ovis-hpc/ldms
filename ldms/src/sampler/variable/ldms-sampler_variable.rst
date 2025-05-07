.. _variable:

===============
variable
===============

--------------------------------------
Man page for the LDMS variable plugin
--------------------------------------

:Date:   08 Jul 2020
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| config name=variable [ <attr>=<value> ]

DESCRIPTION
===========

The variable plugin provides test data with a periodically redefined
schema and set. Currently the period is every 4th sample. The data of
the sampler is monotonically increasing integers. The data set size
changes with each redefinition.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The variable plugin does not use the sampler_base base class, but
follows the naming conventions of sampler_base except for schema and
instance name.

**config**
   | name=<plugin_name> [schema=<sname>]
   | configuration line

   name=<plugin_name>
      |
      | This MUST be variable.

   schema=<schema>
      |
      | Optional schema name prefix. The string given will be suffixed
        with an integer N in the range 1-9 to create the schema name.
        The schema will also contain N integer metrics.

   instance=<inst>
      |
      | Optional instance name prefix. The string given will be suffixed
        with an integer in the range 1-9 to create the instance name. If
        not specified, will default prefix is \`$HOST/variable`.

NOTES
=====

The intent of the sampler is to simulate any sampler which may under
some condition redefine the same instance name and schema name for a set
after properly retiring a different definition using the same names. It
is not for production use.

To collect CSV data from this sampler, configure 9 store policies
matching ${schema}[1-9], since the current storage policy mechanism does
not allow matching multiple schemas.

BUGS
====

No known bugs.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=variable
   config name=variable producer=vm1_1 instance=vm1_1/variable
   start name=variable interval=1000000

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`
