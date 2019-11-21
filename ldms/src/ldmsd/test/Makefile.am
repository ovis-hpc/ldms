sbin_PROGRAMS =
pkglib_LTLIBRARIES =

AM_LDFLAGS = @OVIS_LIB_ABS@
AM_CPPFLAGS = @OVIS_INCLUDE_ABS@

sbin_PROGRAMS += ldmsd_stream_publish
ldmsd_stream_publish_SOURCES = ldmsd_stream_publish.c
ldmsd_stream_publish_LDADD = -lldms -lldmsd_request -lldmsd_stream -ljson_util -lovis_util

sbin_PROGRAMS += ldmsd_stream_subscribe
ldmsd_stream_subscribe_SOURCES = ldmsd_stream_subscribe.c
ldmsd_stream_subscribe_LDADD = -lldms -lldmsd_request -lldmsd_stream -ljson_util -lovis_util
ldmsd_stream_subscribe_LDFLAGS = $(AM_LDFLAGS) -pthread 
