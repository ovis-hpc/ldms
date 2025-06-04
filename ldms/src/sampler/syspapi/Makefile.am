include $(top_srcdir)/ldms/rules.mk


pkglib_LTLIBRARIES =
dist_man7_MANS=

EXTRA_DIST = test/ldmsd.sh

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = $(top_builddir)/ldms/src/sampler/libsampler_base.la \
		$(top_builddir)/ldms/src/core/libldms.la \
		@LDFLAGS_GETTIME@ \
		$(top_builddir)/lib/src/ovis_util/libovis_util.la \
		$(top_builddir)/lib/src/coll/libcoll.la

libsyspapi_sampler_la_SOURCES = syspapi_sampler.c
libsyspapi_sampler_la_LIBADD = $(COMMON_LIBADD) $(LTLIBPAPI) $(LTLIBPFM) -lpthread \
			       ../papi/libpapi_hook.la
libsyspapi_sampler_la_CFLAGS = $(AM_CFLAGS) -DSYSCONFDIR='"$(sysconfdir)"'
dist_man7_MANS += ldms-sampler_syspapi_sampler.man
pkglib_LTLIBRARIES += libsyspapi_sampler.la

CLEANFILES = $(dist_man7_MANS)
