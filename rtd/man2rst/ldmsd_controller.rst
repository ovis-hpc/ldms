================
ldmsd_controller
================

:Date: 19 Nov 2019

.. contents::
   :depth: 3
..

NAME
=================

ldmsd_controller - a python program to configure an ldms daemon.

SYNOPSIS
=====================

**ldmsd_controller** [OPTIONS]

ldmsd_controller> <cmd> [ <attr>=<value> ]

DESCRIPTION
========================

With LDMS (Lightweight Distributed Metric Service), the ldmsd can be
configured via the ldmsd_controller.

If ldms is built with --enable-readline, one can invoke the
ldmsd_controller from the command line and obtain an input interface
with feedback. In many instances, instances, however, it is prefered to
execute scripts and send the output commands to an ldmsd instead.

ENVIRONMENT
========================

Note: python2.6 with the additional installation of the argparse module
OR python2.7 (which has the argparse module) is required.

PYTHONPATH
   <path_to_ovis_install>/lib[64]/pythonX.Y/site-packages/

PATH
   <path_to_ovis_install>/bin

LDMSD_CONTROLLER OPTIONS
=====================================

**-h,--host** *HOST*
   Hostname of **ldmsd** to connect to

**-p,--port** *PORT*
   The port of **ldmsd** to connect to

**-x,--xprt** *XPRT*
   The transport type (**sock**, **rdma**, **ugni**).

**-a,--auth** *AUTH*
   The LDMS authentication plugin. Please see
   **ldms_authentication**\ (7) for more information.

**-A,--auth-arg** *NAME=VALUE*
   Options *NAME*\ =\ *VALUE* Passing the *NAME*\ =\ *VALUE* option to
   the LDMS Authentication plugin. This command line option can be given
   multiple times. Please see **ldms_authentication**\ (7) for more
   information, and consult the plugin manual page for plugin-specific
   options.

**--source** *SOURCE*
   |
   | Path to the config file

**--script** *SCRIPT*
   |
   | Execute the script and send the output commands to the connected
     ldmsd

**-?**
   Display help

**--help**
   Display help

REGULAR EXPRESSION
===============================

The regular expression specified in *regex=* option of the commands is a
POSIX Extended (modern) Regular Expression. In short, "\*+?{}|^$." are
special regular expression characters. Please see **regex(7)** for more
information.

PLUGIN COMMAND SYNTAX
==================================

Load a plugin
-------------

| **load** attr=<value>

   **name** *name*
      |
      | The plugin name

List the usage of the loaded plugins
------------------------------------

**usage**

unload a plugin
---------------

| **term** attr=<value>

   **name** *name*
      |
      | The plugin name

Send a configuration command to the specified plugin.
-----------------------------------------------------

**config** attr=<value>

   **name** *name*
      |
      | The plugin name

   **attr=value**
      |
      | Plugin specific attr=value tuples

      **Attributes specific for sampler plugins (Some sampler plugins
      may have additional** attributes)

      **producer** *producer*
         |
         | A unique name for the host providing the data

      **instance** *instance*
         |
         | The set instance name. The name must be unique among all
           metric sets in all LDMS daemons.

      **[component_id** *component_id*\ **]**
         |
         | A unique number for the comopnent being monitored. The
           default is zero.

      **[schema** *schema*\ **]**
         |
         | The name of the metric set schema.

      **[job_set** *job_set*\ **]**
         |
         | The set instance name of the set containing the job data. The
           default is 'job_info'.

      **[uid** *uid*\ **]**
         |
         | The user id of the set's owner. The default is the returned
           value of geteuid().

      **[gid** *gid*\ **]**
         |
         | The group id of the set's owner. The default is the returned
           value of getegid().

      **[perm** *perm*\ **]**
         |
         | The sampler plugin instance access permission. The default is
           0440.

Start a sampler plugin
----------------------

**start** attr=<value>

   **name** *name*
      |
      | The plugin name.

   **interval** *interval*
      |
      | The sample interval in microseconds.

   **[offset** *offset*\ **]**
      |
      | Offset (shift) from the sample mark in microseconds. Offset can
        be positive or negative with magnitude up to 1/2 the sample
        interval. If this offset is specified, including 0, collection
        will be synchronous; if the offset is not specified, collection
        will be asynchronous. Optional.

Stop a sampler plugin
---------------------

**stop** attr=<value>

   **name** *name*
      |
      | The plugin name.

AUTHENTICATION COMMAND SYNTAX
==========================================

Add an authentication domain
----------------------------

**auth_add** **name**\ =\ *NAME* **plugin**\ =\ *PLUGIN* [ ... *PLUGIN
ATTRIBUTES* ... ]

   **name**\ =\ *NAME*
      |
      | The name of the authentication domain. This is the name referred
        to by **listen** and **prdcr_add** commands.

   **plugin**\ =\ *none*\ \|\ *ovis*\ \|\ *munge*
      |
      | The LDMS Authentication Plugin for this domain.

   [ ... *PLUGIN ATTRIBUTES* ... ]
      |
      | Arbitrary plugin attributes. Please consult the manual of the
        authentication plugin for more information.

LISTEN COMMAND SYNTAX
==================================

Instruct ldmsd to listen to a port
----------------------------------

**listen** **port**\ =\ *PORT*
**xprt**\ =\ *sock*\ \|\ *rdma*\ \|\ *ugni*\ \|\ *fabric*
[**host**\ =\ *HOST*] [**auth**\ =\ *AUTH_REF*]

   **port**\ =\ *PORT*
      |
      | The port to listen to. Also, please be sure not to use ephemeral
        port (ports in the range of
        **/proc/sys/net/ip4/ip_local_port_range**).

   **xprt**\ =\ *sock*\ \|\ *rdma*\ \|\ *ugni*\ \|\ *fabric*
      |
      | The type of the transport.

   **host**\ =\ *HOST*
      |
      | An optional hostname or IP address to bind. If not given, listen
        to all addresses (0.0.0.0 or PORT).

   **auth**\ =\ *AUTH_REF*
      |
      | Instruct **ldmsd** to use *AUTH_REF* (a name reference to
        **auth** object created by **auth_add** command) to authenticate
        connections on this port. If not given, the port uses the
        default authentication method specified on the CLI options (see
        **ldmsd**\ (8) option **-a**).

PRODUCER COMMAND SYNTAX
====================================

Add a producer to the aggregator
--------------------------------

| **prdcr_add** attr=<value>

   **name** *name*
      |
      | The producer name. The producer name must be unique in an
        aggregator. It is independent of any attributes specified for
        the metric sets or hosts.

   **xprt** *xprt*
      |
      | The transport name [sock, rdma, ugni]

   **host** *host*
      |
      | The hostname of the host

   **type** *conn_type*
      |
      | The connection type [active, passive]

   **interval** *interval*
      |
      | The connection retry interval

   **[perm** *permission*\ **]**
      |
      | The permission to modify the producer in the future

   **[auth** *AUTH_REF*\ **]**
      |
      | Instruct **ldmsd** to use *AUTH_REF* (a name reference to
        **auth** object created by **auth_add** command) with the
        connections to this producer. If not given, the default
        authentication method specified on the CLI options (see
        **ldmsd**\ (8) option **-a**) is used.

Delete a producer from the aggregator
-------------------------------------

| The producer cannot be in use or running
| **prdcr_del** attr=<value>

   **name** *name*
      |
      | The producer name

Start a producer
----------------

**prdcr_start** attr=<value>

   **name** *name*
      |
      | The producer name

   **[interval** *interval*\ **]**
      |
      | The connection retry interval in microsec. If unspecified, the
        previously configured value will be used. Optional.

Start all producers matching a regular expression
-------------------------------------------------

**prdcr_start_regex** attr=<value>

   **regex** *regex*
      |
      | A regular expression

   **[interval** *interval*\ **]**
      |
      | The connection retry interval in microsec. If unspecified, the
        previously configured value will be used. Optional.

Stop a producer
---------------

**prdcr_stop** attr=<value>

   **name** *name*
      |
      | The producer name

Stop all producers matching a regular expression
------------------------------------------------

**prdcr_stop_regex** attr=<value>

   **regex** *regex*
      |
      | A regular expression

Query producer status
---------------------

**prdcr_status** attr=<value>

   **[name** *name*\ **]**
      |
      | The producer name. If none is given, the statuses of all
        producers are reported.

Subscribe for stream data from all matching producers
-----------------------------------------------------

**prdcr_subsribe**

   **regex** *regex*
      |
      | The regular expression matching producer name

   **stream** *stream*
      |
      | The stream name

UPDATER COMMAND SYNTAX
===================================

Add an updater process that will periodically sample producer metric sets
-------------------------------------------------------------------------

**updtr_add** attr=<value>

   **name** *name*
      |
      | The update policy name. The policy name should be unique. It is
        independent of any attributes specified for the metric sets or
        hosts.

   **interval** *interval*
      |
      | The update/collect interval

   **[offset** *offset*\ **]**
      |
      | Offset for synchronized aggregation. Optional.

   **[push** *onchange|true*\ **]**
      |
      | Push mode: 'onchange' and 'true'. 'onchange' means the Updater
        will get an update whenever the set source ends a transaction or
        pushes the update. 'true' means the Updater will receive an
        update only when the set source pushes the update. If \`push\`
        is used, \`auto_interval\` cannot be \`true\`.

   **[auto_interval** *true|false* **]**
      If true, the updater will schedule set updates according to the
      update hint. The sets with no hints will not be updated. If false,
      the updater will schedule the set updates according to the given
      interval and offset values. If not specified, the value is
      *false*.

   **[perm** *permission*\ **]**
      |
      | The permission to modify the updater in the future

Remove an updater from the configuration
----------------------------------------

**updtr_del** attr=<value>

   **name** *name*
      |
      | The update policy name

Add a match condition that specifies the sets to update.
--------------------------------------------------------

**updtr_match_add** attr=<value>

   **name** *name*
      |
      | The update policy name

   **regex** *regex*
      |
      | The regular expression

   **match** *match (inst|schema)*
      |
      | The value with which to compare; if match=inst, the expression
        will match the set's instance name, if match=schema, the
        expression will match the set's schema name.

Remove a match condition from the Updater.
------------------------------------------

**updtr_match_del** attr=<value>

   **name** *name*
      |
      | The update policy name

   **regex** *regex*
      |
      | The regular expression

   **match** *match (inst|schema)*
      |
      | The value with which to compare; if match=inst, the expression
        will match the set's instance name, if match=schema, the
        expression will match the set's schema name.

Add matching producers to an updater policy
-------------------------------------------

This is required before starting the updater.

**updtr_prdcr_add** attr=<value>

   **name** *name*
      |
      | The update policy name

   **regex** *regex*
      |
      | A regular expression matching zero or more producers

Remove matching producers to an updater policy
----------------------------------------------

**updtr_prdcr_del** attr=<value>

   **name** *name*
      |
      | The update policy name

   **regex** *regex*
      |
      | A regular expression matching zero or more producers

Start updaters.
---------------

**updtr_start** attr=<value>

   **name** *name*
      |
      | The update policy name

   **[interval** *interval*\ **]**
      |
      | The update interval in micro-seconds. If this is not specified,
        the previously configured value will be used. Optional.

   **[offset** *offset*\ **]**
      |
      | Offset for synchronized aggregation. Optional.

Stop an updater.
----------------

The Updater must be stopped in order to change it's configuration.

**updtr_stop** attr=<value>

   **name** *name*
      |
      | The update policy name

Query the updater status
------------------------

**updtr_status** attr=<value>

   **[name** *name*\ **]**
      |
      | The updater name. If none is given, the statuses of all updaters
        are reported.

STORE COMMAND SYNTAX
=================================

Create a Storage Policy and open/create the storage instance.
-------------------------------------------------------------

**strgp_add** attr=<value>

   **name** *name*
      |
      | The unique storage policy name.

   **plugin** *plugin*
      |
      | The name of the storage backend.

   **container** *container*
      |
      | The storage backend container name.

   **schema** *schema*
      |
      | The schema name of the metric set to store.

   **[perm** *permission*\ **]**
      |
      | The permission to modify the storage in the future

Remove a Storage Policy
-----------------------

| All updaters must be stopped in order for a storage policy to be
  deleted
| **strgp_del** attr=<value>

   **name** *name*
      |
      | The storage policy name

Add a regular expression used to identify the producers this storage policy will apply to.
------------------------------------------------------------------------------------------

| If no producers are added to the storage policy, the storage policy
  will apply on all producers.
| **strgp_prdcr_add** attr=<value>

   **name** *name*
      |
      | The storage policy name

   **regex** *name*
      |
      | A regular expression matching metric set producers.

Remove a regular expression from the producer match list
--------------------------------------------------------

**strgp_prdcr_del** attr=<value>

   | **name** *name*
   | The storage policy name

   **regex** *regex*
      |
      | The regex of the producer to remove.

Add the name of a metric to store
---------------------------------

**strgp_metric_add** attr=<value>

   | **name** *name*
   | The storage policy name

   **metric** *metric*
      |
      | The metric name. If the metric list is NULL, all metrics in the
        metric set will be stored.

Remove a metric from the set of stored metrics.
-----------------------------------------------

**strgp_metric_del** attr=<value>

   | **name** *name*
   | The storage policy name

   **metric** *metric*
      |
      | The metric to remove

Start a storage policy.
-----------------------

**strgp_start** attr=<value>

   | **name** *name*
   | The storage policy name

Stop a storage policy.
----------------------

A storage policy must be stopped in order to change its configuration.

**strgp_stop** attr=<value>

   | **name** *name*
   | The storage policy name

Query the storage policy status
-------------------------------

**strgp_status** attr=<value>

   **[name** *name*\ **]**
      |
      | The storage policy name. If none is given, the statuses of all
        storage policies are reported.

FAILOVER COMMAND SYNTAX
====================================

Please see **ldmsd_failover**\ (7).

SETGROUP COMMAND SYNTAX
====================================

Please see **ldmsd_setgroup**\ (7).

STREAM COMMAND SYNTAX
==================================

Publish data to the named stream
--------------------------------

**plublish** attr=<value>

   **name** *name*
      |
      | The stream name

   **data** *data*
      |
      | The data to publish

Subscribe to a stream
---------------------

**subscribe** attr=<value>

   **name** *name*
      |
      | The stream name

LDMS DAEMON COMMAND SYNTAX
=======================================

Changing the verbosity level of ldmsd
-------------------------------------

**loglevel** attr=<value>

   | **level** *level*
   | Verbosity levels [DEBUG, INFO, ERROR, CRITICAL, QUIET]

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

   [name *value*]
      The object name of which the status will be reported.

MISC COMMAND SYNTAX
================================

Display the list of available commands
--------------------------------------

|
| **help** <command>

   | [*command]*
   | If a command is given, the help of the command will be printed.
     Otherwise, only the available command names are printed.

Set the user data value for a metric in a metric set.
-----------------------------------------------------

|
| **udata** attr=<value>

   **set** *set*
      |
      | The sampler plugin name

   **metric** *metric*
      |
      | The metric name

   **udata** *udata*
      |
      | The desired user-data. This is a 64b unsigned integer.

Set the user data of multiple metrics using regular expression.
---------------------------------------------------------------

| The user data of the first matched metric is set to the base value.
  The base value is incremented by the given 'incr' value and then sets
  to the user data of the consecutive matched metric and so on.
| **udata_regex** attr=<value>

   **set** *set*
      |
      | The metric set name.

   **regex** *regex*
      |
      | A regular expression to match metric names to be set

   **base** *base*
      |
      | The base value of user data (uint64)

   **[incr** *incr*\ **]**
      |
      | Increment value (int). The default is 0. If incr is 0, the user
        data of all matched metrics are set to the base value. Optional.

Get the LDMS version the running LDMSD is based on.
---------------------------------------------------

**version**

Launch a subshell to do arbitrary commands
------------------------------------------

**!**\ shell-command

Comment (a skipped line)
------------------------

**#**\ comment-string

BUGS
=================

No known bugs.

EXAMPLES
=====================

Example of a script to add producers to updaters
------------------------------------------------

::

   > more add_prdcr.sh
   #!/bin/bash

   SOCKDIR=/XXX/run/ldmsd
   portbase=61100
   port1=`expr $portbase + 1`
   port2=`expr $portbase + 2`
   port3=`expr $portbase + 3`

   echo "prdcr_add name=localhost2 host=localhost type=active xprt=sock port=$port2 interval=20000000"
   echo "prdcr_start name=localhost2"
   echo "prdcr_add name=localhost1 host=localhost type=active xprt=sock port=$port1 interval=20000000"
   echo "prdcr_start name=localhost1"
   echo "updtr_add name=policy5_h1 interval=2000000 offset=0"
   echo "updtr_prdcr_add name=policy5_h1 regex=localhost1"
   echo "updtr_start name=policy5_h1"
   echo "updtr_add name=policy5_h2 interval=5000000 offset=0"
   echo "updtr_prdcr_add name=policy5_h2 regex=localhost2"
   echo "updtr_start name=policy5_h2"

Example of a script to add and start stores
-------------------------------------------

::

   > more add_store.sh
   #!/bin/bash

   # whole path must exist
   STORE_PATH=/XXX/ldmstest/store
   mkdir -p $STORE_PATH
   sleep 1

   # CSV
   echo "load name=store_csv"
   echo "config name=store_csv path=$STORE_PATH action=init altheader=0 rollover=30 rolltype=1"
   echo "config name=store_csv action=custom container=csv schema=cray_aries_r altheader=1  userdata=0"

   echo "strgp_add name=policy_mem plugin=store_csv container=csv schema=meminfo"
   echo "strgp_start name=policy_mem"

   #echo "strgp_add name=csv_memfoo_policy plugin=store_csv container=meminfo schema=meminfo_foo"
   #echo "strgp_prdcr_add name=csv_memfoo_policy regex=localhost*"
   #echo "strgp_start name=csv_memfoo_policy"

Example to start an ldmsd and use ldmsd_controller to call a script
-------------------------------------------------------------------

::

   > ldmsd -x sock:11111 -l log.txt
   > ldmsd_controller --host localhost --port 11111 --xprt sock --script myscript.sh

SEE ALSO
=====================

ldmsd(8), ldmsctl(8), ldms_quickstart(7), ldmsd_failover(7),
ldmsd_setgroup(7)
