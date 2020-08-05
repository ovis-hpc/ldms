include ../common.am

EXTRA_DIST = ldmsd_app_sampler.md

pkglib_LTLIBRARIES = libapp_sampler.la

libapp_sampler_la_SOURCES = app_sampler.c
libapp_sampler_la_CFLAGS  = $(SAMPLER_CFLAGS)
libapp_sampler_la_LIBADD  = $(SAMPLER_LIBADD) \
			    ../../ldmsd/libldmsd_stream.la
libapp_sampler_la_LDFLAGS = $(SAMPLER_LDFLAGS)
