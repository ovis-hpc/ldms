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

.. _netlink-notifier-synopsis:

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

   -c	use task comm field for process name.
   -d	strip off directory path from process name.
   -D	specify run duration in seconds. If unspecified, run forever.
   -e	select which events to monitor.
   -E	equivalent to -e all.
   -g	show glyphs for event types in debug mode.
   -h	show this help.
   -i seconds	 time (float) to sleep between checks for processes exceeding the short dir filter time.
		 If the -i value > the -m value, -i may effectively filter out additional processes.
   -j file	 file to log json messages and transmission status.
   -J file	 file to dump json messages format examples to.
   -l	force stdout line buffering.
   -L file	log to file instead of stdout.
   -r	run with real time FIFO scheduler (available on some kernels).
   -s	show short process name in debugging.
   -S	suppress stream message publication.
   -t	show debugging trace messages.
   -u umin	ignore processes with uid < umin
   -v lvl  log level for stream library messages. Higher is quieter. Error messages are >= 3.
   -q	run quietly
   -x	show extra process information.
   -X	equivalent to -Egrx.
   The ldmsd connection and commonly uninteresting or short-lived processes may be specified with the options or environment variables below.
   The 'short' options do not override the exclude entirely options.
   --exclude-programs[=]<path>	 change the default value of exclude-programs
	 When repeated, all values are concatenated.
	 If given with no value, the default (nullexe):<unknown> is removed.
	 If not given, the default is used unless
	 the environment variable NOTIFIER_EXCLUDE_PROGRAMS is set.
   --exclude-dir-path[=]<path>	 change the default value of exclude-dir-path
	 When repeated, all values are concatenated.
	 If given with no value, the default /sbin is removed.
	 If not given, the default is used unless
	 the environment variable NOTIFIER_EXCLUDE_DIR_PATH is set.
   --exclude-short-path[=]<path>	 change the default value of exclude-short-path
	 When repeated, all values are concatenated.
	 If given with no value, the default /bin:/usr is removed.
	 If not given, the default is used unless
	 the environment variable NOTIFIER_EXCLUDE_SHORT_PATH is set.
   --exclude-short-time[=][val]	 change the default value of exclude-short-time.
	 If repeated, the last value given wins.
	 If given with no value, the default 1 becomes 0 unless
	 the environment variable NOTIFIER_EXCLUDE_SHORT_TIME is set.
   --stream[=]<val>	 change the default value of stream.
	 If repeated, the last value given wins.
	 The default slurm is used if env NOTIFIER_LDMS_STREAM is not set.
   --xprt[=]<val>	 change the default value of xprt.
	 If repeated, the last value given wins.
	 The default sock is used if env NOTIFIER_LDMS_XPRT is not set.
   --host[=]<val>	 change the default value of host.
	 If repeated, the last value given wins.
	 The default localhost is used if env NOTIFIER_LDMS_HOST is not set.
   --port[=]<val>	 change the default value of port.
	 If repeated, the last value given wins.
	 The default 411 is used if env NOTIFIER_LDMS_PORT is not set.
   --auth[=]<val>	 change the default value of auth.
	 If repeated, the last value given wins.
	 The default munge is used if env NOTIFIER_LDMS_AUTH is not set.
   --reconnect[=]<val>	 change the default value of reconnect.
	 If repeated, the last value given wins.
	 The default 600 is used if env NOTIFIER_LDMS_RECONNECT is not set.
   --timeout[=]<val>	 change the default value of timeout.
	 If repeated, the last value given wins.
	 The default 1 is used if env NOTIFIER_LDMS_TIMEOUT is not set.
   --track-dir[=]<path>     change the pids published directory.
	 The default is used if env NOTIFIER_TRACK_DIR is not set.
	 The path given should be on a RAM-based file system for efficiency,
	 and it should not contain any files except those created by
	 this daemon. When enabled, track-dir will be populated even if
	 -S is used to suppress the stream output.
   --purge-track-dir	if track-dir is set, purge any files there
	 which do not correspond to current processes.
	 Equivalently, NOTIFIER_PURGE_TRACK_DIR may be set.
   --component_id=<U64>     set the value of component_id.
	 If not set, the component_id field is not included in the stream formats produced.
   --ProducerName=<name>    set the value of ProducerName
	 If not set, the ProducerName field is not included in the stream formats produced.
   --format=N           change the format of messages to version N.
            If not set, the highest available format is used. See MESSAGE FORMATS.
   --jobid-file=FILE	look for job_id numbers in FILE. The default is not to look
	for a job id file if this option is not given nor NOTIFIER_JOBID_FILE is defined.
	See JOB ID FILES for details.

ENVIRONMENT
===========

The following variables override defaults if a command line option is
not present, as described in the options section.

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
   NOTIFIER_FORMAT=3
   NOTIFIER_HEARTBEAT=(none)
   NOTIFIER_PURGE_TRACK_DIR
   NOTIFIER_JOBID_FILE=(none)

Omitting ``(nullexe):<unknown>`` from **NOTIFIER_EXCLUDE_PROGRAMS** may cause
incomplete output related to processes no longer present. In exotic
circumstances, this may be desirable. The value of
**NOTIFIER_PURGE_TRACK_DIR** is not used to enable purge, just its presence.

FILES
=====

Users or other processes may discover which processes are the subject of
notifications by examining the files in

/NOTIFIER_TRACK_DIR/\*

For each pid started event which would be emitted to an LDMS stream, a
temporary file with the name of the pid is created in
**NOTIFIER_TRACK_DIR**. The file will contain the json event attempted. The
temporary file will be removed when the corresponding pid stopped event
is sent. These files are not removed when the notifier daemon exits, so
that they will be found after a restart. Client applications may
validate a file by checking the contents against the `/proc/$pid/stat`
content, if it exists. Invalid files should be removed by clients or
system scripts; the purge option is provided to optionally do this on
start.

JOB ID FILES
============

The job id file given must contain a list of **KEY=VALUE** pairs, one per
line. Lines starting with # are ignored. If the filename given is
`/search`, a list of default locations is checked
(`/var/run/ldms_jobinfo.data`, `/var/run/ldms.slurm.jobinfo`,
`/var/run/ldms.jobinfo`). A list of variables in the jobid file is
checked for, with the first found being used. The variable names checked
are: **JOBID**, **JOB_ID**, **LSB_JOBID**, **PBS_JOBID**, **SLURM_JOBID**, **SLURM_JOB_ID**.

MESSAGE FORMATS
===============

Message formats tuned to SLURM, LSF, and Linux without a batch scheduler
are published, based on what the notifier detects and the users choice
of ProducerName and component_id. The version of the tuned formats is
specified by number. If started with the ``-J`` option, an example of each
available message format it dumped to the specified file.

**Format 0** omits the start time from slurm process end messages (since it
is only sometimes known) and omits process duration, which depend on the
start time.

**Format 1** includes the start time for slurm process or the dummy value 0
when unknown) and includes process duration for all end messages. When
the start time is unavailable, duration of ``-1.0`` is published. Merging
data from other sources may allow durations flagged as ``-1`` to be computed
in some later data cleanup step.

**Format 2** extends process end messages with the executable name in field
``exe``. When this is not available, exe of ``/no-exe-data`` is published.
Merging data from other sources may allow exe flagged as ``/no-exe-data`` to
be computed in some later data cleanup step.

**Format 3** harmonizes schemas across linux, slurm, and lsf task types so
that all may be stored in common tables for ``task_exit`` and ``task_init``
events if slurm specific fields are omitted from the storage.

NOTES
=====

The core of this utility is derived from forkstat(8).

The output of this utility, if used to drive a sampler, usually needs to
be consumed on the same node.

If not used with a sampler, the ``--component_id`` or ``--ProducerName`` options
are needed to add a node identifier to the messages. Normally a
process-following sampler that creates sets will add the node identifier
automatically.

When the daemon is started after a process is started, the process start
time and therefore process duration may not be available. Similarly exe
may not be available. In message formats which report start time, 0
indicates data was unavailable. For processes without completely known
time bounds, the duration is reported as ``-1.0``. For processes without
known program paths, exe is reported as /no-exe-data.

Several options affect only the trace output.

The check for sufficient privilege occurs after ``-J`` and ``--help`` options
are processed.

EXAMPLES
========

To run for 30 seconds with screen and json.log test output connecting to
the ldmsd from 'ldms-static-test.sh blobwriter' test:

::

   netlink-notifier -t -D 30 -g -u 1 -x  -e exec,clone,exit  \
	-j json.log --exclude-dir-path=/bin:/sbin:/usr \
	--port=61061 --auth=none --reconnect=1

To run in a typical deployment (sock, munge, port 411, localhost,
forever, 10 minute reconnect):

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

forkstat(8), :ref:`ldmsd(8) <ldmsd>`, :ref:`ldms-static-test(8) <ldms-static-test>`
