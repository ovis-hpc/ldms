===============
ldms_auth_naive
===============

:Date:   28 Feb 2018

NAME
====

ldms_auth_naive - naive LDMS authentication implementation FOR TESTING

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
