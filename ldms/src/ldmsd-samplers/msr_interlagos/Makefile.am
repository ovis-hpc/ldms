
include ../common.am

pkglib_LTLIBRARIES = libmsr_interlagos.la

libmsr_interlagos_la_SOURCES = msr_interlagos.c
libmsr_interlagos_la_CFLAGS  = $(SAMPLER_CFLAGS) -pthread
libmsr_interlagos_la_LIBADD  = $(SAMPLER_LIBADD)
libmsr_interlagos_la_LDFLAGS = $(SAMPLER_LDFLAGS)
