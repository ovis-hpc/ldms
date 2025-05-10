.. _ldms-sensors-config:

===================
ldms-sensors-config
===================

-------------------------------------------------------
Generate LDMS filesingle plugin configuration prototype
-------------------------------------------------------

:Date:   15 Dec 2018
:Manual section: 1
:Manual group: LDMS sampler


SYNOPSIS
========

ldms-sensors-config [--sensors=/path/to/sensors]
[--lscpu=/path/to/lscpu] [--test-lscpu=lscpu-log-file]
[--test-sensors=sensors-log-file]

Run 'sensors' under strace to discover where some sensor files live on
the current system and generate a draft metric configuration file for
the LDMS filesingle sampler.

DESCRIPTION
===========

The ldms-sensors-config program generates a draft conf file for the
filesingle sampler. The user should tailor the selection, naming, data
storage type, and default values per :ref:`filesingle(7) <filesingle>`.

OPTIONS
=======

--sensors=<path>
   |
   | specify an alternate location of the sensors program. The default
     is /usr/bin/sensors, and the PATH variable is not used to search
     for alternatives.

--nodash
   |
   | Replace all - characters in metric names with \_ characters.

--lscpu=<path>
   |
   | specify an alternate location of the lscpu program. The default is
     /usr/bin/lscpu and the PATH variable is not used to search for
     alternatives.

--test-lscpu=<path>
   |
   | Specify the location of a pre-collected strace log of lscpu to use
     instead of lscpu run on the local system. Used for testing or
     remote configuration.

--test-sensors=<path>
   |
   | Specify the location of a pre-collected strace log of sensors to
     use instead of sensors run on the local system. Used for testing or
     remote configuration.

EXAMPLES
========

The log file for sensors can be collected with:

script -c 'strace -e trace=open,openat,read sensors -u' sensors.log

The log file for lscpu can be collected with:

script -c 'strace -e trace=open,openat lscpu' /tmp/lscpu.tmp \| grep
'^open.*cpuinfo_max_freq' > lscpu.log; rm /tmp/lscpu.tmp

NOTES
=====

When using test input file(s), the live system data will be used if the
corresponding test file is not specified.

Systems (kernels) lacking cpu frequency reporting produce no output from
lscpu.

The use of --nodash is recommended for compatibility with downstream
analysis tools. White space appearing in metric names is unconditionally
transformed to \_.

SEE ALSO
========

:ref:`sensors(1) <sensors>`, :ref:`lscpu(1) <lscpu>`, :ref:`filesingle(7) <filesingle>`, :ref:`ldmsd(8) <ldmsd>`.
