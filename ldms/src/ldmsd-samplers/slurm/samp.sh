#!/bin/bash

CFG=samp.conf
XPRT=sock:10001
LVL=DEBUG
LOG=samp.log

cat >$CFG <<EOF
load name=slurm plugin=slurm_sampler
config name=slurm
EOF

gdb --args \
ldmsd -F -x $XPRT -c $CFG -v $LVL | tee $LOG
