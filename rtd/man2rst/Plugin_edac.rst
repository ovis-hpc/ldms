===========
Plugin_edac
===========

:Date: 18 Feb 2018

.. contents::
   :depth: 3
..

NAME
============

Plugin_edac - man page for the LDMS edac plugin

SYNOPSIS
================

| Within ldmsd_controller
| config name=edac [ <attr> = <value> ]

DESCRIPTION
===================

The edac plugin provides memory error information from
/sys/devices/system/edac for correctable and uncorrectable errors.

CONFIGURATION ATTRIBUTE SYNTAX
======================================

The edac plugin uses the sampler_base base class. This man page covers
only the configuration attributes, or those with default values,
specific to the this plugin; see ldms_sampler_base.man for the
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
============

This reports counts for both correctable and uncorrectable errors per mc
and per csrow. It also reports the seconds since reset per mc.

EXAMPLES
================

Within ldmsd_controller or a configuration file:

::

   load name=edac
   config name=edac producer=vm1_1 component_id=1 instance=vm1_1/edac max_mc=2 max_csrow=4
   start name=edac interval=1000000

NOTES
=============

An upper limit on metric set size is enforced. Configuring to collect
too many registers will generate an error detailing the compiled size
limit. This limit is only adjustable in the source code.

For more detailed background information, see
www.kernel.org/doc/Documentation/edac.txt and
www.kernel.org/doc/Documentation/ABI/testing/sysfs-devices-edac.

AUTHORS
===============

Kathleen Shoga <shoga1@llnl.gov> (Lawrence Livermore National
Laboratory). Ported to LDMS v3 by Benjamin Allan <baallan@sandia.gov>.
Ported to LDMS v4 by Ann Gentile <gentile@sandia.gov>.

ACKNOWLEDGMENTS
=======================

This work was created under the auspices of the U.S. Department of
Energy by Lawrence Livermore National Laboratory under Contract
DE-AC52-07NA27344. Release Number: LLNL-SM-687054.

SEE ALSO
================

edac(3), edac-util(8), edac-ctl(8), ldms(7), ldms_sampler_base(7)
