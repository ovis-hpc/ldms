#!/bin/bash -x
export CFLAGS="-O2 -g -pipe -Wall -Wp,-D_FORTIFY_SOURCE=2 -fexceptions -fstack-protector --param=ssp-buffer-size=4 -m64 -march=native"
if test -f lib/packaging/ovis-lib-toss.spec.in; then
	# where we ultimately want things deployed
	prefix=/opt/ovis
	expected_event2_prefix=/usr
	export CC=gcc
	if test "x$SNLCLUSTER" = "xchama"; then
		export CC=gcc46
		expected_event2_prefix=/opt/ovis
	fi
	if test -f $expected_event2_prefix/include/event2/event.h; then
		echo "Found $expected_event2_prefix/include/event2/event.h. Good."
	else
		echo "You forgot to install libevent2 rpms in $expected_event2_prefix or you need to edit $0"
		exit 1
	fi
	if test -f ldms/configure; then
		echo "Found ldms/configure. Good."
	else
		echo "You forgot to autogen.sh at the top or you need to edit $0"
		exit 1
	fi

	echo "reinitializing .build-all"
	rm -rf .build-all
	mkdir .build-all
	cd .build-all
	mkdir lib ldms sos distroot
	DISTROOT=`pwd`/distroot
	# where things go during the nonroot build without intermediate root installs
	tprefix=$DISTROOT/$prefix
	expected_ovislib_prefix=$tprefix
	expected_sos_prefix=$tprefix
	allconfig="--prefix=$tprefix --enable-rdma --enable-ssl --enable-zaptest --enable-swig --with-ovis-lib=$expected_ovislib_prefix --with-libevent=$expected_event2_prefix --enable-rdma --enable-sos --with-sos=$expected_sos_prefix --with-ovis-prefix=$expected_ovislib_prefix --disable-perfevent --disable-rpath" 
	export LD_LIBRARY_PATH=$DISTROOT/opt/ovis/lib:$LD_LIBRARY_PATH
	(cd lib; ../../lib/configure $allconfig && make  && make install) && \
	(cd distroot; : find . -name '*.la' -exec /bin/rm {} \; ) && \
	(cd sos; ../../sos/configure  $allconfig && make  && make install) && \
	(cd distroot; : find . -name '*.la' -exec /bin/rm {} \; ) && \
	(cd ldms && LDFLAGS="-L$DISTROOT/opt/ovis/lib" ../../ldms/configure $allconfig && make  && make install ) && \
	(cd distroot; : find . -name '*.la' -exec /bin/rm {} \; ) 
else
	echo "this must be run from the top of ovis source tree"
	exit 1
fi
