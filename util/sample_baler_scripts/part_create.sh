#!/bin/bash
# ./part_create.sh store.master foo

LOCAL_STORE_NAME=$1
PARTNAME=$2

BALER_TOOL_DIR="/home/gentile/Baler/baler_tools"
BALER_ENV="${BALER_TOOL_DIR}/baler_env.sh"
BALER_STORE_DIR="/home/gentile/Baler/mutrino"
BALER_STORE="${BALER_STORE_DIR}/${LOCAL_STORE_NAME}/msg_store/msg" 

source $BALER_ENV
#CURREPOCH=`date +%s`
#CMD="sos_part_create -C ${BALER_STORE} ${CURREPOCH}"

CMD="sos_part_create -C ${BALER_STORE} ${PARTNAME}"
`echo ${CMD}`




