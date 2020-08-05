sbin_PROGRAMS =
pkglib_LTLIBRARIES =

CORE = ../../core
LDMSD = ../../ldmsd

AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) \
	    -I$(top_srcdir)/lib/src \
	    -I.. -I../../ -I$(srcdir)../../ldmsd -I$(LDMSD)

sbin_PROGRAMS += ldmsd_stream_publish
ldmsd_stream_publish_SOURCES = ldmsd_stream_publish.c
ldmsd_stream_publish_CFLAGS = $(AM_CFLAGS)
ldmsd_stream_publish_LDADD = $(CORE)/libldms.la $(LDMSD)/librequest.la $(LDMSD)/libldmsd_stream.la
ldmsd_stream_publish_LDFLAGS = $(AM_LDFLAGS)  \
			       $(top_builddir)/lib/src/json/libjson_util.la \
			       $(top_builddir)/lib/src/ovis_util/libovis_util.la

sbin_PROGRAMS += ldmsd_stream_subscribe
ldmsd_stream_subscribe_SOURCES = ldmsd_stream_subscribe.c
ldmsd_stream_subscribe_CFLAGS = $(AM_CFLAGS)
ldmsd_stream_subscribe_LDADD = $(CORE)/libldms.la $(LDMSD)/librequest.la $(LDMSD)/libldmsd_stream.la
ldmsd_stream_subscribe_LDFLAGS = $(AM_LDFLAGS) -pthread \
				 $(top_builddir)/lib/src/json/libjson_util.la \
				 $(top_builddir)/lib/src/ovis_util/libovis_util.la
