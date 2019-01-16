include ../common.am

dist_man7_MANS = Plugin_edac.man

pkglib_LTLIBRARIES = libedac.la

libedac_la_SOURCES = edac.c
libedac_la_CFLAGS = $(SAMPLER_CFLAGS)
libedac_la_LIBADD = $(SAMPLER_LIBADD)
libedac_la_LDFLAGS = $(SAMPLER_LDFLAGS)
