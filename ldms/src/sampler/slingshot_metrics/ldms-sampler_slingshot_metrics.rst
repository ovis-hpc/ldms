.. _slingshot_metrics:

========================
slingshot_metrics
========================

----------------------------------------------
Man page for the LDMS slingshot_metrics plugin
----------------------------------------------

:Date:   1 May 2022
:Manual section: 7
:Manual group: LDMS sampler


SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| config name=slingshot_metrics [ <attr> = <value> ]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms aemon) are configured via ldmsd_controller or a
configuration file. The slingshot_metrics plugin provides a single
metric set that contains a list of records. Each record contains all of
the metrics for a single slingshot NIC.

The slingshot_metrics sampler plugin provides detailed counter metrics
for each slignshot NIC.

The schema is named "slingshot_metrics" by default.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The slingshot_metrics plugin uses the sampler_base base class. This man
page covers only the configuration attributes, or those with default
values, specific to the this plugin; see :ref:`ldms_sampler_base(7) <ldms_sampler_base>` for the
attributes of the base class.

**config**
   | name=<plugin_name> [counters=<COUNTER NAMES>] [counters_file=<path
     to counters file>]
   | configuration line

   name=<plugin_name>
      |
      | This MUST be slingshot_metrics.

   counters=<COUNTER NAMES>
      |
      | (Optional) A CSV list of names of slingshot counter names. See
        Section COUTNER NAMES for details. If neither this option nor
        counters_file are specified, a default set of counters will be
        used.

   counters_files=<path to counters file>
      |
      | (Optional) A path to a file that contains a list of counter
        names, one per line. See Section COUNTER NAMES for details. A
        line will be consider a comment if the character on the line is
        a "#". If neither this option nor counters are specified, a
        default set of counters will be used.

   refresh_interval_sec=<seconds>
      |
      | (Optional) The sampler caches the list of slinghost devices, and
        that cache is refreshed at the beginning of a sample cycle if
        the refresh interval time has been exceeded.
        refresh_interval_sec sets the minimum number of seconds between
        refreshes of the device cache. The default refresh interval is
        600 seconds.

COUNTER NAMES
=============

The names of the counters can be found in the slingshot/cassini header
file cassini_cntr_def.h in the array c1_cntr_defs (specifically the
strings in the "name" field of said array entries).

In addition to the individual counter names, this plugin allows
specifying entire groups of counters by using the counter name pattern
"group:<group name>", for insance, "group:hni". The available groups
are: ext, pi_ipd, mb, cq, lpe, hni, ext2. These groups correspond with
the enum c_cntr_group in the cassini_cntr_def.h file. Additionally, one
may use "group:all", which simply includes all available counters.

EXAMPLES
========

Within ldmsd_conteroller or a configuration file:

::

   load name=slingshot_metrics
   config name=slingshot_metrics producer=host1 instance=host1/slingshot_metrics counters=ixe_rx_tcp_pkt,group:hni refresh_interval_sec=3600
   start name=slingshot_metrics interval=1000000 offset=0

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`
