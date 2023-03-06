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
libstore_avro_kafka_la_CFLAGS = $(AM_CFLAGS) @LIBAVRO_INCDIR_FLAG@
libstore_avro_kafka_la_LIBADD = $(COMMON_LIBADD) \
			   -lc @LIBAVRO_LIBDIR_FLAG@ \
			   -lrdkafka \
			   -lavro \
			   -lserdes

pkglib_LTLIBRARIES += libstore_avro_kafka.la
