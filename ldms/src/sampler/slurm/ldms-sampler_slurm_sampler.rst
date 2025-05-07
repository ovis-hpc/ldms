.. _slurm_sampler:

====================
slurm_sampler
====================

--------------------------------------------
Man page for the LDMSD slurm_sampler plugin
--------------------------------------------

:Date:   30 Sep 2019
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

Within ldmsd_controller or a configuration file:
**config** **name=slurm_sampler** **producer=**\ *PRODUCER*
**instance=**\ *INSTANCE* [ **component_id=\ COMP_ID** ] [
**stream=\ STREAM** ] [ **job_count=\ MAX_JOBS** ] [
**task_count=\ MAX_TASKS** ]

DESCRIPTION
===========

**slurm_sampler** is a sampler plugin that collects the information of
the Slurm jobs running on the node. It subscribes to the specified
**stream** to which the **slurm_notifier** SPANK plugin (see
:ref:`slurm_notifier(7) <slurm_notifier>`) publish Slurm job events (default
stream: *slurm*). The sampler supports multi-tenant jobs.

The **job_count** option is the number of slots in the LDMS set
allocated for concurrent jobs. If the number of concurrent jobs on the
node is greater than **job_count**, the new job will occupy the slot of
the oldest job. If **job_count** is not specified, the default value is
*8*.

The **task_count** is the maximum number of tasks per job on the node.
If not specified, it is *CPU_COUNT*. In the event of the sampler failed
to obtain *CPU_COUNT*, the default value is *64*.

CONFIG OPTIONS
==============

**name=slurm_sampler**
   This MUST be slurm_sampler (the name of the plugin).

**producer=**\ *PRODUCER*
   The name of the data producer (e.g. hostname).

**instance=**\ *INSTANCE*
   The name of the set produced by this plugin. This option is required.

**component_id=**\ *COMPONENT_ID*
   An integer identifying the component (default: *0*).

**stream=**\ *STREAM*
   The name of the LDMSD stream to get the job event data.

**job_count=**\ *MAX_JOBS*
   The number of slots to hold job information. If all slots are
   occupied at the time the new job arrived, the oldest slot is reused.
   The default value is *8*.

**task_count=**\ *MAX_TASKS*
   The number of slots for tasks information per job. If not specified,
   the sampler will try to obtain system CPU_COUNT and use it as
   task_count. If it failed, the default value is *64*.

BUGS
====

No known bugs.

EXAMPLES
========

Plugin configuration example:

   ::

      load name=slurm_sampler
      config name=slurm_sampler producer=${HOSTNAME} instance=${HOSTNAME}/slurm \
             component_id=2 stream=slurm job_count=8 task_count=8
      start name=slurm_sampler interval=1000000 offset=0

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`,
:ref:`ldms_sampler_base(7) <ldms_sampler_base>`.
