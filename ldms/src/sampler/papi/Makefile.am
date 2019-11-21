pkglib_LTLIBRARIES =
dist_man7_MANS=

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = -lsampler_base -lldms @LDFLAGS_GETTIME@ -lovis_util -lcoll

libpapi_sampler_la_SOURCES = papi_sampler.h papi_sampler.c papi_config.c
libpapi_sampler_la_LIBADD = $(COMMON_LIBADD) -ljson_util -lpapi -lm \
			    -lldmsd_stream -lpthread
libpapi_sampler_la_CFLAGS = -DSYSCONFDIR='"$(sysconfdir)"'
pkglib_LTLIBRARIES += libpapi_sampler.la

