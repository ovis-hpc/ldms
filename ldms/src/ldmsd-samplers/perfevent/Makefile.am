include ../common.am

pkglib_LTLIBRARIES = libperfevent.la

libperfevent_la_SOURCES = perfevent.c
libperfevent_la_CFLAGS  = $(SAMPLER_CFLAGS)
libperfevent_la_LIBADD  = $(SAMPLER_LIBADD)
libperfevent_la_LDFLAGS = $(SAMPLER_LDFLAGS) -lm
