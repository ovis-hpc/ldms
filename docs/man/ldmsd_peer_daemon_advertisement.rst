===============================
ldmsd_peer_daemon_advertisement
===============================

:Date:   12 December 2024

NAME
====

ldmsd_peer_daemon_advertisement - Manual for LDMSD Peer Daemon
Advertisement

SYNOPSIS
========

\**Peer side Commands*\*

-  name=\ *NAME* xprt=\ *XPRT* host=\ *HOST* port=\ *PORT*
   reconnect=\ *RECONNECT* [auth=\ *AUTH_DOMAIN*]

-  name=\ *NAME* [xprt=\ *XPRT* host=\ *HOST* port=\ *PORT*
   auth=\ *AUTH_DOMAIN* reconnect=\ *RECONNECT*]

-  name=\ *NAME*

-  name=\ *NAME*

-  [name=\ *NAME*]

\**Aggregator Side Commands*\*

-  name=\ *NAME*" [disable_start=\ *TURE|FALSE*] [regex=\ *REGEX*]
   [ip=\ *CIDR*]

-  name=\ *NAME*

-  name=\ *NAME*

-  name=\ *NAME*

-

DESCRIPTION
===========

LDMSD Peer Daemon Advertisement is a capability that enables LDMSD to
automatically add producers for advertisers whose hostname matches a
regular expression or whose IP address is in a range. The feature
reduces the need for manual configuration of producers in configuration
files.

Admins specify the aggregator's hostname and listening port in the
peer's configuration via the **advertiser_add** command and start the
advertisement with the **advertiser_start** command. The peer daemon
advertises their hostname to the aggregator. On the aggregator, admins
specify a regular expression to be matched with the peer hostname or an
IP range that the peer IP address falls in via the **prdcr_listen_add**
command. The **prdcr_listen_start** command is used to tell the
aggregator to automatically add producers corresponding to a peer daemon
whose hostname matches the regular expression or whose IP address falls
in the IP range. If neither a regular expression nor an IP range is
given, the aggregator will create a producer upon receiving any
advertisement messages.

The auto-generated producers are of the ‘advertised’ type. The producer
name is **<host:port>**, where **host** is the peer hostname, and
**port** is the first listening port of the peer daemon. LDMSD
automatically starts the advertised producers, unless the
'disable_start' attribute is given on the **prdcr_listen_add** line. The
advertised producers need to be stopped manually by using the command
**prdcr_stop** or **prdcr_stop_regex**. They can be restarted by using
the command **prdcr_start** or **prdcr_start_regex**.

The description for each command and its parameters are as follows.

\**Peer Side Commands*\*

**advertiser_add** adds a new advertisement. The parameters are:

   -  Advertiser name

   -  Aggregator hostname

   -  Transport to connect to the aggregator

   -  Listen port of the aggregator

   -  Reconnect interval

   -  The authentication domain to be used to connect to the aggregator

**advertiser_start** starts an advertisement. If the advertiser does not
exist, LDMSD will create the advertiser. In this case, the mandatory
attributes for **advertiser_add must be given. The parameters are:**

   -  Name of the advertiser to be started

   -  Aggregator hostname

   -  Transport to connect to the aggregator

   -  Listen port of the aggregator

   -  Reconnect interval

   -  The authentication domain to be used to connect to the aggregator

**advertiser_stop** stops an advertisement. The parameters are:

   -  Nmae of the advertiser to be stopped

**advertiser_del** deletes an advertisement. The parameters are:

   -  Name of the advertiser to be deleted

**advertiser_status reports the status of each advertisement. An
optional parameter is:**

   -  Advertiser name

\**Aggregator Side commands*\*

**prdcr_listen_add** adds a prdcr_listen. The parameters are:

   -  String of the prdcr_listen name.

   -  True to tell LDMSD not to start producers automatically

   -  Regular expression to match with hostnames of peer daemons

   -  IP Range in the CIDR format either in IPV4

**prdcr_listen_start** starts accepting peer advertisement with matches
hostnames. The parameters are:

   -  Name of prdcr_listen to be started

**prdcr_listen_stop** stops accepting peer advertisement with matches
hostnames. The parameters are:

   -  Name of prdcr_listen to be stopped

**prdcr_listen_del** deletes a prdcr_listen. The parameters are:

   -  Name of prdcr_listen to be deleted

**prdcr_listen_status** report the status of each prdcr_listen object.
There is no parameter.

EXAMPLE
=======

In this example, there are three LDMS daemons running on **node-1**,
**node-2**, and **node03**. LDMSD running on **node-1** and **node-2**
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
   Name                 State      Regex           IP Range
   -------------------- ---------- --------------- ------------------------------
   computes             running    node-[1-2]      -
   Producers: node-1:411, node-2:411
   sock:node-3:411>

SEE ALSO
========

**ldmsd**\ (8) **ldmsd_controller**\ (8)
