.. _ibnet:

============
ibnet
============

-----------------------------------
Man page for the LDMS ibnet plugin
-----------------------------------

:Date:   19 May 2020
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| config name=ibnet [ <attr>=<value> ]

DESCRIPTION
===========

The ibnet plugin provides port info from InfiniBand equipment supporting
extended hardware counters. Each port is handled in a separate data set.
Overall timing of the data collection process is handled in another
optional data set. Plugins for the ldmsd (ldms daemon) are configured
via ldmsd_controller or a configuration file.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

**config**
   | name=<plugin_name> port-name=<hca> source-list=<lidfile>
     [port-number=<num>] [metric-conf=<metricfile>]
     [node-name-map=<nnmap>] [timing=<timeopt>] [millis=<ms>]
     [producer=<name>] [instance=<name>] [component_id=<uint64_t>]
     [schema=<name_base>] [uid=<user-id>] [gid=<group-id>] [perm=<mode_t
     permission bits>] [debug]
   | configuration line

   name=<plugin_name>
      |
      | This MUST be ibnet.

   producer=<pname>.
      |
      | The producer string value for the timing set. Default is the
        result of gethostname().

   instance=<set_name>
      |
      | The name of the timing metric set. Default is
        $producer/ibnet_timing.

   source-list=<lidfile>
      |
      | Lidfile is the name of a file of LID/port specifications. See
        PORT FILE for format details.

   port-name=<hca> [port-number=<num>]
      |
      | Hca is the name of the local IB interface to access the network.
        Num is the number of the port on the interface used to access
        the network. The default is 1.

   schema=<name_base>
      |
      | Optional schema base name. The default is ibnet. The name base
        is suffixed to create uniquely defined schema names based on the
        plugin options specified.

   component_id=<compid>
      |
      | Optional component identifier for the timing set. Defaults to
        zero.

   metric-conf=<metricfile>
      |
      | The file listing the metric groups to collect. See METRIC GROUPS
        below.

   ca_port=<port>
      |
      | The port number to use, which must be active.

   millis=<millisecond timeout>
      |
      | The number of milliseconds of the timeout on the MAD calls.
        Default 0, which will use the mad library timeout of 1 second.

   timing=<T>
      |
      | Disable timing (T=0), enable aggregate timing (T=1), or enable
        individual port timing(T=2) or enable port offset timing(T=3).
        The metric set will contain sampling process timing metrics if T
        > 0.

   node-name-map=<nnmap>
      |
      | The file name nnmap, as used by ibnetdiscover and opensm, of a
        mapping from IB GUIDs to short names of IB hardware items
        (switch, node, etc) suitable for use in populating names of
        sets.

PORT FILE
=========

The lid/port file format is

::

   lid, hexguid, nports, plist
    * where hexguid is 0x....,
    * nports is int,
    * plist is ints nports long or * if range is 1-nports,
    * if not using a name map, names will be GUID_hex.

The portrange will be an integer expression in the style 1,5,7-9,13,
without repeats, whitespace, reversed ranges, or overlapping ranges. LID
is an integer in the range 0-65535. The same LID may be on multiple
lines so long as the ports listed for it are not repeated.

The lid file can be generated with ldms-gen-lidfile.sh.

METRIC GROUPS
=============

The metric groups file contains a list of items, one per line, naming
groups of metrics to collect. The groups are named corresponding to
groups in the infiniband-diags perfquery utility options. The
correspondence is not exact. To disable a listed metric group, delete
its name from the file or comment it out by prepending a # to the group,
e.g. '#xmtsl'. '#' followed by whitespace is not allowed. Carriage
returns are optional.

INTERNAL METRICS
================

port_query_time
   |
   | Time in seconds spend in the single port MAD call.

port_query_offset
   |
   | Time in microseconds from start of all MAD calls in the current
     update to the end of the mad call for the specific port.

ib_query_time
   |
   | Time in seconds making all MAD calls in the update.

ib_data_process_time
   |
   | Time in seconds decoding all MAD data in the update

BUGS
====

The perfquery extended_speeds option is not supported.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=ibnet
   config name=ibnet producer=compute1 instance=compute1/ibnet component_id=1 port-name=mlx5_0 source-list=/path/lidfile
   start name=ibnet interval=1000000

NOTES
=====

The exact schema name that will be generated can be determined using the
ldms_ibnet_schema_name utility. The subsets available from the fabric
depend on the hardware, firmware, and in some cases the subnet manager
versions.

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`,
:ref:`ldms_ibnet_schema_name(1) <ldms_ibnet_schema_name>`, ldms-ibnet-sampler-:ref:`gen(1) <gen>`.
