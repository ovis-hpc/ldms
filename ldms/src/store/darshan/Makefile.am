include $(top_srcdir)/ldms/rules.mk


SUBDIRS =
lib_LTLIBRARIES =
pkglib_LTLIBRARIES =
dist_man7_MANS =
CORE = ../../core

AM_LDFLAGS = @OVIS_LIB_ABS@
AM_CPPFLAGS = @OVIS_INCLUDE_ABS@

CFLAGS := $(filter-out -Werror, ${CFLAGS})

COMMON_LIBADD = $(top_builddir)/ldms/src/core/libldms.la \
		$(top_builddir)/lib/src/ovis_util/libovis_util.la \
		$(top_builddir)/lib/src/ovis_json/libovis_json.la

libdarshan_stream_store_la_SOURCES = darshan_stream_store.c
libdarshan_stream_store_la_CFLAGS = @SOS_INCDIR_FLAG@ $(AM_CFLAGS) -g -O0
libdarshan_stream_store_la_LIBADD = $(COMMON_LIBADD) \
	@SOS_LIB64DIR_FLAG@ @SOS_LIBDIR_FLAG@ -lsos -lc

pkglib_LTLIBRARIES += libdarshan_stream_store.la
dist_man7_MANS += ldms-store_darshan_stream_store.man

CLEANFILES = $(dist_man7_MANS)
