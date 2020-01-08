pkglib_LTLIBRARIES =
dist_man7_MANS=

CORE = ../../core
SAMPLER= ../../sampler
AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) @OVIS_LIB_INCDIR_FLAG@ \
	-I../ -I../../ -I$(srcdir)/../../ldmsd
AM_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@

COMMON_LIBADD = $(CORE)/libldms.la ../../ldmsd/libldmsd_stream.la \
		@LDFLAGS_GETTIME@ -lovis_util -lcoll -lpthread

libhello_sampler_la_SOURCES = hello_sampler.c hello_sampler.h ../sampler_base.c
libhello_sampler_la_LIBADD = $(COMMON_LIBADD) -ljson_util
libhello_sampler_la_CFLAGS = $(AM_CFLAGS) -DSYSCONFDIR='"$(sysconfdir)"'
pkglib_LTLIBRARIES += libhello_sampler.la

sbin_PROGRAMS = hello_publisher
hello_publisher_SOURCES = hello_publisher.c
hello_publisher_LDADD = $(COMMON_LIBADD) -ljson_util \
			      ../../ldmsd/libldmsd_stream.la
hello_publisher_CFLAGS = $(AM_CFLAGS)
	-DSYSCONFDIR='"$(sysconfdir)"'

