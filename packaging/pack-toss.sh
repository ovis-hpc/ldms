#!/bin/sh +x
cleanup=1
if test -f packaging/pack-toss.sh; then
	echo "building all for /usr with private libevent2"
	echo -n "is this ok? y/[n]"
	read isok
	if test "x$isok" != "xy"; then
		echo "you did not say y. bye."
		exit 1
	fi
	pd=.build-one/toss/RPMS/x86_64
	rm -rf .build-one
	mkdir -p $pd
	cd .build-one
	if ! test -f ../libevent-2.0.21-stable.tar.gz; then
		cd ..
		echo "need a copy of libevent-2.0.21-stable.tar.gz in before continuing"
		echo `pwd`
		exit 1
	fi
	cp ../libevent-2.0.21-stable.tar.gz .
	LIBEVENT_BUILD=1 CC=gcc46 CXX=g++ ../configure \
		--enable-ssl \
		--enable-rdma \
		--enable-ncsa-unified \
		--disable-zap --disable-zaptest \
		--with-libevent=`pwd`/toss/BUILD/ldms-all-2.2.0/libevent-2.0.21-stable/lib/ovis-ldms \
		--disable-rpath \
		--enable-authentication \
		--disable-readline \
		&& make toss
	cd ..
	find .build-one -name '*.rpm' -exec echo Created {} \;
	mkdir -p centos-rpms centos-srpms && \
	cp $pd/../../SRPMS/* centos-srpms && \
	cp $pd/*rpm centos-rpms && echo "DUMPED RPMS in centos-srpms,centos-rpms"

	if test "x$cleanup" = "x1"; then
		/bin/rm -rf .build-one
		echo build tree removed.
	fi
else
	echo "this must be run from the top of ovis source tree"
	exit 1
fi
