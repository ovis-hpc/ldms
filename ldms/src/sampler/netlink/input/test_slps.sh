#! /bin/bash
# this tests using both ldmsd instances created here
# and root-owned instances assumed to be running on
# sock:411:ovis (with tester assumed to have auth key file access)
# sock:412:munge
# If these root daemons are missing, the test will have some results
# containing "pub=0".

# all outputs must match the rm next.
/bin/rm -f ./test_slps.*
# /bin/rm -rf ./blobs
# cat << EOF > test_slps.subs.conf
# load name=blob_stream_writer plugin=blob_stream_writer
# config name=blob_stream_writer path=. container=blobs stream=teststream timing=1 types=1
# config name=blob_stream_writer path=. container=blobs stream=myteststream timing=1 types=1
# EOF
if test -z "$LDMS_AUTH"; then
	LDMS_AUTH=none
fi

set_kokkos () {
	export KOKKOS_LDMS_PORT=10445
	#export KOKKOS_LDMS_HOST=127.0.0.2
	export KOKKOS_LDMS_HOST=127.0.0.1
	export KOKKOS_LDMS_STREAM=kokkos-stream2
	export KOKKOS_LDMS_SEND_LOG=test_slps.log.3
	export KOKKOS_LDMS_XPRT=sock
	export KOKKOS_LDMS_AUTH=$LDMS_AUTH
	export KOKKOS_LDMS_TIMEOUT=2
	export KOKKOS_LDMS_RECONNECT=59
}

timeout=1m
export ZAP_MAX_IO=8
export ZAP_IO_MAX=8
$BIN/ldmsd -x sock:10444:localhost -a $LDMS_AUTH -l ./test_slps.daemon.1.log  -r ./test_slps.pid.1 -v DEBUG
$BIN/ldmsd -x sock:10445:localhost -a $LDMS_AUTH -l ./test_slps.daemon.2.log  -r ./test_slps.pid.2 -v DEBUG
sleep 1
dpid=$(cat ./test_slps.pid.1)
ps $dpid
dpid=$(cat ./test_slps.pid.2)
ps $dpid
sleep 2
targs1="stream=myteststream target=sock:10444:$LDMS_AUTH:2:localhost timeout=5 blocking=1 debug_level=0 send_log=test_slps.log.1"
targs2="stream=myteststream target=sock:10445:$LDMS_AUTH:2:localhost timeout=5 debug_level=2 send_log=test_slps.log.2"
env | grep KOKKOS
echo
echo ./test_slps $targs1
#valgrind -v --show-leak-kinds=all --trace-children=yes --track-origins=yes --leak-check=full $TESTBIN/test_slps $targs1
timeout -s TERM -k 10 $timeout $TESTBIN/test_slps $targs1
case $? in
0)
	echo no-kokkos blocking ok
	;;
1*)
	echo no-kokkos blocking test timed out
	exit 1
	;;
esac
echo
echo ./test_slps $targs2
#valgrind -v --track-origins=yes --leak-check=full $TESTBIN/test_slps $targs2
timeout -s TERM -k 10 $timeout $TESTBIN/test_slps $targs2
case $? in
0)
	echo no-kokkos nonblocking ok
	;;
1*)
	echo no-kokkos nonblocking test timed out
	exit 1
	;;
esac

set_kokkos

echo
echo "with env kokkos"
env | grep KOKKOS
echo "./test_slps $targs1.kokkos debug_level=2"
#valgrind -v  --tool=drd $TESTBIN/test_slps $targs1.kokkos debug_level=0
#valgrind -v --leak-check=full --show-leak-kinds=all --show-reachable=yes $TESTBIN/test_slps $targs1.kokkos debug_level=0
timeout -s TERM -k 10 $timeout $TESTBIN/test_slps $targs1.kokkos debug_level=0
case $? in
0)
	echo kokkos nonblocking ok
	;;
1*)
	echo kokkos nonblocking test timed out
	exit 1
	;;
esac
echo
echo ./test_slps $targs2.kokkos
#valgrind -v --track-origins=yes --leak-check=full --show-leak-kinds=all $TESTBIN/test_slps $targs2.kokkos
timeout -s TERM -k 10 $timeout $TESTBIN/test_slps $targs2.kokkos
case $? in
0)
	echo kokkos blocking ok
	;;
1*)
	echo kokkos blocking test timed out
	exit 1
	;;
esac

kill $(cat ./test_slps.pid.1)
kill $(cat ./test_slps.pid.2)
rm -f ./test_slps.pid.* ./test_slps.pid.*.version
