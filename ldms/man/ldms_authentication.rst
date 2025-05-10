.. _ldms_authentication:

===================
ldms_authentication
===================

----------------------------------
Authentication in LDMS transports
----------------------------------

:Date:   28 Feb 2018
:Manual section: 8
:Manual group: LDMS

DESCRIPTION
===========

LDMS applications use authentication plugins in LDMS transports to
authenticate the peers. In other words, not only **ldmsd** authenticates
the client connections, the clients (**ldms_ls**, **ldmsctl**,
**ldmsd_controller**, and other **ldmsd**) authenticate the **ldmsd**
too.

**ldmsd**, **ldms_ls**, **ldmsd_controller**, and **ldmsctl** use the
following options for authentication purpose:

**-a**\ *AUTH_PLUGIN*
   Specifying the name of the authentication plugin. The default is
   "none" (no authentication).

**-A**\ *NAME*\ **=**\ *VALUE*
   Specifying options to the authentication plugin. This option can be
   given multiple times.

**auth** configuration object has been introduced in **ldmsd** version
4.3.4. It describes an authentication domain in the configuration file
with **auth_add** command. **listen** and **prdcr_add** config commands
can refer to **auth** object created by **auth_add** command to specify
the authentication domain a listening port or a producer connection
belong to. If no **auth** option is specified, **listen** and
**prdcr_add** commands fall back to use the authentication method
specified by **-a, -A** CLI options (which is default to **none**).

Please consult the manual of the plugin for more details.

LIST OF LDMS_AUTH PLUGINS
=========================

**none**
   Authentication will NOT be used (allow all connections) (see
   :ref:`ldms_auth_none(7) <ldms_auth_none>`).

**ovis**
   The shared secret authentication using ovis_ldms (see
   :ref:`ldms_auth_ovis(7) <ldms_auth_ovis>`).

**naive**
   The naive authentication for testing. (see :ref:`ldms_auth_naive(7) <ldms_auth_naive>`).

**munge**
   User credential authentication using Munge. (see
   :ref:`ldms_auth_munge(7) <ldms_auth_munge>`).

SEE ALSO
========

:ref:`ldms_auth_none(7) <ldms_auth_none>`, :ref:`ldms_auth_ovis(7) <ldms_auth_ovis>`,
:ref:`ldms_auth_naive(7) <ldms_auth_naive>`, :ref:`ldms_auth_munge(7) <ldms_auth_munge>`, :ref:`ldmsctl(8) <ldmsctl>`,
:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_ls(8) <ldms_ls>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`,
:ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldms_build_install(7) <ldms_build_install>`
