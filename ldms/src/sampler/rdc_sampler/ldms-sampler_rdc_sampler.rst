.. _rdc_sampler:

==================
rdc_sampler
==================

-----------------------------------------
Man page for the LDMS rdc_sampler plugin
-----------------------------------------

:Date:   1 Apr 2021
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| config name=rdc_sampler [ <attr>=<value> ]

DESCRIPTION
===========

The rdc_sampler plugin provides AMD gpu device data. Data sets may be
wide or per-device. Plugins for the ldmsd (ldms daemon) are configured
via ldmsd_controller or a configuration file.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

**config**
   | name=<plugin_name> [producer=<name>] [instance=<name>]
     [component_id=<uint64_t>] [schema=<name_base>] [uid=<user-id>]
     [gid=<group-id>] [perm=<mode_t permission bits>] [metrics=LIST]
     [update_freq=MICROSEC] [max_keep_age=SEC] [max_keep_samples=N]
   | configuration line

   name=<plugin_name>
      |
      | This MUST be rdc_sampler.

   producer=<pname>.
      |
      | The producer string value for the timing set.

   instance=<set_prefix>
      |
      | The set instance names will be suffixed by device number
        (gpu%d).

   schema=<name_base>
      |
      | Optional schema base name. The default is rdc_sampler. The name
        base is suffixed to create uniquely defined schema names based
        on the plugin options specified.

   component_id=<compid>
      |
      | Optional component identifier for the timing set. Defaults to
        zero.

   metrics=LIST
      |
      | The list of values to be collected as named in rdc_field_t from
        rdc/rdc.h.

   update_freq=MICROSEC
      |
      | An argument passed to rdc_field_watch.

   max_keep_age=SEC
      |
      | An argument passed to rdc_field_watch.

   max_keep_samples=N
      |
      | An argument passed to rdc_field_watch.

   warmup=K
      |
      | Delay K cycles update_freq long before attempting to read data
        from the gpu.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=rdc_sampler
   config name=rdc_sampler component_id=1
   start name=rdc_sampler interval=1000000

NOTES
=====

The exact schema name that will be generated can be determined using the
ldms_rdc_schema_name utility. The data available may depend on the
specific GPUs and their configuration.

The rdc libraries loaded by the plugin may emit inconsequential error
messages to stdout. Two such begin with "<timestamp> ERROR
RdcLibraryLoader.cc" "<timestamp> ERROR RdcMetricFetcherImpl.cc" The
latter suggests you may have requested metrics unsupported by your
hardware.

BUGS
====

At ldmsd exit, there is a race between sampler termination and the rdc
library thread cleanup. This may lead to an exception being thrown in
the library code that terminates ldmsd with a C++ exception message.

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`,
:ref:`ldms_rdc_schema_name(1) <ldms_rdc_schema_name>`
