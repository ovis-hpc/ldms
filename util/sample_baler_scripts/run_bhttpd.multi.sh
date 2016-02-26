#!/bin/bash
# ./run_bhttpd.multi.sh master store.master 205.137.89.103 18888
# ./run_bhttpd.multi.sh slave store.slave 205.137.89.103 18889
ROLE=$1
LOCAL_STORE_NAME=$2
ADDR=$3
BHTTPDPORT=$4

LOGNAME="bhttpd.${ROLE}.log"
BALER_TOOL_DIR="/home/gentile/Baler/baler_tools"
BALER_ENV="${BALER_TOOL_DIR}/baler_env.sh"
BALER_STORE_DIR="/home/gentile/Baler/mutrino"

source $BALER_ENV

CMD="bhttpd -s ${BALER_STORE_DIR}/${LOCAL_STORE_NAME} -l ${BALER_STORE_DIR}/${LOGNAME} -a ${ADDR} -p ${BHTTPDPORT}"
`echo ${CMD}`

