SUBDIRS =
lib_LTLIBRARIES =
pkglib_LTLIBRARIES =
dist_man7_MANS=
CORE = ../../core

CFLAGS := $(filter-out -Werror, ${CFLAGS})

AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) -I../.. @OVIS_LIB_INCDIR_FLAG@ -I$(srcdir)/../../ldmsd
AM_LDFLAGS = @OVIS_LIB_LIBDIR_FLAG@
COMMON_LIBADD = $(CORE)/libldms.la -lovis_util

libhello_stream_store_la_SOURCES = hello_stream_store.c
libhello_stream_store_la_CFLAGS = @SOS_INCDIR_FLAG@ $(AM_CFLAGS) -g -O0
libhello_stream_store_la_LIBADD = -lc $(COMMON_LIBADD) -ljson_util
pkglib_LTLIBRARIES += libhello_stream_store.la
dist_man7_MANS += Plugin_hello_stream_store.man
