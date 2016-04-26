#!/bin/bash

# STDERR => STDOUT
exec 2>&1

# Default values
BLOG=./balerd.log
BSTORE=./store
BCONFIG=./balerd.cfg
BTEST_N_PATTERNS=128
BTEST_ENG_DICT="../eng-dictionary"
BTEST_HOST_LIST="../host.list"
BTEST_BIN_RSYSLOG_PORT=33333
BPROF=balerd.prof

BOUT_THREADS=1
BIN_THREADS=1
BLOG_LEVEL=INFO

source ./common.sh

__check_config "$0"

# balerd.cfg generation
{ cat <<EOF
tokens type=ENG path=$BTEST_ENG_DICT
tokens type=HOST path=$BTEST_HOST_LIST
plugin name=bout_sos_img delta_ts=3600
plugin name=bout_sos_img delta_ts=60
plugin name=bout_sos_msg
plugin name=bin_rsyslog_tcp port=$BTEST_BIN_RSYSLOG_PORT
EOF
} > $BCONFIG

OPERF_OPTIONS="-g -d $BPROF"
BALERD_OPTS="-s $BSTORE -l $BLOG -C $BCONFIG -m master -v $BLOG_LEVEL -I $BIN_THREADS -O $BOUT_THREADS"
BALERD_CMD="balerd -F $BALERD_OPTS"

BPID=0

check_balerd() {
	jobs '%$BALERD_CMD' > /dev/null 2>&1 || \
		__err_exit "balerd is not running"
	# repeat to cover the "Done" case.
	sleep 1
	jobs '%$BALERD_CMD' > /dev/null 2>&1 || \
		__err_exit "balerd is not running"
}

wait_balerd() {
	echo "waiting ..."
	if (( ! BPID )); then
		echo "wait_balerd -- WARN: BPID not set"
		return
	fi
	P=`top -p $BPID -b -n 1 | grep 'balerd' | awk '{print $9}' | cut -f 1 -d .`
	while (( P > 10 )); do
		sleep 1
		X=($(top -p $BPID -b -n 1 | tail -n 1))
		P=${X[8]%%.*}
	done
}

wait_bsos_part_modify() {
	echo "waiting bsos_part_modify ..."
	while pgrep -P $BASHPID bsos_part_modify > /dev/null; do
		sleep 1
	done
}

stat_balerd() {
	if (( ! BPID )); then
		echo "stat_balerd -- WARN: BPID not set"
		return
	fi
	while true; do
		DT=$(date)
		ST=$(cat /proc/$BPID/stat)
		STM=$(cat /proc/$BPID/statm)
		echo $DT $ST >> $BSTAT
		echo $DT $STM >> $BSTATM
		sleep 1;
	done
}

if [[ -d $BSTORE ]]; then
	X=`lsof +D $BSTORE | grep '^balerd' | wc -l`
	if ((X)); then
		__err_exit "Another balerd is running with the store: $BSTORE"
	fi
fi

__cleanup() {
	jobs -l
	KCMD="kill $(jobs -p)"
	echo "KCMD: $KCMD"
	$KCMD
}

# Hook to kill all jobs at exit
trap '__cleanup' EXIT

./clean.sh

if __has_operf; then
	mkdir -p $BPROF
fi

__info "starting balerd, cmd: $BALERD_CMD"
$BALERD_CMD &

sleep 1

check_balerd

BPID=`jobs -p '%$BALERD_CMD'`

stat_balerd &

__info "Start sending data to balerd"

# stat_balerd &
./gen-log.pl -p

BSTORE=$(readlink -f $BSTORE)
SUBSTORES="msg_store/msg img_store/60-1 img_store/3600-1"
PART_COUNT=0

set -e
for X in $(ls messages.* | sort -V); do
	[ -s $X ] || continue # skip zero-length file
	echo "creating partition $X"
	for Y in $SUBSTORES; do
		CMD="sos_part_create -C $BSTORE/$Y -s primary $X"
		__info "CMD: $CMD"
		$CMD
	done
	if [ -n "$BTEST_ENABLE_OFFLINE" ] &&
			(($PART_COUNT >= $BTEST_N_ACTIVE_PART)); then
		P=$(($PART_COUNT - $BTEST_N_ACTIVE_PART))
		for Y in $SUBSTORES; do
			CMD="bsos_part_modify -C $BSTORE/$Y -s offline messages.$P"
			__info "Bringing $Y/messages.$P offline"
			__info "CMD: $CMD"
			$CMD &
		done
		wait_bsos_part_modify
	fi
	echo "processing $X ... "
	./syslog2baler.pl -p $BTEST_BIN_RSYSLOG_PORT < $X
	wait_balerd
	echo "DONE"
	PART_COUNT=$((PART_COUNT+1))
done
set +e

__info "done sending data .. wait a little while for balerd to process them"

time -p wait_balerd
sleep 1

check_balerd

./run-check.sh

echo -e "${BLD}${GRN}FINISHED!!!${NC}"
