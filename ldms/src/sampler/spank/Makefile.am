include $(top_srcdir)/ldms/rules.mk


pkglib_LTLIBRARIES =
dist_man7_MANS=

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = $(top_builddir)/ldms/src/sampler/libsampler_base.la \
		$(top_builddir)/ldms/src/core/libldms.la \
		@LDFLAGS_GETTIME@ \
		$(top_builddir)/lib/src/ovis_util/libovis_util.la \
		$(top_builddir)/lib/src/coll/libcoll.la \
		$(top_builddir)/lib/src/ovis_json/libovis_json.la

libslurm_notifier_la_SOURCES = slurm_notifier.c
libslurm_notifier_la_LIBADD = $(COMMON_LIBADD)
libslurm_notifier_la_CFLAGS = $(SLURM_CFLAGS) -DSYSCONFDIR='"$(sysconfdir)"'
dist_man7_MANS += ldms-sampler_slurm_notifier.man
pkglib_LTLIBRARIES += libslurm_notifier.la

CLEANFILES = $(dist_man7_MANS)
