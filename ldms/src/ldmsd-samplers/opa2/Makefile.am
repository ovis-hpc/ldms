include ../common.am

pkglib_LTLIBRARIES = libopa2.la

libopa2_la_SOURCES = opa2.c
libopa2_la_CFLAGS  = $(SAMPLER_CFLAGS)
libopa2_la_LIBADD  = $(SAMPLER_LIBADD) -libmad -libumad -lpthread
libopa2_la_LDFLAGS = $(SAMPLER_LDFLAGS)
