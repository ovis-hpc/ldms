.. _ldms_auth_ovis:

==============
ldms_auth_ovis
==============

--------------------------------------------
LDMS authentication using ovis_auth library
--------------------------------------------

:Date:   28 Feb 2018
:Manual section: 7
:Manual group: LDMS authentication

SYNOPSIS
========

*ldms_app* **-a ovis [-A conf=**\ *PATH*\ **]**

DESCRIPTION
===========

**ldms_auth_ovis** uses shared secret to authenticate the connection.
The secret is a text file containing the line:

   secretword=X

where X is a string at least 8 characters long. Lines starting with # in
the file are ignored.

Four locations are checked in order for the secret:

1) the full file path given on the command line via ``-A conf=authfile``,

2) the full file path given in environment variable **LDMS_AUTH_FILE**,

3) $HOME/.ldmsauth.conf, and

4) $SYSCONFDIR/ldmsauth.conf (e.g. /etc/ldmsauth.conf).

where $HOME is taken from */etc/password* and $SYSCONFDIR is determined
at ldms compile time. If one of these is not set, the search continues
with the next location. A failure in reading one, if the file exists,
ends the search and is a failure to authenticate.

The secret file permissions must be set to 600 or more restrictive.

ENVIRONMENT
===========

"LDMS_AUTH_FILE" is a full file path for a secretword file. It is not
necessary, if the file is in one of the other checked locations.

NOTES
=====

Authentication can be disabled at ldms build time by configuring your
ldms build with --disable-ovis_auth. Then no secretword file is required
or checked.

BUGS
====

Networked file system users should verify the privacy of their secret
files, as various access control list schemes might be more permissive
than the standard permissions bits.
