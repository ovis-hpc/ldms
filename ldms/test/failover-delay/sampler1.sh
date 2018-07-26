#!/bin/bash
mkdir -p cfg log
cat > cfg/sampler1 <<EOF
load name=meminfo
config name=meminfo instance=sampler1/meminfo producer=sampler1
start name=meminfo interval=1000000
EOF
gdb --args ldmsd -x sock:10001 -c cfg/sampler1 -F -v INFO | tee log/sampler1
