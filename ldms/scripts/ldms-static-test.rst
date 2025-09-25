.. _ldms-static-test:

================
ldms-static-test
================

---------------------------
Run a canned test scenario
---------------------------

:Date:   4 Oct 2020
:Manual section: 8
:Manual group: LDMS scripts

SYNOPSIS
========

ldms-static-test.sh -l

ldms-static-test.sh -h

ldms-static-test.sh <input_file> [test_dir]

DESCRIPTION
===========

The ldms-static-test.sh command starts a canned test defined in the
input_file using a standard environment. The input file is written in a
simple bash macro language described in LANGUAGE below. Supporting
configuration file fragments will be used, as determined from the input
file. See FILES below. This tests ldmsd run with static configuration
files (as would normally happen as a system service) and shut down with
a signal.

OPTIONS
=======

-l
   |
   | List the canned tests available.

-h
   |
   | List help message.

LANGUAGE
========

The following macro language is provided as extensions on bash. Other
bash use is also possible, but not recommended.

DAEMONS <daemon-numbers>
   |
   | Give all the numbers that will be used in the LDMSD invocations
     anywhere in the test. This causes port variables to be defined so
     that any daemon can connect to any other by referencing $portN as
     explained in ENVIRONMENT below. If omitted, the ordering and
     aggregation relationships of LDMSD calls may be infeasible.
     If only a single number N is given, it will be expanded to the range
     $(seq N).

SET_LOG_LEVEL <DEBUG|ERROR|INFO|ALL|QUIET|WARNING|CRITICAL>
   |
   | Set the generic log level for daemons started after
     calling SET_LOG_LEVEL.

FILECNT_LDMSD <daemon-numbers>
   |
   | Collect the list of open files from /proc/self/fd/ and print the
     total to stdout. See FILES below.

LDMSD [conf-options] <daemon-numbers>
   |
   | This starts a number of daemons described by daemon-numbers. The
     numbers can be an unquoted list, such as 1 2 3. The environment of
     each daemon (and its config script) will contain the variable i set
     to one of the given values, as described in ENVIRONMENT. For each
     value of i, a configuration fragment $input_file.$i must also
     exist. Use :ref:`seq(1) <seq>` to generate large number sequences.

See CONFIGURATION OPTIONS below for the explanation of [conf-options].

MESSAGE [arguments]
   |
   | The expanded arguments are logged.

LDMS_LS <daemon-numbers> [ldms_ls_args]
   |
   | This invokes ldms_ls on the k-th ldmsd daemons.
     Valgrind is applied if vgon is current.
     If more than one daemon is given, the list must be quoted.

LDMS_STATUS <daemon-numbers>
   |
   | This sends the status command to the k-th ldmsd daemons.
     using ldmsd_controller. Valgrind is applied if vgon is current.
     If more than one daemon is given, the list must be quoted.

FILECNT_LDMSD <daemon-numbers>
   |
   | This reports FileCount, Threads, VmRSS, VmSize from the
     k-th daemon process, as seen in /proc/$pid/[fd,status].
     Here the daemon-numbers list must not be unquoted.

KILL_LDMSD [strict] <daemon-numbers>
   |
   | Kills the listed daemons. If optional keyword strict is supplied,
     then missing daemons will cause the bypass variable to be set and
     the script to include an error code when it exits.

SLEEP <n>
   |
   | Sleeps n seconds and logs a message about it.

JOBDATA jobfile [daemon-numbers]
   |
   | Creates jobfile with data for the jobid plugin to parse. If daemon
     numbers are specified, creates a jobfile.$k for each value of k
     listed in daemon-numbers. Each file will have unique numeric
     values, sequentially increasing. This does not provide data in the
     slurm-plugin sampler binary format.

vgon
   |
   | Turns on use of valgrind for any ldmsd or ldms_ls subsequently
     started.

vgoff
   |
   | Turns off use of valgrind for any ldmsd or ldms_ls subsequently
     started.

file_created <filename>
   |
   | Verifies the existence and readability of filename.

rollover_created <filename>
   |
   | Verifies the existence and readability of rollover files matching
     pattern filename.[0-9]*.

bypass=<0,1>
   |
   | This variable assignment disables (1) or enables (0) all the macros
     described above. Typical use is to skip one or more operations
     while debugging a test script.

KILL_LDMSD_STRICT=<0,1>
   |
   | This variable allows the script author to control whether
     KILL_LDMSD is strict by default or not. If enabled (1), the script
     will exit with error code 1 following a failed KILL_LDMSD. If
     disabled (0) the script will suppress error codes from killing
     missing daemons. Typically used for debugging missing pid files and
     unexpectedly dead daemons. Supplying the keyword ‘strict’ before
     the numeric arguments to KILL_LDMSD also sets KILL_LDMSD_STRICT=1.

portbase=<K>
   |
   | The listening port numbers assigned to the daemons will be K+i,
     where i is as described for macro LDMSD. It is a good idea (to
     support automated testing) if portbase is set in <input_file> so
     that each test uses a unique range of ports. This enables tests to
     proceed in parallel.

Group Operations
-------------------

The following functions manage parallel and group-defined behaviors.

LDMS_AGG_WAIT <agg-daemon> <agg-timeout> <schema> <set-timeout> <daemon number>
   |
   | Wait until all daemons given have been seen to have the schema named present
     by a checker of the daemon running in the agg-daemon process rank. Waits
     up to agg-timeout seconds for the agg-daemon to appear and waits up to
     set-timeout for the sets to appear on that daemon.

LDMS_AGG_WAITSET_COUNT agg-daemon agg_timeout set-count set_timeout
   |
   | Wait until the agg-daemon given has been seen to have no more than
     set-count sets. Waits up to agg_timeout for the connection to agg-daemon
     to succeed. Waits up to set-timeout seconds for sets to disappear.
     This is a test that expected sets have disappeared after upstream daemons
     have been stopped.

CONFIGURATION OPTIONS
=====================

The LDMSD command macro supports either assembling a final configuration script
from raw input files or generating it from a YAML file (possibly also assembled).
The following options apply in either case:

-s <wait_microseconds>
   |
   | After an ldmsd is started, wait up to wait_microseconds checking
     for the daemon PID file to exist. The appropriate wait time is
     variable depending on the complexity of the configuration. If not
     specified, the default is 2 seconds wait time.

Generation from YAML
--------------------

For yaml configuration scripts, the options are -y, -Q, -R, -z below
and they are mutually exclusive of the raw configuration options.

-y
   |
   | Start the daemon using the corresponding generated yaml file.

-Q <prolog-file>
   |
   | Prepend the named prolog-file to the total yaml used.
     May be repeated.

-R <epilog-file>
   |
   | Append the named epilog-file to the total yaml used.
     May be repeated.

-S <variable>
   |
   | Append the named variable to the list of those that
     will be substituted in generated yaml. @variable@ is
     replaced with the current value. May be repeated.

-z
   |
   | Where multiple daemon numbers are specified, the input generated
     for the first number is cloned to all subsequent daemons. See
     FILES. This allows a single file to serve many similar daemon
     instances in scale testing.


Assembly from raw inputs
------------------------

For raw configuration scripts, the options are -p, -P, -c below
and they are mutually exclusive of the yaml configuration options.

The LDMSD command supports the following options for building
configurations from raw script pieces. Note that all -P
options are processed before all -p options in a single LDMSD call.

-p <prolog file>
   |
   | The prolog file is included before the usually expected input file.
     The location of prolog files is handled as are the test input
     files. See FILES below. Multiple -p options are allowed.

-P <looped-prolog-file,daemon-csl>
   |
   | The looped-prolog-file is included before the usually expected
     input file, once for each value in daemon-csl. Daemon-csl is a
     comma separated list of daemon numbers, e.g. a complete argument
     example is "-P producer,3,4,5". The variable ${j} is substituted
     with a daemon number from the list for each inclusion.

The location of looped prolog files is handled as are the test input
files. See FILES below. Multiple -P options are allowed.

-c
   |
   | Where multiple daemon numbers are specified, the input generated
     for the first number is cloned to all subsequent daemons. See
     FILES. This allows a single file to serve many similar daemon
     instances in scale testing.

ENVIRONMENT
===========

The following variables can be set in the script to affect the launch of
ldmsd:

LDMSD_EXTRA
   |
   | If set, these arguments are are appended to the ldmsd launch.
     Typical use is to specify "-m MEMSIZE" or other unusual arguments.
     The following flags are always determined for the user and must not
     be present in LDMSD_EXTRA: -x -c -l -v -r.

VG
   |
   | If valgrind is used (see vgon, vgoff), then $VG is the name of the
     debugging tool wrapped around the launch of ldmsd. The default is
     'valgrind'.

VGARGS
   |
   | If valgrind is used (see vgon, vgoff), then $VGARGS is appended to
     the default valgrind arguments.

VGTAG
   |
   | If valgrind is used (see vgon, vgoff), then $VGTAG is inserted in
     the valgrind output file name when defined. A good practice is for
     VGTAG to start with ".".

KILL_NO_TEARDOWN
   |
   | Set KILL_NO_TEARDOWN=1 to suppress attempting configuration cleanup
     during KILL_LDMSD. If set, ldmsd internal cleanup() function will
     attempt partial cleanup, but possibly leave active data structures
     to be reported by valgrind. The following variables are visible to
     the input file and the configuration file.

i
   |
   | Daemon configuration files and commands can refer to ${i} where i
     is the integer daemon number supplied via LDMSD for the specific
     daemon using the script.

portN
   |
   | Daemon configuration files and commands can refer to ${portN} where
     N is any value of 'i' described above. portN is the data port
     number of the N-th daemon.

input
   |
   | The name of the input file as specified when invoking this command.

testname
   |
   | The base name (directories stripped) of the input file name. This
     variable makes it possible to use similar input across many test
     files when the name of the input file is the same as the plugin
     tested.

TESTDIR
   |
   | Root directory of the shared testing setup.

TESTDIR_FAST
   |
   | Root directory of the fast testing scratch (local RAM or NVME)
     This is removed at the end of a run in most circumstances.

STOREDIR
   |
   | A directory that should be used for store output configuration.

LOGDIR
   |
   | A directory that should be used for log outputs.

LDMS_AUTH_FILE
   |
   | Secret file used for daemon communication.

XPRT
   |
   | The transport used. It may be specified in the environment to
     override the default 'sock', and it is exported to the executed
     daemon environment.

HOST
   |
   | The host name used for a specific interface. It may be specified in
     the environment to override the default 'localhost', and it is
     exported to the executed daemon environment.

NOTES
=====

Any other variable may be defined and exported for use in the
attribute/value expansion of values in plugin configuration.

EXIT CODES
==========

Expected exit codes are 0 and 1. If the exit codes is 0, then the
program will proceed. If the exit code is 1 then the script will stop
and notify the user.

FILES
=====

*$input_file.$i*
   |
   | For each value of i specifed to start an ldmsd, a configuration
     file named $input_file.$i must also exist. The syntax in the file
     must be consistent with the other yaml or raw options present.

Exception: For any single "LDMSD -c <daemon-numbers>" or
"LDMSD -z <daemon-numbers>", only
$input_file.$i for the first listed number is needed; the first file
will be used for all subsequent numbers and any matching files except
the first are ignored. Where prologs or epilogs are also specified, the regular
inclusion process is applied to the first file.

*[test_dir]*
   |
   | If test_dir is supplied, it is used as the test output directory.
     The default output location is \`pwd`/ldmstest/$testname.

*$docdir/examples/static-test/$input_file*
   |
   | If input_file is not found in the current directory, it is checked
     for in $docdir/examples/static-test/$input_file.

GENERATED FILES
===============

*$test_dir/logs/vg.$k$VGTAG.%p*
   | *$test_dir/logs/vgls.$k$VGTAG.%p*
   | The valgrind log for the kth daemon with PID %p or the valgrind log
     for ldms_ls of the kth daemon with PID %p, if valgrind is active.

*$test_dir/logs/$k.txt*
   |
   | The log for the kth daemon.

*$test_dir/logs/teardown.$k.txt*
   |
   | The teardown log for the kth daemon.

*$test_dir/run/yaml.$k*
   |
   | The yaml input for the kth daemon.

*$test_dir/run/yaml.$k.pre-sed*
   |
   | The yaml input for the kth daemon before @variable@
     substitution is performed, if -S was given.

*$test_dir/run/conf.$k*
   |
   | The raw input for the kth daemon.
     Where both conf.k and yaml.k are generated, conf.k is for review only
     and yaml.k is used in starting the daemon k.

*$test_dir/run/revconf.$k*
   |
   | The input for the kth daemon teardown.

*$test_dir/run/env.$k*
   |
   | The environment present for the kth daemon.

*$test_dir/run/start.$k*
   |
   | The start command of the kth daemon.

*$test_dir/store/*
   |
   | The root of store output locations.

*$test_dir/run/ldmsd/secret*
   |
   | The secret file for authentication.

NOTES
=====
Some test input files can be scaled by setting the environment variable 'maxdaemon'
to an integer larger than the default value set in the input file.
It is recommended, but not required, that any scalable test case follow this convention.

SEE ALSO
========

:ref:`seq(1) <seq>`
