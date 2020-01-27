[#]: # (man)

NAME
====

ldmsd-aggregator - LDMSD Usage as an Aggregator


SYNOPSIS
========

`ldmsd` -c *AGG.CONF* [options]

This page is a guide to configure `ldmsd` as an aggregator. Please see
**ldmsd**(1) for the full list of daemon options. Also, see
**ldmsd_controller**(8) for the full list of configuration commands.


DESCRIPTION
===========

`ldmsd` can be configured to collect data from other `ldmsd`'s over LDMS
network. In this case, the ldmsd is called an aggregator. The aggregated sets
reside in this aggregator can then be collected by other `ldmsd`'s or be viewed
by `ldms_ls`. An aggregator can optionally be configured to store the collected
data as well. See [AGGREGATE][1] section for how to setup ldmsd to
aggregate data from other ldmsd, and [STORE][2] section for how to store the
aggregated data.

[1]: #aggregate
[2]: #store

**NOTICE**: ldmsd does **NOT** support being both aggregator and sampler at the
            same time.


AGGREGATE
=========

To aggregate LDMS sets from other `ldmsd`'s (or programs implementing LDMS API),
the `ldmsd` needs `prdcr` (producer) to manage LDMS connections, and `updtr`
(updater) configuration objects to issue LDMS updates. The following sub
sections [Producer](#producer) and [Updater](#updater) describe how to setup
`prdcr` and `updtr` respectively.

Producer
--------

A `prdcr` (producer) is an object inside `ldmsd` that manages an LDMS connection
to another ldmsd (or other program providing LDMS set). A `prdcr` can be
created, configured, and start through LDMSD config file or `ldmsd_controller`
as the following template:

```
prdcr_add name=NAME host=HOST port=PORT xprt=XPRT type=TYPE \
                    interval=INTV
prdcr_start name=NAME
```

`prdcr_add` command creates and configures prdcr object. The *NAME* is the
unique name to refer to the object. This must be unique among all configuration
objects. *HOST* and *PORT* is the hostname/IP address and the port of the other
`ldmsd`. The *XPRT* is the transport type (e.g. sock).  *INTV* is the connection
interval in microseconds. The *TYPE* can be `active` (connection initiated by
us) or `passive` (connection initiated by peer).

`prdcr_start` starts the prdcr object. The prdcr object will try connecting to
the peer in the specified interval, and keep trying reconnecting in such
interval when disconnected.

The `prdcr` object only connects and lookup the sets. To update the data in the
sets, we need an `updtr` (updater).


Updater
-------

An `updtr` (updater) is an object inside `ldmsd` that updates the LDMS sets from
one or more `prdcr`. An `updtr` can be created, configured, and start as the
following template:

```
updtr_add name=NAME interval=INTV [offset=OFFSET] \
                    [auto_interval=(true|false)]
updtr_prdcr_add name=NAME regex=PRDCR_REGEX
updtr_start name=NAME
```

`updtr_add` creates and configures the updater. The *NAME* is the unique name
given to the updater. The name must be unique among all configuration objects.
The *INTV* (microseconds) is the interval for an updater to update the
associated sets. If *OFFSET* (microseconds) is set, the update time is sync with
the wallclock + *OFFSET*. If auto\_interval is *true*, the updater updates each
set according to its interval in the metadata of each set. The **interval**
attribute is still needed as a fallback value.

`updtr_prdcr_add` tells the updater (identified by *NAME*) to update sets from
the producers matching the regular expression *PRDCR\_REGEX*.

`updtr_start` simply starts the updater.


STORE
=====

The LDMS data obtained by `updtr` can be stored using an LDMSD storage plugin
instance and a `strgp` (storage policy). An LDMSD storage plugin instance
governs how the data is stored (e.g. as a CSV format or into SOS database), and
`strgp` specify which data goes into which storage plugin instance. A storage
plugin instance can associate with only one `strgp`, and vice versa. In
addition, a `strgp` can only associate with one schema. The following is a
template to create/configure storage plugin instance and `strgp`.

```
load name=INST plugin=STORE_PLUGIN_NAME
config name=INST STORE_INST_OPTIONS

strgp_add name=NAME container=INST schema=SCHEMA
strgp_prdcr_add name=NAME regex=PRDCR_REGEX
strgp_start name=NAME
```

`load` creates a new plugin instance, identified by *INST*, of the plugin
*STORE_PLUGIN_NAME*. Then, the `config` command configures the storage plugin
instance (see specific plugin manual).

`strgp_add` creates a storage policy object. The *NAME* is the name of the
storage policy object. The attribute `container` *INST* refers to the storage
plugin instance to associate with. The `schema` *SCHEMA* attribute refers to the
LDMS schema to process by this policy.

`strgp_prdcr_add` associates `strgp` with the `prdcr` matching the
*PRDCR_REGEX*, so that the LDMS sets from the matching producers will be
processed by the storage policy.

`strgp_start` starts the policy. After this command, the data will be sent to
the storage plugin instance after each successful update on the LDMS sets in the
matching producers and schema.


EXAMPLE
=======

The following is an example of an ldmsd aggregator config file that collects
data from two other ldmsds (nid00001:10001 and nid00002:10001) over LDMS socket
transport, and stores meminfo and procnetdev data using CSV storage plugin.

```
# Producers
prdcr_add name=p1 host=nid00001 port=10001 xprt=sock type=active \
                  interval=1000000
prdcr_start name=p1

prdcr_add name=p2 host=nid00002 port=10001 xprt=sock type=active \
                  interval=1000000
prdcr_start name=p2

# Store for meminfo
load name=csv_mem plugin=store_csv
config name=csv_mem path=/csv/meminfo
strgp_add name=s_mem container=csv_mem schema=meminfo
strgp_prdcr_add name=s_mem regex=.*
strgp_start name=s_mem

# Store for procnetdev
load name=csv_net plugin=store_csv
config name=csv_net path=/csv/netdev
strgp_add name=s_net container=csv_net schema=procnetdev
strgp_prdcr_add name=s_net regex=.*
strgp_start name=s_net

# Updater
updtr_add name=u interval=1000000 auto_interval=true
updtr_prdcr_add name=u regex=.*
updtr_start name=u
```

SEE ALSO
========

**ldmsd**(1)
[ldmsd-sampler](ldmsd-sampler.md)(7)
