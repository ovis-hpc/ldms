.. _slingshot_switch:

===================
slingshot_switch
===================

-------------------------------------------------------
Man page for the LDMS slingshot_switch sampler plugin
-------------------------------------------------------

:Date:   17 Nov 2023
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| config name=slingshot_switch [ <attr> = <value> ]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. The slingshot_switch plugin uses LDMS_V_LIST and
LDMS_V_RECORD to provide slingshot switch info via the dump_counters
command run on the switch.

slingshot_switch and slingshot_switch_1 are the same plugins. There are
two copies to enable sampling at two different rates.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The slingshot_switch plugin uses the sampler_base base class. This man
page covers only the configuration attributes, or those with default
values, specific to the this plugin; see ldms_sampler_base.man for the
attributes of the base class.

**config**
   | name=<plugin_name>
   | configuration line

   name=<plugin_name>
      |
      | This MUST be slingshot_switch (or slingshot_switch_1).

   conffile=<conffile>
      |
      | Configuration file. First non-comment line must be "n=XXX" or
        "p=XXX,YYY,ZZZ". p does not support ranges. Then variables or
        groups are listed one per line. Comments lines can be in the
        file designated by the first line being a '#'.

      Arguments are those of dump_counters.

   schema=<schema>
      |
      | Optional schema name. It is intended that the same sampler on
        different nodes with different metrics have a different schema.
        If not specified, will default to \`slingshot_switch\` (or
        \`slingshot_switch_1`).

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

   load name=slingshot_switch
   config name=slingshot_switch producer=vm1_1 instance=vm1_1/slingshot_switch conffile=/home/confffile.txt
   start name=slingshot_switch interval=1000000 offset=0

or the above with \`slingshot_switch_1`.

conffile.txt can look something like:

::

   #This can be a leading comment(s)
   n=65
   # This can be an interspersed comment(s)
   cfrx
   #This is yet another comment(s)

2) For confile sampler_ss.conf:

::

   env SWITCH=$(hostname)
   env COMPONENT_ID=1

   load name=slingshot_switch
   config name=slingshot_switch producer=${SWITCH} component_id=${COMPONENT_ID} instance=${SWITCH}/port_metrics conffile=/rwfs/OVIS_slingshot-4.4.1/etc/ldms/slingshot_ldms_1s.txt
   start name=slingshot_switch interval=1000000

with slingshot_ldms_1s.txt:

::

   p=0,1,2,3
   rfc_3635

Command line to start ldmsd using the above:

::

   /rwfs/OVIS_slingshot-4.4.1/etc/ldms# ldmsd -x sock:411 -c /rwfs/OVIS_slingshot-4.4.1/etc/ldms/sampler_ss.conf -m 2M -v QUIET

Then ldms_ls output:

::

   x3000c0r42b0/port_metrics1: consistent, last update: Fri Nov 17 17:24:08 2023 +0000 [23292us]
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

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldsm_sampler_base>`
