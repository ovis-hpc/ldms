.. _hweventpapi:

==================
hweventpapi
==================

-----------------------------------------
Man page for the LDMS hweventpapi plugin
-----------------------------------------

:Date:   18 Feb 2018
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| config name=hweventpapi [ <attr>=<value> ]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. The hweventpapi plugin provides energy sampling
using RAPL via the PAPI interface for sandybridge.

WARNING: This sampler is unsupported.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The hweventpapi plugin uses the sampler_base base class. This man page
covers only the configuration attributes, or those with default values,
specific to the this plugin; see :ref:`ldms_sampler_base(7) <ldms_sampler_base>` for the
attributes of the base class.

**config**
   | name=<plugin_name> [schema=<sname>]
   | configuration line

   name=<plugin_name>
      |
      | This MUST be hweventpapi.

   schema=<schema>
      |
      | Optional schema name. It is intended that the same sampler on
        different nodes with different metrics have a different schema.
        If not specified, will default to \`hweventpapi`.

   metafile=<PATH>
      |
      | The metafile defines what to collect with undocumented
        "attribute=value" syntax. The metafile is watched for changes
        and automatically reloaded.

BUGS
====

This man page is incomplete.

NOTES
=====

-  This sampler is unsupported.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=jobinfo
   config name=jobinfo producer=${HOSTNAME} instance=${HOSTNAME}/jobinfo component_id=${COMPONENT_ID} uid=0 gid=0 perm=0700
   load name=hweventpapi
   config name=hweventpapi producer=${HOSTNAME} instance=${HOSTNAME}/hweventpapi job_set=${HOSTNAME}/jobinfo component_id=${COMPONENT_ID} metafile=/tmp/papi.conf uid=0 gid=0 perm=0700
   start name=hweventpapi interval=1000000 offset=0

Within the metafile configuration:

::

   this needs to be filled in.

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`
