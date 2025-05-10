.. _ldmsd:

=====
ldmsd
=====

---------------------
Start an ldms daemon
---------------------

:Date: 28 Feb 2018
:Manual section: 8
:Manual group: LDMSD

SYNOPSIS
========

ldmsd [OPTION...]

DESCRIPTION
===========

The ldmsd command is used to start an instance of an ldmsd server.
Configuration of the ldmsd is accomplished statically with a
configuration file provided on the command line, or dynamically with the
ldmsd_controller or distributed configuration management server Maestro.

ENVIRONMENT
===========

The following environment variables may be set to override compiled-in defaults:
--------------------------------------------------------------------------------

ZAP_LIBPATH
   Path to the location of the LDMS transport libraries.

LDMSD_PLUGIN_LIBPATH
   Path to the location of the LDMS plugin libraries.

LDMSD_PIDFILE
   Full path name of a file overriding the default of
   /var/run/ldmsd.pid. The command line argument "-r pid-file-path"
   takes precedence over this value.

LDMSD_LOG_TIME_SEC
   If present, log messages are stamped with the epoch time rather than
   the date string. This is useful when sub-second information is
   desired or correlating log messages with other epoch-stamped data.

LDMSD_MEM_SZ
   The size of memory reserved for metric sets. Set this variable or
   specify "-m" to ldmsd. See the -m option for further details. If both
   are specified, the -m option takes precedence over this environment
   variable.

LDMSD_UPDTR_OFFSET_INCR
   The increment to the offset hint in microseconds for updaters that
   determine the update interval and offset automatically. For example,
   if the offset hint is 100000, the updater offset will be 100000 +
   LDMSD_UPDTR_OFFSET_INCR. The default is 100000 (100 milliseconds).

CRAY Specific Environment variables for ugni transport
------------------------------------------------------

ZAP_UGNI_PTAG
   For XE/XK, the PTag value as given by apstat -P. For XC, The value
   does not matter but the environment variable must be set.

ZAP_UGNI_COOKIE
   For XE/XK, the Cookie value corresponding to the PTag value as given
   by apstat -P For XC, the Cookie value (not Cookie2) as given by
   apstat -P

ZAP_UGNI_CQ_DEPTH
   Optional value for the CQ depth. The default is 2048.

ZAP_UGNI_STATE_INTERVAL
   Optional. If set, then ldmsd will check all nodes' states via rca
   interface. States for all nodes are checked and stored at intervals
   determined by this environment variable. The stored values are
   checked against before contacting a node. If you choose to use this
   option, then the rule of thumb is to set ZAP_UGNI_STATE_INTERVAL and
   ZAP_UGNI_STATE_OFFSET such that the node states are checked before
   the metric set update occurs (see interval and offset in
   ldmsd_controller)

ZAP_UGNI_STATE_OFFSET
   Optional. Only relevant if ZAP_UGNI_STATE_INTERVAL is set. Defaults
   to zero. Offset from zero for checking the nodes state (see
   ZAP_UGNI_STATE_INTERVAL, above).

OPTIONS
=======

General/Configuration Options:
------------------------------

**-c** *CONFIG_PATH*
   The path to configuration file (optional, default: <none>). The
   configuration file contains a batch of ldmsd controlling commands,
   such as \`load\` for loading a plugin, and \`prdcr_add\` for defining
   a ldmsd producer to aggregate from (see **ldmsd_controller**\ (8) for
   a complete list of commands, or simply run **ldmsd_controller** then
   **help**). The commands in the configuration file are executed
   sequentially, except for **prdcr_start**, **updtr_start**,
   **strgp_start**, and **failover_start** that will be deferred. If
   **failover_start** is present, the failover service will start first
   (among the deferred). Then, upon failover pairing success or failure,
   the other deferred configuration objects will be started. Please also
   note that while failover service is in use, prdcr, updtr, and strgp
   cannot be altered (start, stop, or reconfigure) over in-band
   configuration. See also REORDERED COMMANDS below.

**-y** *CONFIG_PATH*
   The path to a YAML configuration file (optional, default: <none>).
   The YAML configuration file contains a description of an entire
   cluster of LDMS daemons. Please see "man ldmsd_yaml_parser" for more
   information regarding the YAML configuration file.

**-m, --set_memory** *MEMORY_SIZE*
   |
   | MEMORY_SIZE is the maximum size of pre-allocated memory for metric
     sets. The given size must be less than 1 petabytes. For example,
     20M or 20mb are 20 megabytes. The default is adequate for most
     ldmsd acting in the collector role. For aggregating ldmsd, a rough
     estimate of preallocated memory needed is (Number of nodes
     aggregated) x (Number of metric sets per node) x 4k. Data sets
     containing arrays may require more. The estimate can be checked by
     enabling DEBUG logging and examining the mm_stat bytes_used+holes
     value at ldmsd exit.

**-n, --daemon_name** *NAME*
   |
   | The name of the LDMS daemon. By default it is "HOSTNAME:PORT". When
     configuring a LDMSD with a YAML configuration file, the
     "daemon_name" identifies a daemon defined in the configuration
     file. For more information about YAML configuration files, please
     see "man ldmsd_yaml_parser".

**-r, --pid_file** *pid_file*
   The path to the pid file and prefix of the .version banner file

**-V**
   Display LDMS version information and then exit.

**-u** plugin_name
   Display the usage for named plugin. Special names all, sampler, and
   store match all, sampler type, and store type plugins, respectively.

Communication Options:
----------------------

**-x** *XPRT:PORT:HOST*
   |
   | Specifies the transport type to listen on. May be specified more
     than once for multiple transports. The XPRT string is one of
     'rdma', 'sock', or 'ugni' (CRAY XE/XK/XC). A transport specific
     port number must be specified following a ':', e.g. rdma:10000. An
     optional host or address may be specified after the port, e.g.
     rdma:10000:node1-ib, to listen to a specific address.

   The listening transports can also be specified in the configuration
   file using **listen** command, e.g. \`listen xprt=sock port=1234
   host=node1-ib\`. Please see **ldmsd_controller**\ (8) section
   **LISTEN COMMAND SYNTAX** for more details.

**-a, --default_auth** *AUTH*
   Specify the default LDMS Authentication method for the LDMS
   connections in this process (when the connections do not specify
   authentication method/domain). Please see
   **ldms_authentication**\ (7) for more information. If this option is
   not given, the default is "none" (no authentication). Also see
   **ldmsd_controller**\ (8) section **AUTHENTICATION COMMAND SYNTAX**
   for how to define an authentication domain.

**-A, --default_auth_args** *NAME*\ **=**\ *VALUE*
   Passing the *NAME*\ =\ *VALUE* option to the LDMS Authentication
   plugin. This command line option can be given multiple times. Please
   see **ldms_authentication**\ (7) for more information, and consult
   the plugin manual page for plugin-specific options.

Log Verbosity Options:
----------------------

**-l, --log_file** *LOGFILE*
   |
   | LOGFILE is the path to the log file for status messages. Default is
     stdout unless given. The syslog facility is used if LOGFILE is
     exactly "syslog". Silence can be obtained by specifying /dev/null
     for the log file or using command line redirection as illustrated
     below.

**-v, --log_level** *LOG_LEVEL*
   |
   | LOG_LEVEL can be one of DEBUG, INFO, WARN, ERROR, CRITICAL or
     QUIET. The default level is ERROR. QUIET produces only
     user-requested output.

**-L,**\ *--log_config* **<CINT:PATH> \| <CINT> \| <PATH>**
   |
   | Append configuration replay messages or configuration debugging
     messages to the log indicated by -l (when PATH is omitted) or to
     the file named PATH. Bit values of CINT correspond to:

::

         0: no messages
         1: debug messages from the generic 'request' handler
         2: config history messages in replayable format
         4: query history messages in replayable format
         8: failover debugging messages
        16: include delta time prefix when using PATH
        32: include epoch timestamp prefix when using PATH

These values may be added together to enable multiple outputs. All
messages are logged at the user-requested level, LDMSD_LALL. CINT values
2, 26 and 27 are often interesting. When CINT is omitted, 1 is the
default. When PATH is used, the log messages are flushed to as they are
generated.

SPECIFYING COMMAND-LINE OPTIONS IN CONFIGURATION FILES
======================================================

While command-line options are useful for quick configuration, complex
setups or repeated deployments benefit from configuration files. These
files provide a centralized location to define all initial settings for
LDMSD, promoting readability, maintainability, and easy sharing across
deployments. This section serves as a reference for configuration
commands used in these files. These commands offer an alternative
approach to specifying the initial state of LDMSD compared to using
command-line options

Configuration commands to initialize LDMSD
------------------------------------------

**log_file** sets the log file path.

   path=PATH
      The log file path

**log_level** sets the log verbosify. The default is ERROR.

   level=LEVEL
      The log level ordered from the most to the least severity:
      CRITICAL, ERROR, WARNING, INFO, and DEBUG.

**set_memory** sets the total set memory. The default is 512 MB.

   size=SIZE
      The total set memory size.

**pid_file** sets the path to the PID file.

   path=PATH
      The PID file path

**banner** specifies the banner mode.

   mode=0|1|2
      0 means no banner; 1 means auto-deleting the banner file at exit;
      and 2 means leaving the banner file.

**worker_threads** sets the number of threads scheduling sample and
update events.

   num=NUM
      Number of threads that are responsible for scheduling sample, dir,
      lookup, and update events.

**default_auth** defines the default authentication domain. The default
is no authentication.

   plugin=NAME
      The authentication plugin name

   [auth_attr=ttr_value]
      The attribute-value pairs of the authentication plugin

**auth_add** defines an additional authentication domain.

   name=NAME
      The authentication domain name

   plugin=PI_NAME
      The autnentication plugin name

   [auth_attr=ttr_value]
      The attribute-value pairs of the authentication plugin

**listen** defines a listen endpoint.

   xprt=XPRT
      Endpoint transport: sock, rdma, ugni

   port=PORT
      Listening port

   [host=HOST]
      Listening host

   [auth=AUTH]
      Authentication domain. The default authentication domain is used
      if none is specified.

**default_quota** sets the receiving quota in bytes

   quota=BYTES
      The quota limit in bytes

**publish_kernel** enables LDMSD to publish kernel metrics and specifies
the kernel metric file.

   path=PATH
      The path to the kernel metric file

**daemon_name** sets the LDMS process name.

   name=NAME
      LDMS process name

'option' configuration command to set the command-line options
--------------------------------------------------------------

Apart from the configuration commands above, the configuration command
'option' can be used to specify the command-line option.

   option <COMMAND-LINE OPTIONS>

   **-a,**\ *--default_auth*
   **-A,**\ *--default_auth_args*
   **-B,**\ *--banner*
   **-k,**\ *--publish_kernel*
   **-l,**\ *--log_file* **PATH**
   **-m,**\ *--set_memory*
   **-n,**\ *--daemon_name*
   **-P,**\ *--worker_threads*
   **-r,**\ *--pid_file*
   **-s,**\ *--kernel_set_path*
   **-v,**\ *--log_level*
   **-L,**\ *--log_config* **<CINT[:PATH]>**

Specifying the listen endpoints in configuraton files
-----------------------------------------------------

Users can use the 'listen' command to define the listen endpoints. For example,
   listen xprt=sock port=411

Example
-------

> cat ldmsd.conf

::

     # cmd-line options
     log_file path=/opt/ovis/var/ldmsd.log
     log_level level=ERROR
     set_memory size=2GB
     worker_threads num=16
     default_auth plugin=munge
     listen xprt=ugni port=411
     # meminfo
     load name=meminfo
     config name=meminfo producer=nid0001 instance=nid0001/meminfo
     start name=meminfo interval=1000000 offset=0

RUNNING LDMSD ON CRAY XE/XK/XC SYSTEMS USING APRUN
==================================================

ldsmd can be run as either a user or as root using the appropriate PTag
and cookie.

Check (or set) the PTag and cookie.

   Cray XE/XK Systems:

   ::

      > apstat -P
      PDomainID           Type    Uid   PTag     Cookie
      LDMS              system      0     84 0xa9380000
      foo               user    22398    243  0x2bb0000

      Cray XC Systems:
      > apstat -P
      PDomainID   Type   Uid     Cookie    Cookie2
      LDMS      system     0 0x86b80000          0
      foo         user 20596 0x86bb0000 0x86bc0000

   Set the environment variables ZAP_UGNI_PTAG and ZAP_UGNI_COOKIE with
   the appropriate ptag and cookie.

   Run ldmsd directly or as part of a script launched from aprun. In
   either case, Use aprun with the correct -p <ptag> when running.

REORDERED COMMANDS
==================

Certain commands in are reordered when processing input scripts
specified with -c or -y. Items related to failover are handled as
described in the '-c' and '-y' sections above. Other commands are
promoted to run before any non-promoted commands from the loaded script.
In particular, env, loglevel, listen, auth, and option are promoted.

NOTES
=====

OCM flags are unsupported at this time.

BUGS
====

None known.

EXAMPLES
========

::

   $/tmp/opt/ovis/sbin/ldmsd -x sock:60000 -p unix:/var/run/ldmsd/metric_socket -l /tmp/opt/ovis/logs/1


   $/tmp/opt/ovis/sbin/ldmsd -x sock:60000 -p sock:61000 -p unix:/var/runldmsd/metric_socket

SEE ALSO
========

:ref:`ldms_authentication(7) <ldms_authentication>`, :ref:`ldmsctl(8) <ldmsctl>`, :ref:`ldms_ls(8) <ldms_ls`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`
