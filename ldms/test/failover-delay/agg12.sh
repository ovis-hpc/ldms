#!/bin/bash
. env.sh
mkdir -p cfg log
cat > cfg/agg12 <<EOF
load name=store_none
config name=store_none delay=5000000

prdcr_add name=prdcr xprt=sock host=localhost port=10002 type=active \
	  interval=1000000
prdcr_start name=prdcr

strgp_add name=strgp plugin=store_none container=none schema=meminfo
strgp_prdcr_add name=strgp regex=.*
strgp_start name=strgp

updtr_add name=updtr interval=1000000 offset=0
updtr_prdcr_add name=updtr regex=.*
updtr_start name=updtr

failover_config host=localhost port=11001 xprt=sock interval=1000000 \
                auto_switch=1 peer_name=agg11
failover_start
EOF
gdb --args ldmsd -x sock:11002 -c cfg/agg12 -n agg12 -F -v INFO | tee log/agg12
