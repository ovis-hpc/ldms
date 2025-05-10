.. _ldmsctl:

=======
ldmsctl
=======

---------------------------------
Issue control commands to ldmsd.
---------------------------------

:Date:   19 Nov 2019
:Manual section: 7
:Manual group: LDMSD

SYNOPSIS
========

ldmsctl [OPTION...]

DESCRIPTION
===========

After LDMS (lightweight Distributed Metric Service) version 3.4, ldmsctl
is an LDMS daemon C-interface that can be used to dynamically configure
an LDMS daemon instead of ldmsd_controller when Python is not available.
After the ldmsctl is started commands can be entered at the prompt or
(usually) a command script can be created and piped into the ldmsctl.

LDMS version 4 requires ldmsctl to use LDMS transport (data channel) to
connect to **ldmsd** to levarage LDMS Authentication plugin in the
transport. Please note that the **ldmsd** may have multiple data
channels, one of which can be dedicated for management use.

ENVIRONMENT
===========

The following environment variables must be set (includes environment
variables needed for the actions, for example, paths to the sampler
libraries to be added):

LD_LIBRARY_PATH
   path_to_ovis_build/lib:path_to_ovis_build/lib/ovis-ldms:path_to_libevent_2.0_build/lib

ZAP_LIBPATH
   path_to_ovis_build/lib/ovis-ldms

LDMSD_PLUGIN_LIBPATH
   path_to_ovis_build/lib/ovis-ldms

PATH
   path_to_ovis_build/sbin:path_to_ovis_build/bin

OPTIONS
=======

**-h**\ *HOST*
   HOST is the hostname to connect to the LDMS daemon

**-p**\ *PORT*
   PORT is the port to connect to the LDMS daemon

**-x**\ *XPRT*
   XPRT is the transport one of sock, ugni, or rdma. Only use with the
   option -i

**-a**\ *AUTH*
   AUTH is the name of the LDMS Authentication plugin to be used for the
   connection. Please see :ref:`ldms_authentication(7) <ldms_authentication>` for more
   information. If this option is not given, the default is "none" (no
   authentication).

**-A**\ *NAME*\ **=**\ *VALUE*
   Passing the *NAME*\ =\ *VALUE* option to the LDMS Authentication
   plugin. This command line option can be given multiple times. Please
   see :ref:`ldms_authentication(7) <ldms_authentication>` for more information, and consult
   the plugin manual page for plugin-specific options.

**-s**\ *SOURCE*
   SOURCE is the path to a configuration file

**-X**\ *COMMAND*
   COMMAND is a shell command to be executed. The output will be sent to
   ldmsd.

**-V**
   Display LDMS version information and then exit.

REGULAR EXPRESSION
==================

The regular expression specified in *regex=* option of the commands is a
POSIX Extended (modern) Regular Expression. In short, "*+?{}|^$." are
special regular expression characters. Please see :ref:`regex(7) <regex>` for more
information.

PLUGIN COMMAND SYNTAX
=====================

Load a plugin
-------------

| **load** attr=<value>

   **name**\ *name*
      |
      | The plugin name

List the usage of the loaded plugins
------------------------------------

**usage**

unload a plugin
---------------

| **term** attr=<value>

   **name**\ *name*
      |
      | The plugin name

Send a configuration command to the specified plugin.
-----------------------------------------------------

**config** attr=<value>

   **name**\ *name*
      |
      | The plugin name

   **attr=value**
      |
      | Plugin specific attr=value tuples

   ..

      **Attributes specific for sampler plugins (Some sampler plugins
      may have additional** attributes)

      **producer**\ *producer*
         |
         | A unique name for the host providing the data

      **instance**\ *instance*
         |
         | The set instance name. The name must be unique among all
           metric sets in all LDMS daemons.

      **[component_id**\ *component_id*\ **]**
         |
         | A unique number for the comopnent being monitored. The
           default is zero.

      **[schema**\ *schema*\ **]**
         |
         | The name of the metric set schema.

      **[job_set**\ *job_set*\ **]**
         |
         | The set instance name of the set containing the job data. The
           default is 'job_info'.

      **[uid**\ *uid*\ **]**
         |
         | The user id of the set's owner. The default is the returned
           value of geteuid().

      **[gid**\ *gid*\ **]**
         |
         | The group id of the set's owner. The default is the returned
           value of getegid().

      **[perm**\ *perm*\ **]**
         |
         | The sampler plugin instance access permission. The default is
           0440.

Start a sampler plugin
----------------------

**start** attr=<value>

   **name**\ *name*
      |
      | The plugin name.

   **interval**\ *interval*
      |
      | The sample interval, which is a float followed by a unit string.
        If no unit string is given, the default unit is microseconds. A
        unit string is one of the followings: us -- microseconds ms --
        milliseconds s -- seconds m -- minutes h -- hours d -- days

   **[offset**\ *offset*\ **]**
      |
      | Offset (shift) from the sample mark. The value is an integer,
        followed by a unit string. Offset can be positive or negative
        with magnitude up to 1/2 the sample interval. If this offset is
        specified, including 0, collection will be synchronous; if the
        offset is not specified, collection will be asynchronous.
        Optional.

Stop a sampler plugin
---------------------

**stop** attr=<value>

   **name**\ *name*
      |
      | The plugin name.

PRODUCER COMMAND SYNTAX
=======================

Add a producer to the aggregator
--------------------------------

| **prdcr_add** attr=<value>

   **name**\ *name*
      |
      | The producer name. The producer name must be unique in an
        aggregator. It is independent of any attributes specified for
        the metric sets or hosts.

   **xprt**\ *xprt*
      |
      | The transport name [sock, rdma, ugni]

   **host**\ *host*
      |
      | The hostname of the host

   **type**\ *conn_type*
      |
      | The connection type [active, passive]

   **reconnect**\ *interval*
      |
      | The connection retry interval, which is a float followed by a
        unit string. If no unit string is given, the default unit is
        microseconds. A unit string is one of the followings: us --
        microseconds ms -- milliseconds s -- seconds m -- minutes h --
        hours d -- days

   **interval**\ *interval*
      |
      | It is being precated. Please use 'reconnect'.

   **[perm**\ *permission*\ **]**
      |
      | The permission to modify the producer in the future

Delete a producer from the aggregator
-------------------------------------

| The producer cannot be in use or running
| **prdcr_del** attr=<value>

   **name**\ *name*
      |
      | The producer name

Start a producer
----------------

**prdcr_start** attr=<value>

   **name**\ *name*
      |
      | The producer name

   **[reconnect**\ *interval*\ **]**
      |
      | The connection retry interval, which is a float followed by a
        unit string. If no unit string is given, the default unit is
        microseconds. A unit string is one of the followings: us --
        microseconds ms -- milliseconds s -- seconds m -- minutes h --
        hours d -- days If unspecified, the previously configured value
        will be used. Optional.

   **[interval**\ *interval*\ **]**
      |
      | It is being deprecated. Please use 'reconnect'.

Start all producers matching a regular expression
-------------------------------------------------

**prdcr_start_regex** attr=<value>

   **regex**\ *regex*
      |
      | A regular expression

   **[reconnect**\ *interval*\ **]**
      |
      | The connection retry interval, which is a float followed by a
        unit string. If no unit string is given, the default unit is
        microseconds. A unit string is one of the followings: us --
        microseconds ms -- milliseconds s -- seconds m -- minutes h --
        hours d -- days If unspecified, the previously configured value
        will be used. Optional.

   **[interval**\ *interval*\ **]**
      |
      | It is being deprecated. Please use 'reconnect'.

Stop a producer
---------------

**prdcr_stop** attr=<value>

   **name**\ *name*
      |
      | The producer name

Stop all producers matching a regular expression
------------------------------------------------

**prdcr_stop_regex** attr=<value>

   **regex**\ *regex*
      |
      | A regular expression

Query producer status
---------------------

**prdcr_status** attr=<value>

   **[name**\ *name*\ **]**
      |
      | The producer name. If none is given, the statuses of all
        producers are reported.

Subscribe for stream data from all matching producers
-----------------------------------------------------

**prdcr_subsribe**

   **regex**\ *regex*
      |
      | The regular expression matching producer name

   **stream**\ *stream*
      |
      | The stream name

UPDATER COMMAND SYNTAX
======================

Add an updater process that will periodically sample producer metric sets
-------------------------------------------------------------------------

**updtr_add** attr=<value>

   **name**\ *name*
      |
      | The update policy name. The policy name should be unique. It is
        independent of any attributes specified for the metric sets or
        hosts.

   **interval**\ *interval*
      |
      | The update/collect interval, which is a float followed by a unit
        string. If no unit string is given, the default unit is
        microseconds. A unit string is one of the followings: us --
        microseconds ms -- milliseconds s -- seconds m -- minutes h --
        hours d -- days

   **[offset**\ *offset*\ **]**
      |
      | Offset for synchronized aggregation. Optional.

   **[push**\ *onchange|true*\ **]**
      |
      | Push mode: 'onchange' and 'true'. 'onchange' means the Updater
        will get an update whenever the set source ends a transaction or
        pushes the update. 'true' means the Updater will receive an
        update only when the set source pushes the update. If \`push\`
        is used, \`auto_interval\` cannot be \`true`.

   **[auto_interval**\ *true|false*\ **]**
      If true, the updater will schedule set updates according to the
      update hint. The sets with no hints will not be updated. If false,
      the updater will schedule the set updates according to the given
      interval and offset values. If not specified, the value is
      *false*.

   **[perm**\ *permission*\ **]**
      |
      | The permission to modify the updater in the future

Remove an updater from the configuration
----------------------------------------

**updtr_del** attr=<value>

   **name**\ *name*
      |
      | The update policy name

Add a match condition that specifies the sets to update.
--------------------------------------------------------

**updtr_match_add** attr=<value>

   **name**\ *name*
      |
      | The update policy name

   **regex**\ *regex*
      |
      | The regular expression

   **match**\ *match (inst|schema)*
      |
      | The value with which to compare; if match=inst, the expression
        will match the set's instance name, if match=schema, the
        expression will match the set's schema name.

Remove a match condition from the Updater.
------------------------------------------

**updtr_match_del** attr=<value>

   **name**\ *name*
      |
      | The update policy name

   **regex**\ *regex*
      |
      | The regular expression

   **match**\ *match (inst|schema)*
      |
      | The value with which to compare; if match=inst, the expression
        will match the set's instance name, if match=schema, the
        expression will match the set's schema name.

Add matching producers to an updater policy
-------------------------------------------

This is required before starting the updater.

**updtr_prdcr_add** attr=<value>

   **name**\ *name*
      |
      | The update policy name

   **regex**\ *regex*
      |
      | A regular expression matching zero or more producers

Remove matching producers to an updater policy
----------------------------------------------

**updtr_prdcr_del** attr=<value>

   **name**\ *name*
      |
      | The update policy name

   **regex**\ *regex*
      |
      | A regular expression matching zero or more producers

Start updaters.
---------------

**updtr_start** attr=<value>

   **name**\ *name*
      |
      | The update policy name

   **[interval**\ *interval*\ **]**
      |
      | The update interval, which is a float followed by a unit string.
        If no unit string is given, the default unit is microseconds. A
        unit string is one of the followings: us -- microseconds ms --
        milliseconds s -- seconds m -- minutes h -- hours d -- days If
        this is not specified, the previously configured value will be
        used. Optional.

   **[offset**\ *offset*\ **]**
      |
      | Offset for synchronized aggregation. Optional.

Stop an updater.
----------------

The Updater must be stopped in order to change it's configuration.

**updtr_stop** attr=<value>

   **name**\ *name*
      |
      | The update policy name

Query the updater status
------------------------

**updtr_status** attr=<value>

   **[name**\ *name*\ **]**
      |
      | The updater name. If none is given, the statuses of all updaters
        are reported.

Query updaters' list of regular expressions to match set names and set schemas
------------------------------------------------------------------------------

**updtr_match_list** attr=<value>

   **[name**\ *name*\ **]**
      |
      | The updater name. If none is given, all updaters' regular
        expressions list are returned.

STORE COMMAND SYNTAX
====================

Create a Storage Policy and open/create the storage instance.
-------------------------------------------------------------

**strgp_add** attr=<value>

   **name**\ *name*
      |
      | The unique storage policy name.

   **plugin**\ *plugin*
      |
      | The name of the storage backend.

   **container**\ *container*
      |
      | The storage backend container name.

   **[schema**\ *schema*\ **]**
      |
      | The schema name of the metric set to store. If 'schema' is
        given, 'regex' is ignored. Either 'schema' or 'regex' must be
        given.

   **[regex**\ *name*\ **]**
      |
      | A regular expression matching set schemas. It must be used with
        decomposition. Either 'schema' or 'regex' must be given.

   **[perm**\ *permission*\ **]**
      |
      | The permission to modify the storage in the future

Remove a Storage Policy
-----------------------

| All updaters must be stopped in order for a storage policy to be
  deleted
| **strgp_del** attr=<value>

   **name**\ *name*
      |
      | The storage policy name

Add a regular expression used to identify the producers this storage policy will apply to.
------------------------------------------------------------------------------------------

| If no producers are added to the storage policy, the storage policy
  will apply on all producers.
| **strgp_prdcr_add** attr=<value>

   **name**\ *name*
      |
      | The storage policy name

   **regex**\ *name*
      |
      | A regular expression matching metric set producers.

Remove a regular expression from the producer match list
--------------------------------------------------------

**strgp_prdcr_del** attr=<value>

   | **name**\ *name*
   | The storage policy name

   **regex**\ *regex*
      |
      | The regex of the producer to remove.

Add the name of a metric to store
---------------------------------

**strgp_metric_add** attr=<value>

   | **name**\ *name*
   | The storage policy name

   **metric**\ *metric*
      |
      | The metric name. If the metric list is NULL, all metrics in the
        metric set will be stored.

Remove a metric from the set of stored metrics.
-----------------------------------------------

**strgp_metric_del** attr=<value>

   | **name**\ *name*
   | The storage policy name

   **metric**\ *metric*
      |
      | The metric to remove

Start a storage policy.
-----------------------

**strgp_start** attr=<value>

   | **name**\ *name*
   | The storage policy name

Stop a storage policy.
----------------------

A storage policy must be stopped in order to change its configuration.

**strgp_stop** attr=<value>

   | **name**\ *name*
   | The storage policy name

Query the storage policy status
-------------------------------

**strgp_status** attr=<value>

   **[name**\ *name*\ **]**
      |
      | The storage policy name. If none is given, the statuses of all
        storage policies are reported.

FAILOVER COMMAND SYNTAX
=======================

Please see :ref:`ldmsd_failover(7) <ldmsd_failover>`.

SETGROUP COMMAND SYNTAX
=======================

Please see :ref:`ldmsd_setgroup(7) <ldmsd_setgroup>`.

STREAM COMMAND SYNTAX
=====================

Publish data to the named stream
--------------------------------

**plublish** attr=<value>

   **name**\ *name*
      |
      | The stream name

   **data**\ *data*
      |
      | The data to publish

Subscribe to a stream
---------------------

**subscribe** attr=<value>

   **name**\ *name*
      |
      | The stream name

LDMS DAEMON COMMAND SYNTAX
==========================

Changing the log levels of LDMSD infrastructures
------------------------------------------------

**loglevel** attr=<value> (deprecated)

**log_level** attr=<value>

**level**\ *string*
   |
   | A string specifying the log levels to be enabled

The valid string are "default", "quiet", and a comma-separated list of
DEBUG, INFO, WARN, ERROR, and CRITICAL. It is case insensitive.
"default" means to set the log level to the defaul log level. "quiet"
means disable the log messages. We note that "<level>," and "<level>"
give different results. "<level>" -- a single level name -- sets the log
level to the given level and all the higher severity levels. In
contrast, "<level>," -- a level name followed by a comma -- sets the log
level to only the given level.

**[name**\ *name*\ **]**
   |
   | A logger name

**[regex**\ *regex*\ **]**
   |
   | A regular expression matching logger names. If neither 'name' or
     'regex' is given, the command sets the default log level to the
     given level. For example, 'regex=xprt.*' will change the
     transport-related log levels. Use log_status to query the available
     log infrastructures.

Query LDMSD's log information
-----------------------------

**log_status** attr=<value>

   | **[name**\ *value*\ **]**
   | A logger name

Exit the connected LDMS daemon gracefully
-----------------------------------------

**daemon_exit**

Query the connected LDMS daemon status
--------------------------------------

**daemon_status**

Tell the daemon to dump it's internal state to the log file.
------------------------------------------------------------

**status** <type> [name=<value>]

   | **[**\ *type]*
   | Reports only the specified objects. The choices are prdcr, updtr
     and strgp.

      | prdcr: list the state of all producers.
      | updtr: list the state of all update policies.
      | strgp: list the state of all storage policies.

   [name\ *value*]
      The object name of which the status will be reported.

SET COMMAND SYNTAX
==================

Set the user data value for a metric in a metric set.
-----------------------------------------------------

|
| **udata** attr=<value>

   **set**\ *set*
      |
      | The sampler plugin name

   **metric**\ *metric*
      |
      | The metric name

   **udata**\ *udata*
      |
      | The desired user-data. This is a 64b unsigned integer.

Set the user data of multiple metrics using regular expression.
---------------------------------------------------------------

| The user data of the first matched metric is set to the base value.
  The base value is incremented by the given 'incr' value and then sets
  to the user data of the consecutive matched metric and so on.
| **udata_regex** attr=<value>

   **set**\ *set*
      |
      | The metric set name.

   **regex**\ *regex*
      |
      | A regular expression to match metric names to be set

   **base**\ *base*
      |
      | The base value of user data (uint64)

   **[incr**\ *incr*\ **]**
      |
      | Increment value (int). The default is 0. If incr is 0, the user
        data of all matched metrics are set to the base value. Optional.

Change the security parameters of LDMS sets using regular expression.
---------------------------------------------------------------------

The set security change affects only the new clients or the new
connections. The clients that already have access to the set will be
able to continue to get set updates, regardless of their permission.

| To apply the new set security to the aggregators, on the first level
  aggregator, users will stop and start the producer from which the set
  has been aggregated. After the connection has been re-established, the
  first-level aggregator can see the set if its permission matches the
  new set security. There are no steps to perform on higher-level
  aggregators. Given that the first-level aggregator has permission to
  see the set, it will compare the second-level aggregator’s permission
  with the set security after successfully looking up the set. The
  second-level aggregator will be able to look up the set if it has
  permission to do so. The process continues on the higher-level
  aggregators automatically.
| **set_sec_mod** attr=<value>

   **regex**\ *"*\ **regex**
      |
      | A regular expression to match set instance names

   **[uid**\ *uid*\ **]**
      |
      | An existing user name string or a UID. Optional

   **[gid**\ *gid*\ **]**
      |
      | A GID. Optional

   **[perm**\ *perm*\ **]**
      |
      | An octal number representing the permission bits. Optional

MISC COMMAND SYNTAX
===================

Display the list of available commands
--------------------------------------

|
| **help** <command>

   | [*command]*
   | If a command is given, the help of the command will be printed.
     Otherwise, only the available command names are printed.

Get the LDMS version the running LDMSD is based on.
---------------------------------------------------

**version**

NOTES
=====

-  ldmsctl is currently kept for backwards compatibility purposes with
   LDMS v2 commands. ldmsctl still works in version 3, however with
   ldmsctl, some capabilitites use v2 pathways as opposed to v3.

-  ldmsctl will be removed in a future release. It is not recommended
   that you use this with v2.

BUGS
====

No known bugs.

EXAMPLES
========

1) Run ldmsctl

::

   $/tmp/opt/ovis/sbin/ldmsctl -h vm1_2 -p 10001 -x sock
   ldmsctl>

2) After starting ldmsctl, configure "meminfo" collector plugin to
collect every second.

::

   Note: interval=<# usec> e.g interval=1000000 defines a one second interval.
   ldmsctl> load name=meminfo
   ldmsctl> config name=meminfo component_id=1 set=vm1_1/meminfo
   ldmsctl> start name=meminfo interval=1000000
   ldmsctl> quit

3) Configure collectors on host "vm1" via bash script called collect.sh

::

   #!/bin/bash
   # Configure "meminfo" collector plugin to collect every second (1000000 usec) on vm1_2
   echo "load name=meminfo"
   echo "config name=meminfo component_id=2 set=vm1_2/meminfo"
   echo "start name=meminfo interval=1000000"
   # Configure "vmstat" collector plugin to collect every second (1000000 usec) on vm1_2
   echo "load name=vmstat"
   echo "config name=vmstat component_id=2 set=vm1_2/vmstat"
   echo "start name=vmstat interval=1000000"

   Make collect.sh executable
   chmod +x collect.sh

   Execute collect.sh (Note: When executing this across many nodes you would use pdsh to execute the script on all nodes
   in parallel)
   > ldmsd -x sock:11111 -l ldmsd.log
   > ldmsctl -x sock -p 11111 -h localhost -X collect.sh

4) Example of updtr_match_list's report

::

   ldmsctl> updtr_add name=meminfo_vmstat interval=1000000 offset=100000
   ldmsctl> updtr_match_add name=meminfo_vmstat regex=meminfo match=schema
   ldmsctl> updtr_match_add name=meminfo_vmstat regex=vmstat match=schema
   ldmsctl>
   ldmsctl> updtr_add name=node01_procstat2 interval=2000000 offset=100000
   ldmsctl> updtr_match_add name=node01_procstat2 regex=node01/procstat2 match=inst
   ldmsctl> updtr_match_list
   Updater Name      Regex              Selector
   ----------------- ------------------ --------------
   meminfo_vmstat
                     vmstat             schema
                     meminfo            schema
   node01_procstat2
                     node01/procstat2   inst
   ldmsctl>

5) Example of log_status's report

::

   ldmsctl> log_status
   Name                 Levels                         Description
   -------------------- ------------------------------ ------------------------------
   ldmsd (default)      ERROR,CRITICAL                 The default log subsystem
   config               default                        Messages for the configuration infrastructure
   failover             default                        Messages for the failover infrastructure
   producer             default                        Messages for the producer infrastructure
   sampler              default                        Messages for the common sampler infrastructure
   store                default                        Messages for the common storage infrastructure
   stream               default                        Messages for the stream infrastructure
   updater              default                        Messages for the updater infrastructure
   xprt.ldms            default                        Messages for ldms
   xprt.zap             default                        Messages for Zap
   xprt.zap.sock        default                        Messages for zap_sock
   ----------------------------------------------------------------------------------
   The loggers with the Log Level as 'default' use the same log level as the
   default logger (ldmsd). When the default log level changes, their log levels
   change accordingly.

6) Change the log level of the config infrastructure to INFO and above

::

   ldmsctl> loglevel name=config level=INFO
   ldmsctl> log_status
   Name                 Log Level                      Description
   -------------------- ------------------------------ ------------------------------
   ldmsd (default)      ERROR,CRITICAL                 The default log subsystem
   config               INFO,WARNING,ERROR,CRITICAL    Messages for the configuration infrastructure
   failover             default                        Messages for the failover infrastructure
   producer             default                        Messages for the producer infrastructure
   sampler              default                        Messages for the common sampler infrastructure
   store                default                        Messages for the common storage infrastructure
   stream               default                        Messages for the stream infrastructure
   updater              default                        Messages for the updater infrastructure
   xprt.ldms            default                        Messages for ldms
   xprt.zap             default                        Messages for Zap
   xprt.zap.sock        default                        Messages for zap_sock
   ----------------------------------------------------------------------------------
   The loggers with the Log Level as 'default' use the same log level as the
   default logger (ldmsd). When the default log level changes, their log levels
   change accordingly.

7) Change the transport-related log levels to ERROR. That is, only the
ERROR messages will be reported.

::

   ldmsctl> loglevel regex=xprt.* level=ERROR,
   ldmsctl> log_status
   Name                 Log Level                      Description
   -------------------- ------------------------------ ------------------------------
   ldmsd (default)      ERROR,CRITICAL                 The default log subsystem
   config               INFO,WARNING,ERROR,CRITICAL    Messages for the configuration infrastructure
   failover             default                        Messages for the failover infrastructure
   producer             default                        Messages for the producer infrastructure
   sampler              default                        Messages for the common sampler infrastructure
   store                default                        Messages for the common storage infrastructure
   stream               default                        Messages for the stream infrastructure
   updater              default                        Messages for the updater infrastructure
   xprt.ldms            ERROR,                         Messages for ldms
   xprt.zap             ERROR,                         Messages for Zap
   xprt.zap.sock        ERROR,                         Messages for zap_sock
   ----------------------------------------------------------------------------------
   The loggers with the Log Level as 'default' use the same log level as the
   default logger (ldmsd). When the default log level changes, their log levels
   change accordingly.

8) Set the log levels of all infrastructures to the default level

::

   ldmsctl> loglevel regex=.* level=default
   ldmsctl> log_status
   Name                 Log Level                      Description
   -------------------- ------------------------------ ------------------------------
   ldmsd (default)      ERROR,CRITICAL                 The default log subsystem
   config               default                        Messages for the configuration infrastructure
   failover             default                        Messages for the failover infrastructure
   producer             default                        Messages for the producer infrastructure
   sampler              default                        Messages for the common sampler infrastructure
   store                default                        Messages for the common storage infrastructure
   stream               default                        Messages for the stream infrastructure
   updater              default                        Messages for the updater infrastructure
   xprt.ldms            default                        Messages for ldms
   xprt.zap             default                        Messages for Zap
   xprt.zap.sock        default                        Messages for zap_sock
   ----------------------------------------------------------------------------------
   The loggers with the Log Level as 'default' use the same log level as the
   default logger (ldmsd). When the default log level changes, their log levels
   change accordingly.

9) Get the information of a specific log infrastructure

::

   ldmsctl> log_status name=config
   Name                 Log Level                      Description
   -------------------- ------------------------------ ------------------------------
   ldmsd (default)      ERROR,CRITICAL                 The default log subsystem
   config               default                        Messages for the configuration infrastructure
   ----------------------------------------------------------------------------------
   The loggers with the Log Level as 'default' use the same log level as the
   default logger (ldmsd). When the default log level changes, their log levels
   change accordingly.
   ldmsctl>

SEE ALSO
========

:ref:`ldms_authentication(7) <ldms_authentication>`, :ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_ls(8) <ldms_ls>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`,
:ref:`ldms_quickstart(7) <ldms_quickstart>`
