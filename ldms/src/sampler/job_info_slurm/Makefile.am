pkglib_LTLIBRARIES =
dist_man7_MANS=

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = -lldms @LDFLAGS_GETTIME@ -lovis_util -lcoll

libjobinfo_slurm_la_SOURCES = jobinfo_slurm.c
libjobinfo_slurm_la_LIBADD = $(COMMON_LIBADD)
libjobinfo_slurm_la_CFLAGS = $(SLURM_INCDIR_FLAG) \
	-I$(top_srcdir)/ldms/src/sampler/job_info -DSYSCONFDIR='"$(sysconfdir)"'
libjobinfo_slurm_la_LDFLAGS = $(AM_LDFLAGS) -module
pkglib_LTLIBRARIES += libjobinfo_slurm.la
