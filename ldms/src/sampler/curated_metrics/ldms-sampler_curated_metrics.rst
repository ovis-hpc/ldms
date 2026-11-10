.. _curated_metrics:

===============
curated_metrics
===============

-------------------------------------
Man page for the LDMS curated_metrics plugin
-------------------------------------

:Date:  17 Oct 2025
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| config name=curated_metrics [ <attr>=<value> ]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. The curated_metrics plugin provides a curated set of
metrics from /proc/meminfo, /proc/loadavg, /proc/stat and /proc/net/dev.
Memory and CPU values are reported as percentages, and event counters
(interrupts, context switches, forks and interface traffic) as per-second
rates over the last sample interval. Per-CPU and per-interface values are
reported as records in the cpu_list and netdev_list metrics.

The first sample after configuration only primes the counters; the set is
updated starting with the second sample.

This plugin is multi-instance capable.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The curated_metrics plugin uses the sampler_base base class. This man page
covers only the configuration attributes, or those with default values,
specific to the this plugin; see :ref:`ldms_sampler_base(7) <ldms_sampler_base>` for the
attributes of the base class.

**config**
   | name=<plugin_instance_name> [plugin=<plugin_name>] [schema=<sname>] [path=<config_file_path>] [max_ifaces=<count>]
   | configuration line

   name=<plugin_instance_name>
      |
      | The instance name for the plugin.

   plugin=<plugin_name>
      |
      | This MUST be curated_metrics. If not included, the "name"
        attribute value is used.

   schema=<schema>
      |
      | Optional schema name. It is intended that the same sampler on
        different nodes with different metrics have a different schema.
        If not specified, will default to \`curated_metrics`.

    path=<config_file_path>
      |
      | Optional file path to a json file that contains a list of metrics to be
        included in the schema. Available metrics are:
        free - Percentage of MemFree
        buff - Percentage of Buffers
        cached - Percentage of Cached
        inactive - Percentage of Inactive
        active - Percentage of Active
        dirty - Percentage of Dirty
        writeback - Percentage of Writeback
        MemTotal - Total Physical memory (MemTotal)
        load1min - Load average of jobs in run queue in last minute
        load5min - Load average of jobs in run queue in last 5 minutes
        load15min - Load average of jobs in run queue in last 15 minutes
        runnable - Currently runnable kernel scheduling entities
        scheduling_entities - Number of kernel scheduling entities that exist on the system
        newest_pid - PID of the process that was most recently created
        cpu_name - Name of the cpu ("cpu" is the aggregate of all cpus).
                   Added automatically when any per-cpu metric is selected.
        idle - Percentage of idle time over the last interval
        sys - Percentage of system time over the last interval
        user - Percentage of user time over the last interval
        iowait - Percentage of iowait time over the last interval
        interrupts - Interrupts per second over the last interval
        context_sw - Context switches per second over the last interval
        fork - Forks per second over the last interval
        procs_blocked - Number of processes currently blocked
        procs_running - Number of processes currently running on CPUs
        iface_name - The name of an interface. Added automatically when any
                     per-interface metric is selected.
        rx_bytes - Bytes received per second for the interface
        rx_packets - Packets received per second for the interface
        rx_errs - Receive errors per second for the interface
        rx_drop - Received packets dropped per second for the interface
        tx_bytes - Bytes transmitted per second for the interface
        tx_packets - Packets transmitted per second for the interface
        tx_errs - Transmit errors per second for the interface

   max_ifaces=<count>
      |
      | Optional number of interface records to reserve in the set. The
        default is the number of entries in "ifaces" when the config file
        has one, otherwise twice the number of interfaces present at
        configuration time plus 8. Interfaces beyond this are not
        reported and a warning is logged once.

CONFIG FILE
===========
Metrics may be selectively specified in a json file passed in the
configuration. Unknown metric names are a configuration error. The
optional "ifaces" list restricts per-interface records to the named
interfaces.

Syntax of file below:

::

   {
           "metrics" : [ "free", "buff", "cached", "rx_bytes" ],
           "ifaces"  : [ "eth0", "ib0" ]
   }


BUGS
====

No known bugs.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=curated_metrics
   config name=curated_metrics producer=vm1_1 instance=vm1_1/curated_metrics
   start name=curated_metrics interval=1000000

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`
