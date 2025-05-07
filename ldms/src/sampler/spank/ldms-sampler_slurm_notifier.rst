.. _slurm_notifier:

=====================
slurm_notifier
=====================

---------------------------------------------
Man page for the SPANK slurm_notifier plugin
---------------------------------------------

:Date:   30 Sep 2019
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

Within plugstack.conf: **required**
*OVIS_PREFIX*/*LIBDIR*/ovis-ldms/libslurm_notifier.so
**stream=**\ *STREAM_NAME* **timeout=**\ *TIMEOUT_SEC* **[user_debug]**
**client=**\ *XPRT*\ **:**\ *HOST*\ **:**\ *PORT*\ **:**\ *AUTH* ...

DESCRIPTION
===========

**slurm_notifier** is a SPANK plugin that notifies **ldmsd** about job
events (e.g. job start, job termination) and related information (e.g.
job_id, task_id, task process ID). The notification is done over
**ldmsd_stream** publish mechanism. See SUBSCRIBERS below for plugins
known to consume the spank plugin messages.

**stream=**\ *STREAM_NAME* specifies the name of publishing stream. The
default value is *slurm*.

**timeout=**\ *TIMEOUT_SEC* is the number of seconds determining the
time-out of the LDMS connections (default *5*).

**user_debug,** if present, enables sending certain plugin management
debugging messages to the user's slurm output. (default: disabled --
slurm_debug2() receives the messages instead).

**client=**\ *XPRT*\ **:**\ *HOST*\ **:**\ *PORT*\ **:**\ *AUTH*
specifies **ldmsd** to which **slurm_notifier** publishes the data. The
*XPRT* specifies the type of the transport, which includes **sock**,
**rdma**, **ugni**, and **fabric**. The *HOST* is the hostname or the IP
address that **ldmsd** resides. The *PORT* is the listening port of the
**ldmsd**. The *AUTH* is the LDMS authentication method that the
**ldmsd** uses, which are **munge**, or **none**. The **client** option
can be repeated to specify multiple **ldmsd**'s.

SUBSCRIBERS
===========

The following plugins are known to process slurm_notifier messages:

::

   slurm_sampler         (collects slurm job & task data)
   slurm_sampler2        (collects slurm job & task data)
   papi_sampler          (collects PAPI data from tasks identified)
   linux_proc_sampler    (collects /proc data from tasks identified)

EXAMPLES
========

/etc/slurm/plugstack.conf:

   ::

      required /opt/ovis/lib64/ovis-ldms/libslurm_notifier.so stream=slurm timeout=5 client=sock:localhost:10000:munge client=sock:node0:10000:munge

SEE ALSO
========

:ref:`spank(8) <spank>`, :ref:`slurm_sampler(7) <slurm_sampler>`,
:ref:`papi_sampler(7) <papi_sampler>`, :ref:`linux_proc_sampler(7) <linux_proc_sampler>`,
:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`,
