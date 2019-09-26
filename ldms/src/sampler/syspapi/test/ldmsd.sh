#!/bin/bash

CONFIG=ldmsd.cfg
PAPI_CFG=syspapi.json

cat >$CONFIG <<EOF
load name=syspapi_sampler
config name=syspapi_sampler instance=syspapi_sampler producer=localhost \
       events=PAPI_TOT_CYC,PAPI_TOT_INS cfg_file=$PAPI_CFG
start name=syspapi_sampler interval=1000000 offset=0
EOF

cat >$PAPI_CFG <<EOF
{
  "schema" : "SySpApI",
  "events" : [
    "PAPI_L1_DCA",
    "PAPI_L1_DCH"
  ]
}
EOF

gdb --args ldmsd -F -c $CONFIG -x sock:10001 -v INFO
