#!/bin/bash

CONF=fptrans.conf

cat >$CONF <<EOF
load name=inst plugin=fptrans
config name=inst

smplr_add name=smp instance=inst
smplr_start name=smp interval=1000000 offset=0
EOF

ldmsd -F -x sock:10001 -v INFO -c $CONF | tee dstat.log
