pkglib_LTLIBRARIES =
dist_man7_MANS=

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = -lsampler_base -lldms @LDFLAGS_GETTIME@ -lovis_util -lcoll

libslurm_notifier_la_SOURCES = slurm_notifier.c
libslurm_notifier_la_LIBADD = $(COMMON_LIBADD) -ljson_util \
			      -lldmsd_stream
libslurm_notifier_la_CFLAGS = $(SLURM_INCDIR_FLAG) -DSYSCONFDIR='"$(sysconfdir)"'
# libslurm_notifier_la_LDFLAGS = -module
pkglib_LTLIBRARIES += libslurm_notifier.la

