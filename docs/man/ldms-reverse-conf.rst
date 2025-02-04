=================
ldms-reverse-conf
=================

:Date:   6 Jun 2022

NAME
====

ldms-reverse-conf.sh - generate a tear-down configuration file

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

ldmsctl(8), ldmsd_controller(8)
