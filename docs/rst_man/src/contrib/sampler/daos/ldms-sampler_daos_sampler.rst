.. _daos_sampler:

===================
daos_sampler
===================

------------------------------------------
Man page for the LDMS DAOS sampler plugin
------------------------------------------

:Date:   28 Apr 2022
:Manual section: 7
:Manual group: LDMS store

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| load name=daos_sampler
| config name=daos_sampler producer=${HOSTNAME}
| start name=daos_sampler interval=1000000

DESCRIPTION
===========

The daos_sampler plugin collects DAOS telemetry from local DAOS I/O
Engine instances.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The daos_sampler plugin uses the sampler_base base class. This man page
only covers the configuration attributes specific to this plugin; see
:ref:`ldms_sampler_base(7) <ldms_sampler_base>` for the attributes of the base class.

name=<plugin_name>
   |
   | This MUST be daos_sampler.

producer=$HOSTNAME
   |
   | The $HOSTNAME variable provides a good unique producer ID.

engine_count=2
   |
   | The default is 2; don't change it unless the number of per-server
     engines is different.

target_count=8
   |
   | The default is 8; don't change it unless the number of targets per
     engine is different.

**SAMPLE FORMAT**

The DAOS telemetry is exposed as a set of trees, with the system name as
the root:

::

     $system/$rank/$target - Per-engine target metrics not associated with a pool
     $system/$rank/$pool - Per-engine top-level pool metrics
     $system/$rank/$pool/$target - Per-engine target metrics associated with a pool

Under each tree is a set of metrics in either counter or gauge format.
Counters are monotonically-increasing uint64 values; gauges are
instantaneous-read uint64 values that can vary up or down. Certain gauge
metrics may have associated statistics in min/max/count/mean/stddev
format.

**EXAMPLE SAMPLER USAGE**

Start ldmsd as usual, for example:

::

   $ ldmsd -m1MB -x sock:10444 -F -c /path/to/sampler.conf

NOTE: The default memory size (512KB) may be too small for the number of
metrics collected. Larger sizes may be specified for a large number of
pools.

Once ldmsd is running, it is possible to check that the DAOS telemetry
appears in the output of ldms_ls, for example:

::

   $ ldms_ls -h localhost -x sock -p 10444 -l
   daos_server/0/0: consistent, last update: Wed Aug 25 18:40:25 2021 +0000 [653335us]
   M char[]     system                                     "daos_server"
   M u32        rank                                       0
   M u32        target                                     0
   D u64        io/latency/update/256B                     0
   D u64        io/latency/update/256B/min                 0
   D u64        io/latency/update/256B/max                 0
   D u64        io/latency/update/256B/samples             0
   D d64        io/latency/update/256B/mean                0.000000
   D d64        io/latency/update/256B/stddev              0.000000
   D u64        io/latency/update/32KB                     611
   D u64        io/latency/update/32KB/min                 611
   D u64        io/latency/update/32KB/max                 611
   D u64        io/latency/update/32KB/samples             1
   D d64        io/latency/update/32KB/mean                611.000000
   D d64        io/latency/update/32KB/stddev              0.000000
   D u64        io/latency/update/64KB                     0
   D u64        io/latency/update/64KB/min                 0
   D u64        io/latency/update/64KB/max                 0
   D u64        io/latency/update/64KB/samples             0
   D d64        io/latency/update/64KB/mean                0.000000
   D d64        io/latency/update/64KB/stddev              0.000000
   D u64        io/latency/update/128KB                    1018
   D u64        io/latency/update/128KB/min                567
   D u64        io/latency/update/128KB/max                1214
   D u64        io/latency/update/128KB/samples            8
   D d64        io/latency/update/128KB/mean               828.000000
   D d64        io/latency/update/128KB/stddev             238.011404
