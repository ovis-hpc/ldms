pkglib_LTLIBRARIES =
dist_man7_MANS=

EXTRA_DIST = test/ldmsd.sh

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@ @LIBPAPI_INCDIR_FLAG@ @LIBPFM_INCDIR_FLAG@ 
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = $(top_builddir)/ldms/src/sampler/libsampler_base.la \
		$(top_builddir)/ldms/src/core/libldms.la \
		@LDFLAGS_GETTIME@ \
		$(top_builddir)/lib/src/ovis_util/libovis_util.la \
		$(top_builddir)/lib/src/coll/libcoll.la

AM_LDFLAGS += @LIBPAPI_LIB64DIR_FLAG@ @LIBPAPI_LIBDIR_FLAG@ \
	     @LIBPFM_LIB64DIR_FLAG@ @LIBPFM_LIBDIR_FLAG@

libsyspapi_sampler_la_SOURCES = syspapi_sampler.c
libsyspapi_sampler_la_LIBADD = $(COMMON_LIBADD) -lpapi -lpfm -lpthread
libsyspapi_sampler_la_CFLAGS = $(AM_CFLAGS) -DSYSCONFDIR='"$(sysconfdir)"'
pkglib_LTLIBRARIES += libsyspapi_sampler.la
