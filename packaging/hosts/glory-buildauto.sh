#!/bin/bash -x
echo building build system dependencies needed for rhel5/centos5/glory
tar jxf autoconf-2.63.tar.bz2
tar xjf m4-1.4.13.tar.bz2
tar xjf automake-1.11.1.tar.bz2
tar xzf readline-6.0.tar.gz
tar zxf libtool-2.2.6b.tar.gz
tar zxf pkg-config-0.23.tar.gz
tar zxf swig-1.3.40.tar.gz
tar zxf doxygen-1.6.1.src.tar.gz


mkdir bm4
(cd bm4; ../m4-1.4.13/configure --prefix=$HOME/autotoolsrh64 && make && make install )

export  PATH=$HOME/autotoolsrh64/bin:$PATH

mkdir blt
(cd blt; ../libtool-2.2.6b/configure --prefix=$HOME/autotoolsrh64 && make && make install )

export LD_LIBRARY_PATH=$HOME/autotoolsrh64/lib:$LD_LIBRARY_PATH

mkdir bac
( cd bac ; ../autoconf-2.63/configure --prefix=$HOME/autotoolsrh64 && make && make install )

mkdir bam
(cd bam && ../automake-1.11.1/configure --prefix=$HOME/autotoolsrh64 && make && make install)

mkdir bpc
 (cd bpc && ../pkg-config-0.23/configure --prefix=$HOME/autotoolsrh64 --with-installed-glib && make && make install )

# no vpath build for readline or swig.

(cd readline-6.0 ; CC=gcc configure --prefix=$HOME/autotoolsrh64 && make && make install )

( cd swig-1.3.40 ; CC=gcc ./configure --prefix=$HOME/autotoolsrh64 --without-alllang --with-tcl=/usr/bin/tclsh --with-python=/apps/x86_64/tools/python/Python-2.6.4/bin/python --with-perl && make && make install )

exit 0
echo where to get the sources
export http_proxy=wwwproxy.ran.sandia.gov
http://sourceforge.net/projects/swig/files/swig/
wget http://ftp.gnu.org/gnu/autoconf/autoconf-2.63.tar.bz2
wget http://ftp.gnu.org/gnu/automake/automake-1.11.1.tar.bz2
wget http://ftp.gnu.org/gnu/libtool/libtool-2.2.6b.tar.gz
wget http://ftp.gnu.org/gnu/m4/m4-1.4.13.tar.bz2
wget ftp://ftp.gnu.org/gnu/readline/readline-6.0.tar.gz
wget ftp://ftp.stack.nl/pub/users/dimitri/doxygen-1.6.1.src.tar.gz
wget http://pkgs.fedoraproject.org/repo/pkgs/pkgconfig/pkg-config-0.23.tar.gz/d922a88782b64441d06547632fd85744/pkg-config-0.23.tar.gz
