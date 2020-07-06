pkglib_LTLIBRARIES =
dist_man7_MANS=

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@ @LIBPAPI_INCDIR_FLAG@
AM_LDFLAGS = @OVIS_LIB_ABS@ @LIBPAPI_LIB64DIR_FLAG@ @LIBPAPI_LIBDIR_FLAG@
COMMON_LIBADD = $(top_builddir)/ldms/src/sampler/libsampler_base.la \
		$(top_builddir)/ldms/src/core/libldms.la \
		$(top_builddir)/ldms/src/ldmsd/libldmsd_stream.la \
		@LDFLAGS_GETTIME@ \
		$(top_builddir)/lib/src/ovis_util/libovis_util.la \
		$(top_builddir)/lib/src/coll/libcoll.la \
		$(top_builddir)/lib/src/ovis_json/libovis_json.la \
		-lpapi -lm -lpthread

libpapi_sampler_la_SOURCES = papi_sampler.h papi_sampler.c papi_config.c
libpapi_sampler_la_LIBADD = $(COMMON_LIBADD)
libpapi_sampler_la_CFLAGS = -DSYSCONFDIR='"$(sysconfdir)"'
pkglib_LTLIBRARIES += libpapi_sampler.la

