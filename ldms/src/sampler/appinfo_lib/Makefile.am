pkglib_LTLIBRARIES =

CORE = ../../core
SAMPLER= ../../sampler
AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) @OVIS_LIB_INCDIR_FLAG@ \
	    -I../../ -I$(srcdir)/../../ldmsd
AM_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@

COMMON_LIBADD = $(CORE)/libldms.la \
		@LDFLAGS_GETTIME@ -lpthread -lovis_util -lcoll \
		../libsampler_base.la

libappinfocl_la_SOURCES = ldms_appinfo.c ldms_appinfo.h ldms_appinfo_shm.h
libappinfocl_la_LIBADD = $(COMMON_LIBADD)
libappinfocl_la_CFLAGS = $(AM_CFLAGS) 
pkglib_LTLIBRARIES += libappinfocl.la
