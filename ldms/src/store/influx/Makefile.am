# SUBDIRS = flxs
lib_LTLIBRARIES =
pkglib_LTLIBRARIES =

AM_LDFLAGS = @OVIS_LIB_ABS@
#COMMON_LIBADD = -lldms @LDFLAGS_GETTIME@ -lovis_util -lcoll
AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
STORE_LIBADD = $(top_builddir)/ldms/src/core/libldms.la \
	       $(top_builddir)/lib/src/ovis_util/libovis_util.la \
	       $(top_builddir)/lib/src/coll/libcoll.la \
	       -lcurl

if ENABLE_INFLUX
libstore_influx_la_SOURCES = store_influx.c
libstore_influx_la_LIBADD = $(STORE_LIBADD)
pkglib_LTLIBRARIES += libstore_influx.la
endif

