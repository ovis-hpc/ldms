.. _ldms_barrier:

============
ldms_barrier
============

-----------------------------------------------------------
Run a self-contained multi-node, multiprocess wait barrier.
-----------------------------------------------------------

:Date: 13 June 2025
:Manual section: 8
:Manual group: LDMS tests

SYNOPSIS
========

Tagged participants barrier clients:

ldms_barrier [-v \*] -c <self-tag> -m <party count> -s <leader_host> [-t <timeout>] [-r <retry_interval_sec_float>] [-i <interface_name>] -p <PORT_NUM> <tag \*>

Tagged participants barrier leader:

ldms_barrier -l [-v \*] -c <self-tag> -m <party count> -s <leader_host> [-t <timeout>] [-r <retry_interval_sec_float>] [-i <interface_name>] -p <PORT_NUM> <tag \*>

Counted participants barrier clients:

ldms_barrier [-v \*] -m <party count> -s <leader_host> [-t <timeout>] [-r <retry_interval_sec_float>] [-i <interface_name>] -p <PORT_NUM>

Counted participants barrier leader:

ldms_barrier -l [-v \*] -m <party count> -s <leader_host> [-t <timeout>] [-r <retry_interval_sec_float>] [-i <interface_name>] -p <PORT_NUM>

DESCRIPTION
===========

The ldms_barrier command is used to start a group of processes which wait
until all have been accounted for (or a timeout is reached).

In counted mode, the leader waits for <party count> anonymous connections.

In tagged mode, the leader waits for N <= <party count> connections, where
each provides a unique tag from the list of tags given. Each client is told
its own tag, the <self-tag>. The leader may also be given a self tag for
scripting convenience.

Unless one or more -v options are given, ldms_barrier produces no output
until there is an error.

RETURN VALUE
============
On success, all processes will return 0 as soon as all have been accounted for
and notified by the leader to proceed.
On timeout or other detectable failure, all including the leader will return an error.

OPTION LIST
===========

**-c,--client-tag** *SELF_TAG*
   |
   | The unique name for this process (client or leader). A short string or
     number is recommended. If a client omits SELF_TAG in tagged mode,
     the barrier will timeout or wait forever. The leader may omit
     this option.

**-m,--max-clients** *PARTY_COUNT*
   |
   | The number of clients to wait for in counted mode. In tagged mode,
     PARTY_COUNT must be >= the number of participants including the leader.

**-t,--timeout** *TIMEOUT*
   |
   | (Optional) The approximate time to wait for all barrier activities to complete.
     The value is an integer in seconds. (default 1000000000)

**-r,--retry-interval** *SECONDS*
   |
   | (Optional) The delay between client connection retries, in seconds.
     Floating point (fractional seconds) are allowed. (default 0.1)

**-s,--server** *LEADER_HOST*
   Host where the leader will be running. (defaults to localhost)

**-p,--port** *PORT*
   PORT where the leader will be running on LEADER_HOST. (default 8080).

**-i,--interface** *NAME*
   (Optional) Network interface name on LEADER_HOST. (defaults to lo).

**-l,--leader**
   Run exactly this one process in the barrier party as the leader.

**-v,--verbose**
   Increase the verbosity of status and debugging output. (repeatable).

**-h,--help**
   Increase the verbosity of status and debugging output. (repeatable).

**-d,--debug-label** *LABEL*
   Change the labels in verbose output from HOST-pid-PID to user-defined string.


NOTES
=====

Because default linux behavior is to sit in TIME_WAIT on recently
used sockets, immediately rerunning a barrier on the same port
or at high port counts may fail with "Failed to bind: Address already in use."
The command:

ss -Hon state all "( sport = :$PORT )"

will reveal when this is the case.

Adapts to IPv4 or IPv6 based on what is discovered about the
LEADER_HOST and NAME.

This program is a tool for LDMS scale testing. It does not depend on
LDMS libraries.

BUGS
====

Diagnosis of misconfiguration requires using the -v option.
The default party count of 3 is not very useful.
Care must be taken in specifying -s and -i options consistently
between the leader and clients.

EXAMPLE
========

On the loopback interface:

::

        #! /bin/bash
        # clients, tags 2-N
        verbose="-v -v"
        PORT=10020
        HOST=localhost
        N=10
        TIMEOUT=5
        RETRY=0.5
        for i in $(seq 2 $N); do
        ldms_barrier $verbose -c $i -m $N -t $TIMEOUT -r $RETRY \
           -s $HOST -p $PORT $(seq $N) > client.log.$i &
        done
        # leader
        LEADERTAG=1
        ldms_barrier -l $verbose -i lo -c $LEADERTAG -m $N -t $TIMEOUT -r $RETRY \
           -s $HOST -p $PORT $(seq $N) > leader.log

SEE ALSO
========

:ref:`ldms_ldms-static-test(8) <ldms-static-test.sh>`, :ref:`ldms_pll-ldms-static-test(8) <pll-ldms-static-test.sh>`

socket(7)
