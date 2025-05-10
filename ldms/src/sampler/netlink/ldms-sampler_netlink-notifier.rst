.. _netlink-notifier:

================
netlink-notifier
================

---------------------------------------------------------------------
Transmit Linux kernel netlink process life messages to ldmsd streams.
---------------------------------------------------------------------

:Date:   25 June 2021
:Manual section: 8
:Manual group: LDMS sampler


ldms-notify - systemd service

.. _netlink-notifier-synopsis

SYNOPSIS
========

ldms-netlink-notifier [OPTION...]

DESCRIPTION
===========

The netlink-notifier generates JSON message for ldmsd and JSON aware
LDMS samplers. Its messages are mostly compatible with those from the
slurm spank based notifier.

OPTIONS
=======

::

   -c    use task comm field for process name.
   -d    strip off directory path from process name.
   -D    specify run duration in seconds. If unspecified, run forever.
   -e    select which events to monitor.
   -E    equivalent to -e all.
   -g    show glyphs for event types in debug mode.
   -h    show this help.
   -i seconds     time (float) to sleep between checks for processes exceeding the short dir filter time.
     If the -i value > the -m value, -i may effectively filter out additional processes.
   -j file     file to log json messages and transmission status.
   -l    force stdout line buffering.
   -L file    log to file instead of stdout.
   -r    run with real time FIFO scheduler (available on some kernels).
   -s    show short process name in debugging.
   -S    suppress stream message publication.
   -t    show debugging trace messages.
   -u umin    ignore processes with uid < umin
   -v lvl  log level for stream library messages. Higher is quieter. Error messages are >= 3.
   -q    run quietly
   -x    show extra process information.
   -X    equivalent to -Egrx.
   The ldmsd connection and commonly uninteresting or short-lived processes may be specified with the options or environment variables below.
   The 'short' options do not override the exclude entirely options.
   --exclude-programs[=]<path>     change the default value of exclude-programs
     When repeated, all values are concatenated.
     If given with no value, the default (nullexe):<unknown> is removed.
     If not given, the default is used unless
     the environment variable NOTIFIER_EXCLUDE_PROGRAMS is set.
   --exclude-dir-path[=]<path>     change the default value of exclude-dir-path
     When repeated, all values are concatenated.
     If given with no value, the default /sbin is removed.
     If not given, the default is used unless
     the environment variable NOTIFIER_EXCLUDE_DIR_PATH is set.
   --exclude-short-path[=]<path>     change the default value of exclude-short-path
     When repeated, all values are concatenated.
     If given with no value, the default /bin:/usr is removed.
     If not given, the default is used unless
     the environment variable NOTIFIER_EXCLUDE_SHORT_PATH is set.
   --exclude-short-time[=][val]     change the default value of exclude-short-time.
     If repeated, the last value given wins.
     If given with no value, the default 1 becomes 0 unless
     the environment variable NOTIFIER_EXCLUDE_SHORT_TIME is set.
   --stream[=]<val>     change the default value of stream.
     If repeated, the last value given wins.
     The default slurm is used if env NOTIFIER_LDMS_STREAM is not set.
   --xprt[=]<val>     change the default value of xprt.
     If repeated, the last value given wins.
     The default sock is used if env NOTIFIER_LDMS_XPRT is not set.
   --host[=]<val>     change the default value of host.
     If repeated, the last value given wins.
     The default localhost is used if env NOTIFIER_LDMS_HOST is not set.
   --port[=]<val>     change the default value of port.
     If repeated, the last value given wins.
     The default 411 is used if env NOTIFIER_LDMS_PORT is not set.
   --auth[=]<val>     change the default value of auth.
     If repeated, the last value given wins.
     The default munge is used if env NOTIFIER_LDMS_AUTH is not set.
   --reconnect[=]<val>     change the default value of reconnect.
     If repeated, the last value given wins.
     The default 600 is used if env NOTIFIER_LDMS_RECONNECT is not set.
   --timeout[=]<val>     change the default value of timeout.
     If repeated, the last value given wins.
     The default 1 is used if env NOTIFIER_LDMS_TIMEOUT is not set.
   --track-dir[=]<path>     change the pids published directory.
     The default is used if env NOTIFIER_TRACK_DIR is not set.
     The path given should be on a RAM-based file system for efficiency,
     and it should not contain any files except those created by
     this daemon. When enabled, track-dir will be populated even if
     -S is used to suppress the stream output.
   --component_id=<U64>     set the value of component_id.
     If not set, the component_id field is not included in the stream formats produced.
   --ProducerName=<name>    set the value of ProducerName
     If not set, the ProducerName field is not included in the stream formats produced.

ENVIRONMENT
===========

The following variables override defaults if a command line option is
not present, as describe in the options section.

::

   NOTIFIER_EXCLUDE_PROGRAMS="(nullexe):<unknown>"
   NOTIFIER_EXCLUDE_DIRS=/sbin
   NOTIFIER_EXCLUDE_SHORT_PATH=/bin:/usr
   NOTIFIER_EXCLUDE_SHORT_TIME=1
   NOTIFIER_TRACK_DIR=/var/run/ldms-netlink-tracked
   NOTIFIER_LDMS_RECONNECT=600
   NOTIFIER_LDMS_TIMEOUT=1
   NOTIFIER_LDMS_STREAM=slurm
   NOTIFIER_LDMS_XPRT=sock
   NOTIFIER_LDMS_HOST=localhost
   NOTIFIER_LDMS_PORT=411
   NOTIFIER_LDMS_AUTH=munge

Omitting (nullexe):<unknown> from NOTIFIER_EXCLUDE_PROGRAMS may cause
incomplete output related to processes no longer present. In exotic
circumstances, this may be desirable anyway.

FILES
=====

Users or other processes may discover which processes are the subject of
notifications by examining the files in

/NOTIFIER_TRACK_DIR/\*

For each pid started event which would be emitted to an LDMS stream, a
temporary file with the name of the pid is created in
NOTIFIER_TRACK_DIR. The file will contain the json event attempted. The
temporary file will be removed when the corresponding pid stopped event
is sent. These files are not removed when the notifier daemon exits.
Client applications may validate a file by checking the contents against
the /proc/$pid/stat content, if it exists. Invalid files should be
removed by clients or system scripts.

NOTES
=====

The core of this utility is derived from :ref:`forkstat(8) <forkstat>`.

The output of this utility, if used to drive a sampler, usually needs to
be consumed on the same node.

If not used with a sampler, the --component_id or --ProducerName options
are needed to add a node identifier to the messages. Normally a
process-following sampler that creates sets will add the node identifier
automatically.

Options are still in development. Several options affect only the trace
output.

EXAMPLES
========

Run for 30 seconds with screen and json.log test output connecting to
the ldmsd from 'ldms-static-test.sh blobwriter' test:

::

   netlink-notifier -t -D 30 -g -u 1 -x  -e exec,clone,exit  \
    -j json.log --exclude-dir-path=/bin:/sbin:/usr \
    --port=61061 --auth=none --reconnect=1"

Run in a typical deployment (sock, munge, port 411, localhost, forever,
10 minute reconnect):

::

   netlink-notifier

Run in a systemd .service wrapper, excluding root owned processes.

::

   EnvironmentFile=-/etc/sysconfig/ldms-netlink-notifier.conf
   ExecStart=/usr/sbin/ldms-netlink-notifier -u 1 -x -e exec,clone,exit

Run in a systemd .service wrapper, excluding root owned processes, with
debugging files

::

   EnvironmentFile=-/etc/sysconfig/ldms-netlink-notifier.conf
   ExecStart=/usr/sbin/ldms-netlink-notifier -u 1 -x -e exec,clone,exit -j /home/user/nl.json -L /home/user/nl.log -t --ProducerName=%H

SEE ALSO
========

:ref:`forkstat(8) <forkstat>`, :ref:`ldmsd(8) <ldmsd>`, :ref:`ldms-static-test(8) <ldms-static-test>`
