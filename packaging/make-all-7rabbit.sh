#!/bin/bash 
echo "$0 `date`" >> .last-make
echo BUILDING FOR 
#export CC=gcc49
#export CXX=g++49

#export LD_LIBRARY_PATH=$HOME/gcc/gcc491/lib64:$HOME/opt/ovis/lib:$LD_LIBRARY_PATH
#export PATH=$HOME/gcc/gcc491/bin:$PATH
#export CFLAGS="-fsanitize=address -Wall -g -O0"

# local path of scratch ldms files
build_subdir=LDMS_objdir

if test -f ldms/src/sampler/meminfo.c; then
	prefix=$HOME/opt/ovis
	expected_event2_prefix=/usr

	if test -f $expected_event2_prefix/include/event2/event.h; then
		echo "Found $expected_event2_prefix/include/event2/event.h. Good."
	else
		echo "You forgot to install libevent -dev package or you need to edit $0"
		exit 1
	fi
	if test -f ldms/configure; then
		echo "Found ldms/configure. Good."
	else
		echo "You forgot to autogen.sh at the top or you need to edit $0 or you need to use
 a released tarred version."
		exit 1
	fi

	srctop=`pwd`
	prefix=$srctop/LDMS_install
	echo "reinitializing build subdirectory $build_subdir" 
	rm -rf $build_subdir
	mkdir $build_subdir
	cd $build_subdir
	expected_ovislib_prefix=$prefix
	expected_sos_prefix=/badsos

	allconfig="--prefix=$prefix --enable-rdma --enable-ssl --enable-sos --disable-perfevent --enable-zap --enable-ovis_auth --with-pkglibdir=ovis-ldms --enable-rabbitv3 --disable-rpath"
	../configure $allconfig && \
	make && \
	make install && \
	../util/nola.sh $prefix
else
	echo "this must be run from the top of ovis source tree"
	exit 1
fi
