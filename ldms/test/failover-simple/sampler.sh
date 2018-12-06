#!/bin/bash
mkdir -p cfg
CFG=cfg/sampler.cfg
cat > $CFG <<EOF
load name=meminfo
config name=meminfo schema=meminfo instance=sampler/meminfo producer=sampler
start name=meminfo interval=1000000 offset=0
EOF
gdb --args ldmsd -F -c $CFG -x sock:10001 -v INFO
