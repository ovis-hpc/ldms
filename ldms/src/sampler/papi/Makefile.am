pkglib_LTLIBRARIES =
dist_man7_MANS=

CORE = ../../core
LDMSD = ../../ldmsd
SAMPLER= ../../sampler
AM_CFLAGS = @LIBPAPI_INCDIR_FLAG@ \
	-I$(srcdir)/$(CORE) -I$(top_srcdir) @OVIS_LIB_INCDIR_FLAG@ \
	-I../ -I../../ -I$(srcdir)/../../ldmsd
AM_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@ @LIBPAPI_LIB64DIR_FLAG@

COMMON_LIBADD = $(CORE)/libldms.la $(LDMSD)/libldmsd_stream.la \
		@LDFLAGS_GETTIME@ -lovis_util -lcoll -lpthread

libpapi_sampler_la_SOURCES = papi_sampler.c papi_config.c ../sampler_base.c ../sampler_base.h
libpapi_sampler_la_LIBADD = $(COMMON_LIBADD) -ljson_util -lpapi -lm
libpapi_sampler_la_CFLAGS = $(AM_CFLAGS) -DSYSCONFDIR='"$(sysconfdir)"'
pkglib_LTLIBRARIES += libpapi_sampler.la

