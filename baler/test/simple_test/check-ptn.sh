#!/bin/bash
source common.sh
__check_config

bquery -s $BSTORE -t ptn |
	grep '^ ' |
	sed 's/^ \+[0-9]\+ //' |
	sort > tmp-ptn.txt

./gen-ptns.pl | sort > tmp-ptn-cmp.txt

diff tmp-ptn.txt tmp-ptn-cmp.txt
