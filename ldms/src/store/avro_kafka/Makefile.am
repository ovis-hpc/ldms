include $(top_srcdir)/ldms/rules.mk


SUBDIRS =
lib_LTLIBRARIES =
pkglib_LTLIBRARIES =

AM_LDFLAGS = @OVIS_LIB_ABS@
AM_CFLAGS = @OVIS_INCLUDE_ABS@

COMMON_LIBADD = $(top_builddir)/ldms/src/core/libldms.la \
		$(top_builddir)/lib/src/ovis_util/libovis_util.la \
		$(top_builddir)/lib/src/ovis_json/libovis_json.la \
		$(top_builddir)/lib/src/ovis_log/libovis_log.la

libstore_avro_kafka_la_SOURCES = store_avro_kafka.c
libstore_avro_kafka_la_CFLAGS = $(AM_CFLAGS)
libstore_avro_kafka_la_LIBADD = $(COMMON_LIBADD) \
			   $(LTLIBRDKAFKA) \
			   $(LTLIBAVRO) \
			   $(LTLIBSERDES)

pkglib_LTLIBRARIES += libstore_avro_kafka.la

dist_man7_MANS = ldms-store_avro_kafka.man

CLEANFILES = $(dist_man7_MANS)
