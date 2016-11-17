#!/bin/sh -x
expected_ovislib_prefix=/opt/ovis

if test -f sos/packaging/ovis-sos.spec.in; then
	echo "reinitializing .build-sos"
	rm -rf .build-sos
	mkdir .build-sos
	cd .build-sos
	if test -f $expected_ovislib_prefix/include/ovis_ctrl/ctrl.h; then
		echo "Found $expected_ovislib_prefix/include/ovis_ctrl/ctrl.h. Good."
	else
		echo "You forgot to install ovis-lib rpms in $expected_ovislib_prefix or you need edit $0"
		exit 1
	fi
	../sos/configure --enable-swig --with-ovis-lib=$expected_ovislib_prefix && make centos
	cd ..
	find .build-sos -name '*.rpm' -exec echo Created {} \;
else
	echo "this must be run from the top of ovis source tree"
	exit 1
fi
