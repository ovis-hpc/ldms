#!/bin/bash
#
# Test listen / connect with various names in environment without IPv6-enabled
#

cd $(dirname $0) # work in the script directory

source py-common.sh

# First make sure that IPv6 is disabled
./ipv6disabled.py || exit -1

# listen on "localhost" shall be able to connect with "localhost"
do_test localhost localhost || exit -1

# No LISTEN_HOST given
do_test - localhost 127.0.0.1 || exit -1

# With explicit "*" host
do_test "*" localhost 127.0.0.1 || exit -1

INFO "<<<< DONE >>>>"
