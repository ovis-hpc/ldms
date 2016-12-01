#!/bin/sh -x
expected_event2_prefix=/opt/ovis

if test -f lib/packaging/ovis-lib-toss.spec.in; then
	echo "reinitializing .build-lib"
	rm -rf .build-lib
	mkdir .build-lib
	cd .build-lib
	if test -f $expected_event2_prefix/include/event2/event.h; then
		echo "Found $expected_event2_prefix/include/event2/event.h. Good."
	else
		echo "You forgot to install libevent2 rpms in $expected_event2_prefix or you need to edit $0"
		exit 1
	fi
	../lib/configure --with-libevent=$expected_event2_prefix --enable-rdma --enable-ssl --enable-zaptest && make centos
	cd ..
	find .build-lib -name '*.rpm' -exec echo Created {} \;
else
	echo "this must be run from the top of ovis source tree"
	exit 1
fi
