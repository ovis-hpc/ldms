.. _slingshot_info:

=====================
slingshot_info
=====================

--------------------------------------------
Man page for the LDMS slingshot_info plugin
--------------------------------------------

:Date:   1 May 2022
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| config name=slingshot_info [ <attr> = <value> ]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms aemon) are configured via ldmsd_controller or a
configuration file. The slingshot_info plugin provides a single metric
set that contains a list of records. Each record contains all of the
informational fields for a single slingshot NIC.

The slingshot_info sampler plugin provides a fairly small set of general
information about each slingshot NIC, including FRU description, serial
number, etc. Likely users will want to sample this plugin relatively
infrequently. For detailed slingshot NIC counter data, see the
slingshot_metrics sampler plugin.

The schema is named "slingshot_info" by default.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The slingshot_info plugin uses the sampler_base base class. This man
page covers only the configuration attributes, or those with default
values, specific to the this plugin; see :ref:`ldms_sampler_base(7) <ldms_sampler_base>` for the
attributes of the base class.

**config**
   | name=<plugin_name> [counters=<COUNTER NAMES>] [counters_file=<path
     to counters file>]
   | configuration line

   name=<plugin_name>
      |
      | This MUST be slingshot_info.

EXAMPLES
========

Within ldmsd_conteroller or a configuration file:

::

   load name=slingshot_info
   config name=slingshot_info producer=host1 instance=host1/slingshot_info
   start name=slingshot_info interval=1000000 offset=0

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`,
:ref:`slingshot_metrics(7) <slingshot_metrics>`
