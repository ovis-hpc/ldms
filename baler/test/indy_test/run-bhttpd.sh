#!/bin/bash
source ./common.sh
__check_config "$0"

export BHTTPD_QUERY_SESSION_TIMEOUT=600
for ((X=0; $X<$BTEST_N_DAEMONS; X++)); do
	#start_bhttpd $X
	start_bhttpd_mirror $X
done
