pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
check_PROGRAMS =
dist_man7_MANS =

CORE = ../../core
SAMPLER= ../../sampler
AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) -I../.. @OVIS_LIB_INCDIR_FLAG@ \
	    -I$(srcdir)/../../ldmsd
AM_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@

BASE_LIBADD = ../libsampler_base.la
COMMON_LIBADD = $(CORE)/libldms.la libparse_stat.la \
	@LDFLAGS_GETTIME@ -lovis_util -lcoll

if ENABLE_DSTAT
lib_LTLIBRARIES += libparse_stat.la
libparse_stat_la_SOURCES = parse_stat.c parse_stat.h

libdstat_la_SOURCES = dstat.c
libdstat_la_LIBADD = $(BASE_LIBADD) $(COMMON_LIBADD)
libdstat_la_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/$(SAMPLER)
pkglib_LTLIBRARIES += libdstat.la
dist_man7_MANS += Plugin_dstat.man

check_PROGRAMS += parse_stat_test
parse_stat_test_SOURCES = parse_stat.c parse_stat.h
parse_stat_test_CFLAGS = -DMAIN
endif
