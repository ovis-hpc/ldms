#!/bin/bash
#
# sniff-test ldmsd basic functionality in IPv4-only environment

cd $(dirname $0) # work from script dir
source ldmsd-common.sh

# First make sure that IPv6 is disabled
./ipv6disabled.py || exit -1

do_test - localhost 127.0.0.1 || exit -1
do_test '*' localhost 127.0.0.1 || exit -1
do_test localhost localhost || exit -1

INFO "<<<< DONE >>>>"
