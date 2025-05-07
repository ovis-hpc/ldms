.. _ldms_rdc_schema_name:

====================
ldms_rdc_schema_name
====================

--------------------------------------------------------
Man page for the LDMS rdc_sampler plugin support utility
--------------------------------------------------------

:Date:   2 April 2021
:Manual section: 1
:Manual group: LDMS sampler


SYNOPSIS
========

ldms_rdc_schema_name -h ldms_rdc_schema_name [-d] <plugin config
options>

DESCRIPTION
===========

The rdc_sampler plugin generates a schema name including a hash of
certain configuration data. ldms_rdc_schema_name provides the user with
the resulting name before running ldmsd so that store plugins can be
configured.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

See :ref:`rdc_sampler(7) <rdc_sampler>`.

EXAMPLES
========

::

   #ldms_rdc_schema_name -h
   <dump of the usage details from the plugin.>

   # ldms_rdc_schema_name metrics=base schema=myrdc_sampler | grep -v ERROR
   myrdc_sampler_51dcba58

   # ldms_rdc_schema_name metrics=xgmi
   rdc_sampler_device_e3e41d59

   # ldms_rdc_schema_name -d metrics=xgni
   <error messages about unknown xgni option>

NOTES
=====

The rdc libraries loaded by the plugin and the program may emit
inconsequential error messages to stdout. One such begins with
"<timestamp> ERROR RdcLibraryLoader.cc".

SEE ALSO
========

:ref:`rdc_sampler(7) <rdc_sampler>`
