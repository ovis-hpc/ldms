include ../common.am

EXTRA_DIST = ldmsd_store_app.md

pkglib_LTLIBRARIES = libstore_app.la

libstore_app_la_SOURCES = store_app.c
libstore_app_la_CFLAGS  = $(STORE_CFLAGS) @SOS_INCDIR_FLAG@
libstore_app_la_LIBADD  = $(STORE_LIBADD)
libstore_app_la_LDFLAGS = $(STORE_LDFLAGS) @SOS_LIBDIR_FLAG@ \
                           @SOS_LIB64DIR_FLAG@ -lsos
