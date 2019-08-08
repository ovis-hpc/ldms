pkglib_LTLIBRARIES =
dist_man7_MANS=

CORE = ../../core
SAMPLER= ../../sampler
AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) @OVIS_LIB_INCDIR_FLAG@ \
	    @LIBPAPI_INCDIR_FLAG@ \
	    @LIBPFM_INCDIR_FLAG@ \
	    -I../.. -I$(srcdir)/../../ldmsd
AM_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@ \
	     @LIBPAPI_LIB64DIR_FLAG@ @LIBPAPI_LIBDIR_FLAG@ \
	     @LIBPFM_LIB64DIR_FLAG@ @LIBPFM_LIBDIR_FLAG@

COMMON_LIBADD = $(CORE)/libldms.la \
		@LDFLAGS_GETTIME@ -lovis_util -lcoll -lpthread

libsyspapi_la_SOURCES = syspapi.c ../sampler_base.c ../sampler_base.h
libsyspapi_la_LIBADD = $(COMMON_LIBADD) -lpapi -lpfm
libsyspapi_la_CFLAGS = $(AM_CFLAGS) -DSYSCONFDIR='"$(sysconfdir)"'
pkglib_LTLIBRARIES += libsyspapi.la
