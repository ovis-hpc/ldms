#!/bin/bash
LOCAL_STORE_NAME=$1
DIRECTIVE=$2

BALER_TOOL_DIR="/home/gentile/Baler/baler_tools"
BALER_ENV="${BALER_TOOL_DIR}/baler_env.sh"
BALER_STORE_DIR="/home/gentile/Baler/mutrino"
BALER_STORE_HEAD="${BALER_STORE_DIR}/${LOCAL_STORE_NAME}" 

source $BALER_ENV

CMD="bquery --store-path=${BALER_STORE_HEAD} -t ${DIRECTIVE}"
`echo ${CMD}`




