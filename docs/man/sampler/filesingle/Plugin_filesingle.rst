=================
Plugin_filesingle
=================

:Date:   15 Dec 2018

NAME
====

Plugin_filesingle - man page for the LDMS filesingle plugin

SYNOPSIS
========

| Within ldmsd_controller or in a configuration file
| config name=filesingle conf=<metric_definitions> [timing]

DESCRIPTION
===========

The filesingle plugin provides metrics pulled from files containing a
single numeric value or character. This supports flexible definition of,
among others, sensor hardware, file system, and cpu metrics.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

See ldms_sampler_base(7) for the common sampler options.

**config**
   | conf=<metric_definitions> [timing]

..

   conf=<file>
      |
      | File lines contain the source, type, and default value for each
        metric. See CONF FILE SYNTAX below.

   timing
      |
      | If keyword 'timing' is included in the options, extra metrics
        measuring the time to collect every defined metric will be
        included. This allows for the discovery of slow sensors. Each
        timing metric will have the name of the timed metric with
        ".time" appended. Do not use "timing="; it is ignored.

COLLECTION
==========

Each metric is collected from a separate file. If this process fails for
any reason at all, the default value is collected instead. The timing
metrics (type S64) report the number of microseconds measured bracketing
the open/read/close cycle of the metric's value file. The timing of a
failed collection is -1. Each file is open, read, and closed for each
data sample collected.

CONF FILE SYNTAX
================

Each line of the conf file must be empty, contain a comment or contain:

<metric_name> <source_file> <metric_type> <default_value>

The metric and file names must not contain spaces. The metric type is
one of: S8, S16, S32, S64, U8, U16, U32, U64, F32, D64, CHAR.

Lines starting with # are comment lines. Line continuations are not
allowed.

The script ./ldms-sensors-config(1) generates an example metrics config
file from the data reported by sensors(1). Metric names, types, and
defaults generated can be tuned to user preferences.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=filesingle
   config name=filesingle conf=/etc/sysconfig/ldms.d/plugins-conf/filesingle.conf
   start name=filesingle interval=10000000 offset=0

For the contents of filesingle.conf (on a specific machine):

::

   power1 /sys/class/hwmon/hwmon0/device/power1_average S64 -1
   coretemp.Physical_id_0 /sys/class/hwmon/hwmon1/temp1_input S64 -1
   coretemp.Core_0 /sys/class/hwmon/hwmon1/temp2_input S64 -1
   core0.cur_freq /sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_cur_freq S64 -1

The power reading, two temperatures, and cpu frequency are collected.

NOTES
=====

The values collected are the raw values from the sources; converting to
humane units is left to data post-processors. In the specific example
given, the raw power reading has units of microwatts, the temperatures
have units of millidegrees Celsius, and the cpu frequency is reported in
milliHertz. To determine the appropriate unit conversions for your
system, compare the output of sensors(1) or lscpu(1) to the value found
in the raw data files.

To determine the file locations of metrics on your system consult the
documentation for the device drivers of interest or the output of
ldms-sensors-config(1) or

"strace -e trace=open <querytool>"

Some metric files may only be readable by the users with administrative
privileges. Some of these may be available without privilege by
extracting them from larger files in /proc, e.g. "cpu MHz" in
/proc/cpuinfo.

Some sensors may not update themselves (at the kernel level) faster than
a certain frequency, even though it is possible to more frequently read
their data files.

SEE ALSO
========

ldms-sensors-config(1), sensors(1), lscpu(1), ldms_sampler_base(7),
proc(5), ldmsd(8), ldmsd_controller(8)
