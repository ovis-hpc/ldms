.. _cray_system_sampler_variants:

===================================
cray_system_sampler_variants
===================================

----------------------------------------------------------------
Man page for all variants of the LDMS cray_system_sampler plugin
----------------------------------------------------------------

:Date:   4 Feb 2018
:Manual section: 7
:Manual group: LDMS sampler


SYNOPSIS
========

| Within ldmsd_controller or in a configuration file
| ``config name=cray_gemini_r_sampler [ <attr> = <value> ]
    config name=cray_aries_r_sampler [ <attr> = <value> ]``

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. There are three variants of the cray_system_sampler.

The cray_gemini_r_sampler (previously called cray_system_sampler_r)
provides data from a variety of sources on the XE/XK systems and uses
the gpcdr module for obtaining HSN data. The cray_aries_r_sampler is for
XC systems and uses the gpcdr module for obtaining HSN data.

To build the cray_gemini_r_sampler, build with the following flags:
``--enable_cray_system_sampler --enable-gemini-gpcdr``

To build the cray_aries_r_sampler, build with the following flags:
``--enable_cray_system_sampler --enable-aries-gpcdr``

You may build multiple variants simultaneously.

ENVIRONMENT
===========

If you have built with ``--enable_cray_nvidia`` and intend to collect
data for gpu devices, then the following environment variable must be
set:

LDMSD_CRAY_NVIDIA_PLUGIN_LIBPATH
   Path to libnvidia-ml.so library

If you intend to collect lustre metrics, you must add to

LD_LIBRARY_PATH
   Path to the lustre plugin (same as the LDMSD_PLUGIN_LIBPATH). This
   will be addressed in a future release.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

**config**
   | name=<plugin_name> producer=<pname> instance=<iname>
     hsn_metrics_type=<mtype> llite=<llite> rtrfile=<rtrfile>
     gpu_devices=<gpulist> off_<namespace>=1 [schema=<sname>]
   | configuration line

   | name=<plugin_name>
   | This MUST be cray_gemini_r sampler OR cray_aries_r_sampler.

   schema=<sname>
      |
      | Optional schema name. It is intended that the same sampler on
        different nodes with different metrics have a different schema.
        This will default to cray_aries_r or cray_gemini_r as
        appropriate, if unspecified.

   hsn_metrics_type=<mtype>
      |
      | HSN metrics type identifier - Values are 0,1,2. 0 = counter data
        only, 1 = derived data (for certain HSN values), 2 = both.
        Default is counter data only. (NOTE: Formerly called
        gemini_metrics_type)

   llite=<llite>
      |
      | CSV separated ost list. Non-existent values will be populated
        with 0's. Only relevant if you have built with --enable-lustre.
        This must be specified if built and not turned off.

   rtrfile=<rtrfile>
      |
      | parsed rtr file with media type and link information. For
        cray_gemini_r/d_samplers only.

   gpu_devices=<gpulist>

   CSV separated gpu device names list. For example:
   gpu_devices=Tesla_K20X. (WARNING: quotes around the device list were
   required in V3, but will fail on V4.) Currently, have to use an
   underscore in the name that will be replaced by a space at the time
   of name resolution. This will be revised in a future release.
   Non-existent gpu device names will be populated with 0 valued
   metrics. Only relevant if you have built with --enable_cray_nvidia.

   off_<namespace>=1
      |
      | Optionally turn off collection for any set of metrics. Valid
        options for <namespace> are:

      hsn (both links and nics. NOTE: even if you intend to turn off hsn metrics,
         you must build with the necessary hsn related flags defined).

      vmstat

      loadavg

      current_freemem

      kgnilnd

      procnetdev

      lustre (if built with --enable-lustre)

      nvidia (if built with --enable-cray_nvidia)

      energy (cray_aries_r_sampler only)

   |
   | Multiple different options should be specified as different
     attributes (e.g., off_vmstat=1 off_lustre=1). Note that not
     specifying any gpu_devices or llites will also result in no gpu or
     lustre metrics, respectively, without requiring this flag. Use of
     this flag has precedence over specifying gpu_devices or llites.

GPU INFORMATION
===============

| **NOTE: the GPU is intended for XK systems only.**
| GPU information is gotten from the libnvidia-ml.so. You will need to
  specify the GPU device names about which you want to collect data. An
  empty device list will not attempt to collect for any devices.
  Currently, have to use an underscore in the name that will be replaced
  by a space at the time of name resolution (e.g., Tesla_K20X). This
  will be revised in a future release. Non-existent gpu device names
  will be populated with 0 valued metrics.

In order to enable the ability to collect GPU information, then build
with: **--enable_cray_nvidia** and with **--with-cray-nvidia-inc** set
to the path to nvml.h.

Further, the configuration variable **gpu_devices** will determine
whether gpu information will be collected. If you specify devices, then
also specify the path to libnvidia_ml.so via the environment variable
**LDMSD_CRAY_NVIDIA_PLUGIN_LIBPATH** as described above. If you do not
specify devices, then collection will not be attempted and neither the
environment variable nor the library need exist.

LUSTRE INFORMATION
==================

Lustre information can be gotten from /proc/fs/lustre/llite:

::

   sh-3.2# ls /proc/fs/lustre/llite/
   lustrefs-ffff88081d38f800  snx11024-ffff88041f1aec00
   You will need to specify the Lustre mount points about which you want to collect data (e.g. "lustrefs,snx11024" in this case).

In order to enable the ability to collect Lustre information then build
with: **--enable-lustre** Note that this will also build the
lustre_sampler.

INTERCONNECT INFORMATION FOR THE GEMINI VARIANTS
================================================

Interconnect information may be needed for the cray_gemini_r_samplers if
hsn metrics are on. No such information is needed for the
cray_aries_r_sampler. The interconnect information is produced in two
steps:

1) From the smw as root:
   ``rtr --interconnect >> interconnect.txt``

This produces a list of all the tile, link, and media information

NOTE: This will be used for the calculation of derived metrics for the
gemini gpcdr interfaces since it is the only way to get the media
information to estimate max BW.

NOTE: the hsn_metrics_type flag in the sampler configuration controls
whether counter-only, derived-only, or both types of metrics will be
output to the set. If you use hsn_metrics_type=0 (counter-only) then the
interconnect file is not required to be specified in the configuration
line.

2) On some host:
   ``parse_rtr_dump interconnect.txt >> parsed_interconnect.txt``

This produces a formatted version of the interconnect.txt file which is
greatly reduced in size. Using the even/oddness of the component numbers
and the slot id at one end of the chassis or the other the direction and
the cable/backplane connection information can be derived. This code
produces that look-up information (~31k for a fully connected 3-D torus)
as opposed to the raw data which grows with the system size.

GEMINI PERFORMANCE COUNTER INFORMATION
======================================

The gemini performance counter information will be accessed and
aggregated by link direction via the gpcdr interface. If your system has
the Oct 2013 Cray release CLE 4.2 UP02 or later that provides access to
this information via the gpcdr module. **NOTE: This sampler currently
supports only a specific grcdr-init.config which specifies certain
variables, sample expiration time, and time units. The configuration
file and instructions for using it can be found in util/gemini.**

ARIES PERFORMANCE COUNTER INFORMATION
=====================================

| The aries performance counter information will be accessed via the
  gpcdr module, if the hsn metrics are turned on. **NOTE: Prior to CLE
  5.2 UP05, the default gpcdr configuration erroneously wrote all the
  aries metrics to the same file within /sys/devices. Due to the number
  and size of the values, this file would exceed the supported file
  sizes within /sys. If you have CLE version < 5.2 UP05, replace your
  gpcdr-init.config file with one that splits up the locations of these
  values into separate files consistent with how they are handled in CLE
  5.2 UP05. This configuration file and instructions for using it can be
  found in util/aries. The plugin will FAIL if you do not have the
  expected files for the split metrics.**

GETTING OTHER ARIES PERFORMANCE COUNTER INFORMATION
===================================================

The cray_aries_r_sampler reads the metrics defined by the particular
gpcdr-init.config file. There is a different sampler called aries_mmr
which enables user determined counters to be read (defined in a config
file). Use this sampler if you want different metrics, and optionally
set off_HSN in the cray_aries_r_sampler. This functionality will soon be
ported into the cray_aries_r_sampler.

DATA DIFFERENCES AMONG THE VARIANTS
===================================

The aries transport does not have X, Y, Z directional link aggregation nor X, Y, Z mesh coord information.

The cray_aries_r_sampler also outputs some additional non-HSN-related data available on the XC systems.

NOTES
=====

-  WARNING: The gpu_devices needed to be given in quotes in v3. This
   will fail in v4.

-  As in v3, the cray_gemini_d variant, which obtained gemini
   performance data from the gpcd interface and computed the link
   aggregation has been deprecated.

-  The aries network counters are in fluctuation and may change at any
   time.

-  If you want different counters, see the aries_mmr sampler (and
   related note above).

BUGS
====

No known bugs.

EXAMPLES
========

1) cray_gemini_r_sampler: Within ldmsd_controller or in a configuration
file:

::

   load name=cray_gemini_r_sampler
   config name=cray_gemini_r_sampler producer=64 instance=nid00064/cray_gemini_r_sampler rtrfile=/projects/ldms/parsed_interconnect.txt llite="snx11000" hsn_metrics_type=2 gpu_devices="Tesla_K20X"
   start name=cray_gemini_r_sampler interval=1000000

::

   #ldms_ls -h nid00064 -x ugni -p 411 -l nid00064/cray_gemini_r_sampler
   nid00064/cray_gemini_r_sampler: consistent, last update: Wed Jan 14 15:08:00 2015 [9395us]
   U64 0                nettopo_mesh_coord_X
   U64 4                nettopo_mesh_coord_Y
   U64 0                nettopo_mesh_coord_Z
   U64 0                X+_traffic (B)
   U64 0                X-_traffic (B)
   U64 5443101840963    Y+_traffic (B)
   U64 65444712         Y-_traffic (B)
   U64 11120553955311   Z+_traffic (B)
   U64 11863298704980   Z-_traffic (B)
   U64 0                X+_packets (1)
   U64 0                X-_packets (1)
   U64 192191790458     Y+_packets (1)
   U64 2516793          Y-_packets (1)
   U64 391797850742     Z+_packets (1)
   U64 407129994346     Z-_packets (1)
   U64 0                X+_inq_stall (ns)
   U64 0                X-_inq_stall (ns)
   U64 2918109228198    Y+_inq_stall (ns)
   U64 128960           Y-_inq_stall (ns)
   U64 2849786867843    Z+_inq_stall (ns)
   U64 2022042625490    Z-_inq_stall (ns)
   U64 0                X+_credit_stall (ns)
   U64 0                X-_credit_stall (ns)
   U64 1937719501518    Y+_credit_stall (ns)
   U64 1596117          Y-_credit_stall (ns)
   U64 1020218245751    Z+_credit_stall (ns)
   U64 1434065336035    Z-_credit_stall (ns)
   U64 0                X+_sendlinkstatus (1)
   U64 0                X-_sendlinkstatus (1)
   U64 12               Y+_sendlinkstatus (1)
   U64 12               Y-_sendlinkstatus (1)
   U64 24               Z+_sendlinkstatus (1)
   U64 24               Z-_sendlinkstatus (1)
   U64 0                X+_recvlinkstatus (1)
   U64 0                X-_recvlinkstatus (1)
   U64 12               Y+_recvlinkstatus (1)
   U64 12               Y-_recvlinkstatus (1)
   U64 24               Z+_recvlinkstatus (1)
   U64 24               Z-_recvlinkstatus (1)
   U64 0                X+_SAMPLE_GEMINI_LINK_BW (B/s)
   U64 0                X-_SAMPLE_GEMINI_LINK_BW (B/s)
   U64 145              Y+_SAMPLE_GEMINI_LINK_BW (B/s)
   U64 148              Y-_SAMPLE_GEMINI_LINK_BW (B/s)
   U64 791              Z+_SAMPLE_GEMINI_LINK_BW (B/s)
   U64 0                Z-_SAMPLE_GEMINI_LINK_BW (B/s)
   U64 0                X+_SAMPLE_GEMINI_LINK_USED_BW (% x1e6)
   U64 0                X-_SAMPLE_GEMINI_LINK_USED_BW (% x1e6)
   U64 1                Y+_SAMPLE_GEMINI_LINK_USED_BW (% x1e6)
   U64 0                Y-_SAMPLE_GEMINI_LINK_USED_BW (% x1e6)
   U64 5                Z+_SAMPLE_GEMINI_LINK_USED_BW (% x1e6)
   U64 0                Z-_SAMPLE_GEMINI_LINK_USED_BW (% x1e6)
   U64 0                X+_SAMPLE_GEMINI_LINK_PACKETSIZE_AVE (B)
   U64 0                X-_SAMPLE_GEMINI_LINK_PACKETSIZE_AVE (B)
   U64 29               Y+_SAMPLE_GEMINI_LINK_PACKETSIZE_AVE (B)
   U64 36               Y-_SAMPLE_GEMINI_LINK_PACKETSIZE_AVE (B)
   U64 32               Z+_SAMPLE_GEMINI_LINK_PACKETSIZE_AVE (B)
   U64 0                Z-_SAMPLE_GEMINI_LINK_PACKETSIZE_AVE (B)
   U64 0                X+_SAMPLE_GEMINI_LINK_INQ_STALL (% x1e6)
   U64 0                X-_SAMPLE_GEMINI_LINK_INQ_STALL (% x1e6)
   U64 0                Y+_SAMPLE_GEMINI_LINK_INQ_STALL (% x1e6)
   U64 0                Y-_SAMPLE_GEMINI_LINK_INQ_STALL (% x1e6)
   U64 0                Z+_SAMPLE_GEMINI_LINK_INQ_STALL (% x1e6)
   U64 0                Z-_SAMPLE_GEMINI_LINK_INQ_STALL (% x1e6)
   U64 0                X+_SAMPLE_GEMINI_LINK_CREDIT_STALL (% x1e6)
   U64 0                X-_SAMPLE_GEMINI_LINK_CREDIT_STALL (% x1e6)
   U64 0                Y+_SAMPLE_GEMINI_LINK_CREDIT_STALL (% x1e6)
   U64 0                Y-_SAMPLE_GEMINI_LINK_CREDIT_STALL (% x1e6)
   U64 0                Z+_SAMPLE_GEMINI_LINK_CREDIT_STALL (% x1e6)
   U64 0                Z-_SAMPLE_GEMINI_LINK_CREDIT_STALL (% x1e6)
   U64 7744750941872    totaloutput_optA
   U64 6297626455024    totalinput
   U64 1163023136       fmaout
   U64 6160662230592    bteout_optA
   U64 6160563192021    bteout_optB
   U64 7744745947301    totaloutput_optB
   U64 418              SAMPLE_totaloutput_optA (B/s)
   U64 302              SAMPLE_totalinput (B/s)
   U64 314              SAMPLE_fmaout (B/s)
   U64 5                SAMPLE_bteout_optA (B/s)
   U64 3                SAMPLE_bteout_optB (B/s)
   U64 417              SAMPLE_totaloutput_optB (B/s)
   U64 0                dirty_pages_hits#stats.snx11000
   U64 0                dirty_pages_misses#stats.snx11000
   U64 0                writeback_from_writepage#stats.snx11000
   U64 0                writeback_from_pressure#stats.snx11000
   U64 0                writeback_ok_pages#stats.snx11000
   U64 0                writeback_failed_pages#stats.snx11000
   U64 680152749        read_bytes#stats.snx11000
   U64 789079262        write_bytes#stats.snx11000
   U64 0                brw_read#stats.snx11000
   U64 0                brw_write#stats.snx11000
   U64 0                ioctl#stats.snx11000
   U64 80               open#stats.snx11000
   U64 80               close#stats.snx11000
   U64 12               mmap#stats.snx11000
   U64 919              seek#stats.snx11000
   U64 1                fsync#stats.snx11000
   U64 0                setattr#stats.snx11000
   U64 31               truncate#stats.snx11000
   U64 0                lockless_truncate#stats.snx11000
   U64 2                flock#stats.snx11000
   U64 197              getattr#stats.snx11000
   U64 2                statfs#stats.snx11000
   U64 144              alloc_inode#stats.snx11000
   U64 0                setxattr#stats.snx11000
   U64 530              getxattr#stats.snx11000
   U64 0                listxattr#stats.snx11000
   U64 0                removexattr#stats.snx11000
   U64 2045             inode_permission#stats.snx11000
   U64 0                direct_read#stats.snx11000
   U64 0                direct_write#stats.snx11000
   U64 0                lockless_read_bytes#stats.snx11000
   U64 0                lockless_write_bytes#stats.snx11000
   U64 0                nr_dirty
   U64 0                nr_writeback
   U64 4                loadavg_latest(x100)
   U64 10               loadavg_5min(x100)
   U64 1                loadavg_running_processes
   U64 171              loadavg_total_processes
   U64 32329476         current_freemem
   U64 217016           SMSG_ntx
   U64 102200875        SMSG_tx_bytes
   U64 221595           SMSG_nrx
   U64 56458802         SMSG_rx_bytes
   U64 0                RDMA_ntx
   U64 0                RDMA_tx_bytes
   U64 4614             RDMA_nrx
   U64 1428503591       RDMA_rx_bytes
   U64 4812898          ipogif0_rx_bytes
   U64 939622           ipogif0_tx_bytes
   U64 17699            Tesla_K20X.gpu_power_usage
   U64 225000           Tesla_K20X.gpu_power_limit
   U64 8                Tesla_K20X.gpu_pstate
   U64 24               Tesla_K20X.gpu_temp
   U64 40185856         Tesla_K20X.gpu_memory_used
   U64 0                Tesla_K20X.gpu_agg_dbl_ecc_l1_cache
   U64 0                Tesla_K20X.gpu_agg_dbl_ecc_l2_cache
   U64 0                Tesla_K20X.gpu_agg_dbl_ecc_device_memory
   U64 0                Tesla_K20X.gpu_agg_dbl_ecc_register_file
   U64 0                Tesla_K20X.gpu_agg_dbl_ecc_texture_memory
   U64 0                Tesla_K20X.gpu_agg_dbl_ecc_total_errors
   U64 0                Tesla_K20X.gpu_util_rate

2) cray_aries_r_sampler:

::

   # ldms_ls -h nid00062 -x ugni -p 60020 -l
   nid00062_60020/cray_aries_r_sampler: consistent, last update: Thu Jan 15 13:56:13 2015 [2293us]
   U64 0                traffic_000 (B)
   U64 0                traffic_001 (B)
   U64 0                traffic_002 (B)
   U64 0                traffic_003 (B)
   U64 0                traffic_004 (B)
   U64 0                traffic_005 (B)
   U64 0                traffic_006 (B)
   U64 2808457000       traffic_007 (B)
   U64 0                traffic_008 (B)
   U64 0                traffic_009 (B)
   U64 0                traffic_010 (B)
   U64 0                traffic_011 (B)
   U64 0                traffic_012 (B)
   U64 0                traffic_013 (B)
   U64 0                traffic_014 (B)
   U64 0                traffic_015 (B)
   U64 2798851906       traffic_016 (B)
   U64 2789807213       traffic_017 (B)
   U64 0                traffic_018 (B)
   U64 0                traffic_019 (B)
   U64 0                traffic_020 (B)
   U64 0                traffic_021 (B)
   U64 0                traffic_022 (B)
   U64 0                traffic_023 (B)
   U64 2767648873       traffic_024 (B)
   U64 2390190506       traffic_025 (B)
   U64 2704874433       traffic_026 (B)
   U64 2720454640       traffic_027 (B)
   U64 0                traffic_028 (B)
   U64 0                traffic_029 (B)
   U64 0                traffic_030 (B)
   U64 0                traffic_031 (B)
   U64 0                traffic_032 (B)
   U64 0                traffic_033 (B)
   U64 2409627500       traffic_034 (B)
   U64 2336628220       traffic_035 (B)
   U64 2367285460       traffic_036 (B)
   U64 6804783540       traffic_037 (B)
   U64 0                traffic_038 (B)
   U64 0                traffic_039 (B)
   U64 0                traffic_040 (B)
   U64 0                traffic_041 (B)
   U64 0                traffic_042 (B)
   U64 0                traffic_043 (B)
   U64 2423880460       traffic_044 (B)
   U64 2392290546       traffic_045 (B)
   U64 2391847740       traffic_046 (B)
   U64 4248258393       traffic_047 (B)
   U64 0                stalled_000 (ns)
   U64 0                stalled_001 (ns)
   U64 0                stalled_002 (ns)
   U64 0                stalled_003 (ns)
   U64 0                stalled_004 (ns)
   U64 0                stalled_005 (ns)
   U64 0                stalled_006 (ns)
   U64 276319362        stalled_007 (ns)
   U64 0                stalled_008 (ns)
   U64 0                stalled_009 (ns)
   U64 0                stalled_010 (ns)
   U64 0                stalled_011 (ns)
   U64 0                stalled_012 (ns)
   U64 0                stalled_013 (ns)
   U64 0                stalled_014 (ns)
   U64 0                stalled_015 (ns)
   U64 418881560        stalled_016 (ns)
   U64 421128055        stalled_017 (ns)
   U64 0                stalled_018 (ns)
   U64 0                stalled_019 (ns)
   U64 0                stalled_020 (ns)
   U64 0                stalled_021 (ns)
   U64 0                stalled_022 (ns)
   U64 0                stalled_023 (ns)
   U64 735567222        stalled_024 (ns)
   U64 671234472        stalled_025 (ns)
   U64 736622287        stalled_026 (ns)
   U64 742093982        stalled_027 (ns)
   U64 0                stalled_028 (ns)
   U64 0                stalled_029 (ns)
   U64 0                stalled_030 (ns)
   U64 0                stalled_031 (ns)
   U64 0                stalled_032 (ns)
   U64 0                stalled_033 (ns)
   U64 683488416        stalled_034 (ns)
   U64 678578952        stalled_035 (ns)
   U64 688886648        stalled_036 (ns)
   U64 950587373        stalled_037 (ns)
   U64 0                stalled_038 (ns)
   U64 0                stalled_039 (ns)
   U64 0                stalled_040 (ns)
   U64 0                stalled_041 (ns)
   U64 0                stalled_042 (ns)
   U64 0                stalled_043 (ns)
   U64 591876345        stalled_044 (ns)
   U64 591162967        stalled_045 (ns)
   U64 594832413        stalled_046 (ns)
   U64 524587565        stalled_047 (ns)
   U64 0                sendlinkstatus_000 (1)
   U64 0                sendlinkstatus_001 (1)
   U64 0                sendlinkstatus_002 (1)
   U64 0                sendlinkstatus_003 (1)
   U64 0                sendlinkstatus_004 (1)
   U64 0                sendlinkstatus_005 (1)
   U64 0                sendlinkstatus_006 (1)
   U64 3                sendlinkstatus_007 (1)
   U64 0                sendlinkstatus_008 (1)
   U64 0                sendlinkstatus_009 (1)
   U64 0                sendlinkstatus_010 (1)
   U64 0                sendlinkstatus_011 (1)
   U64 0                sendlinkstatus_012 (1)
   U64 0                sendlinkstatus_013 (1)
   U64 0                sendlinkstatus_014 (1)
   U64 0                sendlinkstatus_015 (1)
   U64 3                sendlinkstatus_016 (1)
   U64 3                sendlinkstatus_017 (1)
   U64 0                sendlinkstatus_018 (1)
   U64 0                sendlinkstatus_019 (1)
   U64 0                sendlinkstatus_020 (1)
   U64 0                sendlinkstatus_021 (1)
   U64 0                sendlinkstatus_022 (1)
   U64 0                sendlinkstatus_023 (1)
   U64 3                sendlinkstatus_024 (1)
   U64 3                sendlinkstatus_025 (1)
   U64 3                sendlinkstatus_026 (1)
   U64 3                sendlinkstatus_027 (1)
   U64 0                sendlinkstatus_028 (1)
   U64 0                sendlinkstatus_029 (1)
   U64 0                sendlinkstatus_030 (1)
   U64 0                sendlinkstatus_031 (1)
   U64 0                sendlinkstatus_032 (1)
   U64 0                sendlinkstatus_033 (1)
   U64 3                sendlinkstatus_034 (1)
   U64 3                sendlinkstatus_035 (1)
   U64 3                sendlinkstatus_036 (1)
   U64 3                sendlinkstatus_037 (1)
   U64 0                sendlinkstatus_038 (1)
   U64 0                sendlinkstatus_039 (1)
   U64 0                sendlinkstatus_040 (1)
   U64 0                sendlinkstatus_041 (1)
   U64 0                sendlinkstatus_042 (1)
   U64 0                sendlinkstatus_043 (1)
   U64 3                sendlinkstatus_044 (1)
   U64 3                sendlinkstatus_045 (1)
   U64 3                sendlinkstatus_046 (1)
   U64 3                sendlinkstatus_047 (1)
   U64 0                recvlinkstatus_000 (1)
   U64 0                recvlinkstatus_001 (1)
   U64 0                recvlinkstatus_002 (1)
   U64 0                recvlinkstatus_003 (1)
   U64 0                recvlinkstatus_004 (1)
   U64 0                recvlinkstatus_005 (1)
   U64 0                recvlinkstatus_006 (1)
   U64 3                recvlinkstatus_007 (1)
   U64 0                recvlinkstatus_008 (1)
   U64 0                recvlinkstatus_009 (1)
   U64 0                recvlinkstatus_010 (1)
   U64 0                recvlinkstatus_011 (1)
   U64 0                recvlinkstatus_012 (1)
   U64 0                recvlinkstatus_013 (1)
   U64 0                recvlinkstatus_014 (1)
   U64 0                recvlinkstatus_015 (1)
   U64 3                recvlinkstatus_016 (1)
   U64 3                recvlinkstatus_017 (1)
   U64 0                recvlinkstatus_018 (1)
   U64 0                recvlinkstatus_019 (1)
   U64 0                recvlinkstatus_020 (1)
   U64 0                recvlinkstatus_021 (1)
   U64 0                recvlinkstatus_022 (1)
   U64 0                recvlinkstatus_023 (1)
   U64 3                recvlinkstatus_024 (1)
   U64 3                recvlinkstatus_025 (1)
   U64 3                recvlinkstatus_026 (1)
   U64 3                recvlinkstatus_027 (1)
   U64 0                recvlinkstatus_028 (1)
   U64 0                recvlinkstatus_029 (1)
   U64 0                recvlinkstatus_030 (1)
   U64 0                recvlinkstatus_031 (1)
   U64 0                recvlinkstatus_032 (1)
   U64 0                recvlinkstatus_033 (1)
   U64 3                recvlinkstatus_034 (1)
   U64 3                recvlinkstatus_035 (1)
   U64 3                recvlinkstatus_036 (1)
   U64 3                recvlinkstatus_037 (1)
   U64 0                recvlinkstatus_038 (1)
   U64 0                recvlinkstatus_039 (1)
   U64 0                recvlinkstatus_040 (1)
   U64 0                recvlinkstatus_041 (1)
   U64 0                recvlinkstatus_042 (1)
   U64 0                recvlinkstatus_043 (1)
   U64 3                recvlinkstatus_044 (1)
   U64 3                recvlinkstatus_045 (1)
   U64 3                recvlinkstatus_046 (1)
   U64 3                recvlinkstatus_047 (1)
   U64 0                SAMPLE_ARIES_TRAFFIC_000 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_001 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_002 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_003 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_004 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_005 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_006 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_007 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_008 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_009 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_010 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_011 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_012 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_013 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_014 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_015 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_016 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_017 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_018 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_019 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_020 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_021 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_022 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_023 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_024 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_025 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_026 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_027 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_028 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_029 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_030 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_031 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_032 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_033 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_034 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_035 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_036 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_037 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_038 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_039 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_040 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_041 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_042 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_043 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_044 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_045 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_046 (B/s)
   U64 0                SAMPLE_ARIES_TRAFFIC_047 (B/s)
   U64 776690512        totaloutput
   U64 1706236864       totalinput
   U64 787546224        fmaout
   U64 1559125          bteout
   U64 0                SAMPLE_totaloutput (B/s)
   U64 0                SAMPLE_totalinput (B/s)
   U64 0                SAMPLE_fmaout (B/s)
   U64 0                SAMPLE_bteout (B/s)
   U64 186510227        energy(J)
   U64 0                dirty_pages_hits#stats.snx11024
   U64 0                dirty_pages_misses#stats.snx11024
   U64 0                writeback_from_writepage#stats.snx11024
   U64 0                writeback_from_pressure#stats.snx11024
   U64 0                writeback_ok_pages#stats.snx11024
   U64 0                writeback_failed_pages#stats.snx11024
   U64 0                read_bytes#stats.snx11024
   U64 0                write_bytes#stats.snx11024
   U64 0                brw_read#stats.snx11024
   U64 0                brw_write#stats.snx11024
   U64 0                ioctl#stats.snx11024
   U64 0                open#stats.snx11024
   U64 0                close#stats.snx11024
   U64 0                mmap#stats.snx11024
   U64 0                seek#stats.snx11024
   U64 0                fsync#stats.snx11024
   U64 0                setattr#stats.snx11024
   U64 0                truncate#stats.snx11024
   U64 0                lockless_truncate#stats.snx11024
   U64 0                flock#stats.snx11024
   U64 0                getattr#stats.snx11024
   U64 0                statfs#stats.snx11024
   U64 0                alloc_inode#stats.snx11024
   U64 0                setxattr#stats.snx11024
   U64 0                getxattr#stats.snx11024
   U64 0                listxattr#stats.snx11024
   U64 0                removexattr#stats.snx11024
   U64 0                inode_permission#stats.snx11024
   U64 0                direct_read#stats.snx11024
   U64 0                direct_write#stats.snx11024
   U64 0                lockless_read_bytes#stats.snx11024
   U64 0                lockless_write_bytes#stats.snx11024
   U64 0                nr_dirty
   U64 0                nr_writeback
   U64 7                loadavg_latest(x100)
   U64 19               loadavg_5min(x100)
   U64 1                loadavg_running_processes
   U64 265              loadavg_total_processes
   U64 64677284         current_freemem
   U64 913429           SMSG_ntx
   U64 585293572        SMSG_tx_bytes
   U64 930111           SMSG_nrx
   U64 276154553        SMSG_rx_bytes
   U64 0                RDMA_ntx
   U64 0                RDMA_tx_bytes
   U64 15065            RDMA_nrx
   U64 1193365117       RDMA_rx_bytes
   U64 28558491         ipogif0_rx_bytes
   U64 1626210          ipogif0_tx_bytes

SEE ALSO
========

:ref:`ldmsd(7) <ldmsd>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`, :ref:`kgnilnd(7) <kgnilnd>`, :ref:`aries_mmr(7) <aries_mmr>`,
:ref:`aries_linkstatus(7) <aries_linkstatus>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`
