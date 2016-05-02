#!/bin/bash

export BLOG=balerd.log
export BHTTPDLOG=bhttpd.log
export BSTORE=store
export BSTAT=balerd.stat
export BSTATM=balerd.statm
export BCONFIG=balerd.cfg
# Baler configuration file (balerd.cfg) will be automatically generated.

export BTEST_ENG_DICT="../eng-dictionary"
export BTEST_HOST_LIST="./host.list"
export BTEST_BIN_RSYSLOG_PORT=10000
export BTEST_BHTTPD_PORT=18000
export BTEST_MASTER_PORT=30000
export BTEST_TS_BEGIN=1435294800
export BTEST_TS_LEN=$((1*3600*24))
export BTEST_TS_INC=$((3600))
export BTEST_NODE_BEGIN=0
export BTEST_NODE_LEN=64
export BTEST_N_PATTERNS=8
export BTEST_N_DAEMONS=4
export BTEST_BHTTPD_LOGLEVEL=DEBUG

# Give any value to this variable to enable offline partition
export BTEST_ENABLE_OFFLINE=Y

export BOUT_THREADS=1
export BIN_THREADS=1
export BLOG_LEVEL=INFO

source ./env.sh
