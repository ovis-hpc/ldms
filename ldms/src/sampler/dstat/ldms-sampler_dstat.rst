.. _dstat:

============
dstat
============

-----------------------------------
Man page for the LDMS dstat plugin
-----------------------------------

:Date:   4 Nov 2020
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

| Within ldmsd_controller
| config name=dstat [ <attr> = <value> ]

DESCRIPTION
===========

The dstat plugin provides ldmsd process information from
/proc/self/[io,stat,statm,fd].

CONFIGURATION ATTRIBUTE SYNTAX
==============================

**config**
   | name=<plugin_name> component_id=<comp_id> [io=<bool>] [stat=<bool>]
     [statm=<bool>] [mmalloc=<bool>] [fd=<bool>] [fdtypes=<bool>]
     set=<set_name>
   | configuration line

   name=<plugin_name>
      |
      | This MUST be dstat.

   producer=<pname>
      |
      | The producer string value.

   instance=<set_name>
      |
      | The name of the metric set.

   schema=<schema>
      |
      | Optional schema name. It is required by most storage backends
        that the same sampler on different nodes with different metric
        subsets needs to have a unique schema name. Use auto-schema=1
        instead of schema to automatically meet the backend requirement.

   auto-schema=<bool>
      |
      | If true, change the schema name to dstat_$X, where $X will be a
        unique hex value derived from the data selection options. If
        both schema and auto-schema are given, for
        backward-compatibility auto-schema is ignored for the dstat
        plugin.

   component_id=<comp_id>
      |
      | The component id numerical value.

   io=<bool>
      |
      | Include the metrics from /proc/self/io.

   stat=<bool>
      |
      | Include the metrics from /proc/self/stat.

   tick=<bool>
      |
      | Include the sc_clk_tck from :ref:`sysconf(3) <sysconf>` as a metric.

   statm=<bool>
      |
      | Include the metrics from /proc/self/statm.

   mmalloc=<bool>
      |
      | Include the mmap memory usage metric from LDMS mmalloc.

   fd=<bool>
      |
      | Include the number of open file descriptors found in
        /proc/self/fd.

   fdtypes=<bool>
      |
      | Include the number and types of open file descriptors found in
        /proc/self/fd. This option may have high overhead on aggregators
        with many open connections.

DATA
====

This reports metrics from /proc/self/[io,stat,statm] by default. If
specific subsets are named (io=true), then unnamed sets are suppressed.
Units on the /proc metric values are documented in the man pages. The
unit of the mmalloc metric is bytes.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=dstat
   config name=dstat producer=vm1_1 component_id=1 instance=vm1_1/dstat
   start name=dstat interval=1000000

NOTES
=====

See :ref:`proc(5) <proc>` for the definitions of all the metrics except sc_clk_tck and
fd data. Metrics which are invariant (other than pids and sc_clk_tck)
are not included. Where naming is potentially ambiguous and a more
specific name is used in /proc/self/status for the same metrics, the
name from /proc/self/status is used.

Requesting mmalloc or fd or fdtypes (any of which may be high overhead)
requires explicitly requesting it and all others which are wanted.

The numbers listed in /proc/self/fd/ are symbolic links. The "types" of
reported are based on the names pointed to by the links as follows:

::

   fd_count        total number of open file descriptors.
   fd_max          highest file number.
   fd_socket       count of link targets starting with "socket:"
   fd_dev          count of link targets starting with "/dev:"
   fd_anon_inode   count of link targets starting with "anon_inode:"
   fd_pipe         count of link targets starting with "pipe:"
   fd_path         count of link targets starting with . or / but not /dev.

On most HPC Linux systems sc_clk_tck is 100 Hz. Less common values are
250, 300, and 1000.

This is the LDMSD answer to the ancient question "Quis custodiet ipsos
custodes?"

SEE ALSO
========

:ref:`proc(5) <proc>`, :ref:`ldmsd(8) <ldmsd>`, :ref:`sysconf(3) <sysconf>`
