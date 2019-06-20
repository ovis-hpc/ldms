include ../common.am

pkglib_LTLIBRARIES =

libappinfocl_la_SOURCES = ldms_appinfo.c ldms_appinfo.h ldms_appinfo_shm.h
libappinfocl_la_CFLAGS = -I$(srcdir)/../../core \
			 -I$(top_srcdir) @OVIS_LIB_INCDIR_FLAG@ \
			 -I../../ -I$(srcdir)/../../ldmsd
libappinfocl_la_LIBADD = ../../core/libldms.la \
			 @LDFLAGS_GETTIME@ -lpthread -lovis_util -lcoll
libappinfocl_la_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@
pkglib_LTLIBRARIES += libappinfocl.la

libappinfo_la_SOURCES = appinfo.c
libappinfo_la_CFLAGS  = $(SAMPLER_CFLAGS)
libappinfo_la_LIBADD  = $(SAMPLER_LIBADD)
libappinfo_la_LDFLAGS = $(SAMPLER_LDFLAGS)
pkglib_LTLIBRARIES += libappinfo.la
