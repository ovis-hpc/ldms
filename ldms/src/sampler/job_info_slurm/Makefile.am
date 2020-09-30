pkglib_LTLIBRARIES =
dist_man7_MANS=

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = $(top_builddir)/ldms/src/core/libldms.la \
		@LDFLAGS_GETTIME@ \
		$(top_builddir)/lib/src/ovis_util/libovis_util.la \
		$(top_builddir)/lib/src/coll/libcoll.la

libjobinfo_slurm_la_SOURCES = jobinfo_slurm.c
libjobinfo_slurm_la_LIBADD = $(COMMON_LIBADD)
libjobinfo_slurm_la_CFLAGS = $(SLURM_CFLAGS) \
	-I$(top_srcdir)/ldms/src/sampler/job_info -DSYSCONFDIR='"$(sysconfdir)"'
libjobinfo_slurm_la_LDFLAGS = $(AM_LDFLAGS) -module
pkglib_LTLIBRARIES += libjobinfo_slurm.la
