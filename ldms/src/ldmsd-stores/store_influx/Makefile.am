include ../common.am

pkglib_LTLIBRARIES = libstore_influx.la

libstore_influx_la_SOURCES = store_influx.c
libstore_influx_la_CFLAGS  = $(STORE_CFLAGS)
libstore_influx_la_LIBADD  = $(STORE_LIBADD) -lcoll -lovis_util -lcurl
libstore_influx_la_LDFLAGS = $(STORE_LDFLAGS)
