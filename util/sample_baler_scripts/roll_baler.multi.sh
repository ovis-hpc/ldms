#!/bin/bash
# ./roll_baler.multi.sh store.master 
# It will use the date for the new part, it will make it primary
LOCAL_STORE_NAME=$1

BALER_TOOL_DIR="/home/gentile/Baler/baler_tools"
BALER_ENV="${BALER_TOOL_DIR}/baler_env.sh"
BALER_STORE_DIR="/home/gentile/Baler/mutrino"

source $BALER_ENV

CURREPOCH=`date +%s`
CMD="${BALER_TOOL_DIR}/part_create.sh ${LOCAL_STORE_NAME} ${CURREPOCH}"
`echo ${CMD}`

CMD="${BALER_TOOL_DIR}/part_modify_baler.sh ${LOCAL_STORE_NAME} ${CURREPOCH} active"
`echo ${CMD}`

CMD="${BALER_TOOL_DIR}/part_modify_baler.sh ${LOCAL_STORE_NAME} ${CURREPOCH} primary"
`echo ${CMD}`


