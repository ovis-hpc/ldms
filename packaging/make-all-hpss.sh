#!/bin/bash -x
# relies on autotools from rh64 backported for build of liebevent and ldms
export PATH=$HOME/autotoolsrh64/bin:$PATH
export CC=gcc
export LD_LIBRARY_PATH=$HOME/opt/ovis/lib:$HOME/autotoolsrh64/lib:$LD_LIBRARY_PATH
if ! test -d $HOME/autotoolsrh64; then
	echo "for rh5 build, you need the autotools set from redhat 6"
	echo "installed in $HOME/autotoolsrh64"
	echo "see glory-buildauto.sh for hints on how to build that."
	exit 1
fi
if test -f lib/packaging/ovis-lib-toss.spec.in; then
	prefix=$HOME/opt/ovis
	expected_event2_prefix=$HOME/event/opt/ovis
	expected_ovislib_prefix=$prefix
	expected_sos_prefix=$prefix
	allconfig="--prefix=$prefix --disable-rdma --enable-ssl --disable-zaptest --disable-swig --with-libevent=$expected_event2_prefix --disable-ncsa-unified --disable-sos   --disable-dependency-tracking --disable-perfevent --with-ldflags-readline=-ltermcap --enable-authentication --disable-sysclassib"
	glibconfig="GLIB20_CFLAGS=-I/usr/lib64/glib-2.0/include GLIB20_LIBS=-L/lib64"

	if test -f $expected_event2_prefix/include/event2/event.h; then
		echo "Found $expected_event2_prefix/include/event2/event.h. Good."
	else
		echo "You forgot to install libevent2 rpms in $expected_event2_prefix or you need to edit $0"
		exit 1
	fi

	echo "reinitializing objdir"
	rm -rf LDMS_objdir
	mkdir LDMS_objdir
	cd LDMS_objdir
	 LDFLAGS="-L/home/baallan/opt/ovis/lib" GLIB20_CFLAGS=-I/usr/lib64/glib-2.0/include GLIB20_LIBS=-L/lib64 ../configure $allconfig && make  && make install
#	LDFLAGS="-L/home/baallan/opt/ovis/lib" ../configure $allconfig && make  && make install
else
	echo "this must be run from the top of ovis source tree"
	exit 1
fi
