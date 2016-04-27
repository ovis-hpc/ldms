#!/bin/bash
source ./common.sh
__check_config "$0"

# Put query test cases here
for X in $(ls check-*.*); do
	if [ ! -x $X ]; then
		continue;
	fi
	__info "${BLD}${YLW}$X ..........................${NC}"
	time -p ./$X
	if (($?)); then
		__err "................... $X ${BLD}${RED}failed${NC}"
	else
		__info "................... $X ${BLD}${GRN}success${NC}"
	fi
done
