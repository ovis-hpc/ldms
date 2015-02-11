
Quick start guide:

-----------------------------------------------------------
0) Requirements:

libevent2 (not libevent 1.4) must be installed somewhere (it need not be the 
system default). The most recently tested libevent we test with is from 
libevent.org:
	libevent-2.0.21-stable.tar.gz
It is easy to build and install; see README.libevent2.
Ubuntu 12 and other modern distributions supply libevent2 packages.

For rdma support, ibmad and umad devel packages must be installed.
For authentication support, an openssl devel package must be installed.


-----------------------------------------------------------
1) Building:

Options from all the packages can be combined and given as the options to 
the top level configure in this directory. The subpackages will ignore
unknown options (with warning about them).

In the 2.2.0 release, authentication between daemons and clients
is not provided by default. Build with --enable-authentication if 
security is required.

##### rpms for redhat

From the top of the released source tree

	./packaging/pack-toss.sh

will generate rpm files in centos-rpms/ if all goes well.

These may be installed with rpm -Uvh ldms-all*.rpm, unless
you are in a cluster environment. The rpms are intended to
be relocatable.

In particular, the build process expects gcc46 to be available
(as it is in the TOSS environment) and the stock gcc 4.4 is not
good enough.

##### targeting /opt/ovis with custom libevent installed in /opt/ovis

libevent2_prefix=/opt/ovis
if test -x $libevent2_prefix/lib/libevent_openssl.so; then
  ./configure --prefix=/opt/ovis --with-libevent=$libevent2_prefix [options]
  make
  sudo make install
fi

./configure --help=recursive 
will show the options for all the subpackages (lib, ldms).
Several practical configurations are illustrated in
packaging/make-all-$platform.sh


##### targeting a user directory build
packaging/make-all-top.sh is a good starting place for new builders.




-----------------------------------------------------------
2) Running:
Put $prefix/bin in your path and then run

	ldms_local_clocktest.sh

Which will run 3 user-space daemons and several clients on localhost to verify 
that the tcp socket transport works on your host. It may complain about
not finding a secret word file. If so, "man LDMS_Authentication" to
find out how to fix it.


-----------------------------------------------------------
3) Support

https://ovis.ca.sandia.gov/mediawiki/index.php/Main_Page
or
ovis-help@sandia.gov

