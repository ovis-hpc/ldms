.. _ldms_auth_naive:

===============
ldms_auth_naive
===============

-----------------------------------------------------
Naive LDMS authentication implementation FOR TESTING
-----------------------------------------------------

:Date:   28 Feb 2018
:Manual section: 7
:Manual group: LDMS authentication

SYNOPSIS
========

*ldms_app* **-a naive** **[-A uid=**\ *UID*\ **]** **[-A
gid=**\ *GID*\ **]**

DESCRIPTION
===========

**ldms_auth_naive** LDMS authentication plugin naively believes the
peer's credential declaration. The purpose of this plugin is purely for
testing the permission control of various objects in **ldmsd**. The
**uid** and **gid** options are used to specify the user credential. If
**uid** and/or **gid** are not specified, the default is -1.
