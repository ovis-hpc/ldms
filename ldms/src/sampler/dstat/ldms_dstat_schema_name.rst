.. _ldms_dstat_schema_name:

======================
ldms_dstat_schema_name
======================

--------------------------------------------------
Man page for the LDMS dstat plugin support utility
--------------------------------------------------

:Date:   17 Nov 2020
:Manual section: 1
:Manual group: LDMS sampler


SYNOPSIS
========

ldms_dstat_schema_name <plugin config options>

DESCRIPTION
===========

The dstat plugin optionally generates a schema name including a short
hash of certain configuration data. ldms_dstat_schema_name provides the
user with the schema name the dstat plugin will generate for the given
options.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

See :ref:`dstat(7) <dstat>`.

EXAMPLES
========

::

   ldms_dstat_schema_name auto-schema=1 fd=1

   yields

   dstat_10

SEE ALSO
========

:ref:`dstat(7) <dstat>`
