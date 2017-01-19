pkglib_LTLIBRARIES =
dist_man7_MANS=

CORE = ../../core
SAMPLER= ../../sampler
AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) @OVIS_LIB_INCDIR_FLAG@
AM_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@

# common libadd for llnl samplers
COMMON_LIBADD = $(CORE)/libldms.la
		@LDFLAGS_GETTIME@ -lovis_util -lcoll

if ENABLE_JOBID
JOBID_LIBFLAGS = ../libjobid.la
endif

if ENABLE_LLNL_EDAC
libedac_la_SOURCES = edac.c
libedac_la_LIBADD = $(COMMON_LIBADD) $(JOBID_LIBFLAGS)
libedac_la_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/$(SAMPLER)
pkglib_LTLIBRARIES += libedac.la
dist_man7_MANS += Plugin_edac.man
endif
