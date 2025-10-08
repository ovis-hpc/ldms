.. _ldmsd_config_files:

==================
ldmsd_config_files
==================

------------------------------------
Manual for LDMSD Configuration Files
------------------------------------

:Date: 08 October 2025
:Manual section: 7
:Manual group: LDMSD


SYNOPSIS
========

ldmsd [-c <CONFIG_PATH>] [-c <CONFIG_PATH> ..]


DESCRIPTION
===========

LDMSD configuration files provide a maintainable way to define the initial state
and behavior of an LDMS daemon. These files contain configuration commands
that are processed sequentially at daemon startup.

SPECIFYING CONFIGURATION FILES
===============================

Configuration files are specified using the **-c** command-line option:

::

        ldmsd -c /path/to/config_file

Multiple configuration files can be provided by repeating the **-c** option:

::

        ldmsd -c /path/to/config_path_1 -c /path/to/config_file_2

When multiple configuration files are given, they are processed in
the order specified on the command line.

ENVIRONMENT VARIABLES
=====================

Configuration files support defining and referencing environment variables,
enabling parameterized configurations.

Defining Environment Variables
-------------------------------

Use the **env** command to define an environment variable within a configuration file:

::

        env <name>=<value>

Example:

::

        env MYNAME=node-1
        env MYCOMPID=1

Referencing Environment Variables
----------------------------------

Reference environment variables using the syntax **${<env_name>}**:

::

        config name=meminfo producer=${MYNAME} instance=${MYNAME}/meminfo component_id=${MYCOMPID}

Environment variables can be:

* Defined within the configuration file using the **env** command
* Already present in the environment when ldmsd starts

**Important**: If a referenced environment variable does not exist (neither defined
in the configuration file nor in the environment), it will be substituted with
an empty string. This behavior is the same as shell variable expansion.

CONFIGURATION COMMANDS
======================

Configuration files can contain two types of commands:

1. **Initialization commands** - Commands that set up the initial state of LDMS daemon.

2. **Operational commands** - Commands that define the work LDMS daemon needs to perform,
   such as loading plugins, configuring plugins, starting samplers, connecting to
   other LDMS daemons, and defining storage policies. For a complete list
   of these commands, see :ref:`ldmsd_controller(8) <ldmsd_controller>`.

Commands to Initialize LDMS Daemon
-----------------------------------

The following commands are equivalent to command-line options and are used
to initialize LDMS daemon settings. These commands provide the same functionality
as command-line options but in a more readable and maintainable format.

**log_file** sets the log file path.

   path=PATH
      The log file path

**log_level** sets the log verbosity. The default is ERROR.

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

   [auth_attr=attr_value]
      The attribute-value pairs of the authentication plugin

**auth_add** defines an additional authentication domain.

   name=NAME
      The authentication domain name

   plugin=PI_NAME
      The authentication plugin name

   [auth_attr=attr_value]
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

**stream_enable** enables stream communication in the daemon.

   Stream communication is disabled by default and must be explicitly enabled when needed.

   No Parameters

**msg_enable** enables LDMS message functionality in the daemon.

   LDMS message is disabled by default and must be explicitly enabled when needed.

   No Parameters

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


COMMAND PROCESSING ORDER
========================

LDMS daemons process configuration commands in a specific order to ensure
proper initialization. Understanding this order is important when working with
multiple configuration files or when dependencies exist between commands.

Priority Commands
-----------------

The following commands are processed **before** all other commands, regardless of
where they appear in the configuration file(s):

* **env** - Environment variable definitions
* Initialization commands listed in the **Commands to Initialize LDMS Daemon** section
* **listen** - Listen endpoint definitions
* **auth_add** - Additional authentication domains
* **stream_enable** - Enable Stream functionality
* **msg_enable** - Enable Message functionality

Processing Order with Multiple Configuration Files
---------------------------------------------------

When multiple configuration files are specified with **-c**, the processing order is:

1. **Priority commands** are extracted from all configuration files and processed first:

   a. All Priority commands from the first configuration file are processed in the order they appear in that file
   b. Then all Priority commands from the second configuration file are processed in order
   c. This continues for all configuration files

2. **All other commands** are then processed:

   a. Remaining commands from the first configuration file are processed in order
   b. Then remaining commands from the second configuration file
   c. This continues for all configuration files

Deferred Commands
-----------------

Some commands are deferred and executed after all configuration files have
been processed:

* **prdcr_start** - Start producers
* **updtr_start** - Start updaters
* **strgp_start** - Start storage policies
* **failover_start** - Start failover service (**DEPRECATED**)

If **failover_start** is present, the failover service will start first
among the deferred commands. Upon failover pairing success or failure,
the other deferred configuration objects will be started.

**Note 1**: While failover service is in use, prdcr, updtr, and strgp cannot
be altered (start, stop, or reconfigure) over in-band configuration.
**Note 2**: The failover functionality is deprecated and will be removed in a future release/

EXAMPLES
========

Sampler Configuration
---------------------

A simple sampler daemon configuration (samplerd.conf):

::

        > cat samplerd.conf
        # Daemon settings
        log_file path=/var/log/ldmsd_samplerd.log
        set_memory size=512M

        # Authentication and listen endpoint
        default_auth plugin=munge
        listen xprt=sock port=10001

        # Environment variables for plugin configuration
        env SAMP_INTERVAL=1s

        # Load and configure meminfo sampler plugin
        load name=meminfo
        config name=meminfo producer=${HOSTNAME} instance=${HOSTNAME}/meminfo component_id=${MYCOMPID}
        start name=meminfo interval=${SAMP_INTERVAL}

        > export MYCOMPID=1; ldmsd -c samplerd.conf


Aggregator Configuration
------------------------

A simple aggregator daemon configuration (agg.conf):

::

        > cat agg.conf
        # Daemon settings
        log_file path=/var/log/ldmsd_agg.log
        set_memory size=1G

        # Authentication and listen endpoint
        default_auth plugin=munge
        listen xprt=sock port=10001

        # Environment variables
        env LDMS_XPRT=sock
        env RECONNECT=5s
        env UPDT_INTERVAL=1s

        # Define producers to aggregate from
        prdcr_add name=node-1 port=10001 xprt=${LDMS_XPRT} host=node-1 type=active reconnect=${RECONNECT}
        prdcr_start_regex regex=.*

        # Define updater to collect metrics
        updtr_add name=all interval=${UPDT_INTERVAL} offset=100ms
        updtr_prdcr_add name=all regex=.*
        updtr_start name=all

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_authentication(7) <ldms_authentication>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`