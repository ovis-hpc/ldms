===============
Plugin_coretemp
===============

:Date:   3 May 2022

NAME
====

Plugin_coretemp - An LDMS sampler plugin that monitors CPU temperature
data

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| load name=coretemp config name=coretemp producer=<name>
  instance=<name> component_id=<int>

DESCRIPTION
===========

The coretemp sampler collects information from the Linux coretemp module
through files located in /sys/devices/platform. Files in this directory
are walked recursively and regular expressions are used to select
entries produced by the Linux coretemp module.

See the Linux modprobe(8) command for information on how to load Linux
modules.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

See man Plugin_base.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=coretemp
   config name=coretempp producer=vm1_1 instance=vm1_1/coretemp
   start name=coretemp interval=1000000 offset=0

SEE ALSO
========

ldmsd(8), ldms_quickstart(7), ldmsd_controller(8), ldms_sampler_base(7),
Plugin_filesingle(7)
