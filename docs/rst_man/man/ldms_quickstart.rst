.. _ldms_quickstart:

===============
ldms_quickstart
===============

---------------------------------
Man page for Quick Start of LDMS
---------------------------------

:Date:   12 Dec 2016
:Manual section: 7
:Manual group: LDMS

INTRODUCTION
============

LDMS is the Lightweight Distributed Metric Service. LDMS is a
distributed data collection, transport, and storage tool that supports a
wide variety of configuration options. There are three main functional
components described below.

*Samplers* run one or more plugins that periodically sample data of
interest. Each plugin defines a group of metrics called a metric set.
The sampling frequency is user defined and can be dynamically changed. A
host can simultaneously run multiple plugins. Configuration flags
determine whether the sampling plugins run synchronously or
asynchonously (both on a host and across hosts). Memory allocated for a
particular metric set is overwritten by each successive sampling. The
host daemon does not retain sample history; plugins do not typically
retain history, but can be written to do so.

*Aggregators* collect data in a pull fashion from samplers and/or other
aggregators. The collection frequency is user defined and operates
independently of other collection operations and sampling operations.
Distinct metric sets can be collected at different frequencies. Once
started, the aggregation schedule cannot be altered without restarting
the aggregator. Fan-in refers to the number of hosts collected from by a
single aggregator. Maximum fan-in varies by transport but is roughly
9,000:1 for the socket transport and for the RDMA transport over
Infiniband. It is > 15000:1 for RDMA over the Cray Gemini transport.
Daisy chaining is not limited to two levels; multiple aggregators may
aggregate from the same sampler or aggregator ldmsd. Fan-in at higher
levels is limited by the aggregator host capabilities (CPU, memory,
network bandwidth, and storage bandwidth).

*Storage* plugins write in a variety of formats. Comma Separated Value
(CSV) file storage of metric sets plugins are provided. Storage occurs
when a valid updated metric set data is collected by an aggregator that
has been configured to write that data to storage. Collection of a
metric set whose data has not been updated or is incomplete does not
result in a write to storage in any format.

The host daemon is the same base code in all cases; differentiation is
based on configuration of plugins for sampling or storage and on
configuring aggregation of data from other host daemons.

DESCRIPTION
===========

Quick Start instructions for LDMS (Lightweight Distributed Metric
Service).

This man page describes how to configure and run LDMS daemons (ldmsd) to
perform the following tasks:

-  collect data

-  aggregate data from multiple ldmsds

-  store collected data to files.

There are three basic configurations that will be addressed:

-  configuring an ldmsd with collector plugins

-  configuring a ldmsd to aggregate information from other ldmsds

-  configuring a store_csv storage plugin on an ldmsd.

The order in which these configurations should be performed does not
matter with respect to collectors and aggregators.

While a complete listing of flags and parameters can be seen by running
ldmsd and the configuration tools with the --help directive, this
document describes the flags and parameters required for running a basic
setup.

There are no run scripts provided in the current release; the examples
here can be used in the creation of such.

Arrangement of this document
============================

This document is arranged as follows:

   1) Prerequisites

   2) Build and install

   3) Configuring and Starting an ldmsd (general)

   4) through 8) Example ldmsd configurations and queries

   9) Protection Domain Tags (Cray Only)

   10) Troubleshooting

   11) About the man pages

1) PREREQUISITES
================

-  All sections below assume the build directory is /tmp/opt/ovis.

-  libevent-2.0 is a requirement.

-  Python 2.7 or Python 2.6 with the argparse module is required for
   ldmsd_controller

2) BUILD/INSTALL:
=================

There is a separate document with build/install instructions.

The default ldms build in v3 has authentication turned on. This document
does not include use of the authentication flags; the instructions here
are as if you had built with --disable_ovis_auth. For more information
on authentication, see the ldms_authentication man page.

3) CONFIGURING AND STARTING AN LDMSD
====================================

3-1) Environment Variables for LDMS
-----------------------------------

You will need to set the following environment variables when running
LDMS daemons. This assumes that ldms has been installed in to
/tmp/opt/ovis.

::

   export LD_LIBRARY_PATH=/tmp/opt/ovis/lib/:/tmp/opt/ovis/lib/ovis-ldms/:<path to libevent-2.0>/lib:$LD_LIBRARY_PATH
   export ZAP_LIBPATH=/tmp/opt/ovis/lib/ovis-ldms
   export LDMSD_PLUGIN_LIBPATH=/tmp/opt/ovis/lib/ovis-ldms
   export PATH=/tmp/opt/ovis/sbin/:/tmp/opt/ovis/bin:$PATH
   export LDMSD_SOCKPATH=/tmp/run/ldmsd

LDMSD_SOCKPATH determines the location for the unix domain socket
(described in the ldmsd args below). The default is /var/run/ldmsd. Make
sure you use a location that is writeable if you are running as
non-root.

3-2) Options for Configuring Plugins of an ldmsd
------------------------------------------------

Plugins for an ldmsd can be configured via a configuration file
specified as an argument to the "-c" flag. Also, ldmsd_controller is a
configuration tool that can work in interactive mode and can also can be
directed commands/scripts to a socket. The plugin configuration commands
are the same in all cases.

In the instructions below, we briefly illustrate use of the
configuration script to ldmsd vs ldmsd_controller. Some environmental
variables have been supressed in this section for clarity. In all
subsequent examples (Sections 4+), we provide versbose detail for the
ldmsd configuration script method only. Altering this to use the other
methods should then be obvious.

3-2a) Configuring an ldmsd via a configuration script
-----------------------------------------------------

This is the most usual mode of configuring ldms in production scenarios
and can also be used for test scenarios.

Example commands for configuring a sampler:

::

   > more config.file

   load name=meminfo
   config name=meminfo producer=vm1_1 instance=vm1_1/meminfo
   start name=meminfo interval=1000000

The path to the configuration script is then provided to the ldmsd via
the "-c" flag when it is started:

Example ldmsd start command with a configuration script:

::

   ldmsd -x sock:60000 -S tmp/ldmsd/sock1 -l /tmp/log/logfile -v DEBUG -c ./config.file

3-2b) Configuring ldmsd via ldmsd_controller
--------------------------------------------

You can use ldmsd_controller to connect to the ldmsd at any time to
issue plugin commands. This is most often used for dynamically issuing
commands to a running ldmsd.

Example ldmsd start command without a configuration script:

::

   ldmsd -x sock:60000 -S tmp/ldmsd/sock1 -l /tmp/log/logfile -v DEBUG

Call the ldmsd_controller interactively and enter the same commands as
you would in the configuration script.

::

   ldmsd_controller --host vm1_1 --port=61000
   ldmsd_controller> load name=meminfo
   ldmsd_controller> config name=meminfo producer=vm1_1 instance=vm1_1/meminfo
   ldmsd_controller> start name=meminfo interval=1000000
   ldmsd_controller> quit

Relatedly, you can run ldmsd_controller with the commands in script
form. For example:

::

   > more config.sh

   #!/bin/bash
   echo "load name=meminfo"
   echo "config name=meminfo producer=vm1_1 instance=vm1_1/meminfo"
   echo "start name=meminfo interval=1000000"

Call the ldmsd_controller with the script:

::

   ldmsd_controller --host vm1_1 --port=60000 --script ./config.sh

ldmsd_contoller may be executed multiple times to issues different
commands to the same ldmsd.

3-3) Starting an ldmsd
----------------------

3-3a) Set environment variables, as described above.

3-3b) Run ldmsd:

::

   <path to executable>/ldmsd -x <transport>:<listen port> -S <unix domain socket path/name> -l <log file path/name> -v <LOG_LEVEL> -c config.file

Notes:

-  Transport is one of: sock, rdma, ugni (ugni is Cray specific for
   using RDMA over the Gemini/Aries network)

-  The configuration file contains the commands to configure the
   plugins.

-  The unix domain socket can be used to communicate configuration
   information to an ldmsd. The default path for this is
   /var/run/ldmsd/. To change this the environment variable
   LDMSD_SOCKPATH must be set to the desired path (e.g. export
   LDMSD_SOCKPATH=/tmp/run/ldmsd)

-  No log can be can be obtained by using LOG_LEVEL QUIET, or specifying
   /dev/null for the log file, or using command line redirection.

-  The default is to run as a background process but the -F flag can be
   specified for foreground

-  A script can be made to start ldmsd and collectors on a host where
   that script contains the information to execute the command.

3-3c) Examples for launching ldmsd:

-  Start an ldmsd on the socket transport with a log file and a
   configuration file.

::

   /tmp/opt/ovis/sbin/ldmsd -x sock:60000 -S /var/run/ldmsd/metric_socket -l /tmp/opt/ovis/logs/1 -c config.file

   Same but with log level QUIET
   /tmp/opt/ovis/sbin/ldmsd -x sock:60000 -S /var/run/ldmsd/metric_socket -l /tmp/opt/ovis/logs/1 -c config.file -V QUIET

-  Start 2 instances of ldmsd on host vm1

::

   Note: Make sure to use different socket names and listen on different ports if you are on the same host.
   /tmp/opt/ovis/sbin/ldmsd -x sock:60000 -S /var/run/ldmsd/metric_socket_vm1_1 -l /tmp/opt/ovis/logs/vm_1 -c config.file
   /tmp/opt/ovis/sbin/ldmsd -x sock:60001 -S /var/run/ldmsd/metric_socket_vm1_2 -l /tmp/opt/ovis/logs/vm_2 -c config.file

4) EXAMPLE: CONFIGURE AN LDMSD WITH SAMPLER PLUGINS
===================================================

4-1) Create the configuration file for the sampler plugins:
-----------------------------------------------------------

Configure a "meminfo" collector plugin to collect every second.

::

   load name=meminfo
   config name=meminfo producer=vm1_1 instance=vm1_1/meminfo
   start name=meminfo interval=1000000


   Notes:
   For synchronous operation include "offset=<#usec>" in start line (e.g. start name=meminfo interval=xxx offset=yyy).
   This will cause the sampler to target interval + yyy aligned to the second and micro second
   (e.g. every 5 seconds with an offset of 0 usec would ideally result in collections at 00:00:00, 00:00:05, 00:00:10, etc.
   whereas with an offset of 100,000 usec it would be 00:00:00.1, 00:00:05.1, 00:00:10.1, etc)
   Different plugins may have additional configuration parameters.

4-2) Set environment variables, as described above.
---------------------------------------------------

4-3) Start the ldmsd with the config file, as described above. e.g.,
--------------------------------------------------------------------

   ldmsd -x sock:60000 -S tmp/ldmsd/sock1 -l /tmp/log/logfile -v DEBUG
   -c ./config.file

4-4) Verifying the collector
----------------------------

At this point the ldmsd collector should be checked using the utility
ldms_ls (See Using ldms_ls below)

5) EXAMPLE: CONFIGURE AN AGGREGATOR USING LDMSD_CONTROLLER
==========================================================

5-1) Start 2 separate ldmsds, one on host vm1_1 and one on host vm1_2, with sampler plugins, as described above
---------------------------------------------------------------------------------------------------------------

5-2) Write a script to add producers and start collecting from them:
--------------------------------------------------------------------

This adds vm1_1 as a producer with its sets collected at 2 second
intervals and vm1_2 as a producer with its sets collected at 5 second
intervals. Here the "name" of the producer must match the "producer"
name given to the sampler.

The first set of lines adds the producers. The second set of lines
establishes the aggregation from them. at the specified intervals.

::

   > more add_prdcr.config
   prdcr_add name=vm1_2 host=vm1 type=active xprt=sock port=60001 interval=20000000
   prdcr_start name=vm1_2
   prdcr_add name=vm1_1 host=vm1 type=active xprt=sock port=60000 interval=20000000
   prdcr_start name=vm1_1
   updtr_add name=policy2_h1 interval=2000000 offset=0
   updtr_prdcr_add name=policy2_h1 regex=vm1_1
   updtr_start name=policy2_h1
   updtr_add name=policy5_h2 interval=5000000 offset=0
   updtr_prdcr_add name=policy5_h2 regex=vm1_2
   updtr_start name=policy5_h2

5-3) Set environment variables, as described above
--------------------------------------------------

5-4) Start an ldmsd on your host to aggregate using the configuration file
--------------------------------------------------------------------------

   /tmp/opt/ovis/sbin/ldmsd -x sock:60002 -S
   /var/run/ldmsd/metric_socket_agg -l /tmp/opt/ovis/logs/vm1_agg -c
   ./add_prdcr.sh

Notes:

-  There is no requirement that aggregator intervals match collection
   intervals

-  Because the collection and aggregation processes operate
   asynchronously there is the potential for duplicate data collection
   as well as missed samples. The first is handled by the storage
   plugins by comparing generation numbers and not storing duplicates.
   The second implies either a loss in fidelity (if collecting counter
   data) or a loss of data points here and there (if collecting
   differences of counter values or non counter values). This can be
   handled using the synchronous option on both collector and aggregator
   but is not covered here.

5-4) At this point the ldmsd collector should be checked using the utility ldms_ls
----------------------------------------------------------------------------------

(See Using ldms_ls below). In this case you should see metric sets for
both vm1_1 and vm1_2 displayed when you query the aggregator ldmsd using
ldms_ls.

6) EXAMPLE: CONFIGURE AN LDMS AGGREGATOR WITH A STORAGE PLUGIN
==============================================================

6-1) Add storage configuration lines to the configuration file described above.
-------------------------------------------------------------------------------

This adds a store_csv to store sets whose schema are meminfo or vmstat
and whose instance name matches the regex. A set's schema and instance
names will be seen in the output of ldms_ls (described below).

> more add_store.sh load name=store_csv config name=store_csv
path=<<STORE_PATH>> action=init altheader=0 rollover=30 rolltype=1
strgp_add name=policy_mem plugin=store_csv container=csv schema=meminfo
strgp_prdcr_add name=policy_mem regex=vm\* strgp_start
name=policy_vmstat strgp_add name=policy_vmstat plugin=store_csv
container=csv schema=vmstat strgp_prdcr_add name=policy_vmstat
regex=vm\* strgp_start name=policy_vmstat

Notes:

-  For the csv store, the whole path must pre-exist.

-  See the store_csv man page for more info on the plugin
   configuration arguments.

-  If you want to collect on a host and store that data on the same
   host, run two ldmsd's: one with a collector plugin only and one as an
   aggegrator with a store plugin only.

6-2) Set environment variables, as described above
--------------------------------------------------

6-3) Start the aggregator with the full configuration file (both aggregator and store lines), as described above
----------------------------------------------------------------------------------------------------------------

6-4) Verify the store
---------------------

Go to data store and verify files have been created and are being
written to

::

   cd <<STORE_PATH>>/<container>
   ls -ltr

You can now utilize this data.

Data will flush to the store when the OS flushes data unless an advanced
flag is used. Thus, in a default configuration, if you have a small
number of nodes and/or a long interval, you may not see data appear in
the store for a few minutes.

7) EXAMPLES: USING LDMS_LS TO DISPLAY SETS/METRICS FROM AN LDMSD
================================================================

7-1) Set environment variables, as described above
--------------------------------------------------

7-2a) Query ldmsd on host vm1 listening on port 60000 (sampler) using the sock transport for metric sets being served by that ldmsd
-----------------------------------------------------------------------------------------------------------------------------------

::

   ldms_ls -h vm1 -x sock -p 60000
   Should return:
   vm1_1/meminfo
   vm1_1/vmstat

7-2b) Query ldmsd on host vm1 listening on port 60002 (aggregator) using the sock transport for metric sets being served by that ldmsd
--------------------------------------------------------------------------------------------------------------------------------------

::

   ldms_ls -h vm1 -x sock -p 60002
   Should return:
   vm1_1/meminfo
   vm1_1/vmstat
   vm1_2/meminfo
   vm1_2/vmstat

7-2c) Query ldmsd on host vm1 listening on port 60000 using the sock transport for the names and contents of metric sets being served by that ldmsd.
----------------------------------------------------------------------------------------------------------------------------------------------------

Should return: Set names (vm1_1/meminfo and vm1_1/vmstat in this case)
as well as all names and values associated with each set respectively.
Only vm1_1/meminfo shown here.

::

   > ldms_ls -h vm1 -x sock -p 60000 -l
   vm1_1/meminfo: consistent, last update: Wed Jul 31 21:51:08 2013 [246540us]
   U64 33084652         MemTotal
   U64 32092964         MemFree
   U64 0                Buffers
   U64 49244            Cached
   U64 0                SwapCached
   U64 13536            Active
   U64 39844            Inactive
   U64 5664             Active(anon)
   U64 13540            Inactive(anon)
   U64 7872             Active(file)
   U64 26304            Inactive(file)
   U64 2996             Unevictable
   U64 2988             Mlocked
   U64 0                SwapTotal
   U64 0                SwapFree
   U64 0                Dirty
   U64 0                Writeback
   U64 7164             AnonPages
   U64 6324             Mapped
   U64 12544            Shmem
   U64 84576            Slab
   U64 3948             SReclaimable
   U64 80628            SUnreclaim
   U64 1608             KernelStack
   U64 804              PageTables
   U64 0                NFS_Unstable
   U64 0                Bounce
   U64 0                WritebackTmp
   U64 16542324         CommitLimit
   U64 73764            Committed_AS
   U64 34359738367      VmallocTotal
   U64 3467004          VmallocUsed
   U64 34356268363      VmallocChunk
   U64 0                HugePages_Total
   U64 0                HugePages_Free
   U64 0                HugePages_Rsvd
   U64 0                HugePages_Surp
   U64 2048             Hugepagesize
   U64 565248           DirectMap4k
   U64 5726208          DirectMap2M
   U64 27262976         DirectMap1G

7-2d) Query for a non-existent set:
===================================

::

   ldms_ls -h vm1 -x sock -p 60000 -l vm1_1/foo
   ldms_ls: No such file or directory
   ldms_ls: lookup failed for set 'vm1_1/foo'

7-2e) Display metadata about sets contained by vm1 ldmsd listening on port 60000
================================================================================

::

   ldms_ls -h vm1 -x sock -p 60000 -v
   vm1_1/meminfo: consistent, last update: Fri Dec 16 17:12:08 2016 [5091us]
     METADATA --------
       Producer Name : vm1_1
       Instance Name : vm1_1/meminfo
         Schema Name : meminfo
                Size : 1816
        Metric Count : 43
                  GN : 2
     DATA ------------
           Timestamp : Fri Dec 16 17:12:08 2016 [5091us]
            Duration : [0.000072s]
          Consistent : TRUE
                Size : 384
                  GN : 985
     -----------------

8) STOP AN LDMSD
================

To kill all ldmsd on a host
---------------------------

::

   killall ldmsd

9) PROTECTION DOMAIN TAGS (Cray)
================================

9-1) Cray XE/XK:
----------------

If you are going to be using the "ugni" transport (RDMA over Gemini) you
will need to run with either system (as root) or user (as user) ptags.
While root CAN run using any ptag the fact that its use is unknown to
ALPS could cause collisions with applications.

To see current ptags:
---------------------

::

   > apstat -P
   PDomainID           Type    Uid   PTag     Cookie
   LDMS              system      0     84 0xa9380000

To create a userspace ptag:
---------------------------

::

   apmgr pdomain -c <somenamehere>

   Example:
   > apmgr pdomain -c foo
   > apstat -P
   PDomainID           Type    Uid   PTag     Cookie
   LDMS              system      0     84 0xa9380000
   foo                 user     12345  233 0xa1230000

Note: A system administrator will have to setup system ptags and/or
enable users to set up ptags.

To remove a userspace ptag:
---------------------------

::

   apmgr pdomain -r <somenamehere>

Note: The userid of the ptag being removed must match that of the user
running the command or root

PTAG-Related Enviroment variables for ldms (XE/XK)
--------------------------------------------------

Set the following environment variables for either user or system ptags
(example shows user ptag values):

::

   export ZAP_UGNI_PTAG 233
   export ZAP_UGNI_COOKIE 0xa1230000

Starting ldms from aprun with ptags
-----------------------------------

When running with user space ptags you must specify the ptag name when
using aprun

::

   aprun <<usual aprun args here>> -p foo ldmsd <<usual ldmsd flags here>>
   or
   aprun <<usual aprun args here>> -p foo ldms_ls <<usual ldms_ls flags here>>

Note: On some systems you will run aprun after a qsub -I or within a
script specified in qsub or similiar.

9-2) Cray XC, CLE <= 5.2:
-------------------------

If you are going to be using the "ugni" transport (RDMA over Aries) you
will need to run with either system (as root) or user (as user) ptags.
While root CAN run using any ptag the fact that its use is unknown to
ALPS could cause collisions with applications.

To see current ptags:
---------------------

::

   > apstat -P
   PDomainID   Type   Uid     Cookie    Cookie2
   LDMS      system     0 0x86b80000          0

To create a userspace ptag:
---------------------------

::

   apmgr pdomain -c <somenamehere>

   Example:
   > apmgr pdomain -c foo
   > apstat -P
   PDomainID   Type   Uid     Cookie    Cookie2
   LDMS      system     0 0x86b80000          0
   foo         user 20596 0x86bb0000 0x86bc0000

Note: A system administrator will have to setup system ptags and/or
enable users to set up ptags.

To remove a userspace ptag:
---------------------------

::

   apmgr pdomain -r <somenamehere>

Note: The userid of the ptag being removed must match that of the user
running the command or root

PTAG-Related Enviroment variables for ldms (XC)
-----------------------------------------------

Set the following environment variables. On XC the ptag value doesn't
matter but ZAP_UGNI_PTAG must be defined. Set the Cookie (not Cookie2)
for either user or system ptag.

::

   export ZAP_UGNI_PTAG=0
   export ZAP_UGNI_COOKIE=0x86bb0000

Starting ldms from aprun with ptags
-----------------------------------

When running with user space ptags you must specify the ptag name when
using aprun

::

   aprun <<usual aprun args here>> -p foo ldmsd <<usual ldmsd flags here>>
   or
   aprun <<usual aprun args here>> -p foo ldms_ls <<usual ldms_ls flags here>>

Note: On some systems you will run aprun after a qsub -I or within a
script specified in qsub or similiar.

10) TROUBLESHOOTING
===================

What causes the following error: libibverbs: Warning: RLIMIT_MEMLOCK is 32768 bytes?
------------------------------------------------------------------------------------

Running as a user with "max locked memory" set too low. The following is
an example of trying to run ldms_ls as a user with "max locked memory"
set to 32k:

::

   ldms_ls -h <hostname> -x rdma -p <portnum>
   libibverbs: Warning: RLIMIT_MEMLOCK is 32768 bytes.
      This will severely limit memory registrations.
   RDMA: recv_buf reg_mr failed: error 12
   ldms_ls: Cannot allocate memory

Why doesn't my ldmsd start?
---------------------------

Possible options:

-  Check for existing /var/run/ldms/metric_socket or similar. Sockets
   can be left if an ldmsd did not clean up upon termination. kill -9
   may leave the socket hanging around.

-  The port you are trying to use may already be in use on the node. The
   following shows the logfile output of such a case:

::

   Tue Sep 24 08:36:54 2013: Started LDMS Daemon version 2.1.0
   Tue Sep 24 08:36:54 2013: Process 123456 listening on transport ugni:60020
   Tue Sep 24 08:36:54 2013: EV_WARN: Can't change condition callbacks once they have been initialized.
   Tue Sep 24 08:36:54 2013: Error 12 listening on the 'ugni' transport.
   Tue Sep 24 08:36:54 2013: LDMS Daemon exiting...status 7
   If using the -l flag make sure that your log directory exists prior to running
   If writing to a store with this particular lmdsd make sure that your store directory exists prior to running
   If you are running on a Cray with transport ugni using a user space PTag, check that you called aprun with the -p flag
   aprun -N 1 -n <number of nodes> -p <ptag name> run_my_ldmsd.sh

How can I find what process is using the port?
----------------------------------------------

   netstat -abno

Why arent all my hosts/sets adding to the aggregator?
-----------------------------------------------------

Possible options:

-  use -m flag on the aggregator to use more memory when adding a lot of
   hosts

-  use -p on the aggregator to use more processors

Why isn't my ldmsd storing its own set to the store?
----------------------------------------------------

Currently, this is not supported. You can use a separate ldmsd on the
same host to gather another ldmsd's data for that host.

Why is my aggregator not responding (CRAY XE/XK)?
-------------------------------------------------

Running a ldmsd aggregator as a user but trying to aggregate from a
ldmsd that uses a system ptag can result in the aggregator hanging
(alive but not responding and not writing to the store). The following
is the logfile output of such an aggregator:

::

   Tue Sep 24 08:42:40 2013: Connected to host 'nid00081:60020'
   Tue Sep 24 08:42:42 2013: cq_thread_proc: Error 11  monitoring the CQ.

11) MAN PAGES
=============

ldms comes with man pages. In the build process these will be installed
in <build_path>/ovis/share/man. Man pages are in the following
catagories:

General
-------

General pages address information, such as ldms_build_install,
ldms_quickstart, and ldms_authentication.

Utilities
---------

Utilities pages address the various utilities and commands such as
ldmsd, ldmsd_controller, and ldms_ls.

Plugins
-------

Plugin pages address all plugins, both samplers and stores. Naming
convention for these pages is XXX. For example: aries_mmr,
cray_system_sampler_variants, kgnilnd, meminfo,
procinterrupts, procnetdev, procnfs,
store_csv, store_function_csv, store_sos, and
vmstat.

NOTES
=====

As part of the install, test scripts are placed in /tmp/opt/ovis/bin.
These scripts may serve as additional examples. These are being
converted from using the obsolete ldmsctl tool to the ldmsd_controller
tool, so they may not be fully updated at any given time.

BUGS
====

No known bugs.

SEE ALSO
========

:ref:`ldms_build_install(7) <ldms_build_install>`, :ref:`ldmsd(8) <ldmsd>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`,
:ref:`ldms_authentication(7) <ldms_authentication>`, :ref:`ldms_build_install(7) <ldms_build_install>`, :ref:`ldms_ls(8) <ldms_ls>`
