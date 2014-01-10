#!/bin/bash -x
echo BUILDING FOR UBUNTU 12.04
export CC=gcc
export LD_LIBRARY_PATH=$HOME/opt/ovis/lib:$LD_LIBRARY_PATH
if test -f lib/packaging/ovis-lib-toss.spec.in; then
	prefix=$HOME/opt/ovis
	expected_event2_prefix=/usr
	expected_ovislib_prefix=$prefix
	expected_sos_prefix=$prefix

	allconfig="--prefix=$prefix --enable-rdma --enable-ssl --disable-zap  --with-ovis-lib=$expected_ovislib_prefix --enable-sos --with-sos=$expected_sos_prefix "
CFLAGS='-Wall -g'


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
	(cd lib; ../../lib/configure CFLAGS="$CFLAGS" $allconfig  && make   && make install) && \
	(cd sos; ../../sos/configure CFLAGS="$CFLAGS" $allconfig  && make  && make install) && \
	cd ldms && LDFLAGS="-L$HOME/opt/ovis/lib" ../../ldms/configure CFLAGS="$CFLAGS" $allconfig && make  && make install
else
	echo "this must be run from the top of ovis source tree"
	exit 1
fi
