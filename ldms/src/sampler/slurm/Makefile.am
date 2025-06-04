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
		$(top_builddir)/lib/src/ovis_json/libovis_json.la \
		-lpthread

libslurm_sampler_la_SOURCES = slurm_sampler.c slurm_sampler.h
libslurm_sampler_la_LIBADD = $(COMMON_LIBADD)
libslurm_sampler_la_CFLAGS = -DSYSCONFDIR='"$(sysconfdir)"'
dist_man7_MANS += ldms-sampler_slurm_sampler.man
pkglib_LTLIBRARIES += libslurm_sampler.la

libslurm_sampler2_la_SOURCES = slurm_sampler2.c
libslurm_sampler2_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libslurm_sampler2.la

CLEANFILES = $(dist_man7_MANS)
