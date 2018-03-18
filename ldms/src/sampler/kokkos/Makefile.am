SUBDIRS =
lib_LTLIBRARIES =
pkglib_LTLIBRARIES =
CORE = ../../core

CFLAGS := $(filter-out -Werror, ${CFLAGS})

AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir)/src/ldmsd -I$(top_srcdir) @OVIS_LIB_INCDIR_FLAG@
AM_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@
COMMON_LIBADD = $(CORE)/libldms.la @LDFLAGS_GETTIME@ -lovis_util

BUILT_SOURCES = json_parser.h
AM_YFLAGS = -d

libkokkos_store_la_SOURCES = json_lexer.l json_parser.y kokkos_store.c \
	json.h kvd_svc.c kvd_svc.h kvd_log.h kvd_log.c
libkokkos_store_la_CFLAGS = @SOS_INCDIR_FLAG@ $(AM_CFLAGS) -g -O0
libkokkos_store_la_LIBADD = -ly -lfl -lc $(COMMON_LIBADD) \
	@SOS_LIB64DIR_FLAG@ @SOS_LIBDIR_FLAG@ -lsos \
	-lssl \
	-levent -levent_openssl -levent_pthreads
pkglib_LTLIBRARIES += libkokkos_store.la

