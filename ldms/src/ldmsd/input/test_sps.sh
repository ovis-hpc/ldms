#! /bin/bash
$BIN/ldmsd -x sock:10444:localhost -a none -l ./test_sps.daemon.log  -r ./test_sps.pid
sleep 1
dpid=$(cat ./test_sps.pid)
ps $dpid
sleep 2
echo ./test_sps stream=myteststream client=sock:localhost:10444:none:2 timeout=5 blocking=1
gdb ./test_sps 
kill -9 $(cat ./test_sps.pid)
