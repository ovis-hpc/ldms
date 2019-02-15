lib_LTLIBRARIES =

AM_CFLAGS = -I$(srcdir)/../ -I$(top_srcdir) -I..

if ENABLE_MMALLOC
libmmalloc_la_SOURCES = mmalloc.c mmalloc.h
libmmalloc_la_LIBADD = ../coll/libcoll.la
libmmallocinclude_HEADERS =  mmalloc.h
libmmallocincludedir = $(includedir)/mmalloc
libmmalloc_conf = /etc/ld.so.conf.d/libmmalloc.conf
lib_LTLIBRARIES += libmmalloc.la

mmrtest_SOURCES = mmalloc.c mmalloc.h
mmrtest_LDADD = ../coll/libcoll.la
mmrtest_CFLAGS = $(AM_CFLAGS) -DMMR_TEST
bin_PROGRAMS = mmrtest

endif

#install-exec-hook:
#	echo $(libdir) > $(libmmalloc_conf)
#	ldconfig
#
#uninstall-local:
#	rm -f $(libmmalloc_conf)
#	ldconfig
