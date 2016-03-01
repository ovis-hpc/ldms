#!/bin/bash
# ./part_query.sh store.master
LOCAL_STORE_NAME=$1

BALER_TOOL_DIR="/home/gentile/Baler/baler_tools"
BALER_ENV="${BALER_TOOL_DIR}/baler_env.sh"
BALER_STORE_DIR="/home/gentile/Baler/mutrino"
BALER_STORE="${BALER_STORE_DIR}/${LOCAL_STORE_NAME}/msg_store/msg" 

source $BALER_ENV

CMD="sos_part_query -C ${BALER_STORE} -v"
`echo ${CMD}`





