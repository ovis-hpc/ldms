#!/bin/bash 
if ! test -f ldms/config.h.in; then
echo 'run from source tree top'
exit 1
fi
targets="ldms/src lib/src sos/src"
filter="grep -v configure |grep -v make "
for i in 65536 32768 16384 8192 4096 2048 1024 512 256 126 64 32 16; do
fgrep -rn \[${i}\] $targets  | grep -v config |grep -v Make
done
