.. _cray_dvs_sampler:

=======================
cray_dvs_sampler
=======================

----------------------------------------------
Man page for the LDMS cray_dvs_sampler plugin
----------------------------------------------

:Date:   05 Feb 2018
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| config name=cray_dvs_sampler [ <attr>=<value> ]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file.

The cray_dvs_sampler plugin provides memory info from
/proc/fs/dvs/mount/[mount-id]/stats. A separate metric set is produced
for each mount point. Metric set names are of the form \`XXX'.

See section \`DATA AND THE CONFIGURATION FILE' for information on the
variables and configuration file.

This sampler is for Cray systems only.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The cray_dvs_sampler plugin uses the sampler_base base class. This man
page covers only the configuration attributes, or those with default
values, specific to the this plugin; see :ref:`ldms_sampler_base(7) <ldms_sampler_base>` for the
attributes of the base class.

**config**
   | name=<plugin_name> [schema=<sname> conffile=<cfile>]
   | configuration line

   name=<plugin_name>
      |
      | This MUST be cray_dvs_sampler

   schema=<schema>
      |
      | Optional schema name. It is intended that the same sampler on
        different nodes with different metrics have a different schema.
        If not specified, will default to \`cray_dvs_sampler`.

   conffile=<cfile>
      |
      | Optional path to the configuration file

DATA AND THE CONFIGURATION FILE
===============================

| The data source is /proc/fs/dvs/mount/[mount-id]/stats. This file
  consists of a number of lines of the format
| variablename: v1 v2 ... vN

The number of values varies between 1 and 6. Each line will then produce
between 1 and 6 metrics with names of the form variablename appended by
an additional string associated with thr interpretation of that value
(e.g, min, err).

By default, this sampler will collect all the variables for all mount
points. The number of metrics can be downselected by using a
configuration file (see conffile argument). The format of this file is
one variablename per line, comments start with '#' and blank lines are
skipped. Note that the variablename from the dataline is what is
specified in the configuration file, not the metricnames associated with
that variablename in the data source file. As a result, all metrics
associated with a give line in the dvs stats source are included or
excluded together.

NOTES
=====

-  In the config, the sampler is called cray_dvs_sampler. Also the
   library is called libcray_dvs_sampler. However, the source file is
   dvs_sampler.c

-  This sampler is for Cray systems only.

BUGS
====

None known.

EXAMPLES
========

TBD

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`
