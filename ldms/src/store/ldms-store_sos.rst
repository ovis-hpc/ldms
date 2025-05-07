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

| Within ldmsd_controller script:
| ldmsd_controller> load name=store_sos
| ldmsd_controller> config name=store_sos path=path
| ldmsd_controller> strgp_add plugin=store_sos [ <attr> = <value> ]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), store plugins for
the ldmsd (ldms daemon) are configured via the ldmsd_controller. The
store_sos plugin is a sos store.

To build the store_sos, build with the following flag: **--enable_sos**

STORE_SOS INIT CONFIGURATION ATTRIBUTE SYNTAX
=============================================

**config**
   | name=<plugin_name> path=<path>
   | ldmsd_controller configuration line

   name=<plugin_name>
      |
      | This MUST be store_sos.

   path=<path>
      |
      | The store will be put into a directory whose root is specified
        by the path argument. This directory must exist; the store will
        be created. The full path to the store will be
        <path>/<container>. The schema(s) determine the schemas of the
        data base. Container and schema are set when the strgp is added.

STRGP_ADD ATTRIBUTE SYNTAX
==========================

The strgp_add sets the policies being added. This line identifies the
container and schema for a store.

**strgp_add**
   | plugin=store_sos name=<policy_name> schema=<schema>
     container=<container> [decomposition=<DECOMP_CONFIG_FILE_JSON>]
   | ldmsd_controller strgp_add line

   plugin=<plugin_name>
      |
      | This MUST be store_sos.

   name=<policy_name>
      |
      | The policy name for this strgp.

   container=<container>
      |
      | The container and schema define the store as described above
        (see path).

   schema=<schema>
      |
      | The container and schema define the store as described above
        (see path). You can have multiples of the same path and
        container, but with different schema (which means they will have
        different metrics) and they will be stored in the same store.

   decomposition=<DECOMP_CONFIG_FILE_JSON>
      |
      | Optionally use set-to-row decomposition with the specified
        configuration file in JSON format. See more about decomposition
        in :ref:`ldmsd_decomposition(7) <ldmsd_decomposition>`.

USING SOS COMMANDS TO MANAGE PARTITIONS
=======================================

Some of the basic sos commands are given below. SOS tools will be built
into XXX. Any commands given with no argument, will return usage info.

**sos_part_query**
   | <container>
   | List the partitions defined in a container.

**sos_part_create**
   | -C <path> [<attr>=<value>] part_name
   | Create a partition.

   **-C**\ *<path>*
      |
      | Path to the container

   **-s**\ *state*
      |
      | State of the new partition (case insensitive). Default is
        OFFLINE. Optional parameter. Valid options are:

   -  PRIMARY: all new allocations go in this partition

   -  ONLINE: objects are accessible, but the partition does not grow

   -  OFFLINE: object references are invalid; the partition may be moved
      or deleted.

   **part_name**
      |
      | Name of the partition

**sos_part_delete**
   | -C <path> <name>
   | Delete a partition in a container. The partition must be in the
     OFFLINE state to be deleted.

   **-C**\ *<path>*
      |
      | Path to the container

   **name**
      |
      | Name of the parition

**sos_part_modify**
   | -C <path> [<attr>=<value>] part_name
   | Modify the state of a partition.

   **-C**\ *<path>*
      |
      | Path to the container

   **-s**\ *state*
      |
      | State of the new partition (case insensitive). Default is
        OFFLINE. Optional parameter. Valid options are:

   -  PRIMARY: all new allocations go in this partition

   -  ONLINE: objects are accessible, but the partition does not grow

   -  OFFLINE: object references are invalid; the partition may be moved
      or deleted.

   **part_name**
      |
      | Name of the partition

**sos_part_move**
   |
   | Move a partition to another storage location. -C <path> -p
     <new_path> part_name

   **-C**\ *<path>*
      |
      | Path to the container

   **-p**\ *<new_path>*
      |
      | The new path.

   **part_name**
      |
      | Name of the partition

USING SOS COMMANDS TO LOOK AT DATA IN A PARTITION
=================================================

sos_cmd can be used to get data from an sos instance. Some relevant
command options are below. Example usage is in the example section.

**sos_cmd**
   | -C <path> -l
   | Print a directory of the schemas.

   **-C**\ *<path>*
      |
      | Path to the container

**sos_cmd**
   | -C <path> -i
   | Show debug information for the container

   **-C**\ *<path>*
      |
      | Path to the container

**sos_cmd**
   | -C <path> -q -S <schema> -X <index> -V <var1> -V <var2>....
   | Print data from a container

   **-C**\ *<path>*
      |
      | Path to the container

   **-q**
      Used to query

   **-S**\ *<schema>*
      |
      | Schema querying against

   **-X**\ *<index>*
      |
      | Variable that is indexed to use in the query.

   **-V**\ *<var>*
      |
      | One or more vars to output.

NOTES
=====

-  The configuration lines do not allow specification of the partition,
   that is done automatically (by default this is the epoch timestamp).

-  Management of partitions is done outside of LDMS (e.g., cron script
   that calls creation of new partitions and changes from PRIMARY to
   ACTIVE).

BUGS
====

No known bugs.

EXAMPLES
========

Configuring store_sos:
----------------------

::

   ldmsd_controller> load name=store_sos
   ldmsd_controller> config name=store_sos path=/XXX/storedir
   ldmsd_controller> strgp_add name=sos_mem_policy plugin=store_sos container=sos schema=meminfo

Querying a container's partitions:
----------------------------------

::

   $ sos_part /NVME/0/SOS_ROOT/Test
    Partition Name       RefCount Status           Size     Modified         Accessed         Path
    -------------------- -------- ---------------- -------- ---------------- ---------------- ----------------
         00000000               3 ONLINE                 1M 2015/08/25 13:49 2015/08/25 13:51 /SOS_STAGING/Test
         00000001               3 ONLINE                 2M 2015/08/25 11:54 2015/08/25 13:51 /NVME/0/SOS_ROOT/Test
         00000002               3 ONLINE                 2M 2015/08/25 11:39 2015/08/25 13:51 /NVME/0/SOS_ROOT/Test
         00000003               3 ONLINE PRIMARY         2M 2015/08/25 11:39 2015/08/25 13:51 /NVME/0/SOS_ROOT/Test

Looking at a container's directory:
-----------------------------------

Variables that are options for -X in the sos_cmd will have indexed = 1

::

   $ sos_cmd -C /NVME/0/LDMS -l
   schema :
       name      : aries_nic_mmr
       schema_sz : 1944
       obj_sz    : 192
       id        : 129
       -attribute : timestamp
           type          : TIMESTAMP
           idx           : 0
           indexed       : 1
           offset        : 8
       -attribute : comp_time
           type          : UINT64
           idx           : 1
           indexed       : 1
           offset        : 16
       -attribute : job_time
           type          : UINT64
           idx           : 2
           indexed       : 1
           offset        : 24
       -attribute : component_id
           type          : UINT64
           idx           : 3
           indexed       : 0
           offset        : 32
       -attribute : job_id
           type          : UINT64
           idx           : 4
           indexed       : 0
           offset        : 40
       -attribute : AR_NIC_NETMON_ORB_EVENT_CNTR_REQ_PKTS
           type          : UINT64
           idx           : 5
           indexed       : 0
           offset        : 48
       -attribute : AR_NIC_NETMON_ORB_EVENT_CNTR_REQ_FLITS
           type          : UINT64
           idx           : 6
           indexed       : 0
           offset        : 56
       -attribute : AR_NIC_NETMON_ORB_EVENT_CNTR_REQ_STALLED
           type          : UINT64
           idx           : 7
           indexed       : 0
           offset        : 64
     ...

Looking at variable values in a container:
------------------------------------------

::

   $ sos_cmd -C /NVME/0/LDMS -q -S aries_nic_mmr -X timestamp -V timestamp -V AR_NIC_NETMON_ORB_EVENT_CNTR_REQ_PKTS
   timestamp                        AR_NIC_NETMON_ORB_EVENT_CNTR_REQ_PKTS
   -------------------------------- ------------------
                  1447449560.003480         1642207034
                  1447449630.002155         1642213993
                  1447449630.003115           88703749
                  1447449630.003673           74768272
                  1447449640.002818           74768367
                  1447449640.003201           88703844
                  1447449640.003249         1642214024
                  1447449650.002885           74768402
                  1447449650.003263         1642214059
                  1447449650.003325           88703874
                  1447449660.002954           74768511
                  1447449660.003308         1642214174
                  1447449660.003444           88703993
                  1447449670.003015           74768547
                  1447449670.003361         1642214205
                  1447449670.003601           88704024
                  1447449680.003081           74768582

SEE ALSO
========

:ref:`ldms(7) <ldms>`, :ref:`store_csv(7) <store_csv>`, :ref:`ldmsd_decomposition(7) <ldmsd_decomposition>`
