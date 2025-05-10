.. _coretemp:

===============
coretemp
===============

---------------------------------------------------------
An LDMS sampler plugin that monitors CPU temperature data
---------------------------------------------------------

:Date:   3 May 2022
:Manual section: 7
:Manual group: LDMS sampler


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

See the Linux :ref:`modprobe(8) <modprobe>` command for information on how to load Linux
modules.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

See man base.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=coretemp
   config name=coretempp producer=vm1_1 instance=vm1_1/coretemp
   start name=coretemp interval=1000000 offset=0

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`,
:ref:`filesingle(7) <filesingle>`
