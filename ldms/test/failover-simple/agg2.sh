#!/bin/bash
mkdir -p cfg
CFG=cfg/agg2.cfg
cat > $CFG <<EOF
failover_config host=localhost xprt=sock port=11001 interval=2000000 \
                timeout_factor=5 peer_name=agg1
failover_start
EOF
gdb --args ldmsd -F -n agg2 -c $CFG -x sock:11002 -v INFO | tee agg2.log
