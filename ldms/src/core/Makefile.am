lib_LTLIBRARIES =

sbin_PROGRAMS = ldms_msg_chan_client

AM_LDFLAGS = @OVIS_LIB_ABS@
AM_CPPFLAGS = $(DBGFLAGS) @OVIS_INCLUDE_ABS@

AM_CFLAGS = -DSYSCONFDIR='"$(sysconfdir)"' -DPLUGINDIR='"$(pkglibdir)"'
AM_CFLAGS += \
"-DLDMS_SRCDIR=\"$(abs_top_srcdir)\"" \
"-DLDMS_BUILDDIR=\"$(abs_top_builddir)\""

ldmscoreincludedir = $(includedir)/ldms
ldmscoreinclude_HEADERS = ldms.h ldms_core.h ldms_xprt.h ldms_auth.h \
			  ldms_json.h \
			  ldms_msg_chan.h \
			  kldms_req.h ldms_heap.h rrbt.h

libldms_la_SOURCES = ldms.c ldms_xprt.c ldms_private.h \
		     ldms_auth.c ldms_xprt_auth.c \
		     rrbt.c rrbt.h \
		     ldms_heap.c ldms_heap.h \
		     ldms_rail.c ldms_rail.h \
		     ldms_msg.c ldms_msg.h \
		     ldms_msg_chan.c ldms_msg_chan.h \
		     ldms_qgroup.c ldms_qgroup.h \
		     ldms_json.c ldms_json.h
libldms_la_LIBADD = -ldl -lpthread $(top_builddir)/lib/src/coll/libcoll.la \
		    $(top_builddir)/lib/src/ovis_json/libovis_json.la \
		    $(top_builddir)/lib/src/ovis_event/libovis_event.la \
		    $(top_builddir)/lib/src/mmalloc/libmmalloc.la \
		    $(top_builddir)/lib/src/zap/libzap.la \
		    $(top_builddir)/lib/src/ovis_log/libovis_log.la \
		    @OPENSSL_LIBS@

lib_LTLIBRARIES += libldms.la

ldms_msg_chan_client_SOURCES = ldms_msg_chan_client.c
ldms_msg_chan_client_LDADD = \
	 libldms.la \
	 $(top_builddir)/lib/src/ovis_util/libovis_util.la \
	 $(top_builddir)/lib/src/ovis_log/libovis_log.la \
	 $(top_builddir)/lib/src/ovis_event/libovis_event.la \
	 $(top_builddir)/lib/src/ovis_json/libovis_json.la \
	 $(LOVIS_UTIL) \
	-lpthread \
	@LDFLAGS_GETTIME@

if HAVE_LIBAVRO
if HAVE_LIBSERDES

ldmscoreinclude_HEADERS += ldms_msg_avro_ser.h
libldms_msg_avro_ser_la_SOURCES = ldms_msg_avro_ser.c
libldms_msg_avro_ser_la_LIBADD = libldms.la @LIBAVRO@ @LIBSERDES@
lib_LTLIBRARIES += libldms_msg_avro_ser.la

endif
endif
