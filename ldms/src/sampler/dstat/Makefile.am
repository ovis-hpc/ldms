include $(top_srcdir)/ldms/rules.mk


pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
check_PROGRAMS =
bin_PROGRAMS =
dist_man7_MANS =
dist_man1_MANS =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = $(top_builddir)/ldms/src/sampler/libsampler_base.la \
		$(top_builddir)/ldms/src/ldmsd/libldmsd_plugattr.la \
		libparse_stat.la \
		$(top_builddir)/ldms/src/core/libldms.la \
		@LDFLAGS_GETTIME@ \
		$(top_builddir)/lib/src/ovis_util/libovis_util.la \
		$(top_builddir)/lib/src/coll/libcoll.la

if ENABLE_DSTAT
lib_LTLIBRARIES += libparse_stat.la
libparse_stat_la_SOURCES = parse_stat.c parse_stat.h

libdstat_la_SOURCES = dstat.c
libdstat_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libdstat.la
dist_man7_MANS += ldms-sampler_dstat.man

bin_PROGRAMS += ldms_dstat_schema_name
dist_man1_MANS += ldms_dstat_schema_name.man
ldms_dstat_schema_name_SOURCES = dstat.c
ldms_dstat_schema_name_CFLAGS = -DMAIN
ldms_dstat_schema_name_LDADD = \
	$(top_builddir)/lib/src/ovis_util/libovis_util.la \
	$(top_builddir)/lib/src/ovis_log/libovis_log.la

check_PROGRAMS += parse_stat_test
parse_stat_test_SOURCES = parse_stat.c parse_stat.h
parse_stat_test_CFLAGS = -DMAIN
endif

CLEANFILES = $(dist_man1_MANS) $(dist_man7_MANS)
