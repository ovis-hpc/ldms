include ../common.am

lib_LTLIBRARIES = libparse_stat.la
libparse_stat_la_SOURCES = parse_stat.c parse_stat.h

check_PROGRAMS = parse_stat_test
parse_stat_test_SOURCES = parse_stat.c parse_stat.h
parse_stat_test_CFLAGS = -DMAIN

pkglib_LTLIBRARIES = libdstat.la
libdstat_la_SOURCES = dstat.c
libdstat_la_CFLAGS  = $(SAMPLER_CFLAGS)
libdstat_la_LIBADD  = $(SAMPLER_LIBADD) \
		      libparse_stat.la \
		      $(top_builddir)/lib/src/mmalloc/libmmalloc.la \
		      $(top_builddir)/lib/src/ovis_util/libovis_util.la \
		      $(top_builddir)/lib/src/coll/libcoll.la
libdstat_la_LDFLAGS = $(SAMPLER_LDFLAGS) @LDFLAGS_GETTIME@

dist_man7_MANS = Plugin_dstat.man
