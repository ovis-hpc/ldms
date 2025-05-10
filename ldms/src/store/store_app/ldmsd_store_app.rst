.. _ldmsd_store_app:

================
ldmsd_store_app
================

-------------------------------
LDMSD store_app storage plugin
-------------------------------

:Date:   30 Sep 2019
:Manual section: 7
:Manual group: LDMS store

SYNOPSIS
========

**load** **name**\ =\ **store_app**

**config** **name**\ =\ **store_app** **path**\ =\ *STORE_ROOT_PATH* [
**perm\ =\ OCTAL_PERM** ]

**strgp_add** **name**\ =\ *STRGP_NAME* **plugin**\ =\ **store_app**
**container**\ =\ *CONTAINER_NAME* **schema**\ =\ *LDMS_SCHEMA*

**strgp_prdcr_add** **name**\ =\ *STRGP_NAME*
**regex**\ =\ *PRDCR_REGEX*

DESCRIPTION
===========

``store_app`` is an LDMSD storage plugin for storing data from the
sets from ``app_sampler`` LDMSD sampler plugin. ``store_app``
uses **SOS** as its database back-end. The ``path`` option
points to the directory containing **SOS** containers for this
plugin (one container per ``strgp``). If the container does not
exist, it will be created with permission given by ``perm`` option
(default: 0660). The container contains multiple schemas, each of which
assoicates with a metric from the sets from ``app_sampler`` (e.g.
``stat_utime``). Schemas in the container have the following
attributes:

-  ``timestamp`` : the data sampling timestamp.

-  ``component_id``: the component ID producing the data.

-  ``job_id``: the Slurm job ID.

-  ``app_id``: the application ID.

-  ``rank``: the Slurm task rank.

-  **METRIC_NAME**: the metric value (the name of this attribute is the
   metric name of the metric).

-  ``comp_time``: (indexed) the join of ``component_id`` and
   ``timestamp``.

-  ``time_job``: (indexed) the join of ``timestamp`` and
   ``job_id``.

-  ``job_rank_time``: (indexed) the join of ``job_id``,
   ``rank``, and ``timestamp``.

-  ``job_time_rank``: (indexed) the join of ``job_id``,
   ``timestamp``, and ``rank``.

CONFIG OPTIONS
==============

-  **name**

        The name of the plugin instance to configure.

-  **path**

        The path to the directory that contains SOS containers (one container per strgp).

-  **perm**

        The octal mode (e.g. 0777) that is used in SOS container creation.
        The default is **0660**.

EXAMPLES
========

   ::

      # in ldmsd config file
      load name=store_app
      config name=store_app path=/sos perm=0600
      strgp_add name=app_strgp plugin=mstore_app container=app schema=app_sampler
      # NOTE: the schema in strgp is LDMS set schema, not to confuse with the one
      # schema per metric in our SOS container.
      strgp_prdcr_add name=app_strgp regex=.*
      strgp_start name=app_strgp

The following is an example on how to retrieve the data using Python:

   ::

      from sosdb import Sos
      cont = Sos.Container()
      cont.open('/sos/app')
      sch = cont.schema_by_name('status_vmsize')
      attr = sch.attr_by_name('time_job') # attr to iterate over must be indexed
      itr = attr.attr_iter()
      b = itr.begin()
      while b == True:
        obj = itr.item()
        print(obj['status_vmsize']) # object attribute access by name
        print(obj[5]) # equivalent to above
        print(obj[:]) # get everything at once
        b = itr.next()

SEE ALSO
========

:ref:`app_sampler(7) <app_sampler>`, :ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`,
:ref:`ldmsd_controller(8) <ldmsd_controller>`,
