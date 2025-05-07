.. _lustre2_client:

=====================
lustre2_client
=====================

--------------------------------------------
Man page for the LDMS lustre2_client plugin
--------------------------------------------

:Date:   26 Oct 2017
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| ldmsctl> config name=lustre2_client [ <attr> = <value> ]

DESCRIPTION
===========

The lustre2_client plugin provides Lustre metric information.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

This plugin uses the sampler_base base class. This man page covers only
the configuration attributes, or those with default values, specific to
the this plugin; see :ref:`ldms_sampler_base(7) <ldms_sampler_base>` for the attributes of
the base class.

**config**\ **name**\ *=<plugin_name>*\ *<SAMPLER_BASE_OPTIONS> osc*\ **=<CSV>**\ *mdc*\ **=<CSV>**\ *llite*\ **=<CSV>**\ *osc_path =<oscpath>*\ **mdc_path=<mdcpath>**\ *"*\ **llite_path=<llitepath>**

Descriptions:

   **name**\ *=<plugin_name>*
      This MUST be lustre2_client.

   **<SAMPLER_BASE_OPTIONS>**
      Please see :ref:`ldms_sampler_base(7) <ldms_sampler_base>` for sampler_base options.

   **osc**\ *=<CSV>*
      CSV list of OSC's.

   **mdc**\ *=<CSV>*
      CSV list of MDC's.

   **llite**\ *=<CSV>*
      CSV list of LLITE's.

   **osc_path**\ *=<oscpath>*
      A user custom path to osc.

   **mdc_path**\ *=<mdcpath>*
      A user custom path to osc.

   **llite_path**\ *=<llitepath>*
      A user custom path to llite.

NOTES
=====

For oscs,mdcs and llites: if not specified, NONE of the oscs/mdcs/llites
will be added. If {oscs,mdcs,llites} is set to \*, all of the available
{oscs,mdcs,llites} at the time will be added.

The names that make up the list of oscs, mdcs and llites do not have to
include the uid part. For example, 'lustre-ffff8803245d4000' is the
actual file in /proc/fs/lustre/llite/, but you can just say
llites=lustre to include this component into the set.

osc_path, mdc_path, llite_path are optional full path names of stats
files if not in default location. The default locations are:
/sys/kernel/debug/lustre/{osc, mdc, llite}, and /proc/fs/lustre/{osc,
mdc, llite} depends on the Lustre version. Be aware that
/sys/kernel/debug is only readable by privileged users.

BUGS
====

None known.

EXAMPLES
========

::

   load name=lustre2_client
   config name=lustre2_client producer=compute1 component_id=1 instance=compute1/lustre2_client llites=*
   ldmsctl> start name=lustre2_client interval=1000000
   ldmsctl> quit

SEE ALSO
========

:ref:`ldms_sampler_base(7) <ldms_sampler_base>`, :ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`,
:ref:`ldmsd_controller(8) <ldmsd_controller>`
