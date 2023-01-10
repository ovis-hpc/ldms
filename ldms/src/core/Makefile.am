lib_LTLIBRARIES =

AM_LDFLAGS = @OVIS_LIB_ABS@
AM_CPPFLAGS = $(DBGFLAGS) @OVIS_INCLUDE_ABS@

AM_CFLAGS = -DSYSCONFDIR='"$(sysconfdir)"' -DPLUGINDIR='"$(pkglibdir)"'
AM_CFLAGS += \
"-DLDMS_SRCDIR=\"$(abs_top_srcdir)\"" \
"-DLDMS_BUILDDIR=\"$(abs_top_builddir)\""

ldmscoreincludedir = $(includedir)/ldms
ldmscoreinclude_HEADERS = ldms.h ldms_core.h ldms_xprt.h ldms_auth.h \
			  kldms_req.h ldms_heap.h rrbt.h

libldms_la_SOURCES = ldms.c ldms_xprt.c ldms_private.h \
		     ldms_auth.c ldms_xprt_auth.c \
		     rrbt.c rrbt.h \
		     ldms_heap.c ldms_heap.h \
		     ldms_rail.c ldms_rail.h \
		     ldms_stream.c ldms_stream.h
libldms_la_LIBADD = -ldl -lpthread $(top_builddir)/lib/src/coll/libcoll.la \
		    $(top_builddir)/lib/src/ovis_json/libovis_json.la \
		    $(top_builddir)/lib/src/ovis_ev/libovis_ev.la \
		    $(top_builddir)/lib/src/mmalloc/libmmalloc.la \
		    $(top_builddir)/lib/src/zap/libzap.la \
		    $(top_builddir)/lib/src/ovis_log/libovis_log.la \
		    @OPENSSL_LIBS@

lib_LTLIBRARIES += libldms.la
