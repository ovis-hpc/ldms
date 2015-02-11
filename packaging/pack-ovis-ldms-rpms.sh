#!/bin/sh -x
expected_ovislib_prefix=/opt/ovis
expected_event2_prefix=/opt/ovis ; # should look this up from an ovis-lib package file
expected_sos_prefix=/opt/ovis ; # should look this up from an ovis-sos package file


if test -f ldms/packaging/ovis-ldms.spec.in; then
	echo "reinitializing .build-ldms"
	rm -rf .build-ldms
	mkdir .build-ldms
	cd .build-ldms
	if test -f $expected_ovislib_prefix/include/ovis_ctrl/ctrl.h; then
		echo "Found $expected_ovislib_prefix/include/ovis_ctrl/ctrl.h. Good."
	else
		echo "You forgot to install ovis-lib rpms in $expected_ovislib_prefix or you need to edit $0"
		exit 1
	fi
        if test -f $expected_event2_prefix/include/event2/event.h; then
                echo "Found $expected_event2_prefix/include/event2/event.h. Good."
        else
                echo "You forgot to install libevent2 rpms in $expected_event2_prefix or you need to edit $0"
                exit 1
        fi
        if test -f $expected_sos_prefix/include/sos/sos.h; then
                echo "Found $expected_event2_prefix/include/sos/sos.h. Good."
        else
                echo "You forgot to install ovis-sos rpms in $expected_event2_prefix or you need to edit $0"
                exit 1
        fi

	../ldms/configure --with-libevent=$expected_event2_prefix --enable-rdma --enable-ncsa-unified --enable-sos --with-ovis-lib=$expected_ovislib_prefix --with-sos=$expected_sos_prefix && make centos
	cd ..
	find .build-ldms -name '*.rpm' -exec echo Created {} \;
else
	echo "this must be run from the top of ovis source tree"
	exit 1
fi
