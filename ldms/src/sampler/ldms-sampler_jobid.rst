.. _jobid:

============
jobid
============

-----------------------------------
Man page for the LDMS jobid plugin
-----------------------------------

:Date:   03 Dec 2016
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

| Within ldmsd_controller or in a configuration file
| config name=jobid [ <attr>=<value> ]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. The jobid plugin provides jobid info from
/var/run/ldms.jobinfo or similar files replaced periodically by resource
managers. When files are missing, the value 0 or equivalent is reported.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

**config**
   | name=<plugin_name> producer=<pname> instance=<set_name>
     [component_id=<compid> schema=<sname>] [with_jobid=<bool>]
     file=<filepath>
   | configuration line

   name=<plugin_name>
      |
      | This MUST be jobid.

   producer=<pname>
      |
      | The producer name value.

   instance=<set_name>
      |
      | The name of the metric set.

   schema=<schema>
      |
      | Optional schema name. It is intended that the same sampler on
        different nodes with different metrics have a different schema.
        If not specified, will default to \`vmstat`.

   component_id=<compid>
      |
      | Optional component identifier. Defaults to zero.

   with_jobid=<bool>
      |
      | Option to lookup job_id with set or 0 if not. The job_id column
        will always appear, but populated witn zero.

BUGS
====

No known implementation bugs. Design features you may not like: Relies
on site-specific resource manager configuration to produce the file
read. Does not query local or remote manager daemons. May be slow to
sample and generate undesirable filesystem events if filepath is on a
networked filesystem instead of a node-local RAM partition as is usual
in clusters.

NOTES
=====

The colname option from LDMS v2 slurmjobid plugin is no longer
supported. The sampler offset for the jobid plugin should be slightly
less than all other plugins to ensure consistency in the job information
reported for a given time interval across all other plugins. The time
interval for the jobid plugin need only be approximately the clock
granularity of the resource manager.

Other samplers use the jobid plugin as the jobid data source. If the
jobid sampler is not loaded, these samplers will report 0 jobid values.

FILE FORMAT
===========

The file consists of key-value pairs, one per line, separated by an
equals-sign. The recognized keys are:

JOBID
   An unsigned integer (up to 64-bit) identifying the job. The number
   zero is reserved to mean that no job is currently running.

UID
   An unsigned integer (up to 64-bit) representing the User ID
   associated with the job.

APPID
   An unsigned integer (up to 64-bit) representing the an application ID
   for the job.

USER
   A string representing the username associated with the job.

Only the JOBID field is required. The other fields are optional, and
will default to zero.

EXAMPLES
========

::

   Within ldmsd_controller or in a configuration file
   load name=jobid
   config name=jobid component_id=1 producer=vm1_1 instance=vm1_1/jobid
   start name=jobid interval=1000000 offset=-100000


   Within ldmsd_controller or in a configuration file
   load name=jobid
   config name=jobid component_id=1 producer=vm1_1 instance=vm1_1/jobid file=/var/run/rman/node/jobinfo
   start name=jobid interval=1000000 offset=-100000

Slurm 2.x installations can populate /var/run/ldms.jobid by adding the
following lines to slurm.epilog and slurm.prolog, respectively.

::


   echo "JOBID=0" > /var/run/ldms.jobinfo

   and

   echo JOBID=$SLURM_JOBID > /var/run/ldms.jobinfo
   echo UID=$SLURM_UID >> /var/run/ldms.jobinfo
   echo USER=$SLURM_JOB_USER >> /var/run/ldms.jobinfo

SEE ALSO
========

:ref:`ldms(7) <ldms>`, :ref:`ldmsd(8) <ldmsd>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`
