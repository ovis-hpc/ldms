SUBDIRS =
lib_LTLIBRARIES =
pkglib_LTLIBRARIES =
CORE = ../../core

AM_LDFLAGS = @OVIS_LIB_ABS@
#COMMON_LIBADD = -lldms @LDFLAGS_GETTIME@ -lovis_util -lcoll
AM_CPPFLAGS = @OVIS_INCLUDE_ABS@

CFLAGS := $(filter-out -Werror, ${CFLAGS})

COMMON_LIBADD = -lldms -lovis_util

libkokkos_store_la_SOURCES = kokkos_store.c
libkokkos_store_la_CFLAGS = @SOS_INCDIR_FLAG@ $(AM_CFLAGS) -g -O0
libkokkos_store_la_LIBADD = $(COMMON_LIBADD) \
	@SOS_LIB64DIR_FLAG@ @SOS_LIBDIR_FLAG@ -lsos -lcrypto -ljson_util -lc

pkglib_LTLIBRARIES += libkokkos_store.la

EXTRA_DIST=kokkos.h kokkos_sampler.c
