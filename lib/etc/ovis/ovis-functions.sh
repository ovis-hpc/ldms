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

sos_check_single() {
	local P=$1
	local POLICY=$2

	sos_verify -s $P
	if [ 0 -eq $? ]; then
		return 0
	fi

	case "$POLICY" in
	restore)
		sos_restore -s $P
		if [ 0 -eq $? ]; then
			return 0
		fi
		# re-init the store if cannot restore
		echo "reinitializing $P"
		sos_reinit -s $P
		return $?
		;;
	reinit)
		echo "reinitializing $P"
		sos_reinit -s $P
		return $?
		;;
	esac

	return 255
}

sos_check_dir() {
	local D=$1
	local POLICY=$2

	if [ ! -d "$D" ]; then
		return 0
	fi
	local LIST
	LIST=$(ls $D/*_sos.OBJ)
	for X in $LIST; do
		X=${X%_sos.OBJ}
		sos_check_single $X $POLICY
		if [ 0 -ne $? ]; then
			return 1
		fi
	done
	return 0
}
