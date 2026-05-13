#!/bin/bash

LOG() {
	echo $(date) "$@"
}

INFO() {
	LOG INFO "$@"
}

ERROR() {
	LOG ERROR "$@"
}

ERR() {
	ERROR "$@"
}

do_test() {
	#
	# do_test LISTEN_HOST CONNECT_HOST1 [ CONNECT_HOST2 ... ]
	#
	# WORKINGDIR is expected to be the script dirrectory.
	#
	# Example:
	#   do_test - 127.0.0.1 ::1 localhost
	#   do_test "*" 127.0.0.1 ::1 localhost
	#   do_test localhost localhost
	#
	local L=$1
	shift
	local HOSTS=( "$@" )
	local RC=0
	INFO "===================="
	INFO "Testing parameters:"
	INFO "  listen-host: ${L}"
	INFO "  connect-hosts: ${HOSTS[*]}"

	if [[ "${L}" == "-" ]]; then
		unset LISTEN_HOST
		LISTEN_CFG="-c samp_listen_no_host.conf"
	else
		export LISTEN_HOST=${L}
		LISTEN_CFG="-c samp_listen.conf"
	fi

	CMD="ldmsd ${LISTEN_CFG} -c samp.conf &"

	INFO "Starting ldmsd, cmd: ${CMD}"
	eval ${CMD}

	sleep 0.5

	T=1

	for H in "${HOSTS[@]}" ; do
		CMD="ldms_ls -h ${H}"
		INFO "ldms_ls command: ${CMD}"
		OUT=$( ${CMD} )
		INFO "output: ${OUT}"
		EXPECT="samp/meminfo"
		if [[ "${OUT}" != "${EXPECT}" ]] ; then
			ERROR "bad output"
			RC=-1
		fi

		# pub / sub
		TAG="TAG${T}"
		CMD="./sub.py -H ${H} -T ${TAG} >${TAG}.txt 2>&1 &"
		INFO "subscriber command: ${CMD}"
		eval ${CMD}
		SUB_PID=$!

		sleep 0.5
		DATA="DATA${T}"
		CMD="./pub.py -H ${H} -T ${TAG} -D ${DATA}"
		INFO "publisher command: ${CMD}"
		eval ${CMD}

		wait ${SUB_PID}

		RECV=$( cat ${TAG}.txt )
		EXP_RECV="${TAG}: ${DATA}"
		if [[ "${RECV}" != "${EXP_RECV}" ]] ; then
			ERROR "sub.py receive bad data: ${RECV}"
			RC=-1
		fi

		(( T++ ))
	done

	kill $(jobs -p)
	wait
	INFO "===================="
	return ${RC}
}
