.. _opa2:

===========
opa2
===========

---------------------------------------------------
Man page for the LDMS opa2 OmniPath network plugin
---------------------------------------------------

:Date:   5 Feb 2018
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| load name=opa2 config name=opa2 [ <attr>=<value> ]

DESCRIPTION
===========

The opa2 plugin provides local port counters from OmniPath hardware. A
separate data set is created for each port. All sets use the same
schema.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

**config**
   | name=<plugin_name> producer=<pname> instance=<instance>
     [schema=<sname>] [component_id=<compid>] [ports=<portlist>]
   | configuration line

   name=<plugin_name>
      |
      | This MUST be opa2.

   producer=<pname>
      |
      | The producer string value.

   instance=<set_name>
      |
      | The set_name supplied is ignored, and the name
        $producer/$CA/$port is used.

   schema=<schema>
      |
      | Optional schema name. Default opa2. The same schema is used for
        all sets.

   component_id=<compid>
      |
      | Optional component identifier. Defaults to zero.

   ports=<portlist>
      |
      | Port list is a comma separated list of ca_name.portnum or a '*'.
        The default is '*', which collects a set for every host fabric
        interface port.

BUGS
====

None known.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=opa2
   config name=opa2 producer=compute1 instance=compute1/opa2 component_id=1
   start name=opa2 interval=1000000

NOTES
=====

This sampler will be expanded in the future to capture additional
metrics.

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`
