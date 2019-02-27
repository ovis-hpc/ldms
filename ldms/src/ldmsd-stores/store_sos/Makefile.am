include ../common.am

pkglib_LTLIBRARIES = libstore_sos.la

libstore_sos_la_SOURCES = store_sos.c
libstore_sos_la_CFLAGS = $(STORE_CFLAGS) @SOS_INCDIR_FLAG@
libstore_sos_la_LIBADD = $(STORE_LIBADD)
libstore_sos_la_LDFLAGS = $(STORE_LDFLAGS) @SOS_LIBDIR_FLAG@ \
			  @SOS_LIB64DIR_FLAG@ -lsos
