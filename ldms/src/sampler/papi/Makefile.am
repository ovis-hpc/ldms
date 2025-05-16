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
		$(top_builddir)/lib/src/ovis_log/libovis_log.la \
		-lm -lpthread

libpapi_hook_la_SOURCES = papi_hook.c papi_hook.h
pkglib_LTLIBRARIES += libpapi_hook.la

libpapi_sampler_la_SOURCES = papi_sampler.h papi_sampler.c papi_config.c
libpapi_sampler_la_LIBADD = $(COMMON_LIBADD) $(LTLIBPAPI) libpapi_hook.la
libpapi_sampler_la_CFLAGS = -DSYSCONFDIR='"$(sysconfdir)"'
pkglib_LTLIBRARIES += libpapi_sampler.la
dist_man7_MANS += ldms-sampler_papi_sampler.man
