lib_LTLIBRARIES =

AM_CFLAGS = -I$(srcdir)/../

if ENABLE_COLL
libcoll_la_SOURCES = rbt.c rbt.h idx.c idx.h idx_priv.h str_map.c str_map.h
libcollinclude_HEADERS = rbt.h idx.h str_map.h
libcollincludedir = $(includedir)/coll
libcoll_conf = /etc/ld.so.conf.d/libcoll.conf
lib_LTLIBRARIES += libcoll.la
endif

#install-exec-hook:
#	echo $(libdir) > $(libcoll_conf)
#	ldconfig
#
#uninstall-local:
#	rm -f $(libcoll_conf)
#	ldconfig
