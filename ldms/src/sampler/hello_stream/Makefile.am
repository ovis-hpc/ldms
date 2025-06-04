include $(top_srcdir)/ldms/rules.mk


pkglib_LTLIBRARIES =
dist_man7_MANS=

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = $(top_builddir)/lib/src/ovis_util/libovis_util.la \
		$(top_builddir)/lib/src/coll/libcoll.la \
		$(top_builddir)/ldms/src/core/libldms.la \
		@LDFLAGS_GETTIME@

if ENABLE_HELLO_STREAM

libhello_sampler_la_SOURCES = hello_sampler.c
libhello_sampler_la_LIBADD = $(COMMON_LIBADD) \
			     $(top_builddir)/lib/src/ovis_json/libovis_json.la \
			     $(top_builddir)/ldms/src/sampler/libsampler_base.la
pkglib_LTLIBRARIES += libhello_sampler.la
dist_man7_MANS += ldms-sampler_hello_sampler.man


SUBDIRS = stream_configs

docversionexamplesdir = $(docdir)/examples/hello_sampler_util

endif

CLEANFILES = $(dist_man7_MANS)
