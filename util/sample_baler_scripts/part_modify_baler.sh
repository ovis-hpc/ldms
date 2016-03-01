#!/bin/bash
#./part_modify store.master foo active
# 02/28/16 - only make a partition active or primary
LOCAL_STORE_NAME=$1
PARTNAME=$2
STATE=$3

BALER_TOOL_DIR="/home/gentile/Baler/baler_tools"
BALER_ENV="${BALER_TOOL_DIR}/baler_env.sh"
BALER_STORE_DIR="/home/gentile/Baler/mutrino"
BALER_STORE="${BALER_STORE_DIR}/${LOCAL_STORE_NAME}/msg_store/msg" 

source $BALER_ENV

CMD="bsos_part_modify -C ${BALER_STORE} -s ${STATE} ${PARTNAME}"
`echo ${CMD}`




