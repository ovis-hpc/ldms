include ../common.am

pkglib_LTLIBRARIES = libsyspapi_sampler.la

libsyspapi_sampler_la_SOURCES = syspapi_sampler.c
libsyspapi_sampler_la_CFLAGS  = $(SAMPLER_CFLAGS) \
				@LIBPAPI_INCDIR_FLAG@ \
				@LIBPFM_INCDIR_FLAG@
libsyspapi_sampler_la_LIBADD  = $(SAMPLER_LIBADD) \
				-ljson_util -lpapi -lm -lpfm
libsyspapi_sampler_la_LDFLAGS = $(SAMPLER_LDFLAGS) \
				@LIBPAPI_LIB64DIR_FLAG@ @LIBPAPI_LIBDIR_FLAG@ \
				@LIBPFM_LIB64DIR_FLAG@ @LIBPFM_LIBDIR_FLAG@
