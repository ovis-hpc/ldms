[#]: # (man)

NAME
====

ldmsd-sampler - LDMSD Usage as a Sampler


SYNOPSIS
========

**ldmsd** -c *SAMPLER.CONF* [options]

Please see **ldmsd**(1) for the full list of daemon options.


DESCRIPTION
===========

To configure an **ldmsd** to sample data from the data source, an instance of a
**sampler plugin** and a **smplr** (sampler policy object) are needed. The
sampler plugin instance governs how to obtain the data, while the smplr governs
when to obtain such data. A sampler plugin instance can associate to at most one
smplr policy. Some sampler plugin does not need smplr policy as they do not
collect data in a regular interval (e.g. **jobinfo** collects job data when an
job started). Please consult plugin manpage whether it requires a smplr to
operate.

The following is a template of a usual use case of a plugin sampler:

```
load name=inst plugin=PLUGIN
config name=inst [OPTIONS]
smplr_add name=smplr instance=inst
smplr_start name=smplr interval=1000000
```

The first line simply load the plugin *PLUGIN* and name the loaded instance as
"inst". A sampler plugin type can have multiple plugin instances. Sampler plugin
instances do not share smplr policy so that they can be independent.

The second line configures the instance `inst`. Please see [COMMON SAMPLER
PLUGIN OPTOINS](#common-sampler-plugin-options) below for common options for all
sampler plugin instance. Please consult the specific plugin manpages for their
options.

The third line add a smplr policy, named "smplr", and associate it with "inst"
instance.

The fourth line start the "smplr" policy which will invoke inst `sample()`
function every 1 sec.


COMMON SAMPLER PLUGIN OPTIONS
=============================

All common options can be omitted. In such case, the default values will be
applied.

<dl>
<dt>`schema`=<em>SCHEMANAME</em></dt>
<dd>The name of the LDMS schema. (Default: *PLUGIN_NAME*)</dd>


<dt>`instance`=<em>LDMS_SET_NAME</em></dt>
<dd>
  The name of the LDMS set to be created by the plugin instance. If the plugin
  instance will create multiple sets, it might use this parameter as a base name
  of the created sets. Please consult the plugin manpage for more information.
  (Default: *LDMSD_NAME/PLUGIN_INST_NAME*  where *LDMSD_NAME* is the name of the
  **ldmsd** (-n option), and *PLUGIN_INST_NAME* is the name of the plugin
  instance)
</dd>

<dt>`component_id`=<em>COMP_ID</em></dt>
<dd>
  The component ID value to assign to the `component_id` metric in the set.
  (Default: 0)
</dd>
</dl>


EXAMPLE
=======

This is an example of configuring an **ldmsd** in sampler mode to sample:
meminfo, lo network device, and eno1 network device.

```
# samp.cfg
load name=mem plugin=meminfo
config name=mem
smplr_add name=smplr_mem instance=mem
smplr_start name=smplr_mem interval=1000000 offset=0

load name=net_lo plugin=procnetdev
config name=net_lo dev=lo
smplr_add name=smplr_lo instance=net_lo
smplr_start name=smplr_lo interval=1000000 offset=0

load name=net_eno1 plugin=procnetdev
config name=net_eno1 dev=eno1
smplr_add name=smplr_eno1 instance=net_eno1
smplr_start name=smplr_eno1 interval=1000000 offset=0
```

The following command will start an **ldmsd** in foreground mode, and listen to
port 10001 using socket transport:

```
ldmsd -F -c samp.cfg -x sock:10001
```

Then, `ldms_ls -x sock -p 10001 -l` shall give something similar to the
following:

```
z:10001/net_lo: consistent, last update: Thu May 02 16:12:43 2019 -0500 [662us]
D u64        component_id                               0
D u64        job_id                                     0
D u64        app_id                                     0
D u64        rx_bytes                                   57183741 bytes
D u64        rx_packets                                 75486 packets
D u64        rx_errs                                    0 pakcets
D u64        rx_drop                                    0 packets
D u64        rx_fifo                                    0 pakcets
D u64        rx_frame                                   0 pakcets
D u64        rx_compressed                              0 packets
D u64        rx_multicast                               0 packets
D u64        tx_bytes                                   57183741 bytes
D u64        tx_packets                                 75486 packets
D u64        tx_errs                                    0 packets
D u64        tx_drop                                    0 packets
D u64        tx_fifo                                    0 packets
D u64        tx_colls                                   0 packets
D u64        tx_carrier                                 0 packets
D u64        tx_compressed                              0 packets

z:10001/net_eno1: consistent, last update: Thu May 02 16:12:43 2019 -0500 [1308us]
D u64        component_id                               0
D u64        job_id                                     0
D u64        app_id                                     0
D u64        rx_bytes                                   2680519336 bytes
D u64        rx_packets                                 2941523 packets
D u64        rx_errs                                    0 pakcets
D u64        rx_drop                                    0 packets
D u64        rx_fifo                                    0 pakcets
D u64        rx_frame                                   0 pakcets
D u64        rx_compressed                              0 packets
D u64        rx_multicast                               9353 packets
D u64        tx_bytes                                   2020839181 bytes
D u64        tx_packets                                 2593718 packets
D u64        tx_errs                                    0 packets
D u64        tx_drop                                    0 packets
D u64        tx_fifo                                    0 packets
D u64        tx_colls                                   0 packets
D u64        tx_carrier                                 0 packets
D u64        tx_compressed                              0 packets

z:10001/mem: consistent, last update: Thu May 02 16:12:43 2019 -0500 [152us]
D u64        component_id                               0
D u64        job_id                                     0
D u64        app_id                                     0
D u64        MemTotal                                   32367204 kB
D u64        MemFree                                    19493344 kB
D u64        MemAvailable                               27508432 kB
D u64        Buffers                                    1403168 kB
D u64        Cached                                     7129244 kB
D u64        SwapCached                                 0 kB
D u64        Active                                     6540444 kB
D u64        Inactive                                   5305384 kB
D u64        Active(anon)                               3315180 kB
D u64        Inactive(anon)                             775680 kB
D u64        Active(file)                               3225264 kB
D u64        Inactive(file)                             4529704 kB
D u64        Unevictable                                64 kB
D u64        Mlocked                                    64 kB
D u64        SwapTotal                                  2097148 kB
D u64        SwapFree                                   2097148 kB
D u64        Dirty                                      43116 kB
D u64        Writeback                                  0 kB
D u64        AnonPages                                  3313520 kB
D u64        Mapped                                     1128576 kB
D u64        Shmem                                      777456 kB
D u64        Slab                                       798800 kB
D u64        SReclaimable                               722480 kB
D u64        SUnreclaim                                 76320 kB
D u64        KernelStack                                19456 kB
D u64        PageTables                                 77832 kB
D u64        NFS_Unstable                               0 kB
D u64        Bounce                                     0 kB
D u64        WritebackTmp                               0 kB
D u64        CommitLimit                                18280748 kB
D u64        Committed_AS                               12811092 kB
D u64        VmallocTotal                               34359738367 kB
D u64        VmallocUsed                                0 kB
D u64        VmallocChunk                               0 kB
D u64        HardwareCorrupted                          0 kB
D u64        AnonHugePages                              0 kB
D u64        ShmemHugePages                             0 kB
D u64        ShmemPmdMapped                             0 kB
D u64        CmaTotal                                   0 kB
D u64        CmaFree                                    0 kB
D u64        HugePages_Total                            0
D u64        HugePages_Free                             0
D u64        HugePages_Rsvd                             0
D u64        HugePages_Surp                             0
D u64        Hugepagesize                               2048 kB
D u64        DirectMap4k                                305540 kB
D u64        DirectMap2M                                32667648 kB

```


SEE ALSO
========

**ldmsd**(1) [ldmsd-aggregator][ldmsd-aggregator](7)

[ldmsd-aggregator]: ldms/src/ldmsd/ldmsd-aggregator.md
