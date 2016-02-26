#!/bin/bash
# ./run_baler.multi.sh master store.master baler.master.cfg 54000 
# ./run_baler.multi.sh slave store.slave baler.slave.cfg 54000 
ROLE=$1
LOCAL_STORE_NAME=$2
LOCAL_CFG=$3
PORT=$4



LOGNAME="baler.${ROLE}.log"
BALER_TOOL_DIR="/home/gentile/Baler/baler_tools"
BALER_ENV="${BALER_TOOL_DIR}/baler_env.sh"
BALER_STORE_DIR="/home/gentile/Baler/mutrino"

source $BALER_ENV

#CMD="balerd -l ${BALER_STORE_DIR}/${LOGNAME} -s ${BALER_STORE_DIR}/${LOCAL_STORE_NAME} -C ${BALER_TOOL_DIR}/${LOCAL_CFG} -m ${ROLE} -p ${PORT}  -v DEBUG -I 1 -O 1"
CMD="balerd -l ${BALER_STORE_DIR}/${LOGNAME} -s ${BALER_STORE_DIR}/${LOCAL_STORE_NAME} -C ${BALER_TOOL_DIR}/${LOCAL_CFG} -m ${ROLE} -p ${PORT}  -I 1 -O 1"

`echo ${CMD}`




