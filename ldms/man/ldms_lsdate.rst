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

**lsdate** [*OPTION*]... [*FILE*]...

DESCRIPTION
===========

Execute :ref:`ls(1) <ldms_ls>` and apply an output filter to reveal the calendar date of
timestamp suffixed files, such as produced by LDMS CVS stores.
Timestamps are assumed to be seconds since the epoch. Fractional seconds
are not supported.

SEE ALSO
========

:ref:`ls(1) <ldms_ls>`, :ref:`store_csv(7) <store_csv>`

NOTES
=====

The output of lsdate -s and the output of lsdate -l may be surprising.
