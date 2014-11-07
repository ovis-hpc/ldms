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
	LIST=$(ls $D/*_sos.OBJ 2>/dev/null)
	for X in $LIST; do
		X=${X%_sos.OBJ}
		sos_check_single $X $POLICY
		if [ 0 -ne $? ]; then
			return 1
		fi
	done
	return 0
}

collapse_hostnames() {
	local hostnames=$1
	local prefix
	local begin=-1
	local end
	local result=""

	local re='([0-9]*)(.+)'
	local tmp_prefix
	local tmp_number

	for name in $hostnames; do
		rev_name=`echo $name|rev`
		if [[ $rev_name =~ $re ]]; then
			tmp_prefix=`echo ${BASH_REMATCH[2]}|rev`
			tmp_number=`echo ${BASH_REMATCH[1]}|rev`
		else
			return -1
		fi
		if [[ $begin -eq -1 ]]; then
			begin=$tmp_number
			prefix=$tmp_prefix
			if [[ $begin == "" ]]; then
				if [[ $result == "" ]]; then
					result=$prefix
				else
					result="$result,$prefix"
				fi
				begin=-1
			fi
			end=$begin
		else
			if [[ $tmp_prefix == $prefix ]]; then
				tmp=$(expr $tmp_number + 0)
				tmp_end=$(expr $end + 0)
				if [[ $tmp-1 -eq $tmp_end ]]; then
					end=$tmp_number
				else
					if [[ $begin == $end ]]; then
						tmp="$prefix$begin"
					else
						tmp="$prefix[$begin-$end]"
					fi
					result=$result,$tmp
					begin=$tmp_number
					end=$begin
				fi
			else
				if [[ $begin == $end ]]; then
					tmp=$prefix$begin
				else
					tmp=$prefix[$begin-$end]
				fi
				if [[ $result == "" ]]; then
					result="$tmp"
				else
					result="$result,$tmp"
				fi
				begin=`echo ${BASH_REMATCH[1]}|rev`
				prefix=$tmp_prefix
				if [[ $begin == "" ]]; then
					result="$result,$prefix"
					begin=-1
				fi
				end=$begin
			fi
		fi
	done
	if [[ $begin == $end ]]; then
		tmp=$prefix$begin
	else
		tmp=$prefix[$begin-$end]
	fi
	result="$result,$tmp"
	echo $result
	return 0
}
