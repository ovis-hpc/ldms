include ../common.am

pkglib_LTLIBRARIES = libgeneric_sampler.la

libgeneric_sampler_la_SOURCES = generic_sampler.c
libgeneric_sampler_la_CFLAGS = $(SAMPLER_CFLAGS)
libgeneric_sampler_la_LIBADD = $(SAMPLER_LIBADD) \
			       $(top_builddir)/lib/src/coll/libcoll.la
libgeneric_sampler_la_LDFLAGS = $(SAMPLER_LDFLAGS)
