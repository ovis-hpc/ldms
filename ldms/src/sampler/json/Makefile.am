include $(top_srcdir)/ldms/rules.mk


pkglib_LTLIBRARIES =
dist_man7_MANS=

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = $(top_builddir)/ldms/src/sampler/libsampler_base.la \
		$(top_builddir)/ldms/src/core/libldms.la \
		$(top_builddir)/ldms/src/ldmsd/libldmsd_stream.la \
		@LDFLAGS_GETTIME@ \
		$(top_builddir)/lib/src/ovis_util/libovis_util.la \
		$(top_builddir)/lib/src/coll/libcoll.la \
		$(top_builddir)/lib/src/ovis_json/libovis_json.la \
		-lpthread

dist_man7_MANS += ldms-sampler_json_stream_sampler.man
libjson_stream_sampler_la_SOURCES = json_stream_sampler.c
libjson_stream_sampler_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libjson_stream_sampler.la

CLEANFILES = $(dist_man7_MANS)
