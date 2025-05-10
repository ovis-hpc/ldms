.. _aries_mmr_configurable:

=============================
aries_mmr_configurable
=============================

------------------------------------------------
Man page for the aries_mmr_configurable sampler.
------------------------------------------------

:Date:   12 Apr 2020
:Manual section: 7
:Manual group: LDMS sampler


SYNOPSIS
========

| Within ldmsd_controller or in a configuration file
| config name=aries_mmr_configurable [ <attr> = <value> ]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. The aries_mmr_configurable sampler provides aries
network counter information. It is intended to be used for reading and
optionally resetting the configuable counters, however there is nothing
that currently restricts this.

The particular counters to be read and set are specified by
configuration files. No functional combinations of the counters are
supported (i.e., does not sum or scale values). The available counter
names can be discovered by: gpcd_print_valid_tile_mmrs();
gpcd_print_valid_nic_mmrs(); gpcd_print_valid_tile_filtering_mmrs();
gpcd_print_valid_tile_static_mmrs();

A utility providing this service is built as check_mmr_configurable into
bin. The counters are described in Cray's Aries Hardware Counters
Document S-0045. Counters described in that document with ':' extensions
cannot be called by the ':' name in this sampler; rather the counter has
to be read by the base name as hex and the fields separated out by mask,
which is beyond the capability of this sampler.

The aries_XXX_mmr samplers depend on Cray's libgpcd, built with aries
options. This library has been released by Cray in CLE6 and later. You
cannot build this sampler if you do not have the libraries and headers.
If you have the code to build the library, be sure to build the library
with **CFLAGS=-fPIC**

The set and read metrics are added to separate gpcd_contexts. The order
of the metrics in the output is the contexts in a particular order, with
the metrics in each context as they are specified in the file. The
counters for read and set can only be specified once and cannot be
changed. The counters to be set can be reset to their configured values
at any time by issuing the action=reset command to configure.

For the config file, all counter names must be fully spelled out (i.e.,
does not resolve the shorthand given in the documentation for the
counters).

To build the aries_mmr_configurable sampler, build with the following
flags: **--enable-aries_mmr**
**--with-aries-libgpcd=<full_path_to_libgpcd.a>,<full_path_to_lib_gpcd.h>**

**!!!WARNING!!!** Cray does not recommend use of the configurable
counters outside of CrayPAT. Use this Plugin at your own risk.
**!!!WARNING!!!**

CONFIGURATION COMMANDS ORDER
============================

Configuration commands are intended to be issued in the following order:

-  load

-  config action=initialize

-  config action=finalize

-  start

The following config commands can be issued anytime after the start in
any order

-  config action=reset

-  config action=ls

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The aries_mmr_configurable plugin uses the sampler_base base class. This
man page covers only the configuration attributes, or those with default
values, specific to the this plugin; see :ref:`ldms_sampler_base(7) <ldms_sampler_base>` for the
attributes of the base class.

**config**
   | name=<plugin_name> action=<action> [ <attr>=<value> ...]
   | configuration line

   name=<plugin_name>
      |
      | This MUST be aries_mmr_configurable

   action=<action>
      |
      | Options are initialize, finalize, reset, and ls:

   **initialize**
      | [schema=<sname> setfile=<cfile> rtrid=<rtrid>] readfile=<rfile>
      | initialize the plugin. sampler_base configuration arguments
        should be specified at this point.

      setfile=<cfile>
         |
         | Optional configuration file with the counter value assignment
           options.
         | Format: "name,type,default_value" one entry per line.
         | Type is 'H' for Hex or anything else to default to uint64_t.
         | Value should be written out in standard decimal or hex
           (leading 0x) format.
         | Blanklines and comments (specfied by leading #) are allowed.
         | The sampler uses gpcd_lookup_mmr_by_name, so only the names
           that are in the 'valid' sets specified by the gpcd library
           are allowed. As of this writing those can be obtained by:
           gpcd_print_valid_tile_mmrs(); gpcd_print_valid_nic_mmrs();
           gpcd_print_valid_tile_filtering_mmrs();
           gpcd_print_valid_tile_static_mmrs();

      These are printed out in the utility check_mmr_configurable.

      readfile=<rfile>
         |
         | Configuration file with the names of the counters to read.
         | Format "name,type" one entry per line.
         | Type is 'H' for Hex or anything else to default to uint64_t.
           Hex values are written out as a char array.
         | Blanklines and comments (specfied by leading #) are allowed.
         | The sampler uses gpcd_lookup_mmr_by_name, so only the names
           that are in the 'valid' sets specified by the gpcd library
           are allowed. As of this writing those can be obtained by:
           gpcd_print_valid_tile_mmrs(); gpcd_print_valid_nic_mmrs();
           gpcd_print_valid_tile_filtering_mmrs();
           gpcd_print_valid_tile_static_mmrs();

      These are printed out in the utility check_mmr_configurable.

      rtrid=<rtrid>
         |
         | Optional unique rtr string identifier (e.g., c0-0c0s0a0).
           Defaults to 0 length string.

      schema=<sname>
         |
         | Optional schema name. Defaults to 'aries_mmr_configurable'.

   **finalize**
      |
      | Creates the mmr_contexts, sets the set counters to the
        configured values, and creates the set. Takes no arguments. If
        finalize fails, all state is cleared and the plugin can be
        configured again.

   **ls**
      |
      | Prints out the set counter names and their configured values and
        also the read counter names. Takes no arguments.

   **reset**
      |
      | Resets the set counters to their configured values. Takes no
        arguments.

NOTES
=====

-  See WARNINGS above.

-  This is entirely independent of the cray_aries_r_sampler.

-  At the moment, no functions of the data (either in the sampler or in
   a store) are supported.

-  Counters whose names do not resolve are left out.

-  If you start this sampler on a node for which the counters cannot be
   obtained (e.g., an external login node), the set may still get
   created, however the sample function will fail and the plugin will be
   stopped.

-  While the names are checked to be in the valid set (see note above),
   there is nothing that checks that the value that you choose to write
   to a counter is valid.

-  If writing the counters is not enabled, this plugin must be run as
   root in order to call the gpcd command that enables writing the
   counters.

-  This sampler may change at any time.

BUGS
====

-  There is an unavoidable race condition if someone out of band disable
   permissions of writing the counters in between the check in this
   sampler and the actual write.

-  Because the sampler needs to write this will toggle on the write
   ability for anyone.

EXAMPLES
========

| > more setconf.txt
| AR_NIC_NETMON_ORB_EVENT_CNTR_REQ_FLITS,U,0
| AR_NIC_ORB_CFG_NET_RSP_HIST_OVF,H,0xFF
| AR_NIC_ORB_CFG_NET_RSP_HIST_1,H,0x000A000500010000

| > more readconf.txt
| AR_NIC_NETMON_ORB_EVENT_CNTR_REQ_FLITS,U
| AR_NIC_ORB_CFG_NET_RSP_HIST_OVF,H
| AR_NIC_ORB_CFG_NET_RSP_HIST_1,H
| AR_NIC_ORB_PRF_NET_RSP_HIST_BIN01,H
| AR_NIC_ORB_PRF_NET_RSP_HIST_BIN23,H
| AR_NIC_ORB_PRF_NET_RSP_HIST_BIN45,H
| AR_NIC_ORB_PRF_NET_RSP_HIST_BIN67,H

| load name=aries_mmr_configurable
| config name=aries_mmr_configurable producer=localhost1
  instance=localhost1/aries_mmr schema=aries_mmr_configurable
  setfile=XXX/setconf.txt readfile=XXX/Build/readconf.txt component_id=1
  action=initialize aries_rtr_id=c0-0c0a0
| config name=aries_mmr_configurable action=finalize
| config name=aries_mmr_configurable action=ls
| start name=aries_mmr_configurable interval=5000000

| >ldms_ls
| localhost1/aries_mmr: consistent, last update: Sun Apr 12 19:04:00
  2020 -0600 [290661us]
| M u64 component_id 1
| D u64 job_id 0
| D u64 app_id 0
| M char[] aries_rtr_id "c0-0c0a0"
| D u64 AR_NIC_NETMON_ORB_EVENT_CNTR_REQ_FLITS 30756
| D char[] AR_NIC_ORB_CFG_NET_RSP_HIST_OVF "0x0"
| D char[] AR_NIC_ORB_CFG_NET_RSP_HIST_1 "0xa000500010000"
| D char[] AR_NIC_ORB_PRF_NET_RSP_HIST_BIN01 "0xcb400000d6b"
| D char[] AR_NIC_ORB_PRF_NET_RSP_HIST_BIN23 "0x0"
| D char[] AR_NIC_ORB_PRF_NET_RSP_HIST_BIN45 "0x0"
| D char[] AR_NIC_ORB_PRF_NET_RSP_HIST_BIN67 "0x0"

| Also in the logs from the action=ls:
| Sun Apr 12 19:03:55 2020: INFO : Name default R/S
| Sun Apr 12 19:03:55 2020: INFO :
  ------------------------------------------------ --------------------
  -----
| Sun Apr 12 19:03:55 2020: INFO :
  AR_NIC_NETMON_ORB_EVENT_CNTR_REQ_FLITS N/A R
| Sun Apr 12 19:03:55 2020: INFO : AR_NIC_ORB_CFG_NET_RSP_HIST_OVF N/A R
| Sun Apr 12 19:03:55 2020: INFO : AR_NIC_ORB_CFG_NET_RSP_HIST_1 N/A R
| Sun Apr 12 19:03:55 2020: INFO : AR_NIC_ORB_PRF_NET_RSP_HIST_BIN01 N/A
  R
| Sun Apr 12 19:03:55 2020: INFO : AR_NIC_ORB_PRF_NET_RSP_HIST_BIN23 N/A
  R
| Sun Apr 12 19:03:55 2020: INFO : AR_NIC_ORB_PRF_NET_RSP_HIST_BIN45 N/A
  R
| Sun Apr 12 19:03:55 2020: INFO : AR_NIC_ORB_PRF_NET_RSP_HIST_BIN67 N/A
  R
| Sun Apr 12 19:03:55 2020: INFO :
  AR_NIC_NETMON_ORB_EVENT_CNTR_REQ_FLITS 0 S
| Sun Apr 12 19:03:55 2020: INFO : AR_NIC_ORB_CFG_NET_RSP_HIST_OVF 0xff
  S
| Sun Apr 12 19:03:55 2020: INFO : AR_NIC_ORB_CFG_NET_RSP_HIST_1
  0xa000500010000 S

| At any time action=ls or action=reset can be called via
  ldmsd_controller:
| > more aries_mmr_configurable_controller_reset.sh #!/bin/bash
| echo "config name=aries_mmr_configurable action=reset"
| exit
| > ldmsd_controller --host localhost --port=${port1} -a munge --script
  "XXX/aries_mmr_configurable_controller_reset.sh"

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`, :ref:`cray_sampler_variants(7) <cray_sampler_variants>`,
:ref:`aries_linkstatus(7) <aries_linkstatus>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`aries_mmr(7) <aries_mmr>`,
aries_rtr_mmr)7), :ref:`aries_nic_mmr(7) <aries_nic_mmr>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`
