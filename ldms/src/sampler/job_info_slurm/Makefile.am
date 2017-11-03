pkglib_LTLIBRARIES =
dist_man7_MANS=

CORE = ../../core
AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) @OVIS_LIB_INCDIR_FLAG@
AM_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@

COMMON_LIBADD = $(CORE)/libldms.la
		@LDFLAGS_GETTIME@ -lovis_util -lcoll


libjobinfo_slurm_la_SOURCES = jobinfo_slurm.c ../job_info/jobinfo.h
libjobinfo_slurm_la_LIBADD = $(COMMON_LIBADD)
libjobinfo_slurm_la_CFLAGS = $(AM_CFLAGS) -I$(SLURM_INCDIR) \
	-I$(top_srcdir)/src/sampler/job_info -DSYSCONFDIR='"$(sysconfdir)"'
pkglib_LTLIBRARIES += libjobinfo_slurm.la
