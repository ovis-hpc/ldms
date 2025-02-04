===============
Plugin_procstat
===============

:Date:   03 Dec 2016

NAME
====

Plugin_procstat - man page for the LDMS procstat plugin

SYNOPSIS
========

| Within ldmsd_controller or in a configuration file
| config name=procstat [ <attr> = <value> ]

DESCRIPTION
===========

The procstat plugin provides cpu utilization info from /proc/stat,
allowing for hyperthreading and downed core variability. As
hyperthreading might be variable and user selectable depending on system
configuration, the maximum number of cores potentially appearing should
be set in the plugin options with the maxcpu parameter. Cores not
actually appearing will be reported as 0 values.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

See ldms_sampler_base(7) for the common sampler options.

**config**
   | maxcpu=<core_count> <standard options>
   | configuration line

   maxcpu=<core count>
      |
      | Values are 0 to N, where 0 logs only totalized data and N
        reserves slots for N cores. If less than N cores are found,
        0-values are reported. If more than N cores are found, they are
        ignored with an INFO note in the log. Default is the number of
        cores found locally when the sampler is started. If machines
        monitored may have cores disabled or variable hyperthreading
        status, set maxcpu to the most cores that will be reported
        anywhere in the cluster.

   sc_clk_tck=1
      |
      | Enable optional reporting of sysconf(_SC_CLK_TCK), the scheduler
        ticks-per-second defined at kernel build time as CONFIG_HZ,
        collected from sysconf(3). Typically HPC systems use 100, while
        250, 300, 1000 may also occur.

DATA
====

This reports both interrupt count and time processing them. For detailed
interrupt data by type, consider Plugin_procinterrupts(7).

BUGS
====

Reporting all interrupts by name is not implemented.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=procstat
   config name=procstat producer=vm1_1 component_id=1 maxcpu=4 instance=vm1_1/procstat with_jobid=0
   start name=procstat interval=1000000 offset=0

SEE ALSO
========

ldms_sampler_base(7), Plugin_procinterrupts(7), Kernel source
fs/proc/stat.c and proc(5), ldmsd(8), ldmsd_controller(8)
