#!/bin/bash -x
echo BUILDING FOR UBUNTU 12.04
export CC=gcc
export LD_LIBRARY_PATH=$HOME/opt/ovis/lib:$LD_LIBRARY_PATH
if test -f lib/packaging/ovis-lib-toss.spec.in; then
	prefix=$HOME/opt/ovis
	expected_event2_prefix=/usr
	expected_ovislib_prefix=$prefix
	expected_sos_prefix=$prefix

	allconfig="--prefix=$prefix --enable-rdma --enable-ssl --enable-zaptest --enable-swig --with-ovis-lib=$expected_ovislib_prefix --enable-sos --with-sos=$expected_sos_prefix --with-ovis-prefix=$expected_ovislib_prefix "
	allconfig="--prefix=$prefix --enable-rdma --enable-ssl --disable-zap --enable-swig --with-ovis-lib=$expected_ovislib_prefix --enable-sos --with-sos=$expected_sos_prefix --with-ovis-prefix=$expected_ovislib_prefix CFLAGS=-Wall"


	if test -f $expected_event2_prefix/include/event2/event.h; then
		echo "Found $expected_event2_prefix/include/event2/event.h. Good."
	else
		echo "You forgot to install libevent -dev package or you need to edit $0"
		exit 1
	fi

	echo "reinitializing .build-all"
	rm -rf .build-all
	mkdir .build-all
	cd .build-all
	mkdir lib ldms sos
	(cd lib; ../../lib/configure $allconfig && make  && make install) && \
	(cd sos; ../../sos/configure  $allconfig && make  && make install) && \
	cd ldms && LDFLAGS="-L$HOME/opt/ovis/lib" ../../ldms/configure $allconfig && make  && make install
else
	echo "this must be run from the top of ovis source tree"
	exit 1
fi
