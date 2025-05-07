.. _msr_interlagos:

=====================
msr_interlagos
=====================

--------------------------------------------
Man page for the LDMS msr interlagos plugin
--------------------------------------------

:Date:   04 Jan 2018
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

| Within ldmsd_controller script or a configuration script:
| load name=msr_interlagos
| config name=msr_interlagos action=<value> [ <attr> = <value> ]
| add name=msr_interlagos [ <attr> = <value> ]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or by use of a
configuration file provided as an argument to the "-c" flag when
starting ldmsd. In the case of the configuration file the commands are
the same as those used via the ldmsd_controller interface. The
msr_interlagos plugin provides msr counter information for the AMD
Family 15h Models 00h-0Fh Processors (Interlagos) only.

This is a developmental version of the sampler. It may change at any
time.

The sampler will allow you to select from an identified set of counters.
These are only correctly defined for the AMD Interlagos processor. The
counter addresses, what events are potentially being counted, the event
names, the counter types (core, uncore), etc. are defined in a
configuration file. An example of this file can be found at:
/util/configs/msr/interlagos/bw_msr_configs.

The actual counters desired can be defined/modified at/during run time
using the defined event names subject to the constraint that each
counter can only count a single event name at a time. If a second name
mapping into an already selected counter is selected, the selection will
fail. The event names must be identified via the action=add directive
for each desired event name. When all desired event names have been
added, the directive action=finalize is used to instantiate the event
name to counter mappings.

The metric names are reported as generic names in the output set since
their actual identities may be changed dynamically. For any given
counter the first value (e.g., CTR<NUM>) is the uint64 representation of
the counter configuration used in the counter setup. The subsequent
values (e.g., CTR<NUM>_<XY>) are the values read from the counters (1
per numa node or num core values (with optional additional zero values
if maxcore specified (see more below)).

To build the msr_interlagos sampler, build with the following flags:
**--enable_msr_interlagos**

The ldmsd_controller interface includes functions for manipulating the
sampling state and counter identities as described below.

EXTERNAL MODIFICATION OF COUNTERS AND EXTERNAL INTERACTIONS
===========================================================

Note that a user, with appropriate privilege, can change the identity of
the event being collected via an external methodology such as wrmsr.
Because of this, the msr_interlagos plugin first rechecks the event
identity of each counter before sampling, however this is not atomic so
there is a slight possibility of a race condition where the user may
change the counter between the check and the read. If the check fails
zero values are reported for all metrics for that particular counter,
including the control register(s), and the metric name is a zero length
string. This continues until the identity is reset, either by external
methods or by issuing the action=rewrite directive.

If a user job changes the counters, it is intended that interaction with
the Resource Manager can invoke the rewrite command for the counters
once the user job has exited. A script is supplied that can be called
from epilog to perform this event rewrite. The script is blocking on the
rewrite in order to avoid a race condition with the next job setting the
counters before the rewrite is completed. There is a maximum time time
limit on the blocking call in the script. The script return code
indicates success or failure. Note that options that require the LDMS
daemon to check for a flag set by the scheduler are subject to race
conditions.

COUNTER CONFIGURATION FILE
==========================

**!!!WARNING!!!** This plugin only works for Interlagos. Using this
sampler on other architectures or misconfiguration of the configuration
file may result in unforseen results with possible damage to the system
as the control register addresses will not map into the same
functionality. **!!!WARNING!!!**

Fields in the MSR sampler configuration file are: Name, Write_addr,
Event, Umask, Read_addr, os_user, core_ena, core_sel, special_flag,
ctr_type. Please use or modify the example configuration file provided
in /util/configs/msr/interlagos/bw_msr_configs.

Valid options for core_flag are MSR_DEFAULT and UNCORE_PER_NUMA.
MSR_DEFAULT indicates that the associated register will collect the same
event across all entities (core or numa domain). UNCORE_PER_NUMA is only
valid for uncore counters for which the unit mask can be used to specify
for which target numa domain events are being counted. A unit mask of
"0x0" indicates events will be counted for only the numa domain in which
the counter resides. A unit mask of "0xF" indicates events will be
counted for only numa domains in which the counter does not reside. This
enables understanding cache affinity and the level of IO crossing numa
boundaries. Valid options for ctr_type are CTR_NUMCORE and CTR_UNCORE.
These distinguish core and uncore counters.

Lines starting with a # mark are comments and are skipped.

::

   ##### Core counters ##########
   TLB_DM,  0xc0010200, 0x046, 0x07, 0xc0010201, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
   TOT_CYC, 0xc0010202, 0x076, 0x00, 0xc0010203, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
   L2_DCM,  0xc0010202, 0x043, 0x00, 0xc0010203, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
   L1_DCM,  0xc0010204, 0x041, 0x01, 0xc0010205, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
   L1_DCA,  0xc0010204, 0x040, 0x00, 0xc0010205, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
   #LS_DISP,  0xc0010204, 0x029, 0x01, 0xc0010205, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
   #LS_DISP,  0xc0010204, 0x029, 0x02, 0xc0010205, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
   #LS_DISP,  0xc0010204, 0x029, 0x04, 0xc0010205, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
   LS_DISP,  0xc0010204, 0x029, 0x07, 0xc0010205, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
   RETIRED_FLOPS,  0xc0010206, 0x003, 0xFF, 0xc0010207, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
   DP_OPS,  0xc0010206, 0x003, 0xF0, 0xc0010207, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
   VEC_INS, 0xc0010208, 0x0CB, 0x04, 0xc0010209, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
   TOT_INS, 0xc001020A, 0x0C0, 0x00, 0xc001020B, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
   ##### Uncore counters ##########
   L3_CACHE_MISSES, 0xc0010240, 0x4E1, 0xF7, 0xc0010241, 0x0, 0x1, 0x0, MSR_DEFAULT, CTR_UNCORE
   RW_DRAM_EXT, 0xc0010242, 0x1E0, 0xF, 0xc0010243, 0x0, 0x1, 0x0, UNCORE_PER_NUMA, CTR_UNCORE
   IO_DRAM_INT, 0xc0010242, 0x1E1, 0x0, 0xc0010243, 0x0, 0x1, 0x0, UNCORE_PER_NUMA, CTR_UNCORE
   DCT_PREFETCH, 0xc0010242, 0x1F0, 0x64, 0xc0010243, 0x0, 0x1, 0x0, MSR_DEFAULT, CTR_UNCORE
   DCT_RD_TOT, 0xc0010244, 0x1F0, 0x62, 0xc0010245, 0x0, 0x1, 0x0, MSR_DEFAULT, CTR_UNCORE
   RW_DRAM_INT, 0xc0010246, 0x1E0, 0x0, 0xc0010247, 0x0, 0x1, 0x0, UNCORE_PER_NUMA, CTR_UNCORE
   IO_DRAM_EXT, 0xc0010246, 0x1E1, 0xF, 0xc0010247, 0x0, 0x1, 0x0, UNCORE_PER_NUMA, CTR_UNCORE
   DCT_WRT, 0xc0010246, 0x1F0, 0x19, 0xc0010247, 0x0, 0x1, 0x0, MSR_DEFAULT, CTR_UNCORE
   #
   # Note that for the following, CTR_NUMCORE pairs are:
   # [0] Control: 0xc0010200 Data: 0xc0010201
   # [1] Control: 0xc0010202 Data: 0xc0010203
   # [2] Control: 0xc0010204 Data: 0xc0010205
   # [3] Control: 0xc0010206 Data: 0xc0010207
   # [4] Control: 0xc0010208 Data: 0xc0010209
   # [5] Control: 0xc001020A Data: 0xc001020B
   #
   # And CTR_UNCORE pairs are:
   # [0] Control: 0xc0010240 Data: 0xc0010241
   # [1] Control: 0xc0010242 Data: 0xc0010243
   # [2] Control: 0xc0010244 Data: 0xc0010245
   # [3] Control: 0xc0010246 Data: 0xc0010247
   #
   # The first column below indicates the counters available for a particular
   # feature. For example [2:0] indicates that the core counters (CTR_NUMCORE)
   # 0, 1, and 2, as indicated above, are available to count TLB_DM.
   #
   # NOTE: For the UNCORE_PER_NUMA case, use 0x0 to exclude external numa access
   # and 0xF to exclude local numa access and only count external access.
   ##### Core counters ##########
   #[2:0] TLB_DM,  0xc0010200, 0x046, 0x07, 0xc0010201, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
   #[2:0] TOT_CYC, 0xc0010202, 0x076, 0x00, 0xc0010203, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
   #[2:0] L2_DCM,  0xc0010202, 0x043, 0x00, 0xc0010203, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
   #[5:0] L1_DCM,  0xc0010204, 0x041, 0x01, 0xc0010205, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
   #[5:0] L1_DCA,  0xc0010204, 0x040, 0x00, 0xc0010205, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
   #[5:0] LS_DISP,  0xc0010204, 0x029, 0x01, 0xc0010205, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
   #[5:0] LS_DISP,  0xc0010204, 0x029, 0x02, 0xc0010205, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
   #[5:0] LS_DISP,  0xc0010204, 0x029, 0x04, 0xc0010205, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
   #[5:0] LS_DISP,  0xc0010204, 0x029, 0x07, 0xc0010205, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
   #[3] RETIRED_FLOPS,  0xc0010206, 0x003, 0xFF, 0xc0010207, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
   #[3] DP_OPS,  0xc0010206, 0x003, 0xF0, 0xc0010207, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
   #[5:0] VEC_INS, 0xc0010208, 0x0CB, 0x04, 0xc0010209, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
   #[5:0] TOT_INS, 0xc001020A, 0x0C0, 0x00, 0xc001020B, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
   ##### Uncore counters ##########
   #[3:0] L3_CACHE_MISSES, 0xc0010240, 0x4E1, 0xF7, 0xc0010241, 0x0, 0x1, 0x0, MSR_DEFAULT, CTR_UNCORE
   #[3:0] RW_DRAM_EXT, 0xc0010242, 0x1E0, 0xF, 0xc0010243, 0x0, 0x1, 0x0, UNCORE_PER_NUMA, CTR_UNCORE
   #[3:0] IO_DRAM_INT, 0xc0010242, 0x1E1, 0x0, 0xc0010243, 0x0, 0x1, 0x0, UNCORE_PER_NUMA, CTR_UNCORE
   #[3:0] DCT_PREFETCH, 0xc0010242, 0x1F0, 0x64, 0xc0010243, 0x0, 0x1, 0x0, MSR_DEFAULT, CTR_UNCORE
   #[3:0] DCT_RD_TOT, 0xc0010244, 0x1F0, 0x62, 0xc0010245, 0x0, 0x1, 0x0, MSR_DEFAULT, CTR_UNCORE
   #[3:0] RW_DRAM_INT, 0xc0010246, 0x1E0, 0x0, 0xc0010247, 0x0, 0x1, 0x0, UNCORE_PER_NUMA, CTR_UNCORE
   #[3:0] IO_DRAM_EXT, 0xc0010246, 0x1E1, 0xF, 0xc0010247, 0x0, 0x1, 0x0, UNCORE_PER_NUMA, CTR_UNCORE
   #[3:0] DCT_WRT, 0xc0010246, 0x1F0, 0x19, 0xc0010247, 0x0, 0x1, 0x0, MSR_DEFAULT, CTR_UNCORE

OUTPUT FORMAT
=============

Example output format from the "ldms_ls" command is shown below. Since
the counters can be added in any order and be changed dynamically, the
names are generic (e.g., Ctr0_n) with CtrN_name being the string version
of the name and CtrN_wctl being the write control register (event code
and unit mask for the msr variable assigned to that counter).

This is followed a vector of the values. If there is only 1 value in the
vector, then the name is CtrN. If there is a value per numa domain, then
the name is CtrN_n. If there is a value per core, then the name is
CtrN_c.

If the write control register is the same for all values in the vector,
it is only written once and called CtrN_wctl. If the write control
register is different for the values in the vector, as it would be for
the per numa domain values, then the write control register variable is
a vector of length > 1 and is named CtrN_wctl_n. Zeros in the
CtrN_wctl_n indicate that the "maxcore" value specified in the
configuration of the sampler was greater than the actual number of cores
and hence those wctl and variable data values will be 0.

Example output is below:

::

    nid00010/msr_interlagos: consistent, last update: Sun Oct 30 16:34:16 2016 [4398us]
    M u64        component_id                               10
    D u64        job_id                                     0
    D char[]     Ctr0_name                                  "L3_CACHE_MISSES"
    D u64[]      Ctr0_wctl                                  85903603681
    D u64[]      Ctr0_n                                     8761095,660101,0,0
    D char[]     Ctr1_name                                  "DCT_RD_TOT"
    D u64[]      Ctr1_wctl                                  73018663664
    D u64[]      Ctr1_n                                     16748451,1103973,0,0
    D char[]     Ctr2_name                                  "RW_DRAM_EXT"
    D u64[]      Ctr2_wctl_n                                73018642144,73018641888,0,0
    D u64[]      Ctr2_n                                     4901448,7120727,0,0
    D char[]     Ctr3_name                                  "RW_DRAM_INT"
    D u64[]      Ctr3_wctl_n                                73018638816,73018639072,0,0
    D u64[]      Ctr3_n                                     74099900,3773483,0,0
    D char[]     Ctr4_name                                  "TOT_CYC"
    D u64[]      Ctr4_wctl                                  4391030
    D u64[]      Ctr4_c                                     775759456,2595008788,234822206,155962379,51951208,53210798,82771568,52716295,85501768,50656894,175839012,619930959,179902397,110558187,334344071,353769784,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
    D char[]     Ctr5_name                                  "TOT_INS"
    D u64[]      Ctr5_wctl                                  4391104
    D u64[]      Ctr5_c                                     211085929,410194651,45686350,11096207,4489395,4565853,13261794,3626609,15062986,3753527,3802413,194511990,55444449,7321398,39989531,36190191,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
    D char[]     Ctr6_name                                  "L1_DCM"
    D u64[]      Ctr6_wctl                                  4391233
    D u64[]      Ctr6_c                                     5101215,22654419,1078523,247674,101807,99840,403194,75661,403958,81801,106359,2316889,663984,186842,944343,921712,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
    D char[]     Ctr7_name                                  "RETIRED_FLOPS"
    D u64[]      Ctr7_wctl                                  4456195
    D u64[]      Ctr7_c                                     122,197,408,57,3,0,2,0,0,0,2,131,272,0,13,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
    D char[]     Ctr8_name                                  "VEC_INS"
    D u64[]      Ctr8_wctl                                  4392139
    D u64[]      Ctr8_c                                     13185,32428971,9960,8153,65,0,6517,0,2863,0,280,497910,88393,624,59806,26,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
    D char[]     Ctr9_name                                  "TLB_DM"
    D u64[]      Ctr9_wctl                                  4392774
    D u64[]      Ctr9_c                                     1312,131553,1080,698,154,2,546,3,266,59,125,678,901,196,6254,155,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0

LDMSD_CONTROLLER CONFIGURATION COMMANDS ORDER
=============================================

Configuration commands are intended to be issued in the following order:

-  load

-  config action=initialize

-  config action=add (one or more)

-  config action=finalize (one or more)

-  start

The following config commands can be issued anytime after the start in
any order

-  config action=halt

-  config action=continue

-  config action=reassign

-  config action=rewrite

LDMSD_CONTROLLER CONFIGURATION ATTRIBUTE SYNTAX
===============================================

The msr_interlagos plugin uses the sampler_base base class. This man
page covers only the configuration attributes, or those with default
values, specific to the this plugin; see :ref:`ldms_sampler_base(7) <ldms_sampler_base>` for the
attributes of the base class.

**config**
   | name=<plugin_name> action=<action> [ <attr>=<value> ... ]
   | configuration line

   name=<plugin_name>
      |
      | This MUST be msr_interlagos

   action=<action>
      |
      | Options are initialize, add, finalize, halt, continue, reassign,
        rewrite, and ls:

   **initialize**
      | corespernuma=<cpnuma> conffile=<conffile> [maxcore=<maxcore>
        schema=<schema> ]
      | initialize the plugin. sampler_base configuration arguments
        should be specified at this point.

      corespernuma=<corespernuma>
         |
         | Cores per numa node. Used to determine which and how many
           cores are used in setting counters that report per numa node.

      maxcore=<maxcore>
         |
         | Maxcores that will be reported for all core counters and will
           also be used in counters that report per numa node. Must be
           >= actual number of cores. Any additional values will be
           reported with 0 values. Optional. Defaults to using the
           actual number of cores.

      schema=<schema>
         |
         | Schema name. Optional. Defaults to msr_interlagos.

   **add**
      | metricname=<name>
      | add a counter metric to the set. The metric set will be built in
        the order the metrics are added

      metricname=<name>
         |
         | The name of counter e.g., L3_CACHE_MISSES. Options are listed
           in a separate section of this man page.

   **finalize**
      |
      | creates the set after all the adds. No metrics may be added
        after this point.

   **halt**
      | metricname=<name>
      | halts collection for this counter. Zero values will be returned
        for all metrics for this counter.

      metricname=<name>
         |
         | The name of counter e.g., L3_CACHE_MISSES. metricname=all
           halts all.

   **continue**
      | metricname=<name>
      | continues collection for this counter after a halt.

      metricname=<name>
         |
         | The name of counter e.g., L3_CACHE_MISSES. metricname=all
           continues all.

   **rewrite**
      | metricname=<name>
      | rewrites the counter variable. Used in case the counter variable
        has been changed for this address external to ldms.

      metricname=<name>
         |
         | The name of counter e.g., L3_CACHE_MISSES. metricname=all
           rewrites all counters.

   **reassign**
      | oldmetricname=<oldname> newmetricname=<newname>
      | replaces a metric in the metric set with a new one. It must be
        the same size (e.g., numcores vs single value) as the previous
        counter.

      oldmetricname=<oldname>
         |
         | The name of counter to be replaced e.g., TOT_CYC

      newmetricname=<newname>
         |
         | The name of counter that the previous variable will be
           replaced with e.g., TOT_INS

   **ls**
      |
      | writes info about the intended counters to the log file.

BUGS
====

The sampler is not robust to errors in the configuration file (i.e.,
there is no error checking with respect to registers being written to or
the contents being written). An error could result in unexpected
operation including damage to the host.

NOTES
=====

-  This is a developmental version of the sampler. It may change at any
   time.

-  The format of the configuration file and the fields has changed since
   the v2 release.

-  This plugin only works for Interlagos. Using this sampler on other
   architectures may result in badness as the addresses will not be
   correct.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

| config name=msr_interlagos action=initialize producer=nid00010
  instance=nid00010 component_id=10 corespernuma=8
  conffile=/XXX/msr_conf.txt
| config name=msr_interlagos action=add metricname=L3_CACHE_MISSES
| config name=msr_interlagos action=add metricname=TOT_CYC
| config name=msr_interlagos action=finalize
| config name=msr_interlagos action=reassign oldmetricname=TOT_CYC
  newmetricname=TOT_INS
| config name=msr_interlagos action=halt metricname=TOT_CYC

SEE ALSO
========

:ref:`ldmsd(7) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`,
:ref:`store_function_csv(7) <store_function_csv>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`
