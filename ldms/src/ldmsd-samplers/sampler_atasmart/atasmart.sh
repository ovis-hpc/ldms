#!/bin/bash

CONF=atasmart.conf

cat >$CONF <<EOF
load name=inst plugin=sampler_atasmart
config name=inst disk=/dev/sda

smplr_add name=smp instance=inst
smplr_start name=smp interval=1000000 offset=0
EOF

ldmsd -F -x sock:10001 -v INFO -c $CONF | tee atasmart.log
