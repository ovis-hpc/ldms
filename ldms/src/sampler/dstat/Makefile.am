pkglib_LTLIBRARIES =
lib_LTLIBRARIES = libparse_stat.la
check_PROGRAMS =
dist_man7_MANS =

CORE = ../../core
SAMPLER= ../../sampler
AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) -I../.. @OVIS_LIB_INCDIR_FLAG@
AM_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@

libparse_stat_la_SOURCES = parse_stat.c parse_stat.h

if ENABLE_JOBID
JOBID_LIBFLAGS = ../libjobid.la
AM_CFLAGS += -DENABLE_JOBID
endif

COMMON_LIBADD = $(CORE)/libldms.la libparse_stat.la \
	@LDFLAGS_GETTIME@ -lovis_util -lcoll \
	$(JOBID_LIBFLAGS)

if ENABLE_PROCSTAT
libdstat_la_SOURCES = dstat.c dstat.h
libdstat_la_LIBADD = $(COMMON_LIBADD)
libdstat_la_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/$(SAMPLER)
pkglib_LTLIBRARIES += libdstat.la
dist_man7_MANS += Plugin_dstat.man

check_PROGRAMS += parse_stat_test
parse_stat_test_SOURCES: parse_stat.c parse_stat.h
parse_stat_test_CFLAGS = -DMAIN
endif
