include ../common.am

pkglib_LTLIBRARIES = libpapi_sampler.la

libpapi_sampler_la_SOURCES = papi_sampler.c papi_sampler.h papi_config.c
libpapi_sampler_la_CFLAGS  = $(SAMPLER_CFLAGS)
libpapi_sampler_la_LIBADD  = $(SAMPLER_LIBADD) \
			     $(top_builddir)/lib/src/json/libjson_util.la \
			     ../../ldmsd/libldmsd_stream.la \
			     -lpapi -lm
libpapi_sampler_la_LDFLAGS = $(SAMPLER_LDFLAGS)
