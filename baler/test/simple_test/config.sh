#!/bin/bash

export BLOG=./balerd.log
export BSTORE=./store

# Baler configuration file (balerd.cfg) will be automatically generated.

export BTEST_ENG_DICT="../eng-dictionary"
export BTEST_HOST_LIST="../host.list"
export BTEST_BIN_RSYSLOG_PORT=33333
export BTEST_TS_BEGIN=1435294800
export BTEST_TS_LEN=$((3600*24))
export BTEST_TS_INC=600
export BTEST_NODE_BEGIN=0
export BTEST_NODE_LEN=64

export BOUT_THREADS=1
export BIN_THREADS=1
export BLOG_LEVEL=INFO

source ./env.sh
