SUBDIRS =
lib_LTLIBRARIES =
pkglib_LTLIBRARIES =
CORE = ../../core

CFLAGS := $(filter-out -Werror, ${CFLAGS})

AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) -I../.. @OVIS_LIB_INCDIR_FLAG@ -I$(srcdir)/../../ldmsd
AM_LDFLAGS = @OVIS_LIB_LIBDIR_FLAG@
COMMON_LIBADD = $(CORE)/libldms.la -lovis_util

libkokkos_store_la_SOURCES = kokkos_store.c
libkokkos_store_la_CFLAGS = @SOS_INCDIR_FLAG@ $(AM_CFLAGS) -g -O0
libkokkos_store_la_LIBADD = -lc $(COMMON_LIBADD) \
	@SOS_LIB64DIR_FLAG@ @SOS_LIBDIR_FLAG@ -lsos -lcrypto $(top_srcdir)/src/json/libjson_util.la

pkglib_LTLIBRARIES += libkokkos_store.la

