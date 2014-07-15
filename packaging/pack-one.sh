#!/bin/sh +x
noinst=echo
# unset noinst ;# to make packages install
if test -f packaging/pack-one.sh; then
	echo "building all for /opt/ovis"
	echo -n "is this ok? y/[n]"
	read isok
	if test "x$isok" != "xy"; then
		echo "you did not say y. bye."
		exit 1
	fi
	expected_event2_prefix=/opt/ovis
	pd=.build-one/toss/RPMS/x86_64
	rm -rf .build-one
	mkdir -p $pd
	cd .build-one
	CC=gcc46 CXX=g++ ../configure --with-libevent=$expected_event2_prefix --enable-ssl --enable-rdma --enable-ncsa-unified --disable-rpath --disable-zap --disable-zaptest --enable-sos && make toss
	cd ..
	find .build-ldms -name '*.rpm' -exec echo Created {} \;
	mkdir -p centos-rpms centos-srpms && \
	cp $pd/../../SRPMS/* centos-srpms && \
	cp $pd/*rpm centos-rpms && echo "DUMPED RPMS in centos-srpms,centos-rpms" && \
	/bin/rm -rf .build-one
else
	echo "this must be run from the top of ovis source tree"
	exit 1
fi
