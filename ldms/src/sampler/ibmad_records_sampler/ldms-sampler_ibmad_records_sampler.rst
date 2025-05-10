.. _ibmad_records_sampler:

============================
ibmad_records_sampler
============================

--------------------------------------------------
Man page for the LDMS ibmad_records_sampler plugin
--------------------------------------------------

:Date:   1 May 2019
:Manual section: 7
:Manual group: LDMS sampler


SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| config name=ibmad_records_sampler [ <attr>=<value> ]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. The ibmad_records_sampler plugin provides a single
metric set that contains a list of records. Each record contains all of
the metrics for a single infiniband port.

The schema is named "ibmad" by default.

NOTE: This plugin will not currently work with virtual IB devices.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

**config**
   | name=<plugin_name> [schema=<schema_name>] [job_set=<metric set
     name>]
   | configuration line

   name=<plugin_name>
      |
      | This MUST be ibmad_records_sampler.

   schema=<schema_name>
      |
      | The schema name defaults to "ibmad", but it can be renamed at
        the user's choice.

   rate=0
      |
      | Stop the default inclusion of rate values in the set.

   job_set=<metric set name>
      |
      | The name of the metric set that contains the job id information
        (default=job_id)

   include=PORTLIST
      |
      | Ignore any devices and ports discovered that are not matched by
        PORTLIST. See PORTLIST below. Cannot be combined with the
        exclude option.

   exclude=PORTLIST
      |
      | Collect all devices and ports discovered and active that are not
        matched by PORTLIST. See PORTLIST below. Cannot be combined with
        the include option.

   refresh_interval_sec=<seconds>
      |
      | (Optional) The sampler caches the list of infiniband devices,
        and that cache is refreshed at the beginning of a sample cycle
        if the refresh interval time has been exceeded.
        refresh_interval_sec sets the minimum number of seconds between
        refreshes of the device cache. The default refresh interval is
        600 seconds.

PORTLIST
========

Providing a port list specification will stop the automated discovery
process at every sample time from requerying devices and ports that are
not of interest, eliminating nuisance log messages from the MAD
libraries. Such messages are frequently seen on systems using
SocketDirect hardware.

The port list is a comma-separated list of CA name and optionally
number. E.g. "mlx4_0.1,mlx4_1". A device name specified without a port
number (.N) matches all ports on that device. The maximum port number
supported for a single device is 63. Including a device or port which
does not exist or is not active in the port list has no effect on the
metric sets reported.

BUGS
====

No known bugs.

NOTES
=====

The rates reported are computed from the last sample taken and the
present sample; however the last sample may not have been stored
downstream and the sample interval size may vary due to kernel wakeup
variations. Rate values are set to -1 for samples where the rate
computation is invalid.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=ibmad_records_sampler
   config name=ibmad_records_sampler
   start name=ibmad_records_sampler interval=1000000

::

   load name=ibmad_records_sampler
   config name=ibmad_records_sampler include=hfi1_0.1 rate=0
   start name=ibmad_records_sampler interval=1000000

::

   load name=ibmad_records_sampler
   config name=ibmad_records_sampler exclude=mlx5_0.2,mlx5_0.3,mlx5_0.4,
   start name=ibmad_records_sampler interval=1000000

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`
