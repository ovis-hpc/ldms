.. _ldms_ls:

=======
ldms_ls
=======

-------------------------------------
Query an ldmsd for metric set values
-------------------------------------

:Date:   28 Feb 2018
:Manual section: 7
:Manual group: LDMSD

SYNOPSIS
========

ldms_ls [OPTION...] [NAME]

DESCRIPTION
===========

The ldms_ls command is used to query an ldmsd (ldms daemon) for metric
set values.

ENVIRONMENT
===========

The following environment variables must be set:
------------------------------------------------

LD_LIBRARY_PATH
   include the path to ovis/lib and libevent2. On some system, lib64
   rather than lib is required.

PATH
   include the path to ovis/sbin

The following environment variables may be set to override compiled defaults:
-----------------------------------------------------------------------------

ZAP_LIBPATH
   path to ovis/lib/ovis-ldms

LDMSD_PLUGIN_LIBPATH
   path to ovis/lib/ovis-ldms

The following environment variables are optional:
-------------------------------------------------

LDMS_LS_MEM_SZ
   The size of memory reserved for metric sets. See the -m option.

OPTIONS
=======

If the NAME is specified on the command line without -E/-S/-I, only information for that instance = NAME is displayed.

**-E**\ *NAME*
   |
   | Indicates that the NAME is a regular expression.

**-S**\ *NAME*
   |
   | Indicates that the NAME is a schema name.

**-I**\ *NAME*
   |
   | Indicates that the NAME is an instance name. This is the default.

**-h**\ *HOST*
   |
   | HOST to query. Default is localhost.

**-x**\ *TRANSPORT*
   TRANSPORT to use for the query. values are sock, rdma, or ugni (Cray
   XE/XK/XC). Default is sock.

**-p**\ *PORT*
   PORT of the HOST to use for the query. Default is LDMS_DEFAULT_PORT.

**-l**
   Display long listing. Outputs details of the metric set, including
   timestamp, metric names, metric types, and values.

**-f** <format>
   Display output using format, which is one of 'tab' or 'json'. Other
   values are ignored. Output in tab format includes header rows
   starting with # and has tab separated columns.

**-a**\ *AUTH*
   The name of the LDMS Authentication plugin. Please see
   :ref:`ldms_authentication(7) <ldms_authentication>` for more details. (default: "none").

**-A**\ *NAME*\ **=**\ *VALUE*
   The name-value options for the LDMS Authentication plugin. This
   option can be given multiple times. Please see
   :ref:`ldms_authentication(7) <ldms_authentication>` for more information and consult the
   plugin manual for the option details.

**-m**\ *MEMORY_SIZE*
   |
   | MEMORY_SIZE is the size of memory reserved for metric sets. This
     value has precedence over the value of the LDMS_LS_MEM_SZ
     environment variable. The given size must be less than 1 petabytes.
     For example, 20M or 20mb are 20 megabytes. Unless a specific set is
     being queried, this should usually match the size of pre-allocated
     memory specified when starting the remote ldmsd being queried.

**-u**
   Display the user data for the metrics. (Usually compid)

**-v**
   Display metadata information. Specifying this option multiple times
   increases the verbosity.

**-V**
   Display LDMS version information and then exit.

**-w**\ *WAIT_SEC*
   WAIT_SEC is the time to wait before giving up on the server. Default
   is 10 sec.

DEFAULTS
========

**ldms_ls** with no arguments defaults to ``ldms_ls -p XXX -h localhost -x sock``

where XXX is the **LDMS_DEFAULT_PORT**.

NOTES
=====

None.

BUGS
====

No known bugs.

EXAMPLES
========

::

   1) $ldms_ls -h vm1 -x sock -p 60000
   vm1_1/meminfo
   vm1_1/vmstat



   2) $ldms_ls -h vm1 -x sock -p 60000 -l
   vm1_1/meminfo: consistent, last update: Thu Oct 29 08:04:44 2015 [202552us]
   D u64        MemTotal                        132165188
   D u64        MemFree                         129767048
   D u64        Buffers                         0
   D u64        Cached                          46780
   D u64        SwapCached                      0
   D u64        Active                          16116
   D u64        Inactive                        8596
   D u64        Active(anon)                    10440
   D u64        Inactive(anon)                  220
   D u64        Active(file)                    5676
   D u64        Inactive(file)                  8376
   D u64        Unevictable                     35400
   D u64        Mlocked                         6032




   The output format of the data is as follows:
   M/D
   indicates metadata vs data values
   Metrictype
   in the example above, unsigned int 64.
   Value
   Value of the metric

   3) For a non-existent set:
   $ldms_ls -h vm1 -x sock -p 60000 -l vm1_1/foo
   ldms_ls: No such file or directory
   ldms_ls: lookup failed for set 'vm1_1/foo'

   4a) Display metadata:
   ldms_ls -h vm1 -x sock -p 60000 -v
   Schema         Instance                 Flags  Msize  Dsize  Hsize  UID    GID    Perm       Update            Duration          Info
   -------------- ------------------------ ------ ------ ------ ------ ------ ------ ---------- ----------------- ----------------- --------
   vmstat         vm1/vmstat         CL    8504   1328      0      0      0 -rw-r--r-- 1734076680.060971          0.000186 "updt_hint_us"="60000000:0"
   -------------- ------------------------ ------ ------ ------ ------ ------ ------ ---------- ----------------- ----------------- --------
   Total Sets: 1, Meta Data (kB): 8.50, Data (kB) 1.33, Memory (kB): 9.83

   4b) Display metadata tabbed:
   ldms_ls -h vm1 -x sock -p 60000 -v -f tab
   #schema instance        flags   msize   dsize   hsize   uid     gid     perm    update  duration        age_seconds     age_intervals   info
   vmstat  amber-login4/vmstat     CL      8504    1328    0       0       0       -rw-r--r--      1734076800.060615                0.000174       10.461       0       "updt_hint_us"="60000000:0"
   #total_sets     meta_data_kb    data_kb memory_kb
   1       8.50     1.33   9.83


   5) Regular Expression:
   $ldms_ls -h vm1 -x sock -p 60000 -E vm1
   vm1_1/meminfo
   vm1_1/vmstat

   $ldms_ls -h vm1 -x sock -p 60000 -E vms
   vm1_1/vmstat

   $ldms_ls -h vm1 -x sock -p 60000 -E -I memin
   vm1_1/meminfo

   $ldms_ls -h vm1 -x sock -p 60000 -E -S ^vmstat$
   vm1_1/vmstat

   $ldms_ls -h vm1 -x sock -p 60000 -E -S cpu
   ldms_ls: No metric sets matched the given criteria

If the -E option is not given, the given string will be taken literally,
i.e., it is equivalent to giving -E ^foo$.

The regular expression option can be used with the -v and -l options. In
this case ldms_ls will display only the information of the metric sets
that matched the given regular expression.

SEE ALSO
========

:ref:`ldms_authentication(7) <ldms_authentication>`, :ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`,
