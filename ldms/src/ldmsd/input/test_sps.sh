#! /bin/bash
/bin/rm -f ./test_sps.daemon.log ./test_sps.pid test_sps.log.2 test_sps.log.1
# /bin/rm -rf ./blobs
# cat << EOF > test_sps.subs.conf
# load name=blob_stream_writer plugin=blob_stream_writer
# config name=blob_stream_writer path=. container=blobs stream=teststream timing=1 types=1
# config name=blob_stream_writer path=. container=blobs stream=myteststream timing=1 types=1
# EOF

# $BIN/ldmsd -x sock:10444:localhost -a none -l ./test_sps.daemon.log  -r ./test_sps.pid -v DEBUG -c ./test_sps.subs.conf
$BIN/ldmsd -x sock:10444:localhost -a none -l ./test_sps.daemon.log  -r ./test_sps.pid -v DEBUG
sleep 1
dpid=$(cat ./test_sps.pid)
ps $dpid
sleep 2
targs1="stream=myteststream client=sock:localhost:10444:none:2 timeout=5 blocking=1 debug_level=0 send_log=test_sps.log.1"
targs2="stream=myteststream client=sock:localhost:10444:none:2 timeout=5 debug_level=2 send_log=test_sps.log.2"
echo ./test_sps $targs1
#gdb $BIN/test_sps
$BIN/test_sps $targs1
echo
echo
echo ./test_sps $targs2
$BIN/test_sps $targs2
kill $(cat ./test_sps.pid)
