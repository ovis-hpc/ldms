.. _ldms_ibnet_schema_name:

======================
ldms_ibnet_schema_name
======================

--------------------------------------------------
Man page for the LDMS ibnet plugin support utility
--------------------------------------------------

:Date:   4 June 2020
:Manual section: 1
:Manual group: LDMS sampler


SYNOPSIS
========

ldms_ibnet_schema_name <plugin config options>

DESCRIPTION
===========

The ibnet plugin generates a schema name including a hash of certain
configuration data. ldms_ibnet_schema_name provides the user with the
resulting name before running ldmsd so that store plugins can be
configured.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

See :ref:`ibnet(7) <ibnet>`.

EXAMPLES
========

::

   ldms_ibnet_schema_name node-name-map=/path/map timing=2 metric-conf=/path/metricsubsets schema=myibnet

   when file /path/metricsubsets contains

   extended
   xmtsl
   rcvsl
   xmtdisc
   rcverr
   oprcvcounters
   flowctlcounters
   vloppackets
   vlopdata
   vlxmitflowctlerrors
   vlxmitcounters
   swportvlcong
   rcvcc
   slrcvfecn
   slrcvbecn
   xmitcc
   vlxmittimecc
   smplctl

   yields

   myibnet_7fffe_tn

NOTES
=====

If the timing option is greater than 0, the name of the overall timing
set will be as for the result given with "_timing" appended.

SEE ALSO
========

:ref:`ibnet(7) <ibnet>`
