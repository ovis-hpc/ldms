.. _ldmsd_yaml_parser:

=================
ldmsd_yaml_parser
=================

---------------------------------------------------------------------------------
A python program to parse a YAML configuration file into a v4 LDMS configuration.
---------------------------------------------------------------------------------

:Date: 20 Nov 2024 "ovis-4.4.5"
:Manual section: 8
:Manual group: LDMSD


SYNOPSIS
========

**ldmsd [OPTIONS]**

**ldmsd_yaml_parser** [OPTIONS]

DESCRIPTION
===========

A single ldmsd can be configured using a YAML configuration file. This
can be done by running ldmsd directly, or generating a configuration
parser with the ldmsd_yaml_parser

LDMSD YAML OPTIONS
==================

**-y,**\ *CONFIG_PATH*
   The path to the YAML configuration file.

**-n,**\ *NAME*
   The name of the LDMS daemon.

LDMSD_YAML_PARSER COMMAND SYNTAX
================================

**--ldms-config,**\ *CONFIG_PATH*
   The path to the YAML configuration file.

**--daemon-name,**\ *NAME*
   The name of the LDMS daemon to configure.

YAML CONFIGURATION SYNTAX
=========================

| The LDMS YAML configuration syntax allows defining a single LMDSDs
  configuration up to an entire LDMS cluster configuration in a single
  file.
| LDMS YAML cluster configuration is organized into “groups” of
  dictionaries with relevant configurations for each group. The top
  level dictionaries are “daemons”, “aggregators”, “samplers”,
  “plugins”, and “stores”.
| Time intervals are defined as a unit string, or an integer in
  microseconds.
| A unit string is one of the followings: us -- microseconds ms --
  milliseconds s -- seconds m -- minutes h -- hours d -- days

| Permissions are defined in a unix-like permissions format, with a dash
  indicating a lack of permission. Executable permisson is not supported
  by "perm" and should always be "-". e.g. "rw-r-----"
| A string of an octal number is also accepted. e.g. "0640".
| "r" represents read permissions.
| "w" or "+" represent write permissions.

| Several of the keys in this configuration syntax utilize the hostlist
  format.
| Please see the hostlist documentation for more information regarding
  this here:
| https://readthedocs.org/projects/py-hostlist/downloads/pdf/latest/

NOTE: When using a configuration string, rather than a dictionary, to
configure a plugin, the string is not parsed by the YAML parser, and the
exact string will be passed to the LDMSD as is. As such, the only
accepted permission format when using a configuration string, rather
than a dictionary, is an octal number, e.g. "0777". If an octal number
is entered into the configuration as an integer, the parser will
interpret the number incorrectly, and the permissions will not be set as
expected.

daemons
=======

List of dictionaries describing LDMS daemons that are part of the
cluster and their endpoints that share transport configurations. The
primary keys are "names", "hosts", "endpoints", and "environment". Any
keys provided that do not match this string values are assumed to be
either CLI commands, or overarching default configuration commands for a
daemon.

names
-----

Regex of a group of LDMS daemon attributes and endpoints that share
transport configuration. These strings are referenced in the samplers
and aggregators sections. Hostlist format.

hosts
-----

Hostnames on which the LDMS daemons will operate. Must be specified
here. May be overridden by a "hosts" definition in the endpoints
section. Hostlist format.

endpoints
---------

List of dictionaries of endpoint configurations for these daemons.

   **names**
      |
      | Unique names by which to identify the transport endpoints.
        Hostlist format. e.g. "sampler-[1-4]"

   **[hosts]**
      |
      | Hosts for an endpoint may be optionally specified if a certain
        interface is desired other than the stated hostname in the top
        level daemons section. Hostlist format. e.g. "node-[1-4]" for 4
        LDMSDs

   **[bind_all]**
      |
      | The bind_all boolean may be specified to configure the LDMS
        daemon to listen on all available interfaces when using this
        endpoint. The prdcr_add command will use either the top level
        host definition, or, if defined, the host specified in the
        endpoints. bind_all accepts <True/False>

   **ports**
      |
      | Daemon endpoint ports on which to communicate. Hostlist or
        integer format. e.g. "[10001-10004]"
      | If there are two endpoints, and two ports, one will be assigned
        to each endpoint.

   **xprt**
      |
      | The communication transport for the endpoint. <sock/rdma/ugni>
        are supported.

   **auth**
      |
      | Dictionary of a authentication domains plugin configuration.

      **name**
         |
         | Unique authentication domain name for this authentication
           configuration.

      **plugin**
         |
         | Name of the authentication domain plugin <ovis/munge>

      **conf**
         |
         | Dictionary of plugin specific configuration options for this
           authentication domain.

aggregators
===========

| List of dictionaries defining aggregator configurations, their “peers”
  i.e. “producers”, that they will be aggregating data from, and the
  endpoints and daemons on which to communicate.
| The daemons reference daemon configuration definitions defined in the
  "daemons" dictionary.
| The stores reference storage policy names defined in the "stores" top
  level dictionary.
| The "plugins" key reference plugin instance names defined in the
  "plugins" top level dictionary.
| The primary keys are "names", "hosts", "endpoints", and "environment"
| Any keys provided that do not match one of these string values are
  assumed to be either CLI commands, or overarching default
  configuration commands for a daemon.

names
-----

String regex in hostlist format of a group of LDMS daemon attributes and
endpoints that share transport configuration in hostlist format. These
strings are referenced in the sampler and aggregator configurations.

hosts
-----

String regex in hostlist format of hostnames on which the LDMS daemon
will operate. Must expand to an equal length as the daemon names, or be
evenly divisble. e.g. 2 hostnames for 4 daemons.

environment
-----------

A dictionary of environment variables for a LDMSD and their values. Keys
are the environment variable name.

[subscribe]
-----------

List of dictionaries of streams to subscribe producers to.

**stream**
   |
   | The name of the stream.

**regex**
   |
   | Regular expression matching producers to subscribe to the stream.

peers
-----

List of dictionaries containing producer configurations. This is an
alternative method to configuring producers than using prdcr_listen.
Producers defined in the "peers" section are as evenly distributed as
possible amongst the "aggregators" defined in the parent directory. e.g.
If there are 2 aggregators, and 4 producers, each aggregator will be
assigned 2 producers in the configuration.

**daemons**
   |
   | String of daemon names in hostlist format that references daemon
     names defined in the top level daemons section.

**endpoints**
   |
   | String of endpoints in hostlist format that references endpoints
     defined in the top level daemons section.

**reconnect**
   |
   | Interval by which the aggregator will attempt to reconnect to a
     disconnected producer. Unit string format.

**type**
   |
   | Producer type. Either active or passive. passive is being
     deprecated.

**[rail]**
   |
   | The number of rail endpoints for the producer (default is 1).

**[quota]**
   |
   | The receive quota the ldmsd being configured advertises to the
     producer (default value from ldmsd --quota). This limits how much
     outstanding data the ldmsd holds for a producer.

**[rx_rate]**
   |
   | The receieve rate limit in bytes/second for this connection. The
     default is -1 (unlimited).

**[perm]**
   |
   | The permissions to modify the producer in the future. String of
     octal number or unix-like permissions format. e.g. "rw-r--r--"

**[cache_ip]**
   |
   | True/False boolean. True will cache the IP address after the first
     successful resolution (default). False will resolve the hostname at
     prdcr_add and at every connection attempt.

**updaters**
   |
   | List of dictionaries of updater policy configurations.

   **mode**
      |
      | Updater mode. Accepted strings are <pull|push|onchange|auto>
        "onchange" means the Updater will get an update whenever the set
        source ends a transaction or pushes the update. "push" means the
        Updater will receive an update only when the set source pushes
        the update.

   The sets with no hints will not be updated. "pull" means the updater
   will schedule the set updates according to the given interval

   **interval**
      |
      | The update/collect interval at which to update the producer.
        Unit string format.

   **[offset]**
      |
      | Offset for synchronized aggregation. Optional. Unit string
        format.

   **[perm]**
      |
      | The permissions that allow modification of an updater in the
        future. String of octal number or unix-like permissions format.
        e.g. "rw-r--r--"

   **[producers]**
      |
      | Optional regular expression matching zero or more producers to
        add to this updater. If omitted, all producers in the parent
        dictionary will be added to this updater.

   **[sets]**
      |
      | Optional list of dictionaries containing regular expressions
        that match either a schema instance name or a metric set
        instance name. If omitted, all sets belonging to producers added
        to this updater will be added to this updater.

      **regex**
         |
         | Regular expression to either match instance names or schemas
           to apply this updater policy too.

      **field**
         |
         | Field to use when matching the regular expression.
           <schema|inst>. schema matches a schema instance name, and
           inst matches a metric set instance name.

prdcr_listen
------------

An optional alternative configuration for how your aggregators will add
producers that is used in conjunction with the top level samplers
"advertise" key. When utilizing producer listen, the aggregator will
listen until a connection is established by a sampler. When using this
configuration, the aggregators configuration information is provided in
the samplers section under the key "advertisers".

   **name**
      |
      | String name for the producer listener - does not need to be
        unique across aggregators.

   **[regex]**
      |
      | A regular expression matching hostnames in advertisements to add
        as a producer.

   **[ip]**
      |
      | An IP masks to filter advertisements using the source IP.

   **[disable_start]**
      |
      | Informs the ldmsd not to start producers.

   **updaters**
      |
      | List of dictionaries containing updater policies for the
        producers that ultimately connect to the producer listener.

      **mode**
         |
         | Updater mode. Accepted strings are <pull|push|onchange|auto>
           "onchange" means the Updater will get an update whenever the
           set source ends a transaction or pushes the update. "push"
           means the Updater will receive an update only when the set
           source pushes the update.

      The sets with no hints will not be updated. "pull" means the
      updater will schedule the set updates according to the given
      interval and offset values.

      **interval**
         |
         | The update/collect interval at which to update the producer.
           Unit string format.

      **[offset]**
         |
         | Offset for synchronized aggregation. Optional. Unit string
           format.

      **[perm]**
         |
         | The permissions to modify the producer in the future. String
           of octal number or unix-like permissions format. e.g.
           "rw-r--r--"

      **[producers]**
         |
         | Optional regular expression matching zero or more producers
           to add to this updater. If omitted, all producers in the
           parent dictionary will be added to this updater.

      **[sets]**
         |
         | List of dictionaries containing regular expressions that
           match either a schema instance name or a metric set instance
           name.

         **regex**
            |
            | Regular expression to either match instance names or
              schemas to apply this updater policy too.

         **field**
            |
            | Field to use when matching the regular expression.
              <schema|inst>. schema matches a schema instance name, and
              inst matches a metric set instance name.

samplers
========

List of dictionaries defining sampler configurations and the LDMS
daemons to apply them to. The daemons reference daemons defined in the
top level "daemons" dictionary. Plugins reference instance names of
plugins defined in the "plugins" top level dictionary.

**daemons**
   |
   | String of daemon names in hostlist format that references daemon
     names defined in the top level daemons section.

**plugins**
   |
   | List of strings of plugin instance names to load that reference
     plugin instance names defined in the top level plugins section.
     String format.

**[advertise]**
   |
   | Alternative configuration to the aggregators "peers" where the
     sampler initiates a connection to the aggregator. The producer
     listener for an advertiser is defined in the top level aggregators
     section.

   **names**
      |
      | String of daemon names in hostlist format to advertise the
        samplers as.

   **hosts**
      |
      | String of daemon hosts in hostlist format, that references
        daemon names defined in the top level "daemons" section, for the
        samplers to advertise to

   **port**
      |
      | String of port(s) in hostlist format of the aggregator daemons
        that the sampler daemons will attempt to connect to.

   **reconnect**
      |
      | The interval at which the sampler will attempt to reconnect to a
        disconnected advertiser. Float followed by a unit string.

   **[rail]**
      |
      | The number of rail endpoints for the producer (default is 1).

   **[quota]**
      |
      | The send quota this ldmsd advertises to the producer. This
        limits how much outstanding data this ldmsd holds for the
        aggregator. This ldmsd will drop messages when it does not have
        enough send quota.

   **[rx_rate]**
      |
      | The receieve rate limit in bytes/second for this connection. The
        default is -1 (unlimited).

   **[perm]**
      |
      | The permissions in order to modify the advertiser in the future.
        String of octal number or unix-like permissions format. e.g.
        "rw-r--r--"

   **[auth]**
      |
      | Dictionary of a authentication domains plugin configuration.

      **name**
         |
         | Unique authentication domain name for this authentication
           configuration.

      **plugin**
         |
         | Name of the authentication domain plugin <ovis/munge>

      **[conf]**
         |
         | Optional dictionary of plugin specific configuration options
           for this authentication domain.

         **["path"**:**/opt/ovis/secret.conf**]

stores
======

Dictionary of storage policies and their configuration information with
each key being a storage policy name.

**container**
   |
   | File path of the database container.

**[schema]**
   |
   | Name of the metric set schema. This is a required argument unless
     decomposition is specified. May not be used in conjunction with
     "regex".

**plugin**
   |
   | Name of a storage plugin that matches a key of a plugin defined in
     the top level plugins section.

**[perm]**
   |
   | The permissions of who can modify the storage plugin in the future.
     String of octal number or unix-like permissions format. e.g.
     "rw-r--r--"

**[decomposition]**
   |
   | Path to a decomposition configuration file.

**[regex]**
   |
   | A regular expression matching the schema set names to apply the
     decomposition file to. May not be used in conjunction with
     "schema".

**[flush]**
   |
   | Optional interval of time that directs flushing of the store to the
     database.

plugins
=======

Dictionary of plugins and their configuration information with each key
being a plugin instance name.

   **name**
      |
      | The name of a plugin to load. e.g. meminfo

   **interval**
      |
      | The interval at which to sample data.

   **[offset]**
      |
      | Offset (shift) from the sample mark in the same format as
        intervals. Offset can be positive or negative with magnitude up
        to 1/2 the sample interval. The default offset is 0. Collection
        is always synchronous.

   **config**
      |
      | A list of dictionaries containing plugin configuration options.
        Each dictionary in the list is a "config" command call, and in
        this fashion, the YAML configuration mimics running multiple
        "config" statements in a conventional v4 configuration file.
        Strings may also be used in lieu of a dictionary, however
        configuration lines defined as strings will be passed as a LDMSD
        request as is, with no parsing done by the YAML parser.

      | NOTE: When using a configuration string, rather than a
        dictionary, to configure a plugin, the string is not parsed by
        the YAML parser, and the exact string will be passed to the
        LDMSD as is. As such, the only accepted permission format when
        using a configuration string, rather than a dictionary, is an
        octal number, e.g. "0777". If an octal number is entered into
        the configuration as an integer, the parser will interpret the
        number incorrectly, and the permissions will not be set as
        expected.

      Any plugin-specific configuration options not listed below will be
      included in the configuration.

         **schema**
            |
            | Name of the metric set to use.

         **[perm]**
            |
            | Access permissions for the metric set within the
              container. String of octal number or unix-like permissions
              format. e.g. "rw-r--r--"

         **[component_id]**
            |
            | Unique ID of the component being monitored. If configuring
              an entire cluster, it's advised to set this to reference
              an environment variable on the system.

         **[producer]**
            |
            | Producer name must be unique in an aggregator. It is
              independent of any attributes specified for the metric
              sets or hosts. A producer name will be generated by the
              yaml using the hostname of the sampler and the plugin
              instance name if one is not specified.
              <hostname>/<plugin_name>
