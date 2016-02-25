#!/bin/bash

# Check if the designated partitions are offline
# This script will not change partition statuses.


source common.sh
__check_config

if [ -z "$BTEST_ENABLE_OFFLINE" ]; then
	exit 0
fi

MSG=$(readlink -f $BSTORE)/msg_store/msg
IMG60=$(readlink -f $BSTORE)/img_store/60-1
IMG3600=$(readlink -f $BSTORE)/img_store/3600-1

PLIST=($(ls messages.*))

_N=$((${#PLIST[*]} - 1))

while ((_N)) && [ ! -s ${PLIST[$_N]} ]; do
	_N=$((_N-1))
done

check_state() {
	PART=$1
	P=${PART##*.}
	STATE=$2
	if (( $P <= ($_N - $BTEST_N_ACTIVE_PART) )); then
		# expecting offline
		[ "$STATE" == "OFFLINE" ]
		return
	fi

	if (( $P == $_N )); then
		[ "$STATE" == "PRIMARY" ]
		return
	fi
	[ "$STATE" == "ACTIVE" ]
}

echo messages.$_N

set -e

for C in $MSG $IMG60 $IMG3600; do
	sos_part_query -C $C -v | grep 'OFFLINE\|ACTIVE\|PRIMARY' |
		grep messages | awk '{ print $1 " " $3 }' |
		while read P S; do
			check_state $P $S && {
				__info "$C part '$P' state: '$S', OK"
			} || {
				__info "$C part '$P' state: '$S', ERROR"
				exit -1
			}
		done
done

exit 0
