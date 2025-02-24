.. _lsdate:

======
lsdate
======

:Date:   June 2018

NAME
====

lsdate - list directory contents with UTC timestamp suffix translation

SYNOPSIS
========

**lsdate** [*OPTION*]... [*FILE*]...

DESCRIPTION
===========

Execute :ref:`ls(1) <ls>` and apply an output filter to reveal the calendar date of
timestamp suffixed files, such as produced by LDMS CVS stores.
Timestamps are assumed to be seconds since the epoch. Fractional seconds
are not supported.

SEE ALSO
========

:ref:`ls(1) <ls>`, :ref:`store_csv(7) <store_csv>`

NOTES
=====

The output of lsdate -s and the output of lsdate -l may be surprising.
