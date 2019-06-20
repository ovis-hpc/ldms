#!/bin/bash

CONF=dstat.conf

cat >$CONF <<EOF
load name=ds plugin=dstat
config name=ds io=1 stat=1 statm=1 mmalloc=1

smplr_add name=smp instance=ds
smplr_start name=smp interval=1000000 offset=0
EOF

ldmsd -F -x sock:10001 -v INFO -c $CONF | tee dstat.log
