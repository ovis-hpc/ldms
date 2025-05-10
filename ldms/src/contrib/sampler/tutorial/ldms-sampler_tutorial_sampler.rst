.. _tutorial_sampler:

=======================
tutorial_sampler
=======================

----------------------------------------------
Man page for the LDMS tutorial_sampler plugin
----------------------------------------------

:Date:   24 Oct 2019
:Manual section: 7
:Manual group: LDMS store

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| config name=tutorial_sampler [ <attr>=<value> ]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. The tutorial_sampler plugin is a demo sampler
described in the LDMSCON2019 tutorial "LDMS v4: Sampler and Store
Writing".

This sampler is a simplified version of test_sampler, with a fixed
number of sets and u64 data types only. Max sets is determined by
MAXSETS in the source.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The tutorial_sampler plugin uses the sampler_base base class. This man
page covers only the configuration attributes, or those with default
values, specific to the this plugin; see :ref:`ldms_sampler_base(7) <ldms_sampler_base>` for the
attributes of the base class.

**config**
   | name=<plugin_name> [schema=<sname>] [num_metrics=<num_metrics>]
   | configuration line

   name=<plugin_name>
      |
      | This MUST be tutorial_sampler.

   num_metrics=<num_metrics>
      |
      | Optional number of metrics for this set. Metrics will be U64.
        Metric names will be 'metric_%d'. If not specified, default
        number of metrics is determined by DEFAULTNUMMETRICS in the
        source.

   schema=<schema>
      |
      | Optional schema name. It is intended that any sets with
        different metrics have a different schema. If not specified,
        will default to \`tutorial_sampler`. Therefore, if you are
        creating multiple sets in this sampler, you will most likely
        want to define schema for each set.

BUGS
====

No known bugs.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   config name=tutorial_sampler producer=localhost1 instance=localhost1/test1 schema=test1 component_id=1
   config name=tutorial_sampler producer=localhost1 instance=localhost1/test2 schema=test2 component_id=2 num_metrics=5
   config name=tutorial_sampler producer=localhost1 instance=localhost1/test3 schema=test3 component_id=1 num_metrics=2
   job_set=localhost1/jobid
   start name=tutorial_sampler interval=1000000

> ldms_ls localhost1/test1: consistent, last update: Thu Oct 24 10:55:14
2019 -0600 [223680us] M u64 component_id 1 D u64 job_id 0 D u64 app_id 0
D u64 metric0 2 D u64 metric1 4 D u64 metric2 6 D u64 metric3 8 D u64
metric4 10 D u64 metric5 12 D u64 metric6 14 D u64 metric7 16 D u64
metric8 18 D u64 metric9 20 localhost1/test2: consistent, last update:
Thu Oct 24 10:55:14 2019 -0600 [223699us] M u64 component_id 2 D u64
job_id 0 D u64 app_id 0 D u64 metric0 4 D u64 metric1 8 D u64 metric2 12
D u64 metric3 16 D u64 metric4 20 localhost1/test3: consistent, last
update: Thu Oct 24 10:55:14 2019 -0600 [223717us] M u64 component_id 1 D
u64 job_id 0 D u64 app_id 0 D u64 metric0 6 D u64 metric1 12

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`,
:ref:`test_sampler(7) <test_sampler>`, :ref:`store_tutorial(7) <store_tutorial>`
