#!/bin/bash -x
echo "$0 `date`" >> .last-make
eventname=libevent-2.0.21-stable
export LOCALEVENT=0 ; # else expect /usr to be a good libevent2
# we build libevent once, then reinstall it as first built if
# it goes missing from the install tree.

export CC=gcc; # on chama, gcc46 is in default path. 
# If using module gnu/4.7.x, change CC above to CC=gcc. gcc 4.4 is not good enough.

export CXX=g++ ; # needed for configure. not used anywhere in build yet.

export CFLAGS="-O2 -g -pipe -Wall -Wp,-D_FORTIFY_SOURCE=2 -fexceptions -fstack-protector --param=ssp-buffer-size=4 -m64 -march=native" ; # cflags common to us, libevent2

# local path of scratch ldms files
build_subdir=LDMS_objdir

# full path of where we want things installed
prefix=`pwd`/LDMS_install

if test -f ldms/src/sampler/meminfo/meminfo.c; then
	mkdir -p $prefix
	# Are we at the top?
	if test -f configure; then
		echo "Found configure. Good."
	else
		echo "You forgot to autogen.sh at the top or you need to edit $0 or you need to use a released tarred version."
		exit 1
	fi
	if test -f /usr/lib64/libevent-2.0.so.5; then
		# ubuntu/debian recent
		expected_event2_prefix=/usr
	else
		expected_event2_prefix=/usr/lib64/ovis-libevent2
	fi
	# clean out old build headers if reinstalling. prevents build confusion.
	oldinc="coll ldms mmalloc ovis_ctrl ovis-test ovis_util sos zap"
	for i in $oldinc; do
		if test -d $prefix/include/$i; then
			echo "rm $i"
			/bin/rm -rf $prefix/include/$i
		fi
	done

	if test -f $expected_event2_prefix/include/event2/event.h; then
		echo "Found $expected_event2_prefix/include/event2/event.h. Good."
	else
		if test "$LOCALEVENT" = "1"; then
			if test -f $expected_event2_prefix/include/event2/event.h; then
				echo "Libevent locally built already. Good."
			else
				if ! test -f $eventname.tar.gz; then
					if test -f ../$eventname.tar.gz; then
						cp ../$eventname.tar.gz .
					else
						echo "You need libevent source dropped in the LDMS top directory."
						echo "Do: wget https://github.com/downloads/libevent/libevent/$eventname.tar.gz"
						echo "or equivalent then rerun $0"
						exit 1
					fi
				fi
				mkdir -p .build-event
				cp $eventname.tar.gz .build-event
				(cd .build-event && ../packaging/TLCC2.libevent2 $expected_event2_prefix )
				if test -f $expected_event2_prefix/include/event2/event.h; then
					echo "Built and installed $expected_event2_prefix/include/event2/event.h. Good."
				else
					echo "Local libevent build failed"
					exit 1
				fi
			fi
		else
			echo "You forgot to install libevent2 rpms in $expected_event2_prefix or you need to edit $0"
			exit 1
		fi
	fi
	
	srctop=`pwd`
	echo "reinitializing build subdirectory $build_subdir" 
	rm -rf $build_subdir
	mkdir $build_subdir
	cd $build_subdir
	expected_ovislib_prefix=$prefix
	expected_sos_prefix=/badsos
	allconfig="--prefix=$prefix --enable-rdma --enable-ssl --with-libevent=$expected_event2_prefix --disable-sos --disable-perfevent --disable-rpath --enable-authentication --enable-sysclassib --with-pkglibdir=ovis-ldms --enable-libgenders --enable-jobid --enable-llnl-edac --enable-opa2 --enable-genderssystemd --enable-atasmart --enable-fptrans --enable-slurmtest --enable-filesingle --enable-dstat --enable-third-plugins=my_plugin,dummytest"
	../configure $allconfig && \
	make && \
	make install && \
	../util/nola.sh $prefix > nola.pkg.log
else
	echo "this must be run from the top of ovis source tree"
	exit 1
fi
