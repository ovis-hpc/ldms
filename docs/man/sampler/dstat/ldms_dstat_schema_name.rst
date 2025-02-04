======================
ldms_dstat_schema_name
======================

:Date:   17 Nov 2020

NAME
====

ldms_dstat_schema_name - man page for the LDMS dstat plugin support
utility

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

See Plugin_dstat(7).

EXAMPLES
========

::

   ldms_dstat_schema_name auto-schema=1 fd=1

   yields

   dstat_10

SEE ALSO
========

Plugin_dstat(7)
