pkglib_LTLIBRARIES =
dist_man7_MANS=

CORE = ../../core
SAMPLER= ../../sampler
AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) @OVIS_LIB_INCDIR_FLAG@ \
	-I../ -I../../ -I$(srcdir)/../../ldmsd
AM_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@

COMMON_LIBADD = $(CORE)/libldms.la ../../ldmsd/libldmsd_stream.la \
		@LDFLAGS_GETTIME@ -lovis_util -lcoll -lpthread

libslurm_sampler_la_SOURCES = slurm_sampler.c slurm_sampler.h ../sampler_base.c
libslurm_sampler_la_LIBADD = $(COMMON_LIBADD) $(top_srcdir)/src/json/libjson_util.la
libslurm_sampler_la_CFLAGS = $(AM_CFLAGS) -DSYSCONFDIR='"$(sysconfdir)"'
pkglib_LTLIBRARIES += libslurm_sampler.la

