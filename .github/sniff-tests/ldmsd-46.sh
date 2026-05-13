#!/bin/bash
#
# sniff-test ldmsd basic functionality in IPv4+6 environment

cd $(dirname $0) # work from script dir
source ldmsd-common.sh

do_test - localhost 127.0.0.1 ::1 || exit -1
do_test '*' localhost 127.0.0.1 ::1 || exit -1
do_test localhost localhost || exit -1

INFO "<<<< DONE >>>>"
