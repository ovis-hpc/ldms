#!/bin/bash

if [[ -z "$CONFIG" ]]; then
	CONFIG="./config.sh"
fi

if [[ -t 1 || -n "$COLOR" ]]; then
	# color code
	BLK='\033[30m'
	RED='\033[31m'
	GRN='\033[32m'
	YLW='\033[33m'
	BLU='\033[34m'
	PPL='\033[35m'
	TEA='\033[36m'
	WHT='\033[37m'
	BLD='\033[1m'
	NC='\033[0m'
else
	BLK=
	RED=
	GRN=
	YLW=
	BLU=
	PPL=
	TEA=
	WHT=
	BLD=
	NC=
fi

__err() {
	echo -e "${BLD}${RED}ERROR:${NC} $@"
}

__info() {
	echo -e "${BLD}${YLW}INFO:${NC} $@"
}

__err_exit() {
	__err $@
	exit -1
}

__check_config() {
	# $1 is prog name
	if [[ -n "$CONFIG" ]]; then
		source "$CONFIG"
	else
		__err_exit "usage: CONFIG=<CONFIG.SH> $1"
	fi
	if [[ ! -f $CONFIG ]]; then
		__err_exit "config file not found, CONFIG: $CONFIG"
	fi
}

__has_operf() {
	if which operf >/dev/null 2>&1; then
		# found
		return 0
	else
		# not found
		return 127
	fi
}
