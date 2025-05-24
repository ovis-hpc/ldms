.. _cxi_sampler:

==============
cxi_sampler
==============

----------------------------------------
Man page for the LDMS cxi_sampler plugin
----------------------------------------

:Date:   04 May 2025
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| config name=cxi_sampler [ sys_path=<PATH> ] [ rh_path=<PATH> ] [ rh_counters=CSV or rh_counters_file=<PATH> ] [ tel_counters=CSV or tel_counters_file=<PATH> ]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. The cxi_sampler plugin provides interface
telemetry information from /sys/kernel/debug/cxi and /var/run/cxi.

There are two classes of information returned: CXI Telementry
information, retry handler information. This data is organized into
two lists of records.  One list contains the telemetry information,
called 'tel_list', and 'rh_list' respectively.

Each record schema is contructed by searching the directories above
skipping sub-directories and special files used to reset the counter
values. The remaining files each become a metric value in the
associated recored.

Each list contains one record for each interface.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The cxi_sampler plugin uses the sampler_base base class. This man page
covers only the configuration attributes specific to the this plugin;
see :ref:`ldms_sampler_base(7) <ldms_sampler_base>` for the attributes
of the base class.

**config**

   | name=INST_NAME [ tel_path=PATH ] [ rh_path=PATH ]  [ rh_counters=CSV or rh_counters_file=<PATH> ] [ tel_counters=CSV or tel_counters_file=<PATH> ]

   name=INST_NAME
      |
      | The configuration instance name.

   rh_path=PATH
      | Optional path to the directory containing the CXI retry handler files.
        The default PATH is /var/run/cxi. This option is primarily for
        testing on systems that lack the actual interface.

   tel_path=<PATH>
      |
      | Optional path to the directory containing the CXI interface telemetry files.
        The default PATH is /sys/kernel/debug/cxi. This option is primarily for
        testing on systems that lack the actual interface.

   rh_counters=<COUNTER NAMES>
      |
      | (Optional) A CSV list of POSIX regular expressions used to match and
        collect metrics from file names under rh_path.
        See Section COUTNER NAMES for details.
        If this option is omitted all counters will be collected.

   rh_counters_file=<PATH>
      |
      | (Optional) A file containing list of POSIX regular expressions used to
        match and collect metrics from file names under rh_path.
        One name per line.
        Ony used if rh_counters is not defined.
        See Section COUTNER NAMES for details.

   tel_counters=<COUNTER NAMES>
      |
      | (Optional) A CSV list of POSIX regular expressions used to match and
        collect metrics from file names under tel_path.
        See Section COUTNER NAMES for details.
        If this option is omitted all counters will be collected.

   tel_counters_file=<PATH>
      |
      | (Optional) A file containing list of POSIX regular expressions used to
        match and collect metrics from file names under tel_path.
        One name per line.
        Ony used if tel_counters is not defined.
        See Section COUTNER NAMES for details.

BUGS
====

No known bugs.

COUNTER NAMES
=============

File names found under <tel_path>/<INTERFACE>/device/telemetry/ and <rh_path>/

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=cxi_sampler
   config name=cxi_sampler producer=${HOSTNAME} instance=${HOSTNAME}/cxi_sampler
   start name=cxi_sampler interval=1s
or

::

   env CXI_COUNTERS=pct_mst_hit_on_som,pct_.*_timeouts,pct_.*_nack.*,pct_trs_replay.*
   env RH_COUNTERS=accel_close_complete,cancel_no_matching_conn
   load name=cxi_sampler
   config name=cxi_sampler producer=${HOSTNAME} instance=${HOSTNAME}/cxi_sampler tel_counters=${CXI_COUNTERS} rh_counters=${RH_COUNTERS}
   start name=cxi_sampler interval=1s
or

::

   env CXI_COUNTERS_FILE=/tmp/tel_filter.txt
   env RH_COUNTERS=accel_close_complete,cancel_no_matching_conn
   load name=cxi_sampler
   config name=cxi_sampler producer=${HOSTNAME} instance=${HOSTNAME}/cxi_sampler tel_counters_file=${CXI_COUNTERS_FILE} rh_counters=${RH_COUNTERS}
   start name=cxi_sampler interval=1s

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`
