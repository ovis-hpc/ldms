.. _aries_mmr:

================
aries_mmr
================

-------------------------------------------------
Man page for the aries_mmr sampler and variants.
-------------------------------------------------

:Date:   05 Jan 2020
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

| Within ldmsd_controller or in a configuration file
| config name=aries_mmr [ <attr> = <value> ]
| config name=aries_nic_mmr [ <attr> = <value> ]
| config name=aries_rtr_mmr [ <attr> = <value> ]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. The aries_XXX_mmr sampler variants. provides aries
network counter information. The particular counters to be read are
specified by configuration files. No functional combinations of the
counters are supported (.i.e., does not sum or scale values).

The aries_XXX_mmr samplers depend on Cray's libgpcd, built with aries
options. This library has been released by Cray in CLE6 and later. You
cannot build this sampler if you do not have the libraries and headers.
If you have the code to build the library, be sure to build with
**CFLAGS=-fPIC**

The difference between the variants is that aries_nic_mmr will skip any
counters in the inputfile that do NOT begin with **AR_NIC_**; aries_rtr_mmr
does the opposite; and aries_mmr does NO name filtering.

Different types of metrics are added to separate gpcd_contexts. The
order of the metrics in the output is the contexts in a particular
order, with the metrics in each context as they are specified in the
file.

For the config file, all counter names must be fully spelled out (i.e.,
does not resolve the shorthand given in the documentation for the
counters).

To build any of the aries_mmr samplers, build with the following flags:
**--enable-aries_mmr**
**--with-aries-libgpcd=<full_path_to_libgpcd.a>,<full_path_to_lib_gpcd.h>**

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The aries_mmr plugin uses the sampler_base base class. This man page
covers only the configuration attributes, or those with default values,
specific to the this plugin; see :ref:`ldms_sampler_base(7) <ldms_sampler_base>` for the
attributes of the base class.

**config**
   | name=<plugin_name> file=<file> [aries_rtr_id=<rtrid>
     schema=<sname>]
   | configuration line

   name=<plugin_name>
      |
      | This MUST be aries_mmr, aries_nic_mmr, or aries_rtr_mmr.

   schema=<sname>
      |
      | Optional schema name. It is intended that the same sampler on
        different nodes with different metrics have a different schema.
        This will default to cray_aries_r or cray_gemini_r as
        appropriate, if unspecified.

   aries_rtr_id=<rtrid>
      |
      | Optional aries router identifier. Defaults to 0 length string.

   file=<file>
      |
      | Configuration file of aries performance counter names that will
        be added in exactly as they are specified. At least one file
        must be specified.

NOTES
=====

-  This is entirely independent of the cray_aries_r_sampler.

-  At the moment, no functions of the data (either in the sampler or in
   a store) are supported.

-  Counters whose names do not resolve are left out.

-  If you start this sampler on a node for which the counters cannot be
   obtained (e.g., an external login node), the set may still get
   created, however the sample function will fail and the plugin will be
   stopped.

-  A non-sampler, standalone version of this code is in the Source in
   util/aries/mmr_reader. It is not built via the build.

-  These samplers may change at any time.

BUGS
====

No known bugs.

EXAMPLES
========

| > cat metrics.txt
| #RAW METRICS
| AR_NIC_NETMON_ORB_EVENT_CNTR_REQ_FLITS

|
| AR_NIC_RSPMON_NPT_EVENT_CNTR_NL_FLITS
| # this is a test
| AR_RTR_1_2_INQ_PRF_INCOMING_FLIT_VC0

| load name=aries_mmr
| config name=aries_mmr producer=localhost2
  instance=localhost2/aries_mmr schema=aries_mmr
  file=/home/XXX/metrics.txt
| start name=aries_mmr interval=1000000

> ldms_ls localhost2/aries_mmr: consistent, last update: Wed Oct 28
08:48:36 2015 [153343us] u64 0 AR_RTR_1_2_INQ_PRF_INCOMING_FLIT_VC0 u64
5968204876 AR_NIC_RSPMON_NPT_EVENT_CNTR_NL_FLITS u64 4182142522
AR_NIC_NETMON_ORB_EVENT_CNTR_REQ_FLITS

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`, :ref:`cray_sampler_variants(7) <cray_sampler_variants>`,
:ref:`aries_linkstatus(7) <aries_linkstatus>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`
