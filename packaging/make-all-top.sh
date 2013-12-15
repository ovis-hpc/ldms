#!/bin/bash -x
export CFLAGS="-O2 -g -pipe -Wall -Wp,-D_FORTIFY_SOURCE=2 -fexceptions -fstack-protector --param=ssp-buffer-size=4 -m64 -march=native"
if test -f lib/packaging/ovis-lib-toss.spec.in; then
	# where we ultimately want things deployed
	prefix=/opt/ovis
	expected_event2_prefix=/usr
	export CC=gcc
	export CXX=g++
	if test -f $expected_event2_prefix/include/event2/event.h; then
		echo "Found $expected_event2_prefix/include/event2/event.h. Good."
	else
		echo "You forgot to install libevent2 rpms in $expected_event2_prefix or you need to edit $0"
		exit 1
	fi
	if test -f ldms/configure; then
		echo "Found ldms/configure. Good."
	else
		echo "You forgot to autogen.sh at the top or you need to edit $0 or you need to use a released tarred version."
		exit 1
	fi
	srctop=`pwd`
	echo "reinitializing .build-all2"
	rm -rf .build-all2
	mkdir .build-all2
	cd .build-all2
	mkdir distroot
	DISTROOT=`pwd`/distroot
	allconfig="--prefix=$prefix --enable-rdma --enable-ssl --enable-zaptest --enable-swig --with-libevent=$expected_event2_prefix --enable-sos --disable-perfevent --disable-rpath" 
	../configure $allconfig && \
	make && \
	DESTDIR=$DISTROOT make install
else
	echo "this must be run from the top of ovis source tree"
	exit 1
fi
