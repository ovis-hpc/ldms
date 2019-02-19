#!/bin/bash

CONF=perfevent.conf

cat >$CONF <<EOF
load name=inst plugin=perfevent
config name=inst action=add pid=-1 cpu=0 type=0 id=0 metricname=CPU0_CYCLE
config name=inst action=add pid=-1 cpu=1 type=0 id=0 metricname=CPU1_CYCLE
config name=inst action=init

smplr_add name=smp instance=inst
smplr_start name=smp interval=1000000 offset=0
EOF

ldmsd -F -x sock:10001 -v INFO -c $CONF | tee perfevent.log
