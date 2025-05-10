.. _store_timescale:

======================
store_timescale
======================

---------------------------------------------
Man page for the LDMS store_timescale plugin
---------------------------------------------

:Date:   24 Oct 2019
:Manual section: 7
:Manual group: LDMS store

SYNOPSIS
========

| Within ldmsd_controller script or a configuration file:
| load name=store_timescale
| strgp_add name=<policyname> plugin=store_timescale container=<c>
  schema=<s>
| strgp_prdcr_add name=<policyname> regex=.\*
| strgp_start name=<policyname>

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. The timescale_store plugin is a store developed by
Shanghai Jiao Tong University HPC Center to store collected data in
TimescaleDB.

This store is a simplified version of store_influx.

STORE_TIMESCALE CONFIGURATION ATTRIBUTE SYNTAX
==============================================

**config**
   | name=<plugin_name> user=<username> pwfile=<path to password file>
     hostaddr=<host ip addr> port=<port no> dbname=<database name>
     measurement_limit=<sql statement length>
   | ldmsd_controller configuration line

   name=<plugin_name>
      |
      | This MUST be store_timescale.

   user=<username>
      |
      | This option is required; It will be used as the user name to
        connect to timescaledb.

   pwfile=<full path to password file>
      |
      | This option is required; The file must have content
        secretword=<password>, the password will be used as the password
        to connect to timescaledb.

   hostaddr=<host ip addr>
      |
      | This option is required; It will be used as the ip addr of
        timescaledb to connect to.

   port=<port no>
      |
      | This option is required; It will be used as the port number of
        timescaledb to connect to.

   dbname=<database name>
      |
      | This option is required; It will be used as the timescaledb
        database name to connect to.

   measurement_limit=<sql statement length>
      |
      | This is optional; It specifies the maximum length of the sql
        statement to create table or insert data into timescaledb;
        default 8192.

STRGP_ADD ATTRIBUTE SYNTAX
==========================

The strgp_add sets the policies being added. This line determines the
output files via identification of the container and schema.

**strgp_add**
   | plugin=store_timescale name=<policy_name> schema=<schema>
     container=<container>
   | ldmsd_controller strgp_add line

   plugin=<plugin_name>
      |
      | This MUST be store_timescale.

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

   load name=store_timescale


   strgp_add name=store_tutorial1 plugin=store_timescale schema=test1 container=tutorial_sampler1


   strgp_prdcr_add name=store_tutorial1 regex=.*


   strgp_start name=store_tutorial1


   strgp_add name=store_tutorial2 plugin=store_tutorial schema=test2 container=tutorial_sampler2


   strgp_prdcr_add name=store_tutorial2 regex=.*


   strgp_start name=store_tutorial2


   strgp_add name=store_tutorial3 plugin=store_tutorial schema=test3 container=tutorial_sampler3


   strgp_prdcr_add name=store_tutorial3 regex=.*


   strgp_start name=store_tutorial3

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`,
:ref:`tutorial_sampler(7) <tutorial_sampler>`, :ref:`store_csv(7) <store_csv>`
