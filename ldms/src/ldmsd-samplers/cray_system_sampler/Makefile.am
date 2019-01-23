include ../common.am

pkglib_LTLIBRARIES = libdvs_sampler.la

libdvs_sampler_la_SOURCES = dvs_sampler.c
libdvs_sampler_la_CFLAGS = $(SAMPLER_CFLAGS)
libdvs_sampler_la_LIBADD = $(SAMPLER_LIBADD)
libdvs_sampler_la_LDFLAGS = $(SAMPLER_LDFLAGS) @LDFLAGS_GETTIME@
