#!/bin/bash -x
export CC=gcc
export LD_LIBRARY_PATH=$HOME/opt/ovis/lib:$HOME/autotoolsrh64/lib:$LD_LIBRARY_PATH
if test -f lib/packaging/ovis-lib-toss.spec.in; then
	prefix=$HOME/opt/ovis
	expected_event2_prefix=$HOME/event/opt/ovis
	expected_ovislib_prefix=$prefix
	expected_sos_prefix=$prefix
	allconfig="--prefix=$prefix --enable-rdma --enable-ssl --enable-zaptest --enable-swig --with-ovis-lib=$expected_ovislib_prefix --with-libevent=$expected_event2_prefix --enable-rdma --enable-ncsa-unified --enable-sos --with-sos=$expected_sos_prefix --with-ovis-prefix=$expected_ovislib_prefix --disable-dependency-tracking --disable-perfevent --with-ldflags-readline=-ltermcap  "
	glibconfig="GLIB20_CFLAGS=-I/usr/lib64/glib-2.0/include GLIB20_LIBS=-L/lib64"

	if test -f $expected_event2_prefix/include/event2/event.h; then
		echo "Found $expected_event2_prefix/include/event2/event.h. Good."
	else
		echo "You forgot to install libevent2 rpms in $expected_event2_prefix or you need to edit $0"
		exit 1
	fi

	echo "reinitializing .build-all"
	rm -rf .build-all
	mkdir .build-all
	cd .build-all
	mkdir lib ldms sos
	(cd lib; ../../lib/configure $allconfig && make  && make install) && \
	(cd sos; ../../sos/configure  $allconfig && make  && make install) && \
	cd ldms && LDFLAGS="-L/apps/x86_64/tools/python/Python-2.6.4/lib -L/home/baallan/opt/ovis/lib" GLIB20_CFLAGS=-I/usr/lib64/glib-2.0/include GLIB20_LIBS=-L/lib64 ../../ldms/configure $allconfig && make  && make install
else
	echo "this must be run from the top of ovis source tree"
	exit 1
fi
