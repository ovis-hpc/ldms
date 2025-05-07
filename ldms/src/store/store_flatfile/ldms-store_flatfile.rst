.. _store_flatfile:

=====================
store_flatfile
=====================

--------------------------------------------
Man page for the LDMS store_flatfile plugin
--------------------------------------------

:Date:   24 Oct 2019
:Manual section: 7
:Manual group: LDMS store

SYNOPSIS
========

| Within ldmsd_controller script or a configuration file:
| load name=store_flatfile
| config name=store_flatfile path=datadir
| strgp_add plugin=store_flatfile [ <attr> = <value> ]

DESCRIPTION
===========

The flatfile store generates one file per metric with time, producer,
component id, and value columns separated by spaces. The file name is
$datadir/$container/$schema/$metric_name.

STRGP_ADD ATTRIBUTE SYNTAX
==========================

The strgp_add sets the policies being added. This line determines the
output files via identification of the container and schema.

**strgp_add**
   | plugin=store_flatfile name=<policy_name> schema=<schema>
     container=<container>
   | ldmsd_controller strgp_add line

   plugin=<plugin_name>
      |
      | This MUST be store_flatfile.

   name=<policy_name>
      |
      | The policy name for this strgp.

   container=<container>
      |
      | The container and the schema determine where the output files
        will be written (see path above). They also are used to match
        any action=custom configuration.node/meminfo.

   schema=<schema>
      |
      | The container and schema determines where the output files will
        be written (see path above).

NOTES
=====

-  As of LDMS Version 4.3.3 there is a change in behavior. Previously
   there was a choice that an associated value with a metric was its
   udata, rather than the component_id. In the code the variable name
   used was 'comp_id', even though it wasn't necessarily input as such
   in the sampler. his code now explictly gets the component_id by name.

-  We expect to develop additional options controlling output files and
   output file format.

-  There is no option to quote string values, handle rollover, or handle
   buffering.

-  There is a maximum of 20 concurrent flatfile stores.

BUGS
====

-  Numeric array metrics are not presently supported.

EXAMPLES
========

Within ldmsd_controller or in a configuration file

::

   load name=store_flatfile
   config name=store_flatfile path=/XXX/datadir

   # log only Active from the meminfo sampler
   strgp_add name=store_flatfile_meminfo plugin=store_flatfile schema=meminfo container=flat
   strgp_prdcr_add name=store_flatfile_meminfo regex=localhost1
   strgp_metric_add name=store_flatfile_meminfo metric=Active
   strgp_start name=store_flatfile_meminfo regex=localhost1

   # log all from vmstat
   strgp_add name=store_flatfile_vmstat plugin=store_flatfile schema=vmstat container=flat
   strgp_prdcr_add name=store_flatfile_vmstat regex=localhost1
   strgp_start name=store_flatfile_vmstat regex=localhost1

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`
