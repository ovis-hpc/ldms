#!/bin/bash
#
# This script is meant to run on the nodes running `slurmd`. The plugstack.conf
# file configure slurmd to load `slurm_notifier` which send job data to
# slurm_sampler plugin in the ldmsd running by this script.


DIR=$HOSTNAME
mkdir -p $DIR

CFG=$DIR/cfg
XPRT=sock:10001
LVL=DEBUG
AUTH="-a munge -A socket=/opt/munge/var/run/munge/munge.socket.2"

cat >$CFG <<EOF
load name=slurm plugin=slurm_sampler
config name=slurm
EOF

gdb --args \
ldmsd -F -x $XPRT -v $LVL -c $CFG $AUTH | tee $DIR/log
