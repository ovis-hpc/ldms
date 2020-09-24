#!/bin/bash -x
if test -f ldms/src/sampler/meminfo.c; then
	echo "this is not quite ready. some configs still depend on prior installs, which is not desirable."

	prefix=/opt/ovis
	expected_event2_prefix=$prefix
	buildprefix="`pwd`/.build-all/centos/BUILDROOT"
	expected_ovislib_prefix=$prefix
	expected_sos_prefix=$prefix
	allconfig="--prefix=$prefix --enable-rdma --enable-ssl --enable-zaptest --with-ovis-lib=$expected_ovislib_prefix --with-libevent=$expected_event2_prefix --enable-rdma --enable-sos --with-sos=$expected_sos_prefix"

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
	../configure $allconfig
else
	echo "this must be run from the top of ovis source tree"
	exit 1
fi
