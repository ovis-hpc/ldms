pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
check_PROGRAMS =
dist_man7_MANS =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = -lsampler_base libparse_stat.la -lldms @LDFLAGS_GETTIME@ -lovis_util -lcoll

if ENABLE_DSTAT
lib_LTLIBRARIES += libparse_stat.la
libparse_stat_la_SOURCES = parse_stat.c parse_stat.h

libdstat_la_SOURCES = dstat.c
libdstat_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libdstat.la
dist_man7_MANS += Plugin_dstat.man

check_PROGRAMS += parse_stat_test
parse_stat_test_SOURCES = parse_stat.c parse_stat.h
parse_stat_test_CFLAGS = -DMAIN
endif
