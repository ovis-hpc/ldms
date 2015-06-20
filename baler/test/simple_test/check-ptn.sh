#!/bin/bash
source common.sh
__check_config

bquery -s $BSTORE -t ptn > tmp-ptn.txt
diff tmp-ptn.txt ptn.txt
