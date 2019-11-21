# SUBDIRS = flxs
lib_LTLIBRARIES =
pkglib_LTLIBRARIES =

AM_LDFLAGS = @OVIS_LIB_ABS@
#COMMON_LIBADD = -lldms @LDFLAGS_GETTIME@ -lovis_util -lcoll
AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
STORE_LIBADD = -lldms -lcoll -lcurl -lovis_util

if ENABLE_INFLUX
libstore_influx_la_SOURCES = store_influx.c
libstore_influx_la_LIBADD = $(STORE_LIBADD) -lcurl
pkglib_LTLIBRARIES += libstore_influx.la
endif

