#!/bin/bash
source common.sh
__check_config "$0"
CMD="balerd -s $BSTORE -l $BLOG -C $BCONFIG -m master -v INFO"
if [[ $1 == '--gdb' ]]; then
	gdb --args $CMD -F
else
	$CMD
fi
