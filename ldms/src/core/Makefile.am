if ENABLE_MMAP
MMAP_FLAGS = -DENABLE_MMAP
# do not add -DENABLE_MMAP to libldms_la_CFLAGS until code fixed.
endif

lib_LTLIBRARIES =

AM_CFLAGS = -Wall $(DBGFLAGS) -I$(top_srcdir) @OVIS_LIB_INCDIR_FLAG@ @LIBEVENT_INCDIR_FLAG@
AM_CPPFLAGS = -DSYSCONFDIR='"$(sysconfdir)"' -DPLUGINDIR='"$(libdir)"'
AM_CFLAGS += \
"-DLDMS_SRCDIR=\"$(abs_top_srcdir)\"" \
"-DLDMS_BUILDDIR=\"$(abs_top_builddir)\""

ldmscoreincludedir = $(includedir)/ldms
ldmscoreinclude_HEADERS = ldms.h ldmsd.h ldms_xprt.h ldms_auth.h ldms_xprt_cmd.h

libldms_la_SOURCES = ldms.c ldms_xprt.c ldms_private.h ldms_auth.c
#libldms_la_CFLAGS = @OVIS_LIB_INCDIR_FLAG@
libldms_la_LIBADD = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@ \
			-lmmalloc -lcoll \
			@OPENSSL_LIBS@ \
			-lpthread \
			-ldl -lrt
lib_LTLIBRARIES += libldms.la

ldms_config.h: ../../config.h
	cp ../../config.h ldms_config.h

install-data-hook:
#	cp $(top_builddir)/scripts/ldmsd /etc/init.d/ldmsd
#	cp $(top_builddir)/scripts/schedstatd /etc/init.d/schedstatd
#	cp $(top_builddir)/scripts/meminfod /etc/init.d/meminfod
#	cp $(top_builddir)/scripts/sysclassibd /etc/init.d/procstatutil
