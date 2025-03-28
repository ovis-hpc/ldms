.. _ldmsd_failover:

==============
ldmsd_failover
==============

-----------------------------------------------------------
Explanation, configuration, and commands for ldmsd failover
-----------------------------------------------------------

:Date:   13 Aug 2018
:Manual section: 7
:Manual group: LDMSD

SYNOPSIS
========

failover_config
   host=\ *HOST* port=\ *PORT* xprt=\ *XPRT* [peer_name=\ *NAME*]
   [interval=\ *USEC*] [timeout_factor=\ *FLOAT*] [auto_switch=\ *0|1*]

failover_start

failover_stop

failover_status

failover_peercfg_start

failover_peercfg_stop

DESCRIPTION
===========

**ldmsd** can be configured to form a failover pair with another
**ldmsd**. In a nutshell, when a failover pair is formed, the ldmsd's
exchange their updater and producer configuration so that when one goes
down, the other will take over the LDMS set aggregation load
(**failover**).

**Ping-echo** mechanism is used to detect the service unavailability.
Each ldmsd in the pair sends ping requests to the other, the peer echo
back along with its status. When the echo has not been received within
the timeout period (see below), the peer configuration is automatically
started (failover).

The following paragraphs explain ldmsd configuration commands relating
to ldmsd failover feature.

**failover_config** configure failover feature in an ldmsd. The failover
service must be stopped before configuring it. The following list
describes the command parameters.

   host=HOST
      The hostname of the failover partner. This is optional in
      re-configuration.

   port=PORT
      The LDMS port of the failover partner. This is optional in
      re-configuration.

   xprt=XPRT
      The LDMS transport type (sock, rdma, or ugni) of the failover
      partner. This is optional in re-configuration.

   peer_name=NAME
      (Optional) The ldmsd name of the failover parter (please see
      option **-n** in :ref:`ldmsd(8) <ldmsd>`). If this is specified, the ldmsd
      will only accept a pairing with other ldmsd with matching name.
      Otherwise, the ldmsd will pair with any ldmsd requesting a
      failover pairing.

   interval=uSEC
      (Optional) The interval (in micro-seconds) for ping and transport
      re-connecting. The default is 1000000 (1 sec).

   timeout_factor=FLOAT
      (Optional) The echo timeout factor. The echo timeout is calculated
      by **timeout_factor \* interval**. The default is 2.

   auto_switch=0|1
      (Optional) If this is on (1), ldmsd will start **peercfg** or stop
      **peercfg** automatically. Otherwise, the user need to issue
      **failover_peercfg_start** or **failover_peercfg_stop** manually.
      By default, this value is 1.

**failover_start** is a command to start the (configured) failover
service. After the failover service has started, it will pair with the
peer, retreiving peer configurations and start peer configurations when
it believes that the peer is not in service (with \`auto_switch=1\`,
otherwise it does nothing).

Please also note that when the failover service is in use (after
**failover_start**), prdcr, updtr, and strgp cannot be altered over the
in-band configuration (start, stop, or reconfigure). The failover
service must be stopped (**failover_stop**) before altering those
configuration objects.

**failover_stop** is a command to stop the failover service. When the
service is stopped, the peer configurations will also be stopped and
removed from the local memory. The peer also won't be able to pair with
local ldmsd when the failover service is stopped. Issuing
**failover_stop** after the pairing process succeeded will stop failover
service on both daemons in the pair.

**failover_status** is a command to report (via **ldmsd_controller**)
the failover statuses.

**failover_peercfg_start** is a command to manually start peer
configruation. Please note that if the **auto_switch** is 1, the ldmsd
will automatically stop peer configuration when it receives the echo
from the peer.

**failover_peercfg_stop** is a command to manually stop peer
configuration. Please note that if the **auto_switch** is 1, the ldmsd
will automatically start peercfg when the echo has timed out.

FAILOVER: AUTOMATIC PEERCFG ACTIVATION
======================================

The peer configuration is automatically activated when an echo-timeout
event occurred (with \`auto_switch=1\`). The echo-timeout is calculated
based on ping interval, ping-echo round-trip time, \`timeout_factor\`
and moving standard deviation of ping-echo round-trip time as follows:

rt_time[N] is an array of last N ping-echo round-trip time.

base = max( max(rt_time), ping_interval ) timeout1 = base + 4 \*
SD(rt_time) timeout2 = base*timeout_factor

timeout = max( timeout1, timeout2 )

EXAMPLES
========

Let's consider the following setup:

::

                           .-------.
                           |  a20  |
                           |-------|
                           | s00/a |
                           | s00/b |
                           | s01/a |
                           | s01/b |
                           | s02/a |
                           | s02/b |
                           | s03/a |
                           | s03/b |
                           '-------'
                               ^
                               |
                   .-----------'-----------.
                   |                       |
               .-------.               .-------.
               |  a10  |               |  a11  |
               |-------|               |-------|
               | s00/a |      pair     | s02/a |
               | s00/b |...............| s02/b |
               | s01/a |               | s03/a |
               | s01/b |               | s03/b |
               '-------'               '-------'
                   ^                       ^
                   |                       |
              .----'---.                 .-'------.
              |        |                 |        |
          .-------..-------.         .-------..-------.
          |  s00  ||  s01  |         |  s02  ||  s03  |
          |-------||-------|         |-------||-------|
          | s00/a || s01/a |         | s02/a || s03/a |
          | s00/b || s01/b |         | s02/b || s03/b |
          '-------''-------'         '-------''-------'

In this setup, we have 4 sampler daemons (*s00* - *s03*), 2 level-1
aggregator (*a10*, *a11*), and 1 level-2 aggregator (*a20*). Each
sampler daemon contain set *a* and set *b*, which are prefixed by the
sampler daemon name. The level-1 aggregators are configured to be a
failover pair, aggregating sets from the sampler daemons as shown in the
picture. And the level-2 aggregator is configured to aggregate sets from
the level-1 aggregators.

The following is a list of configuration and CLI options to achieve the
setup shown above:

::

   # a20.cfg
   prdcr_add name=prdcr_a10 host=a10.hostname port=12345 xprt=sock \
             type=active interval=1000000
   prdcr_start name=prdcr_a10
   prdcr_add name=prdcr_a11 host=a11.hostname port=12345 xprt=sock \
             type=active interval=1000000
   prdcr_start name=prdcr_a11
   updtr_add name=upd interval=1000000 offset=0
   updtr_prdcr_add name=upd regex.*
   updtr_start upd

   # a10.cfg
   prdcr_add name=prdcr_s00 host=s00.hostname port=12345 xprt=sock \
             type=active interval=1000000
   prdcr_start name=prdcr_s00
   prdcr_add name=prdcr_s01 host=s01.hostname port=12345 xprt=sock \
             type=active interval=1000000
   prdcr_start name=prdcr_s01
   updtr_add name=upd interval=1000000 offset=0
   updtr_prdcr_add name=upd regex.*
   updtr_start upd
   failover_config host=a11.hostname port=12345 xprt=sock \
                         interval=1000000 peer_name=a11
   failover_start
   # a10 CLI
   $ ldmsd -c a10.cfg -x sock:12345 -n a10
                                   # name this daemon "a10"

   # a11.cfg
   prdcr_add name=prdcr_s02 host=s02.hostname port=12345 xprt=sock \
             type=active interval=1000000
   prdcr_start name=prdcr_s02
   prdcr_add name=prdcr_s03 host=s03 port=12345 xprt=sock \
             type=active interval=1000000
   prdcr_start name=prdcr_s03
   updtr_add name=upd interval=1000000 offset=0
   updtr_prdcr_add name=upd regex.*
   updtr_start upd
   failover_config host=a10.hostname port=12345 xprt=sock \
                         interval=1000000 peer_name=a10
   failover_start
   # a11 CLI
   $ ldmsd -c a11 -x sock:12345 -n a11
                                   # name this daemon "a11"

   # sampler config are omitted (irrelevant).

With this setup, when *a10* died, *a11* will start aggregating sets from
*s00* and *s01*. When this is done, *a20* will still get all of the sets
through *a11* depicted in the following figure.

::

                           .-------.
                           |  a20  |
                           |-------|
                           | s00/a |
                           | s00/b |
                           | s01/a |
                           | s01/b |
                           | s02/a |
                           | s02/b |
                           | s03/a |
                           | s03/b |
                           '-------'
                               ^
                               |
                               '-----------.
                                           |
               xxxxxxxxx               .-------.
               x  a10  x               |  a11  |
               x-------x               |-------|
               x s00/a x               | s00/a |
               x s00/b x               | s00/b |
               x s01/a x               | s01/a |
               x s01/b x               | s01/b |
               xxxxxxxxx               | s02/a |
                                       | s02/b |
                                       | s03/a |
                                       | s03/b |
                                       '-------'
                                           ^
                                           |
              .--------.-----------------.-'------.
              |        |                 |        |
          .-------..-------.         .-------..-------.
          |  s00  ||  s01  |         |  s02  ||  s03  |
          |-------||-------|         |-------||-------|
          | s00/a || s01/a |         | s02/a || s03/a |
          | s00/b || s01/b |         | s02/b || s03/b |
          '-------''-------'         '-------''-------'

When *a10* heartbeat is back, *a11* will stop its producers/updaters
that were working in place of *a10*. The LDMS network is then recovered
back to the original state in the first figure.

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`
