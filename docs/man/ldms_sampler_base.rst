=================
ldms_sampler_base
=================

:Date:   04 Feb 2018

NAME
====

sampler_base - man page for the LDMS sampler_base which is the base
class for sampler

SYNOPSIS
========

Configuration variable base class for LDMS samplers.

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), sampler plugins for
the ldmsd (ldms daemon) should inherit from the sampler_base base class.
This class defines variables that should be common to all samplers. It
also adds them to the sampler set set and handles their value
assignment.

In order to configure a plugin, one should consult both the plugin
specific man page for the information and configuration arguments
specific to the plugin and this man page for the arguments in the
sampler_base.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

**config**
   name=<plugin_name> producer=<name> instance=<name>
   [component_id=<int>] [schema=<name>] [job_set=<name> job_id=<name>
   app_id=<name> job_start=<name> job_end=<name>]

|
| configuration line

   name=<plugin_name>
      |
      | This will be the name of the plugin being loaded.

   producer=<pname>
      |
      | A unique name for the host providing the data.

   instance=<set_name>
      |
      | A unique name for the metric set.

   schema=<schema>
      |
      | Optional schema name. It is intended that the same sampler on
        different nodes with different metrics have a different schema.
        Defaults to the sampler name.

   component_id=<compid>
      |
      | Optional unique number for the component being monitored,
        Defaults to zero.

   job_set=<name>
      |
      | The instance name of the set containing the job data, default is
        'job_info'.

   job_id=<name>
      |
      | The name of the metric containing the Job Id, default is
        'job_id'.

   app_id=<name>
      |
      | The name of the metric containing the Application Id, default is
        'app_id'.

   job_start=<name>
      |
      | The name of the metric containing the Job start time, default is
        'job_start'.

   job_end=<name>
      |
      | The name of the metric containing the Job end time, default is
        'job_end'.

NOTES
=====

-  This man page does not cover usage of the base class for plugin
   writers.

-  Not all plugins may have been converted to use the base class. The
   plugin specific man page should refer to the sampler_base where this
   has occurred.

BUGS
====

No known bugs.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=meminfo
   config name=meminfo producer=vm1_1 instance=vm1_1/meminfo
   start name=meminfo interval=1000000

SEE ALSO
========

ldmsd(8), ldms_quickstart(7), ldmsd_controller(8),
Plugin_all_example(7), Plugin_aries_linkstatus(7), Plugin_aries_mmr(7),
Plugin_array_example(7), Plugin_clock(7),
Plugin_cray_sampler_variants(7), Plugin_cray_dvs_sampler(7),
Plugin_procdiskstats(7), Plugin_fptrans(7), Plugin_kgnilnd(7),
Plugin_lnet_stats(7), Plugin_meminfo(7), Plugin_msr_interlagos(7),
Plugin_perfevent(7), Plugin_procinterrupts(7), Plugin_procnetdev(7),
Plugin_procnfs(7), Plugin_rapl(7), Plugin_sampler_atasmart(7),
Plugin_sysclassib(7), Plugin_synthetic(7), Plugin_vmstat(7)
