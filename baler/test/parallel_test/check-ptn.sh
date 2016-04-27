#!/bin/bash
source common.sh
__check_config

bhquery -t ptn |
	grep '^ ' |
	sed 's/^\( \+\([^ ]\+\)\)\{6\} *//' |
	sort > tmp-ptn.txt

./gen-ptns.pl | sort > tmp-ptn-cmp.txt

diff tmp-ptn.txt tmp-ptn-cmp.txt
