.. _perfevent:

================
perfevent
================

------------------------------------------------
Man page for the LDMS perfevent sampler plugin.
------------------------------------------------

:Date:   18 Feb 2018
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

| Within ldmsctl
| ldmsctl> config name=perfevent [ <attr>=<value> ]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. The perfevent sampler plugin runs on the nodes and
provides data about the the occurrence of micro-architectural events
using linux perfevent subsystem by accessing hardware performance
counters.

ENVIRONMENT
===========

You will need to build LDMS with --enable-perfevent. Perfevent subsystem
is available since Linux 2.6.31.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The perfevent plugin uses the sampler_base base class. This man page
covers only the configuration attributes, or those with default values,
specific to the this plugin. See :ref:`ldms_sampler_base(7) <ldms_sampler_base>` for the
attributes of the base class; those attributes are specified as part of
the 'init' action arguments.

**config**

| name=<plugin_name> action<action_name> [schema=<sname>]
| configuration line

   name=<plugin_name>
      |
      | This MUST be perfevent.

   schema=<schema>
      |
      | Optional schema name. It is intended that the same sampler on
        different nodes with different metrics have a different schema.

   action=init
      |
      | Perform initialization

   action=del metricname=<string>
      |
      | Deletes the specified event.

   action=ls
      |
      | List the currently configured events.

   action=add metricname=<string> pid=<int> cpu=<int> type=<int> id=<int>
      |
      | Adds a metric to the list of configured events.
      |
      | metricname
      |         The metric name for the event
      | pid
      |         The PID for the process being monitored. The counter will follow
                the process to whichever CPU/core is in use. Note that 'pid' and
                'cpu' are mutually exclusive.
      | cpu
      |         Count this event on the specified CPU. This will accumulate
      |         events across all PID that land on the specified CPU/core. Note
      |         that 'pid' and 'cpu' are mutually exclusive.
      | type
      |         The event type.
      | id
      |         The event id.

   | The pid and cpu arguments allow specifying which process and CPU to
     monitor:
   | pid == 0 and cpu == -1
   |    This measures the calling process/thread on any CPU.
   | pid == 0 and cpu >= 0
   |    This measures the calling process/thread only when running on the specified CPU.
   | pid > 0 and cpu == -1
   |    This measures the specified process/thread on any CPU.
   | pid > 0 and cpu >= 0
   |    This measures the specified process/thread only when running on the specified CPU.
   | pid == -1 and cpu >= 0
   |    This measures all processes/threads on the specified CPU. This
        requires CAP_SYS_ADMIN capability or a /proc/sys/kernel/perf_event_paranoid value of less than 1.
   | pid == -1 and cpu == -1
   |    This setting is invalid and will return an error.

   For more information visit: http://man7.org/linux/man-pages/man2/perf_event_open.2.html

   **type**
      |
      | This field specifies the overall event type. It has one of the following values:
      | PERF_TYPE_HARDWARE
      |         This indicates one of the "generalized" hardware events provided
                by the kernel. See the id field definition for more details.
      | PERF_TYPE_SOFTWARE
      |         This indicates one of the software-defined events provided by
                the kernel (even if no hardware support is available).
      | PERF_TYPE_TRACEPOINT
      |         This indicates a tracepoint provided by the kernel tracepoint infrastructure.
      | PERF_TYPE_HW_CACHE
      |         This indicates a hardware cache event. This has a special
                encoding, described in the id field definition.
      | PERF_TYPE_RAW
      |         This indicates a "raw" implementation-specific event in the id field.
      | PERF_TYPE_BREAKPOINT (since Linux 2.6.33)
      |         This indicates a hardware breakpoint as provided by the CPU.
                Breakpoints can be read/write accesses to an address as well as
                execution of an instruction address.

   **id**
      |
      | This specifies which event you want, in conjunction with the type field.
      | There are various ways to set the id field that are dependent on
        the value of the previously described type field.
      | What follows are various possible settings for id separated out
        by type.
      | - If type is **PERF_TYPE_HARDWARE**, we are measuring one of the
        generalized hardware CPU events. Not all of these are available
        on all platforms. Set id to one of the following:
      |         PERF_COUNT_HW_CPU_CYCLES
      |                 Total cycles. Be wary of what happens during CPU frequency scaling.
      |         PERF_COUNT_HW_INSTRUCTIONS
      |                 Retired instructions. Be careful, these can be affected by
                        various issues, most notably hardware interrupt counts.
      |         PERF_COUNT_HW_CACHE_REFERENCES
      |                 Cache accesses. Usually this indicates Last Level Cache accesses
                        but this may vary depending on your CPU. This may include
                        prefetches and coherency messages; again this depends on the design of your CPU.
      |         PERF_COUNT_HW_CACHE_MISSES
      |                 Cache misses. Usually this indicates Last Level Cache misses;
                        this is intended to be used in conjunction with the
      |         PERF_COUNT_HW_CACHE_REFERENCES
      |                 event to calculate cache miss rates.
      |         PERF_COUNT_HW_BRANCH_INSTRUCTIONS
      |                 Retired branch instructions. Prior to Linux 2.6.35, this used the wrong event on AMD processors.
      |         PERF_COUNT_HW_BRANCH_MISSES
      |                 Mispredicted branch instructions.
      |         PERF_COUNT_HW_BUS_CYCLES
      |                 Bus cycles, which can be different from total cycles.
      |         PERF_COUNT_HW_STALLED_CYCLES_FRONTEND (since Linux 3.0)
      |                 Stalled cycles during issue.
      |         PERF_COUNT_HW_STALLED_CYCLES_BACKEND (since Linux 3.0)
      |                 Stalled cycles during retirement.
      |         PERF_COUNT_HW_REF_CPU_CYCLES (since Linux 3.3)
      |                 Total cycles; not affected by CPU frequency scaling.

      | - If type is **PERF_TYPE_SOFTWARE**, we are measuring software events
         provided by the kernel. Set config to one of the following:
      |    PERF_COUNT_SW_CPU_CLOCK
      |            This reports the CPU clock, a high-resolution per-CPU timer.
      |    PERF_COUNT_SW_TASK_CLOCK
      |            This reports a clock count specific to the task that is running.
      |    PERF_COUNT_SW_PAGE_FAULTS
      |            This reports the number of page faults.
      |    PERF_COUNT_SW_CONTEXT_SWITCHES
      |            This counts context switches. Until Linux 2.6.34, these were all
                   reported as user-space events, after that they are reported as
                   happening in the kernel.
      |    PERF_COUNT_SW_CPU_MIGRATIONS
      |            This reports the number of times the process has migrated to a new CPU.
      |    PERF_COUNT_SW_PAGE_FAULTS_MIN
      |            This counts the number of minor page faults. These did not require disk I/O to handle.
      |    PERF_COUNT_SW_PAGE_FAULTS_MAJ
      |            This counts the number of major page faults. These required disk I/O to handle.
      |    PERF_COUNT_SW_ALIGNMENT_FAULTS (since Linux 2.6.33)
      |            This counts the number of alignment faults. These happen when
                   unaligned memory accesses happen; the kernel can handle these but
                   it reduces performance. This happens only on some architectures (never on x86).
      |    PERF_COUNT_SW_EMULATION_FAULTS (since Linux 2.6.33)
      |            This counts the number of emulation faults. The kernel sometimes
                   traps on unimplemented instructions and emulates them for user
                   space. This can negatively impact performance.
      |    PERF_COUNT_SW_DUMMY (since Linux 3.12)
      |            This is a placeholder event that counts nothing. Informational
                   sample record types such as mmap or comm must be associated with an
                   active event. This dummy event allows gathering such records
                   without requiring a counting event.

      | - If type is **PERF_TYPE_TRACEPOINT**, then we are measuring kernel
          tracepoints. The value to use in id can be obtained from under
          debugfs tracing/events/*/*/id if ftrace is enabled in the kernel.

      | - If type is **PERF_TYPE_HW_CACHE**, then we are measuring a hardware CPU
          cache event. To calculate the appropriate id value use the following equation:

      ::

        (perf_hw_cache_id) \| (perf_hw_cache_op_id << 8) \|
        (perf_hw_cache_op_result_id << 16)


      | where ``perf_hw_cache_id`` is one of:

      | PERF_COUNT_HW_CACHE_L1D
      |    for measuring Level 1 Data Cache
      | PERF_COUNT_HW_CACHE_L1I
      |    for measuring Level 1 Instruction Cache
      | PERF_COUNT_HW_CACHE_LL
      |    for measuring Last-Level Cache
      | PERF_COUNT_HW_CACHE_DTLB
      |    for measuring the Data TLB
      | PERF_COUNT_HW_CACHE_ITLB
      |    for measuring the Instruction TLB
      | PERF_COUNT_HW_CACHE_BPU
      |    for measuring the branch prediction unit
      | PERF_COUNT_HW_CACHE_NODE (since Linux 3.1)
      |    for measuring local memory accesses

      | and ``perf_hw_cache_op_id`` is one of

      | PERF_COUNT_HW_CACHE_OP_READ
      |    for read accesses
      | PERF_COUNT_HW_CACHE_OP_WRITE
      |    for write accesses
      | PERF_COUNT_HW_CACHE_OP_PREFETCH
      |    for prefetch accesses and perf_hw_cache_op_result_id is one of
      | PERF_COUNT_HW_CACHE_RESULT_ACCESS
      |    to measure accesses
      | PERF_COUNT_HW_CACHE_RESULT_MISS
      |    to measure misses

      | If type is **PERF_TYPE_RAW**, then a custom "raw" id value is needed.
        Most CPUs support events that are not covered by the "generalized"
        events. These are implementation defined; see your CPU manual (for
        example the Intel Volume 3B documentation or the AMD BIOS and
        Kernel Developer Guide). The libpfm4 library can be used to
        translate from the name in the architectural manuals to the raw hex
        value perf_event_open() expects in this field.

NOTES
=====

The official way of knowing if ``perf_event_open()`` support is enabled is
checking for the existence of the file
/proc/sys/kernel/perf_event_paranoid.

The enum values for type and id are specified in kernel. Here are the
values in version 3.9 (retrieved from
http://lxr.cpsc.ucalgary.ca/lxr/linux+v3.9/include/uapi/linux/perf_event.h#L28):

::

        enum perf_type_id { PERF_TYPE_HARDWARE = 0, PERF_TYPE_SOFTWARE = 1,
        PERF_TYPE_TRACEPOINT = 2, PERF_TYPE_HW_CACHE = 3, PERF_TYPE_RAW = 4,
        PERF_TYPE_BREAKPOINT = 5,

        PERF_TYPE_MAX, /\* non-ABI \*/ };

        enum perf_hw_id { /\* \* Common hardware events, generalized by the
        kernel: \*/ PERF_COUNT_HW_CPU_CYCLES = 0, PERF_COUNT_HW_INSTRUCTIONS =
        1, PERF_COUNT_HW_CACHE_REFERENCES = 2, PERF_COUNT_HW_CACHE_MISSES = 3,
        PERF_COUNT_HW_BRANCH_INSTRUCTIONS = 4, PERF_COUNT_HW_BRANCH_MISSES = 5,
        PERF_COUNT_HW_BUS_CYCLES = 6, PERF_COUNT_HW_STALLED_CYCLES_FRONTEND = 7,
        PERF_COUNT_HW_STALLED_CYCLES_BACKEND = 8, PERF_COUNT_HW_REF_CPU_CYCLES =
        9,

        PERF_COUNT_HW_MAX, /\* non-ABI \*/ };

        /\* \* Generalized hardware cache events: \* \* { L1-D, L1-I, LLC, ITLB,
        DTLB, BPU, NODE } x \* { read, write, prefetch } x \* { accesses, misses
        } \*/ enum perf_hw_cache_id { PERF_COUNT_HW_CACHE_L1D = 0,
        PERF_COUNT_HW_CACHE_L1I = 1, PERF_COUNT_HW_CACHE_LL = 2,
        PERF_COUNT_HW_CACHE_DTLB = 3, PERF_COUNT_HW_CACHE_ITLB = 4,
        PERF_COUNT_HW_CACHE_BPU = 5, PERF_COUNT_HW_CACHE_NODE = 6,

        PERF_COUNT_HW_CACHE_MAX, /\* non-ABI \*/ }; enum perf_hw_cache_op_id {
        PERF_COUNT_HW_CACHE_OP_READ = 0, PERF_COUNT_HW_CACHE_OP_WRITE = 1,
        PERF_COUNT_HW_CACHE_OP_PREFETCH = 2,

        PERF_COUNT_HW_CACHE_OP_MAX, /\* non-ABI \*/ };

        enum perf_hw_cache_op_result_id { PERF_COUNT_HW_CACHE_RESULT_ACCESS = 0,
        PERF_COUNT_HW_CACHE_RESULT_MISS = 1,

        PERF_COUNT_HW_CACHE_RESULT_MAX, /\* non-ABI \*/ };

        /\* \* Special "software" events provided by the kernel, even if the
        hardware \* does not support performance events. These events measure
        various \* physical and sw events of the kernel (and allow the profiling
        of them as \* well): \*/ enum perf_sw_ids { PERF_COUNT_SW_CPU_CLOCK = 0,
        PERF_COUNT_SW_TASK_CLOCK = 1, PERF_COUNT_SW_PAGE_FAULTS = 2,
        PERF_COUNT_SW_CONTEXT_SWITCHES = 3, PERF_COUNT_SW_CPU_MIGRATIONS = 4,
        PERF_COUNT_SW_PAGE_FAULTS_MIN = 5, PERF_COUNT_SW_PAGE_FAULTS_MAJ = 6,
        PERF_COUNT_SW_ALIGNMENT_FAULTS = 7, PERF_COUNT_SW_EMULATION_FAULTS = 8,

        PERF_COUNT_SW_MAX, /\* non-ABI \*/ };

BUGS
====

No known bugs.

EXAMPLES
========

The following is a short example that measures 4 events.
   |
   | * Total CPU cycles
   | * Total CPU instructions
   | * Total branch instructions
   | * Mispredicted branch instructions

| IF we set the value of PID=1234 and CPU_NUM is -1, this measures the
  process with pid=1234 on any CPU. If the CPU_NUM is 1, this measures
  the process with pid=1234 only on CPU 1.
| IF we set the value of PID=-1 and CPU_NUM is 1, this measures all
  processes/threads on the CPU number 1. This requires CAP_SYS_ADMIN
  capability or a /proc/sys/kernel/perf_event_paranoid value of less
  than 1.

::

        $ldmsctl -S $LDMSD_SOCKPATH

        ldmsctl> load name=perfevent

        ldmsctl> config name=perfevent action=add
        metricname="PERF_COUNT_HW_CPU_CYCLES" pid=$PID cpu=$CPU_NUM type=0 id=0

        ldmsctl> config name=perfevent action=add
        metricname="PERF_COUNT_HW_INSTRUCTIONS" pid=$PID cpu=$CPU_NUM type=0 id=1

        ldmsctl> config name=perfevent action=add
        metricname="PERF_COUNT_HW_BRANCH_INSTRUCTIONS" pid=$PID cpu=$CPU_NUM type=0 id=4

        ldmsctl> config name=perfevent action=add
        metricname="PERF_COUNT_HW_BRANCH_MISSES" pid=$PID cpu=$CPU_NUM type=0 id=5

        ldmsctl> config name=perfevent action=init instance=$INSTANCE_NAME
        producer=$PRODUCER_NAME

        ldmsctl> start name=perfevent interval=$INTERVAL_VALUE

        ldmsctl> quit

SEE ALSO
========

:ref:`PERF_EVENT_OPEN(2) <PERF_EVENT_OPEN>`, :ref:`ldmsd(7) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`
