lib_LTLIBRARIES = \
	libcoll.la \
	libmmalloc.la

AM_CFLAGS = -Wall

libcoll_la_CFLAGS = -g
libcoll_la_SOURCES = rbt.c rbt.h idx.c idx.h
libcollinclude_HEADERS = rbt.h idx.h
libcollincludedir = $(includedir)/coll
libcoll_conf = /etc/ld.so.conf.d/libcoll.conf

libmmalloc_la_CFLAGS = -g
libmmalloc_la_SOURCES = mmalloc.c mmalloc.h
libmmalloc_la_LIBADD = libcoll.la
libmmallocinclude_HEADERS =  mmalloc.h
libmmallocincludedir = $(includedir)/coll
libmmalloc_conf = /etc/ld.so.conf.d/libmmalloc.conf

install-exec-hook:
	echo $(libdir) > $(libcoll_conf)
	echo $(libdir) > $(libmmalloc_conf)
	ldconfig

uninstall-local:
	rm -f $(libcoll_conf)
	rm -f $(libmmalloc_conf)
	ldconfig
