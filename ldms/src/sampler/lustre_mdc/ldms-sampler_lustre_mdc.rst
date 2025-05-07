.. _lustre_mdc:

=================
lustre_mdc
=================

----------------------------------------
Man page for the LDMS lustre_mdc plugin
----------------------------------------

:Date:   1 May 2019
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| config name=lustre_mdc

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file.

The lustre_mdc plugin provides schema lustre_mdc for daemons with read
access to the lustre files in ``/proc/fs/lustre/mdc/*/md_stats`` and
``/sys/kernel/debug/lustre/mdc/*/stats``. The metric sets will have instance
names combining the producer name and the mdc name.

This plugin will work with Lustre versions 2.12 and others which share
these file locations and formats.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

**config**
   | name=<plugin_name> [producer=<name>] [component_id=<u64>]
   | configuration line

   name=<plugin_name>
      |
      | This MUST be lustre_mdc.

   producer=<alternate host name>
      |
      | The default used for producer (if not provided) is the result of
        gethostname(). The set instance names will be
        <producer>/<mdc_name>

   component_id=<uint64_t>
      |
      | Optional (defaults to 0) number of the host where the sampler is
        running. All sets on a host will have the same value.

   job_set=<job information set name>
      |
      | Optional (defaults to "job_info"). Typically should be set to
        <hostname>/jobid or <hostname>/job_info depending on choice of
        job sampling plugin.

   mdc_timing=0
      |
      | Optionally exclude timing data from
        ``/sys/kernel/debug/lustre/mdc/*/stats``. If given, the sampler may
        be run by unprivileged users. If /sys/kernel/debug/ cannot be
        opened by the user, it is a configuration error unless
        mdc_timing=0 is given.

   auto_reset=0
      |
      | Turn off the default behavior of resetting the counters when an
        overflow condition is detected. Reset is implemented by writing
        0 to the corresponding /proc or /sys file.

SCHEMA
======

The default schema name is lustre_mdc_ops_timing with all the data
described in DATA REPORTED below included. If mdc_timing=0 is given,
only the operation counts from md_stats are reported and the default
schema name changes to lustre_mdc_ops.

DATA REPORTED
=============

fs_name: The lustre file system name, e.g. xscratch. mdc: The mdc target
that goes with the metrics, e.g. xscratch-MDT0000. last_reset: The time
of the last reset performed by this sampler for any of its metric sets.

Operation counts from ``/proc/fs/lustre/mdc/*/md_stats``. See also kernel
source lustre/lustre/obdclass/lprocfs_status.c and
lustre/lustre/include/obd_class.h: mps_stats[]: "close", "create",
"enqueue", "getattr", "intent_lock", "link", "rename", "setattr",
"fsync", "read_page", "unlink", "setxattr", "getxattr",
"intent_getattr_async", "revalidate_lock",

Client operation timing statistics (all but .count are in microseconds)
for the following list of fields in
``/sys/kernel/debug/lustre/mdc/*/stats``: "req_waittime", "mds_getattr",
"mds_getattr_lock", "mds_close", "mds_readpage", "mds_connect",
"mds_get_root", "mds_statfs", "ldlm_cancel", "obd_ping", "seq_query",
"fld_query"

and statistics: "__count" the number of events observed, "__min" the
minimum event duration observed, "__max" the maximum duration observed,
"__sum" the sum of all durations observed, "__sumsqs" the sum of squares
of all durations observed

NOTES
=====

The counters and file locations supported by this plugin are those
present in Lustre 2.12. The fields labeled [reqs] are omitted. Data
names not listed here are simply ignored.

The minimum sample interval recommended for this sampler is 5-10
seconds, as the data volume may be substantial and resolving shorter
bursts of metadata activity is generally unnecessary.

The average and sample standard deviation can be computed from sum and
sumsqs, but once these counters roll over to negative values on a high
up-time client, they may be less useful. The counters can be manually
reset with bash:

::

   for i in /proc/fs/lustre/mdc/*/md_stats /sys/kernel/debug/lustre/mdc/*/stats; do
    echo 0 $i;
   done

The lustre utility equivalent of this plugin is to inspect the output of

::

   lctl get_param -R mdc.*.stats lctl get_param -R mdc.*.md_stats

Specifying instance=xxx as an option will be ignored.

BUGS
====

No known bugs.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=lustre_mdc
   config name=lustre_mdc
   start name=lustre_mdc interval=1000000

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`,
:ref:`lctl(8) <lctl>`.
