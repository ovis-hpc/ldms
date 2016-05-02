#!/bin/bash

# STDERR => STDOUT
exec 2>&1

# Default values
BLOG=./balerd.log
BSTORE=./store
BCONFIG=./balerd.cfg
BTEST_N_PATTERNS=128
BTEST_ENG_DICT="../eng-dictionary"
BTEST_HOST_LIST="./host.list"
BTEST_BIN_RSYSLOG_PORT=33333
BPROF=balerd.prof

BOUT_THREADS=1
BIN_THREADS=1
BLOG_LEVEL=INFO

source ./common.sh
__check_config "$0"

BALERD_OPTS="-s $BSTORE -l $BLOG -C $BCONFIG -m master -v $BLOG_LEVEL -I $BIN_THREADS -O $BOUT_THREADS"
BALERD_CMD="balerd -F $BALERD_OPTS"

BPID=0

__cleanup() {
	jobs -l
	JLIST=$(jobs -p)
	if [ -n "$JLIST" ]; then
		KCMD="kill $JLIST"
		__info "KCMD: $KCMD"
		$KCMD
	else
		__info "No jobs to kill"
	fi
}

__info "Generating host.list"
{ echo <<EOF
$(for ((H=0; $H<$BTEST_NODE_LEN; H++)); do
	printf "node%05d" $((BTEST_NODE_BEGIN + H))
done)
EOF
} > $BTEST_HOST_LIST

# Hook to kill all jobs at exit
trap '__cleanup' EXIT

./clean.sh

for ((X=0; $X<$BTEST_N_DAEMONS; X++)); do
	start_balerd $X
done

sleep 1

for ((X=0; $X<$BTEST_N_DAEMONS; X++)); do
	check_balerd $X
done

__info "Generating logs"

./gen-log.pl

BSTORE=$(readlink -f $BSTORE)
SUBSTORES="msg_store/msg img_store/60-1 img_store/3600-1"
PART_COUNT=0

set -e
for X in $(ls messages.* | sort -V); do
	[ -s $X ] || continue # skip zero-length file
	echo "processing $X ... "

	NUM=${X##messages.}

	./syslog2baler.pl -p $((BTEST_BIN_RSYSLOG_PORT + NUM)) < $X

	wait_balerd $NUM
	echo "DONE"
done
set +e

__info "done sending data .. wait a little while for balerd to process them"

for ((X=0; $X<$BTEST_N_DAEMONS; X++)); do
	wait_balerd $X
done

sleep 1

for ((X=0; $X<$BTEST_N_DAEMONS; X++)); do
	check_balerd $X
done

# Now, start bhttpds
for ((X=0; $X<$BTEST_N_DAEMONS; X++)); do
	#start_bhttpd $X
	start_bhttpd_mirror $X
done

./run-check.sh

echo -e "${BLD}${GRN}FINISHED!!!${NC} -- press ENTER to exit"

read
