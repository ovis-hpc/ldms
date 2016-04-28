#!/bin/bash

if [[ -z "$CONFIG" ]]; then
	CONFIG="./config.sh"
fi

TMPDIR=$PWD/tmp
mkdir -p $TMPDIR

if [[ -t 1 || -n "$COLOR" ]]; then
	# color code
	BLK='\033[30m'
	RED='\033[31m'
	GRN='\033[32m'
	YLW='\033[33m'
	BLU='\033[34m'
	PPL='\033[35m'
	TEA='\033[36m'
	WHT='\033[37m'
	BLD='\033[1m'
	NC='\033[0m'
else
	BLK=
	RED=
	GRN=
	YLW=
	BLU=
	PPL=
	TEA=
	WHT=
	BLD=
	NC=
fi

__err() {
	echo -e "${BLD}${RED}ERROR:${NC} $@"
}

__info() {
	echo -e "${BLD}${YLW}INFO:${NC} $@"
}

__err_exit() {
	__err $@
	exit -1
}

__join() {
	local IFS=","
	shift
	echo $*
}

__check_config() {
	# $1 is prog name
	if [[ -n "$CONFIG" ]]; then
		source "$CONFIG"
	else
		__err_exit "usage: CONFIG=<CONFIG.SH> $1"
	fi
	if [[ ! -f $CONFIG ]]; then
		__err_exit "config file not found, CONFIG: $CONFIG"
	fi
	local X
	SRVS=( $(for ((X=0; $X<$BTEST_N_DAEMONS; X++)); do
			echo -n ,localhost:$((BTEST_BHTTPD_PORT + X))
		done) )
	export BHTTPD_SERVERS=${SRVS:1}
}

__has_operf() {
	if which operf >/dev/null 2>&1; then
		# found
		return 0
	else
		# not found
		return 127
	fi
}

get_balerd() {
	NUM=$1
	echo $TMPDIR/balerd.$NUM
}

get_bhttpd() {
	NUM=$1
	echo $TMPDIR/bhttpd.$NUM
}

start_balerd() {
	NUM=$1
	BALERD_LOC=$(which balerd)
	BALERD=$(get_balerd $NUM)
	if [ -z "$BALERD" ]; then
		return 127
	fi

	ln -f -s $BALERD_LOC $BALERD || __err_exit "Cannot link $BALERD"

# balerd.cfg generation
{ cat <<EOF
tokens type=ENG path=$BTEST_ENG_DICT
tokens type=HOST path=$BTEST_HOST_LIST
plugin name=bout_sos_img delta_ts=3600
plugin name=bout_sos_img delta_ts=60
plugin name=bout_sos_msg
plugin name=bin_rsyslog_tcp port=$((BTEST_BIN_RSYSLOG_PORT + NUM))
EOF
} > ${BCONFIG}.${NUM}

	MODE=master
	if (( NUM )); then
		MODE=slave
	fi
	BALERD_OPTS="-s ${BSTORE}.${NUM} -l ${BLOG}.${NUM} \
			-C ${BCONFIG}.${NUM} -m $MODE -v $BLOG_LEVEL \
			-p $BTEST_MASTER_PORT \
			-I $BIN_THREADS -O $BOUT_THREADS"
	BALERD_CMD="$BALERD -F $BALERD_OPTS"
	__info "BALERD_CMD: $BALERD_CMD"

	$BALERD_CMD &
} # start_balerd() function

start_bhttpd() {
	NUM=$1
	BHTTPD_LOC=$(which bhttpd)
	BHTTPD=$(get_bhttpd $NUM)
	ln -f -s $BHTTPD_LOC $BHTTPD
	$BHTTPD -F -s ${BSTORE}.${NUM} \
		-l ${BHTTPDLOG}.${NUM} -v $BTEST_BHTTPD_LOGLEVEL \
		-p $((BTEST_BHTTPD_PORT + $NUM)) \
		&
}

check_balerd() {
	NUM=$1
	BALERD=$(get_balerd $NUM)
	pgrep -P $$ -x $(basename $BALERD) > /dev/null 2>&1 || \
		__err_exit "$BALERD is not running"
	# repeat to cover the "Done" case.
	sleep 1
	pgrep -P $$ -x $(basename $BALERD) > /dev/null 2>&1 || \
		__err_exit "$BALERD is not running"
}

wait_balerd() {
	NUM=$1
	BALERD=$(get_balerd $NUM)
	echo "waiting ... for $BALERD"
	P=$$
	BPID=$(pgrep -P $P -x $(basename $BALERD))
	if [ -z "$BPID" ]; then
		echo "wait_balerd -- WARN: BPID not set"
		return 127
	fi
	X=($(top -p $BPID -b -n 1 | tail -n 1))
	P=${X[8]%%.*}
	while (( P > 10 )); do
		sleep 1
		X=($(top -p $BPID -b -n 1 | tail -n 1))
		P=${X[8]%%.*}
	done
}
