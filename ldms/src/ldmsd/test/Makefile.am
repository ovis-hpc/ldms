sbin_PROGRAMS =
pkglib_LTLIBRARIES =

CORE = ../../core
SAMPLER = ../../sampler
LDMSD = ../../ldmsd

AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) @OVIS_LIB_INCDIR_FLAG@ -I.. -I../../ -I$(srcdir)../../ldmsd -I$(LDMSD)
AM_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@

COMMON_LIBADD = $(CORE)/libldms.la $(LDMSD)/libldmsd_stream.la \
		@LDFLAGS_GETTIME@ -lovis_util -lcoll -lpthread

libtest_stream_sampler_la_SOURCES = test_stream_sampler.c
libtest_stream_sampler_la_LIBADD = $(COMMON_LIBADD) $(CORE)/libldms.la \
        ../libldmsd_stream.la @LDFLAGS_GETTIME@ -lovis_util -lcoll -lpthread -ljson_util
libtest_stream_sampler_la_CFLAGS = $(AM_CFLAGS) -DSYSCONFDIR='"$(sysconfdir)"'
pkglib_LTLIBRARIES += libtest_stream_sampler.la

sbin_PROGRAMS += ldmsd_test_stream
ldmsd_test_stream_SOURCES = ldmsd_test_stream.c
ldmsd_test_stream_CFLAGS = $(AM_CFLAGS) -DLDMSD_TEST_STREAM_CMD
ldmsd_test_stream_LDADD = $(CORE)/libldms.la $(LDMSD)/librequest.la $(LDMSD)/libldmsd_stream.la
ldmsd_test_stream_LDFLAGS = $(AM_LDFLAGS) -ljson_util
