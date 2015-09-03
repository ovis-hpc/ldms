#!/bin/bash

source ./common.sh

__check_config "$0"

rm -rf $BSTORE $BLOG tmp-ptn.txt $BSTAT $BSTATM
