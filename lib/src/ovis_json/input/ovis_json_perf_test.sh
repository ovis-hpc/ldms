#! /bin/bash
tmp=$(mktemp -t $USER.ovis_json_perf_test.XXXXXX)
$BIN/ovis_json_perf_test 100000 > $tmp
#valgrind -v --track-origins=yes $BIN/ovis_json_perf_test 100 > $tmp
if ! test -f $tmp; then
	echo "ERROR: no output file $tmp"
	exit 1
fi
st=$(cat $tmp |grep "sprintf time" |sed -e 's/.* //g')
je=$(cat $tmp |grep "elements time" |sed -e 's/.* //g')
jb=$(cat $tmp |grep "fmt time" |sed -e 's/.* //g')
eslowdown=$(echo "scale=2;$je/$st" |bc)
bslowdown=$(echo "scale=2;$jb/$st" |bc)
echo elements/sprintf duration ratio is $eslowdown
echo bulkfmt/sprintf duration ratio is $bslowdown
