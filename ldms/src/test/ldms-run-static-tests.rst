.. _ldms-run-static-tests:

=====================
ldms-run-static-tests
=====================

--------------------
Execute the program
--------------------

:Date:   21 Aug 2020
:Manual section: 8
:Manual group: LDMS tests

SYNOPSIS
========

run-static-tests.test -l

run-static-tests.test -h

run-static-tests.test <input_file> [test_dir]

DESCRIPTION
===========

The run-static-tests.test initiates the ldms-static-test.test test on
each enabled plugin. The stdout/stderr of each ldms-static-test.sh
invocation will be redirected to a log file and its output tree. This
log file will then be tarred and compressed when ldms-static-test.sh has
finsihed. The return code of ldms-static-test.sh will then be checked by
this driver script. If the return value is 0, then the script will print
"PASS $testname" and if the return value is 1, the script will print
"FAIL $testname". Where testname is each invocation of
ldms-static-test.sh of the enabled plugins. Please see
ldms-static-test.man for more information.

OPTIONS
=======

-l
   |
   | List the enabled plugins.

-h
   |
   | List help message.

LANGUAGE
========

The following macro language is provided as extensions on bash. Other
bash use is also possible, but not recommended.

ENVIRONMENT
===========

Uses the current set environment to run. Environment may need to be
configured before excuting this test script.

input
   |
   | The name of the input file as specified when ldms-static-test.sh is
     invoked for each enabled plugin.

testname
   |
   | The base name (directories stripped) of the input file name. This
     variable makes it possible to use similar input across many test
     files when the name of the input file is the same as the plugin
     tested.

strict
   |
   | If the variable "strict" is used for KILL_LDMSD
     (ldms-static-:ref:`test(8) <test>`) the script will output "FAIL $testname" and
     return an XFAIL to indicate an expected failure only if the test
     case plugin is listed in static-test-list. The stderr of
     ldms-static-test.sh will be redirected to the log file
     test.$testname.log under the default output location of test_dir.

file
   |
   | The file "static-test-list" located in ldms/scripts/ defines a list
     of samplers that are expected to fail. If there is a failed test
     and the sampler is listed in this file, then run-static-test.sh
     will output an "XFAIL" and continue. Developers can modify this
     list to meet their needs.

bypass <1,0>
   |
   | This variable assignment is used to determine an expected failure
     (1) or normal failure (0) of a sampler plugin. This variable is set
     to (1) if the sampler is listed in $file and set to (0) otherwise.
     Used to test the successful and expected failures of each sampler
     plugin.

NOTES
=====

Any other variable may be defined and exported for use in the
attribute/value expansion of values in plugin configuration.

FILES
=====

*$input_file.$i*
   |
   | For each value of i specifed to start an ldmsd, a configuration
     file named $input_file.$i must also exist. This configuration file
     is used when starting the daemon.

*test_dir*
   |
   | Test output directory of ldms-static-test.sh. The default output
     location is \`pwd`/ldmstest/$testname.

GENERATED FILES
===============

*$test_dir/test.$testname.log*
   |
   | The log file containing stderr and stdout of ldms-static-test.sh.

*$test_dir/test.$testname.tgz*
   |
   | Location of the compressed file logfile.

SEE ALSO
========

ldmsd-static-test.man
