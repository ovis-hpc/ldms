#!/bin/bash
#
# This is a convenient wrapper to `ldms_ls` so that we don't have to explicitly
# specify the long auth options every time.
#
# Usage Example:
#     ./ldms_ls.sh -x sock -p 10001 -h nid00001 -l
#
#

AUTH="-a munge -A socket=/opt/munge/var/run/munge/munge.socket.2"

ldms_ls $AUTH $*
