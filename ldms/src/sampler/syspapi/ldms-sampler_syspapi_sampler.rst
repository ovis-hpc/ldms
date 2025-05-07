.. _syspapi_sampler:

======================
syspapi_sampler
======================

----------------------------------------------
Man page for the LDMSD syspapi_sampler plugin
----------------------------------------------

:Date:   30 Sep 2019
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

Within ldmsd_controller or a configuration file:
**config**
**name=syspapi_sampler** **producer=**\ *PRODUCER*
**instance=**\ *INSTANCE* [ **schema=\ SCHEMA** ] [
**component_id=\ COMPONENT_ID** ] [ **cfg_file=\ PATH** ] [
**events=\ EVENTS** ] [ **cumulative=\ 0\ \|\ 1** ] [
**auto_pause=\ 0\ \|\ 1** ]

DESCRIPTION
===========

**syspapi_sampler** collects system-wide hardware event counters using
Linux perf event (see :ref:`perf_event_open(2) <perf_event_open>`), but use PAPI event
names. **libpapi** and **libpfm** are used to translate PAPI event names
to Linux perf event attributes. In the case of per-process (job) data
collection, please see **papi_sampler**. There are two approaches
to define a list of events: 1) **events** option, or 2) PAPI JSON config
file. For the **events** option, simply list the events of interest
separated by comma (e.g. events=PAPI_TOT_INS,PAPI_TOT_CYC). For the PAPI
JSON config file (**cfg_file** option), the format of the file is as
follows:

   ::

      {
          "schema": "my_syspapi",
          "events": [
             ...
          ]
      }

The **schema** is optional, but if specified in the JSON config file, it
precedes the schema name given at the **config** command. The **events**
is a list of PAPI event names (strings).

If both **cfg_file** and **events** options are given to the config
command, the list are concatenated. Please note that an event that
appears on both lists will result in an error.

**auto_pause**\ =\ *1* (which is the default) makes **syspapi_sampler**
paused the data sampling when receiving a notification from
**papi_sampler** that a job is active, and resumed the data sampling
when receiving a notification from **papi_sampler** that all jobs have
terminated. This is to prevent perf system resource contention. We have
seen all 0 counters on **papi_sampler** without any errors (could be a
silent error) when run it with active **syspapi_sampler**.

CONFIG OPTIONS
==============

**name=syspapi_sampler**
   This MUST be syspapi_sampler (the name of the plugin).

**producer=**\ *PRODUCER*
   The name of the data producer (e.g. hostname).

**instance=**\ *INSTANCE*
   The name of the set produced by this plugin.

**schema=**\ *SCHEMA*
   The optional schema name (default: syspapi_sampler). Please note that
   the **"schema"** from the JSON **cfg_file** overrides this option.

**component_id=**\ *COMPONENT_ID*
   An integer identifying the component (default: *0*).

**cfg_file=**\ *PATH*
   The path to JSON-formatted config file. This is optional if
   **events** option is specified. Otherwise, this option is required.

**events=**\ *EVENTS*
   The comma-separated list of PAPI events of interest (e.g.
   *PAPI_TOT_INS,PAPI_TOT_CYC*). This is optional if **cfg_file** is
   specified. Otherwise, this option is required.

**cumulative=**\ *0*\ **\|**\ *1*
   *0* (default) for non-cumulative data sampling (reset after read), or
   *1* for cumulative data sampling.

**auto_pause=**\ *0*\ **\|**\ *1*
   *0* to ignore **papi_sampler** pause/resume notification, or *1*
   (default) to pause/resume according to notifications from
   **papi_sampler**.

BUGS
====

No known bugs.

EXAMPLES
========

Plugin configuration example:

   ::

      load name=syspapi_sampler
      config name=syspapi_sampler producer=${HOSTNAME} \
             instance=${HOSTNAME}/syspapi component_id=2 \
             cfg_file=/tmp/syspapi.json
      start name=syspapi_sampler interval=1000000 offset=0

JSON cfg_file example:

   ::

      {
        "events": [
          "PAPI_TOT_INS",
          "PAPI_TOT_CYC"
        ]
      }

SEE ALSO
========

:ref:`papi_sampler(7) <papi_sampler>`, :ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`,
:ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`.
