.. _store_slurm:

==================
store_slurm
==================

------------------------------------------
Man page for the LDMSD store_slurm plugin
------------------------------------------

:Date:   30 Sep 2019
:Manual section: 7
:Manual group: LDMS store

SYNOPSIS
========

Within ldmsd_controller or a configuration file: **load**
**name=store_slurm**

**config** **name=store_slurm** **path=**\ *STORE_ROOT_PATH* [
**verbosity=\ (0\ \|\ 1\ \|\ 2)** ]

**strgp_add** **name=**\ *STRGP_NAME* **plugin=store_slurm**
**container=**\ *CONTAINER* **schema=mt-slurm**

**strgp_prdcr_add** **name=**\ *STRGP_NAME* **regex=**\ *PRDCR_REGEX*

DESCRIPTION
===========

**store_slurm** is an LDMSD storage plugin that stores job data from
**slurm_sampler** specifically, and must not be used with other data.

PLUGIN CONFIG OPTIONS
=====================

**name=store_slurm**
   This MUST be store_slurm (the name of the plugin).

**path=**\ *STORE_ROOT_PATH*
   The path to the root of the store. SOS container for each schema
   specified by the storage policy (**strgp**) will be placed in the
   *STORE_ROOT_PATH* directory.

**verbosity=(**\ *0*\ **\|**\ *1*\ **\|**\ *2*\ **)**

   *0*
      (default) for SUMMARY verbosity level. The storage plugin only
      stores single entry for each job.

   *1*
      for RANK verbosity level. The storage plugin stores job data entry
      per each rank (process) in the job.

   *2*
      for TIME (the most verbosed) verbosity level. The storage plugin
      stores job data entries every time the slurm_sampler set is
      updated. In this verbosity level, we would have a lot of job
      entries that are the same in everything except for the timestamp.

STORAGE POLICY
==============

An LDMSD storage plugin is like a storage driver that provides only
storing mechanism. A storage policy (**strgp**) is a glue binding data
sets from various producers to a container of a storage plugin.

**strgp_add** command defines a new storage policy, identified by
**name**. The **plugin** attribute tells the storage policy which
storage plugin to work with. The **schema** attribute identifies LDMS
schema the data set of which is consumed by the storage policy. The
**container** attribute identifies a container inside the storage plugin
that will store data.

The **schema** for **store_slurm** is always *mt-slurm* as
**slurm_sampler** restricts "mt-slurm" as its schema name.

**strgp_prdcr_add** is a command to specify producers that feed data to
the storage policy.

BUGS
====

No known bugs.

EXAMPLES
========

Plugin configuration + prdcr example:

   ::

      load name=store_slurm
      config name=store_slurm path=/var/store verbosity=1
      strgp_add name=slurm_strgp plugin=store_slurm container=slurm schema=mt-slurm
      strgp_prdcr_add name=slurm_strgp regex=.*

SEE ALSO
========

:ref:`slurm_sampler(7) <slurm_sampler>`, :ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`,
:ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`.
