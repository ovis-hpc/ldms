#!/bin/bash
#
# Test listen / connect with various names in IPv6-enabled environment
#

cd $(dirname $0) # work in the script directory

source py-common.sh

# listen on "localhost" shall be able to connect with "localhost"
do_test localhost localhost || exit -1

# No LISTEN_HOST given
do_test - localhost ::1 127.0.0.1 || exit -1

# With explicit "*" host
do_test "*" localhost ::1 127.0.0.1 || exit -1

do_test_reject 127.0.0.1 ::1 || exit -1

INFO "<<<< DONE >>>>"
