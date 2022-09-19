#! /bin/bash
in=$1
tac $in | \
grep -v '^# ' | \
grep -v 'option ' | \
grep -v 'listen ' | \
grep -v 'config ' | \
sed -e 's/strgp_start/strgp_stop/' | \
sed -e 's/strgp_prdcr_add/strgp_prdcr_del/' | \
sed -e 's/\(strgp_add\)\( *\)\(name=\)\([a-zA-Zi0-9{}$._-]*\)\(.*\)/strgp_del \3\4/' | \
sed -e 's/load /term /' | \
sed -e 's/\(updtr_start\)\( *\)\(name=\)\([a-zA-Zi0-9{}$._-]*\)\(.*\)/updtr_stop \3\4/' | \
sed -e 's/updtr_match_add/updtr_match_del/' | \
sed -e 's/updtr_prdcr_add/updtr_prdcr_del/' | \
sed -e 's/\(updtr_add\)\( *\)\(name=\)\([a-zA-Zi0-9{}$._-]*\)\(.*\)/updtr_del \3\4/' | \
sed -e 's/\(prdcr_start_regex\)\( *\)\(name=\)\([a-zA-Zi0-9{}$._-]*\)\(.*\) /prdcr_stop_regex \3\4/' | \
sed -e 's/\(prdcr_start\)\( *\)\(name=\)\([a-zA-Zi0-9{}$._-]*\)\(.*\)/prdcr_stop \3\4/' | \
sed -e 's/\(prdcr_add\)\( *\)\(name=\)\([a-zA-Zi0-9{}$._-]*\)\(.*\)/prdcr_del \3\4/' | \
sed -e 's/prdcr_subscribe/prdcr_unsubscribe/' | \
sed -e 's/\(start\)\( *\)\(name=\)\([a-zA-Zi0-9{}$._-]*\)\(.*\)/stop \3\4 /'
cat << EOF
daemon_exit
EOF
