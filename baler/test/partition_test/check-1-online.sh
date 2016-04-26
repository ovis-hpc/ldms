#!/bin/bash
source common.sh
__check_config

MSG=$(readlink -f $BSTORE)/msg_store/msg
IMG60=$(readlink -f $BSTORE)/img_store/60-1
IMG3600=$(readlink -f $BSTORE)/img_store/3600-1

PLIST=($(ls messages.*))

_N=$((${#PLIST[*]} - 1))

while ((_N)) && [ ! -s ${PLIST[$_N]} ]; do
	_N=$((_N-1))
done

set -e

# Bring all offline partitions back to active

for C in $MSG $IMG60 $IMG3600; do
	sos_part_query -C $C -v | grep 'OFFLINE\|ACTIVE\|PRIMARY' |
		grep messages | awk '{ print $1 " " $3 }' |
		while read P S; do
			if [ "$S" == "OFFLINE" ]; then
			__info "making $S part $P active"
			bsos_part_modify -C $C -s active $P -v || exit -1
			fi
		done
done


# expecting all non-offline partitions

for C in $MSG $IMG60 $IMG3600; do
	sos_part_query -C $C -v | grep 'OFFLINE\|ACTIVE\|PRIMARY' |
		grep messages | awk '{ print $1 " " $3 }' |
		while read P S; do
			if [ "$S" == "OFFLINE" ]; then
				__err "$C part $P is offline"
				exit -1
			fi
		done
done

exit 0
