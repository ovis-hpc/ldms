lib_LTLIBRARIES =

AM_CFLAGS = -I$(srcdir)/../

if ENABLE_COLL
libcoll_la_SOURCES = rbt.c rbt.h idx.c idx.h idx_priv.h
libcollinclude_HEADERS = rbt.h idx.h
libcollincludedir = $(includedir)/coll
libcoll_conf = /etc/ld.so.conf.d/libcoll.conf
lib_LTLIBRARIES += libcoll.la
endif

if ENABLE_MMALLOC
libmmalloc_la_SOURCES = mmalloc.c mmalloc.h
libmmalloc_la_LIBADD = libcoll.la
libmmallocinclude_HEADERS =  mmalloc.h
libmmallocincludedir = $(includedir)/coll
libmmalloc_conf = /etc/ld.so.conf.d/libmmalloc.conf
lib_LTLIBRARIES += libmmalloc.la
endif

#install-exec-hook:
#	echo $(libdir) > $(libcoll_conf)
#	echo $(libdir) > $(libmmalloc_conf)
#	ldconfig
#
#uninstall-local:
#	rm -f $(libcoll_conf)
#	rm -f $(libmmalloc_conf)
#	ldconfig
