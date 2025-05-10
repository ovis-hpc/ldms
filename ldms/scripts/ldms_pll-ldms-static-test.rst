.. _pll-ldms-static-test:

====================
pll-ldms-static-test
====================

----------------------------------------
Run a canned test scenario under slurm.
----------------------------------------

:Date:   4 Oct 2020
:Manual section: 8
:Manual group: LDMS scripts

SYNOPSIS
========

pll-ldms-static-test.sh -l

pll-ldms-static-test.sh -h

pll-ldms-static-test.sh -b

pll-ldms-static-test.sh <input_file> [test_dir]

DESCRIPTION
===========

The pll-ldms-static-test.sh command starts a canned test defined in the
input_file using a standard environment. The input file is written in a
simple bash macro language described in LANGUAGE below. Supporting
configuration file fragments will be used, as determined from the input
file. See FILES below. This tests ldmsd run with static configuration
files (as would normally happen as a system service) and shut down with
a signal.

The pll-ldms-static-test.sh command is expected to be driven in a Slurm
sbatch script or interactively on nodes obtained with salloc.

OPTIONS
=======

-l
   |
   | List the canned tests available. Each canned test has a
     corresponding sbatch script. See FILES below for the location of
     these.

-b
   |
   | Get the expected sbatch file location for the test.

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
     aggregation relationships of LDMSD calls may be infeasible. In
     parallel execution, the Nth daemon will run as the (N-1)th slurm
     task, since Slurm numbers tasks from 0 and this tool numbers tasks
     from 1.

FILECNT_LDMSD <daemon-numbers>
   |
   | Collect the list of open files from /proc/self/fd/ and print the
     total to stdout. See FILES below.

LDMSD [conf-options] <daemon-numbers>
   |
   | This starts a number of daemons described by daemon-numbers. The
     numbers can be a given list, such as "1 2 3". The environment of
     each daemon (and its config script) will contain the variable i set
     to one of the given values, as described in ENVIRONMENT. For each
     value of i, a configuration fragment $input_file.$i must also
     exist. Use :ref:`seq(1) <seq>` to generate large number sequences.

See CONFIGURATION OPTIONS below for the explanation of [conf-options].

MESSAGE [arguments]
   |
   | The expanded arguments are logged.

LDMS_LS <k> [ldms_ls_args]
   |
   | This invokes ldms_ls on the k-th ldmsd.

KILL_LDMSD <daemon-numbers>
   |
   | Kills the listed daemons.

SLEEP <n>
   |
   | Sleeps n seconds and logs a message about it.

SEC_LEFT
   |
   | Prints the amount of seconds left in the Slurm job time limit. If
     time is unlimited, a default of 600 is returned since tests should
     never be allowed infinite time. This can be used to manage test
     with sleep time dependent on the requested job time limit.

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

file_created <filename> <daemon numbers>
   |
   | Verifies the local existence and readability of filename for the
     listed daemon(s).

rollover_created <filename> <daemon numbers>
   |
   | Verifies the local existence and readability of rollover files
     matching pattern filename.[0-9]\* for the listed daemon(s).

bypass=<0,1>
   |
   | This variable assignment disables (1) or enables (0) all the macros
     described above. Typical use is to skip one or more operations
     while debugging a test script.

portbase=<K>
   |
   | The listening port numbers assigned to the daemons will be K+i,
     where i is as described for macro LDMSD. It is a good idea (to
     support automated testing) if portbase is set in <input_file> so
     that each test uses a unique range of ports. This enables tests to
     proceed in parallel on the same node.

CONFIGURATION OPTIONS
=====================

The LDMSD command supports the following options. Note that all -P
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
     FILES. This allows a single input file to serve many similar daemon
     instances in scale testing.

-s <wait_microseconds>
   |
   | After an ldmsd is started, wait wait_microseconds before checking
     for the daemon PID file to exist. The appropriate wait time is
     variable depending on the complexity of the configuration. If not
     specified, the default is 2 seconds wait time. If the gnu command
     sleep is not available, 0 wait is applied and work continues.

ENVIRONMENT
===========

The following variables must be set in the job submission script (using
information about allocated resources) and in the environment exported
to the compute nodes:

| i.TP XPRT=$transport_plugin_name
| If not set, defaults to sock.

HOST_SUFFIX=$device_suffix
   |
   | If not using sock transport, the string to append to $HOSTNAME to
     obtain the correct hostname for the fast network device used with
     rdma or fabric transport plugins. E.g. "-ib0".

TEST_HOSTFILE=file
   |
   | Name of the file containing variables named host$i, derived from
     resource manager information, assigning host names to daemon
     numbers (i).

LDMS_DAEMON_ARRAY_FILE=file
   |
   | Name of the file containing array variable named host[$], derived
     from resource manager information, assigning host names to an array
     indexed by daemon number i.

hosts[N]
   |
   | Daemon configuration files and commands can refer to ${hosts${i}}
     where N is any value of 'i' described above. hosts[i] is the
     network hostname for the N-th daemon.

The following variables may be set in the script to affect the launch of
ldmsd or ldms_ls:

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

VGSUFF
   |
   | If valgrind is used (see vgon, vgoff), then $VGSUFF is appended to
     the valgrind output file name when defined. A good practice is for
     VGSUFF to start with ".".

KILL_NO_TEARDOWN
   |
   | Set KILL_NO_TEARDOWN=1 to suppress attempting configuration cleanup
     during KILL_LDMSD. If set, ldmsd internal cleanup() function will
     attempt partial cleanup, but possibly leave active data structures
     to be reported by valgrind.

The following variables are visible to the input file and the
configuration file.

i
   |
   | Daemon configuration files and commands can refer to ${i} where i
     is the integer daemon number supplied via LDMSD for the specific
     daemon using the script.

hostN
   |
   | Daemon configuration files and commands can refer to ${hostN} where
     N is any value of 'i' described above. hostN is the network
     hostname for the N-th daemon.

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
   | Root directory of the testing setup.

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
     the environment to override the default '$(hostname)', and it is
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
     file named $input_file.$i must also exist. This configuration file
     is used when starting the daemon.

Exception: For any single "LDMSD -c <daemon-numbers>", only
$input_file.$i for the first listed number is needed; the first file
will be used for all subsequent numbers and any matching files except
the first are ignored. Where prologs are also specified, the regular
prolog inclusion process is applied to the first file.

*sbatch.$input_file*
   |
   | Submitting the canned test $input_file listed with
     pll-ldms-static-test.sh is easily done with

sbatch $(pll-ldms-static-test.sh -b $input_file)

Which will give the full path to the batch file for test $input_file.

*[test_dir]*
   |
   | If test_dir is supplied, it is used as the test output directory.
     The default output location is
     \`pwd`/ldmstest/$testname/$SLURM_JOBID.$SLURM_CLUSTER_NAME.$SLURM_NTASKS.
     It is the user's job to ensure test_dir is a globally writable
     directory in the cluster before pll-ldms-static-test.sh is run by
     the sbatch job script.

*$docdir/examples/slurm-test/$input_file*
   |
   | If input_file is not found in the current directory, it is checked
     for in $docdir/examples/slurm-test/$input_file.

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

*$test_dir/run/conf.$k*
   |
   | The input for the kth daemon.

*$test_dir/run/revconf.$k*
   |
   | The input for the kth daemon teardown.

*$test_dir/run/env.$k*
   |
   | The environment present for the kth daemon.

*$test_dir/run/ldmsd.pid.$k*
   |
   | The transient pid file of the kth daemon. Contains the pid number.

*$test_dir/run/ldmsd.pid.$k.cnt.$timestamp.$filecnt*
   |
   | The open file list of the kth daemon at time $timestamp. The total
     is $filecnt.

*$test_dir/run/start.$k*
   |
   | The start command of the kth daemon.

*$test_dir/store/*
   |
   | The root of store output locations.

*$test_dir/run/ldmsd/secret.$SLURM_JOBID*
   |
   | The secret file for authentication.

EXAMPLE
=======

With the ldms bin directory in your path, submit a job with

::

   sbatch -n 16 --nodes=4 \
   --time=1 \
   --account=MUALN1 \
   --job-name=ldms-demo \
   -p debug \
   $(pll-ldms-static-test.sh -b cluster)


   The slurm options shown here override the defaults listed in the sbatch input file to run with 16 daemons on 4 nodes for 1 minute. The defaults are site specific, but the example 'cluster' is coded to run on any number of nodes with any number of tasks >= 3. Adding more tasks adds more data producers. Specifying more tasks than nodes assigns daemons round-robin to available nodes. The options specified with --account, and partition (-p) are site specific.

SEE ALSO
========

:ref:`seq(1) <seq>`, :ref:`sbatch(1) <sbatch>`, :ref:`srun(1) <srun>`
