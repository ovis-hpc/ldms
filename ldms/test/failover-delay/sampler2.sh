#!/bin/bash
mkdir -p cfg log
cat > cfg/sampler2 <<EOF
load name=meminfo
config name=meminfo instance=sampler2/meminfo producer=sampler2
start name=meminfo interval=1000000
EOF
gdb --args ldmsd -x sock:10002 -c cfg/sampler2 -F -v INFO | tee log/sampler2
