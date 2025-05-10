.. _aries_linkstatus:

=======================
aries_linkstatus
=======================

---------------------------------------------------------
Man page for the linkstatus plugin for Cray Aries systems
---------------------------------------------------------

:Date:   4 Jan 2018
:Manual section: 7
:Manual group: LDMS sampler


SYNOPSIS
========

| Within ldmsd_controller or in a configuration file
| config name=cray_aries_linkstatus [ <attr> = <value> ]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. aries_linkstatus reads the send and recv status
information from where it is exposed via gpcdr.

Note that the cray_system_sampler variants have the capability to gather
linkstatus information provided by gpcdr using the configuration and
flag for HSN. For XE/XK systems, linkstatus metrics are reasonably
gathered as part of the cray_gemini_r sampler's gathering of the link
aggregated network counter values. However, for XC (Aries) systems, we
recommend gathering the network counter metrics via the aries_nic_mmr
and aries_rtr_mmr samplers (which use the ioctls) and the link status
metrics via this sampler (which reads from the filesystem location where
gpcdr exposes these values. In order to reduce the overhead, then, we
recommend that this sampler collect at lower frequencies than the
network counter samplers.

The aries_linkstatus sampler is built and used independently of the
cray_system_sampler variants and of the aries_mmr samplers.

To build the aries_linkstatus sampler, build with the following flag:
**--enable_aries_linkstatus**

The output format is as follows: There is an array metric of length 8
hex values for each tile row. Therefore, there are 5 metrics for each of
send and receive, associated with tiles 00X-01Y. The send and receive
metrics associated with r1, for example, correspond to the 8 values for
tiles 010 - 017.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The aries_linkstatus plugin uses the sampler_base base class. This man
page covers only the configuration attributes, or those with default
values, specific to the this plugin; see :ref:`ldms_sampler_base(7) <ldms_sampler_base>` for the
attributes of the base class.

**config**
   | name=<plugin_name> file_send=<send_file_name>
     file_recv=<recv_file_name> [schema=<sname>]
   | configuration line

   name=<plugin_name>
      |
      | aries_linkstatus

   file_send=<send_file_name>
      |
      | Location of the file with the sendlinkstatus metrics, as
        specified in the gpcdr configuration file. In the Cray-provided
        default gpcdr configuration, this will be
        /sys/devices/virtual/gni/gpcdr0/metricsets/linksendstatus/metrics.

   file_recv=<recv_file_name>
      |
      | Location of the file with the recvlinkstatus metrics, as
        specified in the gpcdr configuration file. In the Cray-provided
        default gpcdr configuration, this will be
        /sys/devices/virtual/gni/gpcdr0/metricsets/linkrecvstatus/metrics.

   schema=<sname>
      |
      | Optional schema name. It is intended that the same sampler on
        different nodes with different metrics have a different schema.
        This will default to aries_linkstatus, if unspecified.

NOTES
=====

-  The file_send and file_recv can be the same file, if gpcdr is
   configured that way. However, the sampler will do an separate pass
   over the file for each type of metric.

-  The linkstatus metrics are not anticipated to change frequently. In
   order to reduce overhead since the metrics are read from the
   filesystem location where gpcdr exposes these values, it is
   recommended that this sampler collect at lower frequencies than the
   network counter samplers. Reasonable intervals are on order of
   minutes.

-  This sampler is for Cray Aries systems only due to the differing
   format of the names of the linkstatus metrics for Aries vs Gemini. It
   could be extended to handle both.

BUGS
====

No known bugs.

EXAMPLES
========

1) aries_linkstatus: Within ldmsd_controller or in a configuration file:

::

   load name=aries_linkstatus
   config name=aries_linkstatus producer=64 instance=nid00064/aries_linkstatus file_send=/sys/devices/virtual/gni/gpcdr0/metricsets/linksendstatus/metrics file_recv=/sys/devices/virtual/gni/gpcdr0/metricsets/linkrecvstatus/metrics
   start name=aries_linkstatus interval=10000000

::

   #ldms_ls -h nid00064 -x ugni -p 411 -l nid00064/aries_linkstatus

localhost1/aries_linkstatus: consistent, last update: Tue Sep 26
11:35:51 2017 [811278us] M u64 component_id 1 D u64 job_id 0 D u8[]
sendlinkstatus_r0 0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00 D u8[]
sendlinkstatus_r1 0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00 D u8[]
sendlinkstatus_r2 0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03 D u8[]
sendlinkstatus_r3 0x00,0x00,0x00,0x03,0x03,0x03,0x03,0x03 D u8[]
sendlinkstatus_r4 0x03,0x03,0x00,0x03,0x03,0x03,0x03,0x03 D u8[]
recvlinkstatus_r0 0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00 D u8[]
recvlinkstatus_r1 0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00 D u8[]
recvlinkstatus_r2 0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03 D u8[]
recvlinkstatus_r3 0x00,0x00,0x00,0x03,0x03,0x03,0x03,0x03 D u8[]
recvlinkstatus_r4 0x03,0x03,0x00,0x03,0x03,0x03,0x03,0x03

SEE ALSO
========

:ref:`ldmsd(7) <ldmsd>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`, :ref:`cray_system_sampler_variants(7) <cray_system_sampler_variants>`,
:ref:`aries_mmr(7) <aries_mmr>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`
