.. _ldmsd_peer_daemon_advertisement:

===============================
ldmsd_peer_daemon_advertisement
===============================

------------------------------------------
Manual for LDMSD Peer Daemon Advertisement
------------------------------------------

:Date: 12 December 2024
:Manual section: 7
:Manual group: LDMSD


SYNOPSIS
========

\**Peer side Commands*\*

advertiser_add
   name=\ *NAME* xprt=\ *XPRT* host=\ *HOST* port=\ *PORT*
   reconnect=\ *RECONNECT* [auth=\ *AUTH_DOMAIN*]

advertiser_start
   name=\ *NAME* [xprt=\ *XPRT*] [host=\ *HOST*] [port=\ *PORT*]
   [auth=\ *AUTH_DOMAIN*] [reconnect=\ *RECONNECT*]

advertiser_stop
   name=\ *NAME*

advertiser_del
   name=\ *NAME*

advertiser_status
   [name=\ *NAME*]

\**Aggregator Side Commands*\*

prdcr_listen_add
   name=\ *NAME*" [regex=\ *REGEX*] [ip=\ *CIDR*]
   [disable_start=\ *TRUE|FALSE*] [quota=\ *QUOTA*]
   [rx_rate=\ *RX_RATE*] [type=\ *passive|active*]
   [advertiser_port=\ *PORT*] [advertiser_xprt=\ *XPRT*]
   [advertiser_auth\ *AUTH_DOM*] [rails=\ *RAILS*]
   [reconnect=\ *INTERVAL*]

prdcr_listen_start
   name=\ *NAME*

prdcr_listen_stop
   name=\ *NAME*

prdcr_listen_del
   name=\ *NAME*

prdcr_listen_status

DESCRIPTION
===========

LDMSD Peer Daemon Advertisement is a capability that enables LDMSD to
automatically add producers for advertising peers whose hostname matches
a regular expression or whose IP address is in a range. The feature
reduces the need for manual configuration of producers in configuration
files.

LDMSD supports two types of advertised producers: passive and active. In
passive mode (default), the aggregator accepts connections from
advertising peers for data transfer. In active mode, after receiving an
advertisement from a peer, the aggregator initiates a separate
connection to the advertising peer using specified transport parameters
(port, authentication, rail size, etc.). Active mode enables
administrators to control data transfer resouces through rail size
configuration and separate data transfer from communication channels.

Administrators specify the aggregator's hostname and listening port in
the peer's configuration via the **advertiser_add** command and start
the advertisement with the **advertiser_start** command. The peer daemon
advertises their hostname to the aggregator. On the aggregator,
administrators specify a regular expression to be matched with the peer
hostname or an IP range that the peer IP address falls in via the
**prdcr_listen_add** command. The **prdcr_listen_start** command is used
to tell the aggregator to automatically add producers corresponding to a
peer daemon whose hostname matches the regular expression or whose IP
address falls in the IP range. If neither a regular expression nor an IP
range is given, the aggregator will create a producer upon receiving any
advertisement messages.

The auto-generated producers are of the ‘advertised’ type. The producer
name is **<host:port>**, where **host** is the peer hostname, and
**port** is the first listening port of the peer daemon. LDMSD
automatically starts the advertised producers, unless the
'disable_start' attribute is given on the **prdcr_listen_add** line. The
advertised producers need to be stopped manually by using the command
**prdcr_stop** or **prdcr_stop_regex**. They can be restarted by using
the command **prdcr_start** or **prdcr_start_regex**. If type=active is
specified at **prdcr_listen_add** line, the advertised producers will
behave as active producers.

The description for each command and its parameters are as follows.

\**Peer Side Commands*\*

**advertiser_add** adds a new advertisement. The parameters are:

   name=NAME
      Advertiser name

   host=HOST
      Aggregator hostname

   xprt=XPRT
      Transport to connect to the aggregator

   port=PORT
      Listen port of the aggregator

   reconnect=INTERVAL
      Reconnect interval

   [auth=AUTH_DOMAIN]
      The authentication domain to be used to connect to the aggregator

**advertiser_start** starts an advertisement. If the advertiser does not
exist, LDMSD will create the advertiser. In this case, the mandatory
attributes for **advertiser_add must be given. The parameters are:**

   name=NAME
      Name of the advertiser to be started

   [host=HOST]
      Aggregator hostname

   [xprt=XPRT]
      Transport to connect to the aggregator

   [port=PORT]
      Listen port of the aggregator

   [reconnect=INTERVAL]
      Reconnect interval

   [auth=AUTH_DOMAIN]
      The authentication domain to be used to connect to the aggregator

**advertiser_stop** stops an advertisement. The parameters are:

   name=NAME
      Nmae of the advertiser to be stopped

**advertiser_del** deletes an advertisement. The parameters are:

   name=NAME
      Name of the advertiser to be deleted

**advertiser_status reports the status of each advertisement. An
optional parameter is:**

   [name=NAME]
      Advertiser name

\**Aggregator Side commands*\*

**prdcr_listen_add** adds a prdcr_listen. The parameters are:

   name=NAME
      String of the prdcr_listen name

   [type=passive|active]
      Type of advertised producers. Default is passive.

   - passive: aggregator accepts connections from advertising peers

   - active: upon receiving an advertisement, aggregator initiates a
     separate connection back to the advertising peer. Requires
     advertiser_xprt, advertiser_port, reconnect parameters, and
     authentication domain if it is used

   [regex=REGEX]
      Regular expression to match with hostnames of peer daemons

   [ip=CIDR]
      IP Range in the CIDR format either in IPV4

   [quota=QUOTA
      Controls the amount of data that can be received on connections
      from advertising peers. Functions like the quota parameter in
      prdcr_add. If not specified, defaults to the value set by the
      --quota option when starting the LDMS daemon. If neither --quota
      nor this parameter is specified, there is no limit on receive
      quota.

   [rx_rate=RX_RATE]
      Controls the rate of data received (in bytes/second) on
      connections from advertising peers. Functions like the rx_rate
      parameter in prdcr_add. Unluck quota which limits total received
      data, rx_rate limits the data flow per second. If not specified,
      the receive rate is unlimited

   [disable_start=TRUE|FALSE]
      True to tell LDMSD not to start producers automatically

   [advertiser_port=PORT]
      Port number of the advertising peer to connect to. Functions like
      the port parameter in prdcr_add. Required when type=active

   [advertiser_xprt=XPRT]
      Transport type to use when connecting to advertising peers.
      Functions like the xprt in prdcr_add. Required when type=active

   [advertiser_auth=AUTH_DOM]
      Authentication domain for connections to advertising peers.
      Functions like the auth in prdcr_add. If it is omitted when
      type=active, the default authentication is used to connect to
      advertising peers

   [reconnect=INTERVAL]
      Reconnection Interval. Functions like the reconnect in prdcr_add.
      Required when type=active

**prdcr_listen_start** starts accepting peer advertisement with matches
hostnames. The parameters are:

   name=NAME
      Name of prdcr_listen to be started

**prdcr_listen_stop** stops accepting peer advertisement with matches
hostnames. The parameters are:

   name=NAME
      Name of prdcr_listen to be stopped

**prdcr_listen_del** deletes a prdcr_listen. The parameters are:

   name=NAME
      Name of prdcr_listen to be deleted

**prdcr_listen_status** report the status of each prdcr_listen object.
There is no parameter.

EXAMPLE
=======

In this example, there are three LDMS daemons running on **node-1**,
**node-2**, and **node-3**. LDMSD running on **node-1** and **node-2**
are sampler daemons, namely **samplerd-1** and **samplerd-2**. The
aggregator (**agg11**) runs on **node-3**. All LDMSD listen on port 411.

The sampler daemons collect the **meminfo** set, and they are configured
to advertise themselves and connect to the aggregator using sock on host
**node-3** at port 411. They will try to reconnect to the aggregator
every 10 seconds until the connection is established. Once the
connection is established, they will send an advertisement to the
aggregator. The following are the configuration files of the
**samplerd-1** and **samplerd-2**.

::

   > cat samplerd-1.conf
   # Add and start an advertisement
   advertiser_add name=agg11 xprt=sock host=node-3 port=411 reconnect=10s
   advertiser_start name=agg11
   # Load, configure, and start the meminfo plugin
   load name=meminfo
   config name=meminfo producer=samplerd-1 instance=samplerd-1/meminfo
   start name=meminfo interval=1s

   > cat samplerd-2.conf
   # Add and start an advertisement using only the advertiser_start command
   advertiser_start name=agg11 host=node-3 port=411 reconnect=10s
   # Load, configure, and start the meminfo plugin
   load name=meminfo
   config name=meminfo producer=samplerd-2 instance=samplerd-2/meminfo
   start name=meminfo interval=1s

The aggregator is configured to accept advertisements from the sampler
daemons that the hostnames match the regular expressions **node0[1-2]**.

::

   > cat agg.conf
   # Accept advertisements sent from LDMSD running on hostnames matched node-[1-2]
   prdcr_listen_add name=computes regex=node-[1-2]
   prdcr_listen_start name=computes
   # Add and start an updater
   updtr_add name=all_sets interval=1s offset=100ms
   updtr_prdcr_add name=all_sets regex=.*
   updtr_start name=all_sets

LDMSD provides the command **advertiser_status** to report the status of
advertisement of a sampler daemon.

::

   > ldmsd_controller -x sock -p 411 -h node-1
   Welcome to the LDMSD control processor
   sock:node-1:411> advertiser_status
   Name             Aggregator Host  Aggregator Port Transport    Reconnect (us)         State
   ---------------- ---------------- --------------- ------------ --------------- ------------
   agg11                      node-3             411         sock        10000000    CONNECTED
   sock:node-1:411>

Similarly, LDMSD provides the command **prdcr_listen_status** to report
the status of all prdcr_listen objects on an aggregator. The command
also reports the list of auto-added producers corresponding to each
prdcr_listen object.

::

   > ldmsd_controller -x sock -p 411 -h node-3
   Welcome to the LDMSD control processor
   sock:node-3:411> prdcr_listen_status
   Name                 State      Type     IP Range                       Regex
   -------------------- ---------- -------- ------------------------------ --------------------
   computes             running    passive  -                              node-[1-2]
       Connect config: None
   Producers: node-1:411, node-2:411

   sock:node-3:411>

Below is an example of prdcr_status output of advertised producers. The
example uses the --cmd cmd-line option to provide the prdcr_status
command at the start line instead of starting an interactive session.

::

   > ldmsd_controller -x sock -p 411 -h node-3 --cmd 'prdcr_status'
   Name             Host             Port         Transport    auth             State        Type
   ---------------- ---------------- ------------ ------------ ---------------- ------------ --------------------
   node-1:10001     node-1                  42210 sock         DEFAULT          CONNECTED    advertised, passive
       samplerd-1/meminfo meminfo          READY
       samplerd-1/procnetdev2 procnetdev2      READY
   node-2:10001     node-2                  42212 sock         DEFAULT          CONNECTED    advertised, passive
       samplerd-2/meminfo meminfo          READY
       samplerd-2/procnetdev2 procnetdev2      READY

Active Mode Example:

This example demonstrates how to configure active mode producers where
the aggregator initiates the connection request upon receiving an
advertisement from advertising peers. The configuration shows how to set
up multiple rails for enhanced data transfer performance and how to
separate the configuration channel from the data channel using different
port. The sampler daemons run on **node-1** and **node-2**. The
aggregator runs on **node-3**.

::

   > cat samplerd-1.conf
   # Set up the default authentication domain
   default_auth plugin=munge
   # Listen on port 411 for configuration using the default authentication
   listen port=411 xprt=sock
   # Listen on port 412 for data using the default authentication
   listen port=412 xprt=sock
   # Add and start an advertisement
   advertiser_add name=agg11 xprt=sock host=node-3 port=411 reconnect=10s
   advertiser_start name=agg11
   # Load, configure, and start the meminfo plugin
   load name=meminfo
   config name=meminfo producer=samplerd-1 instance=samplerd-1/meminfo
   start name=meminfo interval=1s

   > cat samplerd-2.conf
   # Set up the default authentication domain
   default_auth plugin=munge
   # Listen on port 411 for configuration using the default authentication
   listen port=411 xprt=sock
   # Listen on port 412 for data using the default authentication
   listen port=412 xprt=sock
   # Add and start an advertisement
   advertiser_add name=agg11 xprt=sock host=node-3 port=411 reconnect=10s
   advertiser_start name=agg11
   # Load, configure, and start the meminfo plugin
   load name=meminfo
   config name=meminfo producer=samplerd-2 instance=samplerd-2/meminfo
   start name=meminfo interval=1s

   > cat agg.conf
   # Set up the default authentication domain
   default_auth plugin=munge
   # Listen on port 411 for configuration using the default authentication
   listen port=411 xprt=sock
   # Listen on port 412 for data using the default authentication
   listen port=412 xprt=sock
   # Accept advertisements and create active producers connecting to the data port
   # 412 using the default authentication
   prdcr_listen_add name=computes type=active regex=node-[1-2] advertiser_port=412 advertiser_xprt=sock rail=4 reconnect=1m
   prdcr_listen_start name=computes

::

   > ldmsd_controller -x sock -p 411 -h node-3 -a munge
   Welcome to the LDMSD control processor
   sock:node-3:411> prdcr_listen_status
   Name                 State      Type     IP Range                       Regex
   -------------------- ---------- -------- ------------------------------ --------------------
   computes             running    active   -                              node-[1-2]
       Connect config: xprt=sock port=412 reconnect=1m rail=4
   Producers: node-1:411, node-2:411

   sock:node-3:411>

Below is an example of prdcr_status output of advertised producers. Note
that the port numbers in the Port column is the port the advertised
producers send a connection request to the peers, which are the port the
sampler daemons open for data transfer.

::

   > ldmsd_controller -x sock -p 411 -h node-3 -a munge --cmd 'prdcr_status'
   Name             Host             Port         Transport    auth             State        Type
   ---------------- ---------------- ------------ ------------ ---------------- ------------ --------------------
   node-1:10001     node-1                    412 sock         DEFAULT          CONNECTED    advertised, active
       samplerd-1/meminfo meminfo          READY
       samplerd-1/procnetdev2 procnetdev2      READY
   node-2:10001     node-2                    412 sock         DEFAULT          CONNECTED    advertised, active
       samplerd-2/meminfo meminfo          READY
       samplerd-2/procnetdev2 procnetdev2      READY

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>` :ref:`ldmsd_controller(8) <ldmsd_controller>`
