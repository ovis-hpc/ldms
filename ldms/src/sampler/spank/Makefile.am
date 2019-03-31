pkglib_LTLIBRARIES =
dist_man7_MANS=

CORE = ../../core
LDMSD = ../../ldmsd
AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_builddir)/src @OVIS_LIB_INCDIR_FLAG@
AM_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@

COMMON_LIBADD = $(CORE)/libldms.la $(LDMSD)/libldmsd_stream.la \
		@LDFLAGS_GETTIME@ -lovis_util -lcoll

libslurm_notifier_la_SOURCES = slurm_notifier.c
libslurm_notifier_la_LIBADD = $(COMMON_LIBADD) -ljson_util
libslurm_notifier_la_CFLAGS = $(AM_CFLAGS) $(SLURM_INCDIR_FLAG) \
	-DSYSCONFDIR='"$(sysconfdir)"'
# libslurm_notifier_la_LDFLAGS = -module
pkglib_LTLIBRARIES += libslurm_notifier.la

