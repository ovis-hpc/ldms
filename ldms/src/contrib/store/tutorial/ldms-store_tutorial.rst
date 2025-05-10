.. _store_tutorial:

=====================
store_tutorial
=====================

--------------------------------------------
Man page for the LDMS store_tutorial plugin
--------------------------------------------

:Date:   24 Oct 2019
:Manual section: 7
:Manual group: LDMS store

SYNOPSIS
========

| Within ldmsd_controller script or a configuration file:
| load name=store_tutorial
| config name=store_tutorial path=<path>
| strgp_add name=<policyname> plugin=store_tutorial container=<c>
  schema=<s>
| strgp_prdcr_add name=<policyname> regex=.\*
| strgp_start name=<policyname>

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. The tutorial_store plugin is a demo store described
in the LDMSCON2019 tutorial "LDMS v4: Sampler and Store Writing".

This store is a simplified version of store_csv, handling only U64 and
producing no header and with no rollover.

STORE_TUTORIAL CONFIGURATION ATTRIBUTE SYNTAX
=============================================

**config**
   | name=<plugin_name> path=<path>
   | ldmsd_controller configuration line

   name=<plugin_name>
      |
      | This MUST be store_tutorial.

   path=<path>
      |
      | This option is required; the config line or the options file
        must supply a default value. The output files will be put into a
        directory whose root is specified by the path argument. This
        directory must exist; the subdirectories and files will be
        created. The full path to the output files will be
        <path>/<container>/<schema>. Container and schema are set when
        the strgp is added.

STRGP_ADD ATTRIBUTE SYNTAX
==========================

The strgp_add sets the policies being added. This line determines the
output files via identification of the container and schema.

**strgp_add**
   | plugin=store_tutorial name=<policy_name> schema=<schema>
     container=<container>
   | ldmsd_controller strgp_add line

   plugin=<plugin_name>
      |
      | This MUST be store_tutorial.

   name=<policy_name>
      |
      | The policy name for this strgp.

   container=<container>
      |
      | The container and the schema determine where the output files
        will be written (see path above).

   schema=<schema>
      |
      | The container and the schema determine where the output files
        will be written (see path above). You can have multiples of the
        same sampler, but with different schema (which means they will
        have different metrics) and they will be stored in different
        containers (and therefore files).

STORE COLUMN ORDERING
=====================

This store generates output columns in a sequence influenced by the
sampler data registration. Specifically, the column ordering is

   Time, Time_usec, ProducerName, <sampled metric >\*

The column sequence of <sampled metrics> is the order in which the
metrics are added into the metric set by the sampler.

NOTES
=====

None.

BUGS
====

None known.

EXAMPLES
========

Within ldmsd_controller or in a ldmsd command script file

::

   load name=store_tutorial


   config name=store_tutorial path=/tmp/store


   strgp_add name=store_tutorial1 plugin=store_tutorial schema=test1 container=tutorial_sampler1


   strgp_prdcr_add name=store_tutorial1 regex=.*


   strgp_start name=store_tutorial1


   strgp_add name=store_tutorial2 plugin=store_tutorial schema=test2 container=tutorial_sampler2


   strgp_prdcr_add name=store_tutorial2 regex=.*


   strgp_start name=store_tutorial2


   strgp_add name=store_tutorial3 plugin=store_tutorial schema=test3 container=tutorial_sampler3


   strgp_prdcr_add name=store_tutorial3 regex=.*


   strgp_start name=store_tutorial3

| > ls /tmp/store
| tutorial_sampler1 tutorial_sampler2 tutorial_sampler
| > more /tmp/store/tutorial_sampler1/test1
| 1571943275.194664,194664,localhost1,1,0,0,13,26,39,52,65,78,91,104,117,130
| 1571943276.195789,195789,localhost1,1,0,0,14,28,42,56,70,84,98,112,126,140
| 1571943277.196916,196916,localhost1,1,0,0,15,30,45,60,75,90,105,120,135,150
| 1571943278.198051,198051,localhost1,1,0,0,16,32,48,64,80,96,112,128,144,160
| 1571943279.199184,199184,localhost1,1,0,0,17,34,51,68,85,102,119,136,153,170

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`,
:ref:`tutorial_sampler(7) <tutorial_sampler>`, :ref:`store_csv(7) <store_csv>`
