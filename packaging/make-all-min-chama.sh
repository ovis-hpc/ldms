#!/bin/bash -x
export CFLAGS="-O2 -g -pipe -Wall -Wp,-D_FORTIFY_SOURCE=2 -fexceptions -fstack-protector --param=ssp-buffer-size=4 -m64 -march=native"
export LOCALEVENT=1 ; # else expect /opt/ovis
if test -f lib/packaging/ovis-lib-toss.spec.in; then
	# where we ultimately want things deployed
	prefix=`pwd`/.inst
	rm -rf $prefix/lib $prefix/include $prefix/lib64
	mkdir $prefix
	expected_event2_prefix=/usr
	export CC=gcc
	export CXX=g++
	if test "x$SNLCLUSTER" = "xchama"; then
		export CC=gcc46
		if test "$LOCALEVENT" = "1"; then
			expected_event2_prefix=$prefix
		else
			expected_event2_prefix=/opt/ovis
		fi
	fi
	if test -f $expected_event2_prefix/include/event2/event.h; then
		echo "Found $expected_event2_prefix/include/event2/event.h. Good."
	else
		if test "$LOCALEVENT" = "1"; then
			if test -f $expected_event2_prefix/include/event2/event.h; then
				echo "Found local libevent built already. Good."
			else
				mkdir .build-event
				cp ../event/libevent-2.0.21-stable.tar.gz .build-event
				(cd .build-event && ../CHAMA.libevent2 $prefix )
				if test -f $expected_event2_prefix/include/event2/event.h; then
					echo "Built and installed $expected_event2_prefix/include/event2/event.h. Good."
				else
					echo "Local libevent build failed"
					exit 1
				fi
			fi
		else
			echo "You forgot to install libevent2 rpms in $expected_event2_prefix or you need to edit $0"
			exit 1
		fi
	fi
	if test -f ldms/configure; then
		echo "Found ldms/configure. Good."
	else
		echo "You forgot to autogen.sh at the top or you need to edit $0 or you need to use a released tarred version."
		exit 1
	fi

	echo "reinitializing .build-all"
	rm -rf .build-all
	mkdir .build-all
	cd .build-all
	mkdir lib ldms distroot
	DISTROOT=`pwd`/distroot
	# where things go during the nonroot build without intermediate root installs
	# note: libevent doesn't support destdir
	tprefix=$prefix
	expected_ovislib_prefix=$tprefix
	expected_sos_prefix=/badsos
	allconfig="--prefix=$prefix --enable-rdma --enable-ssl --disable-zaptest --enable-swig --with-ovis-lib=$expected_ovislib_prefix --with-libevent=$expected_event2_prefix --disable-sos --disable-zap --disable-perfevent --disable-rpath" 
	export LD_LIBRARY_PATH=$prefix/lib:$LD_LIBRARY_PATH
	(cd lib; ../../lib/configure $allconfig && make  && make install && ../../packaging/nola.sh $prefix) && \
	(cd ldms && LDFLAGS="-L$prefix/lib" ../../ldms/configure $allconfig && make  && make install  && ../../packaging/nola.sh $prefix)
else
	echo "this must be run from the top of ovis source tree"
	exit 1
fi
