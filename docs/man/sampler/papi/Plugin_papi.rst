===========
Plugin_papi
===========

:Date:   09 May 2016

NAME
====

Plugin_papi - man page for the LDMS papi sampler plugin.

SYNOPSIS
========

| Within ldmsctl
| ldmsctl> config name=spapi [ <attr>=<value> ]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsctl. The papi sampler plugin
runs on the nodes and provides data about the the occurrence of
micro-architectural events using papi library by accessing hardware
performance counters.

ENVIRONMENT
===========

You will need to build LDMS with --enable-papi. Papi library should be
available through plugin library path.

LDMSCTL CONFIGURATION ATTRIBUTE SYNTAX
======================================

**config**

name=<plugin_name> events=<comma separated list of events> pid=<process
id> producer=<producer_name> instance=<instance_name> [schema=<sname>]
[component_id=<compid> with_jobid=<bool>] ldmsctl configuration line

name=<plugin_name>
   |
   | This MUST be spapi.

producer=<pname>
   |
   | The producer string value.

instance=<set_name>
   |
   | The name of the metric set

schema=<schema>
   |
   | Optional schema name. It is intended that the same sampler on
     different nodes with different metrics have a different schema.

component_id=<compid>
   |
   | Optional component identifier. Defaults to zero.

with_jobid=<bool>
   |
   | Option to collect job id with set or 0 if not.

events=<comma separated list of events>
   |
   | Comma separated list of events. Available events can be determined
     using papi_avail command if papi is installed on system.

pid - The PID for the process being monitored
   |

NOTES
=====

In order to check if an event is available on the system you can run
papi_avail.

BUGS
====

No known bugs.

EXAMPLES
========

The following is a short example that measures 4 events.
   |
   | Total CPU cycles
   | Total CPU instructions
   | Total branch instructions
   | Mispredicted branch instructions

$ldmsctl -S $LDMSD_SOCKPATH

| ldmsctl> load name=spapi
| ldmsctl> config name=spapi producer=$PRODUCER_NAME
  instance=$INSTANCE_NAME pid=$PID
  events=PAPI_TOT_INS,PAPI_TOT_CYC,PAPI_BR_INS,PAPI_BR_MSP
| ldmsctl> start name=spapi interval=$INTERVAL_VALUE
| ldmsctl> quit

SEE ALSO
========

papi_avail(1) , ldmsd(7), ldms_quickstart(7)
