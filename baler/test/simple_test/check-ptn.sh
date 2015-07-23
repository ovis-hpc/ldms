#!/bin/bash
source common.sh
__check_config

bquery -s $BSTORE -t ptn |
	grep -m 3 '^ ' |
	sed 's/^ \+[0-9]\+ //' |
	sort > tmp-ptn.txt

{ cat <<EOF
* is * sheep.
* see fire!
This is * test message from *, *: *
EOF
} > tmp-ptn-cmp.txt

diff tmp-ptn.txt tmp-ptn-cmp.txt
