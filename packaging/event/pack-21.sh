#!/bin/sh
#echo cleaning rpms dir
#/bin/rm -f ../RPMS/*/libevent*.rpm
#echo cleaning srpm dir
#/bin/rm -f ../SRPMS/libevent*.src.rpm
script -c 'rpmbuild -ba libevent21ovis.spec' packlibevent21
