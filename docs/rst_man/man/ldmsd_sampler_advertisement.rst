.. _ldmsd_sampler_advertisement:

===========================
ldmsd_sampler_advertisement
===========================

:Date:   27 March 2024
:Manual section: 7
:Manual group: LDMSD


---------------------------------------
Manual for LDMSD Sampler Advertisement
---------------------------------------

SYNOPSIS
========

\**Sampler side Commands*\*

-  name=\ *NAME* xprt=\ *XPRT* host=\ *HOST* port=\ *PORT*
   [auth=\ *AUTH_DOMAIN*]

-  name=\ *NAME* [xprt=\ *XPRT* host=\ *HOST* port=\ *PORT*
   auth=\ *AUTH_DOMAIN*]

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

LDMSD Sampler Discovery is a capability that enables LDMSD to
automatically add producers whose hostnames match a given regular
expression or whose IP addresses fall in a given IP range. If neither
regular expression nor an IP range is given, LDMSD adds a producer
whenever it receives an advertisement message. The feature eliminates
the need for manual configuration of sampler hostnames in the aggregator
configuration file.

Admins specify the aggregator hostname and the listening port in sampler
configuration via the **advertiser_add** command and start the
advertisement with the **advertiser_start** command. The sampler now
advertises its hostname to the aggregator. On the aggregator, admins may
specify a regular expression to be matched with the sampler hostname or
an IP range via the **prdcr_listen_add** command. The
**prdcr_listen_start** command is used to tell the aggregator to
automatically add producers corresponding to a sampler of which the
hostname matches the regular expression or the IP address falls in the
given IP range.

The automatically added producers are of the 'advertised' type. The
producer's name is the same as the value of the ‘name’ attribute given
at the **advertiser_add** line in the sampler configuration file. LDMSD
automatically starts the advertised producers. Admins could provide the
**disable_start** attribute at the **prdcr_listen_add** with the ‘true’
value to let LDMSD not automatically start the advertised producers.
Admins can stop an advertised producer using the **prdcr_stop** or
**prdcr_stop_regex** commands. They can be restarted by using the
**prdcr_start** or **prdcr_start_regex** commands.

The description for each command and its parameters are as follows.

\**Sampler Side Commands*\*

**advertiser_add** adds a new advertisement. The parameters are:

   -  String of the advertisement name. The aggregator uses the string
      as the producer name as well.

   -  Aggregator hostname

   -  Transport to connect to the aggregator

   -  Listen port of the aggregator

   -  Reconnect interval d

   -  The authentication domain to be used to connect to the aggregator

**advertiser_start** starts an advertisement. If the advertiser does not
exist, LDMSD will create the advertiser. In this case, the mandatory
attributes for **advertiser_add must be given. The parameters are:**

   -  The advertisement name to be started

   -  Aggregator hostname

   -  Transport to connect to the aggregator

   -  Listen port of the aggregator

   -  Reconnect interval

   -  The authentication domain to be used to connect to the aggregator

**advertiser_stop** stops an advertisement. The parameters are:

   -  The advertisement name to be stopped

**advertiser_del** deletes an advertisement. The parameters are:

   -  The advertisement name to be deleted

**advertiser_status reports the status of each advertisement. An
optional parameter is:**

   -  Advertisement name

\**Aggregator Side commands*\*

**prdcr_listen_add** adds a regular expression to match sampler
advertisements. The parameters are:

   -  String of the prdcr_listen name.

   -  True to tell LDMSD not to start producers automatically

   -  Regular expression to match with hostnames in sampler
      advertisements

   -  IP Range in the CIDR format either in IPV4 or IPV6

**prdcr_listen_start** starts accepting sampler advertisement with
matches hostnames. The parameters are:

   -  Name of prdcr_listen to be started

**prdcr_listen_stop** stops accepting sampler advertisement with matches
hostnames. The parameters are:

   -  Name of prdcr_listen to be stopped

**prdcr_listen_del** deletes a regular expression to match hostnames in
sampler advertisements. The parameters are:

   -  Name of prdcr_listen to be deleted

**prdcr_listen_status** report the status of each prdcr_listen object.
There is no parameter.

Managing Receive Quota and Rate Limits for Auto-Added Producers
===============================================================

The receive quota and rate limit control machanisms govern the amount of
data a producer receives from the data source connected through
ldms_xprt. This helps prevent data bursts that could overwhelm the LDMS
daemon host and network resources. To configure receive quota and rate
limits, users can create a listening endpoint on the aggregator using
the **listen** command specifying the desired values of the **quota**
and **rx_rate** attributes. Moreover, users configure the sampler
daemons to advertise to the listening endpoint created on the
aggregator, including the preferred receive quota and rate limit values.

EXAMPLE
=======

In this example, there are three LDMS daemons running on **node-1**,
**node-2**, and **node03**. LDMSD running on **node-1** and **node-2**
are sampler daemons, namely **samplerd-1** and **samplerd-2**. The
aggregator (**agg**) runs on **node-3**. All LDMSD listen on port 411.

The sampler daemons collect the **meminfo** set, and they are configured
to advertise themselves and connect to the aggregator using sock on host
**node-3** at port 411. They will try to reconnect to the aggregator
every 10 seconds until the connection is established. The following are
the configuration files of the **samplerd-1** and **samplerd-2**.

::

   > cat samplerd-1.conf
   # Create a listening endpoint
   listen xprt=sock port=411
   # Add and start an advertisement
   advertiser_add name=samplerd-1 xprt=sock host=node-3 port=411 reconnect=10s
   advertiser_start name=samplerd-1
   # Load, configure, and start the meminfo plugin
   load name=meminfo
   config name=meminfo producer=samplerd-1 instance=samplerd-1/meminfo
   start name=meminfo interval=1s

   > cat samplerd-2.conf
   # Create a listening endpoint
   listen xprt=sock port=411
   # Add and start an advertisement using only the advertiser_start command
   advertiser_start name=samplerd-2 host=node-3 port=411 reconnect=10s
   # Load, configure, and start the meminfo plugin
   load name=meminfo
   config name=meminfo producer=samplerd-2 instance=samplerd-2/meminfo
   start name=meminfo interval=1s

The aggregator is configured to accept advertisements from the sampler
daemons that the hostnames match the regular expressions **node0[1-2]**.
The name of the auto-added producers is the name of the advertiser on
the sampler daemons.

::

   > cat agg.conf
   # Create a listening endpoint
   listen xprt=sock port=411
   # Accept advertisements sent from LDMSD running on hostnames matched node-[1-2]
   prdcr_listen_add name=computes regex=node-[1-2]
   prdcr_listen_start name=computes
   # Add and start an updater
   updtr_add name=all_sets interval=1s offset=100ms
   updtr_prdcr_add name=all_sets regex=.*
   updtr_start name=all

LDMSD provides the command **advertiser_status** to report the status of
advertisement of a sampler daemon.

::

   > ldmsd_controller -x sock -p 10001 -h node-1
   Welcome to the LDMSD control processor
   sock:node-1:10001> advertiser_status
   Name             Aggregator Host  Aggregator Port Transport    Reconnect (us)         State
   ---------------- ---------------- --------------- ------------ --------------- ------------
   samplerd-1                 node-3             411         sock        10000000    CONNECTED
   sock:node-1:10001>

Similarly, LDMSD provides the command **prdcr_listen_status** to report
the status of all prdcr_listen objects on an aggregator. The command
also reports the list of auto-added producers corresponding to each
prdcr_listen object.

::

   > ldmsd_controller -x sock -p 10001 -h node-3
   Welcome to the LDMSD control processor
   sock:node-3:10001> prdcr_listen_status
   Name                 State      Regex           IP Range
   -------------------- ---------- --------------- ------------------------------
   computes             running    node-[1-2]      -
   Producers: samplerd-1, samplerd-2
   sock:node-3:10001>

Next is an example that controls the receive quota and rate limits of
the auto-added producers on agg11. Similar to the first example, the
aggregator, agg11, listens on port 411 and waits for advertisements.
Moreover, a listening endpoint on port 412 is added with a receive quota
value. The aggregator also creates producers when an advertisement sent
from the host its IP address falling into the subnet 192.168.0.0:16.

::

   > cat agg11.conf
   # Create a listening endpoint
   listen xprt=sock port=411
   # Create the listening endpoint for receiving advertisement
   listen xprt=sock port=412 quota=4000
   # Accept advertisements sent from LDMSD running on hostnames their IP address
   # falling in the range 192.168.0.0:16.
   prdcr_listen_add name=compute ip=192.168.0.0:16
   prdcr_listen_start name=compute
   # Add and start an updater
   updtr_add name=all_sets interval=1s offset=100ms
   updtr_prdcr_add name=all_sets regex=.*
   updtr_start name=all

There are two sampler daemons, which are configured to advertise to port
412 so that the auto-added producers adopt the receive credidts of the
listening endpoint on port 412.

::

   > cat samplerd-3.conf
   # Create a listening endpoint
   listen xprt=sock port=411
   # Start an advertiser that sends the advertisement to port 412 on the aggregator
   # host
   advertiser_start name=samplerd-3 host=agg11 xprt=sock port=412 reconnect=10s
   # Load, configure, and start the meminfo plugin
   load name=meminfo
   config name=meminfo producer=samplerd-3 instance=samplerd-3/meminfo
   start name=meminfo interval=1s

::

   > cat samplerd-4.conf
   # Create a listening endpoint
   listen xprt=sock port=411
   # Start an advertiser that sends the advertisement to port 412 on the aggregator
   # host
   advertiser_start name=samplerd-4 host=agg11 xprt=sock port=412 reconnect=10s
   # Load, configure, and start the meminfo plugin
   load name=meminfo
   config name=meminfo producer=samplerd-4 instance=samplerd-4/meminfo
   start name=meminfo interval=1s

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>` :ref:`ldmsd_controller(8) <ldmsd_controller>`
