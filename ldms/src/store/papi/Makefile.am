# SUBDIRS = flxs
lib_LTLIBRARIES =
pkglib_LTLIBRARIES =

AM_LDFLAGS = @OVIS_LIB_ABS@
#COMMON_LIBADD = -lldms @LDFLAGS_GETTIME@ -lovis_util -lcoll
AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
STORE_LIBADD = -lldms -lcoll -lovis_util

if ENABLE_SOS
libstore_papi_la_SOURCES = store_papi.c
libstore_papi_la_CFLAGS = @SOS_INCDIR_FLAG@
libstore_papi_la_LIBADD = @SOS_LIBDIR_FLAG@ @SOS_LIB64DIR_FLAG@ $(STORE_LIBADD) -lsos
pkglib_LTLIBRARIES += libstore_papi.la
endif

