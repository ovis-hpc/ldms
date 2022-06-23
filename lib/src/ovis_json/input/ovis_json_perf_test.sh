#! /bin/bash
tmp=$(mktemp -t $USER.ovis_json_perf_test.XXXXXX)
$BIN/ovis_json_perf_test 100000 > $tmp
if ! test -f $tmp; then
	echo "ERROR: no output file $tmp"
	exit 1
fi
st=$(cat $tmp |grep sprintf  |sed -e 's/.* //g')
jb=$(cat $tmp |grep jbuf  |sed -e 's/.* //g')
slowdown=$(echo "scale=2;$jb/$st" |bc)
echo jbuf/sprintf duration ratio is $slowdown
