#!/bin/bash

# STDERR => STDOUT
exec 2>&1

source ./common.sh
source ./config.sh

./gen-hosts.pl > $BTEST_HOST_LIST

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

exit_hook() {
	JOBS=$(jobs -prl)
	echo "Running jobs: $JOBS"
	JOBS=$(jobs -pr)
	CMD="kill $JOBS"
	echo "Kill CMD: $CMD"
	$CMD
}

# Hook to kill all jobs at exit
trap 'exit_hook' EXIT

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
sleep 1

__info "Start sending data to balerd"

time -p ./gen-log.pl | ./syslog2baler.pl -p $BTEST_BIN_RSYSLOG_PORT

if (( $? )); then
	__err_exit "Cannot send data to balerd."
fi

__info "done sending data .. wait a little while for balerd to process them"

time -p wait_balerd

sleep 1

check_balerd

# Put query test cases here
for X in check-*.{pl,sh,py}; do
	test -f $X || continue
	__info "${BLD}${YLW}$X ..........................${NC}"
	time -p ./$X
	if (($?)); then
		__err "................... $X ${BLD}${RED}failed${NC}"
	else
		__info "................... $X ${BLD}${GRN}success${NC}"
	fi
done

sleep 1

echo -e "${BLD}${GRN}FINISHED!!!${NC}"
