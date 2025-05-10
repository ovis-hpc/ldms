.. _app_sampler:

==================
app_sampler
==================

-------------------------
LDMSD app_sampler plugin
-------------------------

:Date:   30 Sep 2019
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

**config** **name=app_sampler** **producer=**\ *PRODUCER*
**instance=**\ *INSTANCE* [ **schema=\ SCHEMA** ] [
**component_id=\ COMPONENT_ID** ] [ **stream=\ STREAM_NAME** ] [
**metrics=\ METRICS** ] [ **cfg_file=\ PATH** ]

DESCRIPTION
===========

**``app_sampler``** collects metrics from **``/proc/<PID>``** according
to current SLURM jobs/tasks running on the system. **``app_sampler``**
depends on **``slurm_notifier``** SPANK plugin to send SLURM job/task
events over **``ldmsd_stream``** (**``stream``** option, default:
slurm). A set is created per task when the task started in the following
format: **``PRODUCER_NAME/JOB_ID/TASK_PID``**. The set is deleted when
the task exited.

By default **``app_sampler``** sampling all available metrics (see
**``LIST OF METRICS``** section). Users may down-select the list of
metrics to monitor by specifying **``metrics``** option (comma-separated
string) or writing a JSON configuration file and specifying
**``cfg_file``** option (see **``EXAMPLES``** section).

CONFIG OPTIONS
==============

-  **name**

        Must be app_sampler.

-  **producer**

        The name of the data producer (e.g. hostname).

-  **instance**

        This is required by sampler_base but is not used by app_sampler. So,
        this can be any string but must be present.

-  **schema**

        The optional schema name (default: app_sampler).

-  **component_id**

        An integer identifying the component (default: *0*).

-  **stream**

        The name of the **``ldmsd_stream``** to listen for SLURM job events.
        (default: slurm).

-  **metrics**

        The comma-separated list of metrics to monitor. The default is ''
        (empty), which is equivalent to monitor ALL metrics.

-  **cfg_file**

        The alternative config file in JSON format. The file is expected to
        have an object that may contain the following attributes:

..

   ::


              {
                      'stream': 'STREAM_NAME'
                      'metrics': [ METRICS ]
              }

The default values are assumed for the attributes that are not
specified. Attributes other than 'stream' and 'metrics' are ignored.

If the **``cfg_file``** is given, **``stream``** and **``metrics``**
options are ignored.

LIST OF METRICS
===============

   ::

      /* from /proc/[pid]/cmdline */
      cmdline_len,
      cmdline,

      /* the number of open files */
      n_open_files,

      /* from /proc/[pid]/io */
      io_read_b,
      io_write_b,
      io_n_read,
      io_n_write,
      io_read_dev_b,
      io_write_dev_b,
      io_write_cancelled_b,

      /* /proc/[pid]/oom_score */
      oom_score,

      /* /proc/[pid]/oom_score_adj */
      oom_score_adj,

      /* path of /proc/[pid]/root */
      root,


      /* /proc/[pid]/stat */
      stat_pid,
      stat_comm,
      stat_state,
      stat_ppid,
      stat_pgrp,
      stat_session,
      stat_tty_nr,
      stat_tpgid,
      stat_flags,
      stat_minflt,
      stat_cminflt,
      stat_majflt,
      stat_cmajflt,
      stat_utime,
      stat_stime,
      stat_cutime,
      stat_cstime,
      stat_priority,
      stat_nice,
      stat_num_threads,
      stat_itrealvalue,
      stat_starttime,
      stat_vsize,
      stat_rss,
      stat_rsslim,
      stat_startcode,
      stat_endcode,
      stat_startstack,
      stat_kstkesp,
      stat_kstkeip,
      stat_signal,
      stat_blocked,
      stat_sigignore,
      stat_sigcatch,
      stat_wchan,
      stat_nswap,
      stat_cnswap,
      stat_exit_signal,
      stat_processor,
      stat_rt_priority,
      stat_policy,
      stat_delayacct_blkio_ticks,
      stat_guest_time,
      stat_cguest_time,
      stat_start_data,
      stat_end_data,
      stat_start_brk,
      stat_arg_start,
      stat_arg_end,
      stat_env_start,
      stat_env_end,
      stat_exit_code,

      /* from /proc/[pid]/status */
      status_name,
      status_umask,
      status_state,
      status_tgid,
      status_ngid,
      status_pid,
      status_ppid,
      status_tracerpid,
      status_uid,
      status_real_user,
      status_eff_user,
      status_sav_user,
      status_fs_user,
      status_gid,
      status_real_group,
      status_eff_group,
      status_sav_group,
      status_fs_group,
      status_fdsize,
      status_groups,
      status_nstgid,
      status_nspid,
      status_nspgid,
      status_nssid,
      status_vmpeak,
      status_vmsize,
      status_vmlck,
      status_vmpin,
      status_vmhwm,
      status_vmrss,
      status_rssanon,
      status_rssfile,
      status_rssshmem,
      status_vmdata,
      status_vmstk,
      status_vmexe,
      status_vmlib,
      status_vmpte,
      status_vmpmd,
      status_vmswap,
      status_hugetlbpages,
      status_coredumping,
      status_threads,
      status_sig_queued,
      status_sig_limit,
      status_sigpnd,
      status_shdpnd,
      status_sigblk,
      status_sigign,
      status_sigcgt,
      status_capinh,
      status_capprm,
      status_capeff,
      status_capbnd,
      status_capamb,
      status_nonewprivs,
      status_seccomp,
      status_speculation_store_bypass,
      status_cpus_allowed,
      status_cpus_allowed_list,
      status_mems_allowed,
      status_mems_allowed_list,
      status_voluntary_ctxt_switches,
      status_nonvoluntary_ctxt_switches,

      /* /proc/[pid]/syscall */
      syscall,

      /* /proc/[pid]/timerslack_ns */
      timerslack_ns,

      /* /proc/[pid]/wchan */
      wchan,

BUGS
====

No known bugs.

EXAMPLES
========

Example 1
---------

Get everyting:

   ::

      config name=app_sampler

Example 2
---------

Down-select and with non-default stream name:

   ::

      config name=app_sampler metrics=stat_pid,stat_utime stream=mystream

Example 3
---------

Down-select using config file, using default stream:

   ::

      config name=app_sampler cfg_file=cfg.json

..

   ::

      # cfg.json
      {
        "metrics" : [
           "stat_pid",
           "stat_utime"
        ]
      }

NOTES
=====

Some of the optionally collected data might be security sensitive.

The status_uid and status_gid values can alternatively be collected as
"status_real_user", "status_eff_user", "status_sav_user",
"status_fs_user", "status_real_group", "status_eff_group",
"status_sav_group", "status_fs_group". These string values are most
efficiently collected if both the string value and the numeric values
are collected.

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`,
:ref:`ldms_sampler_base(7) <ldms_sampler_base>`, :ref:`proc(5) <proc>`,:ref:`sysconf(3) <sysconf>`,:ref:`environ(3) <environ>`
