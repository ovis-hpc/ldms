.. _ldms_auth_munge:

===============
ldms_auth_munge
===============

--------------------------------
LDMS authentication using munge
--------------------------------

:Date:   10 May 2018
:Manual section: 7
:Manual group: LDMS authentication

SYNOPSIS
========

*ldms_app* **-a munge [-A socket=PATH ]**

DESCRIPTION
===========

**ldms_auth_munge** relies on the **munge** service (see :ref:`munge(7) <munge>`)
to authenticate users. The munge daemon (**munged**) must be up and
running.

The optional **socket** option can be used to specify the path to the
munged unix domain socket in the case that munged wasn't using the
default path or there are multiple munge domains configured.

SEE ALSO
========

:ref:`munge(7) <munge>`, :ref:`munged(8) <munged>`
