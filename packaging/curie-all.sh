#!/bin/bash
#
# SYNOPSIS: Remove existing build directories, rebuild, and install everything.
#
# REMARK: doesn't run autogen.sh. Expects a cray sles11 environment.
# needs root for lots of things to work in the cray sampler, probably.
#
BUILD_DIR="build-$HOSTNAME"
PREFIX=$HOME/Build/ovis_v2b
mkdir -p $PREFIX

# add --enable-FEATURE here
ENABLE="--enable-ugni  \
    --enable-ldms-python \
    --enable-kgnilnd \
    --enable-cray-nvidia \
    --enable-lustre \
    --enable-cray_system_sampler \
    --enable-gemini-gpcdr \
    --enable-aries-gpcdr "


# add --disable-FEATURE here
DISABLE="--disable-rpath \
	 --disable-readline \
	 --disable-mmap "


# libevent2 prefix
LIBEVENT_PREFIX=$HOME/libevent2
export LD_LIBRARY_PATH=$LIBEVENT_PREFIX/lib:$PREFIX/lib:$LD_LIBRARY_PATH
if ! test -f $LIBEVENT_PREFIX/lib/libevent_pthreads.so; then
	echo need $HOME/libevent2 to have an installation of libevent
	exit 1
fi


WITH="--with-rca=/opt/cray/rca/default/ --with-krca=/opt/cray/krca/default --with-cray-hss-devel=/opt/cray-hss-devel/default/ --with-cray-nvidia-inc=/home/gentile/Source/gdk_linux_amd64_release/nvml --with-pkglibdir=ovis-ldms"

if [ -n "$LIBEVENT_PREFIX" ]; then
    WITH="$WITH --with-libevent=$LIBEVENT_PREFIX"
fi

# Use the flags below instead if you want to resolve symbols at compile time
CFLAGS='-g -O0 -DDEBUG -Wl,-z,defs -I/opt/cray/rca/default/include'

# Exit immediately if a command failed
set -e

mkdir -p $BUILD_DIR
cd $BUILD_DIR
rm -rf *
../configure --prefix=$PREFIX $ENABLE $DISABLE $WITH CFLAGS="$CFLAGS" "LDFLAGS=$LDFLAGS" CPPFLAGS=$CPPFLAGS && make && make install

exit 0
