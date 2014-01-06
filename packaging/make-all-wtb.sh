#!/bin/bash -x
# a userspace build script for wtb using autotools from redhat6 in /project/ovis/buildtools
export CC=gcc44
export CXX=g++44
export CFLAGS="-O2 -g -pipe -Wall -Wp,-D_FORTIFY_SOURCE=2 -fexceptions -fstack-protector --param=ssp-buffer-size=4 -m64 -march=native"
if test -f lib/packaging/ovis-lib-toss.spec.in; then
	# where we ultimately want things deployed
	prefix=$HOME/ovis/Build
	expected_event2_prefix=$prefix/event
	export LD_LIBRARY_PATH=$prefix/lib:/projects/ovis/buildtools/lib:$expected_event2_prefix/lib
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
	echo "reinitializing .build-all"
	rm -rf .build-all
	mkdir .build-all
	cd .build-all
	allconfig="--prefix=$prefix --enable-rdma --enable-ssl --enable-zaptest --with-libevent=$expected_event2_prefix --enable-sos --disable-perfevent --disable-rpath" 
	LDFLAGS=-L/projects/ovis/buildtools/lib GLIB20_CFLAGS=-I/usr/lib64/glib-2.0/include GLIB20_LIBS=-L/lib64 ../configure $allconfig && \
	make && \
	make install
else
	echo "this must be run from the top of ovis source tree"
	exit 1
fi
