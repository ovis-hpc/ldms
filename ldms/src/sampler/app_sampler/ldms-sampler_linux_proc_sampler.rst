.. _linux_proc_sampler:

=========================
linux_proc_sampler
=========================

-----------------------------------------------
Man page for the LDMS linux_proc_sampler plugin
-----------------------------------------------

:Date:   15 Jul 2021
:Manual section: 7
:Manual group: LDMS sampler


SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| config name=linux_proc_sampler [common attributes] [stream=STREAM]
  [metrics=METRICS] [cfg_file=FILE] [instance_prefix=PREFIX]
  [exe_suffix=1] [argv_sep=<char>] [argv_msg=1] [argv_fmt=<1,2>]
  [env_msg=1] [env_exclude=EFILE] [fd_msg=1] [fd_exclude=EFILE]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. The linux_proc_sampler plugin provides data from
/proc/, creating a different set for each process identified in the
named stream. The stream can come from the ldms-netlink-notifier daemon
or the spank plugin slurm_notifier. The per-process data from
/proc/self/environ and /proc/self/cmdline can optionally be published to
streams.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The linux_proc_sampler plugin uses the sampler_base base class. This man
page covers only the configuration attributes, or those with default
values, specific to the this plugin; see :ref:`ldms_sampler_base(7) <ldms_sampler_base>` for the
attributes of the base class.

**config**
   | name=<plugin_name> [other options]
   | configuration line

   name=<plugin_name>
      |
      | This MUST be linux_proc_sampler.

   instance_prefix=PREFIX
      |
      | Prepend PREFIX to the set instance names. Typically a cluster
        name when needed to disambiguate producer names that appear in
        multiple clusters. (default: no prefix).

   exe_suffix=1
      |
      | If present, set instance names are appended with the full path
        of the executable. This is most likely useful for debugging
        configuration of the notifier up-stream using ldms_ls. (default:
        no such suffix)

   sc_clk_tck=1
      |
      | If present, include sc_clk_tck in the metric set. sc_clk_tck is
        the ticks per second from sysconf(_SC_CLK_TCK). (default: not
        included).

   stream=STREAM
      |
      | The name of the \`ldmsd_stream\` to listen for SLURM job events.
        (default: slurm).

   argv_sep=<char>
      |
      | Replace nul within the cmdline string with char. Special
        specifiers \\b,\n,\t,\v,\r,\f are also supported.

   syscalls=FILE
      |
      | File mapping syscall integers to symbolic names. Not needed
        unless syscall_name is included in the metrics. See FILES for
        details.

   metrics
      |
      | The comma-separated list of metrics to monitor. The default is
        (empty), which is equivalent to monitor ALL metrics.

   cfg_file=CFILE
      |
      | The alternative configuration file in JSON format. The file is
        expected to have an object that contains the following
        attributes: { "stream": "STREAM_NAME", "syscalls" : "/file",
        "metrics": [ comma-separated-quoted-strings ] }. If the
        \`cfg_file\` is given, all other sampler-specific options given
        on the key=value line are ignored.

   argv_msg=1
      |
      | Publish the argv items to a stream named <SCHEMA>_argv, where if
        the schema is not specified, the default schema is
        linux_proc_sampler. (Default: argv_msg=0; no publication of
        argv). E.g. a downstream daemon will need to subscribe to
        linux_proc_sampler_argv to receive the published messages and
        store them.

   argv_fmt=<1,2>
      |
      | Publish the argv items formatted as (1) a json list of strings
        ['argv0', 'argv1'] or (2) a json list of key/value tuples, e.g.
        [ {"k":0, "v":"argv[0]"}, {"k":1, "v":"argv[1]"}].

   env_msg=1
      |
      | Publish the environment items to a stream named <SCHEMA>_env,
        where if the schema is not specified, the default SCHEMA is
        linux_proc_sampler. (Default: env_msg=0; no publication of the
        environment). Environment data is published as a list in the
        style of argv_fmt=2. E.g. a downstream daemon will need to
        subscribe to linux_proc_sampler_env to receive the published
        messages and store them.

   env_exclude=ELIST
      |
      | Exclude the environment items named with regular expressions in
        ELIST. On the configuration key=value line, ELIST must be a file
        name of a file containing a list of regular expressions one per
        line. An environment variable that matches any of the listed
        regular expressions will be excluded. When used in the cfg_file,
        the env_exclude value may be either the string name of the
        regular expression file or a JSON array of expression strings as
        shown in EXAMPLES.

   fd_exclude=ELIST
      |
      | Exclude the files named with regular expressions in ELIST. On
        the configuration key=value line, ELIST must be a file name of a
        file containing a list of regular expressions one per line. A
        file that matches any of the listed regular expressions will be
        excluded. When used in the cfg_file, the fd_exclude value may be
        either the string name of the regular expression file or a JSON
        array of expression strings as shown in EXAMPLES.

   fd_msg=N
      |
      | Publish new /proc/pid/fd scan data to the <SCHEMA>_files stream
        every N-th sample, where if the schema is not specified, the
        default SCHEMA is linux_proc_sampler. (Default: fd_msg=0; no
        publication of the file details). A downstream daemon will need
        to subscribe to linux_proc_sampler_files to receive the
        published messages and store them. Files that are not opened
        long enough to be caught in a scan of fds will be missed. Files
        will be reported as 'opened' the first time seen and as 'closed'
        when they are no longer seen. A file both no longer seen and no
        longer existing will be reported as 'deleted'. Only regular
        files (not sockets, etc) are reported, and additionally files
        matching the fd_expressions are ignored. Use a larger N to
        reduce the scan overhead at the cost of missing short-access
        files. If a close-reopen of the same file occurs between scans,
        no corresponding events are generated.

   published_pid_dir=<path>
      |
      | Name of the directory where netlink-notifier or other notifier
        pids of interest may be found. This directory is scanned at
        sampler startup only, so that pids which were the subject of
        events published before the sampler started can be tracked. If
        not specified, the default directory is
        /var/run/ldms-netlink-tracked. Absence of this directory is not
        a sampler configuration error, as ldmsd may start before the
        notifier process. When starting, the sampler will clean up any
        stale pid references found in this directory. Any pid not
        appearing in this directory is not being tracked.

INPUT STREAM FORMAT
===================

The named ldmsd stream should deliver messages with a JSON format which
includes the following. Messages which do not contain event, data,
job_id, and some form of PID will be ignored. Extra fields will be
ignored.

::

   { "event" = "$e",
     "data" : {
    "job_id" : INT,
    "task_pid" : INT,
    "os_pid" : INT,
    "parent_pid" : INT,
    "is_thread" : INT,
    "exe" : STRING,
    "start" : STRING,
    "start_tick" : STRING
     }
   }

where $e is one of task_init_priv or task_exit. The data fields other
than job_id are all optional, but at least one of os_pid and task_pid
must contain the PID of a process to be monitored. If present and > 0,
task_pid should be the value taken from SLURM_TASK_PID or an equivalent
value from another resource management environment. The value of start,
if provided, should be approximately the epoch time ("%lu.%06lu") when
the PID to be monitored started.

OUTPUT STREAM FORMAT
====================

The json formatted output for argv and environment values includes a
common header:

::

   {
      "producerName":"localhost1",
      "component_id":1,
      "pid":8991,
      "job_id":0,
      "timestamp":"1663086686.947600",
      "task_rank":-1,
      "parent":1,
      "is_thread":0,
      "exe":"/usr/sbin/ldmsd",
      "data":[LIST]

where LIST is formatted as described for argv_fmt option.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=linux_proc_sampler
   config name=linux_proc_sampler producer=vm1_1 instance=vm1_1/linux_proc_sampler metrics=stat_comm,stat_pid,stat_cutime
   start name=linux_proc_sampler interval=1000000

An example metrics configuration file is:

::

   {
     "stream": "slurm",
     "instance_prefix" : "cluster2",
     "syscalls": "/etc/sysconfig/ldms.d/plugins-conf/syscalls.map",
     "env_msg": 1,
     "argv_msg": 1,
     "fd_msg" : 1,
     "fd_exclude": [
           "/dev/",
           "/run/",
           "/var/",
           "/etc/",
           "/sys/",
           "/tmp/",
           "/proc/",
           "/ram/tmp/",
           "/usr/lib"
       ],
     "env_exclude": [
    "COLORTERM",
    "DBU.*",
    "DESKTOP_SESSION",
    "DISPLAY",
    "GDM.*",
    "GNO.*",
    "XDG.*",
    "LS_COLORS",
    "SESSION_MANAGER",
    "SSH.*",
    "XAU.*"
       ],
     "metrics": [
       "stat_pid",
       "stat_state",
       "stat_rss",
       "stat_utime",
       "stat_stime",
       "stat_cutime",
       "stat_cstime",
       "stat_num_threads",
       "stat_comm",
       "n_open_files",
       "io_read_b",
       "io_write_b",
       "status_vmdata",
       "status_rssfile",
       "status_vmswap",
       "status_hugetlbpages",
       "status_voluntary_ctxt_switches",
       "status_nonvoluntary_ctxt_switches",
       "syscall_name"
     ]
   }

Generating syscalls.map:

::

   # ldms-gen-syscalls-map > /etc/sysconfig/ldms.d/plugins-conf/syscalls.map

Obtaining the currently supported optional metrics list:

::

   ldms-plugins.sh linux_proc_sampler

FILES
=====

Data is obtained from (depending on configuration) the following files
in /proc/[PID]/:

::

   cmdline
   exe
   statm
   stat
   status
   fd
   io
   oom_score
   oom_score_adj
   root
   syscall
   timerslack_ns
   wchan

The system call integer:name mapping varies with kernel and is therefore
read from an input file of the format:

::

   # comments
   0 read
    ...

where all lines are <int name> pairs. This file can be created from the
output of ldms-gen-syscall-map. System call names must be less than 64
characters. Unmapped system calls will be given names of the form
SYS_<num>.

The env_msg option can have its output filtered by json or a text file,
e.g.:

::

   # env var name regular expressions (all OR-d together)
   COLORTERM
   DBU.*
   DESKTOP_SESSION
   DISPLAY
   GDM.*
   GNO.*
   XDG.*
   LS_COLORS
   SESSION_MANAGER
   SSH.*
   XAU.*

The fd_msg option can have its output filtered by json or a text file,
e.g.:

::

   /dev/
   /run/
   /var/
   /etc/
   /sys/
   /tmp/
   /proc/
   /ram/tmp/
   /usr/lib64/
   /usr/lib/

The files defined with published_pid_dir appear in (for example)

::

   /var/run/ldms-netlink-tracked/[0-9]*

and each contains the JSON message sent by the publisher. Publishers,
not ldmsd, populate this directory to allow asynchronous startup.

NOTES
=====

The value strings given to the options sc_clk_tck and exe_suffix are
ignored; the presence of the option is sufficient to enable the
respective features.

Some of the optionally collected data might be security sensitive.

The publication of environment and cmdline (argv) stream data is done
once at the start of metric collection for the process. The message will
not be reemitted unless the sampler is restarted. Also, changes to the
environment and argv lists made within a running process are NOT
reflected in the /proc data maintained by the linux kernel. The
environment and cmdline values may contain non-JSON characters; these
will be escaped in the published strings.

The publication of file information via fd_msg information may be
effectively made one-shot-per-process by setting fd_msg=2147483647. This
will cause late-loaded plugin library dependencies to be missed,
however.

The status_uid and status_gid values can alternatively be collected as
"status_real_user", "status_eff_user", "status_sav_user",
"status_fs_user", "status_real_group", "status_eff_group",
"status_sav_group", "status_fs_group". These string values are most
efficiently collected if both the string value and the numeric values
are collected.

SEE ALSO
========

:ref:`syscalls(2) <syscalls>`, :ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`,
:ref:`ldms_sampler_base(7) <ldms_sampler_base>`, :ref:`proc(5) <proc>`, :ref:`sysconf(3) <sysconf>`, :ref:`environ(3) <environ>`.
