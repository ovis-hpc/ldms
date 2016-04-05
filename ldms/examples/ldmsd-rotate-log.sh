#!/bin/bash

LOC="/var/log/ovis"

mode=$1
if [ "$mode" == "rename" ]; then
	echo "rename"
	CURNAME="agg.log"
	OLDNAME="${CURNAME}.`date +%s`"
	mv ${LOC}/${CURNAME} ${LOC}/${OLDNAME}
	echo "logrotate" | ldmsd_controller --host localhost --port 20002 --auth_file ~/.ldmsauth.conf
else
	echo "new name"
	NEWNAME="agg-new-`date +%s`.log"
	echo "logrotate path=${LOC}/${NEWNAME}" | ldmsd_controller --host localhost --port 20002 --auth_file ~/.ldmsauth.conf
fi
