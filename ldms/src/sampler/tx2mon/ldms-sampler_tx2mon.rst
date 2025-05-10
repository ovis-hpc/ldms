.. _tx2mon:

=============
tx2mon
=============

------------------------------------
Man page for the LDMS tx2mon plugin
------------------------------------

:Date:   25 Dec 2020
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

| Within ldmsd configuration
| config name=tx2mon [ <attr> = <value> ]

DESCRIPTION
===========

The tx2mon plugin provides cpu and system-on-chip information from
/sys/bus/platform/devices/tx2mon/[socinfo, node<i>_raw] and reports it
in the same units as the tx2mon command-line utility.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The standard options from sampler_base apply. The specific options for
tx2mon are listed here

**config**
   | name=tx2mon <standard options> [array=<bool>] [extra=<bool>]
     [auto-schema=<bool>]

   schema=<schema>
      |
      | Optional schema name. It is required by most storage backends
        that the same sampler on different nodes with different metric
        subsets needs to have a unique schema name. Use auto-schema=1
        instead of or in addition to schema to automatically meet the
        backend requirement.

   auto-schema=<bool>
      |
      | If true, change the schema name to tx2mon_$X, where $X will be a
        unique value derived from the data selection options. If both
        schema and auto-schema=1 are given, the schema name given is
        used as the base instead of "tx2mon".

   array=<bool>
      |
      | For per-core data, report all array value elements if true.
        Report only maximum and minimum values if false. The default is
        false.

   extra=<bool>
      |
      | For per-core data, report additional information of the internal
        block frequencies and the set system metrics. These additional
        values are static. If false, additional information will not be
        reported. The default is false.

METRICS
=======

The sampler_base standard metrics are included. The following data is
reported in a set instance per socket.

::

   node                 Number of socket i from
                        /sys/bus/platform/devices/tx2mon/node<i>_raw

The metrics listed here are named as their respective fields in
tx2mon/mc_oper_region.h. Where applicable, metrics are converted to the
units listed here from the raw values.

::

   counter        Snapshot counter of the cpu.


   Include the following metrics when array=true:
   freq_cpu[]     Frequency reading of each core.
   tmon_cpu[]     Temperature reading of each core. (deg. C).

Include the following metrics when array=false:

::

   freq_cpu_min   Minimum value found in freq_cpu.
   freq_cpu_max   Maximum value found in freq_cpu.
   tmon_cpu_min   Minimum value found in tmon_cpu. (deg. C)
   tmon_cpu_max   Maximum value found in tmon_cpu. (deg. C)

Include the following metrics unconditionally:

::

   tmon_soc_avg   Average temperature on the SoC. (deg. C)
   pwr_core       Power consumed by all cores on the SoC. (Watt).
   pwr_sram       Power consumed by all internal SRAM on the SoC. (Watt).
   pwr_mem        Power consumed by the LLC ring on the SoC. (Watt)
   pwr_soc        Power consumed by SoC blocks that are misc. (Watt)
   v_core         Voltage consumed by all cores on the SoC. (V)
   v_sram         Voltage consumed by all internal SRAM on the SoC. (V)
   v_mem          Voltage consumed by the LLC ring on the SoC. (V)
   v_soc          Voltage consumed by SoC blocks that are misc. (V).
   active_evt     Provides a bit list of active events that are causing throttling.
   Temperature    Active event with a bit flag where 1 is true.
   Power          Active event with a bit flag where 1 is true.
   External       Active event with a bit flag where 1 is true.
   Unk3           Active event with a bit flag where 1 is true.
   Unk4           Active event with a bit flag where 1 is true.
   Unk5           Active event with a bit flag where 1 is true.
   temp_evt_cnt   Total number of temperature events.
   pwr_evt_cnt       Total number of power events.
   ext_evt_cnt       Total number of exteral events.
   temp_throttle_ms  Time duration of all temperature events in ms.
   pwr_throttle_ms   Time duration of all power events in ms.
   ext_throttle_ms   Time duration of all external events in ms.
   cpu_num        Which processor the data comes from.

Include the following metrics with extra=true:

::

   temp_abs_max       Absolute maximum limit of temperature beyond
                      which the SoC will throttle voltage and frequency.
   temp_soft_thresh   Soft limit of temperature beyond which the SoC will
                      throttle voltage and frequency down.
   temp_hard_thresh   Hard limit of temperature beyond which the SoC will
                      throttle voltage and frequency down.
   freq_mem_net       Frequency reading of the SoC and ring connection.
   freq_max           Maximum limit of SoC frequency. Depends on the SKU.
   freq_min           Minimum limit of SoC frequency. Depends on the SKU.
   freq_socs          Internal block frequency of SOC South clock. (Mhz)
   freq_socn          Internal block frequency of SOC North clock. (Mhz)

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=tx2mon
   config name=tx2mon producer=vm1_1 component_id=1 instance=vm1_1/tx2mon
   start name=tx2mon interval=1000000

NOTES
=====

By default, root privilege is required to read the data files produced
by tx2mon_kmod. The kernel module tx2mon_kmod must be loaded, e.g. by
"modprobe /lib/modules/$(uname -r)/extra/tx2mon_kmod.ko".

The current generated schema names are: tx2mon, tx2mon_01,
tx2mon_11_$n_core, and tx2mon_10_$n_core, where the suffix is derived as
\_(array)(extra)[_ncore]. "tx2mon" is used when tx2mon_00 would occur.
If present, $n_core is the size of the array metrics.

There is additional power consumed by cross-socket interconnect, PCIe,
DDR and other IOs that is not currently reported by this tool.

tx2mon reports on the sensors monitored by the on-chip management
controller. Some of the on-chip components (such as the IO blocks) do
not have sensors and therefore the voltage and power measurements of
these blocks are not provided by tx2mon.

On systems that are not arm 64 (aarch64 from uname), the sampler does
nothing. On systems that are aarch64 but missing
/sys/bus/platform/devices/tx2mon, the sampler issues an error about the
missing tx2mon kernel module.

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`
