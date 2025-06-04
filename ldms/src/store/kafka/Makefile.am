include $(top_srcdir)/ldms/rules.mk


SUBDIRS =
lib_LTLIBRARIES =
pkglib_LTLIBRARIES =
dist_man7_MANS =

AM_LDFLAGS = @OVIS_LIB_ABS@
AM_CFLAGS = @OVIS_INCLUDE_ABS@

COMMON_LIBADD = $(top_builddir)/ldms/src/core/libldms.la \
		$(top_builddir)/lib/src/ovis_util/libovis_util.la \
		$(top_builddir)/lib/src/ovis_json/libovis_json.la

libstore_kafka_la_SOURCES = store_kafka.c
libstore_kafka_la_CFLAGS = $(AM_CFLAGS) -g -O0
libstore_kafka_la_LIBADD = $(COMMON_LIBADD) \
			   -lc @KAFKA_LDFLAGS@

pkglib_LTLIBRARIES += libstore_kafka.la
dist_man7_MANS += ldms-store_kafka.man

CLEANFILES = $(dist_man7_MANS)
