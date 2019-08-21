pkglib_LTLIBRARIES =
dist_man7_MANS=

CORE = ../../core
SAMPLER= ../../sampler
AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) @OVIS_LIB_INCDIR_FLAG@ \
	-I../ -I../../ -I$(srcdir)/../../ldmsd
AM_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@

COMMON_LIBADD = $(CORE)/libldms.la \
		@LDFLAGS_GETTIME@ -lovis_util -lcoll -lpthread

libpapi_sampler_la_SOURCES = papi_sampler.c papi_config.c ../sampler_base.c ../sampler_base.h
libpapi_sampler_la_LIBADD = $(COMMON_LIBADD) -ljson_util -lpapi -lm \
			    ../../ldmsd/libldmsd_stream.la
libpapi_sampler_la_CFLAGS = $(AM_CFLAGS) -DSYSCONFDIR='"$(sysconfdir)"'
pkglib_LTLIBRARIES += libpapi_sampler.la

