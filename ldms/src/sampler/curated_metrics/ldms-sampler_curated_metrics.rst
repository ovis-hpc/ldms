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
configuration file. The curated_metrics plugin provides memory info from
/proc/curated_metrics.

This plugin is multi-instance capable.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The curated_metrics plugin uses the sampler_base base class. This man page
covers only the configuration attributes, or those with default values,
specific to the this plugin; see :ref:`ldms_sampler_base(7) <ldms_sampler_base>` for the
attributes of the base class.

**config**
   | name=<plugin_instance_name> [plugin=<plugin_name>] [schema=<sname>] [path=<config_file_path>]
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
        cached - Percentaged of Cached
        inactive - Percentage of Inactive
        active - Percentage of Active
        dirty - Percentage of Dirty
        writeback - Percentage of Writeback
        phys_mem - Total Physical memory (MemTotal)
        load1min - Load average of last minute
        load5min - Load average of last 5 minutes
        load15min - Load average of last 15 minutes
        runnable - Currently runnable kernel scheduling entities
        scheduling_entities - Number of kernel scheduling entities that exist on the system
        newest_pid - PID of the process that was most recently created
        idle - Percentage of idle
        sys - Percentage of sys processes out of cpu total
        user - Percentage of cpu dedicated to user processes
        iowait - Percentage of processes in iowait
        interrupts - Number of interrupts in the time between samples
        context_sw - Number of context switches between samples
        fork - Number of forks between samples
        procs_blocked - Number of processes currently blocked
        iface_name - The name of a interface. May not be specified in metric config file
        rx_bytes - Number of receive bytes for the iface_name between samples
        rx_packets - Number of receive packets for the iface_name between samples
        rx_errs - Number of receive errors for the iface_name between samples
        rx_drop - Number of network packets dropped for the iface_name between samples
        tx_bytes - Number of transfer bytes for the iface_name between samples
        tx_packets - Number of transfer packets for the iface_name between samples
        tx_errs - Number of transer erros for the iface_name between samples

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
