#!/bin/bash
#
# SYNOPSIS: Remove existing build directories, do the automake routine, rebuild,
#           and install everything.
#
# REMARK: This script doesn't do uninstall. If you wish to uninstall (e.g. make
# uninstall), please go into each build directories ( */build-$HOSTNAME ) and
# call make uninstall there, or just simply do the following
#     for D in */build-$HOSTNAME; do pushd $D; make uninstall; popd; done;
#

BUILD_DIR="build-$HOSTNAME"
PREFIX=/opt/ovis

# add --enable-FEATURE here
ENABLE="--enable-swig --enable-ocmd --enable-parsers \
	--enable-sos --enable-ocm --enable-me --enable-debug\
	--enable-ocm-test --enable-etc"

# add --disable-FEATURE here
DISABLE="--disable-rdma --disable-sysclassib"

# libevent2 prefix
#LIBEVENT_PREFIX=/usr/local

WITH="--with-ovis-lib=$PREFIX --with-sos=$PREFIX --with-ocm=$PREFIX"
if [ -n "$LIBEVENT_PREFIX" ]; then
	WITH="$WITH --with-libevent=$LIBEVENT_PREFIX"
fi

CFLAGS='-g -O0 -DDEBUG -Wl,-z,defs'

# Exit immediately if a command failed
set -e

LIST="lib sos ocm ldms baler me komondor helper-scripts"
for X in $LIST; do
	echo "----------------------------------"
	echo "$X"
	echo "----------------------------------"
	set -x; # enable command echo
	pushd $X
	./autogen.sh
	mkdir -p $BUILD_DIR
	pushd $BUILD_DIR
	rm -rf * # Making sure that the build is clean
	../configure --prefix=$PREFIX $ENABLE $DISABLE $WITH CFLAGS="$CFLAGS"
	make
	chmod o+w -R .
	sudo make install
	sudo ldconfig
	popd
	popd
	set +x; # disable command echo so that it won't print the "for ..." command
	echo "----- DONE -----"
done
