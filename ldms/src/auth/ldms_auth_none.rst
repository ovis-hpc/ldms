.. _ldms_auth_none:

==============
ldms_auth_none
==============

-----------------------------
LDMS authentication disabled
-----------------------------

:Date:   28 Feb 2018
:Manual section: 7
:Manual group: LDMS authentication

SYNOPSIS
========

*ldms_app* **-a none [Default]**

DESCRIPTION
===========

**ldms_auth_none** enables running without authentication of query
sources. Since "-a none" is the default it need not be specified (e.g.,
running "ldmsd -x sock:1024 -a none" is equivalent to simply running
"ldmsd -x sock:1024"). Using this authentication type there will be NO
checks on identities associated with data and/or meta-data information
accesses.
