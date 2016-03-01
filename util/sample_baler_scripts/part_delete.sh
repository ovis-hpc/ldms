#!/bin/bash
# Only use this on partitions that have never been made active...unless you know what you are doing
# ./part_delete.sh store.master foo
LOCAL_STORE_NAME=$1
PARTNAME=$2

BALER_TOOL_DIR="/home/gentile/Baler/baler_tools"
BALER_ENV="${BALER_TOOL_DIR}/baler_env.sh"
BALER_STORE_DIR="/home/gentile/Baler/mutrino"
BALER_STORE="${BALER_STORE_DIR}/${LOCAL_STORE_NAME}/msg_store/msg" 

source $BALER_ENV

CMD="sos_part_delete -C ${BALER_STORE} ${PARTNAME}"
`echo ${CMD}`




