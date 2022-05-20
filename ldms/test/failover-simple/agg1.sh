#!/bin/bash
mkdir -p cfg
CFG=cfg/agg1.cfg
cat > $CFG <<EOF
prdcr_add name=sampler host=localhost xprt=sock port=10001 interval=1000000 \
          type=active
prdcr_start name=sampler
updtr_add name=updtr interval=1000000 offset=500000
updtr_prdcr_add name=updtr regex=.*
updtr_start name=updtr
failover_config host=localhost xprt=sock port=11002 interval=2000000 \
                timeout_factor=5 peer_name=agg2
failover_start
EOF
gdb --args ldmsd -F -n agg1 -c $CFG -x sock:11001 -v INFO | tee agg1.log
