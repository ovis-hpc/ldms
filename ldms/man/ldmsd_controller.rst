.. _ldmsd_controller:

================
ldmsd_controller
================


----------------------------------------------
A python program to configure an ldms daemon.
----------------------------------------------

:Date: 19 Nov 2019
:Manual section: 8
:Manual group: LDMSD

SYNOPSIS
========

**ldmsd_controller** [OPTIONS]

ldmsd_controller> <cmd> [ <attr>=<value> ]

DESCRIPTION
===========

Configure and query the status of a running LDMSD daemon.


ENVIRONMENT
===========

PYTHONPATH
   <path_to_ovis_install>/lib[64]/pythonX.Y/site-packages/

PATH
   <path_to_ovis_install>/bin

LDMSD_CONTROLLER OPTIONS
========================

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
==================

The regular expression specified in *regex=* option of the commands is a
POSIX Extended (modern) Regular Expression. In short, "\*+?{}|^$." are
special regular expression characters. Please see **regex(7)** for more
information.

PLUGIN COMMAND SYNTAX
=====================

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

   ..

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

**start** **name=**\ *NAME* **interval=**\ *INTERVAL*
[**offset=\ OFFSET**] [**exclusive_thread=\ 0|1**]

   **name** *name*
      |
      | The plugin name.

   **interval** *interval*
      |
      | The sample interval, which is a float followed by a unit string.
        If no unit string is given, the default unit is microseconds. A
        unit string is one of the followings: us -- microseconds ms --
        milliseconds s -- seconds m -- minutes h -- hours d -- days

   **[offset** *offset*\ **]**
      |
      | Offset (shift) from the sample mark in the same format as
        intervals. Offset can be positive or negative with magnitude up
        to 1/2 the sample interval. The default offset is 0. Collection
        is always synchronous.

   [**exclusive_thread=\ 0|1**]
      |
      | If exclusive_thread is 0, the sampler shares a thread with other
        sampler. If exclusive_thread is 1, the sampler has an exclusive
        thread to work on. The default is 0 (i.e. share sampling
        threads).

Stop a sampler plugin
---------------------

**stop** attr=<value>

   **name** *name*
      |
      | The plugin name.

AUTHENTICATION COMMAND SYNTAX
=============================

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
=====================

Instruct ldmsd to listen to a port
----------------------------------

**listen** **port**\ =\ *PORT*
**xprt**\ =\ *sock*\ \|\ *rdma*\ \|\ *ugni*\ \|\ *fabric*
[**host**\ =\ *HOST*] [**auth**\ =\ *AUTH_REF*] [**quota**\ =\ *QUOTA*]
[**rx_rate**\ =\ *RX_RATE*]

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

   **[quota** *BYTES*\ **]**
      |
      | The LDMS daemon we are managing uses receive quota (measured in
        bytes) to control the amount of data received on the connections
        established by accepting requests to this listening endpoint.
        The quota value functions similarly to the quota **attribute in
        the** prdcr_add **command,** influencing the amount of data
        producers created by Sampler Advertisement can receive. The
        default value is determined by the command-line --quota option
        used when starting the LDMS daemon (ldmsd). If neither the
        --quota **option nor the** quota **attribute is specified, there
        is** no limit on receive quota.

   **[rx_rate** *BYTES_PER_SEC*\ **]**
      |
      | The receive rate limit (in bytes/second) controls the rate of
        data received on the connections established by accepting
        requests to this listening endpoint. Unlike quota\ **, which
        controls the total amount of received data, the receive** rate
        limit focuses on the data flow per second. If not specified, it
        is unlimited.

PRODUCER COMMAND SYNTAX
=======================

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

   **reconnect** *interval*
      |
      | The connection retry interval, which is a float followed by a
        unit string. If no unit string is given, the default unit is
        microseconds. A unit string is one of the followings: us --
        microseconds ms -- milliseconds s -- seconds m -- minutes h --
        hours d -- days

   **interval** *interval*
      |
      | It is being deprecated. Please use 'reconnect'.

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

   **[rail** *NUM*\ **]**
      |
      | The number of rail endpooints for the prdcr (default: 1).

   **[quota** *BYTES*\ **]**
      |
      | The send quota our ldmsd (the one we are controlling) advertises
        to the prdcr (default: value from ldmsd --quota option). This
        limits how much outstanding data our ldmsd holds for the prdcr.

   **[rx_rate** *BYTES_PER_SEC*\ **]**
      |
      | The recv rate (bytes/sec) limit for this connection. The default
        is -1 (unlimited).

   **[cache_ip** *cache_ip*\ **]**
      |
      | Controls how **ldmsd** handles hostname resolution for producer
        IP addresses. When set to **true** (default), **ldmsd** resolves
        the hostname once during **prdcr_add** and caches the result. If
        the initial resolution fails and the producer is started (via
        **prdcr_start or prdcr_start_regex**), **ldmsd** will retry
        resolution at connection time and each resonnection attempt
        until successful. When set to **false**, **ldmsd** performs
        hostname resolution at **prdcr_add** time and repeats the
        resolution at every connection and reconnection attempt if the
        producer is started.

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

   **[reconnect** *interval*\ **]**
      |
      | The connection retry interval, which is a float followed by a
        unit string. If no unit string is given, the default unit is
        microseconds. A unit string is one of the followings: us --
        microseconds ms -- milliseconds s -- seconds m -- minutes h --
        hours d -- days If unspecified, the previously configured value
        will be used. Optional.

   **[interval** *interval*\ **]**
      |
      | It is being deprecated. Please use 'reconnect'.

Start all producers matching a regular expression
-------------------------------------------------

**prdcr_start_regex** attr=<value>

   **regex** *regex*
      |
      | A regular expression

   **[reconnect** *interval*\ **]**
      |
      | The connection retry interval, which is a float followed by a
        unit stirng. If no unit string is given, the default unit is
        microseconds. A unit string is one of the followings: us --
        microseconds ms -- milliseconds s -- seconds m -- minutes h --
        hours d -- days If unspecified, the previously configured value
        will be used. Optional.

   **[interval** *interval*\ **]**
      |
      | It is being deprecated. Please use 'reconnect'.

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


Disable stream communication
----------------------------

**stream_disable**

Disable the subscription and publication of data on a stream in this daemon.
Once stream communication is disabled, all stream requests will result in an
error of ENOSYS. Stream communication cannot be re-enabled without restarting
the daemon.


Disable LDMS Message Service
----------------------------

**msg_disable**

Disable the LDMS Message Service subscription and publication in this daemon.
Once this is disabled, all LDMS Messages will be dropped. The LDMS Message
Service cannot be re-enabled without restarting the daemon.


Subscribe for stream and/or message data from all matching producers
--------------------------------------------------------------------

**prdcr_subsribe** attr=<value>

   **regex** *regex*
      |
      | The regular expression matching producer name

   **stream** *stream*
      |
      | The stream name

   **message_channel** *NAME_REGEX*
      |
      | The message channel name or regular expression of message channels


UPDATER COMMAND SYNTAX
======================

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
      | The update/collect interval, which is a float followed by a unit
        string. If no unit string is given, the default unit is
        microseconds. A unit string is one of the followings: us --
        microseconds ms -- milliseconds s -- seconds m -- minutes h --
        hours d -- days

   **[offset** *offset*\ **]**
      |
      | Specifies a time shift for synchronized data updates from the server.
        This offset determines when update requests are sent relative to the
        interval boundaries. The offset value delays the update request. This can
        be used to avoid potential conflicts when the server might be collecting
        new samples or getting updates. The offset uses the same format as
        intervals. Default is 0 (no shift).

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
      | The update interval, which is a float followed by a unit string.
        If no unit string is given, the default unit is microseconds. A
        unit string is one of the followings: us -- microseconds ms --
        milliseconds s -- seconds m -- minutes h -- hours d -- days If
        this is not specified, the previously configured value will be
        used. Optional.

   **[offset** *offset*\ **]**
      |
      | Specifies a time shift for synchronized data updates from the server.
        This offset determines when update requests are sent relative to the
        interval boundaries. The offset value delays the update request. This can
        be used to avoid potential conflicts when the server might be collecting
        new samples or getting updates. The offset uses the same format as
        intervals. Default is 0 (no shift).

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

   **[reset** *value*\ **]**
      |
      | If true, reset the updater's counters after returning the
        values. The default is false.

Query the updaters' list of regular expressions to match set names or set schemas
---------------------------------------------------------------------------------

**updtr_match_list** attr=<value>

   **[name** *name*\ **]**
      |
      | The Updater name. If none is given, all updaters' regular
        expression lists will be returned.

STORE COMMAND SYNTAX
====================

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

   **[schema** *schema*\ **]**
      |
      | The schema name of the metric set to store. If 'schema' is
        given, 'regex' is ignored. Either 'schema' or 'regex' must be
        given.

   **[regex** *regex*\ **]**
      |
      | a regular expression matching set schemas. It must be used with
        decomposition. Either 'schema' or 'regex' must be given.

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
=======================

Please see **ldmsd_failover**\ (7).

SETGROUP COMMAND SYNTAX
=======================

Please see **ldmsd_setgroup**\ (7).

STREAM COMMAND SYNTAX
=====================

Publish data to the named stream
--------------------------------

**plublish** attr=<value>

   **name** *name*
      |
      | The stream name

   **data** *data*
      |
      | The data to publish

Subscribe to a stream on matching producers
-------------------------------------------

**prdcr_subscribe** attr=<value>

   **regex** *PRDCR_REGEX*
      |
      | A regular expression matching PRODUCER names

   **stream** *STREAM_NAME_OR_REGEX*
      |
      | The stream name or regular expression

   **[rx_rate** *BYTES_PER_SECOND*\ **]**
      |
      | The recv rate (bytes/sec) limit for the matching streams. The
        default is -1 (unlimited).

LDMS DAEMON COMMAND SYNTAX
==========================

Changing the log levels of LDMSD infrastructures
------------------------------------------------

**loglevel** attr=<value> (deprecated)

**log_level** attr=<value>

**level** *string*
   |
   | A string specifying the log levels to be enabled

   The valid string are "default", "quiet", and a comma-separated list
   of DEBUG, INFO, WARN, ERROR, and CRITICAL. It is case insensitive.
   "default" means to set the log level to the defaul log level. "quiet"
   means disable the log messages. We note that "<level>," and "<level>"
   give different results. "<level>" -- a single level name -- sets the
   log level to the given level and all the higher severity levels. In
   contrast, "<level>," -- a level name followed by a comma -- sets the
   log level to only the given level.

**[name** *name*\ **]**
   |
   | A logger name

**[regex** *regex*\ **]**
   |
   | A regular expression matching logger names. If neither 'name' or
     'regex' is given, the command sets the default log level to the
     given level. For example, 'regex=xprt.\*' will change the
     transport-related log levels. Use log_status to query the available
     log infrastructures.

Query LDMSD's log information
-----------------------------

**log_status** attr=<value>

   | **[name** *value*\ **]**
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

   [name *value*]
      The object name of which the status will be reported.

SET COMMAND SYNTAX
==================

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

   **[uid** *uid*\ **]**
      |
      | An existing user name string or a UID. Optional

   **[gid** *gid*\ **]**
      |
      | A GID. Optional

   **[perm** *perm*\ **]**
      |
      | An octal number representing the permission bits. Optional

STATISTICS COMMAND SYNTAX
=========================

Display the IO thread statistics
--------------------------------

|
| **thread_stats** attr=<value>

   **[reset** *true|false*\ **]**
      |
      | If true, reset the thread statistics after returning the values.
        The default is false.

Query the IO thread, worker thread, and sampling thread utilization statistics. The statistics include both overall utilization (since start/reset) and recent utilization over a time window.

The report is divided into three section:

* LDMSD Worker Thread Statstics - LDMSD event processing threads
* Exclusive Worker Thread Statistics - Sampling execution threads
* IO Thread Statistics - Network I/O threads

The column descriptions for worker threads and xthreads are:

* Thread ID - Linux thread ID (from gettid())
* Linux Thread ID - pthread ID as hex string
* Name - Thread name
* Utilization - Thread utilization ratio (0.0-100.0%) over a recent time window.
* Trailing (s) - Duration of the most recent time period used for utilization calculation (default 3 seconds)
* Event Counts - Number of events processed

The column descriptions for IO threads are:

* Thread ID - Linux thread ID (from gettid())
* Linux Thread ID - pthread ID as hex string
* Name - Thread name
* Utilization - Thread utilization ratio (0.0-100.0%) over a recent time window
* Trailing (s) - Duration of the most recent time period used for utilization calculation (default 3 seconds)
* Send Queue Size - Number of pending send operations
* Num of EPs - Number of endpoints handled by this thread

Additionally, detailed IO Thread usage information is provided showing:

* The percentage of time each thread spends in different LDMS operations
* The absolute time (in microseconds) spent in each operation

Notes:
* A utilization value of '-' indicates insufficient data points for calculation
* The reported utilization is typically calculated over a 3-second window by default
* Idle and active percentages represent recent activity within the time window

Display the transport operation statistics
------------------------------------------

|
| **xprt_stats** attr=<value>

   **[reset** *true|false*\ **]**
      |
      | If true, reset the statistics after returning the values. The
        default is false.

Display the statistics of updaters' update time per set
-------------------------------------------------------

|
| **update_time_stats** attr=<value>

   **[reset** *true|false*\ **]**
      |
      | If true, reset the update time statistics after returning the
        values. The default is false.

   **[name** *name*\ **]**
      |
      | An updater name. Only the statistics of the given updater will
        be reported and reset if reset is true.

Display the statistics of storage policy's store time per set
-------------------------------------------------------------

|
| **store_time_stats** attr=<value>

   **[reset** *true|false*\ **]**
      |
      | If true, reset the store time statistics after returning the
        values. The default is false.

   **[name** *name*\ **]**
      |
      | A storage policy name. Only the statistics of the given storage
        policy will be reported and reset if reset is true.

QGROUP COMMAND SYNTAX
=====================

Get qgroup information
----------------------

|
| **qgroup_info**

   - This command has no attributes. -

Set qgroup parameters
---------------------

|
| **qgroup_config** attr=<value>

   **[quota** *BYTES*\ **]**
      The amount of our quota (bytes). The *BYTES* can be expressed with
      quantifiers, e.g. "1k" for 1024 bytes. The supported quantifiers
      are "b" (bytes), "k" (kilobytes), "m" (megabytes), "g" (gigabytes)
      and "t" (terabytes).

   **[ask_interval** *TIME*\ **]**
      The time interval to ask the members when our quota is low. The
      *TIME* can be expressed with units, e.g. "1s", but will be treated
      as microseconds if no units is specified. The supported units are
      "us" (microseconds), "ms" (milliseconds), "s" (seconds), "m"
      (minutes), "h" (hours), and "d" (days).

   **[ask_amount** *BYTES*\ **]**
      The amount of quota to ask from our members. The *BYTES* can be
      expressed with quantifiers, e.g. "1k" for 1024 bytes. The
      supported quantifiers are "b" (bytes), "k" (kilobytes), "m"
      (megabytes), "g" (gigabytes) and "t" (terabytes).

   **[ask_mark** *BYTES*\ **]**
      The amount of quota to determine as 'low', to start asking quota
      from other members. The *BYTES* can be expressed with quantifiers,
      e.g. "1k" for 1024 bytes. The supported quantifiers are "b"
      (bytes), "k" (kilobytes), "m" (megabytes), "g" (gigabytes) and "t"
      (terabytes).

   **[reset_interval** *TIME*\ **]**
      The time interval to reset our quota to its original value. The
      *TIME* can be expressed with units, e.g. "1s", but will be treated
      as microseconds if no units is specified. The supported units are
      "us" (microseconds), "ms" (milliseconds), "s" (seconds), "m"
      (minutes), "h" (hours), and "d" (days).

Add a member into our qgroup
----------------------------

|
| **qgroup_member_add** attr=<value>

   **xprt** *XPRT*
      The transport type of the connection (e.g. "sock").

   **host** *HOST*
      The hostname or IP address of the member.

   **[port** *PORT*\ **]**
      The port of the member (default: 411).

   **[auth** *AUTH_REF*\ **]**
      The reference to the authentication domain (the **name** in
      **auth_add** command) to be used in this connection If not
      specified, the default authentication domain of the daemon is
      used.

Remove a member from the qgroup
-------------------------------

|
| **qgroup_member_del** attr=<value>

   **host** *HOST*
      The hostname or IP address of the member.

   **[port** *PORT*\ **]**
      The port of the member (default: 411).

Start the qgroup service
------------------------

|
| **qgroup_start**

   - This command has no attributes. -

Stop the qgroup service
-----------------------

|
| **qgroup_stop**

   - This command has no attributes. -

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

Launch a subshell to do arbitrary commands
------------------------------------------

**!**\ shell-command

Comment (a skipped line)
------------------------

**#**\ comment-string

BUGS
====

No known bugs.

EXAMPLES
========

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

   echo "prdcr_add name=localhost2 host=localhost type=active xprt=sock port=$port2 reconnect=20000000"
   echo "prdcr_start name=localhost2"
   echo "prdcr_add name=localhost1 host=localhost type=active xprt=sock port=$port1 reconnect=20000000"
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

Example of updtr_match_list's report
------------------------------------

::

   ldmsd_controller> updtr_add name=meminfo_vmstat interval=1000000 offset=100000
   ldmsd_controller> updtr_match_add name=meminfo_vmstat regex=meminfo match=schema
   ldmsd_controller> updtr_match_add name=meminfo_vmstat regex=vmstat match=schema
   ldmsd_controller>
   ldmsd_controller> updtr_add name=node01_procstat2 interval=2000000 offset=100000
   ldmsd_controller> updtr_match_add name=node01_procstat2 regex=node01/procstat2 match=inst
   ldmsd_controller> updtr_match_list
   Updater Name      Regex              Selector
   ----------------- ------------------ --------------
   meminfo_vmstat
                     vmstat             schema
                     meminfo            schema
   node01_procstat2
                     node01/procstat2   inst
   ldmsd_controller>

Example of log_status's report
------------------------------

::

   ldmsd_controller> log_status
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

   # Change the log level of the config infrastructure to INFO and above
   ldmsd_controller> loglevel name=config level=INFO
   ldmsd_controller> log_status
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

   # Change the transport-related log levels to ERROR. That is, only the ERROR messages will be reported.
   ldmsd_controller> loglevel regex=xprt.* level=ERROR,
   ldmsd_controller> log_status
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

   # Set the log levels of all infrastructures to the default level
   ldmsd_controller> loglevel regex=.* level=default
   ldmsd_controller> log_status
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

   # Get the information of a specific log infrastructure
   ldmsd_controller> log_status name=config
   Name                 Log Level                      Description
   -------------------- ------------------------------ ------------------------------
   ldmsd (default)      ERROR,CRITICAL                 The default log subsystem
   config               default                        Messages for the configuration infrastructure
   ----------------------------------------------------------------------------------
   The loggers with the Log Level as 'default' use the same log level as the
   default logger (ldmsd). When the default log level changes, their log levels
   change accordingly.
   ldmsd_controller>

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldmsctl(8) <ldmsctl>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_failover(7) <ldmsd_failover>`, :ref:`ldmsd_setgroup(7) <ldmsd_setgroup>`
