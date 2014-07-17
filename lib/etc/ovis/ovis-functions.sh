#!/bin/bash

get_pid() {
	full=$1
	base=$(basename $full)
	ps -C $base h -o pid,cmd | grep $full |
		sed 's/ *\([0-9]\+\) .*/\1/'
}

archive_log() {
	log=$1
	local x
	for x in $(seq 4 -1 0); do
		if [ -e $log.$x ] ; then
			mv $log.$x $log.$(($x+1))
		fi
	done

	if [ -e $log ] ; then
		mv $log $log.0
	fi
}
