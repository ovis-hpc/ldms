#!/bin/bash
source ./common.sh
__check_config "$0"

export BHTTPD_QUERY_SESSION_TIMEOUT=600
CMD="bhttpd -v DEBUG -s $BSTORE"
if [[ $1 == '--gdb' ]]; then
	gdb --args $CMD -F
else
	$CMD
fi
