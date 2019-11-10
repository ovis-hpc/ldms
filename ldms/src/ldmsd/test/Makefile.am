sbin_PROGRAMS =
pkglib_LTLIBRARIES =

CORE = ../../core
SAMPLER = ../../sampler
LDMSD = ../../ldmsd

AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) @OVIS_LIB_INCDIR_FLAG@ -I.. -I../../ -I$(srcdir)../../ldmsd -I$(LDMSD)
AM_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@

COMMON_LIBADD = $(CORE)/libldms.la $(LDMSD)/libldmsd_stream.la \
		@LDFLAGS_GETTIME@ -lovis_util -lcoll -lpthread

sbin_PROGRAMS += ldmsd_stream_publish
ldmsd_stream_publish_SOURCES = ldmsd_stream_publish.c
ldmsd_stream_publish_CFLAGS = $(AM_CFLAGS)
ldmsd_stream_publish_LDADD = $(CORE)/libldms.la $(LDMSD)/librequest.la $(LDMSD)/libldmsd_stream.la
ldmsd_stream_publish_LDFLAGS = $(AM_LDFLAGS) $(top_srcdir)/src/json/libjson_util.la -lovis_util

sbin_PROGRAMS += ldmsd_stream_subscribe
ldmsd_stream_subscribe_SOURCES = ldmsd_stream_subscribe.c
ldmsd_stream_subscribe_CFLAGS = $(AM_CFLAGS)
ldmsd_stream_subscribe_LDADD = $(CORE)/libldms.la $(LDMSD)/librequest.la $(LDMSD)/libldmsd_stream.la
ldmsd_stream_subscribe_LDFLAGS = $(AM_LDFLAGS) $(top_srcdir)/src/json/libjson_util.la -pthread -lovis_util
