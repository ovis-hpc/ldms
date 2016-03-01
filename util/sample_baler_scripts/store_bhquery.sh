#!/bin/bash
#./store_bquery.sh 205.137.89.103:18888,205.137.89.103:18889 ptn
ADDRPORTLIST=$1
DIRECTIVE=$2

BALER_TOOL_DIR="/home/gentile/Baler/baler_tools"
BALER_ENV="${BALER_TOOL_DIR}/baler_env.sh"

source $BALER_ENV

CMD="bhquery -t ${DIRECTIVE} -S ${ADDRPORTLIST}"
`echo ${CMD}`




