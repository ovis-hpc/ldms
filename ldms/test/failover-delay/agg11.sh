#!/bin/bash
. env.sh
mkdir -p cfg log
cat > cfg/agg11 <<EOF
load name=store_none
config name=store_none delay=5000000

prdcr_add name=prdcr xprt=sock host=localhost port=10001 type=active \
	  interval=1000000
prdcr_start name=prdcr

strgp_add name=strgp plugin=store_none container=none schema=meminfo
strgp_prdcr_add name=strgp regex=.*
strgp_start name=strgp

updtr_add name=updtr interval=1000000 offset=0
updtr_prdcr_add name=updtr regex=.*
updtr_start name=updtr

failover_config host=localhost port=11002 xprt=sock interval=1000000 \
                auto_switch=1 peer_name=agg12
failover_start
EOF
gdb --args ldmsd -x sock:11001 -c cfg/agg11 -n agg11 -F -v INFO | tee log/agg11
