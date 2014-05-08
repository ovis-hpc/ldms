#!/bin/bash

get_pid() {
	full=$1
	base=$(basename $full)
	ps -C $base h -o pid,cmd | grep $full |
		sed 's/ *\([0-9]\+\) .*/\1/'
}
