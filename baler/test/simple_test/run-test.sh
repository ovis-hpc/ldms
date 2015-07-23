#!/bin/bash

# STDERR => STDOUT
exec 2>&1

# Default values
BLOG=./balerd.log
BSTORE=./store
BCONFIG=./balerd.cfg
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
if __has_operf; then
	BALERD_CMD="operf $OPERF_OPTIONS $BALERD_CMD"
fi

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
	BPID=`jobs -p '%$BALERD_CMD'`
	if (( ! BPID )); then
		return
	fi
	P=`top -p $BPID -b -n 1 | grep 'balerd' | awk '{print $9}' | cut -f 1 -d .`
	while (( P > 10 )); do
		sleep 1
		P=`top -p $BPID -b -n 1 | grep 'balerd' | awk '{print $9}' | cut -f 1 -d .`
	done
}

if [[ -d $BSTORE ]]; then
	X=`lsof +D $BSTORE | grep '^balerd' | wc -l`
	if ((X)); then
		__err_exit "Another balerd is running with the store: $BSTORE"
	fi
fi

# Hook to kill all jobs at exit
trap 'kill -SIGINT $(jobs -p)' EXIT

./clean.sh

if __has_operf; then
	mkdir -p $BPROF
fi

__info "starting balerd, cmd: $BALERD_CMD"
$BALERD_CMD &

sleep 1

check_balerd

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
	__info "${BLD}${YLW}$X ..........................${NC}"
	time -p ./$X
	if (($?)); then
		__err "................... $X ${BLD}${RED}failed${NC}"
	else
		__info "................... $X ${BLD}${GRN}success${NC}"
	fi
done

echo -e "${BLD}${GRN}FINISHED!!!${NC}"
