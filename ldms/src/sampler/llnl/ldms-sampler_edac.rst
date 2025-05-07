.. _edac:

===========
edac
===========

----------------------------------
Man page for the LDMS edac plugin
----------------------------------

:Date:   18 Feb 2018
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

| Within ldmsd_controller
| config name=edac [ <attr> = <value> ]

DESCRIPTION
===========

The edac plugin provides memory error information from
/sys/devices/system/edac for correctable and uncorrectable errors.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The edac plugin uses the sampler_base base class. This man page covers
only the configuration attributes, or those with default values,
specific to the this plugin; see :ref:`ldms_sampler_base(7) <ldms_sampler_base>` for the
attributes of the base class.

**config**
   | name=<plugin_name> max_mc=<max_mc> max_csrow=<max_csrow>
     [schema=<schema>]
   | configuration line

   name=<plugin_name>
      |
      | This MUST be edac.

   schema=<schema>
      |
      | Optional schema name. It is intended that the same sampler on
        different nodes with different metrics have a different schema.
        If not specified, will default to edac.

   max_mc=<max_mc>
      |
      | The number of mc's in /sys/devices/system/edac/mc. Typically
        this number is 2.

   max_csrow=<max_csrow>
      |
      | The number of csrows in a single mc. For example, the value
        should be 4 for when the largest csrow looks like:
        /sys/devices/system/edac/mc/mc0/csrow3. Typically this number is
        8, but it can vary depending on the system.

DATA
====

This reports counts for both correctable and uncorrectable errors per mc
and per csrow. It also reports the seconds since reset per mc.

EXAMPLES
========

In the shell starting the daemon:

::

   export max_mc=$(ls /sys/devices/system/edac/mc/mc* -d |wc -l)
   export max_csrow=$(ls /sys/devices/system/edac/mc/mc0/csrow* -d |wc -l)

   Within ldmsd_controller or a configuration file:
   load name=edac
   config name=edac producer=vm1_1 component_id=1 instance=vm1_1/edac max_mc=${max_mc} max_csrow=${max_csrow} schema=edac_${max_mc}x${max_csrow}
   start name=edac interval=1000000

NOTES
=====

An upper limit on metric set size is enforced. Configuring to collect
too many registers will generate an error detailing the compiled size
limit. This limit is only adjustable in the source code. The edac
information is assumed to be rectangular, that is every mc device has
the same number of csrow elements within. This is known to be untrue on
ThunderX2 processors, but the tx2mon plugin is the correct plugin to use
instead for tracking memory errors on that architecture.

For more detailed background information, see
www.kernel.org/doc/Documentation/edac.txt and
www.kernel.org/doc/Documentation/ABI/testing/sysfs-devices-edac.

SEE ALSO
========

:ref:`edac(3) <edac>`, edac-:ref:`util(8) <util>`, edac-:ref:`ctl(8) <ctl>`, :ref:`ldms(7) <ldms>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`
