#!/bin/bash
#
# This works similar to ./all-autogen*.sh script, but it will generate RPMs
# instead of installing the applications. The resulting RPMs can be found in
# <project-dir>/$BUILD_DIR/rpm7/RPMS
#
# The RPM installation destination is the $PREFIX

BUILD_DIR="build-$HOSTNAME"
PREFIX=/opt/ovis

# add --enable-FEATURE here
ENABLE="--enable-swig \
	--enable-doc \
	--enable-doc-html \
	--enable-doc-man \
	--enable-test \
	--enable-zaptest \
	--enable-ldms-python \
	--enable-rdma \
	--enable-sysclassib \
	--enable-sos --enable-debug"

# add --disable-FEATURE here
DISABLE="--disable-ocm --disable-rpath --disable-jobid"

# libevent2 prefix
#LIBEVENT_PREFIX=/usr/local

WITH_OVIS_LIB="--with-ovis-lib=$PWD/lib/$BUILD_DIR/rpm7/BUILDROOT$PREFIX"
WITH_SOS="--with-sos=$PWD/sos/$BUILD_DIR/rpm7/BUILDROOT$PREFIX"

WITH="$WITH_OVIS_LIB $WITH_SOS"
if [ -n "$LIBEVENT_PREFIX" ]; then
	WITH="$WITH --with-libevent=$LIBEVENT_PREFIX"
fi

function __flags_for {
	echo -n " $DISABLE"
	echo -n " $ENABLE"
	if [ -n "$LIBEVENT_PREFIX" ]; then
		echo -n " --with-libevent=$LIBEVENT_PREFIX"
	fi
	case $1 in
	lib)
		# do nothing
	;;
	sos)
		# do nothing
	;;
	ldms)
		echo -n " --enable-etc"
		echo -n " $WITH_SOS $WITH_OVIS_LIB"
	;;
	baler)
		echo -n " $WITH_SOS $WITH_OVIS_LIB"
	;;
	*)
		echo "ERROR: __with_for(): Unrecognize argument '$1'"
		exit -1
	;;
	esac
}

#CFLAGS='-g -O0 -DDEBUG -Wl,-z,defs -Werror'
CFLAGS='-g -O3'

# Exit immediately if a command failed
set -e

#LIST="lib sos baler"
RPMS_DEST=$PWD/RPM7
rm -rf $RPMS_DEST
mkdir -p $RPMS_DEST

LIST="lib sos ldms baler"
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
	../configure --prefix=$PREFIX $(__flags_for $X) CFLAGS="$CFLAGS"
	make rpm7
	mkdir -p rpm7/BUILDROOT
	pushd rpm7/BUILDROOT
	for Y in ../RPMS/*/*.rpm; do
		echo "-- Extracting $Y --"
		rpm2cpio $Y | cpio -idmv
		mv $Y $RPMS_DEST
	done
	popd # rpm7/BUILDROOT
	popd # $BUILD_DIR
	popd # $X
	set +x; # disable command echo so that it won't print the "for ..." command
	echo "----- DONE -----"
done

echo "Please see the RPMs in $RPMS_DEST"
