.. _lsdate:

======
lsdate
======

--------------------------------------------------------------
List directory contents with UTC timestamp suffix translation
--------------------------------------------------------------

:Date:   June 2018
:Manual section: 1
:Manual group: LDMS

SYNOPSIS
========

**lsdate** [*LSOPTION*]... [*FILE*]...

DESCRIPTION
===========

Execute :ref:`ls(1)` and apply an output filter to reveal the calendar date of
timestamp prefix, infix, or suffixed files, such as produced by LDMS stores.
Timestamps are assumed to be seconds since the epoch. Fractional seconds
are ignored in computing the date.

SEE ALSO
========

:ref:`ls(1) <ldms_ls>`, :ref:`store_csv(7) <store_csv>`

NOTES
=====

The output of lsdate -s and the output of lsdate -l may be surprising.

If more than one epoch timestamp appears in the file name, only the first
one is converted and appended in the output.

Dates after year 2285 are not supported, as these contain 11 or more
digits instead of 10.
