pkglib_LTLIBRARIES =
dist_man7_MANS=

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = $(top_builddir)/ldms/src/sampler/libsampler_base.la \
		$(top_builddir)/ldms/src/core/libldms.la \
		$(top_builddir)/ldms/src/ldmsd/libldmsd_stream.la \
		@LDFLAGS_GETTIME@ \
		$(top_builddir)/lib/src/ovis_util/libovis_util.la \
		$(top_builddir)/lib/src/coll/libcoll.la \
		$(top_builddir)/lib/src/ovis_json/libovis_json.la \
		-lpthread

libslurm_sampler_la_SOURCES = slurm_sampler.c slurm_sampler.h
libslurm_sampler_la_LIBADD = $(COMMON_LIBADD)
libslurm_sampler_la_CFLAGS = -DSYSCONFDIR='"$(sysconfdir)"'
pkglib_LTLIBRARIES += libslurm_sampler.la

libslurm_sampler2_la_SOURCES = slurm_sampler2.c
libslurm_sampler2_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libslurm_sampler2.la
