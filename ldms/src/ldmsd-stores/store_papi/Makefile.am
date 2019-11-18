include ../common.am

pkglib_LTLIBRARIES = libstore_papi.la

libstore_papi_la_SOURCES = store_papi.c
libstore_papi_la_CFLAGS  = $(STORE_CFLAGS) @SOS_INCDIR_FLAG@
libstore_papi_la_LIBADD  = $(STORE_LIBADD)
libstore_papi_la_LDFLAGS = $(STORE_LDFLAGS) @SOS_LIBDIR_FLAG@ \
                           @SOS_LIB64DIR_FLAG@ -lsos
