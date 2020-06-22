lib_LTLIBRARIES =

AM_CFLAGS = $(DBGFLAGS) -I$(top_srcdir) -I.. -I$(top_srcdir)/lib/src
AM_CPPFLAGS = -DSYSCONFDIR='"$(sysconfdir)"' -DPLUGINDIR='"$(pkglibdir)"'
AM_CFLAGS += \
"-DLDMS_SRCDIR=\"$(abs_top_srcdir)\"" \
"-DLDMS_BUILDDIR=\"$(abs_top_builddir)\""

ldmscoreincludedir = $(includedir)/ldms
ldmscoreinclude_HEADERS = ldms.h ldms_core.h ldms_xprt.h ldms_auth.h \
			  kldms_req.h ref.h ldms_grp.h

libldms_la_SOURCES = ldms.c ldms_xprt.c ldms_private.h \
		     ldms_auth.c ldms_xprt_auth.c \
		     ref.h ldms_grp.c
libldms_la_LIBADD = -ldl -lpthread \
		    $(top_builddir)/lib/src/coll/libcoll.la \
		    $(top_builddir)/lib/src/json/libjson_util.la \
		    $(top_builddir)/lib/src/zap/libzap.la \
		    $(top_builddir)/lib/src/zap/libzap.la \
		    $(top_builddir)/lib/src/mmalloc/libmmalloc.la

lib_LTLIBRARIES += libldms.la

install-data-hook:
#	cp $(top_builddir)/scripts/ldmsd /etc/init.d/ldmsd
#	cp $(top_builddir)/scripts/schedstatd /etc/init.d/schedstatd
#	cp $(top_builddir)/scripts/meminfod /etc/init.d/meminfod
#	cp $(top_builddir)/scripts/sysclassibd /etc/init.d/sysclassibd
