.. _slingshot_switch:

===================
slingshot_switch
===================

-------------------------------------------------------
Man page for the LDMS slingshot_switch sampler plugin
-------------------------------------------------------

:Date:   18 Nov 2026
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| load name=<plugin_instance> plugin=slingshot_switch
| config name=<plugin_instance> [ <attr> = <value> ]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. The slingshot_switch plugin uses LDMS_V_LIST and
LDMS_V_RECORD to provide slingshot switch info via the dump_counters
command run on the switch.

slingshot_switch is a multi-instance plugin: it can be loaded several
times, each time with a different name, to run several independent
instances in the same ldmsd process. Each instance is configured and
started separately and has its own conffile, schema, and sample
interval. This replaces the earlier approach of a separate
slingshot_switch_1 plugin solely to sample at a second rate; that is
now done by loading a second instance of this same plugin.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The slingshot_switch plugin uses the sampler_base base class. This man
page covers only the configuration attributes, or those with default
values, specific to this plugin; see :ref:`ldms_sampler_base(7)
<ldms_sampler_base>` for the attributes of the base class.

**load**
   | name=<plugin_instance>
   | plugin=slingshot_switch

   Each ``load`` creates one instance of the plugin. <plugin_instance>
   is the name that the ``config`` and ``start`` commands use to refer
   to that instance. It is not the metric set instance name, which is
   set by ``instance=`` of the base class.

**config**
   | name=<plugin_instance>
   | configuration line

   name=<plugin_instance>
      |
      | The name given to ``load`` for this instance.

   conffile=<conffile>
      |
      | Configuration file. The first line that does not start with
        '#' must be "n=XXX" or "p=XXX,YYY,ZZZ", starting in the first
        column; it must not be blank. p does not support ranges. Then
        variables or groups are listed one per line. A line starting
        with '#' is a comment.

      Each variable is passed to dump_counters as a counter name.

   schema=<schema>
      |
      | Optional schema name. It is intended that the same sampler on
        different nodes, or different instances of this plugin on the
        same node, have a different schema. If not specified, will
        default to ``slingshot_switch``.

An instance can be configured only once. To sample with a different
conffile, schema, or interval, load another instance with a different
name.

BUGS (and future enhancements)
==============================

· This is still under development.

· Does not yet support ranges for the ports.

· Does not check for duplicate ports.

· Could have more robust handling of errors in the config file.

· MAX Ports is 70.

· Possibly can reduce unnecessary allocations in schema_metric_list.

· DEBUG messages are excessive, while this is in development.

· Need to check for extra whitespace in variable names.

· Only checking for the expected number of data output lines. Note that
the output has at least one extra line.

EXAMPLES
========

1) Within ldmsd_controller or a configuration file:

::

   load name=ss_1s plugin=slingshot_switch
   config name=ss_1s producer=vm1_1 instance=vm1_1/slingshot_switch conffile=/etc/ldms/slingshot_1s.txt
   start name=ss_1s interval=1000000 offset=0

To sample at a second rate, load a second instance of the same plugin
under a different name, with its own ``instance=``, ``schema=`` and
``conffile=``:

::

   load name=ss_10s plugin=slingshot_switch
   config name=ss_10s producer=vm1_1 instance=vm1_1/slingshot_switch_10s schema=slingshot_switch_10s conffile=/etc/ldms/slingshot_10s.txt
   start name=ss_10s interval=10000000 offset=0

2) For the ldmsd configuration file sampler_ss.conf:

::

   env SWITCH=$(hostname)
   env COMPONENT_ID=1

   load name=ss_1s plugin=slingshot_switch
   config name=ss_1s producer=${SWITCH} component_id=${COMPONENT_ID} instance=${SWITCH}/port_metrics conffile=/etc/ldms/slingshot_1s.txt
   start name=ss_1s interval=1000000

with slingshot_1s.txt:

::

   p=0,1,2,3
   rfc_3635

Command line to start ldmsd using the above:

::

   ldmsd -x sock:411 -c /etc/ldms/sampler_ss.conf -m 2M -v QUIET

Then ldms_ls output (the counter values will differ):

::

   x3000c0r42b0/port_metrics: consistent, last update: Fri Nov 17 17:24:08 2023 +0000 [23292us]
   M u64        component_id                               1
   D u64        job_id                                     0
   D u64        app_id                                     0
   M record_type slingshot_port                             LDMS_V_RECORD_TYPE
   D list<>     slingshot_port_list
     port (x) IfInDiscards (x) IfInErrors (x) IfInUnknownProtos (x) IfOutDiscards (x) IfOutErrors (x) Dot3HCInPauseFrames (x) Dot3HCOutPauseFrames (x) IfHCInOctets (x) IfHCInUcastPkts (x) IfHCInMulticastPkts (x) IfHCInBroadcastPkts (x) IfHCOutOctets (x) IfHCOutUcastPkts (x) IfHCOutMulticastPkts (x) IfHCOutBroadcastPkts (x)
	 0       3135102637              0            3135102637                 0               0                       0                        0  147205648261491       2495004354147                    2471                       0        1536216301             20234005                        0                        0
	 1       3135102637              0            3135102637                 0               0                       0                        0  147204949152872       2494992497033                       0                       0         698442077             10279716                        0                        0
	 2       3135102637              0            3135102637                 0               0                       0                        0  147205081815556       2494994737508                       0                       0         698345785             10272362                        0                        0
	 3       3135102637              0            3135102637                 0               0                       0                        0  147205460019681       2495001153446                       0                       0         698845184             10277326                        0                        0

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`
