.. _store_sos:

================
store_sos
================

---------------------------------------
Man page for the LDMS store_sos plugin
---------------------------------------

:Date:   21 Dec 2015
:Manual section: 7
:Manual group: LDMS store

SYNOPSIS
========

| load name=NAME plugin=store_sos

DESCRIPTION
===========

The `store_sos` plugin implements an interface between the *ldmsd* storage
infrastructure and the Scalable Object Store (SOS).

To build the store_sos plugin, specify the **--with-sos=PATH** flag where PATH
is the location of the SOS installaction.

This plugin is multi-instance capable.

STORE_SOS INIT CONFIGURATION ATTRIBUTE SYNTAX
=============================================

**config**
   | name=NAME path=PATH [mode=OCTAL] [backend=BACKEND]

   name=NAME
      | This is the storage plugin instance name

   path=PATH
      | The store will place `containers` into the directory named PATH.
      | This directory must exist. The CONTAINER name is specifed when
      | configing the storage policy.

   mode=MODE (default is 0660)
      | An octal number representing the permission bits used when new files
      | are created for the CONTAINER. See the open(3) system call.

   backend=BACKEND (default is "mmos")
      | The BACKEND specifies the storage strategy that will be used for
      | objects stored in the CONTAINER. The BACKEND is one of "mmos" or "lsos"
      | The "mmos" backend is a Memory Mapped Object Store (MMOS). This storage
      | strategy uses the mmap() system call to map file system memory into the
      | address space of the `ldmsd`. This strategy is very efficient for solid
      | state storage devices and local filesystems, but can be very
      | inefficient for distributed file systems. The "lsos" backend is a Log
      | Structured Object Store (LSOS) that uses standard read()/write() and
      | compression to store data in the file system. This strategy is less
      | efficient for object creation and indexing, but is more efficent for
      | distributed storage; using typically less than 1/8 of the storage
      | consumed by MMOS.


NOTES
=====

-  The configuration files do not specify partition names. This is done
   automatically and is an epoch timestamp. See the *time* command.

-  Management of partitions is done outside of *ldmsd*. See the *sos-part*
   command.

BUGS
====

No known bugs.

EXAMPLES
========

Simple store_sos Configuration
------------------------------

This will store metric set data with the schema `meminfo` into the container
/var/tmp/ldms. The example below are the relevant lines in the **ldmsd**
configuration file.

::

   load name=ss1 plugin=store_sos
   config name=ss1 path=/var/tmp
   strgp_add name=strgp_sos plugin=ss1 container=ldms schema=meminfo
   strgp_start name=strgp_sos plugin=ss1 container=ldms schema=meminfo

Store with Two Backends
------------------------

This will store metric set data as described in decomp.conf to two different
containers: one using the MMOS backend and another using the LSOS backend.

You can compare the performance of these two backends using the
`store_time_stats` command.

The relevant lines from a configuration file are as follows::

    load name=mmos plugin=store_sos
    config name=mmos path=/var/tmp/mmos backend=mmos mode=0777

    load name=lsos plugin=store_sos
    config name=lsos path=/var/tmp/lsos backend=lsos mode=0777

    strgp_add name=lsos plugin=lsos container=ldms \
       decomposition=/opt/ovis/etc/decomp.json regex=.*
    strgp_prdcr_add name=lsos regex=.* \
       strgp_start name=lsos

    strgp_add name=mmos plugin=mmos container=ldms \
	decomposition=/opt/ovis/etc/decomp.json regex=.*
    strgp_prdcr_add name=mmos regex=.*

    strgp_start name=mmos
    strgp_start name=lmos

Examine Storage Plugin Performance
----------------------------------

The ldmsd_controller *store_time_stats* command can be used to evaluate plugin performance::

  ldmsd_controller -a munge -p 10002
  Welcome to the LDMSD control processor
  sock:localhost:10002> store_time_stats
  =========================================================================================================================================
  <   Minimum Value          -   Minimum Average value
  >   Maximum Value          +   Maximum Average value
  =========================================================================================================================================
  Schema           Min (us)        Avg (us)        Max (us)        Min Timestamp        Max Timestamp     # of Sets    Count
  ------------------------- --------------- --------------- --------------- -------------------- -------------------- ---------- ----------
      file_importer.fi.helm          1.2800          3.3632          9.9840           1775162441           1775163540          1        699
  <-   file_importer.fi.smart2          1.0240          2.1149          7.1680           1775162271           1775164051          1        696
  >+           perfevent2          5.3760        228.9463      37693.9520           1775162903           1775162207          1      13992
  =========================================================================================================================================
  Storage Policy       Min (us)        Avg (us)        Max (us)        Min Timestamp        Max Timestamp     # of Sets    Count
  ------------------------- --------------- --------------- --------------- -------------------- -------------------- ---------- ----------
  <>+  csv                         1.0240        239.4822      37693.9520           1775162271           1775162207          3       5129
  ------------------ --------------- --------------- --------------- -------------------- -------------------- ---------- ----------
       lsos                        1.0240        230.7347      34880.5120           1775162331           1775162204          3       5129
  ------------------ --------------- --------------- --------------- -------------------- -------------------- ---------- ----------
    -  mmos                        1.0240        155.0978      26222.0800           1775164071           1775163461          3       5129
   ------------------ --------------- --------------- --------------- -------------------- -------------------- ---------- ----------

  Number of store operations per second: 6.4863
  Percentages of each stage:
  commit         :   84.53%
  queue          :   10.40%
  decomp         :    4.34%
  prep           :    0.52%
  wait           :    0.22%


SEE ALSO
========

   :ref:`ldmsd(7) <ldmsd>`
   :ref:`lmsd_decomposition(7) <ldmsd_decomposition>`
   :ref:`sos-part(7) <sos-part>`
