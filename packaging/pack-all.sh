#!/bin/sh
noinst=echo
# unset noinst ;# to make packages install
if test -f packaging/pack-all.sh; then
	echo "building all and installing in /opt/ovis"
	echo -n "is this ok? y/[n]"
	read isok
	if test "x$isok" != "xy"; then
		echo "you did not say y. bye."
		exit 1
	fi
	pl=.build-lib/centos/RPMS/x86_64
	po=.build-sos/centos/RPMS/x86_64
	pd=.build-ldms/centos/RPMS/x86_64
	packaging/pack-ovis-lib-rpms.sh && \
	$noinst sudo su -c "rpm -Uvh $pl/ovis-lib-1* $pl/ovis-lib-devel-* $pl/ovis-lib-doc*" \
	&& packaging/pack-ovis-sos-rpms.sh && \
	$noinst sudo su -c "rpm -Uvh $po/ovis-sos-1*  $po/ovis-sos-dev* $po/ovis-sos-doc*" \
	&& packaging/pack-ovis-ldms-rpms.sh && echo SUCCESSFUL PACKING && \
	mkdir centos-rpms centos-srpms && \
	$noinst sudo su -c "rpm -Uvh $pd/ovis-ldms-1*  $po/ovis-ldms-dev* $po/ovis-sos-doc*" \
	cp $pl/../../SRPMS/* $po/../../SRPMS/* $pd/../../SRPMS/* centos-srpms && \
	cp $pl/*rpm $po/*rpm $pd/*rpm centos-rpms && echo "DUMPED RPMS in centos-srpms,centos-rpms" && \
	rm -rf .build-lib .build-sos .build-ldms
else
	echo "this must be run from the top of ovis source tree"
	exit 1
fi
