#!/bin/bash
error() {
  echo "ERROR:" $@
  exit -1
}
ps_state() {
  _PID=$1
  if [[ -d /proc/${_PID} ]]; then
    _STATE=$(cat /proc/${_PID}/stat | cut -f 3 -d ' ')
    [[ ${_STATE} = "Z" ]] && echo "zombie" || echo "alive"
  else
    echo "dead"
  fi
}
ps_check() {
  local _NAME=$1
  local _PID=$2
  echo -n "Checking $_NAME ($_PID) ..."
  local _S=$(ps_state $_PID)
  echo "$_S"
  [[ "$_S" = "alive" ]] || error "process $_NAME ($_PID) is not alive"
}
set -x
set -e
pgrep munged || sudo -u munge munged
cd /test
# samp
echo "starting samp-4.3.3 w/munge"
ldmsd-4.3.3.sh -c samp-4.3.3.conf -l logs/samp-4.3.3.log -v INFO -x sock:10000 -a munge
# agg1
echo "starting agg-4.3.3 w/munge"
ldmsd-4.3.3.sh -c agg-4.3.3.conf -l logs/agg-4.3.3.log -v INFO -x sock:10001 -a munge
sleep 10
# agg2
echo "starting agg-4 w/munge"
ldmsd-4.sh -c agg-4.conf -l logs/agg-4.log -v INFO -x sock:10002 -a munge &
sleep 10
# check that they're running
SAMP_433_PID=$(pgrep -f samp-4.3.3.conf)
AGG_433_PID=$(pgrep -f agg-4.3.3.conf)
AGG_4_PID=$(pgrep -f agg-4.conf)
echo "--- ldmsd's ---"
pgrep -a ldmsd
echo "---------------"
[[ -n "${SAMP_433_PID}" ]] || error "samp-4.3.3 is not running"
[[ -n "${AGG_433_PID}" ]] || error "agg-4.3.3 is not running"
[[ -n "${AGG_4_PID}" ]] || error "agg-4 is not running"
# ldms_ls-4 to agg-4.3.3
echo -n "ldms_ls agg-4.3.3 -a munge ... "
D0=$( ldms_ls-4.sh -x sock -p 10001 -a munge -h 127.0.0.1 )
echo "result: ${D0}"
# ldms_ls-4 to agg-4
echo -n "ldms_ls agg-4 -a munge ... "
D1=$( ldms_ls-4.sh -x sock -p 10002 -a munge )
echo "result: ${D1}"
[[ "${D0}" = "ovis-4.3.3/meminfo" ]] || error "unexpected ldms_ls agg-4.3.3 result"
[[ "${D1}" = "ovis-4.3.3/meminfo" ]] || error "unexpected ldms_ls agg-4 result"
# ldms_ls-4.3.3 to agg-4
echo -n "ldms_ls(4.3.3) agg-4 -a munge ... "
D2=$( ldms_ls-4.3.3.sh -x sock -p 10002 -a munge )
echo "result: ${D2}"
[[ "${D2}" = "ovis-4.3.3/meminfo" ]] || error "unexpected ldms_ls(4.3.3) agg-4 result"
# ldms_ls-4 to agg-4.3.3 -l
echo -n "ldms_ls -l agg-4.3.3 -a munge ... "
D0=$( ldms_ls-4.sh -l -x sock -p 10001 -a munge -h 127.0.0.1 | grep MemTotal | sed 's/\s\+/ /g' )
echo "${D0}"
[[ -n "${D0}" ]] || error "cannot get MemTotal from ldms_ls -l"
# ldms_ls-4 to agg-4 -l
echo -n "ldms_ls -l agg-4 -a munge ... "
D1=$( ldms_ls-4.sh -l -x sock -p 10002 -a munge | grep MemTotal | sed 's/\s\+/ /g' )
echo "${D1}"
[[ -n "${D1}" ]] || error "cannot get MemTotal from ldms_ls -l"
# ldms_ls-4.3.3 to agg-4 -l
echo -n "ldms_ls-4.3.3 -l agg-4 -a munge ... "
D2=$( ldms_ls-4.3.3.sh -l -x sock -p 10002 -a munge | grep MemTotal | sed 's/\s\+/ /g' )
echo "${D2}"
[[ -n "${D2}" ]] || error "cannot get MemTotal from ldms_ls -l"
# check if they're the same
[[ "${D0}" == "${D1}" ]] || error "agg-4.3.3 MemTotal != agg-4 MemTotal"
[[ "${D1}" == "${D2}" ]] || error "ldms_ls-4 MemTotal != ldms_ls-4.3.3 MemTotal"
# kill samp-4.3.3 so that the set disappeared from agg-4.3.3
echo "Killing samp-4.3.3"
kill ${SAMP_433_PID}
sleep 4
echo "--- ldmsd's ---"
pgrep -a ldmsd
echo "---------------"
echo "--- ldmsd's ---"
ps ax | grep ldmsd
echo "---------------"
# ldms_ls to agg-4.3.3
echo -n "ldms_ls agg-4.3.3 -a munge ... "
D0=$(ldms_ls-4.sh -x sock -p 10001 -a munge -h 127.0.0.1 )
[[ -z "${D0}" ]] && echo "dir(agg-4.3.3) is empty (good)" || error "ERROR: dir(agg-4.3.3) is not empty"
# check aggregators
ps_check agg-4.3.3 ${AGG_433_PID}
ps_check agg-4 ${AGG_4_PID}
# ldms_ls to agg-4
echo -n "ldms_ls agg-4 -a munge ... "
D1=$(ldms_ls-4.sh -x sock -p 10002 -a munge )
[[ -z "${D1}" ]] && echo "dir(agg-4) is empty (good)" || error "ERROR: dir(agg-4) is not empty"
kill ${AGG_433_PID}
kill ${AGG_4_PID}
echo "DONE!"
