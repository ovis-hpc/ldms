#!/bin/bash 
echo "$0 `date`" >> .last-make
echo BUILDING FOR Cray SLES pecos compute node

#export LD_LIBRARY_PATH=$HOME/gcc/gcc491/lib64:$HOME/opt/ovis/lib:$LD_LIBRARY_PATH
#export PATH=$HOME/gcc/gcc491/bin:$PATH
#export CFLAGS="-fsanitize=address -Wall -g -O0"

# local path of scratch ldms files
build_subdir=LDMS_objdir
instdir=LDMS_install

if test "$HOSTNAME" != "mutrino"; then
	echo mutrino login node required.
	exit 1
fi

if test -f packaging/ovis-base.spec.in; then
	prefix=`pwd`/LDMS_install
	expected_event2_prefix=/home/baallan/mutrino/libevent2

	allconfig="--prefix=$prefix --enable-rdma --enable-ssl --enable-zaptest --enable-swig --with-ovis-lib=$expected_ovislib_prefix  --enable-gpcdlocal --enable-aries-mmr --enable-sos --with-sos=$expected_sos_prefix --with-ovis-prefix=$expected_ovislib_prefix --disable-dependency-tracking "


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
	prefix=$srctop/$instdir
	echo "reinitializing build subdirectory $build_subdir" 
	rm -rf $build_subdir
	mkdir $build_subdir
	cd $build_subdir
	expected_ovislib_prefix=$prefix
	expected_sos_prefix=/badsos
# auth + -a none
#	allconfig="--prefix=$prefix --enable-ssl --with-libevent=$expected_event2_prefix --disable-sos --disable-perfevent --enable-zap --disable-swig --enable-ovis_auth --enable-libgenders --with-libgenders=$HOME/ovis/init-2015 --enable-a-none --with-pkglibdir=ovis-ldms LDFLAGS=-fsanitize=address"
# no auth
#	allconfig="--prefix=$prefix --enable-ssl --with-libevent=$expected_event2_prefix --disable-sos --disable-perfevent --enable-zap --disable-swig --disable-ovis_auth --enable-libgenders --with-libgenders=$HOME/ovis/init-2015 --with-pkglibdir=ovis-ldms"
# autH
thinlist="
--disable-sos
--disable-perfevent
--enable-zap
--disable-swig
--enable-ovis_auth
--disable-rpath
--enable-sock
--disable-papi
--disable-perf
--disable-sensors
--disable-flatfile
--disable-meminfo
--disable-array_example
--disable-procinterrupts
--disable-procnetdev
--disable-procnfs
--disable-procsensors
--disable-procstat
--disable-procstatutil
--disable-procstatutil2
--disable-vmstat
--disable-procdiskstats
--disable-atasmart
--disable-hadoop
--disable-generic_sampler
--disable-switchx
--disable-csv
--disable-store
--disable-ldms-python
--disable-readline
--enable-cray_system_sampler
--enable-aries-gpcdr
--enable-gpcdlocal
--enable-aries-mmr
--enable-ugni
--enable-lustre
--disable-mmap
--disable-baler
"
fatlist="
--enable-csv
--disable-sysclassib
--enable-sos
--disable-perfevent
--enable-zap
--disable-swig
--enable-ovis_auth
--disable-rpath
--enable-sock
--disable-papi
--enable-perf
--enable-sensors
--enable-flatfile
--enable-meminfo
--disable-array_example
--enable-procinterrupts
--enable-procnetdev
--enable-procnfs
--enable-procsensors
--disable-procstat
--disable-procstatutil
--disable-procstatutil2
--enable-vmstat
--disable-procdiskstats
--disable-atasmart
--disable-hadoop
--disable-generic_sampler
--disable-switchx
--disable-readline
--enable-cray_system_sampler
--enable-aries-gpcdr
--enable-gpcdlocal
--enable-aries-mmr
--enable-ugni
--enable-lustre
--disable-mmap
--disable-ovis_auth
--disable-yaml
"
#	allconfig="--disable-static --prefix=$prefix $thinlist --enable-ssl --with-libevent=$expected_event2_prefix --with-pkglibdir=ovis-ldms  --with-rca=/opt/cray/rca/default/ --with-krca=/opt/cray/krca/default --with-cray-hss-devel=/opt/cray-hss-devel/default"
	allconfig="--disable-static --prefix=$prefix $fatlist --enable-ssl --with-libevent=$expected_event2_prefix --with-pkglibdir=ovis-ldms  --with-rca=/opt/cray/rca/default/ --with-krca=/opt/cray/krca/default --with-cray-hss-devel=/opt/cray-hss-devel/default"
	../configure $allconfig && \
	make -j 16 && \
	make install && \
	../packaging/nola.sh $prefix
else
	echo "this must be run from the top of ovis source tree"
	exit 1
fi
