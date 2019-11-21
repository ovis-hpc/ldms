pkglib_LTLIBRARIES =
dist_man7_MANS=

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = -lsampler_base -lldms -lldmsd_stream @LDFLAGS_GETTIME@ -lovis_util -lcoll -lpthread

libslurm_sampler_la_SOURCES = slurm_sampler.c slurm_sampler.h
libslurm_sampler_la_LIBADD = $(COMMON_LIBADD) -ljson_util
libslurm_sampler_la_CFLAGS = -DSYSCONFDIR='"$(sysconfdir)"'
pkglib_LTLIBRARIES += libslurm_sampler.la

