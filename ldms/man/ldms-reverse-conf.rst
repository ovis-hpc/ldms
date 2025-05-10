.. _ldms-reverse-conf:

=================
ldms-reverse-conf
=================

----------------------------------------
Generate a tear-down configuration file
----------------------------------------

:Date:   6 Jun 2022
:Manual section: 7
:Manual group: LDMS

SYNOPSIS
========

ldms-reverse-conf.sh <input>

DESCRIPTION
===========

The ldms-reverse-conf.sh command parses an LDMS control script in the
key/value language which sets up samplers, stores, producers, updaters,
and subscriptions, and attempts to generate the matching tear-down
script to stdout. Invoking the ldmsd_controller or ldmsctl with the
teardown script should yield and almost idle daemon (listeners are still
active).

Typically, a daemon is configured and left to run. The intent of this
utility is to make it easy to deconfigure a running daemon in the proper
command order given the original scripted configuration.

SEE ALSO
========

:ref:`ldmsctl(8) <ldmsctl>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`
