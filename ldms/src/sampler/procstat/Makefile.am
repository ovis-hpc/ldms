include $(top_srcdir)/ldms/rules.mk


pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
dist_man7_MANS =
dist_man1_MANS =


AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = $(top_builddir)/ldms/src/sampler/libsampler_base.la \
		$(top_builddir)/ldms/src/core/libldms.la \
		@LDFLAGS_GETTIME@ \
		$(top_builddir)/lib/src/ovis_util/libovis_util.la \
		$(top_builddir)/lib/src/coll/libcoll.la

if ENABLE_PROCSTAT
libprocstat_la_SOURCES = procstat.c
libprocstat_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libprocstat.la
dist_man7_MANS += ldms-sampler_procstat.man
endif

CLEANFILES = $(dist_man7_MANS)
